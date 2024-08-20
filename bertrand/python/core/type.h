#ifndef BERTRAND_PYTHON_CORE_TYPE_H
#define BERTRAND_PYTHON_CORE_TYPE_H

#include "declarations.h"
#include "object.h"
#include "code.h"
#include "except.h"


namespace py {


namespace impl {

    namespace dunder {

        template <typename CRTP>
        concept has_dealloc = requires() {
            { CRTP::__dealloc__ } -> std::convertible_to<void(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_vectorcall = requires() {
            { &CRTP::__vectorcall__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject* const*, size_t, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_await = requires() {
            { CRTP::__await__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_aiter = requires() {
            { CRTP::__aiter__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_anext = requires() {
            { CRTP::__anext__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_asend = requires() {
            { CRTP::__asend__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject*, PyObject**)
            >;
        };

        template <typename CRTP>
        concept has_repr = requires() {
            { CRTP::__repr__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_add = requires() {
            { CRTP::__add__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_subtract = requires() {
            { CRTP::__sub__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_multiply = requires() {
            { CRTP::__mul__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_remainder = requires() {
            { CRTP::__mod__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_divmod = requires() {
            { CRTP::__divmod__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_power = requires() {
            { CRTP::__pow__ } -> std::convertible_to<
                PyObject*(*)(PyObject*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_negative = requires() {
            { CRTP::__neg__ } -> std::convertible_to<PyObject*(*)(PyObject*)>;
        };

        template <typename CRTP>
        concept has_positive = requires() {
            { CRTP::__pos__ } -> std::convertible_to<PyObject*(*)(PyObject*)>;
        };

        template <typename CRTP>
        concept has_absolute = requires() {
            { CRTP::__abs__ } -> std::convertible_to<PyObject*(*)(PyObject*)>;
        };

        template <typename CRTP>
        concept has_bool = requires() {
            { CRTP::__bool__ } -> std::convertible_to<PyObject*(*)(PyObject*)>;
        };

        template <typename CRTP>
        concept has_invert = requires() {
            { CRTP::__invert__ } -> std::convertible_to<PyObject*(*)(PyObject*)>;
        };

        template <typename CRTP>
        concept has_lshift = requires() {
            { CRTP::__lshift__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_rshift = requires() {
            { CRTP::__rshift__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_and = requires() {
            { CRTP::__and__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_xor = requires() {
            { CRTP::__xor__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_or = requires() {
            { CRTP::__or__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_int = requires() {
            { CRTP::__int__ } -> std::convertible_to<PyObject*(*)(PyObject*)>;
        };

        template <typename CRTP>
        concept has_float = requires() {
            { CRTP::__float__ } -> std::convertible_to<PyObject*(*)(PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_add = requires() {
            { CRTP::__iadd__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_subtract = requires() {
            { CRTP::__isub__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_multiply = requires() {
            { CRTP::__imul__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_remainder = requires() {
            { CRTP::__imod__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_power = requires() {
            { CRTP::__ipow__ } -> std::convertible_to<
                PyObject*(*)(PyObject*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_inplace_lshift = requires() {
            { CRTP::__ilshift__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_rshift = requires() {
            { CRTP::__irshift__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_and = requires() {
            { CRTP::__iand__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_xor = requires() {
            { CRTP::__ixor__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_or = requires() {
            { CRTP::__ior__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_floor_divide = requires() {
            { CRTP::__floordiv__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_true_divide = requires() {
            { CRTP::__truediv__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_floor_divide = requires() {
            { CRTP::__ifloordiv__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_true_divide = requires() {
            { CRTP::__itruediv__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_index = requires() {
            { CRTP::__index__ } -> std::convertible_to<PyObject*(*)(PyObject*)>;
        };

        template <typename CRTP>
        concept has_matrix_multiply = requires() {
            { CRTP::__matmul__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_inplace_matrix_multiply = requires() {
            { CRTP::__imatmul__ } -> std::convertible_to<PyObject*(*)(PyObject*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_len = requires() {
            { CRTP::__len__ } -> std::convertible_to<Py_ssize_t(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_contains = requires() {
            { CRTP::__contains__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_getitem = requires() {
            { CRTP::__getitem__ } -> std::convertible_to<PyObject*(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_setitem = requires() {
            { CRTP::__setitem__ } -> std::convertible_to<
                int(*)(CRTP*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_delitem = requires() {
            { CRTP::__delitem__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_hash = requires() {
            { CRTP::__hash__ } -> std::convertible_to<Py_hash_t(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_call = requires() {
            { CRTP::__call__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_str = requires() {
            { CRTP::__str__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_getattr = requires() {
            { CRTP::__getattr__ } -> std::convertible_to<PyObject*(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_setattr = requires() {
            { CRTP::__setattr__ } -> std::convertible_to<
                int(*)(CRTP*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_delattr = requires() {
            { CRTP::__delattr__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_buffer = requires() {
            { CRTP::__buffer__ } -> std::convertible_to<
                int(*)(PyObject*, Py_buffer*, int)
            >;
        };

        template <typename CRTP>
        concept has_release_buffer = requires() {
            { CRTP::__release_buffer__ } -> std::convertible_to<
                void(*)(PyObject*, Py_buffer*)
            >;
        };

        template <typename CRTP>
        concept has_doc = requires() {
            { CRTP::__doc__ } -> std::convertible_to<const char*>;
        };

        template <typename CRTP>
        concept has_traverse = requires() {
            { CRTP::__traverse__ } -> std::convertible_to<
                int(*)(CRTP*, visitproc, void*)
            >;
        };

        template <typename CRTP>
        concept has_clear = requires() {
            { CRTP::__clear__ } -> std::convertible_to<int(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_lt = requires() {
            { CRTP::__lt__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_le = requires() {
            { CRTP::__le__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_eq = requires() {
            { CRTP::__eq__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_ne = requires() {
            { CRTP::__ne__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_ge = requires() {
            { CRTP::__ge__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_gt = requires() {
            { CRTP::__gt__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_iter = requires() {
            { CRTP::__iter__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_next = requires() {
            { CRTP::__next__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        /// NOTE: tp_methods, tp_members, tp_getset are handled via static vectors

        template <typename CRTP>
        concept has_get = requires() {
            { CRTP::__get__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_set = requires() {
            { CRTP::__set__ } -> std::convertible_to<
                int(*)(CRTP*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_delete = requires() {
            { CRTP::__delete__ } -> std::convertible_to<int(*)(CRTP*, PyObject*)>;
        };

        template <typename CRTP>
        concept has_init = requires() {
            { CRTP::__init__ } -> std::convertible_to<
                int(*)(CRTP*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_new = requires() {
            { CRTP::__new__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_instancecheck = requires() {
            { CRTP::__instancecheck__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_subclasscheck = requires() {
            { CRTP::__subclasscheck__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_enter = requires() {
            { CRTP::__enter__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_exit = requires() {
            { CRTP::__exit__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_aenter = requires() {
            { CRTP::__aenter__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

        template <typename CRTP>
        concept has_aexit = requires() {
            { CRTP::__aexit__ } -> std::convertible_to<
                PyObject*(*)(CRTP*, PyObject*, PyObject*, PyObject*)
            >;
        };

        template <typename CRTP>
        concept has_reversed = requires() {
            { CRTP::__reversed__ } -> std::convertible_to<PyObject*(*)(CRTP*)>;
        };

    }

    /* Marks a py::Object subclass as a type object, and exposes several helpers to
    make writing custom types as easy as possible.  Every specialization of `Type<>`
    should inherit from this class and implement a public `__python__` struct that
    inherits from its CRTP helpers. */
    struct TypeTag : BertrandTag {
    protected:

        template <typename... Ts>
        struct Visitor : Ts... {
            using Ts::operator()...;
        };

        template <typename Begin, std::sentinel_for<Begin> End>
        struct Iterator {
            PyObject_HEAD
            Begin begin;
            End end;

            static void __dealloc__(Iterator* self) {
                PyObject_GC_UnTrack(self);
                self->begin.~Begin();
                self->end.~End();
                PyTypeObject* type = Py_TYPE(self);
                type->tp_free(self);
                Py_DECREF(type);
            }

            static int __traverse__(Iterator* self, visitproc visit, void* arg) {
                Py_VISIT(Py_TYPE(self));
                return 0;
            }

            static PyObject* __next__(Iterator* self) {
                try {
                    if (self->begin == self->end) {
                        return nullptr;
                    }
                    if constexpr (std::is_lvalue_reference_v<decltype(*(self->begin))>) {
                        auto result = wrap(*(self->begin));  // non-owning obj
                        ++(self->begin);
                        return release(result);
                    } else {
                        auto result = as_object(*(self->begin));  // owning obj
                        ++(self->begin);
                        return release(result);
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

        /* Shared behavior for all Python types, be they wrappers around external C++
        classes or pure Python, possibly even implemented inline within the CRTP class
        itself.  Each type must minimally support these methods.  Everything else is an
        implementation detail. */
        template <typename CRTP, typename Wrapper, typename CppType>
        struct BaseDef : BertrandTag {
        protected:

            template <StaticStr ModName>
            struct Bindings;

        public:

            /* Generate a new instance of the type object to be attached to the given
            module.

            The `bindings` argument is a helper that includes a number of convenience
            methods for exposing the type to Python.  The `__export__` script must end
            with a call to `bindings.finalize<Bases...>()`, which will build a
            corresponding `PyType_Spec` that can be used to instantiate a unique heap
            type for each module.  This is the correct way to expose types leveraging
            per-module state, which allows for multiple sub-interpreters to run in
            parallel without interfering with one other (and possibly without a central
            GIL).  The bindings are automatically executed by the import system
            whenever the enclosing module is loaded into a fresh interpreter.

            Any attributes added at this stage are guaranteed be unique for each
            module.  It's still up to the user to make sure that any data they
            reference does not cause race conditions with other modules, but the
            modules themselves will not impede this.

            Note that this method does not actually attach the type to the module -
            that is done in the module's `__export__` script, which is semantically
            identical to this method and even provides much of the same syntax. */
            template <StaticStr ModName>
            static Type<Wrapper> __export__(Bindings<ModName> bindings) {
                // bindings.var<"foo">(value);
                // bindings.method<"bar">("docstring", &CRTP::bar);
                // ...
                return bindings.template finalize<Object>();
            }

            /* Return a new reference to the type by importing its parent module and
            unpacking this specific type.

            This is called automatically by the default constructor for
            `Type<Wrapper>`, and is the proper way to handle per-module state, instead
            of using static type objects.  It will implicitly invoke the `__export__()`
            method if the module was not previously loaded, either via a
            `PyImport_Import()` API call or the simplified `Module` constructor. */
            static Type<Wrapper> __import__() {
                throw NotImplementedError(
                    "the __import__() method must be defined for all Python types: "
                    + demangle(typeid(CRTP).name())
                );
            }

        };

        template <typename CRTP, typename Wrapper, typename CppType>
        struct DetectHashable : BaseDef<CRTP, Wrapper, CppType> {};
        template <typename CRTP, typename Wrapper, hashable CppType>
        struct DetectHashable<CRTP, Wrapper, CppType>;

        template <typename CRTP, typename Wrapper, typename CppType>
        struct DetectForwardIterable : DetectHashable<CRTP, Wrapper, CppType> {};
        template <typename CRTP, typename Wrapper, iterable CppType>
        struct DetectForwardIterable<CRTP, Wrapper, CppType>;

        template <typename CRTP, typename Wrapper, typename CppType>
        struct DetectReverseIterable : DetectForwardIterable<CRTP, Wrapper, CppType> {};
        template <typename CRTP, typename Wrapper, reverse_iterable CppType>
        struct DetectReverseIterable<CRTP, Wrapper, CppType>;

        template <typename CRTP, typename Wrapper, typename CppType>
        struct DetectCallable : DetectReverseIterable<CRTP, Wrapper, CppType> {};
        template <typename CRTP, typename Wrapper, has_call_operator CppType>
        struct DetectCallable<CRTP, Wrapper, CppType>;

        /* A CRTP base class that automatically generates boilerplate bindings for a
        new Python type which wraps around an external C++ type.  This is the preferred
        way of writing new Python types in C++.

        This class is only available to specializations of `py::Type<>`, each of which
        should define a public `__python__` struct that inherits from it via the CRTP
        pattern.  It uses template metaprogramming to automate the creation of Python
        types and their associated methods, slots, and other attributes in a way that
        conforms to Python best practices.  This includes:

            -   The use of heap types over static types, which are closer to native
                Python and can be mutated at runtime.
            -   Per-module (rather than per-process) state, which allows for multiple
                sub-interpreters to run in parallel.
            -   Cyclic GC support, which is necessary for types that contain Python
                objects, as well as the heap type itself.
            -   Direct access to the wrapped C++ object, which can be a non-owning,
                mutable or immutable reference, allowing for shared state across the
                language boundary.
            -   Default implementations of common slots like `__repr__`, `__hash__`,
                `__iter__`, and others, which can be detected via template
                metaprogramming and naturally overridden by the user if necessary.
            -   A shared metaclass, which demangles the C++ type name and delegates
                `isinstance()` and `issubclass()` checks to the `py::__isinstance__<>`
                and `py::__issubclass__<>` control structs, respectively.
            -   Support for C++ templates, which can be indexed in Python using the
                metaclass's `__getitem__` slot.
            -   Method overloading, wherein each method is represented as an overload
                set attached to the type object via the descriptor protocol.
            -   Modeling for member and static methods and variables, which are
                translated 1:1 into Python equivalents.

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
        library.

        Users of this type must at least implement the `__import__()` hook, which
        returns a new reference to the type object, wherever it is defined.  If the
        type is defined inline within this class, then the user must also implement the
        `__export__()` hook, which is called to initialize the type and expose it to
        Python.  Everything else is an implementation detail. */
        template <typename CRTP, typename Wrapper>
        struct def<CRTP, Wrapper, void> : BaseDef<CRTP, Wrapper, void> {
        protected:

            template <StaticStr ModName>
            struct Bindings;

        public:
            static constexpr Origin __origin__ = Origin::PYTHON;

            /* Default __export__() script for pure-Python types.  Forwards to
            __import__() in order to simplify binding standard library types as well as
            references to C-style Python types that may be externally defined.
            
            Inline types should implement __export__() like normal. */
            template <StaticStr ModName>
            static Type<Wrapper> __export__(Bindings<ModName> bindings) {
                return CRTP::__import__();
            }

        };

    };

}


////////////////////
////    TYPE    ////
////////////////////


template <>
struct Type<Handle>;
template <>
struct Type<Object>;


template <>
struct Interface<Type<Handle>> {};
template <>
struct Interface<Type<Object>> {};


/* `Type<Handle>` is implemented for completeness, but is identical to `Type<Object>`
(the default implementation). */
template <>
struct Type<Handle> : Object, Interface<Type<Handle>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(
                &PyBaseObject_Type
            ));
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


/* `Type<Object>` (the default specialization) refers to a dynamic type, which can be
used polymorphically to represent the type of any Python object.  More specialized
types can always be implicitly converted to this type, but doing the opposite incurs a
runtime `issubclass()` check, and can potentially raise a `TypeError`, just like
`py::Object`. */
template <>
struct Type<Object> : Object, Interface<Type<Object>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(
                &PyBaseObject_Type
            ));
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


/* Allow py::Type<> to be templated on arbitrary C++ types by redirecting to its Python
equivalent (py::Type<int>() == py::Type<py::Int>()). */
template <typename T> requires (__as_object__<T>::enable)
struct Type<T> : Type<typename __as_object__<T>::type> {};


/* Explicitly call py::Type(obj) to deduce the Python type of an arbitrary object. */
template <typename T> requires (__as_object__<T>::enable)
explicit Type(const T&) -> Type<typename __as_object__<T>::type>;


/* All types are default-constructible by invoking the `__import__()` method, which
ensures that the result always matches the current state of the Python interpreter. */
template <typename T>
struct __init__<Type<T>> : Returns<Type<T>> {
    static auto operator()() {
        return Type<T>::__python__::__import__();
    }
};


/* Metaclasses are represented as types of types, and are constructible by recursively
calling `Py_TYPE(cls)` on the inner type until we reach a concrete implementation. */
template <typename T>
struct __init__<Type<Type<T>>> : Returns<Type<Type<T>>> {
    static auto operator()() {
        return reinterpret_borrow<Type<Type<T>>>(reinterpret_cast<PyObject*>(
            Py_TYPE(ptr(Type<T>()))
        ));
    }
};


/* Implement the CTAD guide by default-initializing the corresponding py::Type. */
template <typename T> requires (__as_object__<T>::enable)
struct __explicit_init__<Type<typename __as_object__<T>::type>, T> :
    Returns<Type<typename __as_object__<T>::type>>
{
    static auto operator()(const T& obj) {
        return Type<typename __as_object__<T>::type>();
    }
};


/// NOTE: additional metaclass constructors for py::Type are defined in common.h


/* Calling a py::Type is the same as invoking the inner type's constructor. */
template <typename T, typename... Args> requires (std::constructible_from<T, Args...>)
struct __call__<Type<T>, Args...> : Returns<T> {
    static auto operator()(const Type<T>& self, Args&&... args) {
        return T(std::forward<Args>(args)...);
    }
};


/* `isinstance()` is already implemented for all `py::Type` specializations by
recurring on the templated type. */
template <typename T, typename Cls>
struct __isinstance__<T, Type<Cls>> : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::python_like<T>) {
            if (!PyType_Check(ptr(obj))) {
                return false;
            }
            if constexpr (impl::dynamic_type<Cls>) {
                return true;
            } else {
                int result = PyObject_IsSubclass(
                    ptr(obj),
                    ptr(Type<Cls>())
                );
                if (result < 0) {
                    Exception::from_python();
                }
                return result;
            }
        } else {
            return false;
        }
    }
    static constexpr bool operator()(const T& obj, const Type<Cls>& cls) {
        if constexpr (impl::python_like<T> && impl::dynamic_type<Cls>) {
            int result = PyObject_IsSubclass(
                ptr(obj),
                ptr(cls)
            );
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            return isinstance<Cls>(obj);
        }
    }
};


/* `issubclass()` is already implemented for all `py::Type` specializations by
recurring on the templated type. */
template <typename T, typename Cls>
struct __issubclass__<T, Type<Cls>> : Returns<bool> {
    static consteval bool operator()() { return issubclass<T, Cls>(); }
    static constexpr bool operator()(const T& obj) { return issubclass<Cls>(obj); }
    static constexpr bool operator()(const T& obj, const Type<Cls>& cls) {
        if constexpr (impl::dynamic_type<Cls>) {
            int result = PyObject_IsSubclass(
                ptr(as_object(obj)),
                ptr(cls)
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        } else {
            return issubclass<Cls>(obj);
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
struct __cast__<Type<From>, Type<To>> : Returns<Type<To>> {
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
struct __cast__<Type<From>, Type<To>> : Returns<Type<To>> {
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


/// NOTE: forward the BertrandMeta interface below if the type originates from C++.


////////////////////
////    META    ////
////////////////////


template <>
struct Type<BertrandMeta>;


template <>
struct Interface<BertrandMeta> {};
template <>
struct Interface<Type<BertrandMeta>> {};


/* An instance of Bertrand's metatype, which is used to represent all C++ types that
have been exposed to Python. */
struct BertrandMeta : Object, Interface<BertrandMeta> {

    BertrandMeta(Handle h, borrowed_t t) : Object(h, t) {}
    BertrandMeta(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<BertrandMeta>::enable<Args...>)
    BertrandMeta(Args&&... args) : Object(
        implicit_ctor<BertrandMeta>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<BertrandMeta>::enable<Args...>)
    explicit BertrandMeta(Args&&... args) : Object(
        explicit_ctor<BertrandMeta>{},
        std::forward<Args>(args)...
    ) {}

};


/* Bertrand's metatype, which is used to expose C++ classes to Python and simplify the
binding process. */
template <>
struct Type<BertrandMeta> : Object, Interface<Type<BertrandMeta>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, BertrandMeta> {
        static constexpr StaticStr __doc__ =
R"doc(A shared metaclass for all Bertrand extension types.

Notes
-----
This metaclass identifies all types that originate from C++.  It is used to
automatically demangle the type name, provide accurate `isinstance()` and
`issubclass()` checks based on Bertrand's C++ control structures, allow C++ template
subscription via `__getitem__`, and provide a common interface for class-level property
descriptors, method overloading (including for built-in slots), and other common
behaviors.

Performing an `isinstance()` check against this type will return `True` if and only if
the candidate type is implemented in C++.)doc";

        PyTypeObject base;

        /* The `instancecheck` and `subclasscheck` function pointers are used to back
        the metaclass's Python-level `__instancecheck__()` and `__subclasscheck()__`
        methods, and are automatically set during construction for each type. */
        bool(*instancecheck)(__python__*, PyObject*);
        bool(*subclasscheck)(__python__*, PyObject*);

        /* C++ needs a few extra hooks to accurately model C++ types in a way that
        shares state.  The getters and setters decribed here are implemented as
        `tp_getset` slots on the metaclass, and describe class-level properties. */
        struct Get {
            PyObject*(*func)(PyObject*, void*);
            void* closure;
        };
        struct Set {
            int(*func)(PyObject*, PyObject*, void*);
            void* closure;
        };
        using Getters = std::unordered_map<std::string_view, Get>;
        using Setters = std::unordered_map<std::string_view, Set>;
        Getters getters;
        Setters setters;

        /* The metaclass will demangle the C++ class name and track template
        instantiations in an internal dictionary accessible through `__getitem__()`. */
        PyObject* demangled;
        PyObject* template_instantiations;

        /* The bindings will create thin wrappers around all of the slots that are
        accessible from Python, which delegate to an internal `py::OverloadSet`.  This
        allows overloads to be registered from either side of the language divide just
        like any other bertrand function, without incurring any additional overhead. */
        PyObject* tp_init;
        PyObject* tp_new;
        PyObject* tp_getattro;
        PyObject* tp_setattro;
        PyObject* tp_delattro;
        PyObject* tp_repr;
        PyObject* tp_hash;
        PyObject* tp_call;
        PyObject* tp_str;
        PyObject* tp_lt;
        PyObject* tp_le;
        PyObject* tp_eq;
        PyObject* tp_ne;
        PyObject* tp_ge;
        PyObject* tp_gt;
        PyObject* tp_iter;
        PyObject* tp_iternext;
        PyObject* tp_reverse;
        PyObject* tp_enter;
        PyObject* tp_exit;
        PyObject* tp_descr_get;
        PyObject* tp_descr_set;
        PyObject* tp_descr_delete;
        PyObject* mp_length;
        PyObject* mp_subscript;
        PyObject* mp_ass_subscript;
        PyObject* mp_del_subscript;
        PyObject* sq_contains;
        PyObject* am_await;
        PyObject* am_aiter;
        PyObject* am_anext;
        PyObject* am_send;
        PyObject* bf_getbuffer;
        PyObject* bf_releasebuffer;
        PyObject* nb_add;
        PyObject* nb_inplace_add;
        PyObject* nb_subtract;
        PyObject* nb_inplace_subtract;
        PyObject* nb_multiply;
        PyObject* nb_inplace_multiply;
        PyObject* nb_remainder;
        PyObject* nb_inplace_remainder;
        PyObject* nb_divmod;
        PyObject* nb_power;
        PyObject* nb_inplace_power;
        PyObject* nb_negative;
        PyObject* nb_positive;
        PyObject* nb_absolute;
        PyObject* nb_bool;
        PyObject* nb_invert;
        PyObject* nb_lshift;
        PyObject* nb_inplace_lshift;
        PyObject* nb_rshift;
        PyObject* nb_inplace_rshift;
        PyObject* nb_and;
        PyObject* nb_inplace_and;
        PyObject* nb_xor;
        PyObject* nb_inplace_xor;
        PyObject* nb_or;
        PyObject* nb_inplace_or;
        PyObject* nb_int;
        PyObject* nb_float;
        PyObject* nb_floor_divide;
        PyObject* nb_inplace_floor_divide;
        PyObject* nb_true_divide;
        PyObject* nb_inplace_true_divide;
        PyObject* nb_matrix_multiply;
        PyObject* nb_inplace_matrix_multiply;

        /* If no OverloadSet exists for a given slot, then we fall back to what Python
        would have used as the default behavior had we not overridden the slot. */
        initproc base_tp_init;
        newfunc base_tp_new;
        getattrofunc base_tp_getattro;
        setattrofunc base_tp_setattro;
        reprfunc base_tp_repr;
        hashfunc base_tp_hash;
        ternaryfunc base_tp_call;
        reprfunc base_tp_str;
        richcmpfunc base_tp_richcompare;
        getiterfunc base_tp_iter;
        iternextfunc base_tp_iternext;
        descrgetfunc base_tp_descr_get;
        descrsetfunc base_tp_descr_set;
        lenfunc base_mp_length;
        binaryfunc base_mp_subscript;
        objobjargproc base_mp_ass_subscript;
        objobjargproc base_sq_contains;
        unaryfunc base_am_await;
        unaryfunc base_am_aiter;
        unaryfunc base_am_anext;
        sendfunc base_am_send;
        getbufferproc base_bf_getbuffer;
        releasebufferproc base_bf_releasebuffer;
        binaryfunc base_nb_add;
        binaryfunc base_nb_inplace_add;
        binaryfunc base_nb_subtract;
        binaryfunc base_nb_inplace_subtract;
        binaryfunc base_nb_multiply;
        binaryfunc base_nb_inplace_multiply;
        binaryfunc base_nb_remainder;
        binaryfunc base_nb_inplace_remainder;
        binaryfunc base_nb_divmod;
        ternaryfunc base_nb_power;
        ternaryfunc base_nb_inplace_power;
        unaryfunc base_nb_negative;
        unaryfunc base_nb_positive;
        unaryfunc base_nb_absolute;
        inquiry base_nb_bool;
        unaryfunc base_nb_invert;
        binaryfunc base_nb_lshift;
        binaryfunc base_nb_inplace_lshift;
        binaryfunc base_nb_rshift;
        binaryfunc base_nb_inplace_rshift;
        binaryfunc base_nb_and;
        binaryfunc base_nb_inplace_and;
        binaryfunc base_nb_xor;
        binaryfunc base_nb_inplace_xor;
        binaryfunc base_nb_or;
        binaryfunc base_nb_inplace_or;
        unaryfunc base_nb_int;
        unaryfunc base_nb_float;
        binaryfunc base_nb_floor_divide;
        binaryfunc base_nb_inplace_floor_divide;
        binaryfunc base_nb_true_divide;
        binaryfunc base_nb_inplace_true_divide;
        binaryfunc base_nb_matrix_multiply;
        binaryfunc base_nb_inplace_matrix_multiply;

        /* Get a new reference to the metatype from the global `bertrand.python`
        module. */
        static Type __import__();  // TODO: defined in __init__.h alongside "bertrand.python" module {
        //     return impl::get_type<BertrandMeta>(Module<"bertrand.python">());
        // }

        /* The metaclass can't use Bindings, since they depend on the metaclass to
        function.  The logic is fundamentally the same, however. */
        template <StaticStr ModName>
        static Type __export__(Module<ModName> module) {
            static PyType_Slot slots[] = {
                {Py_tp_doc, const_cast<char*>(__doc__.buffer)},
                {Py_tp_dealloc, reinterpret_cast<void*>(__dealloc__)},
                {Py_tp_repr, reinterpret_cast<void*>(__repr__)},
                {Py_tp_getattro, reinterpret_cast<void*>(__getattr__)},
                {Py_tp_setattro, reinterpret_cast<void*>(__setattr__)},
                {Py_tp_iter, reinterpret_cast<void*>(__iter__)},
                {Py_mp_length, reinterpret_cast<void*>(__len__)},
                {Py_mp_subscript, reinterpret_cast<void*>(__getitem__)},
                {Py_tp_methods, methods},
                {Py_tp_getset, getset},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = ModName + ".Meta",
                .basicsize = sizeof(BertrandMeta),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_HAVE_GC |
                    Py_TPFLAGS_MANAGED_WEAKREF | Py_TPFLAGS_MANAGED_DICT |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_TYPE_SUBCLASS,
                .slots = slots
            };
            PyObject* cls = PyType_FromModuleAndSpec(
                ptr(module),
                &spec,
                &PyType_Type
            );
            if (cls == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Type>(cls);
        }

        /* Initialize the metaclass's internal fields after calling the type's Python
        __new__() hook.  Ordinarily, these are left uninitialized, which leads to
        undefined behavior when accessed. */
        void initialize() {
            instancecheck = nullptr;
            subclasscheck = nullptr;
            new (&getters) Getters();
            new (&setters) Setters();
            std::string s = "<class '" + impl::demangle(base.tp_name) + "'>";
            demangled = PyUnicode_FromStringAndSize(s.c_str(), s.size());
            if (demangled == nullptr) {
                Exception::from_python();
            }
            template_instantiations = nullptr;
            tp_init = nullptr;
            tp_new = nullptr;
            tp_getattro = nullptr;
            tp_setattro = nullptr;
            tp_delattro = nullptr;
            tp_repr = nullptr;
            tp_hash = nullptr;
            tp_call = nullptr;
            tp_str = nullptr;
            tp_lt = nullptr;
            tp_le = nullptr;
            tp_eq = nullptr;
            tp_ne = nullptr;
            tp_ge = nullptr;
            tp_gt = nullptr;
            tp_iter = nullptr;
            tp_iternext = nullptr;
            tp_reverse = nullptr;
            tp_enter = nullptr;
            tp_exit = nullptr;
            tp_descr_get = nullptr;
            tp_descr_set = nullptr;
            tp_descr_delete = nullptr;
            mp_length = nullptr;
            mp_subscript = nullptr;
            mp_ass_subscript = nullptr;
            mp_del_subscript = nullptr;
            sq_contains = nullptr;
            am_await = nullptr;
            am_aiter = nullptr;
            am_anext = nullptr;
            am_send = nullptr;
            bf_getbuffer = nullptr;
            bf_releasebuffer = nullptr;
            nb_add = nullptr;
            nb_inplace_add = nullptr;
            nb_subtract = nullptr;
            nb_inplace_subtract = nullptr;
            nb_multiply = nullptr;
            nb_inplace_multiply = nullptr;
            nb_remainder = nullptr;
            nb_inplace_remainder = nullptr;
            nb_divmod = nullptr;
            nb_power = nullptr;
            nb_inplace_power = nullptr;
            nb_negative = nullptr;
            nb_positive = nullptr;
            nb_absolute = nullptr;
            nb_bool = nullptr;
            nb_invert = nullptr;
            nb_lshift = nullptr;
            nb_inplace_lshift = nullptr;
            nb_rshift = nullptr;
            nb_inplace_rshift = nullptr;
            nb_and = nullptr;
            nb_inplace_and = nullptr;
            nb_xor = nullptr;
            nb_inplace_xor = nullptr;
            nb_or = nullptr;
            nb_inplace_or = nullptr;
            nb_int = nullptr;
            nb_float = nullptr;
            nb_floor_divide = nullptr;
            nb_inplace_floor_divide = nullptr;
            nb_true_divide = nullptr;
            nb_inplace_true_divide = nullptr;
            nb_matrix_multiply = nullptr;
            nb_inplace_matrix_multiply = nullptr;
            base_tp_init = nullptr;
            base_tp_new = nullptr;
            base_tp_getattro = nullptr;
            base_tp_setattro = nullptr;
            base_tp_repr = nullptr;
            base_tp_hash = nullptr;
            base_tp_call = nullptr;
            base_tp_str = nullptr;
            base_tp_richcompare = nullptr;
            base_tp_iter = nullptr;
            base_tp_iternext = nullptr;
            base_tp_descr_get = nullptr;
            base_tp_descr_set = nullptr;
            base_mp_length = nullptr;
            base_mp_subscript = nullptr;
            base_mp_ass_subscript = nullptr;
            base_sq_contains = nullptr;
            base_am_await = nullptr;
            base_am_aiter = nullptr;
            base_am_anext = nullptr;
            base_am_send = nullptr;
            base_bf_getbuffer = nullptr;
            base_bf_releasebuffer = nullptr;
            base_nb_add = nullptr;
            base_nb_inplace_add = nullptr;
            base_nb_subtract = nullptr;
            base_nb_inplace_subtract = nullptr;
            base_nb_multiply = nullptr;
            base_nb_inplace_multiply = nullptr;
            base_nb_remainder = nullptr;
            base_nb_inplace_remainder = nullptr;
            base_nb_divmod = nullptr;
            base_nb_power = nullptr;
            base_nb_inplace_power = nullptr;
            base_nb_negative = nullptr;
            base_nb_positive = nullptr;
            base_nb_absolute = nullptr;
            base_nb_bool = nullptr;
            base_nb_invert = nullptr;
            base_nb_lshift = nullptr;
            base_nb_inplace_lshift = nullptr;
            base_nb_rshift = nullptr;
            base_nb_inplace_rshift = nullptr;
            base_nb_and = nullptr;
            base_nb_inplace_and = nullptr;
            base_nb_xor = nullptr;
            base_nb_inplace_xor = nullptr;
            base_nb_or = nullptr;
            base_nb_inplace_or = nullptr;
            base_nb_int = nullptr;
            base_nb_float = nullptr;
            base_nb_floor_divide = nullptr;
            base_nb_inplace_floor_divide = nullptr;
            base_nb_true_divide = nullptr;
            base_nb_inplace_true_divide = nullptr;
            base_nb_matrix_multiply = nullptr;
            base_nb_inplace_matrix_multiply = nullptr;
        }

        /* Create a trivial instance of the metaclass to serve as a Python entry point
        for a C++ template hierarchy.  The interface type is not usable on its own
        except to provide access to its C++ instantiations, as well as efficient type
        checks against them and a central point for documentation. */
        template <StaticStr Name, typename Cls, StaticStr ModName>
        static BertrandMeta stub_type(Module<ModName>& module) {
            static std::string docstring;
            static bool doc_initialized = false;
            if (!doc_initialized) {
                docstring = "A public interface for the '" + ModName + "." + Name;
                docstring += "' template hierarchy.\n\n";
                docstring +=
R"doc(This type cannot be used directly, but indexing it with one or more Python
types allows access to individual instantiations with the same syntax as C++.
Note that due to its dynamic nature, Python cannot create any new instantiations
of a C++ template - this class merely navigates the existing instantiations at
the C++ level and retrieves their corresponding Python types.)doc";
                doc_initialized = true;
            }
            static PyType_Slot slots[] = {
                {Py_tp_doc, const_cast<char*>(docstring.c_str())},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = Name,
                .basicsize = sizeof(__python__),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
                    Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_DISALLOW_INSTANTIATION |
                    Py_TPFLAGS_IMMUTABLETYPE,
                .slots = slots
            };

            __python__* cls = reinterpret_cast<__python__*>(PyType_FromMetaclass(
                reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>())),
                ptr(module),
                &spec,
                nullptr
            ));
            if (cls == nullptr) {
                Exception::from_python();
            }
            try {
                cls->initialize();
                cls->instancecheck = template_instancecheck;
                cls->subclasscheck = template_subclasscheck;
                cls->template_instantiations = PyDict_New();
                if (cls->template_instantiations == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<BertrandMeta>(reinterpret_cast<PyObject*>(cls));
            } catch (...) {
                Py_DECREF(cls);
                throw;
            }
            
        };

        /* `repr(cls)` displays the demangled C++ name.  */
        static PyObject* __repr__(__python__* cls) {
            return Py_XNewRef(cls->demangled);
        }

        /* `len(cls)` yields the number of template instantiations registered to this
        type.  This will always succeed: testing `if cls` in Python is sufficient to
        determine whether it is a template interface that requires instantiation. */
        static Py_ssize_t __len__(__python__* cls) {
            return cls->template_instantiations ?
                PyDict_Size(cls->template_instantiations) : 0;
        }

        /* `iter(cls)` yields individual template instantiations in the order in which
        they were defined. */
        static PyObject* __iter__(__python__* cls) {
            if (cls->template_instantiations == nullptr) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%U' has no template instantiations",
                    cls->demangled
                );
                return nullptr;
            }
            return PyObject_GetIter(cls->template_instantiations);
        }

        /* `cls[]` allows navigation of the C++ template hierarchy. */
        static PyObject* __getitem__(__python__* cls, PyObject* key) {
            if (cls->template_instantiations == nullptr) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%U' has no template instantiations",
                    cls->demangled
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
                PyObject* value = PyDict_GetItem(cls->template_instantiations, key);
                if (value == nullptr) {
                    const char* demangled = PyUnicode_AsUTF8(cls->demangled);
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

        /// TODO: will this __doc__ slot be overridden by docs on instances of the
        /// metaclass?

        /* The metaclass's __doc__ string defaults to a getset descriptor that appends
        the type's template instantiations to the normal `tp_doc` slot. */
        static PyObject* doc(__python__* cls, void*) {
            std::string doc = cls->base.tp_doc;
            if (cls->template_instantiations) {
                doc += "\n\n";
                std::string header = "Instantations (" + std::to_string(PyDict_Size(
                    cls->template_instantiations
                )) + ")";
                doc += header + "\n" + std::string(header.size() - 1, '-') + "\n";

                std::string prefix = "    " + std::string(cls->base.tp_name) + "[";
                PyObject* key;
                PyObject* value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(
                    cls->template_instantiations,
                    &pos,
                    &key,
                    &value
                )) {
                    std::string key_string = prefix;
                    size_t i = 0;
                    size_t size = PyTuple_GET_SIZE(key);
                    while (i < size) {
                        PyObject* item = PyTuple_GET_ITEM(key, i);
                        int rc = PyObject_IsInstance(
                            item,
                            ptr(Type<BertrandMeta>())
                        );
                        if (rc < 0) {
                            Exception::from_python();
                        } else if (rc) {
                            const char* demangled_key = PyUnicode_AsUTF8(
                                reinterpret_cast<__python__*>(item)->demangled
                            );
                            if (demangled_key == nullptr) {
                                Exception::from_python();
                            }
                            doc += demangled_key;
                        } else {
                            doc += reinterpret_cast<PyTypeObject*>(item)->tp_name;
                        }
                        if (++i < size) {
                            doc += ", ";
                        }
                    }
                    key_string += "]";

                    const char* demangled_value = PyUnicode_AsUTF8(
                        reinterpret_cast<__python__*>(value)->demangled
                    );
                    if (demangled_value == nullptr) {
                        Exception::from_python();
                    }

                    doc += key_string + " -> " + demangled_value + "\n";
                }
            }
            return PyUnicode_FromStringAndSize(doc.c_str(), doc.size());
        }

        /* `cls.` allows access to class-level variables with shared state. */
        static PyObject* __getattr__(__python__* cls, PyObject* attr) {
            Py_ssize_t size;
            const char* name = PyUnicode_AsUTF8AndSize(attr, &size);
            if (name == nullptr) {
                return nullptr;
            }
            std::string_view view {name, static_cast<size_t>(size)};
            auto it = cls->getters.find(view);
            if (it != cls->getters.end()) {
                return it->second.func(
                    reinterpret_cast<PyObject*>(cls),
                    it->second.closure
                );
            }
            PyErr_Format(
                PyExc_AttributeError,
                "type object '%U' has no attribute '%s'",
                cls->demangled,
                name
            );
            return nullptr;
        }

        /* `cls. = ...` allows assignment to class-level variables with shared state. */
        static int __setattr__(__python__* cls, PyObject* attr, PyObject* value) {
            Py_ssize_t size;
            const char* name = PyUnicode_AsUTF8AndSize(attr, &size);
            if (name == nullptr) {
                return -1;
            }
            std::string_view view {name, static_cast<size_t>(size)};
            auto it = cls->setters.find(view);
            if (it != cls->setters.end()) {
                return it->second.func(
                    reinterpret_cast<PyObject*>(cls),
                    value,
                    it->second.closure
                );
            }
            PyErr_Format(
                PyExc_AttributeError,
                "type object '%U' has no attribute '%s'",
                cls->demangled,
                name
            );
            return -1;
        }

        /* isinstance(obj, cls) forwards the behavior of the __isinstance__ control
        struct and accounts for possible template instantiations. */
        static PyObject* __instancecheck__(__python__* cls, PyObject* instance) {
            try {
                return PyBool_FromLong(cls->instancecheck(cls, instance));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* issubclass(sub, cls) forwards the behavior of the __issubclass__ control
        struct and accounts for possible template instantiations. */
        static PyObject* __subclasscheck__(__python__* cls, PyObject* subclass) {
            try {
                return PyBool_FromLong(cls->subclasscheck(cls, subclass));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __traverse__(__python__* cls, visitproc visit, void* arg) {
            Py_VISIT(cls->demangled);
            Py_VISIT(cls->template_instantiations);
            Py_VISIT(cls->tp_init);
            Py_VISIT(cls->tp_new);
            Py_VISIT(cls->tp_getattro);
            Py_VISIT(cls->tp_setattro);
            Py_VISIT(cls->tp_delattro);
            Py_VISIT(cls->tp_repr);
            Py_VISIT(cls->tp_hash);
            Py_VISIT(cls->tp_call);
            Py_VISIT(cls->tp_str);
            Py_VISIT(cls->tp_lt);
            Py_VISIT(cls->tp_le);
            Py_VISIT(cls->tp_eq);
            Py_VISIT(cls->tp_ne);
            Py_VISIT(cls->tp_ge);
            Py_VISIT(cls->tp_gt);
            Py_VISIT(cls->tp_iter);
            Py_VISIT(cls->tp_iternext);
            Py_VISIT(cls->tp_reverse);
            Py_VISIT(cls->tp_enter);
            Py_VISIT(cls->tp_exit);
            Py_VISIT(cls->tp_descr_get);
            Py_VISIT(cls->tp_descr_set);
            Py_VISIT(cls->tp_descr_delete);
            Py_VISIT(cls->mp_length);
            Py_VISIT(cls->mp_subscript);
            Py_VISIT(cls->mp_ass_subscript);
            Py_VISIT(cls->mp_del_subscript);
            Py_VISIT(cls->sq_contains);
            Py_VISIT(cls->am_await);
            Py_VISIT(cls->am_aiter);
            Py_VISIT(cls->am_anext);
            Py_VISIT(cls->am_send);
            Py_VISIT(cls->bf_getbuffer);
            Py_VISIT(cls->bf_releasebuffer);
            Py_VISIT(cls->nb_add);
            Py_VISIT(cls->nb_inplace_add);
            Py_VISIT(cls->nb_subtract);
            Py_VISIT(cls->nb_inplace_subtract);
            Py_VISIT(cls->nb_multiply);
            Py_VISIT(cls->nb_inplace_multiply);
            Py_VISIT(cls->nb_remainder);
            Py_VISIT(cls->nb_inplace_remainder);
            Py_VISIT(cls->nb_divmod);
            Py_VISIT(cls->nb_power);
            Py_VISIT(cls->nb_inplace_power);
            Py_VISIT(cls->nb_negative);
            Py_VISIT(cls->nb_positive);
            Py_VISIT(cls->nb_absolute);
            Py_VISIT(cls->nb_bool);
            Py_VISIT(cls->nb_invert);
            Py_VISIT(cls->nb_lshift);
            Py_VISIT(cls->nb_inplace_lshift);
            Py_VISIT(cls->nb_rshift);
            Py_VISIT(cls->nb_inplace_rshift);
            Py_VISIT(cls->nb_and);
            Py_VISIT(cls->nb_inplace_and);
            Py_VISIT(cls->nb_xor);
            Py_VISIT(cls->nb_inplace_xor);
            Py_VISIT(cls->nb_or);
            Py_VISIT(cls->nb_inplace_or);
            Py_VISIT(cls->nb_int);
            Py_VISIT(cls->nb_float);
            Py_VISIT(cls->nb_floor_divide);
            Py_VISIT(cls->nb_inplace_floor_divide);
            Py_VISIT(cls->nb_true_divide);
            Py_VISIT(cls->nb_inplace_true_divide);
            Py_VISIT(cls->nb_matrix_multiply);
            Py_VISIT(cls->nb_inplace_matrix_multiply);
            Py_VISIT(Py_TYPE(cls));  // required for heap types
            return 0;
        }

        static int __clear__(__python__* cls) {
            Py_CLEAR(cls->demangled);
            Py_CLEAR(cls->template_instantiations);
            Py_CLEAR(cls->tp_init);
            Py_CLEAR(cls->tp_new);
            Py_CLEAR(cls->tp_getattro);
            Py_CLEAR(cls->tp_setattro);
            Py_CLEAR(cls->tp_delattro);
            Py_CLEAR(cls->tp_repr);
            Py_CLEAR(cls->tp_hash);
            Py_CLEAR(cls->tp_call);
            Py_CLEAR(cls->tp_str);
            Py_CLEAR(cls->tp_lt);
            Py_CLEAR(cls->tp_le);
            Py_CLEAR(cls->tp_eq);
            Py_CLEAR(cls->tp_ne);
            Py_CLEAR(cls->tp_ge);
            Py_CLEAR(cls->tp_gt);
            Py_CLEAR(cls->tp_iter);
            Py_CLEAR(cls->tp_iternext);
            Py_CLEAR(cls->tp_reverse);
            Py_CLEAR(cls->tp_enter);
            Py_CLEAR(cls->tp_exit);
            Py_CLEAR(cls->tp_descr_get);
            Py_CLEAR(cls->tp_descr_set);
            Py_CLEAR(cls->tp_descr_delete);
            Py_CLEAR(cls->mp_length);
            Py_CLEAR(cls->mp_subscript);
            Py_CLEAR(cls->mp_ass_subscript);
            Py_CLEAR(cls->mp_del_subscript);
            Py_CLEAR(cls->sq_contains);
            Py_CLEAR(cls->am_await);
            Py_CLEAR(cls->am_aiter);
            Py_CLEAR(cls->am_anext);
            Py_CLEAR(cls->am_send);
            Py_CLEAR(cls->bf_getbuffer);
            Py_CLEAR(cls->bf_releasebuffer);
            Py_CLEAR(cls->nb_add);
            Py_CLEAR(cls->nb_inplace_add);
            Py_CLEAR(cls->nb_subtract);
            Py_CLEAR(cls->nb_inplace_subtract);
            Py_CLEAR(cls->nb_multiply);
            Py_CLEAR(cls->nb_inplace_multiply);
            Py_CLEAR(cls->nb_remainder);
            Py_CLEAR(cls->nb_inplace_remainder);
            Py_CLEAR(cls->nb_divmod);
            Py_CLEAR(cls->nb_power);
            Py_CLEAR(cls->nb_inplace_power);
            Py_CLEAR(cls->nb_negative);
            Py_CLEAR(cls->nb_positive);
            Py_CLEAR(cls->nb_absolute);
            Py_CLEAR(cls->nb_bool);
            Py_CLEAR(cls->nb_invert);
            Py_CLEAR(cls->nb_lshift);
            Py_CLEAR(cls->nb_inplace_lshift);
            Py_CLEAR(cls->nb_rshift);
            Py_CLEAR(cls->nb_inplace_rshift);
            Py_CLEAR(cls->nb_and);
            Py_CLEAR(cls->nb_inplace_and);
            Py_CLEAR(cls->nb_xor);
            Py_CLEAR(cls->nb_inplace_xor);
            Py_CLEAR(cls->nb_or);
            Py_CLEAR(cls->nb_inplace_or);
            Py_CLEAR(cls->nb_int);
            Py_CLEAR(cls->nb_float);
            Py_CLEAR(cls->nb_floor_divide);
            Py_CLEAR(cls->nb_inplace_floor_divide);
            Py_CLEAR(cls->nb_true_divide);
            Py_CLEAR(cls->nb_inplace_true_divide);
            Py_CLEAR(cls->nb_matrix_multiply);
            Py_CLEAR(cls->nb_inplace_matrix_multiply);
            return 0;
        }

        static void __dealloc__(__python__* cls) {
            PyObject_GC_UnTrack(cls);
            cls->getters.~Getters();
            cls->setters.~Setters();
            Py_XDECREF(cls->demangled);
            Py_XDECREF(cls->template_instantiations);
            Py_XDECREF(cls->tp_init);
            Py_XDECREF(cls->tp_new);
            Py_XDECREF(cls->tp_getattro);
            Py_XDECREF(cls->tp_setattro);
            Py_XDECREF(cls->tp_delattro);
            Py_XDECREF(cls->tp_repr);
            Py_XDECREF(cls->tp_hash);
            Py_XDECREF(cls->tp_call);
            Py_XDECREF(cls->tp_str);
            Py_XDECREF(cls->tp_lt);
            Py_XDECREF(cls->tp_le);
            Py_XDECREF(cls->tp_eq);
            Py_XDECREF(cls->tp_ne);
            Py_XDECREF(cls->tp_ge);
            Py_XDECREF(cls->tp_gt);
            Py_XDECREF(cls->tp_iter);
            Py_XDECREF(cls->tp_iternext);
            Py_XDECREF(cls->tp_reverse);
            Py_XDECREF(cls->tp_enter);
            Py_XDECREF(cls->tp_exit);
            Py_XDECREF(cls->tp_descr_get);
            Py_XDECREF(cls->tp_descr_set);
            Py_XDECREF(cls->tp_descr_delete);
            Py_XDECREF(cls->mp_length);
            Py_XDECREF(cls->mp_subscript);
            Py_XDECREF(cls->mp_ass_subscript);
            Py_XDECREF(cls->mp_del_subscript);
            Py_XDECREF(cls->sq_contains);
            Py_XDECREF(cls->am_await);
            Py_XDECREF(cls->am_aiter);
            Py_XDECREF(cls->am_anext);
            Py_XDECREF(cls->am_send);
            Py_XDECREF(cls->bf_getbuffer);
            Py_XDECREF(cls->bf_releasebuffer);
            Py_XDECREF(cls->nb_add);
            Py_XDECREF(cls->nb_inplace_add);
            Py_XDECREF(cls->nb_subtract);
            Py_XDECREF(cls->nb_inplace_subtract);
            Py_XDECREF(cls->nb_multiply);
            Py_XDECREF(cls->nb_inplace_multiply);
            Py_XDECREF(cls->nb_remainder);
            Py_XDECREF(cls->nb_inplace_remainder);
            Py_XDECREF(cls->nb_divmod);
            Py_XDECREF(cls->nb_power);
            Py_XDECREF(cls->nb_inplace_power);
            Py_XDECREF(cls->nb_negative);
            Py_XDECREF(cls->nb_positive);
            Py_XDECREF(cls->nb_absolute);
            Py_XDECREF(cls->nb_bool);
            Py_XDECREF(cls->nb_invert);
            Py_XDECREF(cls->nb_lshift);
            Py_XDECREF(cls->nb_inplace_lshift);
            Py_XDECREF(cls->nb_rshift);
            Py_XDECREF(cls->nb_inplace_rshift);
            Py_XDECREF(cls->nb_and);
            Py_XDECREF(cls->nb_inplace_and);
            Py_XDECREF(cls->nb_xor);
            Py_XDECREF(cls->nb_inplace_xor);
            Py_XDECREF(cls->nb_or);
            Py_XDECREF(cls->nb_inplace_or);
            Py_XDECREF(cls->nb_int);
            Py_XDECREF(cls->nb_float);
            Py_XDECREF(cls->nb_floor_divide);
            Py_XDECREF(cls->nb_inplace_floor_divide);
            Py_XDECREF(cls->nb_true_divide);
            Py_XDECREF(cls->nb_inplace_true_divide);
            Py_XDECREF(cls->nb_matrix_multiply);
            Py_XDECREF(cls->nb_inplace_matrix_multiply);
            PyTypeObject* type = Py_TYPE(cls);
            type->tp_free(cls);
            Py_DECREF(type);  // required for heap types
        }

    private:

        /* A candidate for the `instancecheck` function pointer to be used for stub
        types. */
        static bool template_instancecheck(__python__* cls, PyObject* instance) {
            if (PyType_IsSubtype(
                Py_TYPE(instance),
                reinterpret_cast<PyTypeObject*>(cls)
            )) {
                return true;
            }
            PyObject* key;
            PyObject* value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(
                cls->template_instantiations,
                &pos,
                &key,
                &value
            )) {
                __python__* instantiation = reinterpret_cast<__python__*>(value);
                if (instantiation->instancecheck(cls, instance)) {
                    return true;
                }
            }
            return false;
        }

        /* A candidate for the `subclasscheck` function pointer to be used for stub
        types. */
        static bool template_subclasscheck(__python__* cls, PyObject* subclass) {
            if (PyType_Check(subclass)) {
                if (PyType_IsSubtype(
                    reinterpret_cast<PyTypeObject*>(subclass),
                    reinterpret_cast<PyTypeObject*>(cls)
                )) {
                    return true;
                }
                PyObject* key;
                PyObject* value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(
                    cls->template_instantiations,
                    &pos,
                    &key,
                    &value
                )) {
                    __python__* instantiation = reinterpret_cast<__python__*>(value);
                    if (instantiation->subclasscheck(cls, subclass)) {
                        return true;
                    }
                }
                return false;
            } else {
                throw TypeError("issubclass() arg 1 must be a class");
            }
        }

        /* All of the above slots are implemented as getset descriptors on the
        metaclass so as to intercept the standard assignment operator and update the
        internal C++ function pointers. */
        struct Slots {

            /// TODO: getters should return the default behavior if the slot is not
            /// overloaded?

            /// TODO: setters should enforce a signature match (# of args, primarily)

            /// TODO: setters need to generate an overload set if the slot was
            /// previously null, or insert into the existing set if it was not.

            struct tp_init {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_init == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_init);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_init);
                        cls->tp_init = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_init;
                    cls->tp_init = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_new {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_new == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_new);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_new);
                        cls->tp_new = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_new;
                    cls->tp_new = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_getattro {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_getattro == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_getattro);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_getattro);
                        cls->tp_getattro = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {  // TODO: enforce a signature match?
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_getattro;
                    cls->tp_getattro = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_setattro {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_setattro == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_setattro);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_setattro);
                        cls->tp_setattro = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_setattro;
                    cls->tp_setattro = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_delattro {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_delattro == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_delattro);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_delattro);
                        cls->tp_delattro = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_delattro;
                    cls->tp_delattro = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_repr {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_repr == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_repr);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_repr);
                        cls->tp_repr = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_repr;
                    cls->tp_repr = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_hash {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_hash == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_hash);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_hash);
                        cls->tp_hash = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_hash;
                    cls->tp_hash = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_call {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_call == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_call);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_call);
                        cls->tp_call = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_call;
                    cls->tp_call = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_str {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_str == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_str);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_str);
                        cls->tp_str = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_str;
                    cls->tp_str = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_lt {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_lt == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_lt);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_lt);
                        cls->tp_lt = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_lt;
                    cls->tp_lt = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_le {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_le == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_le);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_le);
                        cls->tp_le = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_le;
                    cls->tp_le = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_eq {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_eq == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_eq);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_eq);
                        cls->tp_eq = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_eq;
                    cls->tp_eq = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_ne {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_ne == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_ne);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_ne);
                        cls->tp_ne = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_ne;
                    cls->tp_ne = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_ge {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_ge == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_ge);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_ge);
                        cls->tp_ge = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_ge;
                    cls->tp_ge = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_gt {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_gt == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_gt);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_gt);
                        cls->tp_gt = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_gt;
                    cls->tp_gt = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_iter {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_iter == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_iter);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_iter);
                        cls->tp_iter = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_iter;
                    cls->tp_iter = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_iternext {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_iternext == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_iternext);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_iternext);
                        cls->tp_iternext = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_iternext;
                    cls->tp_iternext = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_reverse {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_reverse == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_reverse);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_reverse);
                        cls->tp_reverse = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_reverse;
                    cls->tp_reverse = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_enter {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_enter == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_enter);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_enter);
                        cls->tp_enter = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_enter;
                    cls->tp_enter = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_exit {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_exit == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_exit);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_exit);
                        cls->tp_exit = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_exit;
                    cls->tp_exit = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_descr_get {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_descr_get == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_descr_get);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_descr_get);
                        cls->tp_descr_get = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_descr_get;
                    cls->tp_descr_get = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_descr_set {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_descr_set == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_descr_set);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_descr_set);
                        cls->tp_descr_set = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_descr_set;
                    cls->tp_descr_set = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct tp_descr_delete {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->tp_descr_delete == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->tp_descr_delete);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->tp_descr_delete);
                        cls->tp_descr_delete = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->tp_descr_delete;
                    cls->tp_descr_delete = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct mp_length {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->mp_length == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->mp_length);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->mp_length);
                        cls->mp_length = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->mp_length;
                    cls->mp_length = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct mp_subscript {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->mp_subscript == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->mp_subscript);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->mp_subscript);
                        cls->mp_subscript = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->mp_subscript;
                    cls->mp_subscript = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct mp_ass_subscript {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->mp_ass_subscript == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->mp_ass_subscript);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->mp_ass_subscript);
                        cls->mp_ass_subscript = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->mp_ass_subscript;
                    cls->mp_ass_subscript = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct mp_del_subscript {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->mp_del_subscript == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->mp_del_subscript);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->mp_del_subscript);
                        cls->mp_del_subscript = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->mp_del_subscript;
                    cls->mp_del_subscript = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct sq_contains {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->sq_contains == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->sq_contains);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->sq_contains);
                        cls->sq_contains = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->sq_contains;
                    cls->sq_contains = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct am_await {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->am_await == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->am_await);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->am_await);
                        cls->am_await = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->am_await;
                    cls->am_await = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct am_aiter {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->am_aiter == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->am_aiter);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->am_aiter);
                        cls->am_aiter = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->am_aiter;
                    cls->am_aiter = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct am_anext {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->am_anext == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->am_anext);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->am_anext);
                        cls->am_anext = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->am_anext;
                    cls->am_anext = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct am_send {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->am_send == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->am_send);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->am_send);
                        cls->am_send = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->am_send;
                    cls->am_send = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct bf_getbuffer {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->bf_getbuffer == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->bf_getbuffer);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->bf_getbuffer);
                        cls->bf_getbuffer = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->bf_getbuffer;
                    cls->bf_getbuffer = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct bf_releasebuffer {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->bf_releasebuffer == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->bf_releasebuffer);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->bf_releasebuffer);
                        cls->bf_releasebuffer = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->bf_releasebuffer;
                    cls->bf_releasebuffer = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_add {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_add == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_add);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_add);
                        cls->nb_add = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_add;
                    cls->nb_add = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_add {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_add == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_add);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_add);
                        cls->nb_inplace_add = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_add;
                    cls->nb_inplace_add = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_subtract {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_subtract == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_subtract);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_subtract);
                        cls->nb_subtract = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_subtract;
                    cls->nb_subtract = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_subtract {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_subtract == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_subtract);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_subtract);
                        cls->nb_inplace_subtract = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_subtract;
                    cls->nb_inplace_subtract = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_multiply {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_multiply == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_multiply);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_multiply);
                        cls->nb_multiply = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_multiply;
                    cls->nb_multiply = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_multiply {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_multiply == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_multiply);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_multiply);
                        cls->nb_inplace_multiply = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_multiply;
                    cls->nb_inplace_multiply = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_remainder {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_remainder == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_remainder);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_remainder);
                        cls->nb_remainder = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_remainder;
                    cls->nb_remainder = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_remainder {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_remainder == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_remainder);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_remainder);
                        cls->nb_inplace_remainder = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_remainder;
                    cls->nb_inplace_remainder = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_divmod {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_divmod == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_divmod);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_divmod);
                        cls->nb_divmod = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_divmod;
                    cls->nb_divmod = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_power {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_power == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_power);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_power);
                        cls->nb_power = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_power;
                    cls->nb_power = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_power {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_power == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_power);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_power);
                        cls->nb_inplace_power = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_power;
                    cls->nb_inplace_power = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_negative {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_negative == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_negative);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_negative);
                        cls->nb_negative = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_negative;
                    cls->nb_negative = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_positive {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_positive == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_positive);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_positive);
                        cls->nb_positive = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_positive;
                    cls->nb_positive = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_absolute {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_absolute == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_absolute);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_absolute);
                        cls->nb_absolute = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_absolute;
                    cls->nb_absolute = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_bool {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_bool == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_bool);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_bool);
                        cls->nb_bool = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_bool;
                    cls->nb_bool = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_invert {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_invert == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_invert);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_invert);
                        cls->nb_invert = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_invert;
                    cls->nb_invert = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_lshift {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_lshift == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_lshift);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_lshift);
                        cls->nb_lshift = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_lshift;
                    cls->nb_lshift = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_lshift {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_lshift == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_lshift);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_lshift);
                        cls->nb_inplace_lshift = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_lshift;
                    cls->nb_inplace_lshift = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_rshift {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_rshift == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_rshift);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_rshift);
                        cls->nb_rshift = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_rshift;
                    cls->nb_rshift = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_rshift {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_rshift == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_rshift);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_rshift);
                        cls->nb_inplace_rshift = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_rshift;
                    cls->nb_inplace_rshift = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_and {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_and == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_and);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_and);
                        cls->nb_and = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_and;
                    cls->nb_and = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_and {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_and == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_and);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_and);
                        cls->nb_inplace_and = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_and;
                    cls->nb_inplace_and = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_xor {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_xor == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_xor);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_xor);
                        cls->nb_xor = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_xor;
                    cls->nb_xor = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_xor {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_xor == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_xor);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_xor);
                        cls->nb_inplace_xor = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_xor;
                    cls->nb_inplace_xor = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_or {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_or == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_or);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_or);
                        cls->nb_or = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_or;
                    cls->nb_or = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_or {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_or == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_or);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_or);
                        cls->nb_inplace_or = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_or;
                    cls->nb_inplace_or = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_int {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_int == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_int);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_int);
                        cls->nb_int = nullptr
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_int;
                    cls->nb_int = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_float {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_float == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_float);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_float);
                        cls->nb_float = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_float;
                    cls->nb_float = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_floor_divide {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_floor_divide == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_floor_divide);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_floor_divide);
                        cls->nb_floor_divide = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_floor_divide;
                    cls->nb_floor_divide = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_floor_divide {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_floor_divide == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_floor_divide);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_floor_divide);
                        cls->nb_inplace_floor_divide = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_floor_divide;
                    cls->nb_inplace_floor_divide = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_true_divide {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_true_divide == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_true_divide);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_true_divide);
                        cls->nb_true_divide = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_true_divide;
                    cls->nb_true_divide = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_true_divide {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_true_divide == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_true_divide);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_true_divide);
                        cls->nb_inplace_true_divide = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_true_divide;
                    cls->nb_inplace_true_divide = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_matrix_multiply {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_matrix_multiply == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_matrix_multiply);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_matrix_multiply);
                        cls->nb_matrix_multiply = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_matrix_multiply;
                    cls->nb_matrix_multiply = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

            struct nb_inplace_matrix_multiply {
                static PyObject* get(__python__* cls, void*) {
                    if (cls->nb_inplace_matrix_multiply == nullptr) {
                        Py_RETURN_NONE;
                    }
                    return Py_NewRef(cls->nb_inplace_matrix_multiply);
                }

                static int set(__python__* cls, PyObject* value, void*) {
                    if (value == nullptr) {
                        Py_XDECREF(cls->nb_inplace_matrix_multiply);
                        cls->nb_inplace_matrix_multiply = nullptr;
                        return 0;
                    }
                    if (!PyCallable_Check(value)) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "attribute must be callable"
                        );
                        return -1;
                    }
                    PyObject* temp = cls->nb_inplace_matrix_multiply;
                    cls->nb_inplace_matrix_multiply = Py_NewRef(value);
                    Py_XDECREF(temp);
                    return 0;
                }
            };

        };

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

        /// TODO: document all the descriptors

        inline static PyGetSetDef getset[] = {
            {
                .name = "__doc__",
                .get = reinterpret_cast<getter>(doc),
                .set = nullptr,
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__init__",
                .get = reinterpret_cast<getter>(Slots::tp_init::get),
                .set = reinterpret_cast<setter>(Slots::tp_init::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__new__",
                .get = reinterpret_cast<getter>(Slots::tp_new::get),
                .set = reinterpret_cast<setter>(Slots::tp_new::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__getattr__",
                .get = reinterpret_cast<getter>(Slots::tp_getattro::get),
                .set = reinterpret_cast<setter>(Slots::tp_getattro::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__setattr__",
                .get = reinterpret_cast<getter>(Slots::tp_setattro::get),
                .set = reinterpret_cast<setter>(Slots::tp_setattro::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__delattr__",
                .get = reinterpret_cast<getter>(Slots::tp_delattro::get),
                .set = reinterpret_cast<setter>(Slots::tp_delattro::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__repr__",
                .get = reinterpret_cast<getter>(Slots::tp_repr::get),
                .set = reinterpret_cast<setter>(Slots::tp_repr::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__hash__",
                .get = reinterpret_cast<getter>(Slots::tp_hash::get),
                .set = reinterpret_cast<setter>(Slots::tp_hash::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__call__",
                .get = reinterpret_cast<getter>(Slots::tp_call::get),
                .set = reinterpret_cast<setter>(Slots::tp_call::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__str__",
                .get = reinterpret_cast<getter>(Slots::tp_str::get),
                .set = reinterpret_cast<setter>(Slots::tp_str::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__lt__",
                .get = reinterpret_cast<getter>(Slots::tp_lt::get),
                .set = reinterpret_cast<setter>(Slots::tp_lt::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__le__",
                .get = reinterpret_cast<getter>(Slots::tp_le::get),
                .set = reinterpret_cast<setter>(Slots::tp_le::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__eq__",
                .get = reinterpret_cast<getter>(Slots::tp_eq::get),
                .set = reinterpret_cast<setter>(Slots::tp_eq::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__ne__",
                .get = reinterpret_cast<getter>(Slots::tp_ne::get),
                .set = reinterpret_cast<setter>(Slots::tp_ne::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__ge__",
                .get = reinterpret_cast<getter>(Slots::tp_ge::get),
                .set = reinterpret_cast<setter>(Slots::tp_ge::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__gt__",
                .get = reinterpret_cast<getter>(Slots::tp_gt::get),
                .set = reinterpret_cast<setter>(Slots::tp_gt::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__iter__",
                .get = reinterpret_cast<getter>(Slots::tp_iter::get),
                .set = reinterpret_cast<setter>(Slots::tp_iter::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__next__",
                .get = reinterpret_cast<getter>(Slots::tp_iternext::get),
                .set = reinterpret_cast<setter>(Slots::tp_iternext::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__reversed__",
                .get = reinterpret_cast<getter>(Slots::tp_reverse::get),
                .set = reinterpret_cast<setter>(Slots::tp_reverse::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__enter__",
                .get = reinterpret_cast<getter>(Slots::tp_enter::get),
                .set = reinterpret_cast<setter>(Slots::tp_enter::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__exit__",
                .get = reinterpret_cast<getter>(Slots::tp_exit::get),
                .set = reinterpret_cast<setter>(Slots::tp_exit::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__get__",
                .get = reinterpret_cast<getter>(Slots::tp_descr_get::get),
                .set = reinterpret_cast<setter>(Slots::tp_descr_get::set),
                .doc = nullptr,  // TODO: fill in?
                .closure = nullptr
            },
            {
                .name = "__set__",
                .get = reinterpret_cast<getter>(Slots::tp_descr_set::get),
                .set = reinterpret_cast<setter>(Slots::tp_descr_set::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__delete__",
                .get = reinterpret_cast<getter>(Slots::tp_descr_delete::get),
                .set = reinterpret_cast<setter>(Slots::tp_descr_delete::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__len__",
                .get = reinterpret_cast<getter>(Slots::mp_length::get),
                .set = reinterpret_cast<setter>(Slots::mp_length::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__getitem__",
                .get = reinterpret_cast<getter>(Slots::mp_subscript::get),
                .set = reinterpret_cast<setter>(Slots::mp_subscript::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__setitem__",
                .get = reinterpret_cast<getter>(Slots::mp_ass_subscript::get),
                .set = reinterpret_cast<setter>(Slots::mp_ass_subscript::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__delitem__",
                .get = reinterpret_cast<getter>(Slots::mp_del_subscript::get),
                .set = reinterpret_cast<setter>(Slots::mp_del_subscript::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__contains__",
                .get = reinterpret_cast<getter>(Slots::sq_contains::get),
                .set = reinterpret_cast<setter>(Slots::sq_contains::set),
                .doc = nullptr,
                .closure = nullptr,
            },
            {
                .name = "__buffer__",
                .get = reinterpret_cast<getter>(Slots::bf_getbuffer::get),
                .set = reinterpret_cast<setter>(Slots::bf_getbuffer::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__release_buffer__",
                .get = reinterpret_cast<getter>(Slots::bf_releasebuffer::get),
                .set = reinterpret_cast<setter>(Slots::bf_releasebuffer::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__add__",
                .get = reinterpret_cast<getter>(Slots::nb_add::get),
                .set = reinterpret_cast<setter>(Slots::nb_add::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__iadd__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_add::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_add::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__sub__",
                .get = reinterpret_cast<getter>(Slots::nb_subtract::get),
                .set = reinterpret_cast<setter>(Slots::nb_subtract::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__isub__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_subtract::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_subtract::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__mul__",
                .get = reinterpret_cast<getter>(Slots::nb_multiply::get),
                .set = reinterpret_cast<setter>(Slots::nb_multiply::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__imul__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_multiply::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_multiply::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__mod__",
                .get = reinterpret_cast<getter>(Slots::nb_remainder::get),
                .set = reinterpret_cast<setter>(Slots::nb_remainder::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__imod__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_remainder::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_remainder::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__divmod__",
                .get = reinterpret_cast<getter>(Slots::nb_divmod::get),
                .set = reinterpret_cast<setter>(Slots::nb_divmod::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__pow__",
                .get = reinterpret_cast<getter>(Slots::nb_power::get),
                .set = reinterpret_cast<setter>(Slots::nb_power::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ipow__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_power::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_power::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__neg__",
                .get = reinterpret_cast<getter>(Slots::nb_negative::get),
                .set = reinterpret_cast<setter>(Slots::nb_negative::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__pos__",
                .get = reinterpret_cast<getter>(Slots::nb_positive::get),
                .set = reinterpret_cast<setter>(Slots::nb_positive::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__abs__",
                .get = reinterpret_cast<getter>(Slots::nb_absolute::get),
                .set = reinterpret_cast<setter>(Slots::nb_absolute::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__bool__",
                .get = reinterpret_cast<getter>(Slots::nb_bool::get),
                .set = reinterpret_cast<setter>(Slots::nb_bool::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__invert__",
                .get = reinterpret_cast<getter>(Slots::nb_invert::get),
                .set = reinterpret_cast<setter>(Slots::nb_invert::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__lshift__",
                .get = reinterpret_cast<getter>(Slots::nb_lshift::get),
                .set = reinterpret_cast<setter>(Slots::nb_lshift::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ilshift__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_lshift::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_lshift::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rshift__",
                .get = reinterpret_cast<getter>(Slots::nb_rshift::get),
                .set = reinterpret_cast<setter>(Slots::nb_rshift::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__irshift__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_rshift::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_rshift::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__and__",
                .get = reinterpret_cast<getter>(Slots::nb_and::get),
                .set = reinterpret_cast<setter>(Slots::nb_and::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__iand__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_and::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_and::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__xor__",
                .get = reinterpret_cast<getter>(Slots::nb_xor::get),
                .set = reinterpret_cast<setter>(Slots::nb_xor::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ixor__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_xor::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_xor::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__or__",
                .get = reinterpret_cast<getter>(Slots::nb_or::get),
                .set = reinterpret_cast<setter>(Slots::nb_or::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ior__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_or::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_or::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__int__",
                .get = reinterpret_cast<getter>(Slots::nb_int::get),
                .set = reinterpret_cast<setter>(Slots::nb_int::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__float__",
                .get = reinterpret_cast<getter>(Slots::nb_float::get),
                .set = reinterpret_cast<setter>(Slots::nb_float::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__floordiv__",
                .get = reinterpret_cast<getter>(Slots::nb_floor_divide::get),
                .set = reinterpret_cast<setter>(Slots::nb_floor_divide::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ifloordiv__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_floor_divide::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_floor_divide::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__truediv__",
                .get = reinterpret_cast<getter>(Slots::nb_true_divide::get),
                .set = reinterpret_cast<setter>(Slots::nb_true_divide::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__itruediv__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_true_divide::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_true_divide::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__matmul__",
                .get = reinterpret_cast<getter>(Slots::nb_matrix_multiply::get),
                .set = reinterpret_cast<setter>(Slots::nb_matrix_multiply::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__imatmul__",
                .get = reinterpret_cast<getter>(Slots::nb_inplace_matrix_multiply::get),
                .set = reinterpret_cast<setter>(Slots::nb_inplace_matrix_multiply::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {nullptr, nullptr, nullptr, nullptr, nullptr}
        };

    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::template enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::template enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


template <typename T>
struct __isinstance__<T, BertrandMeta> : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::python_like<T>) {
            int result = PyObject_IsInstance(
                ptr(obj),
                ptr(Type<BertrandMeta>())
            );
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            return false;
        }
    }
    static constexpr bool operator()(const T& obj, const BertrandMeta& cls) {
        if constexpr (impl::python_like<T>) {
            using Meta = Type<BertrandMeta>::__python__;
            Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
            return meta->instancecheck(meta, ptr(obj));
        } else {
            return false;
        }
    }
};


template <typename T>
struct __issubclass__<T, BertrandMeta> : Returns<bool> {
    static consteval bool operator()() {
        return impl::originates_from_cpp<T>;
    }
    static bool operator()(const T& obj) {
        int result = PyObject_IsSubclass(
            ptr(obj),
            ptr(Type<BertrandMeta>())
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    }
    static bool operator()(const T& obj, const BertrandMeta& cls) {
        if constexpr (impl::python_like<T>) {
            using Meta = Type<BertrandMeta>::__python__;
            Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
            return meta->subclasscheck(meta, ptr(obj));
        } else {
            return false;
        }
    }
};


template <>
struct __len__<BertrandMeta>                                : Returns<size_t> {};
template <>
struct __iter__<BertrandMeta>                               : Returns<BertrandMeta> {};
template <typename... T>
struct __getitem__<BertrandMeta, Type<T>...>                : Returns<BertrandMeta> {
    static auto operator()(const BertrandMeta& cls, const Type<T>&... key) {
        using Meta = Type<BertrandMeta>::__python__;
        Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
        if (meta->template_instantiations == nullptr) {
            throw TypeError("class has no template instantiations");
        }
        PyObject* tuple = PyTuple_Pack(sizeof...(T), ptr(key)...);
        if (tuple == nullptr) {
            Exception::from_python();
        }
        PyObject* value = PyDict_GetItem(meta->template_instantiations, tuple);
        Py_DECREF(tuple);
        if (value == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<BertrandMeta>(value);
    }
};


template <impl::originates_from_cpp T>
struct __len__<Type<T>>                                     : Returns<size_t> {};
template <impl::originates_from_cpp T>
struct __iter__<Type<T>>                                    : Returns<BertrandMeta> {};
template <impl::originates_from_cpp T, typename... U>
struct __getitem__<Type<T>, Type<U>...>                     : Returns<BertrandMeta> {
    static auto operator()(const Type<T>& cls, const Type<U>&... key) {
        using Meta = Type<BertrandMeta>::__python__;
        Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
        if (meta->template_instantiations == nullptr) {
            throw TypeError("class has no template instantiations");
        }
        PyObject* tuple = PyTuple_Pack(sizeof...(U), ptr(key)...);
        if (tuple == nullptr) {
            Exception::from_python();
        }
        PyObject* value = PyDict_GetItem(meta->template_instantiations, tuple);
        Py_DECREF(tuple);
        if (value == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<BertrandMeta>(value);
    }
};


namespace impl {

    template <typename CRTP, typename Wrapper, typename CppType>
    template <StaticStr ModName>
    struct TypeTag::BaseDef<CRTP, Wrapper, CppType>::Bindings {
    private:
        using Meta = Type<BertrandMeta>::__python__;
        inline static std::vector<PyMethodDef> tp_methods;
        inline static std::vector<PyGetSetDef> tp_getset;
        inline static std::vector<PyMemberDef> tp_members;

        /// TODO: these can't delegate to the base tp_new, etc. since those will point
        /// back here recursively.
        /// -> The only way to do that would be to also store the original slot values
        /// in the metaclass, and delegate to them where appropriate.
        /// -> What I have to do is initialize the type without any of these slots
        /// filled in initially, then record their values into the metaclass, and then
        /// replace them with the wrappers, which delegate to the PyObject* if present.
        /// I should then be able to assign, delete, or overload the PyObject*
        /// dynamically from Python or C++ and see the changes reflected in both
        /// languages.
        /// -> All of this extra logic is centralized in BaseDef::Bindings::finalize().

        /// TODO: what do I do about the self argument in these wrappers?
        /// -> Invoke the descriptor protocol when accessing the slot.

        static int tp_init(CRTP* self, PyObject* args, PyObject* kwds) {
            Meta* meta = reinterpret_cast<Meta*>(Py_TYPE(self));
            if (meta->tp_init) {
                PyObject* result = PyObject_CallFunctionObjArgs(
                    meta->tp_init,
                    self,
                    args,
                    kwds,
                    nullptr
                );
                if (result == nullptr) {
                    return -1;
                }
                Py_DECREF(result);
                return 0;
            } else if (meta->base_tp_init) {
                return meta->base_tp_init(self, args, kwds);
            } else {
                PyErr_Format(
                    PyExc_TypeError,
                    "cannot initialize object of type '%s'",
                    repr(Type<Wrapper>()).c_str()
                );
                return -1;
            }
        }

        static PyObject* tp_new(PyTypeObject* cls, PyObject* args, PyObject* kwds) {
            Meta* meta = reinterpret_cast<Meta*>(cls);
            if (meta->tp_new) {
                PyObject* result = PyObject_CallFunctionObjArgs(
                    meta->tp_new,
                    cls,
                    args,
                    kwds,
                    nullptr
                );
                if (result == nullptr) {
                    return nullptr;
                }
                return result;
            } else if (meta->base_tp_new) {
                return meta->base_tp_new(cls, args, kwds);
            } else {
                PyErr_Format(
                    PyExc_TypeError,
                    "cannot create object of type '%s'",
                    repr(Type<Wrapper>()).c_str()
                );
                return nullptr;
            }
        }

        static PyObject* tp_getattro(CRTP* self, PyObject* attr) {
            Meta* meta = reinterpret_cast<Meta*>(Py_TYPE(self));
            if (meta->tp_getattro) {
                PyObject* result = PyObject_CallFunctionObjArgs(
                    meta->tp_getattro,
                    self,
                    attr,
                    nullptr
                );
                if (result == nullptr) {
                    return nullptr;
                }
                return result;
            } else if (meta->base_tp_getattro) {
                return meta->base_tp_getattro(self, attr);
            } else {
                PyErr_Format(
                    PyExc_AttributeError,
                    "cannot get attribute '%U' from object of type '%s'",
                    attr,
                    repr(Type<Wrapper>()).c_str()
                );
                return nullptr;
            }
        }

        static int tp_setattro(CRTP* self, PyObject* attr, PyObject* value) {
            Meta* meta = reinterpret_cast<Meta*>(Py_TYPE(self));
            if (value == nullptr) {
                if (meta->tp_delattro) {
                    PyObject* result = PyObject_CallFunctionObjArgs(
                        meta->tp_delattro,
                        self,
                        attr,
                        nullptr
                    );
                    if (result == nullptr) {
                        return -1;
                    }
                    Py_DECREF(result);
                    return 0;
                } else if (meta->base_tp_setattro) {
                    return meta->base_tp_setattro(self, attr);
                } else {
                    PyErr_Format(
                        PyExc_AttributeError,
                        "cannot delete attribute '%U' from object of type '%s'",
                        attr,
                        repr(Type<Wrapper>()).c_str()
                    );
                    return -1;
                }

            } else {
                if (meta->tp_setattro) {
                    PyObject* result = PyObject_CallFunctionObjArgs(
                        meta->tp_setattro,
                        self,
                        attr,
                        value,
                        nullptr
                    );
                    if (result == nullptr) {
                        return -1;
                    }
                    Py_DECREF(result);
                    return 0;
                } else if (meta->base_tp_setattro) {
                    return meta->base_tp_setattro(self, attr, value);
                } else {
                    PyErr_Format(
                        PyExc_AttributeError,
                        "cannot set attribute '%U' on object of type '%s'",
                        attr,
                        repr(Type<Wrapper>()).c_str()
                    );
                    return -1;
                }
            }
        }

        static int mp_ass_subscript(CRTP* self, PyObject* key, PyObject* value) {
            Meta* meta = reinterpret_cast<Meta*>(Py_TYPE(self));
            if (value == nullptr) {
                if (meta->mp_del_subscript) {
                    PyObject* result = PyObject_CallFunctionObjArgs(
                        meta->mp_del_subscript,
                        self,
                        key,
                        nullptr
                    );
                    if (result == nullptr) {
                        return -1;
                    }
                    Py_DECREF(result);
                    return 0;
                } else if (meta->base_mp_ass_subscript) {
                    return meta->base_mp_ass_subscript(self, key, value);
                } else {
                    PyErr_Format(
                        PyExc_TypeError,
                        "cannot delete item '%U' from object of type '%s'",
                        key,
                        repr(Type<Wrapper>()).c_str()
                    );
                    return -1;
                }

            } else {
                if (meta->mp_ass_subscript) {
                    PyObject* result = PyObject_CallFunctionObjArgs(
                        meta->mp_ass_subscript,
                        self,
                        key,
                        value,
                        nullptr
                    );
                    if (result == nullptr) {
                        return -1;
                    }
                    Py_DECREF(result);
                    return 0;
                } else if (meta->base_mp_ass_subscript) {
                    return meta->base_mp_ass_subscript(self, key, value);
                } else {
                    PyErr_Format(
                        PyExc_TypeError,
                        "cannot set item '%U' on object of type '%s'",
                        key,
                        repr(Type<Wrapper>()).c_str()
                    );
                    return -1;
                }
            }
        }

        static PyObject* richcompare(CRTP* self, PyObject* other, int op) {
            switch (op) {
                case Py_LT:
                    if constexpr (dunder::has_lt<CRTP>) {
                        return CRTP::__lt__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_LE:
                    if constexpr (dunder::has_le<CRTP>) {
                        return CRTP::__le__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_EQ:
                    if constexpr (dunder::has_eq<CRTP>) {
                        return CRTP::__eq__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_NE:
                    if constexpr (dunder::has_ne<CRTP>) {
                        return CRTP::__ne__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_GE:
                    if constexpr (dunder::has_ge<CRTP>) {
                        return CRTP::__ge__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_GT:
                    if constexpr (dunder::has_gt<CRTP>) {
                        return CRTP::__gt__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                default:
                    PyErr_Format(
                        PyExc_SystemError,
                        "unrecognized rich comparison code: %d",
                        op
                    );
                    return nullptr;
            }
        }

        static int descr_set(CRTP* self, PyObject* obj, PyObject* value) {
            if (value == nullptr) {
                if constexpr (dunder::has_delete<CRTP>) {
                    return CRTP::__delete__(self, obj);
                } else {
                    PyErr_Format(
                        PyExc_AttributeError,
                        "cannot delete descriptor '%U' from object of type '%s'",
                        obj,
                        repr(Type<Wrapper>()).c_str()
                    );
                    return -1;
                }
            } else {
                if constexpr (dunder::has_set<CRTP>) {
                    return CRTP::__set__(self, obj, value);
                } else {
                    PyErr_Format(
                        PyExc_AttributeError,
                        "cannot set descriptor '%U' on object of type '%s'",
                        obj,
                        repr(Type<Wrapper>()).c_str()
                    );
                    return -1;
                }
            }
        }

        static PyObject* enter(CRTP* self, void* /* unused */) {
            return CRTP::__enter__(self);
        }

        static PyObject* aenter(CRTP* self, void* /* unused */) {
            return CRTP::__aenter__(self);
        }

        static PyObject* exit(CRTP* self, PyObject* const* args, Py_ssize_t nargs) {
            if (PyVectorcall_NARGS(nargs) == 3) {
                return CRTP::__exit__(self, args[0], args[1], args[2]);
            } else {
                PyErr_Format(
                    PyExc_TypeError,
                    "__exit__() takes exactly 3 arguments (%zd given)",
                    PyVectorcall_NARGS(nargs)
                );
                return nullptr;
            }
        }

        static PyObject* aexit(CRTP* self, PyObject* const* args, Py_ssize_t nargs) {
            if (PyVectorcall_NARGS(nargs) == 3) {
                return CRTP::__aexit__(self, args[0], args[1], args[2]);
            } else {
                PyErr_Format(
                    PyExc_TypeError,
                    "__aexit__() takes exactly 3 arguments (%zd given)",
                    PyVectorcall_NARGS(nargs)
                );
                return nullptr;
            }
        }

        static PyObject* reversed(CRTP* self, void* /* unused */) {
            return CRTP::__reversed__(self);
        }

        /* This function is stored as a pointer in the metaclass's C++ members and
        called whenever a Python-level `isinstance()` check is made against this class.
        It will delegate to an `__isinstance__<Object, Wrapper>` specialization if one
        exists.  Otherwise it will default to standard Python behavior. */
        static bool instancecheck(Meta* cls, PyObject* instance) {
            if constexpr (std::is_invocable_v<
                __isinstance__<Object, Wrapper>,
                const Object&
            >) {
                return __isinstance__<Object, Wrapper>{}(
                    reinterpret_borrow<Object>(instance)
                );
            } else {
                return PyType_IsSubtype(
                    Py_TYPE(instance),
                    reinterpret_cast<PyTypeObject*>(cls)
                );
            }
        }

        /* This function is stored as a pointer in the metaclass's C++ members and
        called whenever a Python-level `issubclass()` check is made against this calss.
        It will delegate to an `__issubclass__<Object, Wrapper>` specialization if one
        exists.  Otherwise it will default to standard Python behavior. */
        static bool subclasscheck(Meta* cls, PyObject* subclass) {
            if constexpr (std::is_invocable_v<
                __issubclass__<Object, Wrapper>,
                const Object&
            >) {
                return __issubclass__<Object, Wrapper>{}(
                    reinterpret_borrow<Object>(subclass)
                );
            } else {
                if (PyType_Check(subclass)) {
                    return PyType_IsSubtype(
                        reinterpret_cast<PyTypeObject*>(subclass),
                        reinterpret_cast<PyTypeObject*>(cls)
                    );
                } else {
                    throw TypeError(
                        "issubclass() arg 1 must be a class"
                    );
                }
            }
        }

        struct Context {
            bool is_member_var = false;
            bool is_class_var = false;
            bool is_property = false;
            bool is_class_property = false;
            bool is_type = false;
            bool is_template_interface = false;
            bool is_method = false;
            bool is_classmethod = false;
            bool is_staticmethod = false;
            std::vector<std::function<void(Type<Wrapper>&)>> callbacks;
        };

    public:
        using t_cpp = CppType;

        Module<ModName> module;
        std::unordered_map<std::string, Context> context;
        Meta::Getters class_getters;
        Meta::Setters class_setters;

        Bindings(const Module<ModName>& mod) : module(mod) {}
        Bindings(const Bindings&) = delete;
        Bindings(Bindings&&) = delete;

        /* Finalize a type definition and produce a corresponding type object.  This
        method should always be called in the return statement of an `__export__()`
        script, which automates the import process.

        The argument is a bitmask of Python flags that control various aspects of the
        type's behavior.  By default, the final type supports inheritance, weak
        references, and possesses an instance dictionary, just like normal Python
        classes.  Note that the `Py_TPFLAGS_DEFAULT`, `Py_TPFLAGS_HEAPTYPE`, and
        `Py_TPFLAGS_HAVE_GC` flags are always set and cannot be overridden.  These are
        required for Bertrand types to function correctly with respect to the Python
        interpreter. */
        template <std::derived_from<Object>... Bases>
        Type<Wrapper> finalize(unsigned int tp_flags =
            Py_TPFLAGS_BASETYPE | Py_TPFLAGS_MANAGED_WEAKREF | Py_TPFLAGS_MANAGED_DICT
        ) {
            static unsigned int flags;
            static std::vector<PyType_Slot> slots;
            static bool initialized = false;
            if (!initialized) {
                flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_HAVE_GC;
                flags |= tp_flags;
                if constexpr (dunder::has_dealloc<CRTP>) {
                    slots.push_back({
                        Py_tp_dealloc,
                        reinterpret_cast<void*>(CRTP::__dealloc__)
                    });
                }
                if constexpr (dunder::has_vectorcall<CRTP>) {
                    tp_members.push_back({
                        "__vectorcalloffset__",
                        Py_T_PYSSIZET,
                        offsetof(CRTP, __vectorcall__),
                        Py_READONLY,
                    });
                }
                if constexpr (dunder::has_await<CRTP>) {
                    slots.push_back({
                        Py_am_await,
                        reinterpret_cast<void*>(CRTP::__await__)
                    });
                }
                if constexpr (dunder::has_aiter<CRTP>) {
                    slots.push_back({
                        Py_am_aiter,
                        reinterpret_cast<void*>(CRTP::__aiter__)
                    });
                }
                if constexpr (dunder::has_anext<CRTP>) {
                    slots.push_back({
                        Py_am_anext,
                        reinterpret_cast<void*>(CRTP::__anext__)
                    });
                }
                if constexpr (dunder::has_asend<CRTP>) {
                    slots.push_back({
                        Py_am_send,
                        reinterpret_cast<void*>(CRTP::__asend__)
                    });
                }
                if constexpr (dunder::has_repr<CRTP>) {
                    slots.push_back({
                        Py_tp_repr,
                        reinterpret_cast<void*>(CRTP::__repr__)
                    });
                }
                if constexpr (dunder::has_add<CRTP>) {
                    slots.push_back({
                        Py_nb_add,
                        reinterpret_cast<void*>(CRTP::__add__)
                    });
                }
                if constexpr (dunder::has_subtract<CRTP>) {
                    slots.push_back({
                        Py_nb_subtract,
                        reinterpret_cast<void*>(CRTP::__sub__)
                    });
                }
                if constexpr (dunder::has_multiply<CRTP>) {
                    slots.push_back({
                        Py_nb_multiply,
                        reinterpret_cast<void*>(CRTP::__mul__)
                    });
                }
                if constexpr (dunder::has_remainder<CRTP>) {
                    slots.push_back({
                        Py_nb_remainder,
                        reinterpret_cast<void*>(CRTP::__mod__)
                    });
                }
                if constexpr (dunder::has_divmod<CRTP>) {
                    slots.push_back({
                        Py_nb_divmod,
                        reinterpret_cast<void*>(CRTP::__divmod__)
                    });
                }
                if constexpr (dunder::has_power<CRTP>) {
                    slots.push_back({
                        Py_nb_power,
                        reinterpret_cast<void*>(CRTP::__pow__)
                    });
                }
                if constexpr (dunder::has_negative<CRTP>) {
                    slots.push_back({
                        Py_nb_negative,
                        reinterpret_cast<void*>(CRTP::__neg__)
                    });
                }
                if constexpr (dunder::has_positive<CRTP>) {
                    slots.push_back({
                        Py_nb_positive,
                        reinterpret_cast<void*>(CRTP::__pos__)
                    });
                }
                if constexpr (dunder::has_absolute<CRTP>) {
                    slots.push_back({
                        Py_nb_absolute,
                        reinterpret_cast<void*>(CRTP::__abs__)
                    });
                }
                if constexpr (dunder::has_bool<CRTP>) {
                    slots.push_back({
                        Py_nb_bool,
                        reinterpret_cast<void*>(CRTP::__bool__)
                    });
                }
                if constexpr (dunder::has_invert<CRTP>) {
                    slots.push_back({
                        Py_nb_invert,
                        reinterpret_cast<void*>(CRTP::__invert__)
                    });
                }
                if constexpr (dunder::has_lshift<CRTP>) {
                    slots.push_back({
                        Py_nb_lshift,
                        reinterpret_cast<void*>(CRTP::__lshift__)
                    });
                }
                if constexpr (dunder::has_rshift<CRTP>) {
                    slots.push_back({
                        Py_nb_rshift,
                        reinterpret_cast<void*>(CRTP::__rshift__)
                    });
                }
                if constexpr (dunder::has_and<CRTP>) {
                    slots.push_back({
                        Py_nb_and,
                        reinterpret_cast<void*>(CRTP::__and__)
                    });
                }
                if constexpr (dunder::has_xor<CRTP>) {
                    slots.push_back({
                        Py_nb_xor,
                        reinterpret_cast<void*>(CRTP::__xor__)
                    });
                }
                if constexpr (dunder::has_or<CRTP>) {
                    slots.push_back({
                        Py_nb_or,
                        reinterpret_cast<void*>(CRTP::__or__)
                    });
                }
                if constexpr (dunder::has_int<CRTP>) {
                    slots.push_back({
                        Py_nb_int,
                        reinterpret_cast<void*>(CRTP::__int__)
                    });
                }
                if constexpr (dunder::has_float<CRTP>) {
                    slots.push_back({
                        Py_nb_float,
                        reinterpret_cast<void*>(CRTP::__float__)
                    });
                }
                if constexpr (dunder::has_inplace_add<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_add,
                        reinterpret_cast<void*>(CRTP::__iadd__)
                    });
                }
                if constexpr (dunder::has_inplace_subtract<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_subtract,
                        reinterpret_cast<void*>(CRTP::__isub__)
                    });
                }
                if constexpr (dunder::has_inplace_multiply<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_multiply,
                        reinterpret_cast<void*>(CRTP::__imul__)
                    });
                }
                if constexpr (dunder::has_inplace_remainder<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_remainder,
                        reinterpret_cast<void*>(CRTP::__imod__)
                    });
                }
                if constexpr (dunder::has_inplace_power<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_power,
                        reinterpret_cast<void*>(CRTP::__ipow__)
                    });
                }
                if constexpr (dunder::has_inplace_lshift<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_lshift,
                        reinterpret_cast<void*>(CRTP::__ilshift__)
                    });
                }
                if constexpr (dunder::has_inplace_rshift<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_rshift,
                        reinterpret_cast<void*>(CRTP::__irshift__)
                    });
                }
                if constexpr (dunder::has_inplace_and<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_and,
                        reinterpret_cast<void*>(CRTP::__iand__)
                    });
                }
                if constexpr (dunder::has_inplace_xor<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_xor,
                        reinterpret_cast<void*>(CRTP::__ixor__)
                    });
                }
                if constexpr (dunder::has_inplace_or<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_or,
                        reinterpret_cast<void*>(CRTP::__ior__)
                    });
                }
                if constexpr (dunder::has_floor_divide<CRTP>) {
                    slots.push_back({
                        Py_nb_floor_divide,
                        reinterpret_cast<void*>(CRTP::__floordiv__)
                    });
                }
                if constexpr (dunder::has_true_divide<CRTP>) {
                    slots.push_back({
                        Py_nb_true_divide,
                        reinterpret_cast<void*>(CRTP::__truediv__)
                    });
                }
                if constexpr (dunder::has_inplace_floor_divide<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_floor_divide,
                        reinterpret_cast<void*>(CRTP::__ifloordiv__)
                    });
                }
                if constexpr (dunder::has_inplace_true_divide<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_true_divide,
                        reinterpret_cast<void*>(CRTP::__itruediv__)
                    });
                }
                if constexpr (dunder::has_index<CRTP>) {
                    slots.push_back({
                        Py_nb_index,
                        reinterpret_cast<void*>(CRTP::__index__)
                    });
                }
                if constexpr (dunder::has_matrix_multiply<CRTP>) {
                    slots.push_back({
                        Py_nb_matrix_multiply,
                        reinterpret_cast<void*>(CRTP::__matmul__)
                    });
                }
                if constexpr (dunder::has_inplace_matrix_multiply<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_matrix_multiply,
                        reinterpret_cast<void*>(CRTP::__imatmul__)
                    });
                }
                if constexpr (dunder::has_contains<CRTP>) {
                    slots.push_back({
                        Py_sq_contains,
                        reinterpret_cast<void*>(CRTP::__contains__)
                    });
                }
                if constexpr (impl::sequence_like<Wrapper>) {
                    if constexpr (dunder::has_len<CRTP>) {
                        slots.push_back({
                            Py_sq_length,
                            reinterpret_cast<void*>(CRTP::__len__)
                        });
                    }
                    if constexpr (dunder::has_getitem<CRTP>) {
                        slots.push_back({
                            Py_sq_item,
                            reinterpret_cast<void*>(CRTP::__getitem__)
                        });
                    }
                    if constexpr (dunder::has_setitem<CRTP> || dunder::has_delitem<CRTP>) {
                        slots.push_back({
                            Py_sq_ass_item,
                            reinterpret_cast<void*>(setitem)
                        });
                    }
                    if constexpr (dunder::has_add<CRTP> && has_concat<Wrapper>) {
                        slots.push_back({
                            Py_sq_concat,
                            reinterpret_cast<void*>(CRTP::__add__)
                        });
                    }
                    if constexpr (
                        dunder::has_inplace_add<CRTP> && has_inplace_concat<Wrapper>
                    ) {
                        slots.push_back({
                            Py_sq_inplace_concat,
                            reinterpret_cast<void*>(CRTP::__iadd__)
                        });
                    }
                    if constexpr (dunder::has_multiply<CRTP> && has_repeat<Wrapper>) {
                        slots.push_back({
                            Py_sq_repeat,
                            reinterpret_cast<void*>(CRTP::__mul__)
                        });
                    }
                    if constexpr (
                        dunder::has_inplace_multiply<CRTP> && has_inplace_repeat<Wrapper>
                    ) {
                        slots.push_back({
                            Py_sq_inplace_repeat,
                            reinterpret_cast<void*>(CRTP::__imul__)
                        });
                    }
                }
                if constexpr (impl::mapping_like<Wrapper>) {
                    if constexpr (dunder::has_len<CRTP>) {
                        slots.push_back({
                            Py_mp_length,
                            reinterpret_cast<void*>(CRTP::__len__)
                        });
                    }
                    if constexpr (dunder::has_getitem<CRTP>) {
                        slots.push_back({
                            Py_mp_subscript,
                            reinterpret_cast<void*>(CRTP::__getitem__)
                        });
                    }
                    if constexpr (dunder::has_setitem<CRTP> || dunder::has_delitem<CRTP>) {
                        slots.push_back({
                            Py_mp_ass_subscript,
                            reinterpret_cast<void*>(setitem)
                        });
                    }
                }
                if constexpr (dunder::has_hash<CRTP>) {
                    slots.push_back({
                        Py_tp_hash,
                        reinterpret_cast<void*>(CRTP::__hash__)
                    });
                }
                if constexpr (dunder::has_call<CRTP>) {
                    slots.push_back({
                        Py_tp_call,
                        reinterpret_cast<void*>(CRTP::__call__)
                    });
                }
                if constexpr (dunder::has_str<CRTP>) {
                    slots.push_back({
                        Py_tp_str,
                        reinterpret_cast<void*>(CRTP::__str__)
                    });
                }
                if constexpr (dunder::has_getattr<CRTP>) {
                    slots.push_back({
                        Py_tp_getattro,
                        reinterpret_cast<void*>(CRTP::__getattr__)
                    });
                }
                if constexpr (dunder::has_setattr<CRTP> || dunder::has_delattr<CRTP>) {
                    slots.push_back({
                        Py_tp_setattro,
                        reinterpret_cast<void*>(setattro)
                    });
                }
                if constexpr (dunder::has_buffer<CRTP>) {
                    slots.push_back({
                        Py_bf_getbuffer,
                        reinterpret_cast<void*>(CRTP::__buffer__)
                    });
                }
                if constexpr (dunder::has_release_buffer<CRTP>) {
                    slots.push_back({
                        Py_bf_releasebuffer,
                        reinterpret_cast<void*>(CRTP::__release_buffer__)
                    });
                }
                if constexpr (dunder::has_doc<CRTP>) {
                    slots.push_back({
                        Py_tp_doc,
                        const_cast<void*>(
                            reinterpret_cast<const void*>(CRTP::__doc__.buffer)
                        )
                    });
                }
                if constexpr (dunder::has_traverse<CRTP>) {
                    slots.push_back({
                        Py_tp_traverse,
                        reinterpret_cast<void*>(CRTP::__traverse__)
                    });
                }
                if constexpr (dunder::has_clear<CRTP>) {
                    slots.push_back({
                        Py_tp_clear,
                        reinterpret_cast<void*>(CRTP::__clear__)
                    });
                }
                if constexpr (
                    dunder::has_lt<CRTP> || dunder::has_le<CRTP> ||
                    dunder::has_eq<CRTP> || dunder::has_ne<CRTP> ||
                    dunder::has_gt<CRTP> || dunder::has_ge<CRTP>
                ) {
                    slots.push_back({
                        Py_tp_richcompare,
                        reinterpret_cast<void*>(richcompare)
                    });
                }
                if constexpr (dunder::has_iter<CRTP>) {
                    slots.push_back({
                        Py_tp_iter,
                        reinterpret_cast<void*>(CRTP::__iter__)
                    });
                }
                if constexpr (dunder::has_next<CRTP>) {
                    slots.push_back({
                        Py_tp_iternext,
                        reinterpret_cast<void*>(CRTP::__next__)
                    });
                }
                if constexpr (dunder::has_get<CRTP>) {
                    slots.push_back({
                        Py_tp_descr_get,
                        reinterpret_cast<void*>(CRTP::__get__)
                    });
                }
                if constexpr (dunder::has_set<CRTP> || dunder::has_delete<CRTP>) {
                    slots.push_back({
                        Py_tp_descr_set,
                        reinterpret_cast<void*>(descr_set)
                    });
                }
                if constexpr (dunder::has_init<CRTP>) {
                    slots.push_back({
                        Py_tp_init,
                        reinterpret_cast<void*>(CRTP::__init__)
                    });
                }
                if constexpr (dunder::has_new<CRTP>) {
                    slots.push_back({
                        Py_tp_new,
                        reinterpret_cast<void*>(CRTP::__new__)
                    });
                }
                if constexpr (dunder::has_instancecheck<CRTP>) {
                    tp_methods.push_back({
                        "__instancecheck__",
                        reinterpret_cast<PyCFunction>(CRTP::__instancecheck__),
                        METH_O,
                        nullptr
                    });
                }
                if constexpr (dunder::has_subclasscheck<CRTP>) {
                    tp_methods.push_back({
                        "__subclasscheck__",
                        reinterpret_cast<PyCFunction>(CRTP::__subclasscheck__),
                        METH_O,
                        nullptr
                    });
                }
                if constexpr (dunder::has_enter<CRTP>) {
                    tp_methods.push_back({
                        "__enter__",
                        reinterpret_cast<PyCFunction>(enter),
                        METH_NOARGS,
                        nullptr
                    });
                }
                if constexpr (dunder::has_exit<CRTP>) {
                    tp_methods.push_back({
                        "__exit__",
                        reinterpret_cast<PyCFunction>(exit),
                        METH_FASTCALL,
                        nullptr
                    });
                }
                if constexpr (dunder::has_aenter<CRTP>) {
                    tp_methods.push_back({
                        "__aenter__",
                        reinterpret_cast<PyCFunction>(aenter),
                        METH_NOARGS,
                        nullptr
                    });
                }
                if constexpr (dunder::has_aexit<CRTP>) {
                    tp_methods.push_back({
                        "__aexit__",
                        reinterpret_cast<PyCFunction>(aexit),
                        METH_FASTCALL,
                        nullptr
                    });
                }
                if constexpr (dunder::has_reversed<CRTP>) {
                    tp_methods.push_back({
                        "__reversed__",
                        reinterpret_cast<PyCFunction>(reversed),
                        METH_NOARGS,
                        nullptr
                    });
                }
                if (tp_methods.size()) {
                    tp_methods.push_back({
                        nullptr,
                        nullptr,
                        0,
                        nullptr
                    });
                    slots.push_back({
                        Py_tp_methods,
                        tp_methods.data()
                    });
                }
                if (tp_members.size()) {
                    tp_members.push_back({
                        nullptr,
                        0,
                        0,
                        0,
                        nullptr
                    });
                    slots.push_back({
                        Py_tp_members,
                        tp_members.data()
                    });
                }
                if (tp_getset.size()) {
                    tp_getset.push_back({
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr
                    });
                    slots.push_back({
                        Py_tp_getset,
                        tp_getset.data()
                    });
                }
                slots.push_back({0, nullptr});
                initialized = true;
            }
            static PyType_Spec spec = {
                .name = typeid(Wrapper).name(),
                .basicsize = sizeof(CRTP),
                .itemsize = 0,
                .flags = flags,
                .slots = slots.data()
            };

            // instantiate the type object
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
                    &spec,
                    bases
                )
            );
            Py_XDECREF(bases);
            if (ptr(cls) == nullptr) {
                Exception::from_python();
            }

            // initialize the metaclass fields
            Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
            meta->initialize();
            meta->instancecheck = instancecheck;
            meta->subclasscheck = subclasscheck;
            meta->getters = std::move(class_getters);
            meta->setters = std::move(class_setters);

            // execute the callbacks to populate the type object
            for (auto&& [name, ctx] : context) {
                for (auto&& callback : ctx.callbacks) {
                    callback(cls);
                }
            }
            return cls;
        }

    };

    template <typename CRTP, typename Wrapper>
    template <StaticStr ModName>
    struct TypeTag::def<CRTP, Wrapper, void>::Bindings :
        BaseDef<CRTP, Wrapper, void>::template Bindings<ModName>
    {
        using Meta = typename Type<BertrandMeta>::__python__;
        using Base = BaseDef<CRTP, Wrapper, void>;
        using Base::Base;

        // TODO: convenience methods for exposing members, properties, methods,
        // etc. using the tp_methods, tp_getset, tp_members, etc. slots.
        // All pointers to members must be for the CRTP type, and will always be
        // owned by the CRTP type (so no need for variant).

        // TODO: this requires functions, which require types, which require the
        // metatype.  As a result, none of the metatype's behavior can rely on
        // functions, and none of the functions can rely on itself.  Thereafter,
        // all types should be more or less good to go.

    };

    template <typename CRTP, typename Wrapper, hashable CppType>
    struct TypeTag::DetectHashable<CRTP, Wrapper, CppType> {
        /* tp_hash delegates to `std::hash` if it exists. */
        static Py_hash_t __hash__(CRTP* self) {
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
    };

    template <typename CRTP, typename Wrapper, iterable CppType>
    struct TypeTag::DetectForwardIterable<CRTP, Wrapper, CppType> {
        /* tp_iter delegates to the type's begin() and end() iterators if they exist,
        and provides a thin Python wrapper around them that respects reference
        semantics and const-ness. */
        static PyObject* __iter__(CRTP* self) {
            try {
                PyObject* iter = std::visit(Visitor{
                    [self](CppType& cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::begin(std::declval<CppType>()));
                        using End = decltype(std::ranges::end(std::declval<CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::begin(cpp));
                        new (&iter->end) Iterator::End(std::ranges::end(cpp));
                        return iter;
                    },
                    [self](CppType* cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::begin(std::declval<CppType>()));
                        using End = decltype(std::ranges::end(std::declval<CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::begin(*cpp));
                        new (&iter->end) Iterator::End(std::ranges::end(*cpp));
                        return iter;
                    },
                    [self](const CppType* cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::begin(std::declval<const CppType>()));
                        using End = decltype(std::ranges::end(std::declval<const CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::begin(*cpp));
                        new (&iter->end) Iterator::End(std::ranges::end(*cpp));
                        return iter;
                    }
                }, self->m_cpp);
                PyObject_GC_Track(iter);
                return iter;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }
    };

    template <typename CRTP, typename Wrapper, reverse_iterable CppType>
    struct TypeTag::DetectReverseIterable<CRTP, Wrapper, CppType> {
        /* __reversed__ delegates to the type's rbegin() and rend() iterators if they
        exist, and provides a thin Python wrapper around them that respects reference
        semantics and const-ness. */
        static PyObject* __reversed__(CRTP* self) {
            try {
                PyObject* iter = std::visit(Visitor{
                    [self](CppType& cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::rbegin(std::declval<CppType>()));
                        using End = decltype(std::ranges::rend(std::declval<CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::rbegin(cpp));
                        new (&iter->end) Iterator::End(std::ranges::rend(cpp));
                        return iter;
                    },
                    [self](CppType* cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::rbegin(std::declval<CppType>()));
                        using End = decltype(std::ranges::rend(std::declval<CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::rbegin(*cpp));
                        new (&iter->end) Iterator::End(std::ranges::rend(*cpp));
                        return iter;
                    },
                    [self](const CppType* cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::rbegin(std::declval<const CppType>()));
                        using End = decltype(std::ranges::rend(std::declval<const CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::rbegin(*cpp));
                        new (&iter->end) Iterator::End(std::ranges::rend(*cpp));
                        return iter;
                    }
                }, self->m_cpp);
                PyObject_GC_Track(iter);
                return iter;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }
    };

    // TODO: reserve a C++ field in the metaclass for an overload set for each
    // relevant slot.  This avoids any extra overhead from dynamic Python accesses.
    // Then, expose the internal overload set as a `tp_members` field in the type,
    // so that overloads can be accessed and navigated.  I'll then just special case
    // the method<> helpers to recognize these reserved slots and populate them on
    // the metaclass rather than inserting into the instance dict.
    // -> What would be best is if all of these were implemented within the Bindings
    // class itself, so that whenever a method<> is defined with these targets, a
    // corresponding private method is added to the slots that delegates to the
    // appropriate member in the metaclass.  That can be done instead of raw-dogging
    // the slots directly in the type definition.  That might mean I can ignore CRTP
    // inspection entirely, and just rely on the __export__ script to define the
    // corresponding operators.

    template <typename CRTP, typename Wrapper, has_call_operator CppType>
    struct TypeTag::DetectCallable<CRTP, Wrapper, CppType> {
        // TODO: insert a vectorcall-based __call__ method here, which invokes an
        // overload set.  Perhaps the overload set can be stored internally as a C++
        // field for maximum performance.  Same with __init__/__new__.
    };

    template <typename CRTP, typename Wrapper, typename CppType>
    struct TypeTag::def :
        BaseDef<CRTP, Wrapper, CppType>,
        DetectHashable<CRTP, Wrapper, CppType>,
        DetectForwardIterable<CRTP, Wrapper, CppType>,
        DetectReverseIterable<CRTP, Wrapper, CppType>,
        DetectCallable<CRTP, Wrapper, CppType>
    {
    protected:
        using Variant = std::variant<CppType, CppType*, const CppType*>;

        template <StaticStr ModName>
        struct Bindings : BaseDef<CRTP, Wrapper, CppType>::template Bindings<ModName> {
        private:
            using Meta = typename Type<BertrandMeta>::__python__;
            using Base = BaseDef<CRTP, Wrapper, CppType>;

            /* Insert a Python type into the global `bertrand.python.type_map`
            dictionary, so that it can be mapped to its C++ equivalent and used
            when generating Python bindings. */
            template <typename Cls>
            static void register_type(Module<ModName>& mod, PyTypeObject* type);

        public:
            using Base::Base;

            /// TODO: all of these bindings could potentially be lifted to
            /// BaseDef::Bindings and centralized there.  This class would just do
            /// some extra introspection on the C++ type and call these helpers to
            /// register the appropriate slots just like any other method.

            /* Expose an immutable member variable to Python as a getset descriptor,
            which synchronizes its state. */
            template <StaticStr Name, typename T>
            static void var(const T CppType::*value) {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_member_var = true,
                };
                static bool skip = false;
                if (skip) {
                    return;
                }
                static auto get = [](PyObject* self, void* closure) -> PyObject* {
                    try {
                        CRTP* obj = reinterpret_cast<CRTP*>(self);
                        auto member_ptr = reinterpret_cast<const T CppType::*>(closure);
                        return std::visit(
                            Visitor{
                                [member_ptr](const CppType& obj) {
                                    if constexpr (impl::python_like<T>) {
                                        return Py_NewRef(ptr(obj.*member_ptr));
                                    } else {
                                        return release(wrap(obj.*member_ptr));
                                    }
                                },
                                [member_ptr](const CppType* obj) {
                                    if constexpr (impl::python_like<T>) {
                                        return Py_NewRef(ptr(obj->*member_ptr));
                                    } else {
                                        return release(wrap(obj->*member_ptr));
                                    }
                                }
                            },
                            obj->m_cpp
                        );
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) + "' is immutable.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                };
                Base::tp_getset.push_back({
                    Name,
                    +get,  // converts a stateless lambda to a function pointer
                    +set,
                    nullptr,
                    const_cast<void*>(reinterpret_cast<const void*>(value))
                });
                skip = true;
            }

            /* Expose a mutable member variable to Python as a getset descriptor, which
            synchronizes its state. */
            template <StaticStr Name, typename T>
            static void var(T CppType::*value) {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_member_var = true,
                };
                static bool skip = false;
                if (skip) {
                    return;
                }
                static auto get = [](PyObject* self, void* closure) -> PyObject* {
                    try {
                        CRTP* obj = reinterpret_cast<CRTP*>(self);
                        auto member_ptr = reinterpret_cast<T CppType::*>(closure);
                        return std::visit(
                            Visitor{
                                [member_ptr](CppType& obj) {
                                    if constexpr (impl::python_like<T>) {
                                        return Py_NewRef(ptr(obj.*member_ptr));
                                    } else {
                                        return release(wrap(obj.*member_ptr));
                                    }
                                },
                                [member_ptr](CppType* obj) {
                                    if constexpr (impl::python_like<T>) {
                                        return Py_NewRef(ptr(obj->*member_ptr));
                                    } else {
                                        return release(wrap(obj->*member_ptr));
                                    }
                                },
                                [member_ptr](const CppType* obj) {
                                    if constexpr (impl::python_like<T>) {
                                        return Py_NewRef(ptr(obj->*member_ptr));
                                    } else {
                                        return release(wrap(obj->*member_ptr));
                                    }
                                }
                            },
                            obj->m_cpp
                        );
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                    if (new_val == nullptr) {
                        std::string msg = "variable '" + Name + "' of type '" +
                            impl::demangle(typeid(Wrapper).name()) +
                            "' cannot be deleted.";
                        PyErr_SetString(PyExc_TypeError, msg.c_str());
                        return -1;
                    }
                    try {
                        CRTP* obj = reinterpret_cast<CRTP*>(self);
                        auto member_ptr = reinterpret_cast<T CppType::*>(closure);
                        std::visit(
                            Visitor{
                                [member_ptr, new_val](CppType& obj) {
                                    obj.*member_ptr = static_cast<T>(
                                        reinterpret_borrow<Object>(new_val)
                                    );
                                },
                                [member_ptr, new_val](CppType* obj) {
                                    obj->*member_ptr = static_cast<T>(
                                        reinterpret_borrow<Object>(new_val)
                                    );
                                },
                                [new_val](const CppType* obj) {
                                    throw TypeError(
                                        "variable '" + Name + "' of type '" +
                                        impl::demangle(typeid(Wrapper).name()) +
                                        "' is immutable."
                                    );
                                }
                            },
                            obj->m_cpp
                        );
                        return 0;
                    } catch (...) {
                        Exception::to_python();
                        return -1;
                    }
                };
                Base::tp_getset.push_back({
                    Name,
                    +get,  // converts a stateless lambda to a function pointer
                    +set,
                    nullptr,  // doc
                    reinterpret_cast<void*>(value)
                });
                skip = true;
            }

            /* Expose an immutable static variable to Python using the `__getattr__()`
            slot of the metatype, which synchronizes its state. */
            template <StaticStr Name, typename T>
            void classvar(const T& value) {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_class_var = true,
                };
                static auto get = [](PyObject* cls, void* closure) -> PyObject* {
                    try {
                        const T* value = reinterpret_cast<const T*>(closure);
                        if constexpr (impl::python_like<T>) {
                            return Py_NewRef(ptr(*value));
                        } else {
                            return release(wrap(*value));
                        }
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) + "' is immutable.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                };
                Base::class_getters[Name] = {
                    +get,
                    const_cast<void*>(reinterpret_cast<const void*>(&value))
                };
                Base::class_setters[Name] = {
                    +set,
                    const_cast<void*>(reinterpret_cast<const void*>(&value))
                };
            }

            /* Expose a mutable static variable to Python using the `__getattr__()` and
            `__setattr__()` slots of the metatype, which synchronizes its state.  */
            template <StaticStr Name, typename T>
            void classvar(T& value) {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_class_var = true,
                };
                static auto get = [](PyObject* self, void* value) -> PyObject* {
                    try {
                        T* value = reinterpret_cast<T*>(value);
                        if constexpr (impl::python_like<T>) {
                            return Py_NewRef(ptr(*value));
                        } else {
                            return release(wrap(*value));
                        }
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                static auto set = [](PyObject* self, PyObject* new_val, void* value) -> int {
                    if (new_val == nullptr) {
                        std::string msg = "variable '" + Name + "' of type '" +
                            impl::demangle(typeid(Wrapper).name()) +
                            "' cannot be deleted.";
                        PyErr_SetString(PyExc_TypeError, msg.c_str());
                        return -1;
                    }
                    try {
                        *reinterpret_cast<T*>(value) = static_cast<T>(
                            reinterpret_borrow<Object>(new_val)
                        );
                        return 0;
                    } catch (...) {
                        Exception::to_python();
                        return -1;
                    }
                };
                Base::class_getters[Name] = {+get, reinterpret_cast<void*>(&value)};
                Base::class_setters[Name] = {+set, reinterpret_cast<void*>(&value)};
            }

            /* Expose a C++-style const getter to Python as a getset descriptor. */
            template <StaticStr Name, typename Return>
            static void property(Return(CppType::*getter)() const) {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_property = true,
                };
                static bool skip = false;
                if (skip) {
                    return;
                }
                static auto get = [](PyObject* self, void* closure) -> PyObject* {
                    try {
                        CRTP* obj = reinterpret_cast<CRTP*>(self);
                        auto member_ptr = reinterpret_cast<Return(CppType::*)() const>(closure);
                        return release(wrap(std::visit(
                            Visitor{
                                [member_ptr](const CppType& obj) {
                                    if constexpr (std::is_lvalue_reference_v<Return>) {
                                        if constexpr (impl::python_like<Return>) {
                                            return Py_NewRef(ptr((obj.*member_ptr)()));
                                        } else {
                                            return release(wrap((obj.*member_ptr)()));
                                        }
                                    } else {
                                        return release(as_object(
                                            (obj.*member_ptr)()
                                        ));
                                    }
                                },
                                [member_ptr](const CppType* obj) {
                                    if constexpr (std::is_lvalue_reference_v<Return>) {
                                        if constexpr (impl::python_like<Return>) {
                                            return Py_NewRef(ptr((obj->*member_ptr)()));
                                        } else {
                                            return release(wrap((obj->*member_ptr)()));
                                        }
                                    } else {
                                        return release(as_object(
                                            (obj->*member_ptr)()
                                        ));
                                    }
                                }
                            },
                            obj->m_cpp
                        )));
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                Base::tp_getset.push_back({
                    Name,
                    +get,  // converts a stateless lambda to a function pointer
                    nullptr,  // setter
                    nullptr,  // doc
                    const_cast<void*>(reinterpret_cast<const void*>(getter))
                });
                skip = true;
            }

            /* Expose a C++-style getter/setter pair to Python as a getset
            descriptor. */
            template <StaticStr Name, typename Return, typename Value>
            static void property(
                Return(CppType::*getter)() const,
                void(CppType::*setter)(Value&&)
            ) {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_property = true,
                };
                static bool skip = false;
                if (skip) {
                    return;
                }
                static struct Closure {
                    Return(CppType::*getter)() const;
                    void(CppType::*setter)(Value&&);
                } closure = {getter, setter};
                static auto get = [](PyObject* self, void* closure) -> PyObject* {
                    try {
                        CRTP* obj = reinterpret_cast<CRTP*>(self);
                        Closure* ctx = reinterpret_cast<Closure*>(closure);
                        return release(wrap(std::visit(
                            Visitor{
                                [ctx](CppType& obj) {
                                    if constexpr (std::is_lvalue_reference_v<Return>) {
                                        if constexpr (impl::python_like<Return>) {
                                            return Py_NewRef(ptr(
                                                (obj.*(ctx->getter))()
                                            ));
                                        } else {
                                            return release(wrap(
                                                (obj.*(ctx->getter))()
                                            ));
                                        }
                                    } else {
                                        return release(as_object(
                                            (obj.*(ctx->getter))()
                                        ));
                                    }
                                },
                                [ctx](CppType* obj) {
                                    if constexpr (std::is_lvalue_reference_v<Return>) {
                                        if constexpr (impl::python_like<Return>) {
                                            return Py_NewRef(ptr(
                                                (obj->*(ctx->getter))()
                                            ));
                                        } else {
                                            return release(wrap(
                                                (obj->*(ctx->getter))()
                                            ));
                                        }
                                    } else {
                                        return release(as_object(
                                            (obj->*(ctx->getter))()
                                        ));
                                    }
                                },
                                [ctx](const CppType* obj) {
                                    if constexpr (std::is_lvalue_reference_v<Return>) {
                                        if constexpr (impl::python_like<Return>) {
                                            return Py_NewRef(ptr(
                                                (obj->*(ctx->getter))()
                                            ));
                                        } else {
                                            return release(wrap(
                                                (obj->*(ctx->getter))()
                                            ));
                                        }
                                    } else {
                                        return release(as_object(
                                            (obj->*(ctx->getter))()
                                        ));
                                    }
                                }
                            },
                            obj->m_cpp
                        )));
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                    if (new_val == nullptr) {
                        std::string msg = "variable '" + Name + "' of type '" +
                            impl::demangle(typeid(Wrapper).name()) +
                            "' cannot be deleted.";
                        PyErr_SetString(PyExc_TypeError, msg.c_str());
                        return -1;
                    }
                    try {
                        CRTP* obj = reinterpret_cast<CRTP*>(self);
                        Closure* ctx = reinterpret_cast<Closure*>(closure);
                        std::visit(
                            Visitor{
                                [ctx, new_val](CppType& obj) {
                                    obj.*(ctx->setter)(static_cast<Value>(
                                        reinterpret_borrow<Object>(new_val)
                                    ));
                                },
                                [ctx, new_val](CppType* obj) {
                                    obj->*(ctx->setter)(static_cast<Value>(
                                        reinterpret_borrow<Object>(new_val)
                                    ));
                                },
                                [new_val](const CppType* obj) {
                                    throw TypeError(
                                        "variable '" + Name + "' of type '" +
                                        impl::demangle(typeid(Wrapper).name()) +
                                        "' is immutable."
                                    );
                                }
                            },
                            obj->m_cpp
                        );
                        return 0;
                    } catch (...) {
                        Exception::to_python();
                        return -1;
                    }
                };
                tp_getset.push_back({
                    Name,
                    +get,  // converts a stateless lambda to a function pointer
                    +set,
                    nullptr,  // doc
                    &closure
                });
                skip = true;
            }

            /* Expose a C++-style static getter to Python using the `__getattr__()`
            slot of the metatype. */
            template <StaticStr Name, typename Return>
            void classproperty(Return(*getter)()) {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_class_property = true,
                };
                static auto get = [](PyObject* cls, void* closure) -> PyObject* {
                    try {
                        Return(*func)() = reinterpret_cast<Return(*)()>(closure);
                        if constexpr (std::is_lvalue_reference_v<Return>) {
                            if constexpr (impl::python_like<Return>) {
                                return Py_NewRef(ptr(func()));
                            } else {
                                return release(wrap(func()));
                            }
                        } else {
                            return release(as_object(func()));
                        }
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                Base::class_getters[Name] = {+get, reinterpret_cast<void*>(getter)};
            }

            /* Expose a C++-style static getter/setter pair to Python using the
            `__getattr__()` and `__setattr__()` slots of the metatype. */
            template <StaticStr Name, typename Return, typename Value>
            void classproperty(Return(*getter)(), void(*setter)(Value&&)) {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_class_property = true,
                };
                static struct Closure {
                    Return(*getter)();
                    void(*setter)(Value&&);
                } closure = {getter, setter};
                static auto get = [](PyObject* cls, void* closure) -> PyObject* {
                    try {
                        Closure* ctx = reinterpret_cast<Closure*>(closure);
                        if constexpr (std::is_lvalue_reference_v<Return>) {
                            if constexpr (impl::python_like<Return>) {
                                return Py_NewRef(ptr(ctx->getter()));
                            } else {
                                return release(wrap(ctx->getter()));
                            }
                        } else {
                            return release(as_object(ctx->getter()));
                        }
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                static auto set = [](PyObject* cls, PyObject* new_val, void* closure) -> int {
                    if (new_val == nullptr) {
                        std::string msg = "variable '" + Name + "' of type '" +
                            impl::demangle(typeid(Wrapper).name()) +
                            "' cannot be deleted.";
                        PyErr_SetString(PyExc_TypeError, msg.c_str());
                        return -1;
                    }
                    try {
                        reinterpret_cast<Closure*>(closure)->setter(static_cast<Value>(
                            reinterpret_borrow<Object>(new_val)
                        ));
                        return 0;
                    } catch (...) {
                        Exception::to_python();
                        return -1;
                    }
                };
                Base::class_getters[Name] = {+get, reinterpret_cast<void*>(&closure)};
                Base::class_setters[Name] = {+set, reinterpret_cast<void*>(&closure)};
            }

            /* Expose a nested py::Object type to Python. */
            template <
                StaticStr Name,
                std::derived_from<Object> Cls,
                std::derived_from<Object>... Bases
            > requires (
                !impl::is_generic<Cls> &&
                (impl::has_type<Cls> && impl::is_type<Type<Cls>>) &&
                ((impl::has_type<Bases> && impl::is_type<Type<Bases>>) && ...)
            )
            void type(std::string&& doc = "") {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                static std::string docstring = std::move(doc);
                Base::context[Name] = {
                    .is_type = true,
                    .callbacks = {
                        [](Type<Wrapper>& type) {
                            Module<ModName> mod = reinterpret_steal<Module<ModName>>(
                                PyType_GetModule(ptr(type))
                            );
                            if (ptr(mod) == nullptr) {
                                Exception::from_python();
                            }
                            Type<Cls> cls = Type<Cls>::__python__::__export__(mod);

                            if (PyObject_SetAttr(ptr(type), Name, ptr(cls))) {
                                Exception::from_python();
                            }

                            // insert into the module's C++ type map for fast lookup
                            // using C++ template syntax
                            using Mod = Module<ModName>::__python__;
                            reinterpret_cast<Mod*>(ptr(mod))->type_map[typeid(Cls)] =
                                reinterpret_cast<PyTypeObject*>(ptr(cls));

                            // insert into the global type map for use when generating
                            // C++ bindings from Python type hints
                            register_type<Cls>(
                                mod,
                                reinterpret_cast<PyTypeObject*>(ptr(cls))
                            );
                        }
                    }
                };
            }

            /* Expose a nested and templated py::Object type to Python. */
            template <
                StaticStr Name,
                template <typename...> typename Cls
            >
            void type() {
                if (Base::context.contains(Name)) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) +
                        "' already has an attribute named '" + Name + "'."
                    );
                }
                Base::context[Name] = {
                    .is_type = true,
                    .is_template_interface = true,
                    .callbacks = {
                        [](Type<Wrapper>& type) {
                            Module<ModName> mod = reinterpret_steal<Module<ModName>>(
                                PyType_GetModule(ptr(type))
                            );
                            if (ptr(mod) == nullptr) {
                                Exception::from_python();
                            }
                            BertrandMeta stub = Meta::stub_type<Name, Cls>(mod);
                            if (PyObject_SetAttr(ptr(type), Name, ptr(stub))) {
                                Exception::from_python();
                            }
                        }
                    }
                };
            }

            /* Expose a template instantiation of a nested py::Object type to Python. */
            template <
                StaticStr Name,
                std::derived_from<Object> Cls,
                std::derived_from<Object>... Bases
            > requires (
                impl::is_generic<Cls> &&
                (impl::has_type<Cls> && impl::is_type<Type<Cls>>) &&
                ((impl::has_type<Bases> && impl::is_type<Type<Bases>>) && ...)
            )
            void type() {
                auto it = Base::context.find(Name);
                if (it == Base::context.end()) {
                    throw TypeError(
                        "No template interface found for type '" + Name + "' in "
                        "class '" + impl::demangle(typeid(Wrapper).name()) + "' "
                        "with specialization '" +
                        impl::demangle(typeid(Cls).name()) + "'.  Did you "
                        "forget to register the unspecialized template first?"
                    );
                } else if (!it->second.is_template_interface) {
                    throw AttributeError(
                        "Class '" + impl::demangle(typeid(Wrapper).name()) + "' "
                        "already has an attribute named '" + Name + "'."
                    );
                }
                it->second.callbacks.push_back([](Type<Wrapper>& type) {
                    // get the template interface with the same name
                    BertrandMeta existing = reinterpret_steal<BertrandMeta>(
                        PyObject_GetAttr(
                            ptr(type),
                            impl::TemplateString<Name>::ptr
                        )
                    );
                    if (ptr(existing) == nullptr) {
                        Exception::from_python();
                    }

                    // call the type's __export__() method
                    Module<ModName> mod = reinterpret_steal<Module<ModName>>(
                        PyType_GetModule(ptr(type))
                    );
                    if (ptr(mod) == nullptr) {
                        Exception::from_python();
                    }
                    Type<Cls> cls = Type<Cls>::__python__::__export__(mod);

                    // Insert the template interface into the type's __bases__
                    PyTypeObject* pycls = reinterpret_cast<PyTypeObject*>(ptr(cls));
                    PyObject* curr_bases = pycls->tp_bases;
                    PyObject* add_base = PyTuple_Pack(1, ptr(existing));
                    if (add_base == nullptr) {
                        Exception::from_python();
                    }
                    PyObject* new_bases = PySequence_Concat(curr_bases, add_base);
                    Py_DECREF(add_base);
                    if (new_bases == nullptr) {
                        Exception::from_python();
                    }
                    pycls->tp_bases = new_bases;
                    Py_DECREF(curr_bases);
                    PyType_Modified(pycls);

                    // insert into the template interface's __getitem__ dict
                    PyObject* key = PyTuple_Pack(
                        sizeof...(Bases) + 1,
                        ptr(Type<Bases>())...
                    );
                    if (key == nullptr) {
                        Exception::from_python();
                    }
                    if (PyDict_SetItem(
                        reinterpret_cast<typename Type<BertrandMeta>::__python__*>(
                            ptr(existing)
                        )->template_instantiations,
                        key,
                        ptr(cls)
                    )) {
                        Py_DECREF(key);
                        Exception::from_python();
                    }
                    Py_DECREF(key);

                    // insert into the module's C++ type map for fast lookup
                    // using C++ template syntax
                    reinterpret_cast<CRTP*>(ptr(mod))->type_map[typeid(Cls)] =
                        reinterpret_cast<PyTypeObject*>(ptr(cls));

                    // insert into the global type map for use when generating C++
                    // bindings from Python type hints
                    register_type<Cls>(
                        mod,
                        reinterpret_cast<PyTypeObject*>(ptr(cls))
                    );
                });
            }

            /* Expose a C++ instance method to Python as an instance method, which can
            be overloaded from either side of the language boundary.  */
            template <StaticStr Name, typename Return, typename... Target>
            void method(Return(CppType::*func)(Target...)) {
                // TODO: generate an overload set (requires function refactor)

                // func is a pointer to a member function of CppType, which can be
                // called like this:
                //      obj.*func(std::forward<Args>(args)...);
            }

            /* Expose a C++ static method to Python as a class method, which can be
            overloaded from either side of the language boundary.  */
            template <StaticStr Name, typename Return, typename... Target>
            void classmethod(Return(*func)(Target...)) {
                // TODO: generate an overload set (requires function refactor)

                // func is a normal function pointer
            }

            /* Expose a C++ static method to Python as a static method, which can be
            overloaded from either side of the language boundary.  */
            template <StaticStr Name, typename Return, typename... Target>
            void staticmethod(Return(*func)(Target...)) {
                // TODO: generate an overload set (requires function refactor)

                // func is a normal function pointer
            }

            Type<Wrapper> finalize(unsigned int tp_flags =
                Py_TPFLAGS_BASETYPE | Py_TPFLAGS_MANAGED_WEAKREF | Py_TPFLAGS_MANAGED_DICT
            ) {
                auto result = Base::finalize(tp_flags);
                if constexpr (impl::iterable<CppType>) {
                    register_iterator<Iterator<
                        decltype(std::ranges::begin(std::declval<CppType>())),
                        decltype(std::ranges::end(std::declval<CppType>()))
                    >>(result);
                }
                if constexpr (impl::iterable<const CppType>) {
                    register_iterator<Iterator<
                        decltype(std::ranges::begin(std::declval<const CppType>())),
                        decltype(std::ranges::end(std::declval<const CppType>()))
                    >>(result);
                }
                if constexpr (impl::reverse_iterable<CppType>) {
                    register_iterator<Iterator<
                        decltype(std::ranges::rbegin(std::declval<CppType>())),
                        decltype(std::ranges::rend(std::declval<CppType>()))
                    >>(result);
                }
                if constexpr (impl::reverse_iterable<const CppType>) {
                    register_iterator<Iterator<
                        decltype(std::ranges::rbegin(std::declval<const CppType>())),
                        decltype(std::ranges::rend(std::declval<const CppType>()))
                    >>(result);
                }
                return result;
            }

        private:

            template <typename T>
            static void register_iterator(Type<Wrapper>& type) {
                static PyType_Slot slots[] = {
                    {
                        Py_tp_iter,
                        reinterpret_cast<void*>(PyObject_SelfIter)
                    },
                    {
                        Py_tp_iternext,
                        reinterpret_cast<void*>(T::__next__)
                    },
                    {0, nullptr}
                };
                static PyType_Spec spec = {
                    .name = typeid(T).name(),
                    .basicsize = sizeof(T),
                    .itemsize = 0,
                    .flags =
                        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
                        Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE |
                        Py_TPFLAGS_DISALLOW_INSTANTIATION,
                    .slots = slots
                };
                PyObject* cls = PyType_FromModuleAndSpec(
                    ptr(Base::module),
                    &spec,
                    nullptr
                );
                if (cls == nullptr) {
                    Exception::from_python();
                }
                int rc = PyObject_SetAttrString(ptr(type), typeid(T).name(), cls);
                Py_DECREF(cls);
                if (rc) {
                    Exception::from_python();
                }
            }

        };

    public:
        using t_cpp = CppType;

        static constexpr Origin __origin__ = Origin::CPP;
        static constexpr StaticStr __doc__ = [] {
            return (
                "A Bertrand-generated Python wrapper for the '" +
                impl::demangle(typeid(CppType).name()) + "' C++ type."
            );
        }();
        PyObject_HEAD
        Variant m_cpp;

        template <typename Begin, std::sentinel_for<Begin> End>
        using Iterator = TypeTag::Iterator<Begin, End>;

        /* C++ constructor that forwards to the wrapped object's constructor.  This is
        called from the Wrapper's C++ constructors to convert a C++ value into a Python
        object outside the Python-level __new__/__init__ sequence.  It can be overridden
        if the object type includes more members than just the wrapped C++ object, but
        this will generally never occur.  Any required fields should be contained
        within the C++ object itself. */
        template <typename... Args>
        static Wrapper __create__(Args&&... args) {
            PyTypeObject* type = reinterpret_cast<PyTypeObject*>(ptr(Type<Wrapper>()));
            Object self = reinterpret_steal<Object>(type->tp_alloc(type, 0));
            if (ptr(self) == nullptr) {
                Exception::from_python();
            }
            CRTP* obj = reinterpret_cast<CRTP*>(ptr(self));
            new (&obj->m_cpp) CppType(std::forward<Args>(args)...);
            return reinterpret_steal<Wrapper>(release(self));
        }

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

        /* tp_dealloc calls the ordinary C++ destructor. */
        static void __dealloc__(CRTP* self) {
            PyObject_GC_UnTrack(self);  // required for heap types
            self->~CRTP();  // cleans up all C++ resources
            PyTypeObject* type = Py_TYPE(self);
            type->tp_free(self);
            Py_DECREF(type);  // required for heap types
        }

        // TODO: maybe there needs to be some handling to make sure that the container
        // yields specifically lvalue references to Python objects?  I might also need
        // to const_cast them in the __clear__ method in order to properly release
        // everything.

        /* tp_traverse registers any Python objects that are owned by the C++ object
        with Python's cyclic garbage collector. */
        static int __traverse__(CRTP* self, visitproc visit, void* arg) {
            if constexpr (
                impl::iterable<CppType> &&
                std::derived_from<std::decay_t<impl::iter_type<CppType>>, Object>
            ) {
                if (std::holds_alternative<CppType>(self->m_cpp)) {
                    for (auto&& item : std::get<CppType>(self->m_cpp)) {
                        Py_VISIT(ptr(item));
                    }
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
            if constexpr (
                impl::iterable<CppType> &&
                std::derived_from<std::decay_t<impl::iter_type<CppType>>, Object>
            ) {
                if (std::holds_alternative<CppType>(self->m_cpp)) {
                    /// NOTE: In order to avoid a double free, we release() all of the
                    /// container's items here, so that when their destructors are called
                    /// normally in __dealloc__(), they see a null pointer and do nothing.
                    for (auto&& item : std::get<CppType>(self->m_cpp)) {
                        Py_XDECREF(release(item));
                    }
                }
            }
            return 0;
        }

        /* TODO: implement the following slots using metaprogramming where possible:
        *
        // * tp_name
        // * tp_basicsize
        // * tp_itemsize
        // * tp_dealloc
        // * tp_repr
        // * tp_hash
        // * tp_vectorcall_offset  // needs some extra work in PyMemberDef
        // * tp_dictoffset (or Py_TPFLAGS_MANAGED_DICT)
        // * tp_weaklistoffset (or Py_TPFLAGS_MANAGED_WEAKREF)
        * tp_call
        // * tp_str
        // * tp_traverse
        // * tp_clear
        // * tp_iter
        * tp_iternext
        * tp_init
        // * tp_alloc  // -> leave blank
        * tp_new  // -> always does C++-level initialization.
        // * tp_free  // leave blank
        // * tp_is_gc  // leave blank
        // * tp_finalize  // leave blank
        // * tp_vectorcall  (not valid for heap types?)
        *
        * to be defined in subclasses (check for dunder methods on CRTP type?):
        *
        * tp_as_number
        * tp_as_sequence
        * tp_as_mapping
        * tp_as_async
        * tp_getattro
        * tp_setattro
        * tp_as_buffer
        // * tp_doc
        * tp_richcompare
        * tp_methods
        * tp_members
        * tp_getset
        // * tp_bases
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
        */

    private:
        inline static std::vector<PyMethodDef> tp_methods;
        inline static std::vector<PyGetSetDef> tp_getset;
        inline static std::vector<PyMemberDef> tp_members;

        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::originates_from_cpp<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::CppType>
            )
        friend auto wrap(const T& obj) -> __as_object__<T>::type;
        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::originates_from_cpp<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::CppType>
            )
        friend auto wrap(T& obj) -> __as_object__<T>::type;
        template <typename T> requires (impl::cpp_or_originates_from_cpp<T>)
        friend auto& unwrap(T& obj);
        template <typename T> requires (impl::cpp_or_originates_from_cpp<T>)
        friend auto& unwrap(const T& obj);

        /* Implements py::wrap() for immutable references. */
        static Wrapper _wrap(const CppType& cpp) {
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
        static Wrapper _wrap(CppType& cpp) {
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

    };

}


/// NOTE: all types must manually define __as_object__ as they are generated, since it
/// can't be deduced any other way.


/// TODO: what about isinstance/issubclass?


template <impl::originates_from_cpp Self, typename T>
    requires (std::convertible_to<T, impl::cpp_type<Self>>)
struct __init__<Self, T>                                    : Returns<Self> {
    static Self operator()(T&& value) {
        return Type<Self>::__python__::__create__(std::forward<T>(value));
    }
};


template <impl::originates_from_cpp Self, typename... Args>
    requires (std::constructible_from<impl::cpp_type<Self>, Args...>)
struct __explicit_init__<Self, Args...>                     : Returns<Self> {
    static Self operator()(Args&&... args) {
        return Type<Self>::__python__::__create__(std::forward<Args>(args)...);
    }
};


template <impl::originates_from_cpp Self, typename T>
    requires (std::convertible_to<impl::cpp_type<Self>, T>)
struct __cast__<Self, T>                                    : Returns<T> {
    static T operator()(const Self& self) {
        return unwrap(self);
    }
};


template <impl::originates_from_cpp Self, typename T>
    requires (impl::explicitly_convertible_to<impl::cpp_type<Self>, T>)
struct __explicit_cast__<Self, T>                           : Returns<T> {
    static T operator()(const Self& self) {
        return static_cast<T>(unwrap(self));
    }
};


template <impl::originates_from_cpp Self, typename... Args>
    requires (std::is_invocable_v<impl::cpp_type<Self>, Args...>)
struct __call__<Self, Args...> : Returns<std::invoke_result_t<impl::cpp_type<Self>, Args...>> {};


/// TODO: __getattr__/__setattr__/__delattr__ must be defined for each type, and cannot
/// be deduced here.


template <impl::originates_from_cpp Self, typename... Key>
    requires (impl::supports_lookup<impl::cpp_type<Self>, Key...>)
struct __getitem__<Self, Key...> : Returns<impl::lookup_type<impl::cpp_type<Self>, Key...>> {};


template <impl::originates_from_cpp Self, typename Value, typename... Key>
    requires (impl::supports_item_assignment<impl::cpp_type<Self>, Value, Key...>)
struct __setitem__<Self, Value, Key...> : Returns<void> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_size<impl::cpp_type<Self>>)
struct __len__<Self> : Returns<size_t> {};


template <impl::originates_from_cpp Self>
    requires (impl::iterable<impl::cpp_type<Self>>)
struct __iter__<Self> : Returns<impl::iter_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::reverse_iterable<impl::cpp_type<Self>>)
struct __reversed__<Self> : Returns<impl::reverse_iter_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self, typename Key>
    requires (impl::has_contains<impl::cpp_type<Self>, Key>)
struct __contains__<Self, Key> : Returns<bool> {};


template <impl::originates_from_cpp Self>
    requires (impl::hashable<impl::cpp_type<Self>>)
struct __hash__<Self> : Returns<size_t> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_abs<impl::cpp_type<Self>>)
struct __abs__<Self> : Returns<impl::abs_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_pos<impl::cpp_type<Self>>)
struct __pos__<Self> : Returns<impl::pos_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_neg<impl::cpp_type<Self>>)
struct __neg__<Self> : Returns<impl::neg_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_preincrement<impl::cpp_type<Self>>)
struct __increment__<Self> : Returns<impl::preincrement_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_predecrement<impl::cpp_type<Self>>)
struct __decrement__<Self> : Returns<impl::predecrement_type<impl::cpp_type<Self>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_lt<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __lt__<L, R> : Returns<impl::lt_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_le<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __le__<L, R> : Returns<impl::le_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_eq<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __eq__<L, R> : Returns<impl::eq_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ne<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ne__<L, R> : Returns<impl::ne_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ge<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ge__<L, R> : Returns<impl::ge_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_gt<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __gt__<L, R> : Returns<impl::gt_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_add<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __add__<L, R> : Returns<impl::add_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_iadd<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __iadd__<L, R> : Returns<impl::iadd_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_sub<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __sub__<L, R> : Returns<impl::sub_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_isub<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __isub__<L, R> : Returns<impl::isub_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_mul<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __mul__<L, R> : Returns<impl::mul_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_imul<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __imul__<L, R> : Returns<impl::imul_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_truediv<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __truediv__<L, R> : Returns<impl::truediv_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_itruediv<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __itruediv__<L, R> : Returns<impl::itruediv_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_mod<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __mod__<L, R> : Returns<impl::mod_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_imod<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __imod__<L, R> : Returns<impl::imod_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_pow<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __pow__<L, R> : Returns<impl::pow_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_lshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __lshift__<L, R> : Returns<impl::lshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ilshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ilshift__<L, R> : Returns<impl::ilshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_rshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __rshift__<L, R> : Returns<impl::rshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_irshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __irshift__<L, R> : Returns<impl::irshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_and<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __and__<L, R> : Returns<impl::and_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_iand<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __iand__<L, R> : Returns<impl::iand_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_or<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __or__<L, R> : Returns<impl::or_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ior<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ior__<L, R> : Returns<impl::ior_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_xor<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __xor__<L, R> : Returns<impl::xor_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ixor<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ixor__<L, R> : Returns<impl::ixor_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


////////////////////
////    CODE    ////
////////////////////


template <>
struct Type<Code>;


template <>
struct Interface<Type<Code>> {
    [[nodiscard]] static Code compile(const std::string& source);
    [[nodiscard]] static Py_ssize_t line_number(const Code& self) noexcept;
    [[nodiscard]] static Py_ssize_t argcount(const Code& self) noexcept;
    [[nodiscard]] static Py_ssize_t posonlyargcount(const Code& self) noexcept;
    [[nodiscard]] static Py_ssize_t kwonlyargcount(const Code& self) noexcept;
    [[nodiscard]] static Py_ssize_t nlocals(const Code& self) noexcept;
    [[nodiscard]] static Py_ssize_t stacksize(const Code& self) noexcept;
    [[nodiscard]] static int flags(const Code& self) noexcept;

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] static Str filename(const Code& self);
    [[nodiscard]] static Str name(const Code& self);
    [[nodiscard]] static Str qualname(const Code& self);
    [[nodiscard]] static Tuple<Str> varnames(const Code& self);
    [[nodiscard]] static Tuple<Str> cellvars(const Code& self);
    [[nodiscard]] static Tuple<Str> freevars(const Code& self);
    [[nodiscard]] static Bytes bytecode(const Code& self);
    [[nodiscard]] static Tuple<Object> consts(const Code& self);
    [[nodiscard]] static Tuple<Str> names(const Code& self);
};


template <>
struct Type<Code> : Object, Interface<Type<Code>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(
                reinterpret_cast<PyObject*>(&PyCode_Type)
            );
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline Code Interface<Type<Code>>::compile(const std::string& source) {
    return Code::compile(source);
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::line_number(const Code& self) noexcept {
    return self.line_number;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::argcount(const Code& self) noexcept {
    return self.argcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::posonlyargcount(const Code& self) noexcept {
    return self.posonlyargcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::kwonlyargcount(const Code& self) noexcept {
    return self.kwonlyargcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::nlocals(const Code& self) noexcept {
    return self.nlocals;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::stacksize(const Code& self) noexcept {
    return self.stacksize;
}
[[nodiscard]] inline int Interface<Type<Code>>::flags(const Code& self) noexcept {
    return self.flags;
}


/////////////////////
////    FRAME    ////
/////////////////////


template <>
struct Type<Frame>;


template <>
struct Interface<Type<Frame>> {
    [[nodiscard]] static std::string to_string(const Frame& self);
    [[nodiscard]] static std::optional<Code> code(const Frame& self);
    [[nodiscard]] static std::optional<Frame> back(const Frame& self);
    [[nodiscard]] static size_t line_number(const Frame& self);
    [[nodiscard]] static size_t last_instruction(const Frame& self);
    [[nodiscard]] static std::optional<Object> generator(const Frame& self);

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] static Object get(const Frame& self, const Str& name);
    [[nodiscard]] static Dict<Str, Object> builtins(const Frame& self);
    [[nodiscard]] static Dict<Str, Object> globals(const Frame& self);
    [[nodiscard]] static Dict<Str, Object> locals(const Frame& self);
};


template <>
struct Type<Frame> : Object, Interface<Type<Frame>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(
                reinterpret_cast<PyObject*>(&PyFrame_Type)
            );
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<Frame>>::to_string(const Frame& self) {
    return self.to_string();
}
[[nodiscard]] inline std::optional<Code> Interface<Type<Frame>>::code(const Frame& self) {
    return self.code;
}
[[nodiscard]] inline std::optional<Frame> Interface<Type<Frame>>::back(const Frame& self) {
    return self.back;
}
[[nodiscard]] inline size_t Interface<Type<Frame>>::line_number(const Frame& self) {
    return self.line_number;
}
[[nodiscard]] inline size_t Interface<Type<Frame>>::last_instruction(const Frame& self) {
    return self.last_instruction;
}
[[nodiscard]] inline std::optional<Object> Interface<Type<Frame>>::generator(const Frame& self) {
    return self.generator;
}


/////////////////////////
////    TRACEBACK    ////
/////////////////////////


template <>
struct Type<Traceback>;


template <>
struct Interface<Type<Traceback>> {
    [[nodiscard]] static std::string to_string(const Traceback& self);
};


template <>
struct Type<Traceback> : Object, Interface<Type<Traceback>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(
                reinterpret_cast<PyObject*>(&PyTraceBack_Type)
            );
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<Traceback>>::to_string(const Traceback& self) {
    return self.to_string();
}


///////////////////////////////
////    EXCEPTION TYPES    ////
///////////////////////////////


template <>
struct Type<Exception>;


template <>
struct Interface<Type<Exception>> {
    [[noreturn, clang::noinline]] static void from_python();  // defined in __init__.h
    static void to_python();
};


template <>
struct Type<Exception> : Object, Interface<Type<Exception>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(PyExc_Exception);
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


inline void Interface<Type<Exception>>::to_python() {
    Exception::to_python();
}


#define BUILTIN_EXCEPTION_TYPE(CLS, BASE, PYTYPE)                                       \
    template <>                                                                         \
    struct Type<CLS>;                                                                   \
                                                                                        \
    template <>                                                                         \
    struct Interface<Type<CLS>> : Interface<Type<BASE>> {};                             \
                                                                                        \
    template <>                                                                         \
    struct Type<CLS> : Object, Interface<Type<CLS>>, impl::TypeTag {                    \
        struct __python__ : TypeTag::def<__python__, CLS> {                             \
            static Type __import__() {                                                  \
                return reinterpret_borrow<Type>(PYTYPE);                                \
            }                                                                           \
        };                                                                              \
                                                                                        \
        Type(Handle h, borrowed_t t) : Object(h, t) {}                                  \
        Type(Handle h, stolen_t t) : Object(h, t) {}                                    \
                                                                                        \
        template <typename... Args>                                                     \
            requires (implicit_ctor<Type>::template enable<Args...>)                    \
        Type(Args&&... args) : Object(                                                  \
            implicit_ctor<Type>{},                                                      \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
                                                                                        \
        template <typename... Args>                                                     \
            requires (explicit_ctor<Type>::template enable<Args...>)                    \
        explicit Type(Args&&... args) : Object(                                         \
            explicit_ctor<Type>{},                                                      \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
    };


BUILTIN_EXCEPTION_TYPE(ArithmeticError, Exception, PyExc_ArithmeticError)
    BUILTIN_EXCEPTION_TYPE(FloatingPointError, ArithmeticError, PyExc_FloatingPointError)
    BUILTIN_EXCEPTION_TYPE(OverflowError, ArithmeticError, PyExc_OverflowError)
    BUILTIN_EXCEPTION_TYPE(ZeroDivisionError, ArithmeticError, PyExc_ZeroDivisionError)
BUILTIN_EXCEPTION_TYPE(AssertionError, Exception, PyExc_AssertionError)
BUILTIN_EXCEPTION_TYPE(AttributeError, Exception, PyExc_AttributeError)
BUILTIN_EXCEPTION_TYPE(BufferError, Exception, PyExc_BufferError)
BUILTIN_EXCEPTION_TYPE(EOFError, Exception, PyExc_EOFError)
BUILTIN_EXCEPTION_TYPE(ImportError, Exception, PyExc_ImportError)
    BUILTIN_EXCEPTION_TYPE(ModuleNotFoundError, ImportError, PyExc_ModuleNotFoundError)
BUILTIN_EXCEPTION_TYPE(LookupError, Exception, PyExc_LookupError)
    BUILTIN_EXCEPTION_TYPE(IndexError, LookupError, PyExc_IndexError)
    BUILTIN_EXCEPTION_TYPE(KeyError, LookupError, PyExc_KeyError)
BUILTIN_EXCEPTION_TYPE(MemoryError, Exception, PyExc_MemoryError)
BUILTIN_EXCEPTION_TYPE(NameError, Exception, PyExc_NameError)
    BUILTIN_EXCEPTION_TYPE(UnboundLocalError, NameError, PyExc_UnboundLocalError)
BUILTIN_EXCEPTION_TYPE(OSError, Exception, PyExc_OSError)
    BUILTIN_EXCEPTION_TYPE(BlockingIOError, OSError, PyExc_BlockingIOError)
    BUILTIN_EXCEPTION_TYPE(ChildProcessError, OSError, PyExc_ChildProcessError)
    BUILTIN_EXCEPTION_TYPE(ConnectionError, OSError, PyExc_ConnectionError)
        BUILTIN_EXCEPTION_TYPE(BrokenPipeError, ConnectionError, PyExc_BrokenPipeError)
        BUILTIN_EXCEPTION_TYPE(ConnectionAbortedError, ConnectionError, PyExc_ConnectionAbortedError)
        BUILTIN_EXCEPTION_TYPE(ConnectionRefusedError, ConnectionError, PyExc_ConnectionRefusedError)
        BUILTIN_EXCEPTION_TYPE(ConnectionResetError, ConnectionError, PyExc_ConnectionResetError)
    BUILTIN_EXCEPTION_TYPE(FileExistsError, OSError, PyExc_FileExistsError)
    BUILTIN_EXCEPTION_TYPE(FileNotFoundError, OSError, PyExc_FileNotFoundError)
    BUILTIN_EXCEPTION_TYPE(InterruptedError, OSError, PyExc_InterruptedError)
    BUILTIN_EXCEPTION_TYPE(IsADirectoryError, OSError, PyExc_IsADirectoryError)
    BUILTIN_EXCEPTION_TYPE(NotADirectoryError, OSError, PyExc_NotADirectoryError)
    BUILTIN_EXCEPTION_TYPE(PermissionError, OSError, PyExc_PermissionError)
    BUILTIN_EXCEPTION_TYPE(ProcessLookupError, OSError, PyExc_ProcessLookupError)
    BUILTIN_EXCEPTION_TYPE(TimeoutError, OSError, PyExc_TimeoutError)
BUILTIN_EXCEPTION_TYPE(ReferenceError, Exception, PyExc_ReferenceError)
BUILTIN_EXCEPTION_TYPE(RuntimeError, Exception, PyExc_RuntimeError)
    BUILTIN_EXCEPTION_TYPE(NotImplementedError, RuntimeError, PyExc_NotImplementedError)
    BUILTIN_EXCEPTION_TYPE(RecursionError, RuntimeError, PyExc_RecursionError)
BUILTIN_EXCEPTION_TYPE(StopAsyncIteration, Exception, PyExc_StopAsyncIteration)
BUILTIN_EXCEPTION_TYPE(StopIteration, Exception, PyExc_StopIteration)
BUILTIN_EXCEPTION_TYPE(SyntaxError, Exception, PyExc_SyntaxError)
    BUILTIN_EXCEPTION_TYPE(IndentationError, SyntaxError, PyExc_IndentationError)
        BUILTIN_EXCEPTION_TYPE(TabError, IndentationError, PyExc_TabError)
BUILTIN_EXCEPTION_TYPE(SystemError, Exception, PyExc_SystemError)
BUILTIN_EXCEPTION_TYPE(TypeError, Exception, PyExc_TypeError)
BUILTIN_EXCEPTION_TYPE(ValueError, Exception, PyExc_ValueError)
    BUILTIN_EXCEPTION_TYPE(UnicodeError, ValueError, PyExc_UnicodeError)
        // BUILTIN_EXCEPTION_TYPE(UnicodeDecodeError, PyExc_UnicodeDecodeError)
        // BUILTIN_EXCEPTION_TYPE(UnicodeEncodeError, PyExc_UnicodeEncodeError)
        // BUILTIN_EXCEPTION_TYPE(UnicodeTranslateError, PyExc_UnicodeTranslateError)


#undef BUILTIN_EXCEPTION_TYPE


template <>
struct Type<UnicodeDecodeError>;


template <>
struct Interface<Type<UnicodeDecodeError>> : Interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string encoding(const UnicodeDecodeError& self);
    [[nodiscard]] static std::string object(const UnicodeDecodeError& self);
    [[nodiscard]] static Py_ssize_t start(const UnicodeDecodeError& self);
    [[nodiscard]] static Py_ssize_t end(const UnicodeDecodeError& self);
    [[nodiscard]] static std::string reason(const UnicodeDecodeError& self);
};


template <>
struct Type<UnicodeDecodeError> : Object, Interface<Type<UnicodeDecodeError>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(PyExc_UnicodeDecodeError);
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<UnicodeDecodeError>>::encoding(
    const UnicodeDecodeError& self
) {
    return self.encoding;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeDecodeError>>::object(
    const UnicodeDecodeError& self
) {
    return self.object;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeDecodeError>>::start(
    const UnicodeDecodeError& self
) {
    return self.start;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeDecodeError>>::end(
    const UnicodeDecodeError& self
) {
    return self.end;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeDecodeError>>::reason(
    const UnicodeDecodeError& self
) {
    return self.reason;
}


template <>
struct Type<UnicodeEncodeError>;


template <>
struct Interface<Type<UnicodeEncodeError>> : Interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string encoding(const UnicodeEncodeError& self);
    [[nodiscard]] static std::string object(const UnicodeEncodeError& self);
    [[nodiscard]] static Py_ssize_t start(const UnicodeEncodeError& self);
    [[nodiscard]] static Py_ssize_t end(const UnicodeEncodeError& self);
    [[nodiscard]] static std::string reason(const UnicodeEncodeError& self);
};


template <>
struct Type<UnicodeEncodeError> : Object, Interface<Type<UnicodeEncodeError>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(PyExc_UnicodeEncodeError);
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<UnicodeEncodeError>>::encoding(
    const UnicodeEncodeError& self
) {
    return self.encoding;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeEncodeError>>::object(
    const UnicodeEncodeError& self
) {
    return self.object;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeEncodeError>>::start(
    const UnicodeEncodeError& self
) {
    return self.start;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeEncodeError>>::end(
    const UnicodeEncodeError& self
) {
    return self.end;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeEncodeError>>::reason(
    const UnicodeEncodeError& self
) {
    return self.reason;
}


template <>
struct Type<UnicodeTranslateError>;


template <>
struct Interface<Type<UnicodeTranslateError>> : Interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string object(const UnicodeTranslateError& self);
    [[nodiscard]] static Py_ssize_t start(const UnicodeTranslateError& self);
    [[nodiscard]] static Py_ssize_t end(const UnicodeTranslateError& self);
    [[nodiscard]] static std::string reason(const UnicodeTranslateError& self);
};


template <>
struct Type<UnicodeTranslateError> : Object, Interface<Type<UnicodeTranslateError>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(PyExc_UnicodeTranslateError);
        }
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<UnicodeTranslateError>>::object(
    const UnicodeTranslateError& self
) {
    return self.object;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeTranslateError>>::start(
    const UnicodeTranslateError& self
) {
    return self.start;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeTranslateError>>::end(
    const UnicodeTranslateError& self
) {
    return self.end;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeTranslateError>>::reason(
    const UnicodeTranslateError& self
) {
    return self.reason;
}


}  // namespace py


#endif
