#ifndef BERTRAND_PYTHON_CORE_TYPE_H
#define BERTRAND_PYTHON_CORE_TYPE_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"
#include "pytypedefs.h"


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

        // TODO: wrapping a new exception type with a custom type would involve
        // setting the wrapper type to void.  That would give you a new exception
        // type that you could then use from Python.  That's how all non-builtin
        // exception types would be implemented.

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


/* Forward the BertrandMeta interface if the given type originates from C++. */
template <impl::originates_from_cpp T>
struct __len__<Type<T>> : Returns<size_t> {};
template <impl::originates_from_cpp T>
struct __iter__<Type<T>> : Returns<BertrandMeta> {};
template <impl::originates_from_cpp T, typename U>
struct __getitem__<Type<T>, Type<U>> : Returns<BertrandMeta> {};
template <impl::originates_from_cpp T>
struct __getitem__<Type<T>, Tuple<Type<Object>>> : Returns<BertrandMeta> {};
// template <impl::originates_from_cpp T, typename... Ts>
// struct __getitem__<Type<T>, Struct<Type<Ts...>>> : Returns<BertrandMeta> {};  // TODO: implement Struct


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


/* Bertrand's metatype, which is used to expose C++ classes to Python. */
template <>
struct Type<BertrandMeta> : Object, Interface<Type<BertrandMeta>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, BertrandMeta> {
        static constexpr StaticStr __doc__ =
R"doc(A shared metaclass for all Bertrand extension types.

Notes
-----
This metaclass identifies all types that originate from C++.  It is used to
automatically demangle the type name and implement certain common behaviors that are
shared across all types, such as template subscripting via `__getitem__` and accurate
`isinstance()` checks based on Bertrand's C++ control structures.)doc";

        /* C++ needs a few extra hooks to accurately model C++ types in a way that
        shares state.  The getters and setters decribed here are implemented according
        to Python's `tp_getset` slot, and describe class-level computed properties. */
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

        /* The `instancecheck` and `subclasscheck` function pointers are used to back
        the metaclass's Python-level `__instancecheck__()` and `__subclasscheck()__`
        methods, and are automatically set during construction for each type. */
        PyTypeObject base;
        bool(*instancecheck)(__python__*, PyObject*);
        bool(*subclasscheck)(__python__*, PyObject*);
        Getters getters;
        Setters setters;
        PyObject* demangled;
        PyObject* template_instantiations;

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
            cls->instancecheck = template_instancecheck;
            cls->subclasscheck = template_subclasscheck;
            cls->demangled = nullptr;  // prevent dangling pointers
            cls->template_instantiations = nullptr;
            new (&cls->getters) Getters();
            new (&cls->setters) Setters();
            cls->demangled = cls->get_demangled_name();
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

        static void __dealloc__(__python__* cls) {
            PyObject_GC_UnTrack(cls);
            cls->getters.~Getters();
            cls->setters.~Setters();
            Py_XDECREF(cls->demangled);
            Py_XDECREF(cls->template_instantiations);
            PyTypeObject* type = Py_TYPE(cls);
            type->tp_free(cls);
            Py_DECREF(type);  // required for heap types
        }

        static int __traverse__(__python__* cls, visitproc visit, void* arg) {
            Py_VISIT(cls->demangled);
            Py_VISIT(cls->template_instantiations);
            Py_VISIT(Py_TYPE(cls));  // required for heap types
            return 0;
        }

