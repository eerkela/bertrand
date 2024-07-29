#ifndef BERTRAND_PYTHON_COMMON_OBJECT_H
#define BERTRAND_PYTHON_COMMON_OBJECT_H

#include "declarations.h"
#include "except.h"
#include "ops.h"


namespace py {


//////////////////////
////    HANDLE    ////
//////////////////////


/* A non-owning reference to a raw Python object. */
class Handle : public impl::BertrandTag {
protected:
    PyObject* m_ptr;

    friend inline PyObject* ptr(Handle handle);
    friend inline PyObject* release(Handle handle);

public:

    Handle() = default;
    Handle(PyObject* ptr) : m_ptr(ptr) {}

    /* Check for exact pointer identity. */
    [[nodiscard]] bool is(Handle other) const {
        return m_ptr == ptr(other);
    }

    /* Contains operator.  Equivalent to Python's `in` keyword, but with reversed
    operands (i.e. `x in y` -> `y.contains(x)`).  This is consistent with other STL
    container types, and the allowable key types can be specified via the __contains__
    control struct. */
    template <typename Self, typename Key> requires (__contains__<Self, Key>::enable)
    [[nodiscard]] bool contains(this const Self& self, const Key& key) {
        using Return = typename __contains__<Self, Key>::type;
        static_assert(
            std::same_as<Return, bool>,
            "contains() operator must return a boolean value.  Check your "
            "specialization of __contains__ for these types and ensure the Return "
            "type is set to bool."
        );
        if constexpr (impl::proxy_like<Key>) {
            return self.contains(key.value());
        } else if constexpr (impl::has_call_operator<__contains__<Self, Key>>) {
            return __contains__<Self, Key>{}(self, key);
        } else {
            int result = PySequence_Contains(
                self.m_ptr,
                ptr(as_object(key))
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    }

    /* Contextually convert an Object into a boolean value for use in if/else 
    statements, with the same semantics as in Python. */
    [[nodiscard]] explicit operator bool() const {
        int result = PyObject_IsTrue(m_ptr);
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    /* Universal implicit conversion operator.  Implemented via the __cast__ control
    struct. */
    template <typename Self, typename T>
        requires (__cast__<Self, T>::enable)
    [[nodiscard]] operator T(this const Self& self) {
        return __cast__<Self, T>{}(self);
    }

    /* Universal explicit conversion operator.  Implemented via the __explicit_cast__
    control struct. */
    template <typename Self, typename T>
        requires (!__cast__<Self, T>::enable && __explicit_cast__<Self, T>::enable)
    [[nodiscard]] explicit operator T(this const Self& self) {
        return __explicit_cast__<Self, T>{}(self);
    }

    /* Call operator.  This can be enabled for specific argument signatures and return
    types via the __call__ control struct, enabling static type safety for Python
    functions in C++. */
    template <typename Self, typename... Args>
        requires (__call__<std::remove_cvref_t<Self>, Args...>::enable)
    auto operator()(this Self&& self, Args&&... args) {
        using call = __call__<std::remove_cvref_t<Self>, Args...>;
        using Return = typename call::type;
        static_assert(
            std::is_void_v<Return> || std::derived_from<Return, Object>,
            "Call operator must return either void or a py::Object subclass.  "
            "Check your specialization of __call__ for the given arguments and "
            "ensure that it is derived from py::Object."
        );
        if constexpr (impl::has_call_operator<call>) {
            return call{}(std::forward<Self>(self), std::forward<Args>(args)...);
        } else if constexpr (std::is_void_v<Return>) {
            Function<Return(Args...)>::template invoke_py<Return>(
                ptr(self),
                std::forward<Args>(args)...
            );
        } else {
            return Function<Return(Args...)>::template invoke_py<Return>(
                ptr(self),
                std::forward<Args>(args)...
            );
        }
    }

    /* Index operator.  Specific key and element types can be controlled via the
    __getitem__, __setitem__, and __delitem__ control structs. */
    template <typename Self, typename Key> requires (__getitem__<Self, Key>::enable)
    auto operator[](this const Self& self, const Key& key) {
        using Return = typename __getitem__<Self, Key>::type;
        if constexpr (impl::proxy_like<Key>) {
            return self[key.value()];
        } else {
            return impl::Item<Self, Key>(self, key);
        }
    }

    /* Slice operator.  This is just syntactic sugar for the index operator with a
    py::Slice operand, allowing users to specify slices using a condensed initializer
    list. */
    template <typename Self> requires (__getitem__<Self, Slice>::enable)
    auto operator[](
        this const Self& self,
        const std::initializer_list<impl::SliceInitializer>& slice
    );

};


[[nodiscard]] inline PyObject* ptr(Handle obj) {
    return obj.m_ptr;
}


[[nodiscard]] inline PyObject* release(Handle obj) {
    PyObject* temp = obj.m_ptr;
    obj.m_ptr = nullptr;
    return temp;
}


//////////////////////
////    OBJECT    ////
//////////////////////


namespace impl {

    /* A common metaclass for all Bertrand-generated Python types. */
    struct BertrandMeta : public BertrandTag {
        PyTypeObject base;
        PyObject* demangled;
        PyObject* template_instantiations;

        /* Initialize the metaclass.  This should be called in every subclass's
        __ready__() method in order to ensure that the metaclass is valid before
        calling `PyType_FromMetaClass()`. */
        static void __ready__() {
            static bool initialized = false;
            if (!initialized) {
                type.tp_base = &PyType_Type;
                if (PyType_Ready(&type) < 0) {
                    Exception::from_python();
                }
                initialized = true;
            }
        }

        /* Free the demangled name alongside the type object. */
        static void __dealloc__(BertrandMeta* self) {
            Py_XDECREF(self->demangled);
            Py_XDECREF(self->template_instantiations);
            type.tp_free(reinterpret_cast<PyObject*>(self));
        }

        /* Calling repr() on a C++ class normally yields a mangled name.  This
        metaclass demangles the output, so that the user sees normal C++ types.  */
        static PyObject* __repr__(BertrandMeta* self) {
            return Py_XNewRef(self->demangled);
        }

        /* Subscripting the metaclass allows navigation of the C++ template
        hierarchy. */
        static PyObject* __getitem__(BertrandMeta* self, PyObject* key) {
            const char* demangled = PyUnicode_AsUTF8(self->demangled);
            if (demangled == nullptr) {
                return nullptr;
            }
            if (self->template_instantiations == nullptr) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%s' has no template parameters",
                    demangled
                );
                return nullptr;
            }

            if (PyTuple_Check(key)) {
                Py_INCREF(key);
            } else {
                PyObject* tuple = PyTuple_Pack(1, key);
                if (tuple == nullptr) {
                    return nullptr;
                }
                key = tuple;
            }

            try {
                PyObject* value = PyDict_GetItem(self->template_instantiations, key);
                if (value == nullptr) {
                    std::string message = "class template has not been instantiated: ";
                    message += demangled;
                    PyObject* py_repr = PyObject_Repr(key);
                    if (py_repr == nullptr) {
                        Py_DECREF(key);
                        return nullptr;
                    }
                    Py_DECREF(key);
                    Py_ssize_t py_len;
                    const char* py_str = PyUnicode_AsUTF8AndSize(
                        py_repr,
                        &py_len
                    );
                    if (py_str == nullptr) {
                        Py_DECREF(py_repr);
                        return nullptr;
                    }
                    std::string key_repr(py_str, py_len);
                    Py_DECREF(py_repr);
                    message += "[";
                    message += key_repr.substr(1, key_repr.size() - 2);
                    message += "]";
                    PyErr_SetString(PyExc_TypeError, message.c_str());
                    return nullptr;
                }
                return value;

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }

        }

        /* The length of the type yields the number of template instantiations it is
        tracking. */
        static Py_ssize_t __len__(BertrandMeta* self) {
            if (self->template_instantiations == nullptr) {
                const char* demangled = PyUnicode_AsUTF8(self->demangled);
                if (demangled == nullptr) {
                    return -1;
                }
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%s' has no template parameters",
                    demangled
                );
                return -1;
            }
            return PyDict_Size(self->template_instantiations);
        }

        inline static PyMappingMethods mapping = {
            .mp_length = (lenfunc) &__len__,
            .mp_subscript = (binaryfunc) &__getitem__,
        };

        static PyTypeObject type;

    };

