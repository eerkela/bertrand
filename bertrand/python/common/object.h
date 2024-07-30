#ifndef BERTRAND_PYTHON_COMMON_OBJECT_H
#define BERTRAND_PYTHON_COMMON_OBJECT_H

#include "Python.h"
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
                __type__.tp_base = &PyType_Type;
                if (PyType_Ready(&__type__) < 0) {
                    Exception::from_python();
                }
                initialized = true;
            }
        }

        /* Free the demangled name alongside the type object. */
        static void __dealloc__(BertrandMeta* self) {
            Py_XDECREF(self->demangled);
            Py_XDECREF(self->template_instantiations);
            __type__.tp_free(reinterpret_cast<PyObject*>(self));
        }

        /* Calling repr() on a C++ class normally yields a mangled name.  This
        metaclass demangles the output, so that the user sees normal C++ types.  */
        static PyObject* __repr__(BertrandMeta* self) {
            return Py_XNewRef(self->demangled);
        }

        /* Subscripting the metaclass allows navigation of the C++ template
        hierarchy. */
        static PyObject* __getitem__(BertrandMeta* self, PyObject* key) {
            if (self->template_instantiations == nullptr) {
                const char* demangled = PyUnicode_AsUTF8(self->demangled);
                if (demangled == nullptr) {
                    return nullptr;
                }
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%s' has no template instantiations",
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
                    const char* demangled = PyUnicode_AsUTF8(self->demangled);
                    if (demangled == nullptr) {
                        Py_DECREF(key);
                        return nullptr;
                    }
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
            return self->template_instantiations ?
                PyDict_Size(self->template_instantiations) : 0;
        }

        /* Iterate over the type to yield the individual template instantiations, in
        the order in which they were defined. */
        static PyObject* __iter__(BertrandMeta* self) {
            if (self->template_instantiations == nullptr) {
                const char* demangled = PyUnicode_AsUTF8(self->demangled);
                if (demangled == nullptr) {
                    return nullptr;
                }
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%s' has no template instantiations",
                    demangled
                );
                return nullptr;
            }
            return PyObject_GetIter(self->template_instantiations);
        }

        /* Create an instance of the metaclass to serve as a public interface for a
        class template hierarchy.  The proxy class is not usable on its own, except to
        provide access to its instantiations, a single entry point for documentation,
        and type checks against its instantiations. */
        template <std::derived_from<impl::ModuleTag> Mod>
        static BertrandMeta* stub(
            Mod& parent,
            const std::string& name,
            const std::string& doc,
            const std::vector<PyTypeObject*>& bases
        ) {
            // check if the type already exists in the module
            PyObject* existing = PyObject_GetAttrString(ptr(parent), name.c_str());
            if (existing != nullptr) {
                if (
                    !PyType_Check(existing) ||
                    !PyType_IsSubtype(
                        reinterpret_cast<PyTypeObject*>(existing),
                        &__type__
                    ) ||
                    !reinterpret_cast<BertrandMeta*>(existing)->template_instantiations
                ) {
                    std::string existing_repr = repr(Handle(existing));
                    Py_DECREF(existing);
                    throw TypeError(
                        "name conflict: cannot create a template stub named '" +
                        name + "', which is already set to " + existing_repr
                    );
                }
                return reinterpret_cast<BertrandMeta*>(Py_NewRef(existing));
            } else {
                PyErr_Clear();
            }

            // otherwise, initialize it from scratch
            static std::string _name = name;
            static std::string _doc = doc;
            static PyType_Slot slots[] = {
                {Py_tp_doc, (void*) _doc.c_str()},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = _name.c_str(),
                .basicsize = sizeof(BertrandMeta),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_IMMUTABLETYPE,
                .slots = slots
            };

            PyObject* _bases = PyTuple_New(bases.size());
            if (_bases == nullptr) {
                Exception::from_python();
            }
            for (size_t i = 0; i < bases.size(); i++) {
                PyTuple_SET_ITEM(_bases, i, Py_NewRef(bases[i]));
            } 

            BertrandMeta* cls = reinterpret_cast<impl::BertrandMeta*>(
                PyType_FromMetaclass(
                    &impl::BertrandMeta::__type__,
                    ptr(parent),
                    &spec,
                    _bases
                )
            );
            Py_DECREF(_bases);
            if (cls == nullptr) {
                Exception::from_python();
            }
            if (cls->demangle()) {
                Py_DECREF(cls);
                Exception::from_python();
            }
            cls->template_instantiations = PyDict_New();
            if (cls->template_instantiations == nullptr) {
                Py_DECREF(cls->demangled);
                Py_DECREF(cls);
                Exception::from_python();
            }
            return cls;
        };

        /* Populate the demangled name.  This method should only be called once when
        initializing a new type.  It's not safe to call it multiple times. */
        int demangle() {
            std::string demangled =
                "<class '" + impl::demangle(this->base.tp_name) + "'>";
            this->demangled = PyUnicode_FromStringAndSize(
                demangled.c_str(),
                demangled.size()
            );
            if (this->demangled == nullptr) {
                return -1;
            }
            return 0;
        }

        inline static PyMappingMethods mapping = {
            .mp_length = (lenfunc) &__len__,
            .mp_subscript = (binaryfunc) &__getitem__,
        };

        static PyTypeObject __type__;

    };

    inline PyTypeObject BertrandMeta::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(nullptr, 0)
        .tp_name = "bertrand.Meta",
        .tp_basicsize = sizeof(BertrandMeta),
        .tp_itemsize = 0,
        .tp_dealloc = (destructor) BertrandMeta::__dealloc__,
        .tp_repr = (reprfunc) BertrandMeta::__repr__,
        .tp_as_mapping = &BertrandMeta::mapping,
        .tp_str = (reprfunc) BertrandMeta::__repr__,
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
        .tp_iter = (getiterfunc) BertrandMeta::__iter__,
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

    /* A CRTP base class that automatically generates boilerplate bindings for a new
    Python type which wraps an existing C++ type.  This is the preferred way of writing
    new Python types in C++.  By inheriting from this class in a nested (public)
    `__python__` type, a `py::Object` subclass avoids the need to:

        -   Navigate the vagaries of the CPython API when it comes to type creation and
            management.
        -   Handle low-level memory management and interactions with Python's garbage
            collector.
        -   Handle shared state and direct access from C++ to Python and vice versa.
        -   Initialize and store the type object itself.
        -   Implement common operators that can be inferred through template
            metaprogramming, like `__hash__`, `__repr__`, `__iter__`, and others.
        -   Automatically generate overload sets and public proxies for template
            instantiations, as well as expose the template hierarchy to Python

    Needless to say, these are all very complex and error-prone tasks that are best
    left to Bertrand's metaprogramming facilities.  By using this base class, the user
    can focus only on the public interface of the class they're trying to bind and not
    any of the low-level details necessary to configure it for Python.  It also strongly
    couples that type with the enclosing wrapper, producing a single unit of code. */
    template <typename Wrapper, typename CRTP, typename CppType>
    struct __python__ : public impl::BertrandTag {
    private:
        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::is_extension<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
            )
        friend __as_object__<T>::type wrap(const T& obj);
        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::is_extension<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
            )
        friend __as_object__<T>::type wrap(T& obj);
        template <std::derived_from<Object> T> requires (impl::is_extension<T>)
        friend auto& unwrap(T& obj);
        template <std::derived_from<Object> T> requires (impl::is_extension<T>)
        friend auto& unwrap(const T& obj);

        /* Implements py::wrap() for immutable references. */
        static Wrapper _wrap(const CppType& cpp) {
            PyObject* self = __type__->tp_alloc(__type__, 0);
            if (self == nullptr) {
                Exception::from_python();
            }
            new (&reinterpret_cast<CRTP*>(self)->m_cpp) Variant(&cpp);
            return reinterpret_steal<Wrapper>(self);
        }

        /* Implements py::wrap() for mutable references. */
        static Wrapper _wrap(CppType& cpp) {
            PyObject* self = __type__->tp_alloc(__type__, 0);
            if (self == nullptr) {
                Exception::from_python();
            }
            new (&reinterpret_cast<CRTP*>(self)->m_cpp) Variant(&cpp);
            return reinterpret_steal<Wrapper>(self);
        }

        /* Implements py::unwrap() for immutable wrappers. */
        static const CppType& _unwrap(const Wrapper& obj) {
            return std::visit(Visitor{
                [](const CppType& cpp) {
                    return cpp;
                },
                [](const CppType* cpp) {
                    return *cpp;
                }
            }, obj.m_cpp);
        }

        /* Implements py::unwrap() for mutable wrappers. */
        static CppType& _unwrap(Wrapper& obj) {
            return std::visit(Visitor{
                [](CppType& cpp) {
                    return cpp;
                },
                [](CppType* cpp) {
                    return *cpp;
                },
                [&obj](const CppType* cpp) {
                    throw TypeError(
                        "requested a mutable reference to const object: " +
                        repr(obj)
                    );
                }
            }, obj.m_cpp);
        }

        /* Generates a PyType_Spec config that models the CRTP type in Python.  This is
        called automatically in __ready__(), which handles type initialization and
        attaches the result to a module or public template proxy. */
        static PyType_Spec& type_spec() {
            unsigned int tpflags =
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE;
            #if (PY_MAJOR_VERSION >= 3 && PY_MAJOR_VERSION >= 12)
                tpflags |= Py_TPFLAGS_MANAGED_WEAKREF;
            #endif
            static std::vector<PyType_Slot> type_slots = {
                {Py_tp_dealloc, (void*) CRTP::__dealloc__},
                {Py_tp_repr, (void*) CRTP::__repr__},
                {Py_tp_hash, (void*) CRTP::__hash__},
                {Py_tp_str, (void*) CRTP::__str__},
            };
            if constexpr (
                impl::is_iterable<CppType> &&
                std::derived_from<impl::iter_type<CppType>, Object>
            ) {
                tpflags |= Py_TPFLAGS_HAVE_GC;
                type_slots.push_back({Py_tp_traverse, (void*) CRTP::__traverse__});
                type_slots.push_back({Py_tp_clear, (void*) CRTP::__clear__});
            }

            // TODO: continue on with the rest of the slots

            type_slots.push_back({0, nullptr});
            static std::string name = impl::demangle(typeid(CppType).name());
            static PyType_Spec spec = {
                .name = name.c_str(),
                .basicsize = sizeof(CRTP),
                .itemsize = 0,
                .flags = tpflags,
                .slots = type_slots.data(),
            };
            return spec;
        }

        // TODO: template_key may need to deal with template parameters that don't have
        // an equivalent Python type.  

        /* Generates the key under which to insert a particular template instantiation
        into the public proxy's `__getitem__` dict, so that the template hierarchy is
        navigable in Python. */
        template <typename T>
        struct template_key {
            /// NOTE: this specialization should never be called since impl::is_generic
            /// guarantees that the type is a template.
            static PyObject* operator()() {
                throw TypeError(
                    "cannot generate a key for a non-template type: " +
                    repr(Type<T>())
                );
            }
        };
        template <template <typename...> class T, typename... Ts>
            requires (__as_object__<Ts>::enable && ...)  // TODO: is this necessary?
        struct template_key<T<Ts...>> {
            static PyObject* operator()() {
                PyObject* key = PyTuple_Pack(sizeof...(Ts), ptr(Type<Ts>())...);
                if (key == nullptr) {
                    Exception::from_python();
                }
                return key;
            }
        };

    protected:
        using Variant = std::variant<CppType, CppType*, const CppType*>;

        template <typename... Ts>
        struct Visitor : Ts... {
            using Ts::operator()...;
        };

    public:
        inline static PyTypeObject* __type__ = nullptr;

        PyObject_HEAD
        Variant m_cpp;
        using t_cpp = CppType;

        /* TODO: implement the following slots using metaprogramming where possible:
         *
        //  * tp_name
        //  * tp_basicsize
        //  * tp_itemsize
        //  * tp_dealloc
        //  * tp_repr
        //  * tp_hash
         * tp_vectorcall_offset
         * tp_call
        //  * tp_str
        //  * tp_traverse
        //  * tp_clear
         * tp_iter
         * tp_iternext
         * tp_init
         * tp_alloc
         * tp_new
         * tp_free
         * tp_is_gc
         * tp_finalize
         * tp_vectorcall
         *
         * to be defined in subclasses:
         *
         * tp_as_number
         * tp_as_sequence
         * tp_as_mapping
         * tp_as_async
         * tp_getattro
         * tp_setattro
         * tp_as_buffer
         * tp_doc
         * tp_richcompare
         * tp_methods
         * tp_members
         * tp_getset
         * tp_bases
         * tp_descr_get
         * tp_descr_set
         *
         * These cannot be contextually inferred from the AST, and must have
         * corresponding attributes in the original C++ type:
         *
         * tp_as_async  -> [[py::__await__]] + [[py::__aiter__]] + [[py::__anext__]] + [[py::__asend__]]
         * tp_getattro  -> [[py::__getattr__]]
         * tp_setattro  -> [[py::__setattr__]] + [[py::__delattr__]]
         * tp_as_buffer -> [[py::__buffer__]] + [[py::__release_buffer__]]
         * tp_descr_get -> [[py::__get__]]
         * tp_descr_set -> [[py::__set__]] + [[py::__delete__]]
         *
         * TODO: perhaps these are passed into __ready__() from the derived class as
         * arguments?
         */

        // TODO: I'll have to implement a Python wrapper around a C++ iterator, which
        // would probably need to use the non-owning reference types in order to
        // accurately reflect mutability and avoid unnecessary allocation overhead.

        /* tp_repr demangles the exact C++ type name and includes memory address
        similar to Python. */
        static PyObject* __repr__(CRTP* self) {
            try {
                std::string str = std::visit(Visitor{
                    [](const CppType& cpp) {
                        return repr(cpp);
                    },
                    [](const CppType* cpp) {
                        return repr(*cpp);
                    }
                }, self->m_cpp);
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

        /* tp_hash delegates to `std::hash` if possible, or raises a TypeError. */
        static Py_hash_t __hash__(CRTP* self) {
            if constexpr (!impl::hashable<CppType>) {
                PyErr_Format(
                    PyExc_TypeError,
                    "unhashable type: '%s'",
                    impl::demangle(CRTP::__type__->tp_name).c_str()
                );
                return -1;
            } else {
                try {
                    return std::visit(Visitor{
                        [](const CppType& cpp) {
                            return std::hash<CppType>{}(cpp);
                        },
                        [](const CppType* cpp) {
                            return std::hash<CppType>{}(*cpp);
                        }
                    }, self->m_cpp);
                } catch (...) {
                    Exception::to_python();
                    return -1;
                }
            }
        }

        /* tp_str defaults to tp_repr. */
        static PyObject* __str__(CRTP* self) {
            return __repr__(self);
        }

        /* tp_dealloc calls the ordinary C++ destructor. */
        static void __dealloc__(CRTP* self) {
            if (std::holds_alternative<CppType>(self->m_cpp)) {
                std::get<CppType>(self->m_cpp).~CppType();
            }
            CRTP::type.tp_free(self);
        }

        /* Owning references to C++ objects that themselves reference Python objects
         * need to be registered with Python's cyclic garbage collector.  This is
         * extremely confusing and dangerous to do alone, so we automate it here by
         * detecting C++ types that yield Python objects when iterated over.
         */

        static int __traverse__(CRTP* self, visitproc visit, void* arg) {
            if (std::holds_alternative<CppType>(self->m_cpp)) {
                for (auto& item : std::get<CppType>(self->m_cpp)) {
                    Py_VISIT(ptr(item));
                }
            }
            return 0;
        }

        static int __clear__(CRTP* self) {
            if (std::holds_alternative<CppType>(self->m_cpp)) {
                /// NOTE: In order to avoid a double free, we release() all of the
                /// container's items here, so that when their destructors are called
                /// in __dealloc__(), they see a null pointer and do nothing.
                for (auto& item : std::get<CppType>(self->m_cpp)) {
                    Py_XDECREF(release(item));
                }
            }
            return 0;
        }

        /* Initialize a type and attach it to a module.  This is called automatically
        for every type during the module initialization function, which is executed by
        calling `impl::ModuleDef::__ready__()`. */
        template <std::derived_from<impl::ModuleTag> Mod>
        static void __ready__(
            Mod& parent,
            const std::string& name,
            const std::string& doc,
            const std::vector<PyTypeObject*>& bases
        ) {
            static bool initialized = false;
            if (initialized) {
                return;
            }

            // initialize the metaclass if it hasn't been already
            impl::BertrandMeta::__ready__();

            // if the wrapper type is not generic, it can be directly instantiated and
            // attached to the module
            if constexpr (!impl::is_generic<Wrapper>) {
                PyObject* _bases = PyTuple_New(bases.size());
                if (_bases == nullptr) {
                    Exception::from_python();
                }
                for (size_t i = 1; i < bases.size() + 1; ++i) {
                    PyTuple_SET_ITEM(_bases, i, Py_NewRef(bases[i]));
                }

                // generate a type spec and convert to a heap type using BertrandMeta
                impl::BertrandMeta* cls = reinterpret_cast<impl::BertrandMeta*>(
                    PyType_FromMetaclass(
                        &impl::BertrandMeta::__type__,
                        ptr(parent),
                        &type_spec(),
                        _bases
                    )
                );
                Py_DECREF(_bases);
                if (cls == nullptr) {
                    Exception::from_python();
                }

                try {
                    cls->template_instantiations = nullptr;
                    if (cls->demangle()) {
                        Exception::from_python();
                    }

                } catch (...) {
                    Py_DECREF(cls);
                    throw;
                }

                // store a borrowed reference to the type for later use
                __type__ = reinterpret_cast<PyTypeObject*>(cls);
                Py_DECREF(cls);  // module now owns only reference

            // if the wrapper type is generic, a public proxy type needs to be created
            // that can be indexed to resolve individual instantiations
            } else {
                // initialize or retrieve the proxy type under which the instantiation
                // will be stored.  the instantiation always inherits from this type,
                // in addition to any other bases that are specified
                impl::BertrandMeta* stub = impl::BertrandMeta::stub(
                    parent,
                    name,
                    doc,
                    bases
                );
                PyObject* _bases = PyTuple_New(bases.size() + 1);
                if (_bases == nullptr) {
                    Py_DECREF(stub);
                    Exception::from_python();
                }
                for (size_t i = 0; i < bases.size(); ++i) {
                    PyTuple_SET_ITEM(_bases, i, Py_NewRef(bases[i]));
                }
                PyTuple_SET_ITEM(_bases, bases.size(), stub);  // steals a reference

                // use the metaclass to convert the type_spec into a heap type
                impl::BertrandMeta* cls = reinterpret_cast<impl::BertrandMeta*>(
                    PyType_FromMetaclass(
                        &impl::BertrandMeta::__type__,
                        nullptr,  // instantiations aren't directly attached to a module
                        &type_spec(),
                        _bases
                    )
                );
                Py_DECREF(_bases);
                if (cls == nullptr) {
                    Exception::from_python();
                }

                try {
                    cls->template_instantiations = nullptr;
                    if (cls->demangle()) {
                        Exception::from_python();
                    }

                    // insert the class into the parent's template_instantiations
                    PyObject* key = build_key<CppType>{}();
                    if (PyDict_SetItem(
                        stub->template_instantiations,
                        key,
                        reinterpret_cast<PyObject*>(cls)
                    )) {
                        Py_DECREF(key);
                        Exception::from_python();
                    }
                    Py_DECREF(key);

                } catch (...) {
                    Py_DECREF(cls);
                    throw;
                }

                // store a borrowed reference to the type for later use
                __type__ = reinterpret_cast<PyTypeObject*>(cls);
                Py_DECREF(cls);  // stub now owns the only reference
            }

            initialized = true;
        }

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


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj) {
    return T(obj, Object::borrowed_t{});
}


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj) {
    return T(obj, Object::stolen_t{});
}