        /* Helper to ensure the demangled name is always consistent. */
        PyObject* get_demangled_name() {
            std::string s = "<class '" + impl::demangle(base.tp_name) + "'>";
            return PyUnicode_FromStringAndSize(s.c_str(), s.size());
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

        inline static PyGetSetDef getset[] = {
            {
                .name = "__doc__",
                .get = (getter) doc,
                .set = nullptr,
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
struct __len__<BertrandMeta> : Returns<size_t> {};
template <>
struct __iter__<BertrandMeta> : Returns<BertrandMeta> {};
template <typename T>
struct __getitem__<BertrandMeta, Type<T>> : Returns<BertrandMeta> {};
template <>
struct __getitem__<BertrandMeta, Tuple<Type<Object>>> : Returns<BertrandMeta> {};
// template <typename... Ts>
// struct __getitem__<BertrandMeta, Struct<Type<Ts>...>> : Returns<BertrandMeta> {};  // TODO: implement Struct


namespace impl {

    template <typename CRTP, typename Wrapper, typename CppType>
    template <StaticStr ModName>
    struct TypeTag::BaseDef<CRTP, Wrapper, CppType>::Bindings {
    private:
        using Meta = Type<BertrandMeta>::__python__;
        inline static std::vector<PyMethodDef> tp_methods;
        inline static std::vector<PyGetSetDef> tp_getset;
        inline static std::vector<PyMemberDef> tp_members;

        static int setattro(CRTP* self, PyObject* attr, PyObject* value) {
            if (value == nullptr) {
                if constexpr (dunder::has_delattr<CRTP>) {
                    return CRTP::__delattr__(self, attr);
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
                if constexpr (dunder::has_setattr<CRTP>) {
                    return CRTP::__setattr__(self, attr, value);
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

        static int setitem(CRTP* self, PyObject* key, PyObject* value) {
            if (value == nullptr) {
                if constexpr (dunder::has_delitem<CRTP>) {
                    return CRTP::__delitem__(self, key);
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
                if constexpr (dunder::has_setitem<CRTP>) {
                    return CRTP::__setitem__(self, key, value);
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
            meta->instancecheck = instancecheck;
            meta->subclasscheck = subclasscheck;
            new (&meta->getters) Meta::Getters(std::move(class_getters));
            new (&meta->setters) Meta::Setters(std::move(class_setters));
            meta->template_instantiations = nullptr;
            meta->demangled = meta->get_demangled_name();
            if (meta->demangled == nullptr) {
                Exception::from_python();
            }

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
    struct TypeTag::DetectHashable<CRTP, Wrapper, CppType> :
        BaseDef<CRTP, Wrapper, CppType>
    {
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
    struct TypeTag::DetectForwardIterable<CRTP, Wrapper, CppType> :
        DetectHashable<CRTP, Wrapper, CppType>
    {
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
    struct TypeTag::DetectReverseIterable<CRTP, Wrapper, CppType> :
        DetectForwardIterable<CRTP, Wrapper, CppType>
    {
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

    template <typename CRTP, typename Wrapper, has_call_operator CppType>
    struct TypeTag::DetectCallable<CRTP, Wrapper, CppType> :
        DetectReverseIterable<CRTP, Wrapper, CppType>
    {
        // TODO: insert a vectorcall-based __call__ method here, which invokes an
        // overload set.  Perhaps the overload set can be stored internally as a C++
        // field for maximum performance.  Same with __init__/__new__.
    };

    template <typename CRTP, typename Wrapper, typename CppType>
    struct TypeTag::def : DetectCallable<CRTP, Wrapper, CppType> {
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


/// NOTE: all types must define __as_object__ as they are generated, since it can't be
/// deduced any other way.


template <impl::originates_from_cpp Self, typename T>
    requires (std::convertible_to<T, typename Type<Self>::__python__::t_cpp>)
struct __init__<Self, T> : Returns<Self> {
    static Self operator()(T&& value) {
        return Type<Self>::__python__::__create__(std::forward<T>(value));
    }
};


template <impl::originates_from_cpp Self, typename... Args>
    requires (std::constructible_from<typename Type<Self>::__python__::t_cpp, Args...>)
struct __explicit_init__<Self, Args...> : Returns<Self> {
    static Self operator()(Args&&... args) {
        return Type<Self>::__python__::__create__(std::forward<Args>(args)...);
    }
};


template <impl::originates_from_cpp Self, typename T>
    requires (std::convertible_to<typename Type<Self>::__python__::t_cpp, T>)
struct __cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return py::unwrap(self);
    }
};


template <impl::originates_from_cpp Self, typename T>
    requires (impl::explicitly_convertible_to<typename Type<Self>::__python__::t_cpp, T>)
struct __explicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return static_cast<T>(py::unwrap(self));
    }
};


// TODO: continue defining control structs for all of the rest of the basic behavior.
// hashability
// iteration
// etc. for all possible iterators.  They can all use the default behavior, since
// that automatically unpacks C++ objects.



///////////////////////////////
////    EXCEPTION TYPES    ////
///////////////////////////////


// TODO: there has to be some handling for a Wrapper type that derives from
// std::exception.  In essence, that's the same as a C++ std::exception subclass as the
// CppType, but a little more finnicky.  It probably has to be a separate overload of
// the def helpers, which is a drag.


#define BUILTIN_EXCEPTION_TYPE(CLS, PYTYPE)                                             \
    template <>                                                                         \
    struct Type<CLS> : Object, impl::TypeTag {                                          \
        struct __python__ : TypeTag::def<__python__, CLS> {                             \
            static Type __import__() {                                                  \
                return reinterpret_borrow<Type>(PYTYPE);                                \
            }                                                                           \
        };                                                                              \
                                                                                        \
        Type(Handle h, borrowed_t t) : Object(h, t) {}                                  \
        Type(Handle h, stolen_t t) : Object(h, t) {}                                    \
                                                                                        \
        template <typename... Args> requires (implicit_ctor<Type>::template enable<Args...>) \
        Type(Args&&... args) : Object(                                                  \
            implicit_ctor<Type>{},                                                      \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
                                                                                        \
        template <typename... Args> requires (explicit_ctor<Type>::template enable<Args...>) \
        explicit Type(Args&&... args) : Object(                                         \
            explicit_ctor<Type>{},                                                      \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
    };


BUILTIN_EXCEPTION_TYPE(Exception, PyExc_Exception)
BUILTIN_EXCEPTION_TYPE(ArithmeticError, PyExc_ArithmeticError)
    BUILTIN_EXCEPTION_TYPE(FloatingPointError, PyExc_FloatingPointError)
    BUILTIN_EXCEPTION_TYPE(OverflowError, PyExc_OverflowError)
    BUILTIN_EXCEPTION_TYPE(ZeroDivisionError, PyExc_ZeroDivisionError)
BUILTIN_EXCEPTION_TYPE(AssertionError, PyExc_AssertionError)
BUILTIN_EXCEPTION_TYPE(AttributeError, PyExc_AttributeError)
BUILTIN_EXCEPTION_TYPE(BufferError, PyExc_BufferError)
BUILTIN_EXCEPTION_TYPE(EOFError, PyExc_EOFError)
BUILTIN_EXCEPTION_TYPE(ImportError, PyExc_ImportError)
    BUILTIN_EXCEPTION_TYPE(ModuleNotFoundError, PyExc_ModuleNotFoundError)
BUILTIN_EXCEPTION_TYPE(LookupError, PyExc_LookupError)
    BUILTIN_EXCEPTION_TYPE(IndexError, PyExc_IndexError)
    BUILTIN_EXCEPTION_TYPE(KeyError, PyExc_KeyError)
BUILTIN_EXCEPTION_TYPE(MemoryError, PyExc_MemoryError)
BUILTIN_EXCEPTION_TYPE(NameError, PyExc_NameError)
    BUILTIN_EXCEPTION_TYPE(UnboundLocalError, PyExc_UnboundLocalError)
BUILTIN_EXCEPTION_TYPE(OSError, PyExc_OSError)
    BUILTIN_EXCEPTION_TYPE(BlockingIOError, PyExc_BlockingIOError)
    BUILTIN_EXCEPTION_TYPE(ChildProcessError, PyExc_ChildProcessError)
    BUILTIN_EXCEPTION_TYPE(ConnectionError, PyExc_ConnectionError)
        BUILTIN_EXCEPTION_TYPE(BrokenPipeError, PyExc_BrokenPipeError)
        BUILTIN_EXCEPTION_TYPE(ConnectionAbortedError, PyExc_ConnectionAbortedError)
        BUILTIN_EXCEPTION_TYPE(ConnectionRefusedError, PyExc_ConnectionRefusedError)
        BUILTIN_EXCEPTION_TYPE(ConnectionResetError, PyExc_ConnectionResetError)
    BUILTIN_EXCEPTION_TYPE(FileExistsError, PyExc_FileExistsError)
    BUILTIN_EXCEPTION_TYPE(FileNotFoundError, PyExc_FileNotFoundError)
    BUILTIN_EXCEPTION_TYPE(InterruptedError, PyExc_InterruptedError)
    BUILTIN_EXCEPTION_TYPE(IsADirectoryError, PyExc_IsADirectoryError)
    BUILTIN_EXCEPTION_TYPE(NotADirectoryError, PyExc_NotADirectoryError)
    BUILTIN_EXCEPTION_TYPE(PermissionError, PyExc_PermissionError)
    BUILTIN_EXCEPTION_TYPE(ProcessLookupError, PyExc_ProcessLookupError)
    BUILTIN_EXCEPTION_TYPE(TimeoutError, PyExc_TimeoutError)
BUILTIN_EXCEPTION_TYPE(ReferenceError, PyExc_ReferenceError)
BUILTIN_EXCEPTION_TYPE(RuntimeError, PyExc_RuntimeError)
    BUILTIN_EXCEPTION_TYPE(NotImplementedError, PyExc_NotImplementedError)
    BUILTIN_EXCEPTION_TYPE(RecursionError, PyExc_RecursionError)
BUILTIN_EXCEPTION_TYPE(StopAsyncIteration, PyExc_StopAsyncIteration)
BUILTIN_EXCEPTION_TYPE(StopIteration, PyExc_StopIteration)
BUILTIN_EXCEPTION_TYPE(SyntaxError, PyExc_SyntaxError)
    BUILTIN_EXCEPTION_TYPE(IndentationError, PyExc_IndentationError)
        BUILTIN_EXCEPTION_TYPE(TabError, PyExc_TabError)
BUILTIN_EXCEPTION_TYPE(SystemError, PyExc_SystemError)
BUILTIN_EXCEPTION_TYPE(TypeError, PyExc_TypeError)
BUILTIN_EXCEPTION_TYPE(ValueError, PyExc_ValueError)
    BUILTIN_EXCEPTION_TYPE(UnicodeError, PyExc_UnicodeError)
        BUILTIN_EXCEPTION_TYPE(UnicodeDecodeError, PyExc_UnicodeDecodeError)
        BUILTIN_EXCEPTION_TYPE(UnicodeEncodeError, PyExc_UnicodeEncodeError)
        BUILTIN_EXCEPTION_TYPE(UnicodeTranslateError, PyExc_UnicodeTranslateError)


#undef BUILTIN_EXCEPTION_TYPE


}  // namespace py


#endif