    inline PyTypeObject BertrandMeta::type = {
        .ob_base = PyVarObject_HEAD_INIT(nullptr, 0)
        .tp_name = "bertrand.Meta",
        .tp_basicsize = sizeof(BertrandMeta),
        .tp_itemsize = 0,
        .tp_dealloc = (destructor) &BertrandMeta::__dealloc__,
        .tp_repr = (reprfunc) &BertrandMeta::__repr__,
        .tp_as_mapping = &BertrandMeta::mapping,
        .tp_str = (reprfunc) &BertrandMeta::__repr__,
        .tp_flags =
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_IMMUTABLETYPE |
            Py_TPFLAGS_DISALLOW_INSTANTIATION,
        .tp_doc =
R"doc(A shared metaclass for all Bertrand-generated Python types.

Notes
-----
This metaclass identifies all the types that originate from C++.  It is used to
automatically demangle the type name in contexts where that is desirable, and to
implement certain common behaviors that are shared across all Bertrand types, like
template subscripting via `__getitem__`.)doc",
    };

    /* Implements common logic for the impl::Binding<> CRTP base class. */
    template <typename CRTP, typename CppType>
    struct Bind : public BertrandTag {
        PyObject_HEAD
        CppType m_cpp;

        inline static PyTypeObject* __type__ = nullptr;

