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

    namespace pytraits {

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

    }

    /* Marks a py::Object subclass as a type object, and exposes several helpers to
    make writing custom types as easy as possible.  Every specialization of `Type<>`
    should inherit from this class and implement a public `__python__` struct that
    inherits from its CRTP helpers. */
    struct TypeTag : public BertrandTag {
    private:

        /* Shared behavior for all Python types, be they wrappers around external C++
        classes or pure Python.  Each type must minimally support these methods.
        Everything else is an implementation detail for the type's Python
        representation. */
        template <typename CRTP, typename Wrapper, typename CppType>
        struct BaseDef : public BertrandTag {
        protected:
            inline static std::vector<PyMethodDef> tp_methods;
            inline static std::vector<PyGetSetDef> tp_getset;
            inline static std::vector<PyMemberDef> tp_members;

            template <StaticStr ModName>
            friend struct Bindings;

            template <StaticStr ModName>
            struct Bindings;

        public:

            /* Generate a new instance of the type object to be attached to the given
            module.

            The `bindings` argument is a helper that includes a number of convenience
            methods for exposing the type to Python.  The __export__ script must end
            with a call to `bindings.finalize<Bases...>()`, which will build a
            corresponding `PyType_Spec` that will be used to instantiate a unique heap
            type for each module.  This is the correct way to expose types with
            per-module state, as it allows for multiple sub-interpreters to run in
            parallel without interfering with one other.  It is automatically executed
            by the import system whenever the enclosing module is loaded into a fresh
            interpreter.

            Note that this method does not need to actually attach the type to the
            module; that is done elsewhere in the import process in order to allow for
            the modeling of templates. */
            template <StaticStr ModName>
            static Type<Wrapper> __export__(Bindings<ModName> bindings) {
                // bindings.var<"foo">(value);
                // bindings.method<"bar">("docstring", &CRTP::bar);
                // ...
                return bindings.template finalize<>();
            }

            /* Return a new reference to the type by importing its parent module and
            unpacking this specific type.  This is called automatically by the default
            constructor for `Type<Wrapper>`, and is the proper way to handle per-module
            state, instead of using static type objects.  It will implicitly invoke the
            `__export__()` method during the import process, either via
            `PyImport_Import()` or the `Module` constructor directly. */
            static Type<Wrapper> __import__() {
                throw NotImplementedError(
                    "the __import__() method must be defined for all Python types: "
                    + demangle(typeid(CRTP).name())
                );
            }

        };

    protected:

        /* A CRTP base class that automatically generates boilerplate bindings for a
        new Python type which wraps around an external C++ type.  This is the preferred
        way of writing new Python types in C++.

        This class is only available to specializations of `py::Type<>`, each of which
        should define a public `__python__` struct that inherits from it.  It uses CRTP
        to automate the creation of Python types and their associated methods, slots,
        and other attributes in a way that conforms to Python's best practices.  This
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
            -   A shared metaclass, which demangles the C++ type name and delegates
                `isinstance()` and `issubclass()` checks to the `py::__isinstance__`
                and `py::__issubclass__` control structs, respectively.
            -   Support for C++ templates, which can be indexed in Python using the
                metaclass's `__getitem__` slot.
            -   Method overloading, wherein each method is represented as an overload
                set attached to the type object via the descriptor protocol.

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
        returns a new reference to the type object.  If the type is implemented inline,
        then they must also implement the `__export__()` hook, which is called to
        initialize the type object and expose it to Python.  Everything else is an
        implementation detail. */
        template <typename CRTP, typename Wrapper>
        struct def<CRTP, Wrapper, void> : public BaseDef<CRTP, Wrapper, void> {
            static constexpr Origin __origin__ = Origin::PYTHON;
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
    static constexpr bool operator()(const T& obj) {
        return isinstance<Cls>(obj);
    }
    static constexpr bool operator()(const T& obj, const Type<Cls>& cls) {
        if constexpr (impl::is_object_exact<T>) {
            int result = PyObject_IsInstance(
                ptr(as_object(obj)),
                ptr(cls)
            );
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            return issubclass<T, Cls>();  // TODO: is this correct?
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
        return issubclass<T, Cls>();
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
        if constexpr (operator()() && impl::type_like<T>) {
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

        PyTypeObject base;
        bool(*instancecheck)(PyObject*);
        bool(*subclasscheck)(PyObject*);
        Getters getters;
        Setters setters;
        PyObject* demangled;
        PyObject* template_instantiations;

        template <StaticStr ModName>
        static Type __export__(Bindings<ModName> parent) {
            static PyType_Slot slots[] = {
                {Py_tp_doc, const_cast<char*>(__doc__.buffer)},
                {Py_tp_dealloc, reinterpret_cast<void*>(__dealloc__)},
                {Py_tp_repr, reinterpret_cast<void*>(__repr__)},
                {Py_tp_str, reinterpret_cast<void*>(__repr__)},
                {Py_tp_getattro, reinterpret_cast<void*>(__getattr__)},
                {Py_tp_setattro, reinterpret_cast<void*>(__setattr__)},
                {Py_tp_iter, reinterpret_cast<void*>(__iter__)},
                {Py_mp_length, reinterpret_cast<void*>(__len__)},
                {Py_mp_subscript, reinterpret_cast<void*>(__getitem__)},
                {Py_tp_methods, methods},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = ModName + ".Meta",
                .basicsize = sizeof(BertrandMeta),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION |
                    Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_TYPE_SUBCLASS |
                    Py_TPFLAGS_MANAGED_WEAKREF | Py_TPFLAGS_MANAGED_DICT,
                .slots = slots
            };
            PyObject* cls = PyType_FromModuleAndSpec(
                ptr(parent),
                &spec,
                &PyType_Type
            );
            if (cls == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Type>(cls);
        }

        /* Get a new reference to the metatype from the root module. */
        static Type __import__();  // TODO: defined in __init__.h alongside "bertrand.python" module {
        //     return impl::get_type<BertrandMeta>(Module<"bertrand">());
        // }

        /* Free the type name and any template instantiations when an instance of this
        type falls out of scope. */
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

        /* Track instances of this type with Python's cyclic garbage collector. */
        static int __traverse__(__python__* cls, visitproc visit, void* arg) {
            Py_VISIT(cls->demangled);
            Py_VISIT(cls->template_instantiations);
            Py_VISIT(Py_TYPE(cls));  // required for heap types
            return 0;
        }

        /* `repr(cls)` demangles the C++ type name.  */
        static PyObject* __repr__(__python__* cls) {
            return Py_XNewRef(cls->demangled);
        }

        /* `len(cls)` yields the number of template instantiations it is tracking. */
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

        /* `cls.` allows class-level variables and properties with shared state. */
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

        /* `cls. = ...` allows assignment to class-level variables and properties with
        shared state. */
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
        struct and accounts for template instantiations. */
        static PyObject* __instancecheck__(__python__* cls, PyObject* instance) {
            try {
                if (cls->instancecheck(instance)) {
                    Py_RETURN_TRUE;
                } else if (cls->template_instantiations) {
                    PyObject* key;
                    PyObject* value;
                    Py_ssize_t pos = 0;
                    while (PyDict_Next(
                        cls->template_instantiations,
                        &pos,
                        &key,
                        &value
                    )) {
                        if (reinterpret_cast<__python__*>(value)->instancecheck(instance)) {
                            Py_RETURN_TRUE;
                        }
                    }
                }
                Py_RETURN_FALSE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* issubclass(sub, cls) forwards the behavior of the __issubclass__ control
        struct and accounts for template instantiations. */
        static PyObject* __subclasscheck__(__python__* cls, PyObject* subclass) {
            try {
                if (cls->subclasscheck(subclass)) {
                    Py_RETURN_TRUE;
                } else if (cls->template_instantiations) {
                    PyObject* key;
                    PyObject* value;
                    Py_ssize_t pos = 0;
                    while (PyDict_Next(
                        cls->template_instantiations,
                        &pos,
                        &key,
                        &value
                    )) {
                        if (reinterpret_cast<__python__*>(value)->subclasscheck(subclass)) {
                            Py_RETURN_TRUE;
                        }
                    }
                }
                Py_RETURN_FALSE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
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

        /* Helper to ensure the demangled name is always consistent. */
        PyObject* get_demangled_name() {
            std::string s = "<class '" + impl::demangle(base.tp_name) + "'>";
            return PyUnicode_FromStringAndSize(s.c_str(), s.size());
        }

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
// struct __issubclass__<T, BertrandMeta>                      : Returns<bool> {};  // TODO: implement default behavior
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
    template <StaticStr ModName>
    struct TypeTag::BaseDef<CRTP, Wrapper, CppType>::Bindings {
    private:

        static int setattro(CRTP* self, PyObject* attr, PyObject* value) {
            if (value == nullptr) {
                if constexpr (pytraits::has_delattr<CRTP>) {
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
                if constexpr (pytraits::has_setattr<CRTP>) {
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
                if constexpr (pytraits::has_delitem<CRTP>) {
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
                if constexpr (pytraits::has_setitem<CRTP>) {
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
                    if constexpr (pytraits::has_lt<CRTP>) {
                        return CRTP::__lt__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_LE:
                    if constexpr (pytraits::has_le<CRTP>) {
                        return CRTP::__le__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_EQ:
                    if constexpr (pytraits::has_eq<CRTP>) {
                        return CRTP::__eq__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_NE:
                    if constexpr (pytraits::has_ne<CRTP>) {
                        return CRTP::__ne__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_GE:
                    if constexpr (pytraits::has_ge<CRTP>) {
                        return CRTP::__ge__(self, other);
                    } else {
                        Py_RETURN_NOTIMPLEMENTED;
                    }
                case Py_GT:
                    if constexpr (pytraits::has_gt<CRTP>) {
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
                if constexpr (pytraits::has_delete<CRTP>) {
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
                if constexpr (pytraits::has_set<CRTP>) {
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

    public:
        Module<ModName> module;

        Bindings(const Module<ModName>& mod) : module(mod) {}
        Bindings(const Bindings&) = delete;
        Bindings(Bindings&&) = delete;

        template <std::derived_from<Object>... Bases>
        Type<Wrapper> finalize() {
            static std::vector<PyType_Slot> slots;
            static bool initialized = false;
            if (!initialized) {
                if constexpr (pytraits::has_dealloc<CRTP>) {
                    slots.push_back({
                        Py_tp_dealloc,
                        reinterpret_cast<void*>(CRTP::__dealloc__)
                    });
                }
                if constexpr (pytraits::has_vectorcall<CRTP>) {
                    tp_members.push_back({
                        "__vectorcalloffset__",
                        Py_T_PYSSIZET,
                        offsetof(CRTP, __vectorcall__),
                        Py_READONLY,
                    });
                }
                if constexpr (pytraits::has_await<CRTP>) {
                    slots.push_back({
                        Py_am_await,
                        reinterpret_cast<void*>(CRTP::__await__)
                    });
                }
                if constexpr (pytraits::has_aiter<CRTP>) {
                    slots.push_back({
                        Py_am_aiter,
                        reinterpret_cast<void*>(CRTP::__aiter__)
                    });
                }
                if constexpr (pytraits::has_anext<CRTP>) {
                    slots.push_back({
                        Py_am_anext,
                        reinterpret_cast<void*>(CRTP::__anext__)
                    });
                }
                if constexpr (pytraits::has_asend<CRTP>) {
                    slots.push_back({
                        Py_am_send,
                        reinterpret_cast<void*>(CRTP::__asend__)
                    });
                }
                if constexpr (pytraits::has_repr<CRTP>) {
                    slots.push_back({
                        Py_tp_repr,
                        reinterpret_cast<void*>(CRTP::__repr__)
                    });
                }
                if constexpr (pytraits::has_add<CRTP>) {
                    slots.push_back({
                        Py_nb_add,
                        reinterpret_cast<void*>(CRTP::__add__)
                    });
                }
                if constexpr (pytraits::has_subtract<CRTP>) {
                    slots.push_back({
                        Py_nb_subtract,
                        reinterpret_cast<void*>(CRTP::__sub__)
                    });
                }
                if constexpr (pytraits::has_multiply<CRTP>) {
                    slots.push_back({
                        Py_nb_multiply,
                        reinterpret_cast<void*>(CRTP::__mul__)
                    });
                }
                if constexpr (pytraits::has_remainder<CRTP>) {
                    slots.push_back({
                        Py_nb_remainder,
                        reinterpret_cast<void*>(CRTP::__mod__)
                    });
                }
                if constexpr (pytraits::has_divmod<CRTP>) {
                    slots.push_back({
                        Py_nb_divmod,
                        reinterpret_cast<void*>(CRTP::__divmod__)
                    });
                }
                if constexpr (pytraits::has_power<CRTP>) {
                    slots.push_back({
                        Py_nb_power,
                        reinterpret_cast<void*>(CRTP::__pow__)
                    });
                }
                if constexpr (pytraits::has_negative<CRTP>) {
                    slots.push_back({
                        Py_nb_negative,
                        reinterpret_cast<void*>(CRTP::__neg__)
                    });
                }
                if constexpr (pytraits::has_positive<CRTP>) {
                    slots.push_back({
                        Py_nb_positive,
                        reinterpret_cast<void*>(CRTP::__pos__)
                    });
                }
                if constexpr (pytraits::has_absolute<CRTP>) {
                    slots.push_back({
                        Py_nb_absolute,
                        reinterpret_cast<void*>(CRTP::__abs__)
                    });
                }
                if constexpr (pytraits::has_bool<CRTP>) {
                    slots.push_back({
                        Py_nb_bool,
                        reinterpret_cast<void*>(CRTP::__bool__)
                    });
                }
                if constexpr (pytraits::has_invert<CRTP>) {
                    slots.push_back({
                        Py_nb_invert,
                        reinterpret_cast<void*>(CRTP::__invert__)
                    });
                }
                if constexpr (pytraits::has_lshift<CRTP>) {
                    slots.push_back({
                        Py_nb_lshift,
                        reinterpret_cast<void*>(CRTP::__lshift__)
                    });
                }
                if constexpr (pytraits::has_rshift<CRTP>) {
                    slots.push_back({
                        Py_nb_rshift,
                        reinterpret_cast<void*>(CRTP::__rshift__)
                    });
                }
                if constexpr (pytraits::has_and<CRTP>) {
                    slots.push_back({
                        Py_nb_and,
                        reinterpret_cast<void*>(CRTP::__and__)
                    });
                }
                if constexpr (pytraits::has_xor<CRTP>) {
                    slots.push_back({
                        Py_nb_xor,
                        reinterpret_cast<void*>(CRTP::__xor__)
                    });
                }
                if constexpr (pytraits::has_or<CRTP>) {
                    slots.push_back({
                        Py_nb_or,
                        reinterpret_cast<void*>(CRTP::__or__)
                    });
                }
                if constexpr (pytraits::has_int<CRTP>) {
                    slots.push_back({
                        Py_nb_int,
                        reinterpret_cast<void*>(CRTP::__int__)
                    });
                }
                if constexpr (pytraits::has_float<CRTP>) {
                    slots.push_back({
                        Py_nb_float,
                        reinterpret_cast<void*>(CRTP::__float__)
                    });
                }
                if constexpr (pytraits::has_inplace_add<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_add,
                        reinterpret_cast<void*>(CRTP::__iadd__)
                    });
                }
                if constexpr (pytraits::has_inplace_subtract<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_subtract,
                        reinterpret_cast<void*>(CRTP::__isub__)
                    });
                }
                if constexpr (pytraits::has_inplace_multiply<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_multiply,
                        reinterpret_cast<void*>(CRTP::__imul__)
                    });
                }
                if constexpr (pytraits::has_inplace_remainder<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_remainder,
                        reinterpret_cast<void*>(CRTP::__imod__)
                    });
                }
                if constexpr (pytraits::has_inplace_power<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_power,
                        reinterpret_cast<void*>(CRTP::__ipow__)
                    });
                }
                if constexpr (pytraits::has_inplace_lshift<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_lshift,
                        reinterpret_cast<void*>(CRTP::__ilshift__)
                    });
                }
                if constexpr (pytraits::has_inplace_rshift<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_rshift,
                        reinterpret_cast<void*>(CRTP::__irshift__)
                    });
                }
                if constexpr (pytraits::has_inplace_and<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_and,
                        reinterpret_cast<void*>(CRTP::__iand__)
                    });
                }
                if constexpr (pytraits::has_inplace_xor<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_xor,
                        reinterpret_cast<void*>(CRTP::__ixor__)
                    });
                }
                if constexpr (pytraits::has_inplace_or<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_or,
                        reinterpret_cast<void*>(CRTP::__ior__)
                    });
                }
                if constexpr (pytraits::has_floor_divide<CRTP>) {
                    slots.push_back({
                        Py_nb_floor_divide,
                        reinterpret_cast<void*>(CRTP::__floordiv__)
                    });
                }
                if constexpr (pytraits::has_true_divide<CRTP>) {
                    slots.push_back({
                        Py_nb_true_divide,
                        reinterpret_cast<void*>(CRTP::__truediv__)
                    });
                }
                if constexpr (pytraits::has_inplace_floor_divide<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_floor_divide,
                        reinterpret_cast<void*>(CRTP::__ifloordiv__)
                    });
                }
                if constexpr (pytraits::has_inplace_true_divide<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_true_divide,
                        reinterpret_cast<void*>(CRTP::__itruediv__)
                    });
                }
                if constexpr (pytraits::has_index<CRTP>) {
                    slots.push_back({
                        Py_nb_index,
                        reinterpret_cast<void*>(CRTP::__index__)
                    });
                }
                if constexpr (pytraits::has_matrix_multiply<CRTP>) {
                    slots.push_back({
                        Py_nb_matrix_multiply,
                        reinterpret_cast<void*>(CRTP::__matmul__)
                    });
                }
                if constexpr (pytraits::has_inplace_matrix_multiply<CRTP>) {
                    slots.push_back({
                        Py_nb_inplace_matrix_multiply,
                        reinterpret_cast<void*>(CRTP::__imatmul__)
                    });
                }
                if constexpr (pytraits::has_contains<CRTP>) {
                    slots.push_back({
                        Py_sq_contains,
                        reinterpret_cast<void*>(CRTP::__contains__)
                    });
                }
                if constexpr (impl::sequence_like<Wrapper>) {
                    if constexpr (pytraits::has_len<CRTP>) {
                        slots.push_back({
                            Py_sq_length,
                            reinterpret_cast<void*>(CRTP::__len__)
                        });
                    }
                    if constexpr (pytraits::has_getitem<CRTP>) {
                        slots.push_back({
                            Py_sq_item,
                            reinterpret_cast<void*>(CRTP::__getitem__)
                        });
                    }
                    if constexpr (pytraits::has_setitem<CRTP> || pytraits::has_delitem<CRTP>) {
                        slots.push_back({
                            Py_sq_ass_item,
                            reinterpret_cast<void*>(setitem)
                        });
                    }
                    if constexpr (pytraits::has_add<CRTP> && has_concat<Wrapper>) {
                        slots.push_back({
                            Py_sq_concat,
                            reinterpret_cast<void*>(CRTP::__add__)
                        });
                    }
                    if constexpr (
                        pytraits::has_inplace_add<CRTP> && has_inplace_concat<Wrapper>
                    ) {
                        slots.push_back({
                            Py_sq_inplace_concat,
                            reinterpret_cast<void*>(CRTP::__iadd__)
                        });
                    }
                    if constexpr (pytraits::has_multiply<CRTP> && has_repeat<Wrapper>) {
                        slots.push_back({
                            Py_sq_repeat,
                            reinterpret_cast<void*>(CRTP::__mul__)
                        });
                    }
                    if constexpr (
                        pytraits::has_inplace_multiply<CRTP> && has_inplace_repeat<Wrapper>
                    ) {
                        slots.push_back({
                            Py_sq_inplace_repeat,
                            reinterpret_cast<void*>(CRTP::__imul__)
                        });
                    }
                } else if constexpr (impl::mapping_like<Wrapper>) {
                    if constexpr (pytraits::has_len<CRTP>) {
                        slots.push_back({
                            Py_mp_length,
                            reinterpret_cast<void*>(CRTP::__len__)
                        });
                    }
                    if constexpr (pytraits::has_getitem<CRTP>) {
                        slots.push_back({
                            Py_mp_subscript,
                            reinterpret_cast<void*>(CRTP::__getitem__)
                        });
                    }
                    if constexpr (pytraits::has_setitem<CRTP> || pytraits::has_delitem<CRTP>) {
                        slots.push_back({
                            Py_mp_ass_subscript,
                            reinterpret_cast<void*>(setitem)
                        });
                    }
                }
                if constexpr (pytraits::has_hash<CRTP>) {
                    slots.push_back({
                        Py_tp_hash,
                        reinterpret_cast<void*>(CRTP::__hash__)
                    });
                }
                if constexpr (pytraits::has_call<CRTP>) {
                    slots.push_back({
                        Py_tp_call,
                        reinterpret_cast<void*>(CRTP::__call__)
                    });
                }
                if constexpr (pytraits::has_str<CRTP>) {
                    slots.push_back({
                        Py_tp_str,
                        reinterpret_cast<void*>(CRTP::__str__)
                    });
                }
                if constexpr (pytraits::has_getattr<CRTP>) {
                    slots.push_back({
                        Py_tp_getattro,
                        reinterpret_cast<void*>(CRTP::__getattr__)
                    });
                }
                if constexpr (pytraits::has_setattr<CRTP> || pytraits::has_delattr<CRTP>) {
                    slots.push_back({
                        Py_tp_setattro,
                        reinterpret_cast<void*>(setattro)
                    });
                }
                if constexpr (pytraits::has_buffer<CRTP>) {
                    slots.push_back({
                        Py_bf_getbuffer,
                        reinterpret_cast<void*>(CRTP::__buffer__)
                    });
                }
                if constexpr (pytraits::has_release_buffer<CRTP>) {
                    slots.push_back({
                        Py_bf_releasebuffer,
                        reinterpret_cast<void*>(CRTP::__release_buffer__)
                    });
                }
                if constexpr (pytraits::has_doc<CRTP>) {
                    slots.push_back({
                        Py_tp_doc,
                        reinterpret_cast<void*>(CRTP::__doc__.buffer)
                    });
                }
                if constexpr (pytraits::has_traverse<CRTP>) {
                    slots.push_back({
                        Py_tp_traverse,
                        reinterpret_cast<void*>(CRTP::__traverse__)
                    });
                }
                if constexpr (pytraits::has_clear<CRTP>) {
                    slots.push_back({
                        Py_tp_clear,
                        reinterpret_cast<void*>(CRTP::__clear__)
                    });
                }
                if constexpr (
                    pytraits::has_lt<CRTP> || pytraits::has_le<CRTP> ||
                    pytraits::has_eq<CRTP> || pytraits::has_ne<CRTP> ||
                    pytraits::has_gt<CRTP> || pytraits::has_ge<CRTP>
                ) {
                    slots.push_back({
                        Py_tp_richcompare,
                        reinterpret_cast<void*>(richcompare)
                    });
                }
                if constexpr (pytraits::has_iter<CRTP>) {
                    slots.push_back({
                        Py_tp_iter,
                        reinterpret_cast<void*>(CRTP::__iter__)
                    });
                }
                if constexpr (pytraits::has_next<CRTP>) {
                    slots.push_back({
                        Py_tp_iternext,
                        reinterpret_cast<void*>(CRTP::__next__)
                    });
                }
                if constexpr (pytraits::has_get<CRTP>) {
                    slots.push_back({
                        Py_tp_descr_get,
                        reinterpret_cast<void*>(CRTP::__get__)
                    });
                }
                if constexpr (pytraits::has_set<CRTP> || pytraits::has_delete<CRTP>) {
                    slots.push_back({
                        Py_tp_descr_set,
                        reinterpret_cast<void*>(descr_set)
                    });
                }
                if constexpr (pytraits::has_init<CRTP>) {
                    slots.push_back({
                        Py_tp_init,
                        reinterpret_cast<void*>(CRTP::__init__)
                    });
                }
                if constexpr (pytraits::has_new<CRTP>) {
                    slots.push_back({
                        Py_tp_new,
                        reinterpret_cast<void*>(CRTP::__new__)
                    });
                }
                if constexpr (pytraits::has_instancecheck<CRTP>) {
                    tp_methods.push_back({
                        "__instancecheck__",
                        reinterpret_cast<PyCFunction>(CRTP::__instancecheck__),
                        METH_O,
                        nullptr
                    });
                }
                if constexpr (pytraits::has_subclasscheck<CRTP>) {
                    tp_methods.push_back({
                        "__subclasscheck__",
                        reinterpret_cast<PyCFunction>(CRTP::__subclasscheck__),
                        METH_O,
                        nullptr
                    });
                }




                // TODO: check these and ensure the argument signatures are correct
                if constexpr (pytraits::has_enter<CRTP>) {
                    tp_methods.push_back({
                        "__enter__",
                        reinterpret_cast<PyCFunction>(CRTP::__enter__),
                        METH_NOARGS,
                        nullptr
                    });
                }
                if constexpr (pytraits::has_exit<CRTP>) {
                    tp_methods.push_back({
                        "__exit__",
                        reinterpret_cast<PyCFunction>(CRTP::__exit__),
                        METH_VARARGS,
                        nullptr
                    });
                }
                if constexpr (pytraits::has_aenter<CRTP>) {
                    tp_methods.push_back({
                        "__aenter__",
                        reinterpret_cast<PyCFunction>(CRTP::__aenter__),
                        METH_NOARGS,
                        nullptr
                    });
                }
                if constexpr (pytraits::has_aexit<CRTP>) {
                    tp_methods.push_back({
                        "__aexit__",
                        reinterpret_cast<PyCFunction>(CRTP::__aexit__),
                        METH_VARARGS,
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
                .flags = CRTP::tp_flags,
                .slots = slots.data()
            };
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

            // TODO: initialize BertrandMeta fields

            return cls;
        }

    };

    template <typename CRTP, typename Wrapper, typename CppType>
    struct TypeTag::def : public BaseDef<CRTP, Wrapper, CppType> {
    protected:
        using Variant = std::variant<CppType, CppType*, const CppType*>;

        template <typename... Ts>
        struct Visitor : Ts... {
            using Ts::operator()...;
        };

        /* A mutable blueprint for a type, which is configured in the type's
        `__bind__()` method. */
        struct Bindings {
            using PyMeta = typename Type<BertrandMeta>::__python__;
            PyObject* context;
            PyMeta::Getters class_getters;
            PyMeta::Setters class_setters;

            Bindings(const Bindings&) = delete;
            Bindings(Bindings&&) = delete;
            Bindings() : context(PyDict_New()) {
                if (context == nullptr) {
                    Exception::from_python();
                }
            }
            ~Bindings() {
                Py_DECREF(context);
            }

            /* Expose an immutable member variable to Python as a getset descriptor,
            which synchronizes its state. */
            template <StaticStr Name, typename T>
            static void var(const T CppType::*value) {
                static bool skip = false;
                if (skip) {
                    return;
                }
                static auto get = [](PyObject* self, void* closure) -> PyObject* {
                    try {
                        CRTP* obj = reinterpret_cast<CRTP*>(self);
                        auto member_ptr = reinterpret_cast<const T CppType::*>(closure);
                        return release(wrap(std::visit(
                            Visitor{
                                [member_ptr](const CppType& obj) {
                                    return obj.*member_ptr;
                                },
                                [member_ptr](const CppType* obj) {
                                    return obj->*member_ptr;
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
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) + "' is immutable.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                };
                def::tp_getset.push_back({
                    Name,
                    +get,  // converts a stateless lambda to a function pointer
                    +set,
                    nullptr,  // doc
                    const_cast<void*>(reinterpret_cast<const void*>(value))
                });
                skip = true;
            }

            /* Expose a mutable member variable to Python as a getset descriptor, which
            synchronizes its state. */
            template <StaticStr Name, typename T>
            static void var(T CppType::*value) {
                static bool skip = false;
                if (skip) {
                    return;
                }
                static auto get = [](PyObject* self, void* closure) -> PyObject* {
                    try {
                        CRTP* obj = reinterpret_cast<CRTP*>(self);
                        auto member_ptr = reinterpret_cast<T CppType::*>(closure);
                        return release(wrap(std::visit(
                            Visitor{
                                [member_ptr](CppType& obj) {
                                    return obj.*member_ptr;
                                },
                                [member_ptr](CppType* obj) {
                                    return obj->*member_ptr;
                                },
                                [member_ptr](const CppType* obj) {
                                    return obj->*member_ptr;
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
                def::tp_getset.push_back({
                    Name,
                    +get,  // converts a stateless lambda to a function pointer
                    +set,
                    nullptr,  // doc
                    const_cast<void*>(reinterpret_cast<const void*>(value))
                });
                skip = true;
            }

            /* Expose an immutable static variable to Python using the `__getattr__()`
            slot of the metatype, which synchronizes its state. */
            template <StaticStr Name, typename T>
            void classvar(const T& value) {
                static auto get = [](PyObject* cls, void* closure) -> PyObject* {
                    try {
                        return release(wrap(*reinterpret_cast<const T*>(closure)));
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
                class_getters[Name] = {
                    +get,
                    const_cast<void*>(reinterpret_cast<const void*>(&value))
                };
                class_setters[Name] = {
                    +set,
                    const_cast<void*>(reinterpret_cast<const void*>(&value))
                };
            }

            /* Expose a mutable static variable to Python using the `__getattr__()` and
            `__setattr__()` slots of the metatype, which synchronizes its state.  */
            template <StaticStr Name, typename T>
            void classvar(T& value) {
                static auto get = [](PyObject* self, void* value) -> PyObject* {
                    try {
                        return release(wrap(*reinterpret_cast<T*>(value)));
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
                        auto obj = reinterpret_borrow<Object>(new_val);
                        *reinterpret_cast<T*>(value) = static_cast<Wrapper>(obj);
                        return 0;
                    } catch (...) {
                        Exception::to_python();
                        return -1;
                    }
                };
                class_getters[Name] = {+get, reinterpret_cast<void*>(&value)};
                class_setters[Name] = {+set, reinterpret_cast<void*>(&value)};
            }

            /* Expose a C++-style const getter to Python as a getset descriptor. */
            template <StaticStr Name, typename Return>
            static void property(Return(CppType::*getter)() const) {
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
                                    return (obj.*member_ptr)();
                                },
                                [member_ptr](const CppType* obj) {
                                    return (obj->*member_ptr)();
                                }
                            },
                            obj->m_cpp
                        )));
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                def::tp_getset.push_back({
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
                        Closure* context = reinterpret_cast<Closure*>(closure);
                        return release(wrap(std::visit(
                            Visitor{
                                [context](CppType& obj) {
                                    return (obj.*(context->getter))();
                                },
                                [context](CppType* obj) {
                                    return (obj->*(context->getter))();
                                },
                                [context](const CppType* obj) {
                                    return (obj->*(context->getter))();
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
                        Closure* context = reinterpret_cast<Closure*>(closure);
                        std::visit(
                            Visitor{
                                [context, new_val](CppType& obj) {
                                    obj.*(context->setter)(static_cast<Value>(
                                        reinterpret_borrow<Object>(new_val)
                                    ));
                                },
                                [context, new_val](CppType* obj) {
                                    obj->*(context->setter)(static_cast<Value>(
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
                static auto get = [](PyObject* cls, void* closure) -> PyObject* {
                    try {
                        return release(wrap((reinterpret_cast<Return(*)()>(closure))()));
                    } catch (...) {
                        Exception::to_python();
                        return nullptr;
                    }
                };
                class_getters[Name] = {+get, reinterpret_cast<void*>(getter)};
            }

            /* Expose a C++-style static getter/setter pair to Python using the
            `__getattr__()` and `__setattr__()` slots of the metatype. */
            template <StaticStr Name, typename Return, typename Value>
            void classproperty(Return(*getter)(), void(*setter)(Value&&)) {
                static struct Closure {
                    Return(*getter)();
                    void(*setter)(Value&&);
                } closure = {getter, setter};
                static auto get = [](PyObject* cls, void* closure) -> PyObject* {
                    try {
                        return release(wrap(
                            reinterpret_cast<Closure*>(closure)->getter()
                        ));
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
                        Closure* context = reinterpret_cast<Closure*>(closure);
                        context->setter(static_cast<Value>(
                            reinterpret_borrow<Object>(new_val)
                        ));
                        return 0;
                    } catch (...) {
                        Exception::to_python();
                        return -1;
                    }
                };
                class_getters[Name] = {+get, reinterpret_cast<void*>(&closure)};
                class_setters[Name] = {+set, reinterpret_cast<void*>(&closure)};
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

            /* Initialize a type from the binding blueprint. */
            void finalize(Type<Wrapper>& cls) {
                PyMeta* meta = reinterpret_cast<PyMeta*>(ptr(cls));

                // populate the metaclass's C++ fields
                meta->instancecheck = &def::instancecheck;
                meta->subclasscheck = &def::subclasscheck;
                new (&meta->getters) PyMeta::Getters(std::move(class_getters));
                new (&meta->setters) PyMeta::Setters(std::move(class_setters));
                meta->template_instantiations = nullptr;
                meta->demangled = meta->get_demangled_name();
                if (meta->demangled == nullptr) {
                    Exception::from_python();
                }

                // transfer the context to the Python type object
                PyObject* key;
                PyObject* value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(context, &pos, &key, &value)) {
                    if (PyObject_SetAttr(cls, key, value)) {
                        Exception::from_python();
                    }
                }
            }

        };

    public:
        static constexpr Origin __origin__ = Origin::CPP;
        static constexpr StaticStr __doc__ = [] {
            return (
                "A Bertrand-generated Python wrapper for the '" +
                impl::demangle(typeid(CppType).name()) + "' C++ type."
            );
        }();
        static constexpr unsigned int tp_flags =
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
            Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_MANAGED_WEAKREF |
            Py_TPFLAGS_MANAGED_DICT;

        PyObject_HEAD
        Variant m_cpp;

        // TODO: maybe all of this is implemented in Bindings.finalize()?  The
        // __export__ method could just return Bindings.finalize() directly?

        /* Default implementation of `__export__` for C++ extensions, which delegates
        to the `__bind__` helper to simplify type creation.  Users should never need to
        override this method - just customize the __bind__ script instead. */
        template <typename... Bases, StaticStr ModName>
        static Type<Wrapper> __export__(Module<ModName>& module) {
            Bindings bind;
            CRTP::__bind__(bind);

            static bool initialized = false;
            static std::vector<PyType_Slot> type_slots {
                // {Py_tp_doc, reinterpret_cast<void*>(CRTP::__doc__.buffer)},
                // {Py_tp_dealloc, reinterpret_cast<void*>(CRTP::__dealloc__)},
                // {Py_tp_repr, reinterpret_cast<void*>(CRTP::__repr__)},
                // {Py_tp_hash, reinterpret_cast<void*>(CRTP::__hash__)},
                // {Py_tp_str, reinterpret_cast<void*>(CRTP::__str__)},
                // {Py_tp_traverse, reinterpret_cast<void*>(CRTP::__traverse__)},
                // {Py_tp_clear, reinterpret_cast<void*>(CRTP::__clear__)},
            };
            if (!initialized) {
                tp_methods.push_back({
                    nullptr,
                    nullptr,
                    0,
                    nullptr
                });
                tp_getset.push_back({
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr
                });
                tp_members.push_back({
                    nullptr,
                    0,
                    0,
                    0,
                    nullptr
                });
                if (tp_methods.size() > 1) {
                    type_slots.push_back({
                        Py_tp_methods,
                        tp_methods.data()
                    });
                }
                if (tp_getset.size() > 1) {
                    type_slots.push_back({
                        Py_tp_getset,
                        tp_getset.data()
                    });
                }
                if (tp_members.size() > 1) {
                    type_slots.push_back({
                        Py_tp_members,
                        tp_members.data()
                    });
                }
                type_slots.push_back({0, nullptr});
                initialized = true;
            }
            static PyType_Spec type_spec = {
                .name = typeid(CppType).name(),
                .basicsize = sizeof(CRTP),
                .itemsize = 0,
                .flags = CRTP::tp_flags,
                .slots = type_slots.data()
            };

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
                    &type_spec,
                    bases
                )
            );
            Py_XDECREF(bases);
            if (ptr(cls) == nullptr) {
                Exception::from_python();
            }

            bind.finalize(cls);
            return cls;
        }

        /* Expose a C++ class's attributes to Python using Bertrand's binding helpers.

        Every C++ extension needs to implement this function, which is executed as a
        script whenever its enclosing module is imported.  The argument is a mutable
        context that includes a variety of helpers for exposing C++ attributes to
        Python, including member and static variables with shared state, property and
        class property descriptors, as well as instance, class, and static methods that
        can be overloaded from either side of the language boundary.

        Any attribute added at this stage is guaranteed be unique for each module,
        allowing sub-interpreters with correct, per-module state.  It's still up to the
        user to make sure that the data referenced by the bindings does not cause race
        conditions with other modules, but the modules themselves will not present a
        barrier in this regard. */
        static void __bind__(Bindings& bind) {}

        /// NOTE: make sure that every subclass defines `__import__()`

        /* tp_dealloc calls the ordinary C++ destructor. */
        static void __dealloc__(CRTP* self) {
            PyObject_GC_UnTrack(self);  // required for heap types
            if (std::holds_alternative<CppType>(self->m_cpp)) {
                std::get<CppType>(self->m_cpp).~CppType();
            }
            PyTypeObject* type = Py_TYPE(self);
            type->tp_free(self);
            Py_DECREF(type);  // required for heap types
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

        // TODO: maybe there needs to be some handling to make sure that the container
        // yields specifically lvalue references to Python objects?  I might also need
        // to const_cast them in the __clear__ method in order to properly release
        // everything.

        /* tp_traverse registers any Python objects that are owned by the C++ object
        with Python's cyclic garbage collector. */
        static int __traverse__(CRTP* self, visitproc visit, void* arg) {
            if constexpr (
                impl::is_iterable<CppType> &&
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
                impl::is_iterable<CppType> &&
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

        // TODO: the only way to properly handle the call operator, init, etc. is to
        // use overload sets, and register each one independently.

        /* TODO: implement the following slots using metaprogramming where possible:
        *
        //  * tp_name
        //  * tp_basicsize
        //  * tp_itemsize
        //  * tp_dealloc
        //  * tp_repr
        //  * tp_hash
        * tp_vectorcall_offset  // needs some extra work in PyMemberDef
        * tp_dictoffset (or Py_TPFLAGS_MANAGED_DICT)
        * tp_weaklistoffset (or Py_TPFLAGS_MANAGED_WEAKREF)
        * tp_call
        //  * tp_str
        //  * tp_traverse
        //  * tp_clear
        * tp_iter
        * tp_iternext
        * tp_init
        * tp_alloc  // -> leave blank
        * tp_new  // -> always does C++-level initialization.
        * tp_free  // leave blank
        * tp_is_gc  // leave blank
        * tp_finalize  // leave blank
        * tp_vectorcall  (not valid for heap types?)
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

        // TODO: I'll have to implement a Python wrapper around a C++ iterator, which
        // would probably need to use the non-owning reference types in order to
        // accurately reflect mutability and avoid unnecessary allocation overhead.

    private:
        inline static std::vector<PyMethodDef> tp_methods;
        inline static std::vector<PyGetSetDef> tp_getset;
        inline static std::vector<PyMemberDef> tp_members;

        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::is_extension<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::CppType>
            )
        friend auto wrap(const T& obj) -> __as_object__<T>::type;
        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::is_extension<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::CppType>
            )
        friend auto wrap(T& obj) -> __as_object__<T>::type;
        template <std::derived_from<Object> T> requires (impl::is_extension<T>)
        friend auto& unwrap(T& obj);
        template <std::derived_from<Object> T> requires (impl::is_extension<T>)
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

        // TODO: I need to make sure that delegating to C++ isinstance()/issubclass()
        // does not cause an infinite recursion.  The default specialization of
        // __issubclass__ for C++-origin types should not call a Python-level
        // check, or possibly the other way around.  It might be possible to use
        // PyType_IsSubtype() to break the recursion.
        // -> Maybe when I write the __isinstance__ check for C++-origin types, it
        // will default to PyType_IsSubType()?

        /* Implements Python-level `isinstance()` checks against the class, forwarding
        to the `__isinstance__` control struct. */
        static bool instancecheck(PyObject* instance) {
            return isinstance<Wrapper>(reinterpret_borrow<Object>(instance));
        }

        /* Implements Python-level `issubclass()` checks against the class, forwarding
        to the `__issubclass__` control struct. */
        static bool subclasscheck(PyObject* subclass) {
            return issubclass<Wrapper>(reinterpret_borrow<Object>(subclass));
        }

    };

}


}  // namespace py


#endif  // BERTRAND_PYTHON_COMMON_TYPE_H
