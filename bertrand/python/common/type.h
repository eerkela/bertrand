#ifndef BERTRAND_PYTHON_COMMON_TYPE_H
#define BERTRAND_PYTHON_COMMON_TYPE_H


#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"


// TODO: the only way to get multiple inheritance to work is to implement every class's
// interface as a CRTP mixin so they all share a single object pointer.  That way,
// you could always inherit from Object and then add in the other interfaces as needed.
// Perhaps this can replace tag classes to some extent as well.

// py::Interface<py::List<T>>.


namespace py {


namespace impl {

    struct TypeTag : public BertrandTag {
    private:

        struct BaseDef : public BertrandTag {

            // TODO: any shared interface (maybe __setup__/__ready__/__import__?)

        };

    protected:

        /* A CRTP base class that automatically generates boilerplate bindings for a
        new Python type that wraps around an external C++ type.  This is the preferred
        way of writing new Python types in C++.

        This class is only available to specializations of `py::Type<>`, each of which
        should define a public `__python__` struct that inherits from it.  It uses CRTP
        to automate the creation of Python types and their associated methods, slots,
        and other attributes, in a way that conforms to Python's best practices.  This
        includes:

            -   The use of heap types over static types, which are closer to native
                Python and can be mutated at runtime.
            -   Per-module (rather than per-process) state, which allows for multiple
                sub-interpreters to run in parallel.
            -   Cyclic GC support, which is necessary for types that contain Python
                objects, as well as the heap type itself.
            -   Direct access to the wrapped C++ object, which can be a non-owning,
                mutable or immutable reference, allowing for shared state across both
                languages.
            -   Default implementations of common slots like `__repr__`, `__hash__`,
                `__iter__`, and others, which can be detected via template
                metaprogramming and overridden by the user if necessary.
            -   A common metaclass, which demangles the C++ type name and delegates
                `isinstance()` and `issubclass()` checks to the `py::__isinstance__`
                and `py::__issubclass__` control structs, respectively.
            -   Support for C++ templates, which can be indexed in Python using the
                metaclass's `__getitem__` slot.
            -   Method overloading, wherein each method is represented as an overload
                set attached to the type via the descriptor protocol.

        Needless to say, these are all very complex and error-prone tasks that are best
        left to Bertrand's metaprogramming facilities.  By using this base class, the
        user can focus only on the public interface of the class they're trying to bind
        and not on any of the low-level details necessary to configure it for Python.
        Its protected status also strongly couples the type with the enclosing wrapper,
        producing a single unit of code that is easier to reason about and manage. */
        template <typename CRTP, typename Wrapper, typename CppType = void>
        struct def;

        /* A specialization of `def` for pure Python classes, which have no C++
        equivalent.  This is the default specialization if the C++ type is omitted
        during inheritance.  It expects the user to either implement a raw CPython type
        directly inline or supply a reference to an externally-defined `PyTypeObject`,
        which presumably exists somewhere in the CPython API or a third-party
        library. */
        template <typename CRTP, typename Wrapper>
        struct def<CRTP, Wrapper, void> : public BaseDef {
            static constexpr Origin __origin__ = Origin::PYTHON;