template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::is_extension<typename __as_object__<T>::type> &&
        std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
    )
[[nodiscard]] __as_object__<T>::type wrap(T& obj)  {
    return T::__python__::_wrap(obj);
}


template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::is_extension<typename __as_object__<T>::type> &&
        std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
    )
[[nodiscard]] __as_object__<T>::type wrap(const T& obj) {
    return T::__python__::_wrap(obj);
}


template <std::derived_from<Object> T> requires (impl::is_extension<T>)
[[nodiscard]] auto& unwrap(T& obj) {
    return T::__python__::_unwrap(obj);
}


template <std::derived_from<Object> T> requires (impl::is_extension<T>)
[[nodiscard]] const auto& unwrap(const T& obj) {
    return T::__python__::_unwrap(obj);
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


//////////////////////
////    MODULE    ////
//////////////////////


namespace impl {

    /* A unique subclass of Python's base module type that allows us to add descriptors
    for computed properties and overload sets without affecting any other modules. */
    template <StaticStr Name>
    struct PyModule {
        PyModuleObject base;

        // TODO: ensure everything below this line is handled properly, and then
        // implement the module initialization helpers in the public API, particularly
        // submodule() and var().  The first is done, and the second should take a
        // reference (mutable or immutable) to a raw variable, and generate a property
        // descript with an appropriate getter/setter and attach it to the module's
        // type object.  Types and functions should be handled via their own __ready__
        // calls, which can attach them to either a module, submodule, or type as
        // needed.

        // TODO: if I want to use raw getset descriptors, then I probably need to
        // register those *before* readying the type, since they are part of the
        // PyType_Spec that needs to be defined.  Perhaps this is implemented as a
        // vector of internal descriptor structs that are generated by a var()
        // static method.  The vector's data() would then be used to initialize the
        // PyType_Spec when __ready__() is called.  The best way to do this is to
        // probably place the configuration structs within __ready__ as static
        // variables, so that they are only evaluated once __ready__() is actually
        // called.  This gives us time to append variables to the vector and then
        // pass them in as static getset descriptors, rather than dynamically
        // generating a property.

        // So the module initialization function basically progresses as follows:
        // -> call the module's `var()` method for each variable that needs to be
        // exposed.
        // -> __ready__() all the global types that do not have any template parameters,
        // including a synthesized root type for templates.  That should probably also
        // be handled as a helper method similar to var(), or be subsumed into __ready__
        // the first time a template type is readied.

        /* Initialize the module and assign its type. */
        static Module<Name> __ready__(const Str& doc) {
            PyObject* _mod = PyModule_Create(&module_def);
            if (_mod == nullptr) {
                Exception::from_python();
            }
            Module<Name> mod = reinterpret_steal<Module<Name>>(_mod);
            PyObject* type = PyType_FromSpec(&type_spec);
            if (type == nullptr) {
                return nullptr;
            }
            Py_SET_TYPE(ptr(mod), reinterpret_cast<PyTypeObject*>(type));
            setattr<"__doc__">(mod, doc);
            return mod;
        }

        template <StaticStr SubName>
        static Module<Name + "." + SubName> submodule(
            Module<Name>& parent,
            const Str& doc
        ) {
            auto mod = Module<Name + "." + SubName>::__ready__(doc);

            // add the submodule to the interpreter's import list
            PyObject* _mod_ptr = PyImport_AddModuleObject(
                impl::TemplateString<Name + "." + SubName>::ptr
            );
            if (_mod_ptr == nullptr) {
                Exception::from_python();
            }
            Py_DECREF(_mod_ptr);

            // set the submodule as an attribute of the parent module
            if (PyObject_SetAttr(
                ptr(parent),
                impl::TemplateString<SubName>::ptr,
                ptr(mod)
            )) {
                Exception::from_python();
            }
            return mod;
        }

        // TODO: perhaps add a var() method for adding a global variable to the
        // module scope.  It could take separate overloads for const vs mutable
        // references, and generate a setter for the second case, and only a setter
        // for the first.

        // TODO: I might be able to do something similar with functions, in which
        // case I would automatically generate an overload set for each function as
        // it is added, or append to it as needed.  Alternatively, I can use the same
        // attach() method that I'm exposing to the public.

        // TODO: maybe also a __type__ method, which would add a type directly to the
        // module if it is not generic, or generate a front-facing abstract type if it
        // is.  That should pretty much automate the process of handling generic types.

        inline static PyModuleDef module_def = {
            .m_base = PyModuleDef_HEAD_INIT,
            .m_name = Name,
            .m_doc = "A Python wrapper around the '" + Name + "' C++ module.",
            .m_size = -1,
        };

        inline static PyType_Slot type_slots[] = {
            {Py_tp_base, &PyModule_Type},
            {0, nullptr}
        };

        inline static PyType_Spec type_spec = {
            .name = typeid(PyModule).name(),
            .basicsize = sizeof(PyModuleObject),
            .itemsize = 0,
            .flags =
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
                Py_TPFLAGS_DISALLOW_INSTANTIATION,
            .slots = type_slots,
        };

    };

}


/* A Python module with a unique type, which supports the addition of descriptors for
computed properties, overload sets, etc. */
template <StaticStr Name>
class Module : public Object, public impl::ModuleTag {
    using Base = Object;

public:

    Module(Handle h, borrowed_t t) : Base(h, t) {}
    Module(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Module, __init__<Module, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Module, std::remove_cvref_t<Args>...>::enable
        )
    Module(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Module, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Module, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Module, __explicit_init__<Module, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Module, std::remove_cvref_t<Args>...>::enable
        )
    explicit Module(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Module, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

};


/* Default-initializing a py::Module is functionally equivalent to an import
statement. */
template <StaticStr Name>
struct __init__<Module<Name>>                               : Returns<Module<Name>> {
    static auto operator()() {
        PyObject* mod = PyImport_Import(impl::TemplateString<Name>::ptr);
        if (mod == nullptr) {
            throw Exception();
        }
        return reinterpret_steal<Module<Name>>(mod);
    }
};


template <StaticStr Name>
class Type<Module<Name>> : public Object {
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


template <StaticStr Name>
struct __init__<Type<Module<Name>>> {
    static auto operator()() {
        return reintepret_steal<Type<Module<Name>>>(
            reinterpret_cast<PyObject*>(Py_TYPE(ptr(Module<Name>())))
        );
    }
};


}  // namespace py


#endif