        /* Free the C++ object when the Python object is garbage collected. */
        static void __dealloc__(CRTP* self) {
            self->m_cpp.~CppType();
            CRTP::type.tp_free(self);
        }

        /* Default repr() func demangles the type name and includes memory address
        similar to Python. */
        static PyObject* __repr__(CRTP* self) {
            try {
                std::string str = repr(self->m_cpp);
                PyObject* result = PyUnicode_FromStringAndSize(
                    str.c_str(),
                    str.size()
                );
                if (result == nullptr) {
                    return nullptr;
                }
                return result;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        // TODO: unary operators (e.g. iterators, call operator, indexing, etc.) might
        // be easily implemented here if the underlying object supports them.


        // TODO: the only way to do this properly is via the AST parser.  I'm going to
        // have to try and figure out how many possible overloads there are for each
        // operator, and generate an overload set for each one.  This is going to be
        // very difficult to do, but it's the only way to get this to work.
        // -> There will have to be a pretty robust way, given a TemplateDecl, to
        // figure out a list of all the unique instantiations of that template across
        // the module unit.  I would then generate an overload set containing each one
        // and expose that to Python.

        // TODO: turns out there's a `FunctionTemplateDecl::specializations()` method
        // that should allow me to iterate over the specializations.  I can then add
        // them to the overload set and expose them to Python as if they were
        // traditional function overloads.

        // static PyObject* __add__(PyObject* lhs, PyObject* rhs) {
        //     int unwrap_lhs = PyObject_IsInstance(
        //         lhs,
        //         reinterpret_cast<PyObject*>(CRTP::type)
        //     );
        //     if (unwrap_lhs == -1) {
        //         return nullptr;
        //     }
        //     int unwrap_rhs = PyObject_IsInstance(
        //         rhs,
        //         reinterpret_cast<PyObject*>(CRTP::type)
        //     );
        //     if (unwrap_rhs == -1) {
        //         return nullptr;
        //     }
        //     try {
        //         if (unwrap_lhs && unwrap_rhs) {
        //             CppType& l = reinterpret_cast<CRTP*>(lhs)->m_cpp;
        //             CppType& r = reinterpret_cast<CRTP*>(rhs)->m_cpp;
        //             return release(as_object(l + r));
        //         } else if (unwrap_lhs) {
        //             CppType& l = reinterpret_cast<CRTP*>(lhs)->m_cpp;
        //             Object r = reinterpret_borrow<Object>(rhs);
        //             return release(as_object(l + r));
        //         } else if (unwrap_rhs) {
        //             Object l = reinterpret_borrow<Object>(lhs);
        //             CppType& r = reinterpret_cast<CRTP*>(rhs)->m_cpp;
        //             return release(as_object(l + r));
        //         } else {
        //             Object l = reinterpret_borrow<Object>(lhs);
        //             Object r = reinterpret_borrow<Object>(rhs);
        //             throw TypeError(
        //                 "unsupported operand types for +: '" +
        //                 repr(Type(l)) + "' and '" + repr(Type(r)) + "'"
        //             );
        //         }
        //     } catch (...) {
        //         Exception::to_python();
        //         return nullptr;
        //     }
        // }

    };

}


/* An owning reference to a dynamically-typed Python object. */
class Object : public Handle {
protected:
    struct borrowed_t {};
    struct stolen_t {};