            /* Return a new reference to the Python type.  This must be overridden by
            all subclasses, and may require custom C API code.  It is called whenever
            `Type<Wrapper>()` is default-constructed, in order to enable per-module
            state. */
            static Type<Wrapper> __import__() {
                throw NotImplementedError(
                    "the __import__() method must be defined for all Python types: "
                    + demangle(typeid(CRTP).name())
                );
            }

        };

    };

}


////////////////////
////    TYPE    ////
////////////////////


/* A reference to a Python type object.  Every subclass of `py::Object` has a
corresponding specialization, which is used to replicate the python `type` statement in
C++.  Specializations can use this opportunity to statically type the object's fields
and correctly model static attributes/methods in C++. */
template <typename T = Object>
class Type;


/* `Type<Object>` (the default specialization) refers to a dynamic type, which can be
used polymorphically to represent the type of any Python object.  More specialized
types can always be implicitly converted to this type, but doing the opposite incurs a
runtime `issubclass()` check, and can potentially raise a `TypeError`, just like
`py::Object`. */
template <>
class Type<Object> : public Object, public impl::TypeTag {
    using Base = Object;

public:
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(
                &PyBaseObject_Type
            ));
        }
    };

    Type(Handle h, borrowed_t t) : Base(h, t) {}
    Type(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Base(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Base(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


/* Allow py::Type<> to be templated on C++ types by redirecting to the equivalent
Python type. */
template <typename T> requires (__as_object__<T>::enable)
class Type<T> : public Type<typename __as_object__<T>::type> {};


/* Explicitly call py::Type(obj) to deduce the Python type of an arbitrary object. */
template <typename T> requires (__as_object__<T>::enable)
explicit Type(const T&) -> Type<typename __as_object__<T>::type>;


/* All types are default-constructible by importing the associated Python module and
accessing the type object.  This respects per-module state and ensures that the result
always matches the type that is stored in the Python interpreter. */
template <typename T>
struct __init__<Type<T>>                                    : Returns<Type<T>> {
    static auto operator()() {
        return Type<T>::__python__::__import__();
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
template <typename T> requires (__as_object__<T>::enable)
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


/* `isinstance()` is already implemented for all `py::Type` subclasses.  It first does
a compile-time check to see whether the argument is compatible with Python and inherits
from the templated type, and then only calls the Python-level `isinstance()` if a match
is possible. */
template <typename T, typename Cls>
struct __isinstance__<T, Type<Cls>>                      : Returns<bool> {
    static consteval bool operator()(const T& obj) {
        return (
            __as_object__<T>::enable &&
            std::derived_from<typename __as_object__<T>::type, Cls>
        );
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
a compile-time check to see whether the argument is compatible with Python and inherits
from the templated type, and then only calls the Python-level `issubclass()` if a match
is possible. */
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


/* Implicitly convert the py::Type of a subclass into one of its parent classes or
a type of the same category. */
template <typename From, typename To> requires (
    std::derived_from<
        typename __as_object__<From>::type,
        typename __as_object__<To>::type
    >
)
struct __cast__<Type<From>, Type<To>>                       : Returns<Type<To>> {
    static auto operator()(const Type<From>& from) {
        return reinterpret_borrow<Type<To>>(ptr(from));
    }
    static auto operator()(Type<From>&& from) {
        return reinterpret_steal<Type<To>>(release(from));
    }
};


/* Implicitly convert the py::Type of a parent class into one of its subclasses by
applying a runtime `issubclass()` check. */
template <typename From, typename To> requires (
    !std::same_as<
        typename __as_object__<To>::type,
        typename __as_object__<From>::type
    > &&
    std::derived_from<
        typename __as_object__<To>::type,
        typename __as_object__<From>::type
    >
)
struct __cast__<Type<From>, Type<To>>                       : Returns<Type<To>> {
    static auto operator()(const Type<From>& from) {
        if (issubclass<
            typename __as_object__<To>::type,
            typename __as_object__<From>::type
        >()) {
            return reinterpret_borrow<Type<To>>(ptr(from));
        } else {
            throw TypeError(
                "cannot convert Python type from '" + repr(Type<From>()) +
                 "' to '" + repr(Type<To>()) + "'"
            );
        }
    }
    static auto operator()(Type<From>&& from) {
        if (issubclass<
            typename __as_object__<To>::type,
            typename __as_object__<From>::type
        >()) {
            return reinterpret_steal<Type<To>>(release(from));
        } else {
            throw TypeError(
                "cannot convert Python type from '" + repr(Type<From>()) +
                 "' to '" + repr(Type<To>()) + "'"
            );
        }
    }
};


////////////////////
////    META    ////
////////////////////


/* An instance of Bertrand's metatype, which is used to represent C++ types that have
been exposed to Python. */
class BertrandMeta : public Object {
    using Base = Object;

public:

    BertrandMeta(Handle h, borrowed_t t) : Base(h, t) {}
    BertrandMeta(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args> requires (implicit_ctor<BertrandMeta>::enable<Args...>)
    BertrandMeta(Args&&... args) : Base(
        implicit_ctor<BertrandMeta>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<BertrandMeta>::enable<Args...>)
    explicit BertrandMeta(Args&&... args) : Base(
        explicit_ctor<BertrandMeta>{},
        std::forward<Args>(args)...
    ) {}

};


/* Bertrand's metatype, which is used to expose C++ classes to Python. */
template <>
class Type<BertrandMeta> : public Object, public impl::TypeTag {
    using Base = Object;

public:

    /* The python representation of the metatype.  Whenever `TypeTag::def` is
    instantiated with a C++ class, it will generate an instance of this type. */
    struct __python__ : public TypeTag::def<__python__, BertrandMeta> {
        static constexpr StaticStr __doc__ =
R"doc(A shared metaclass for all Bertrand-generated Python types.

Notes
-----
This metaclass identifies all types that originate from C++.  It is used to
automatically demangle the type name and implement certain common behaviors that are
shared across all Bertrand types, such as template subscripting via `__getitem__` and
accurate `isinstance()` checks based on Bertrand's C++ control structures.)doc";

        PyTypeObject base;
        PyObject* demangled;
        PyObject* template_instantiations;

        // TODO: this should be split into __setup__() and __ready__() and use helpers
        // from TypeTag::def

        /* Ready the metatype and attach it to a module. */
        template <std::derived_from<impl::ModuleTag> Mod>
        static PyTypeObject* __ready__(Mod& parent) {
            static PyType_Slot slots[] = {
                {Py_tp_doc, const_cast<char*>(__doc__.buffer)},
                {Py_tp_dealloc, reinterpret_cast<void*>(__dealloc__)},
                {Py_tp_repr, reinterpret_cast<void*>(__repr__)},
                {Py_tp_str, reinterpret_cast<void*>(__repr__)},
                {Py_tp_iter, reinterpret_cast<void*>(__iter__)},
                {Py_mp_length, reinterpret_cast<void*>(__len__)},
                {Py_mp_subscript, reinterpret_cast<void*>(__getitem__)},
                {Py_tp_methods, methods},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = "bertrand.Meta",
                .basicsize = sizeof(BertrandMeta),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_HAVE_GC,
                .slots = slots
            };
            PyTypeObject* cls = reinterpret_cast<PyTypeObject*>(PyType_FromModuleAndSpec(
                ptr(parent),
                &spec,
                &PyType_Type
            ));
            if (cls == nullptr) {
                Exception::from_python();
            }
            return cls;
        }

        /* Get a new reference to the metatype from the root module. */
        static Type __import__();  // TODO: defined in common.h {
        //     return impl::get_type<BertrandMeta>(Module<"bertrand">());
        // }

        /* repr(cls) demangles the C++ type name.  */
        static PyObject* __repr__(__python__* self) {
            return Py_XNewRef(self->demangled);
        }

        /* len(cls) yields the number of template instantiations it is tracking. */
        static Py_ssize_t __len__(__python__* self) {
            return self->template_instantiations ?
                PyDict_Size(self->template_instantiations) : 0;
        }

        /* iter(cls) yields individual template instantiations in the order in which
        they were defined. */
        static PyObject* __iter__(__python__* self) {
            if (self->template_instantiations == nullptr) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%U' has no template instantiations",
                    self->demangled
                );
                return nullptr;
            }
            return PyObject_GetIter(self->template_instantiations);
        }

        /* cls[] allows navigation of the C++ template hierarchy. */
        static PyObject* __getitem__(__python__* self, PyObject* key) {
            if (self->template_instantiations == nullptr) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%U' has no template instantiations",
                    self->demangled
                );
                return nullptr;
            }

            if (PyTuple_Check(key)) {
                Py_INCREF(key);
            } else {
                key = PyTuple_Pack(1, key);
                if (key == nullptr) {
                    return nullptr;
                }
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

        /* isinstance(obj, cls) forwards the behavior of the __isinstance__ control
        struct. */
        static PyObject* __instancecheck__(__python__* self, PyObject* instance) {
            PyObject* forward = PyObject_GetAttr(
                instance,
                impl::TemplateString<"_instancecheck">::ptr  // TODO: implement these
            );
            if (forward == nullptr) {
                return nullptr;
            }
            PyObject* result = PyObject_CallOneArg(forward, instance);
            Py_DECREF(forward);
            return result;
        }

        /* issubclass(sub, cls) forwards the behavior of the __issubclass__ control
        struct. */
        static PyObject* __subclasscheck__(__python__* self, PyObject* subclass) {
            PyObject* forward = PyObject_GetAttr(
                subclass,
                impl::TemplateString<"_subclasscheck">::ptr  // TODO: implement these
            );
            if (forward == nullptr) {
                return nullptr;
            }
            PyObject* result = PyObject_CallOneArg(forward, subclass);
            Py_DECREF(forward);
            return result;
        }

        /* Free the type name and any template instantiations when an instance of this
        type falls out of scope. */
        static void __dealloc__(__python__* self) {
            PyObject_GC_UnTrack(self);
            Py_XDECREF(self->demangled);
            Py_XDECREF(self->template_instantiations);
            PyTypeObject* type = Py_TYPE(self);
            type->tp_free(self);
            Py_DECREF(type);  // required for heap types
        }

        /* Track instances of this type with Python's cyclic garbage collector. */
        static int __traverse__(__python__* self, visitproc visit, void* arg) {
            Py_VISIT(self->demangled);
            Py_VISIT(self->template_instantiations);
            Py_VISIT(Py_TYPE(self));  // required for heap types
            return 0;
        }

        /* Initialize the demangled name and template instantiation dictionary.  This
        method should only be called once when initializing a new type.  It's not safe
        to call it multiple times. */
        void initialize() {
            this->template_instantiations = nullptr;
            std::string demangled =
                "<class '" + impl::demangle(this->base.tp_name) + "'>";
            this->demangled = PyUnicode_FromStringAndSize(
                demangled.c_str(),
                demangled.size()
            );
            if (this->demangled == nullptr) {
                Exception::from_python();
            }
        }

        /* Create a trivial instance of the metaclass to serve as a public interface
        for a class template hierarchy.  The interface is not usable on its own
        except to provide access to its instantiations, type checks against them,
        and a central point for documentation. */
        template <StaticStr Name, typename Cls, typename... Bases, StaticStr ModName>
        static BertrandMeta stub_type(Module<ModName>& parent) {
            static PyType_Slot slots[] = {
                {Py_tp_doc, const_cast<char*>(__doc__.buffer)},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = Name,
                .basicsize = sizeof(__python__),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_IMMUTABLETYPE,
                .slots = slots
            };

            PyObject* bases = nullptr;
            if constexpr (sizeof...(Bases)) {
                bases = PyTuple_Pack(sizeof...(Bases), ptr(Type<Bases>())...);
                if (bases == nullptr) {
                    Exception::from_python();
                }
            }

            __python__* cls = reinterpret_cast<__python__*>(PyType_FromMetaclass(
                reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>())),
                ptr(parent),
                &spec,
                bases
            ));
            Py_DECREF(bases);
            if (cls == nullptr) {
                Exception::from_python();
            }

            std::string demangled =
                "<class '" + impl::demangle(cls->base.tp_name) + "'>";
            cls->demangled = PyUnicode_FromStringAndSize(
                demangled.c_str(),
                demangled.size()
            );
            if (cls->demangled == nullptr) {
                Py_DECREF(cls);
                Exception::from_python();
            }

            cls->template_instantiations = PyDict_New();
            if (cls->template_instantiations == nullptr) {
                Py_DECREF(cls);
                Exception::from_python();
            }
            return reinterpret_steal<BertrandMeta>(reinterpret_cast<PyObject*>(cls));
        };

    private:

        inline static PyMethodDef methods[] = {
            {
                .ml_name = "__instancecheck__",
                .ml_meth = (PyCFunction) __instancecheck__,
                .ml_flags = METH_O,
                .ml_doc =
R"doc(Determine if an object is an instance of the class or its subclasses.

Parameters
----------
instance : object
    The object to check against this type.

Returns
-------
bool
    Forwards the result of the __isinstance__ control struct at the C++ level.

Notes
-----
This method is called automatically by Python when using the `isinstance()` function to
test against this type.  It is equivalent to a C++ `py::isinstance()` call, which can
be customize by specializing the `__isinstance__` control struct.)doc"
            },
            {
                .ml_name = "__subclasscheck__",
                .ml_meth = (PyCFunction) __subclasscheck__,
                .ml_flags = METH_O,
                .ml_doc =
R"doc(Determine if a class is a subclass of this class.

Parameters
----------
subclass : type
    The class to check against this type.

Returns
-------
bool
    Forwards the result of the __issubclass__ control struct at the C++ level.

Notes
-----
This method is called automatically by Python when using the `issubclass()` function
to test against this type.  It is equivalent to a C++ `py::issubclass()` call, which
can be customize by specializing the `__issubclass__` control struct.)doc"
            },
            {nullptr, nullptr, 0, nullptr}
        };

    };

    Type(Handle h, borrowed_t t) : Base(h, t) {}
    Type(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Base(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Base(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


// template <typename T>
// struct __isinstance__<T, BertrandMeta>                      : Returns<bool> {};  // TODO: implement default behavior
// template <typename T>
// struct __issubclass__<T, BertrandMeta>                       : Returns<bool> {};  // TODO: implement default behavior
template <>
struct __len__<BertrandMeta>                                : Returns<size_t> {};
template <>
struct __iter__<BertrandMeta>                               : Returns<BertrandMeta> {};
template <typename T>
struct __getitem__<BertrandMeta, Type<T>>                   : Returns<BertrandMeta> {};
template <>
struct __getitem__<BertrandMeta, Tuple<Type<Object>>>       : Returns<BertrandMeta> {};
// template <typename... Ts>
// struct __getitem__<BertrandMeta, Struct<Type<Ts>...>>       : Returns<BertrandMeta> {};  // TODO: implement Struct


namespace impl {

    template <typename CRTP, typename Wrapper, typename CppType>
    struct TypeTag::def : public BaseDef {
        static constexpr Origin __origin__ = Origin::CPP;
        static constexpr StaticStr __doc__ = [] {
            return (
                "A Bertrand-generated Python wrapper for the '" +
                impl::demangle(typeid(CppType).name()) + "' C++ type."
            );
        }();

        using t_cpp = CppType;

    protected:
        using Variant = std::variant<t_cpp, t_cpp*, const t_cpp*>;

        template <typename... Ts>
        struct Visitor : Ts... {
            using Ts::operator()...;
        };

        // TODO: implement the following helpers to call during type initialization:
        // def<"name">(doc, func, defaults)
        // var<"name">(value)
        // property<"name">(doc, getter, setter, deleter)
        // cls<Derived, Bases...>();
        // classvar<"name">(value)  <- attached directly to type object
        // classproperty<"name">(getter, setter, deleter)  <- supported through __getattr__() on metaclass, not documentable
        // classmethod<"name">(doc, func, defaults)
        // staticmethod<"name">(doc, func, defaults)

    public:
        PyObject_HEAD
        Variant m_cpp;

        /* Initialize the type's `PyType_Spec` the first time the module is imported.

        Note that the resulting spec is stored with static duration and reused to
        initialize the type whenever it is imported within the same process.  As such,
        this method will only be called *once*, the first time the type is imported.
        Sub interpreters will still create their own unique types from the resulting
        spec (satisfying per-module state), but the spec itself is a singleton.

        The default implementation does nothing, but you can insert calls to any of the
        protected helpers that modify the type's slots, including the addition of
        getset descriptors, number/buffer protocols, and other special attributes that
        are contained within the `PyTypeObject` definition itself.  All other dynamic
        configuration (i.e. anything that modifies an *instance* of the type object
        rather than its definition) should be performed in `__ready__()` instead. */
        static void __setup__() {}

        /* Internal context for the `__setup__()` method. */
        static PyType_Spec& __setup_impl__() {
            static unsigned int tpflags =
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
                Py_TPFLAGS_HAVE_GC;
            #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)
                tpflags |= Py_TPFLAGS_MANAGED_WEAKREF;
            #endif

            static bool initialized = false;
            if (!initialized) {
                CRTP::__setup__();
                CRTP::tp_methods.push_back({nullptr, nullptr, 0, nullptr});
                CRTP::tp_getset.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
                CRTP::tp_members.push_back({nullptr, 0, 0, 0, nullptr});
                // TODO: and others...
                initialized = true;
            }

            static std::vector<PyType_Slot> type_slots;
            if (CRTP::tp_methods.size() > 1) {
                type_slots.push_back({Py_tp_methods, CRTP::tp_methods.data()});
            }
            if (CRTP::tp_getset.size() > 1) {
                type_slots.push_back({Py_tp_getset, CRTP::tp_getset.data()});
            }
            if (CRTP::tp_members.size() > 1) {
                type_slots.push_back({Py_tp_members, CRTP::tp_members.data()});
            }
            type_slots.push_back({0, nullptr});

            static PyType_Spec type_spec = {
                .name = typeid(CppType).name(),
                .basicsize = sizeof(CRTP),
                .itemsize = 0,
                .flags = tpflags,
                .slots = type_slots.data()
            };
            return type_spec;
        }

        /* Initialize the final type object created by every import.

        Note that unlike `__setup__()`, which is only called once per process, this
        method is called *every* time the type is imported into a fresh interpreter.
        It is the proper place to add overload sets, types, class and static methods,
        and any other objects that cannot be efficiently (and globally) represented in
        the module's `PyType_Spec`.

        The default implementation does nothing, but you can essentially treat this just
        like a Python-level `__init__` constructor for the type object itself.  Any
        attributes added at this stage are guaranteed be unique for each module,
        including any descriptors that are attached to its type.  It's still up to you
        to make sure that the data they reference does not leak into other modules, but
        the modules themselves will not interfere with this in any way. */
        static void __ready__(Type<Wrapper>& cls) {}

        // TODO: I'm no longer adding the proxy type as a base, so I need some handling
        // in the metaclass's __instancecheck__() and __subclasscheck__() slots to
        // account for this.  Probably what I need to do is, if the template_instantations
        // are not null, I loop over all the types and forward the isinstance() check.

        /* Internal context for the `__ready__()` method. */
        template <typename... Bases, StaticStr ModName>
        static Type<Wrapper> __ready_impl__(Module<ModName>& module) {
            PyObject* bases = nullptr;
            if constexpr (sizeof...(Bases)) {
                bases = PyTuple_Pack(sizeof...(Bases), ptr(Type<Bases>())...);
                if (bases == nullptr) {
                    Exception::from_python();
                }
            }

            Type<Wrapper> cls = reinterpret_steal<Type<Wrapper>>(
                PyType_FromMetaclass(
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>())),
                    ptr(module),
                    &__setup_impl__(),
                    bases
                )
            );
            Py_XDECREF(bases);
            if (ptr(cls) == nullptr) {
                Exception::from_python();
            }

            reinterpret_cast<typename Type<BertrandMeta>::__python__*>(
                ptr(Type<BertrandMeta>())
            )->initialize();

            CRTP::__ready__(cls);
            return cls;
        }

        /* Return a new reference to the type by importing the corresponding module
        and unpacking this specific type.  This is called automatically by the default
        constructor for `Type<Wrapper>`, and is the proper way to handle per-module
        state, instead of using static type objects.  It will implicitly invoke the
        `__setup__()` and `__ready__()` methods during the import process, either via
        `PyImport_Import()` or the `Module` constructor directly.

        Note the differences between this method, `__setup__()`, and `__ready__()`.
        `__setup__()` is called once with process-level scope to initialize a
        `PyType_Spec` representing the type's internal slots.  Then, whenever the
        enclosing module is imported, a new type object will be instantiated and passed
        to `__ready__()`, which completes its setup.  Finally, `__import__()` is called
        to retrieve a new reference to the type object from the imported module.  By
        following this 3-step process, all types can be correctly stored in per-module
        state and modified without affecting any sub-interpreters, whilst ensuring
        agreement between Python and C++ on the identity of each module/type. */
        static Type<Wrapper> __import__() {
            throw NotImplementedError(
                "the __import__() method must be defined for all Python types: "
                + demangle(typeid(CRTP).name())
            );
        }

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
                    [](const t_cpp& cpp) {
                        return repr(cpp);
                    },
                    [](const t_cpp* cpp) {
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
            if constexpr (!impl::hashable<t_cpp>) {
                PyErr_Format(
                    PyExc_TypeError,
                    "unhashable type: '%s'",
                    impl::demangle(CRTP::__type__->tp_name).c_str()
                );
                return -1;
            } else {
                try {
                    return std::visit(Visitor{
                        [](const t_cpp& cpp) {
                            return std::hash<t_cpp>{}(cpp);
                        },
                        [](const t_cpp* cpp) {
                            return std::hash<t_cpp>{}(*cpp);
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
            PyObject_GC_UnTrack(self);  // required for heap types
            if (std::holds_alternative<t_cpp>(self->m_cpp)) {
                std::get<t_cpp>(self->m_cpp).~t_cpp();
            }
            PyTypeObject* type = Py_TYPE(self);
            type->tp_free(self);
            Py_DECREF(type);  // required for heap types
        }

        /* tp_traverse registers any Python objects that are owned by the C++ object
        with Python's cyclic garbage collector. */
        static int __traverse__(CRTP* self, visitproc visit, void* arg) {
            if (std::holds_alternative<t_cpp>(self->m_cpp)) {
                for (auto& item : std::get<t_cpp>(self->m_cpp)) {
                    Py_VISIT(ptr(item));
                }
            }
            Py_VISIT(Py_TYPE(self));  // required for heap types
            return 0;
        }

        /* tp_clear breaks possible reference cycles for Python objects that are owned
        by the C++ object.  Note that special care has to be taken to avoid
        double-freeing any Python objects between this method and the `__dealloc__()`
        destructor. */
        static int __clear__(CRTP* self) {
            if (std::holds_alternative<t_cpp>(self->m_cpp)) {
                /// NOTE: In order to avoid a double free, we release() all of the
                /// container's items here, so that when their destructors are called
                /// normally in __dealloc__(), they see a null pointer and do nothing.
                for (auto& item : std::get<t_cpp>(self->m_cpp)) {
                    Py_XDECREF(release(item));
                }
            }
            return 0;
        }

    private:

        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::is_extension<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
            )
        friend auto wrap(const T& obj) -> __as_object__<T>::type;
        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::is_extension<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
            )
        friend auto wrap(T& obj) -> __as_object__<T>::type;
        template <std::derived_from<Object> T> requires (impl::is_extension<T>)
        friend auto& unwrap(T& obj);
        template <std::derived_from<Object> T> requires (impl::is_extension<T>)
        friend auto& unwrap(const T& obj);

        /* Implements py::wrap() for immutable references. */
        static Wrapper _wrap(const t_cpp& cpp) {
            Type<Wrapper> type;
            PyTypeObject* type_ptr = reinterpret_cast<PyTypeObject*>(ptr(type));
            PyObject* self = type_ptr->tp_alloc(type_ptr, 0);
            if (self == nullptr) {
                Exception::from_python();
            }
            new (&reinterpret_cast<CRTP*>(self)->m_cpp) Variant(&cpp);
            return reinterpret_steal<Wrapper>(self);
        }

        /* Implements py::wrap() for mutable references. */
        static Wrapper _wrap(t_cpp& cpp) {
            Type<Wrapper> type;
            PyTypeObject* type_ptr = reinterpret_cast<PyTypeObject*>(ptr(type));
            PyObject* self = type_ptr->tp_alloc(type_ptr, 0);
            if (self == nullptr) {
                Exception::from_python();
            }
            new (&reinterpret_cast<CRTP*>(self)->m_cpp) Variant(&cpp);
            return reinterpret_steal<Wrapper>(self);
        }

        /* Implements py::unwrap() for immutable wrappers. */
        static const t_cpp& _unwrap(const Wrapper& obj) {
            return std::visit(Visitor{
                [](const t_cpp& cpp) {
                    return cpp;
                },
                [](const t_cpp* cpp) {
                    return *cpp;
                }
            }, obj.m_cpp);
        }

        /* Implements py::unwrap() for mutable wrappers. */
        static t_cpp& _unwrap(Wrapper& obj) {
            return std::visit(Visitor{
                [](t_cpp& cpp) {
                    return cpp;
                },
                [](t_cpp* cpp) {
                    return *cpp;
                },
                [&obj](const t_cpp* cpp) {
                    throw TypeError(
                        "requested a mutable reference to const object: " +
                        repr(obj)
                    );
                }
            }, obj.m_cpp);
        }

        // TODO: add these to tp_methods as class methods

        /* Implements Python-level `isinstance()` checks against the class, forwarding
        to the `__isinstance__` control struct. */
        static PyObject* _instancecheck(PyObject* cls, PyObject* instance) {
            try {
                return PyBool_FromLong(
                    isinstance<Wrapper>(reinterpret_borrow<Object>(instance))
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Implements Python-level `issubclass()` checks against the class, forwarding
        to the `__issubclass__` control struct. */
        static PyObject* _subclasscheck(PyObject* cls, PyObject* subclass) {
            try {
                return PyBool_FromLong(
                    issubclass<Wrapper>(reinterpret_borrow<Object>(subclass))
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Generates a PyType_Spec config that models the CRTP type in Python.  This is
        called automatically in __ready__(), which handles type initialization and
        attaches the result to a module or public template proxy. */
        static PyType_Spec& type_spec() {
            unsigned int tpflags =
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
                Py_TPFLAGS_HAVE_GC;
            #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)
                tpflags |= Py_TPFLAGS_MANAGED_WEAKREF;
            #endif
            static std::vector<PyType_Slot> type_slots = {
                {Py_tp_dealloc, (void*) CRTP::__dealloc__},
                {Py_tp_repr, (void*) CRTP::__repr__},
                {Py_tp_hash, (void*) CRTP::__hash__},
                {Py_tp_str, (void*) CRTP::__str__},
                {Py_tp_traverse, (void*) CRTP::__traverse__},
                {Py_tp_clear, (void*) CRTP::__clear__},
                // {Py_tp_iter, (void*) CRTP::__iter__},
                // {Py_tp_new, (void*) CRTP::__new__},  // set to PyObject_GenericNew
            };

            // TODO: continue on with the rest of the slots

            // TODO: add _instancecheck and _subclasscheck to the tp_methods as class
            // or static methods, so they can be called directly from the metaclass's
            // __instancecheck__ and __subclasscheck__ slots.

            type_slots.push_back({0, nullptr});
            static std::string name = impl::demangle(typeid(t_cpp).name());
            static PyType_Spec spec = {
                .name = name.c_str(),
                .basicsize = sizeof(CRTP),
                .itemsize = 0,
                .flags = tpflags,
                .slots = type_slots.data(),
            };
            return spec;
        }

    };

}


}  // namespace py


#endif  // BERTRAND_PYTHON_COMMON_TYPE_H
