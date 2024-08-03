#ifndef BERTRAND_PYTHON_COMMON_MODULE_H
#define BERTRAND_PYTHON_COMMON_MODULE_H


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
    protected:
        /* A CRTP base class that automatically generates boilerplate bindings for a
        new Python type which wraps around an external C++ type.  This is the preferred
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
        struct def<CRTP, Wrapper, void> : public BertrandTag {
            static constexpr Origin __origin__ = Origin::PYTHON;

            /* Return a new reference to the Python type.  This must be overridden by
            all subclasses, and may require custom C API code.  It is called whenever
            `Type<Wrapper>` is default-constructed, in order to enable per-module
            state. */
            static Type<Wrapper> __import__() {
                throw NotImplementedError(
                    "the __import__() method must be defined for all Python types: "
                    + demangle(typeid(CRTP).name())
                );
            }

        };
    };


    // TODO: default Type<> constructors pull from an unordered map held in per-module
    // state.  This is populated in the module's type() helper, and the map itself
    // only holds a borrowed reference.

    // TODO: all py::Object subclasses 


    /* A common metaclass for all Bertrand-generated Python types. */
    struct BertrandMeta : public BertrandTag {
        // static PyTypeObject* __python__;

    private:

        // /* Create an instance of the metaclass to serve as a public interface for a
        // class template hierarchy.  The proxy class is not usable on its own, except to
        // provide access to its instantiations, a single entry point for documentation,
        // and type checks against its instantiations. */
        // template <impl::is_module Mod>
        // static BertrandMeta* stub(
        //     Mod& parent,
        //     const std::string& name,
        //     const std::string& doc,
        //     const std::vector<PyTypeObject*>& bases
        // ) {
        //     // check if the type already exists in the module
        //     PyObject* existing = PyObject_GetAttrString(ptr(parent), name.c_str());
        //     if (existing != nullptr) {
        //         if (
        //             !PyType_Check(existing) ||
        //             !PyType_IsSubtype(
        //                 reinterpret_cast<PyTypeObject*>(existing),
        //                 __python__
        //             ) ||
        //             !reinterpret_cast<BertrandMeta*>(existing)->template_instantiations
        //         ) {
        //             std::string existing_repr = repr(Handle(existing));
        //             Py_DECREF(existing);
        //             throw TypeError(
        //                 "name conflict: cannot create a template stub named '" +
        //                 name + "', which is already set to " + existing_repr
        //             );
        //         }
        //         return reinterpret_cast<BertrandMeta*>(Py_NewRef(existing));
        //     } else {
        //         PyErr_Clear();
        //     }

        //     // otherwise, initialize it from scratch
        //     static std::string _name = name;
        //     static std::string _doc = doc;
        //     static PyType_Slot slots[] = {
        //         {Py_tp_doc, (void*) _doc.c_str()},
        //         {0, nullptr}
        //     };
        //     static PyType_Spec spec = {
        //         .name = _name.c_str(),
        //         .basicsize = sizeof(BertrandMeta),
        //         .itemsize = 0,
        //         .flags =
        //             Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
        //             Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_IMMUTABLETYPE,
        //         .slots = slots
        //     };

        //     PyObject* _bases = PyTuple_New(bases.size());
        //     if (_bases == nullptr) {
        //         Exception::from_python();
        //     }
        //     for (size_t i = 0; i < bases.size(); i++) {
        //         PyTuple_SET_ITEM(_bases, i, Py_NewRef(bases[i]));
        //     } 

        //     BertrandMeta* cls = reinterpret_cast<impl::BertrandMeta*>(
        //         PyType_FromMetaclass(
        //             &impl::BertrandMeta::__python__,
        //             ptr(parent),
        //             &spec,
        //             _bases
        //         )
        //     );
        //     Py_DECREF(_bases);
        //     if (cls == nullptr) {
        //         Exception::from_python();
        //     }
        //     if (cls->demangle()) {
        //         Py_DECREF(cls);
        //         Exception::from_python();
        //     }
        //     cls->template_instantiations = PyDict_New();
        //     if (cls->template_instantiations == nullptr) {
        //         Py_DECREF(cls->demangled);
        //         Py_DECREF(cls);
        //         Exception::from_python();
        //     }
        //     return cls;
        // };

        // /* Populate the demangled name.  This method should only be called once when
        // initializing a new type.  It's not safe to call it multiple times. */
        // int demangle() {
        //     std::string demangled =
        //         "<class '" + impl::demangle(this->base.tp_name) + "'>";
        //     this->demangled = PyUnicode_FromStringAndSize(
        //         demangled.c_str(),
        //         demangled.size()
        //     );
        //     if (this->demangled == nullptr) {
        //         return -1;
        //     }
        //     return 0;
        // }

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
    struct __python__ : impl::TypeTag::def<__python__, Type> {
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


/* Explicitly call py::Type(obj) to deduce the Python type of an arbitrary object. */
template <typename T> requires (__as_object__<T>::enable)
explicit Type(const T&) -> Type<typename __as_object__<T>::type>;


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


/* Implicitly convert the py::Type of a subclass into one of its parent classes. */
template <typename From, typename To> requires (std::derived_from<
    typename __as_object__<From>::type,
    typename __as_object__<To>::type
>)
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
template <typename From, typename To> requires (std::derived_from<
    typename __as_object__<To>::type,
    typename __as_object__<From>::type
>)
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
    struct __python__ : public impl::TypeTag::def<__python__, BertrandMeta> {
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
            type->tp_free(reinterpret_cast<PyObject*>(self));
            Py_DECREF(type);  // required for heap types
        }

        /* Track instances of this type with Python's cyclic garbage collector. */
        static int __traverse__(__python__* self, visitproc visit, void* arg) {
            Py_VISIT(self->demangled);
            Py_VISIT(self->template_instantiations);
            Py_VISIT(Py_TYPE(self));  // required for heap types
            return 0;
        }

        /* Ready the metatype and attach it to a module. */
        template <std::derived_from<impl::ModuleTag> Mod>
        static PyTypeObject* __ready__(Mod& parent) {
            static PyType_Slot slots[] = {
                {Py_tp_base, &PyType_Type},
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
            PyObject* cls = PyType_FromModuleAndSpec(
                ptr(parent),
                &spec,
                nullptr
            );
            if (cls == nullptr) {
                Exception::from_python();
            }
            return reinterpret_cast<PyTypeObject*>(cls);
        }

        /* Get a new reference to the metatype from the root module. */
        static Type __import__();  // TODO: defined in common.h {
        //     return impl::get_type<BertrandMeta>(Module<"bertrand">());
        // }

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


// TODO: add control structs for BertrandMeta instances ([], isinstance, issubclass)


template <>
struct __len__<BertrandMeta>                                : Returns<size_t> {};
template <>
struct __iter__<BertrandMeta>                               : Returns<BertrandMeta> {};


namespace impl {

    template <typename CRTP, typename Wrapper, typename CppType>
    struct TypeTag::def : public BertrandTag {
        static constexpr Origin __origin__ = Origin::CPP;
        static constexpr StaticStr __doc__ = [] {
            return (
                "A Bertrand-generated Python wrapper for the '" +
                impl::demangle(typeid(CppType).name()) + "' C++ type."
            );
        }();

    protected:
        using Variant = std::variant<CppType, CppType*, const CppType*>;

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

        // __setup__() does one-time setup for the PyType_Spec, which is stored with
        // static duration.
        // __ready__() is called for every type created from that spec, and allows the
        // attachment of attributes that should/can not be stored in the type object
        // itself.

    public:
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

        /* TODO: maybe I need to use an approach similar to __module__, where the type
        setup is split into a __new__ (maybe __setup__?) method which initializes the
        type's slots.  This would enhance symmetry between the two.  Maybe that's what
        goes in __ready__, and then __module__ provides the module-level setup in the
        helper. */

        // TODO: I'd need 2 separate methods for this, one which is run before the
        // type is instantiated and the second which is run after.  The first can
        // affect the static PyType_Spec, and the second will affect the instance
        // before it's attached to the module via the helper.
        // -> Ideally, I can use a similar syntax to __module__, with perhaps more
        // symmetry with Python.  I can use property(), classmethod(), staticmethod()
        // method(), var(), and other specialized decorators to control how each
        // method is called.  var() is probably appropriate for modules, and the
        // method types are just replaced with function().  There's no need to expose
        // submodule() for types, but there is a need to model nested subtypes.

        /* Initialize a type and attach it to a module.  This is called automatically
        for every type during the module initialization function, which is executed by
        calling `impl::ModuleDef::__ready__()`. */
        template <impl::is_module Mod>
        static void __ready__(
            Mod& parent,
            const std::string& name,
            const std::string& doc,
            const std::vector<PyTypeObject*>& bases
        ) {
            using Meta = Type<py::BertrandMeta>::__python__;
            static bool initialized = false;
            if (initialized) {
                return;
            }

            // initialize the metaclass if it hasn't been already
            // impl::BertrandMeta::__ready__();

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
                Meta* cls = reinterpret_cast<Meta*>(
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
                    PyObject* key = template_key<CppType>{}();
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
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE;
            #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)
                tpflags |= Py_TPFLAGS_MANAGED_WEAKREF;
            #endif
            static std::vector<PyType_Slot> type_slots = {
                {Py_tp_dealloc, (void*) CRTP::__dealloc__},
                {Py_tp_repr, (void*) CRTP::__repr__},
                {Py_tp_hash, (void*) CRTP::__hash__},
                {Py_tp_str, (void*) CRTP::__str__},
                // {Py_tp_iter, (void*) CRTP::__iter__},
                // {Py_tp_new, (void*) CRTP::__new__},  // set to PyObject_GenericNew
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

            // TODO: add _instancecheck and _subclasscheck to the tp_methods as class
            // or static methods, so they can be called directly from the metaclass's
            // __instancecheck__ and __subclasscheck__ slots.

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

    };

}


}  // namespace py


#endif  // BERTRAND_PYTHON_COMMON_MODULE_H