    template <std::derived_from<Object> T>
    friend T reinterpret_borrow(Handle);
    template <std::derived_from<Object> T>
    friend T reinterpret_steal(Handle);

    /* A CRTP base class that generates boilerplate Python bindings around an external
    C++ class. */
    template <typename CRTP, typename CppType>
    struct __python__ : impl::Bind<CRTP, CppType> {

        /* Initialize a non-generic type using the derived class's type_spec.  This
        should be called in the module initialization function for every type that is
        exposed to Python.  Such types must be attached to a module object. */
        template <std::derived_from<impl::ModuleTag> Parent>
        static void __ready__(Parent& parent) {
            static bool initialized = false;
            if (!initialized) {
                // initialize the metaclass if it hasn't been already
                impl::BertrandMeta::__ready__();

                // use the metaclass to convert the type_spec into a heap type
                impl::BertrandMeta* cls = reinterpret_cast<impl::BertrandMeta*>(
                    PyType_FromMetaclass(
                        &impl::BertrandMeta::type,
                        &CRTP::type_spec,
                        ptr(parent),
                        nullptr  // bases are encoded in the type_spec itself
                    )
                );
                if (cls == nullptr) {
                    Exception::from_python();
                }

                // store a borrowed reference to the type for later use
                impl::Bind<CRTP, CppType>::__type__ =
                    reinterpret_cast<PyTypeObject*>(cls);

                try {
                    // set the demangled name for the type
                    std::string demangled =
                        "<class '" + impl::demangle(cls->base.tp_name) + "'>";
                    cls->demangled = PyUnicode_FromStringAndSize(
                        demangled.c_str(),
                        demangled.size()
                    );
                    if (cls->demangled == nullptr) {
                        Exception::from_python();
                    }
                    cls->template_instantiations = nullptr;
                } catch (...) {
                    Py_DECREF(cls);
                    throw;
                }
                Py_DECREF(cls);  // parent type now owns only reference
                initialized = true;
            }
        }

        inline static PyType_Spec type_spec = {

        };

    };

    /* A CRTP base class for Python bindings around existing C++ classes.  If the C++
    class is a template accepting only other types, then this specialization will be
    chosen, which enables the C++ template hierarchy to be navigated from Python. */
    template <typename CRTP, impl::is_generic CppType>
    struct __python__<CRTP, CppType> : impl::Bind<CRTP, CppType> {
    private:

        template <typename T>
        struct build_key {
            /// NOTE: this specialization should never be called since impl::is_generic
            /// guarantees that the type is a template.  It's here to ensure that the
            /// compiler doesn't complain about the lack of a default specialization.
            static PyObject* operator()() {
                throw TypeError(
                    "cannot generate a key for a non-template type: " +
                    repr(Type<T>())
                );
            }
        };
        template <template <typename...> class T, typename... Ts>
        struct build_key<T<Ts...>> {
            static PyObject* operator()() {
                PyObject* key = PyTuple_Pack(sizeof...(Ts), ptr(Type<Ts>())...);
                if (key == nullptr) {
                    Exception::from_python();
                }
                return key;
            }
        };

    public:

        /* Initialize a generic type using the derived class's type_spec.  This should
        be called in the module initialization function for every type that is exposed
        to Python. */
        template <std::derived_from<impl::TypeTag> Parent>
        static void __ready__(Parent& parent) {
            static bool initialized = false;
            if (!initialized) {
                // initialize the metaclass if it hasn't been already
                impl::BertrandMeta::__ready__();

                // ensure that the parent type is an instance of the metaclass and has
                // a template_instantiations dictionary
                int is_meta = PyType_IsSubtype(
                    Py_TYPE(ptr(parent)),
                    &impl::BertrandMeta::type
                );
                if (is_meta < 0) {
                    Exception::from_python();
                }
                impl::BertrandMeta* parent_ptr = reinterpret_cast<impl::BertrandMeta*>(
                    ptr(parent)
                );
                if (parent_ptr->template_instantiations == nullptr) {
                    parent_ptr->template_instantiations = PyDict_New();
                    if (parent_ptr->template_instantiations == nullptr) {
                        Exception::from_python();
                    }
                }

                // use the metaclass to convert the type_spec into a heap type
                impl::BertrandMeta* cls = PyType_FromMetaclass(
                    &impl::BertrandMeta::type,
                    &CRTP::type_spec,
                    nullptr,  // template instantiations are not attached to any module
                    nullptr  // bases are encoded in the PyType_Spec itself
                );
                if (cls == nullptr) {
                    Exception::from_python();
                }

                try {
                    // set the demangled name for the type
                    std::string demangled =
                        "<class '" + impl::demangle(cls->base.tp_name) + "'>";
                    cls->demangled = PyUnicode_FromStringAndSize(
                        demangled.c_str(),
                        demangled.size()
                    );
                    if (cls->demangled == nullptr) {
                        Exception::from_python();
                    }
                    cls->template_instantiations = nullptr;

                    // insert the class into the parent's template_instantiations
                    PyObject* key = build_key<CppType>{}();
                    if (PyDict_SetItem(
                        parent_ptr->template_instantiations,
                        key,
                        reinterpret_cast<PyObject*>(cls)
                    )) {
                        Py_DECREF(key);
                        Exception::from_python();
                    }
                    Py_DECREF(key);

                    // store a borrowed reference to the type for later use
                    impl::Bind<CRTP, CppType>::__type__ =
                        reinterpret_cast<PyTypeObject*>(cls);

                } catch (...) {
                    Py_DECREF(cls);
                    throw;
                }
                Py_DECREF(cls);  // parent type now owns the only reference
                initialized = true;
            }
        }

        inline static PyType_Spec type_spec = {

        };
    };

public:

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) : Handle(Py_XNewRef(py::ptr(other))) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) : Handle(py::release(other)) {}

    /* reinterpret_borrow() constructor.  Borrows a reference to a raw Python handle. */
    Object(Handle h, borrowed_t) : Handle(Py_XNewRef(py::ptr(h))) {}

    /* reinterpret_steal() constructor.  Steals a reference to a raw Python handle. */
    Object(Handle h, stolen_t) : Handle(py::ptr(h)) {}

    /* Universal implicit constructor.  Implemented with C++ syntax via the __init__
    control struct. */
    template <typename... Args>
        requires (
            std::is_invocable_r_v<
                Object,
                __init__<Object, std::remove_cvref_t<Args>...>,
                Args...
            >
        )
    Object(Args&&... args) : Handle((
        Interpreter::init(),
        __init__<Object, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Universal explicit constructor.  Implemented with C++ syntax via the
    __explicit_init__ control struct. */
    template <typename... Args>
        requires (
            !__init__<Object, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<
                Object,
                __explicit_init__<Object, std::remove_cvref_t<Args>...>,
                Args...
            >
        )
    explicit Object(Args&&... args) : Handle((
        Interpreter::init(),
        __explicit_init__<Object, std::remove_cvref_t<Args>...>{}(
            std::forward<Args>(args)...
        )
    )) {}

    /* Destructor.  Allows any object to be stored with static duration. */
    ~Object() noexcept {
        if (Py_IsInitialized()) {
            Py_XDECREF(m_ptr);
        }
    }

    /* Copy assignment operator. */
    Object& operator=(const Object& other) {
        print("object copy assignment");  // TODO: remove debugging print statement
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(other.m_ptr);
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) {
        print("object move assignment");  // TODO: remove debugging print statement
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

};


template <std::derived_from<Object> T> requires (impl::is_extension<T>)
[[nodiscard]] inline auto& unwrap(T& obj) {
    using pytype = T::__python__;
    pytype* instance = reinterpret_cast<pytype*>(ptr(obj));
    return instance->m_cpp;
}


template <std::derived_from<Object> T> requires (impl::is_extension<T>)
[[nodiscard]] inline const auto& unwrap(const T& obj) {
    using pytype = T::__python__;
    pytype* instance = reinterpret_cast<pytype*>(ptr(obj));
    return instance->m_cpp;
}


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj) {
    return T(obj, Object::borrowed_t{});
}


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj) {
    return T(obj, Object::stolen_t{});
}


template <typename T>
struct __issubclass__<T, Object>                            : Returns<bool> {
    static consteval bool operator()() { return std::derived_from<T, Object>; }
    static constexpr bool operator()(const T& obj) { return operator()(); }
    static bool operator()(const T& obj, const Object& cls) {
        int result = PyObject_IsSubclass(
            ptr(as_object(obj)),
            ptr(cls)
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


template <typename T>
struct __isinstance__<T, Object>                            : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::python_like<T>) {
            return ptr(obj) != nullptr;
        } else {
            return issubclass<Object>(obj);
        }
    }
    static bool operator()(const T& obj, const Object& cls) {
        int result = PyObject_IsInstance(
            ptr(as_object(obj)),
            ptr(cls)
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


/* Default initialize py::Object to None. */
template <>
struct __init__<Object> {
    static auto operator()() {
        return reinterpret_steal<Object>(Py_NewRef(Py_None));
    }
};


/* Implicitly convert any C++ value into a py::Object by invoking as_object(). */
template <impl::cpp_like T> requires (__as_object__<T>::enable)
struct __init__<Object, T>                                  : Returns<Object> {
    static auto operator()(const T& value) {
        return reinterpret_steal<Object>(release(as_object(value)));
    }
};


/// NOTE: additional delegating constructors for py::Object are defined in common.h


/* Implicitly convert a py::Object (or any of its subclasses) into one of its
subclasses by applying a runtime type check. */
template <std::derived_from<Object> From, std::derived_from<From> To>
struct __cast__<From, To>                                   : Returns<To> {
    static auto operator()(const From& from) {
        if (isinstance<To>(from)) {
            return reinterpret_borrow<To>(ptr(from));
        } else {
            throw TypeError(
                "cannot convert Python object from type '" + repr(Type<From>()) +
                 "' to type '" + repr(Type<To>()) + "'"
            );
        }
    }
    static auto operator()(From&& from) {
        if (isinstance<To>(from)) {
            return reinterpret_steal<To>(release(from));
        } else {
            throw TypeError(
                "cannot convert Python object from type '" + repr(Type<From>()) +
                 "' to type '" + repr(Type<To>()) + "'"
            );
        }
    }
};


/* Implicitly convert a py::Object into any C++ type by checking for an equivalent
Python type via __as_object__, implicitly converting to that type, and then implicitly
converting to the C++ type in a 2-step process. */
template <typename To>
    requires (!impl::bertrand_like<To> && __as_object__<To>::enable)
struct __cast__<Object, To>                                 : Returns<To> {
    static auto operator()(const Object& self) {
        return self.operator typename __as_object__<To>::type().operator To();
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ integer by
calling `int(obj)` at the Python level. */
template <std::derived_from<Object> From, std::integral To>
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(const From& from) {
        long long result = PyLong_AsLongLong(ptr(from));
        if (result == -1 && PyErr_Occurred()) {
            Exception::from_python();
        }
        constexpr auto min = std::numeric_limits<To>::min();
        constexpr auto max = std::numeric_limits<To>::max();
        if (result < min || result > max) {
            std::string message = "integer out of range for ";
            message += BERTRAND_STRINGIFY(To);
            message += ": ";
            message += std::to_string(result);
            throw OverflowError(message);
        }
        return result;
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ floating-point
number by calling `float(obj)` at the Python level. */
template <std::derived_from<Object> From, std::floating_point To>
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(const From& from) {
        double result = PyFloat_AsDouble(ptr(from));
        if (result == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return result;
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ complex number
by calling `complex(obj)` at the Python level. */
template <std::derived_from<Object> From, impl::cpp_like To>
    requires (impl::complex_like<To>)
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(const From& from) {
        Py_complex result = PyComplex_AsCComplex(ptr(from));
        if (result.real == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return To(result.real, result.imag);
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ sd::string
representation by calling `str(obj)` at the Python level. */
template <std::derived_from<Object> From> 
struct __explicit_cast__<From, std::string>                 : Returns<std::string> {
    static auto operator()(const From& from) {
        PyObject* str = PyObject_Str(ptr(from));
        if (str == nullptr) {
            Exception::from_python();
        }
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(str, &size);
        if (data == nullptr) {
            Py_DECREF(str);
            Exception::from_python();
        }
        std::string result(data, size);
        Py_DECREF(str);
        return result;
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into any C++ type by
checking for an equivalent Python type via __as_object__, explicitly converting to that
type, and then explicitly converting to the C++ type in a 2-step process. */
template <std::derived_from<Object> From, typename To>
    requires (!impl::bertrand_like<To> && __as_object__<To>::enable)
struct __explicit_cast__<From, To>                          : Returns<To> {
    static auto operator()(const From& from) {
        return static_cast<To>(static_cast<typename __as_object__<To>::type>(from));
    }
};


////////////////////
////    TYPE    ////
////////////////////


/* A reference to a Python type object.  Every subclass of `py::Object` has a
corresponding specialization, which is used to replicate the python `type` statement in
C++.  Specializations can use this opportunity to statically type the object's fields
and correctly model static attributes/methods in C++. */
template <typename T = Object>
class Type : public Object, public impl::TypeTag {
    static_assert(
        std::derived_from<T, Object>,
        "Type can only be specialized for a subclass of py::Object.  Use "
        "`__as_object__` to redirect C++ types to their corresponding `py::Object` "
        "class."
    );

    using Base = Object;

public:

    Type(Handle h, borrowed_t t) : Base(h, t) {}
    Type(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Type, __init__<Type, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Type, std::remove_cvref_t<Args>...>::enable
        )
    Type(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Type, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Type, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Type, __explicit_init__<Type, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Type, std::remove_cvref_t<Args>...>::enable
        )
    explicit Type(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Type, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

};


/* Allow py::Type<> to be templated on C++ types by redirecting to the equivalent
Python type. */
template <typename T> requires (!std::derived_from<T, Object> && __as_object__<T>::enable)
class Type<T> : public Type<typename __as_object__<T>::type> {};


/* Explicitly call py::Type(obj) to deduce the Python type of an arbitrary object. */
template <typename T> requires (__as_object__<T>::enable)
explicit Type(const T&) -> Type<typename __as_object__<T>::type>;


/* `isinstance()` is already implemented for all `py::Type` subclasses.  It first does
a compile time check to see whether the argument is Python-compatible and inherits from
the templated type, and then follows up with a Python-level `isinstance()` check only
if necessary. */
template <typename T, typename Cls>
struct __isinstance__<T, Type<Cls>>                      : Returns<bool> {
    static consteval bool operator()(const T& obj) {
        return __as_object__<T>::enable &&
            std::derived_from<typename __as_object__<T>::type, Cls>;
    }
    static constexpr bool operator()(const T& obj, const Type<Cls>& cls) {
        if constexpr (operator()()) {
            int result = PyObject_IsInstance(
                ptr(as_object(obj)),
                ptr(cls)
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        } else {
            return false;
        }
    }
};


/* `issubclass()` is already implemented for all `py::Type` subclasses.  It first does
a compile time check to see whether the argument is Python-compatible and inherits from
the templated type, and then follows up with a Python-level `isinstance()` check only
if necessary */
template <typename T, typename Cls>
struct __issubclass__<T, Type<Cls>>                        : Returns<bool> {
    static consteval bool operator()() {
        return __as_object__<T>::enable &&
            std::derived_from<typename __as_object__<T>::type, Cls>;
    }
    static constexpr bool operator()(const T& obj) {
        if constexpr (operator()() && impl::type_like<T>) {
            int result = PyObject_IsSubclass(
                ptr(as_object(obj)),
                ptr(Type<Cls>())
            );
            if (result == -1) {
                Exception::from_python();
            }
        } else {
            return false;
        }
    }
    static constexpr bool operator()(const T& obj, const Type<Cls>& cls) {
        if constexpr (operator()()) {
            int result = PyObject_IsSubclass(
                ptr(as_object(obj)),
                ptr(cls)
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        } else {
            return false;
        }
    }
};


/* Types that wrap an external C++ type (whose PyTypeObject*s are generated by
Bertrand) are automatically default-constructable to the corresponding type
object. */
template <impl::is_extension T>
struct __init__<Type<T>>                                    : Returns<Type<T>> {
    static auto operator()() {
        return reinterpret_borrow<Type<T>>(
            reinterpret_cast<PyObject*>(T::__python__::__type__)
        );
    }
};


/* Types that do not have an equivalent C++ representation (e.g built-in Python types
or hand-written extensions) need to specifically overload the default constructor for
the corresponding type object, so that it points to the existing PyTypeObject*
definition.  The constructor should simply borrow a reference to the existing type. */
template <>
struct __init__<Type<Object>>                               : Returns<Type<Object>> {
    static auto operator()() {
        return reinterpret_borrow<Type<Object>>(reinterpret_cast<PyObject*>(
            &PyBaseObject_Type
        ));
    }
};


/* Metaclasses are represented as types of types, and are constructible by recursively
calling `Py_TYPE(cls)` until we reach a concrete type. */
template <typename T>
struct __init__<Type<Type<T>>>                              : Returns<Type<Type<T>>> {
    static auto operator()() {
        return reinterpret_borrow<Type<Type<T>>>(reinterpret_cast<PyObject*>(
            Py_TYPE(ptr(Type<T>()))
        ));
    }
};


/* Implement the CTAD guide by default-initializing the corresponding py::Type. */
template <typename T>
    requires (
        __as_object__<T>::enable &&
        __init__<Type<typename __as_object__<T>::type>>::enable
    )
struct __explicit_init__<Type<typename __as_object__<T>::type>, T> {
    static auto operator()(const T& obj) {
        return Type<typename __as_object__<T>::type>();
    }
};


/// NOTE: additional metaclass constructors for py::Type are defined in common.h


/* Calling a py::Type is the same as invoking the templated type's constructor. */
template <typename T, typename... Args> requires (std::constructible_from<T, Args...>)
struct __call__<Type<T>, Args...>                           : Returns<T> {
    static auto operator()(const Type<T>& self, Args&&... args) {
        return T(std::forward<Args>(args)...);
    }
};


}  // namespace py


#endif
