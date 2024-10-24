#ifndef BERTRAND_CORE_UNION_H
#define BERTRAND_CORE_UNION_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"
#include "access.h"


namespace py {


/// TODO: when a Union is returned to Python, I should unpack it and only return the
/// active member, not the actual union itself.  That effectively means that the
/// Union object will never need to be used in Python itself, and exists mostly for the
/// benefit of C++, in order to allow it to conform to Python's dynamic typing.


template <typename... Types>
    requires (
        sizeof...(Types) > 0 &&
        (!std::is_reference_v<Types> && ...) &&
        (!std::is_const_v<Types> && ...) &&
        (!std::is_volatile_v<Types> && ...) &&
        (std::derived_from<Types, Object> && ...) &&
        impl::types_are_unique<Types...>
    )
struct Union;


template <std::derived_from<Object> T> requires (!std::same_as<T, NoneType>)
using Optional = Union<T, NoneType>;


template <impl::has_python T> requires (!std::same_as<T, NoneType>)
Union(T) -> Union<obj<T>, NoneType>;


namespace impl {

    template <typename T>
    constexpr bool _py_union = false;
    template <typename... Types>
    constexpr bool _py_union<Union<Types...>> = true;
    template <typename T>
    concept py_union = _py_union<std::remove_cvref_t<T>>;

    /* Converting a pack to a union involves converting all C++ types to their Python
    equivalents, then deduplicating the results.  If the final pack contains only a
    single type, then that type is returned directly, rather than instantiating a new
    Union. */
    template <typename>
    struct to_union;
    template <has_python... Matches>
    struct to_union<Pack<Matches...>> {
        template <typename pack>
        struct convert {
            template <typename T>
            struct ToPython { using type = python_type<T>; };
            template <>
            struct ToPython<void> { using type = NoneType; };
            template <typename>
            struct helper;
            template <typename... Unique>
            struct helper<Pack<Unique...>> {
                using type = Union<std::remove_cvref_t<Unique>...>;
            };
            using type = helper<
                typename Pack<typename ToPython<Matches>::type...>::deduplicate
            >::type;
        };
        template <typename Match>
        struct convert<Pack<Match>> { using type = Match; };
        using type = convert<typename Pack<Matches...>::deduplicate>::type;
    };

    /* Allow implicit conversion from the union type if and only if all of its
    qualified members are convertible to that type. */
    template <py_union U>
    struct UnionToType {
        template <typename>
        struct convertible;
        template <typename... Types>
        struct convertible<Union<Types...>> {
            template <typename Out>
            static constexpr bool convert =
                (std::convertible_to<qualify<Types, U>, Out> && ...);
        };
        template <typename Out>
        static constexpr bool convert = convertible<std::remove_cvref_t<U>>::convert;
    };

    /* Allow implicit conversion from a std::variant if and only if all members have
    equivalent Python types, and those types are convertible to the expected members of
    the union. */
    template <is_variant V>
    struct VariantToUnion {
        template <typename>
        struct convertible {
            static constexpr bool enable = false;
            using type = void;
            template <typename...>
            static constexpr bool convert = false;
        };
        template <has_python... Types>
        struct convertible<std::variant<Types...>> {
            static constexpr bool enable = true;
            using type = to_union<Pack<python_type<Types>...>>::type;
            template <typename, typename... Ts>
            static constexpr bool _convert = true;
            template <typename... To, typename T, typename... Ts>
            static constexpr bool _convert<Pack<To...>, T, Ts...> =
                (std::convertible_to<qualify<T, V>, To> || ...) &&
                _convert<Pack<To...>, Ts...>;
            template <typename... Ts>
            static constexpr bool convert = _convert<Pack<Types...>, Ts...>;
        };
        static constexpr bool enable = convertible<std::remove_cvref_t<V>>::enable;
        using type = convertible<std::remove_cvref_t<V>>::type;
        template <typename... Ts>
        static constexpr bool convert = convertible<
            std::remove_cvref_t<V>
        >::template convert<Ts...>;
    };

    /* Raw types are converted to a pack of length 1. */
    template <typename T>
    struct UnionTraits {
        using type = T;
        using pack = Pack<T>;
    };

    /* Unions are converted into a pack of the same length. */
    template <py_union T>
    struct UnionTraits<T> {
    private:

        template <typename>
        struct _pack;
        template <typename... Types>
        struct _pack<Union<Types...>> {
            using type = Pack<Types...>;
        };

    public:
        using type = T;
        using pack = _pack<std::remove_cvref_t<T>>::type;
    };

    /* A generalized operator factory that uses template metaprogramming to evaluate a
    control structure over a set of argument types and collect the possible returns.
    Splits unions, variants, and optionals into a cartesian product of possible
    permutations, extracts those that satisfy the control structure, and then collapses
    the results into either a single type or an output union. */
    template <template <typename...> class control>
    struct union_operator {
        template <typename...>
        struct op { static constexpr bool enable = false; };
        template <typename... Args>
            requires (sizeof...(Args) > 0 && (py_union<Args> || ...))
        struct op<Args...> {
        private:

            /* 1. Convert the input arguments into a sequence of packs, where unions,
            optionals, and variants are represented as packs of length > 1, then
            compute the cartesian product. */
            template <typename>
            struct _product;
            template <typename First, typename... Rest>
            struct _product<Pack<First, Rest...>> {
                using type = First::template product<Rest...>;
            };
            using product = _product<Pack<typename UnionTraits<Args>::pack...>>::type;

            /* 2. Produce an output pack containing all of the valid return types for
            each permutation that satisfies the control structure. */
            template <typename>
            struct traits;
            template <typename... Permutations>
            struct traits<Pack<Permutations...>> {
                template <typename out, typename...>
                struct _returns { using type = out; };
                template <typename... out, typename P, typename... Ps>
                struct _returns<Pack<out...>, P, Ps...> {
                    template <typename>
                    struct filter { using type = Pack<out...>; };
                    template <typename P2> requires (P2::template enable<control>)
                    struct filter<P2> {
                        using type = Pack<out..., typename P2::template type<control>>;
                    };
                    using type = _returns<typename filter<P>::type, Ps...>::type;
                };
                using returns = _returns<Pack<>, Permutations...>::type;
            };

            /* 3. Deduplicate the return types, merging those that only differ in
            cvref qualifications, which forces a copy/move when called. */
            using returns = traits<product>::returns::deduplicate;

        public:

            /* 4. Enable the operation if and only if at least one permutation
            satisfies the control structure, and thus has a valid return type. */
            static constexpr bool enable = returns::n > 0;

            /* 5. If there is only one valid return type, return it directly, otherwise
            construct a new union and convert all of the results to Python. */
            using type = to_union<returns>::type;

            /* 6. Calling the operator will determine the actual type held in each
            union and forward to either a success or failure callback that is templated
            to receive them.  If the control structure is not enabled for the observed
            types, then the failure callback will be used, which is expected to raise a
            Python-style runtime error, as if you were working with a generic object.
            Otherwise, the success callback will be used, which will implement the
            operation with the internal state of each union. */
            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                OnSuccess&& success,
                OnFailure&& failure,
                Args... args
            ) {
                return call<0>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Args>(args)...
                );
            }

        private:

            template <
                size_t I,
                typename OnSuccess,
                typename OnFailure,
                typename... Actual
            >
            static type call(
                OnSuccess&& success,
                OnFailure&& failure,
                Actual&&... args
            ) {
                // base case: all arguments have been processed
                if constexpr (I == sizeof...(Args)) {
                    if constexpr (control<Actual...>::enable) {
                        if constexpr (
                            !std::is_void_v<type> &&
                            std::is_invocable_r_v<void, OnSuccess, Actual...>
                        ) {
                            success(std::forward<Actual>(args)...);
                            return None;
                        } else {
                            return success(std::forward<Actual>(args)...);
                        }
                    } else {
                        failure(std::forward<Actual>(args)...);
                        std::unreachable();  // failure() always raises an error
                    }

                // recursive case: determine the actual type of the current argument
                } else {
                    using T = unpack_type<I, Actual...>;

                    // if the argument is a union, recur until the correct index is
                    // identified, then reinterpret and continue to the next argument
                    if constexpr (py_union<T>) {
                        return unpack_union<I, std::remove_cvref_t<T>>::template call<0>(
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure),
                            std::forward<Actual>(args)...
                        );

                    // otherwise, skip to the next argument
                    } else {
                        return call<I + 1>(
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure),
                            std::forward<Actual>(args)...
                        );
                    }
                }
            }

            template <size_t I, typename>
            struct unpack_union;
            template <size_t I, typename... Types>
            struct unpack_union<I, Union<Types...>> {
                template <
                    size_t J,
                    typename OnSuccess,
                    typename OnFailure,
                    typename... Actual
                >
                static type call(
                    OnSuccess&& success,
                    OnFailure&& failure,
                    Actual&&... args
                ) {
                    // the union's active index is always within bounds
                    if constexpr (J == sizeof...(Types)) {
                        std::unreachable();

                    // if J is equal to the union's active index, then reinterpret
                    // the object accordingly, carrying over any cvref qualifiers from
                    // the union itself
                    } else {
                        using U = std::conditional_t<
                            std::is_reference_v<unpack_type<I, Actual...>>,
                            unpack_type<I, Actual...>,
                            std::add_rvalue_reference_t<unpack_type<I, Actual...>>
                        >;
                        U arg = reinterpret_cast<U>(
                            unpack_arg<I>(std::forward<Actual>(args)...)
                        );
                        if (J == arg->m_index) {
                            return []<size_t... Prev, size_t... Next>(
                                U arg,
                                std::index_sequence<Prev...>,
                                std::index_sequence<Next...>,
                                OnSuccess&& success,
                                OnFailure&& failure,
                                Actual&&... args
                            ) {
                                // once the correct type has been identified, return
                                // back to the outer call<>() helper.
                                using T = qualify<unpack_type<J, Types...>, U>;
                                return op::call<I + 1>(
                                    std::forward<OnSuccess>(success),
                                    std::forward<OnFailure>(failure),
                                    unpack_arg<Prev>(std::forward<Actual>(args)...)...,
                                    reinterpret_cast<T>(std::forward<U>(arg)->m_value),
                                    unpack_arg<I + 1 + Next>(std::forward<Actual>(args)...)...
                                );
                            }(
                                std::forward<U>(arg),
                                std::make_index_sequence<I>{},
                                std::make_index_sequence<sizeof...(Args) - (I + 1)>{},
                                std::forward<OnSuccess>(success),
                                std::forward<OnFailure>(failure),
                                std::forward<Actual>(args)...
                            );
                        }
                        return unpack_union::call<J + 1>(
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure),
                            std::forward<Actual>(args)...
                        );
                    }
                }
            };
        };
    };

    // unary operators
    template <typename T>
    using UnionHash = union_operator<__hash__>::template op<T>;
    template <typename T>
    using UnionLen = union_operator<__len__>::template op<T>;
    template <typename T>
    using UnionIter = union_operator<__iter__>::template op<T>;
    template <typename T>
    using UnionReversed = union_operator<__reversed__>::template op<T>;
    template <typename T>
    using UnionAbs = union_operator<__abs__>::template op<T>;
    template <typename T>
    using UnionInvert = union_operator<__invert__>::template op<T>;
    template <typename T>
    using UnionPos = union_operator<__pos__>::template op<T>;
    template <typename T>
    using UnionNeg = union_operator<__neg__>::template op<T>;
    template <typename T>
    using UnionIncrement = union_operator<__increment__>::template op<T>;
    template <typename T>
    using UnionDecrement = union_operator<__decrement__>::template op<T>;

    // binary operators
    template <typename L, typename R>
    using UnionContains = union_operator<__contains__>::template op<L, R>;
    template <typename L, typename R>
    using UnionLess = union_operator<__lt__>::template op<L, R>;
    template <typename L, typename R>
    using UnionLessEqual = union_operator<__le__>::template op<L, R>;
    template <typename L, typename R>
    using UnionEqual = union_operator<__eq__>::template op<L, R>;
    template <typename L, typename R>
    using UnionNotEqual = union_operator<__ne__>::template op<L, R>;
    template <typename L, typename R>
    using UnionGreaterEqual = union_operator<__ge__>::template op<L, R>;
    template <typename L, typename R>
    using UnionGreater = union_operator<__gt__>::template op<L, R>;
    template <typename L, typename R>
    using UnionAdd = union_operator<__add__>::template op<L, R>;
    template <typename L, typename R>
    using UnionSub = union_operator<__sub__>::template op<L, R>;
    template <typename L, typename R>
    using UnionMul = union_operator<__mul__>::template op<L, R>;
    template <typename L, typename R>
    using UnionTrueDiv = union_operator<__truediv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionFloorDiv = union_operator<__floordiv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionMod = union_operator<__mod__>::template op<L, R>;
    template <typename L, typename R>
    using UnionLShift = union_operator<__lshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionRShift = union_operator<__rshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionAnd = union_operator<__and__>::template op<L, R>;
    template <typename L, typename R>
    using UnionXor = union_operator<__xor__>::template op<L, R>;
    template <typename L, typename R>
    using UnionOr = union_operator<__or__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceAdd = union_operator<__iadd__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceSub = union_operator<__isub__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceMul = union_operator<__imul__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceTrueDiv = union_operator<__itruediv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceFloorDiv = union_operator<__ifloordiv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceMod = union_operator<__imod__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceLShift = union_operator<__ilshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceRShift = union_operator<__irshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceAnd = union_operator<__iand__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceXor = union_operator<__ixor__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceOr = union_operator<__ior__>::template op<L, R>;

    // ternary operators
    template <typename Base, typename Exp, typename Mod>
    using UnionPow = union_operator<__pow__>::template op<Base, Mod, Exp>;
    template <typename Base, typename Exp, typename Mod>
    using UnionInplacePow = union_operator<__ipow__>::template op<Base, Exp, Mod>;

    // n-ary operators
    template <typename Self, typename... Args>
    using UnionCall = union_operator<__call__>::template op<Self, Args...>;
    template <typename Self, typename... Key>
    using UnionGetItem = union_operator<__getitem__>::template op<Self, Key...>;
    template <typename Self, typename Value, typename... Key>
    using UnionSetItem = union_operator<__setitem__>::template op<Self, Value, Key...>;
    template <typename Self, typename... Key>
    using UnionDelItem = union_operator<__delitem__>::template op<Self, Key...>;

    /// NOTE: Attr<> operators cannot be generalized due to accepting a non-type
    /// template parameter (the attribute name).

    template <typename, StaticStr>
    struct UnionGetAttr { static constexpr bool enable = false; };
    template <py_union Self, StaticStr Name>
    struct UnionGetAttr<Self, Name> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename... Types>
            requires (__getattr__<qualify_lvalue<Types, Self>, Name>::enable || ...)
        struct traits<Union<Types...>> {
            template <typename result, typename... Ts>
            struct unary { using type = result; };
            template <typename... Matches, typename T, typename... Ts>
            struct unary<Pack<Matches...>, T, Ts...> {
                template <typename>
                struct conditional { using type = Pack<Matches...>; };
                template <typename T2>
                    requires (__getattr__<qualify_lvalue<T2, Self>, Name>::enable)
                struct conditional<T2> {
                    using type = Pack<
                        Matches...,
                        typename __getattr__<qualify_lvalue<T2, Self>, Name>::type
                    >;
                };
                using type = unary<typename conditional<T>::type, Ts...>::type;
            };
            static constexpr bool enable = true;
            using type = to_union<typename unary<Pack<>, Types...>::type>::type;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = traits<std::remove_cvref_t<Self>>::type;
    };

    template <typename, StaticStr, typename>
    struct UnionSetAttr { static constexpr bool enable = false; };
    template <py_union Self, StaticStr Name, typename Value>
    struct UnionSetAttr<Self, Name, Value> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__setattr__<qualify_lvalue<Types, Self>, Name, Value>::enable || ...)
        static constexpr bool match<Union<Types...>> = true;
        static constexpr bool enable = match<std::remove_cvref_t<Self>>;
        using type = void;
    };

    template <typename, StaticStr>
    struct UnionDelAttr { static constexpr bool enable = false; };
    template <py_union Self, StaticStr Name>
    struct UnionDelAttr<Self, Name> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__delattr__<qualify_lvalue<Types, Self>, Name>::enable || ...)
        static constexpr bool match<Union<Types...>> = true;
        static constexpr bool enable = match<std::remove_cvref_t<Self>>;
        using type = void;
    };

    /// NOTE: __isinstance__ and __issubclass__ cannot be generalized due to using
    /// several different call signatures, which must be handled uniquely.

    /// TODO: Maybe __isinstance__, __issubclass__, and __repr__ structures need to
    /// default to enabled as long as the template conditions in the operator are met.
    /// -> changes to declarations to make sure the state of the control structure
    /// always determines the enable state of the operator.
    /// -> Maybe default __isinstance__ and __issubclass__ can enable the 2-argument
    /// form if the object implements an `__instancecheck__()` or `__subclasscheck__()`
    /// Python member.

    template <typename Derived, typename Base>
    struct UnionIsInstance {
        template <typename, typename>
        struct op { static constexpr bool enable = false; };
        template <typename... Ds, typename... Bs>
        struct op<Union<Ds...>, Union<Bs...>> {
            /// TODO: true if ALL Ds are instances of ANY Bs
        };
        template <typename... Ds, typename B>
        struct op<Union<Ds...>, B> {
            /// TODO: true if ALL Ds are instances of B
        };
        template <typename D, typename... Bs>
        struct op<D, Union<Bs...>> {
            /// TODO: true if D is an instances of ANY Bs
        };
        static constexpr bool enable = op<
            std::remove_cvref_t<Derived>,
            std::remove_cvref_t<Base>
        >::enable;
        using type = bool;
    };

    template <typename Derived, typename Base>
    struct UnionIsSubclass {
        template <typename, typename>
        struct op { static constexpr bool enable = false; };
        template <typename... Ds, typename... Bs>
        struct op<Union<Ds...>, Union<Bs...>> {
            /// TODO: true if ALL Ds are subclasses of ANY Bs
        };
        template <typename... Ds, typename B>
        struct op<Union<Ds...>, B> {
            /// TODO: true if ALL Ds are subclasses of B
        };
        template <typename D, typename... Bs>
        struct op<D, Union<Bs...>> {
            /// TODO: true if D is a subclass of ANY Bs
        };
        static constexpr bool enable = op<
            std::remove_cvref_t<Derived>,
            std::remove_cvref_t<Base>
        >::enable;
        using type = bool;
    };

}


template <typename... Types>
struct Interface<Union<Types...>> : impl::UnionTag {};
template <typename... Types>
struct Interface<Type<Union<Types...>>> : impl::UnionTag {};


template <typename... Types>
    requires (
        sizeof...(Types) > 0 &&
        (!std::is_reference_v<Types> && ...) &&
        (!std::is_const_v<Types> && ...) &&
        (!std::is_volatile_v<Types> && ...) &&
        (std::derived_from<Types, Object> && ...) &&
        impl::types_are_unique<Types...>
    )
struct Union : Object, Interface<Union<Types...>> {
    struct __python__ : def<__python__, Union>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A simple union type in Python, similar to `std::variant` in C++.

Notes
-----
Due to its dynamic nature, all Python objects can technically be unions by
default, just not in a type-safe manner, and not in a way that can be easily
translated into C++.  This class is meant to fix that by providing a more
structured way of handling these cases, allowing Python variables and functions
that are annotated with `Union[T1, T2, ...]` or `T1 | T2 | ...` to have those
same semantics reflected in C++ and enforced at compile time.  Generally
speaking, users should never need to specify one of these types from Python,
as the existing `|` syntax will automatically create them as needed.  They are
fairly common when writing Python code from C++, however.

Unions are implemented identically to `std::variant` in C++, except that the
value is stored as an opaque `PyObject*` pointer rather than using aligned
storage.  As such, unions have a fixed size and alignment, consisting of only
a single pointer and an index to identify the active type.  They support all of
the same operations as `std::variant`, including `std::holds_alternative`,
`std::get`, `std::get_if`, and `std::visit` with identical semantics, on top of
specializing each of the built-in control structures to forward to the active
type, which gives them the same interface as a generic `Object`.  Each operator
will be enabled if and only if at least one member of the union supports it,
and will produce a new union if multiple results are possible.  This leads to
an automatic narrowing behavior, whereby operating on a union will
progressively reduce the number of possible results until only one remains.  If
the active member does not support a given operation, then a corresponding
Python error will be raised at runtime, as if the operation had been attempted
from Python directly.  As in Python, it is the user's responsibility to
determine the active type of the union before performing any operations on it
if they wish to avoid this behavior.

Note that `Optional[T]` is a simple alias to `Union[T, NoneType]` in both
languages.  They have no special behavior otherwise, which ensures that no
special cases exist when translating Python type annotations into C++ and vice
versa.  Therefore, a Python annotation of `T1 | T2 | None` will be directly
transformed into `Union<T1, T2, NoneType>` in C++ and vice versa, without any
further considerations.

Also note that if a union includes `NoneType` as a valid member, then it can be
used to model C++ types which have no direct Python equivalent, such as
`std::optional` and pointers, either smart or otherwise.  In this case, `None`
will be used to represent empty optionals and null pointers, which will be
translated back and forth between the two languages as needed.

Lastly, `Union[...]` types can be supplied to both Python and C++-level
`isinstance()` and `issubclass()` checks as if they were tuples of their
constituent types, and are produced automatically whenever a bertrand type is
combined with `|` syntax from Python.

Examples
--------
>>> from bertrand import Union
>>> x = Union[int, str](42)
>>> x
42
>>> x + 15
57
>>> x = Union[int, str]("hello")
>>> x
'hello'
>>> x.capitalize()
'Hello'
>>> x = Union[int, str](True)  # bool is a subclass of int
>>> x
True
>>> x.capitalize()
Traceback (most recent call last):
    ...
AttributeError: 'bool' object has no attribute 'capitalize'
>>> x = Union[int, str](1+2j)  # complex is not a member of the union
Traceback (most recent call last):
    ...
TypeError: cannot convert complex to Union[int, str]

Just like other Bertrand types, unions are type-safe and usable in both
languages with the same interface and semantics.  This allows for seamless
interoperability across the language barrier, and ensures that the same code
can be used in both contexts.

```
export module example;
export import :bertrand;

export namespace example {

    py::Union<py::Int, py::Str> foo(py::Int x, py::Str y) {
        if (y % 2) {
            return x;
        } else {
            return y;
        }
    }

    py::Str bar(py::Union<py::Int, py::Str> x) {
        if (std::holds_alternative<py::Int>(x)) {
            return "int";
        } else {
            return "str";
        }
    }

}
```

>>> import example
>>> example.foo(0, "hello")
'hello'
>>> example.foo(1, "hello")
1
>>> example.bar(0)
'int'
>>> example.bar("hello")
'str'
>>> example.bar(1.0)
Traceback (most recent call last):
    ...
TypeError: cannot convert float to Union[int, str]

Additionally, Python functions that accept or return unions using Python-style
type hints will automatically be translated into C++ functions using the
corresponding `py::Union<...>` types when bindings are generated.  Note that
due to the interaction between unions and optionals, a `None` type hint as a
member of the union in Python syntax will be transformed into a nested optional
type in C++, as described above.

```
#example.py

def foo(x: int | str) -> str:
    if isinstance(x, int):
        return "int"
    else:
        return "str"

def bar(x: int | None = None) -> str:
    if x is None:
        return "none"
    else:
        return "int"

def baz(x: int | str | None = None) -> str:
    if x is None:
        return "none"
    elif isinstance(x, int):
        return "int"
    else:
        return "str"
```

```
import example;

int main() {
    using namespace py;

    // Str(*example::foo)(Arg<"x", Union<Int, Str>>)
    example::foo(0);        // "int"
    example::foo("hello");  // "str"

    // Str(*example::bar)(Arg<"x", Optional<Int>>::opt)
    example::bar();         // "none"
    example::bar(None);     // "none"
    example::bar(0);        // "int"

    // Str(*example::baz)(Arg<"x", Union<Int, Str, NoneType>>::opt)
    example::baz();         // "none"
    example::baz(None);     // "none"
    example::baz(0);        // "int"
    example::baz("hello");  // "str"
}
```)doc";

    private:

        template <size_t I>
        static size_t find_matching_type(const Object& obj, size_t& first) {
            Type<impl::unpack_type<I, Types...>> type;
            if (reinterpret_cast<PyObject*>(Py_TYPE(ptr(obj))) == ptr(type)) {
                return I;
            } else if (first == sizeof...(Types)) {
                int rc = PyObject_IsInstance(
                    ptr(obj),
                    ptr(type)
                );
                if (rc == -1) {
                    Exception::to_python();
                } else if (rc) {
                    first = I;
                }
            }
            if constexpr (I + 1 >= sizeof...(Types)) {
                return sizeof...(Types);
            } else {
                return find_matching_type<I + 1>(obj, first);
            }
        }

    public:

        Object m_value;
        size_t m_index;
        vectorcallfunc vectorcall = reinterpret_cast<vectorcallfunc>(__call__);

        template <typename T> requires (std::same_as<std::remove_cvref_t<T>, Types> || ...)
        explicit __python__(T&& value) :
            m_value(std::forward<T>(value)),
            m_index(impl::index_of<std::remove_cvref_t<T>, Types...>)
        {}

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            __python__* self = reinterpret_cast<__python__*>(
                type->tp_alloc(type, 0)
            );
            if (self != nullptr) {
                new (&self->m_value) Object(None);
                self->m_index = 0;
            }
            return self;
        }

        static int __init__(
            __python__* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                if (kwargs) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "Union constructor does not accept keyword arguments"
                    );
                    return -1;
                }
                size_t nargs = PyTuple_GET_SIZE(args);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "Union constructor requires exactly one argument"
                    );
                    return -1;
                }
                constexpr StaticStr str = "bertrand";
                PyObject* name = PyUnicode_FromStringAndSize(str, str.size());
                if (name == nullptr) {
                    return -1;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(name));
                Py_DECREF(name);
                if (bertrand.is(nullptr)) {
                    return -1;
                }
                Object converted = reinterpret_steal<Object>(PyObject_CallOneArg(
                    ptr(bertrand),
                    PyTuple_GET_ITEM(args, 0)
                ));
                if (converted.is(nullptr)) {
                    return -1;
                }
                size_t subclass = sizeof...(Types);
                size_t match = find_matching_type<0>(converted, subclass);
                if (match == sizeof...(Types)) {
                    if (subclass == sizeof...(Types)) {
                        std::string message = "cannot convert object of type '";
                        message += impl::demangle(Py_TYPE(ptr(converted))->tp_name);
                        message += "' to '";
                        message += impl::demangle(ptr(Type<Union<Types...>>())->tp_name);
                        message += "'";
                        PyErr_SetString(PyExc_TypeError, message.c_str());
                        return -1;
                    } else {
                        match = subclass;
                    }
                }
                self->m_index = match;
                self->m_value = std::move(converted);
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        template <StaticStr ModName>
        static Type<Union> __export__(Module<ModName>& mod);
        static Type<Union> __import__();

        static PyObject* __wrapped__(__python__* self) noexcept {
            return Py_NewRef(ptr(self->m_value));
        }

        static PyObject* __repr__(__python__* self) noexcept {
            return PyObject_Repr(ptr(self->m_value));
        }

        /// TODO: these slots should only be enabled if the underlying objects support
        /// them.  Basically, when I'm exposing the heap type, I'll use a static
        /// vector and conditionally append all of these slots.

        static PyObject* __hash__(__python__* self) noexcept {
            return PyObject_Hash(ptr(self->m_value));
        }

        static PyObject* __call__(
            __python__* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            return PyObject_Vectorcall(
                ptr(self->m_value),
                args,
                nargsf,
                kwnames
            );
        }

        static PyObject* __str__(__python__* self) noexcept {
            return PyObject_Str(ptr(self->m_value));
        }

        static PyObject* __getattr__(__python__* self, PyObject* attr) noexcept {
            try {
                Py_ssize_t len;
                const char* data = PyUnicode_AsUTF8AndSize(attr, &len);
                if (data == nullptr) {
                    return nullptr;
                }
                std::string_view name = {data, static_cast<size_t>(len)};
                if (name == "__wrapped__") {
                    return PyObject_GenericGetAttr(ptr(self->m_value), attr);
                }
                return PyObject_GetAttr(ptr(self->m_value), attr);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __setattr__(
            __python__* self,
            PyObject* attr,
            PyObject* value
        ) noexcept {
            try {
                Py_ssize_t len;
                const char* data = PyUnicode_AsUTF8AndSize(attr, &len);
                if (data == nullptr) {
                    return -1;
                }
                std::string_view name = {data, static_cast<size_t>(len)};
                if (name == "__wrapped__") {
                    std::string message = "cannot ";
                    message += value ? "set" : "delete";
                    message += " attribute '" + std::string(name) + "'";
                    PyErr_SetString(
                        PyExc_AttributeError,
                        message.c_str()
                    );
                    return -1;
                }
                return PyObject_SetAttr(ptr(self->m_value), attr, value);

            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static int __traverse__(
            __python__* self,
            visitproc visit,
            void* arg
        ) noexcept {
            PyTypeObject* type = Py_TYPE(ptr(self->m_value));
            if (type->tp_traverse) {
                return type->tp_traverse(ptr(self->m_value), visit, arg);
            }
            return def<__python__, Union>::__traverse__(self, visit, arg);
        }

        static int __clear__(__python__* self) noexcept {
            PyTypeObject* type = Py_TYPE(ptr(self->m_value));
            if (type->tp_clear) {
                return type->tp_clear(ptr(self->m_value));
            }
            return def<__python__, Union>::__clear__(self);
        }

        static int __richcmp__(
            __python__* self,
            PyObject* other,
            int op
        ) noexcept {
            return PyObject_RichCompareBool(ptr(self->m_value), other, op);
        }

        static PyObject* __iter__(__python__* self) noexcept {
            return PyObject_GetIter(ptr(self->m_value));
        }

        static PyObject* __next__(__python__* self) noexcept {
            return PyIter_Next(ptr(self->m_value));
        }

        static PyObject* __get__(
            __python__* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            PyTypeObject* cls = reinterpret_cast<PyTypeObject*>(type);
            if (cls->tp_descr_get) {
                return cls->tp_descr_get(ptr(self), obj, type);
            }
            PyErr_SetString(
                PyExc_TypeError,
                "object is not a descriptor"
            );
            return nullptr;
        }

        static PyObject* __set__(
            __python__* self,
            PyObject* obj,
            PyObject* value
        ) noexcept {
            PyTypeObject* cls = reinterpret_cast<PyTypeObject*>(Py_TYPE(ptr(self)));
            if (cls->tp_descr_set) {
                return cls->tp_descr_set(ptr(self), obj, value);
            }
            if (value) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "object does not support descriptor assignment"
                );
            } else {
                PyErr_SetString(
                    PyExc_AttributeError,
                    "object does not support descriptor deletion"
                );
            }
            return nullptr;
        }

        static PyObject* __getitem__(__python__* self, PyObject* key) noexcept {
            return PyObject_GetItem(ptr(self->m_value), key);
        }

        static PyObject* __sq_getitem__(__python__* self, Py_ssize_t index) noexcept {
            return PySequence_GetItem(ptr(self->m_value), index);
        }

        static PyObject* __setitem__(
            __python__* self,
            PyObject* key,
            PyObject* value
        ) noexcept {
            return PyObject_SetItem(ptr(self->m_value), key, value);
        }

        static int __sq_setitem__(
            __python__* self,
            Py_ssize_t index,
            PyObject* value
        ) noexcept {
            return PySequence_SetItem(ptr(self->m_value), index, value);
        }

        static Py_ssize_t __len__(__python__* self) noexcept {
            return PyObject_Length(ptr(self->m_value));
        }

        static int __contains__(__python__* self, PyObject* key) noexcept {
            return PySequence_Contains(ptr(self->m_value), key);
        }

        static PyObject* __await__(__python__* self) noexcept {
            PyAsyncMethods* async = Py_TYPE(ptr(self))->tp_as_async;
            if (async && async->am_await) {
                return async->am_await(ptr(self));
            }
            PyErr_SetString(
                PyExc_TypeError,
                "object is not awaitable"
            );
            return nullptr;
        }

        static PyObject* __aiter__(__python__* self) noexcept {
            PyAsyncMethods* async = Py_TYPE(ptr(self))->tp_as_async;
            if (async && async->am_aiter) {
                return async->am_aiter(ptr(self));
            }
            PyErr_SetString(
                PyExc_TypeError,
                "object is not an async iterator"
            );
            return nullptr;
        }

        static PyObject* __anext__(__python__* self) noexcept {
            PyAsyncMethods* async = Py_TYPE(ptr(self))->tp_as_async;
            if (async && async->am_anext) {
                return async->am_anext(ptr(self));
            }
            PyErr_SetString(
                PyExc_TypeError,
                "object is not an async iterator"
            );
            return nullptr;
        }

        static PySendResult __asend__(
            __python__* self,
            PyObject* arg,
            PyObject** prsesult
        ) noexcept {
            return PyIter_Send(ptr(self->m_value), arg, prsesult);
        }

        static PyObject* __add__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Add(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Add(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __iadd__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceAdd(ptr(lhs->m_value), rhs);
        }

        static PyObject* __sub__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Subtract(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Subtract(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __isub__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceSubtract(ptr(lhs->m_value), rhs);
        }

        static PyObject* __mul__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Multiply(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Multiply(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repeat__(__python__* lhs, Py_ssize_t rhs) noexcept {
            return PySequence_Repeat(ptr(lhs->m_value), rhs);
        }

        static PyObject* __imul__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceMultiply(ptr(lhs->m_value), rhs);
        }

        static PyObject* __irepeat__(__python__* lhs, Py_ssize_t rhs) noexcept {
            return PySequence_InPlaceRepeat(ptr(lhs->m_value), rhs);
        }

        static PyObject* __mod__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Remainder(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Remainder(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __imod__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceRemainder(ptr(lhs->m_value), rhs);
        }

        static PyObject* __divmod__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Divmod(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Divmod(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __power__(PyObject* lhs, PyObject* rhs, PyObject* mod) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Power(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs,
                        mod
                    );
                } else if (PyType_IsSubtype(
                    Py_TYPE(rhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Power(
                        lhs,
                        ptr(reinterpret_cast<__python__*>(rhs)->m_value),
                        mod
                    );
                }
                return PyNumber_Power(
                    lhs,
                    rhs,
                    ptr(reinterpret_cast<__python__*>(mod)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ipower__(__python__* lhs, PyObject* rhs, PyObject* mod) noexcept {
            return PyNumber_InPlacePower(ptr(lhs->m_value), rhs, mod);
        }

        static PyObject* __neg__(__python__* self) noexcept {
            return PyNumber_Negative(ptr(self->m_value));
        }

        static PyObject* __pos__(__python__* self) noexcept {
            return PyNumber_Positive(ptr(self->m_value));
        }

        static PyObject* __abs__(__python__* self) noexcept {
            return PyNumber_Absolute(ptr(self->m_value));
        }

        static int __bool__(__python__* self) noexcept {
            return PyObject_IsTrue(ptr(self->m_value));
        }

        static PyObject* __invert__(__python__* self) noexcept {
            return PyNumber_Invert(ptr(self->m_value));
        }

        static PyObject* __lshift__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Lshift(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Lshift(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ilshift__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceLshift(ptr(lhs->m_value), rhs);
        }

        static PyObject* __rshift__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Rshift(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Rshift(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __irshift__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceRshift(ptr(lhs->m_value), rhs);
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __iand__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceAnd(ptr(lhs->m_value), rhs);
        }

        static PyObject* __xor__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Xor(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Xor(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ixor__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceXor(ptr(lhs->m_value), rhs);
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ior__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceOr(ptr(lhs->m_value), rhs);
        }

        static PyObject* __int__(__python__* self) noexcept {
            return PyNumber_Long(ptr(self->m_value));
        }

        static PyObject* __float__(__python__* self) noexcept {
            return PyNumber_Float(ptr(self->m_value));
        }

        static PyObject* __floordiv__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_FloorDivide(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_FloorDivide(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ifloordiv__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceFloorDivide(ptr(lhs->m_value), rhs);
        }

        static PyObject* __truediv__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_TrueDivide(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_TrueDivide(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __itruediv__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceTrueDivide(ptr(lhs->m_value), rhs);
        }

        static PyObject* __index__(__python__* self) noexcept {
            return PyNumber_Index(ptr(self->m_value));
        }

        static PyObject* __matmul__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_MatrixMultiply(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_MatrixMultiply(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __imatmul__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceMatrixMultiply(ptr(lhs->m_value), rhs);
        }

        static int __buffer__(
            __python__* exported,
            Py_buffer* view,
            int flags
        ) noexcept {
            return PyObject_GetBuffer(ptr(exported->m_value), view, flags);
        }

        static void __release_buffer__(__python__* exported, Py_buffer* view) noexcept {
            PyBuffer_Release(view);
        }

    private:

        inline static PyGetSetDef properties[] = {
            {
                "__wrapped__",
                reinterpret_cast<getter>(__wrapped__),
                nullptr,
                PyDoc_STR(
R"doc(The value stored in the optional.

Returns
-------
object
    The value stored in the optional, or None if it is currently empty.

Notes
-----
The presence of a `__wrapped__` attribute triggers some special behavior in
both the Python and Bertrand APIs.  In Python, it allows the `inspect` module
to unwrap the optional and inspect the internal value, in the same way as
`functools.partial` and `functools.wraps`.  In Bertrand, some operators
(like the `isinstance()` operator) will check for the presence of this
attribute and unwrap the optional if it is present.)doc"
                )
            },
            {nullptr}
        };

        inline static PyAsyncMethods async = {
            .am_await = reinterpret_cast<unaryfunc>(__await__),
            .am_aiter = reinterpret_cast<unaryfunc>(__aiter__),
            .am_anext = reinterpret_cast<unaryfunc>(__anext__),
            .am_send = reinterpret_cast<sendfunc>(__asend__),
        };

        inline static PyNumberMethods number = {
            .nb_add = reinterpret_cast<binaryfunc>(__add__),
            .nb_subtract = reinterpret_cast<binaryfunc>(__sub__),
            .nb_multiply = reinterpret_cast<binaryfunc>(__mul__),
            .nb_remainder = reinterpret_cast<binaryfunc>(__mod__),
            .nb_divmod = reinterpret_cast<binaryfunc>(__divmod__),
            .nb_power = reinterpret_cast<ternaryfunc>(__power__),
            .nb_negative = reinterpret_cast<unaryfunc>(__neg__),
            .nb_positive = reinterpret_cast<unaryfunc>(__pos__),
            .nb_absolute = reinterpret_cast<unaryfunc>(__abs__),
            .nb_bool = reinterpret_cast<inquiry>(__bool__),
            .nb_invert = reinterpret_cast<unaryfunc>(__invert__),
            .nb_lshift = reinterpret_cast<binaryfunc>(__lshift__),
            .nb_rshift = reinterpret_cast<binaryfunc>(__rshift__),
            .nb_and = reinterpret_cast<binaryfunc>(__and__),
            .nb_xor = reinterpret_cast<binaryfunc>(__xor__),
            .nb_or = reinterpret_cast<binaryfunc>(__or__),
            .nb_int = reinterpret_cast<unaryfunc>(__int__),
            .nb_float = reinterpret_cast<unaryfunc>(__float__),
            .nb_inplace_add = reinterpret_cast<binaryfunc>(__iadd__),
            .nb_inplace_subtract = reinterpret_cast<binaryfunc>(__isub__),
            .nb_inplace_multiply = reinterpret_cast<binaryfunc>(__imul__),
            .nb_inplace_remainder = reinterpret_cast<binaryfunc>(__imod__),
            .nb_inplace_power = reinterpret_cast<ternaryfunc>(__ipower__),
            .nb_inplace_lshift = reinterpret_cast<binaryfunc>(__ilshift__),
            .nb_inplace_rshift = reinterpret_cast<binaryfunc>(__irshift__),
            .nb_inplace_and = reinterpret_cast<binaryfunc>(__iand__),
            .nb_inplace_xor = reinterpret_cast<binaryfunc>(__ixor__),
            .nb_inplace_or = reinterpret_cast<binaryfunc>(__ior__),
            .nb_floor_divide = reinterpret_cast<binaryfunc>(__floordiv__),
            .nb_true_divide = reinterpret_cast<binaryfunc>(__truediv__),
            .nb_inplace_floor_divide = reinterpret_cast<binaryfunc>(__ifloordiv__),
            .nb_inplace_true_divide = reinterpret_cast<binaryfunc>(__itruediv__),
            .nb_index = reinterpret_cast<unaryfunc>(__index__),
            .nb_matrix_multiply = reinterpret_cast<binaryfunc>(__matmul__),
            .nb_inplace_matrix_multiply = reinterpret_cast<binaryfunc>(__imatmul__),
        };

        inline static PyMappingMethods mapping = {
            .mp_length = reinterpret_cast<lenfunc>(__len__),
            .mp_subscript = reinterpret_cast<binaryfunc>(__getitem__),
            .mp_ass_subscript = reinterpret_cast<objobjargproc>(__setitem__)
        };

        inline static PySequenceMethods sequence = {
            .sq_length = reinterpret_cast<lenfunc>(__len__),
            .sq_concat = reinterpret_cast<binaryfunc>(__add__),
            .sq_repeat = reinterpret_cast<ssizeargfunc>(__repeat__),
            .sq_item = reinterpret_cast<ssizeargfunc>(__sq_getitem__),
            .sq_ass_item = reinterpret_cast<ssizeobjargproc>(__sq_setitem__),
            .sq_contains = reinterpret_cast<objobjproc>(__contains__),
            .sq_inplace_concat = reinterpret_cast<binaryfunc>(__iadd__),
            .sq_inplace_repeat = reinterpret_cast<ssizeargfunc>(__irepeat__)
        };

        inline static PyBufferProcs buffer = {
            .bf_getbuffer = reinterpret_cast<getbufferproc>(__buffer__),
            .bf_releasebuffer = reinterpret_cast<releasebufferproc>(__release_buffer__)
        };

    };

    Union(PyObject* p, borrowed_t t) : Object(p, t) {}
    Union(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = Union> requires (__initializer__<Self>::enable)
    Union(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<Union>::template enable<Args...>)
    Union(Args&&... args) : Object(
        implicit_ctor<Union>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Union>::template enable<Args...>)
    explicit Union(Args&&... args) : Object(
        explicit_ctor<Union>{},
        std::forward<Args>(args)...
    ) {}

    template <impl::inherits<Object> T>
    [[nodiscard]] bool is(T&& other) const {
        return (*this)->m_value.is(std::forward<T>(other));
    }

    [[nodiscard]] bool is(PyObject* other) const {
        return (*this)->m_value.is(other);
    }

};


template <typename... Ts>
struct __template__<Union<Ts...>>                           : Returns<Object> {
    static Object operator()() {
        PyObject* result = PyTuple_Pack(
            sizeof...(Ts),
            ptr(Type<Ts>())...
        );
        if (result == nullptr) {
            Exception::to_python();
        }
        return reinterpret_steal<Object>(result);
    }
};


/// TODO: __explicit_cast__ for union types?


/* Initializer list constructor is only enabled for `Optional<T>`, and not for any
other form of union, where such a call might be ambiguous. */
template <typename T> requires (__initializer__<T>::enable)
struct __initializer__<Optional<T>>                        : Returns<Optional<T>> {
    using Element = __initializer__<T>::type;
    static Optional<T> operator()(const std::initializer_list<Element>& init) {
        return impl::construct<Optional<T>>(T(init));
    }
};


/* Default constructor is enabled as long as at least one of the member types is
default constructible, in which case the first such type is initialized.  If NoneType
is present in the union, it is preferred over any other type. */
template <typename... Ts> requires (std::is_default_constructible_v<Ts> || ...)
struct __init__<Union<Ts...>>                               : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t none_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t none_idx<I, U, Us...> =
        std::same_as<std::remove_cvref_t<U>, NoneType> ?
            0 : none_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t idx<I, U, Us...> =
        std::is_default_constructible_v<U> ? 0 : idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()() {
        if constexpr (none_idx<0, Ts...> < sizeof...(Ts)) {
            return impl::construct<Union<Ts...>>(None);
        } else {
            return impl::construct<Union<Ts...>>(
                impl::unpack_type<idx<0, Ts...>>()
            );
        }
    }
};


/* Explicit constructor calls are only allowed for `Optional<T>`, and not for any
other form of union, where such a call might be ambiguous. */
template <typename T, typename... Args>
    requires (sizeof...(Args) > 0 && std::constructible_from<T, Args...>)
struct __init__<Optional<T>, Args...>                : Returns<Optional<T>> {
    static Optional<T> operator()(Args&&... args) {
        return impl::construct<Optional<T>>(T(std::forward<Args>(args)...));
    }
};


/* Universal conversion from any type that is convertible to one or more types within
the union.  Prefers exact matches (and therefore copy/move semantics) over secondary
conversions, and always converts to the first matching type within the union */
template <typename From, typename... Ts>
    requires (!impl::py_union<From> && (std::convertible_to<From, Ts> || ...))
struct __cast__<From, Union<Ts...>>                            : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cvref_t<From>, U> ? 0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<From, U> ? 0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
            return impl::construct<Union<Ts...>>(
                impl::unpack_type<match_idx<0, Ts...>>(std::forward<From>(from))
            );
        } else {
            return impl::construct<Union<Ts...>>(
                impl::unpack_type<convert_idx<0, Ts...>>(std::forward<From>(from))
            );
        }
    }
};


/* Universal conversion from a union to any type for which each element of the union is
convertible.  This covers conversions to `std::variant` provided all types are
accounted for, as well as to `std::optional` and pointer types whereby `NoneType` is
convertible to `std::nullopt` and `nullptr`, respectively. */
template <impl::py_union From, typename To>
    requires (!impl::py_union<To> && impl::UnionToType<From>::template convert<To>)
struct __cast__<From, To>                                   : Returns<To> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static To get(From from) {
            return reinterpret_cast<impl::unpack_type<I, Types...>&>(
                std::forward<From>(from)->m_value
            );
        }
    };

    template <size_t I>
    static To get(From from) {
        using F = std::remove_cvref_t<From>;
        if (I == from->m_index) {
            return context<F>::template get<I>(std::forward<From>(from));
        } else {
            return get<I + 1>(std::forward<From>(from));
        }
    }

    static To operator()(From from) {
        return get<0>(std::forward<From>(from));
    }
};


template <impl::is_variant T> requires (impl::VariantToUnion<T>::enable)
struct __cast__<T> : Returns<typename impl::VariantToUnion<T>::type> {};
template <impl::is_optional T> requires (impl::has_python<impl::optional_type<T>>)
struct __cast__<T> : Returns<
    Optional<impl::python_type<std::remove_cv_t<impl::optional_type<T>>>>
> {};
template <impl::has_python T> requires (impl::python<T> || (
    impl::has_cpp<impl::python_type<T>> && std::same_as<
        std::remove_cv_t<T>,
        impl::cpp_type<impl::python_type<std::remove_cv_t<T>>>
    >
))
struct __cast__<T*> : Returns<
    Optional<impl::python_type<std::remove_cv_t<T>>>
> {};
template <impl::is_shared_ptr T> requires (impl::has_python<impl::shared_ptr_type<T>>)
struct __cast__<T> : Returns<
    Optional<impl::python_type<std::remove_cv_t<impl::shared_ptr_type<T>>>>
> {};
template <impl::is_unique_ptr T> requires (impl::has_python<impl::unique_ptr_type<T>>)
struct __cast__<T> : Returns<
    Optional<impl::python_type<std::remove_cv_t<impl::unique_ptr_type<T>>>>
> {};


template <impl::is_variant From, std::derived_from<Object> To>
    requires (impl::VariantToUnion<From>::template convert<To>)
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(From value) {
        return std::visit(
            []<typename T>(T&& value) -> To {
                return std::forward<T>(value);
            },
            std::forward<From>(value)
        );
    }
};


template <impl::is_variant From, typename... Ts>
    requires (impl::VariantToUnion<From>::template convert<Ts...>)
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    static Union<Ts...> operator()(From value) {
        return std::visit(
            []<typename T>(T&& value) -> Union<Ts...> {
                return impl::construct<Union<Ts...>>(
                    impl::python_type<T>(std::forward<T>(value))
                );
            },
            std::forward<From>(value)
        );
    }
};


template <impl::is_optional From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<impl::optional_type<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    using T = impl::optional_type<From>;

    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cv_t<T>, U> ? 0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<T, U> ? 0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from.has_value()) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<match_idx<0, Ts...>>(
                        std::forward<From>(from).value()
                    )
                );
            } else {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<convert_idx<0, Ts...>>(
                        std::forward<From>(from).value()
                    )
                );
            }
        } else {
            return impl::construct<Union<Ts...>>(None);
        }
    }
};


template <impl::is_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<impl::ptr_type<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cv_t<impl::ptr_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<impl::ptr_type<From>, U> ?
            0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<match_idx<0, Ts...>>(*from)
                );
            } else {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<convert_idx<0, Ts...>>(*from)
                );
            }
        } else {
            return impl::construct<Union<Ts...>>(None);
        }
    }
};


template <impl::is_shared_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<impl::shared_ptr_type<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cv_t<impl::shared_ptr_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<impl::shared_ptr_type<From>, U> ?
            0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<match_idx<0, Ts...>>(*from)
                );
            } else {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<convert_idx<0, Ts...>>(*from)
                );
            }
        } else {
            return impl::construct<Union<Ts...>>(None);
        }
    }
};


template <impl::is_unique_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<impl::unique_ptr_type<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cv_t<impl::unique_ptr_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<impl::unique_ptr_type<From>, U> ?
            0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<match_idx<0, Ts...>>(*from)
                );
            } else {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<convert_idx<0, Ts...>>(*from)
                );
            }
        } else {
            return impl::construct<Union<Ts...>>(None);
        }
    }
};


/// NOTE: all other operations are only enabled if one or more members of the union
/// support them, and will return a new union type holding all of the valid results,
/// or a standard type if the resulting union would be a singleton.


template <impl::py_union Derived, impl::py_union Base>
    requires (impl::UnionIsInstance<Derived, Base>::enable)
struct __isinstance__<Derived, Base>                        : Returns<bool> {
    static constexpr bool operator()(Derived obj) {
        /// TODO: if Derived is a member of the union or a superclass of a member,
        /// then this can devolve to a simple index check.  Otherwise, it should
        /// extract the underlying object and recur.
    }
    static constexpr bool operator()(Derived obj, Base base) {
        /// TODO: this should only be enabled if one or more members supports it with
        /// the given base type.
    }
};


/// TODO: does this conflict with Object<>?


template <impl::py_union Derived, typename Base>
    requires (impl::UnionIsInstance<Derived, Base>::enable)
struct __isinstance__<Derived, Base>                        : Returns<bool> {
    static constexpr bool operator()(Derived obj) {
        /// TODO: returns true if any of the types match
    }
    static constexpr bool operator()(Derived obj, Base base) {
        /// TODO: only enabled if one or more members supports it
    }
};


template <typename Derived, impl::py_union Base>
    requires (impl::UnionIsInstance<Derived, Base>::enable)
struct __isinstance__<Derived, Base>                        : Returns<bool> {
    static constexpr bool operator()(Derived obj) {
        /// TODO: returns true if any of the types match
    }
    static constexpr bool operator()(Derived obj, Base base) {
        /// TODO: only enabled if one or more members supports it
    }
};


/// TODO: isinstance() and issubclass() can be optimized in some cases to just call
/// std::holds_alternative<T>() on the underlying object, rather than needing to
/// invoke Python.  This can be done if the type that is checked against is a
/// supertype of any of the members of the union, in which case the type check can be
/// reduced to just a simple comparison against the index.


template <impl::py_union Self, StaticStr Name>
    requires (impl::UnionGetAttr<Self, Name>::enable)
struct __getattr__<Self, Name> : Returns<typename impl::UnionGetAttr<Self, Name>::type> {
    using type = impl::UnionGetAttr<Self, Name>::type;

    template <typename>
    struct call {};
    template <typename... Types>
    struct call<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static type exec(Self self) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__getattr__<T, Name>::enable) {
                return getattr<Name>(
                    reinterpret_cast<T>(std::forward<Self>(self)->m_value)
                );
            } else {
                throw AttributeError(
                    "'" + impl::demangle(typeid(T).name()) + "' object has no "
                    "attribute '" + Name + "'"
                );
            }
        }
    };

    template <size_t I>
    static type exec(Self self) {
        if (I == self->m_index) {
            return call<std::remove_cvref_t<Self>>::template exec<I>(
                std::forward<Self>(self)
            );
        } else {
            if constexpr ((I + 1) < impl::UnionTraits<Self>::n) {
                return exec<I + 1>(std::forward<Self>(self));
            } else {
                throw AttributeError(
                    "'" + impl::demangle(typeid(Self).name()) + "' object has no "
                    "attribute '" + Name + "'"
                );
            }
        }
    }

    static type operator()(Self self) {
        return exec<0>(std::forward<Self>(self));
    }
};


template <impl::py_union Self, StaticStr Name, typename Value>
    requires (impl::UnionSetAttr<Self, Name, Value>::enable)
struct __setattr__<Self, Name, Value> : Returns<void> {
    template <typename>
    struct call {};
    template <typename... Types>
    struct call<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static void exec(Self self, Value value) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__setattr__<T, Name, Value>::enable) {
                setattr<Name>(
                    reinterpret_cast<T>(std::forward<Self>(self)->m_value),
                    std::forward<Value>(value)
                );
            } else {
                throw AttributeError(
                    "cannot set attribute '" + Name + "' on object of type '" +
                    impl::demangle(typeid(T).name()) + "'"
                );
            }
        }
    };

    template <size_t I>
    static void exec(Self self, Value value) {
        if (I == self->m_index) {
            call<std::remove_cvref_t<Self>>::template exec<I>(
                std::forward<Self>(self),
                std::forward<Value>(value)
            );
        } else {
            if constexpr ((I + 1) < impl::UnionTraits<Self>::n) {
                exec<I + 1>(
                    std::forward<Self>(self),
                    std::forward<Value>(value)
                );
            } else {
                throw AttributeError(
                    "cannot set attribute '" + Name + "' on object of type '" +
                    impl::demangle(typeid(Self).name()) + "'"
                );
            }
        }
    }

    static void operator()(Self self, Value value) {
        exec<0>(std::forward<Self>(self), std::forward<Value>(value));
    }
};


template <impl::py_union Self, StaticStr Name>
    requires (impl::UnionDelAttr<Self, Name>::enable)
struct __delattr__<Self, Name> : Returns<void> {
    template <typename>
    struct call {};
    template <typename... Types>
    struct call<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static void exec(Self self) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__delattr__<T, Name>::enable) {
                delattr<Name>(
                    reinterpret_cast<T>(std::forward<Self>(self)->m_value)
                );
            } else {
                throw AttributeError(
                    "cannot delete attribute '" + Name + "' on object of type '" +
                    impl::demangle(typeid(T).name()) + "'"
                );
            }
        }
    };

    template <size_t I>
    static void exec(Self self) {
        if (I == self->m_index) {
            call<std::remove_cvref_t<Self>>::template exec<I>(
                std::forward<Self>(self)
            );
        } else {
            if constexpr ((I + 1) < impl::UnionTraits<Self>::n) {
                exec<I + 1>(std::forward<Self>(self));
            } else {
                throw AttributeError(
                    "cannot delete attribute '" + Name + "' on object of type '" +
                    impl::demangle(typeid(Self).name()) + "'"
                );
            }
        }
    }

    static void operator()(Self self) {
        exec<0>(std::forward<Self>(self));
    }
};


template <impl::py_union Self>
struct __repr__<Self> : Returns<std::string> {
    template <typename>
    struct call {};
    template <typename... Types>
    struct call<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static std::string exec(Self self) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__repr__<T>::enable) {
                return repr(reinterpret_cast<T>(std::forward<Self>(self)->m_value));
            } else {
                return repr(std::forward<Self>(self)->m_value);
            }
        }
    };

    template <size_t I>
    static std::string exec(Self self) {
        if (I == self->m_index) {
            return call<std::remove_cvref_t<Self>>::template exec<I>(
                std::forward<Self>(self)
            );
        }
        if constexpr ((I + 1) < impl::UnionTraits<Self>::n) {
            return exec<I + 1>(std::forward<Self>(self));
        } else {
            throw TypeError(
                "cannot represent object of type '" +
                impl::demangle(typeid(Self).name()) + "'"
            );
        }
    }

    static std::string operator()(Self self) {
        return exec<0>(std::forward<Self>(self));
    }
};


template <impl::py_union Self, typename... Args>
    requires (impl::UnionCall<Self, Args...>::enable)
struct __call__<Self, Args...> : Returns<typename impl::UnionCall<Self, Args...>::type> {
    using type = impl::UnionCall<Self, Args...>::type;
    static type operator()(Self self, Args&&... args) {
        return impl::UnionCall<Self, Args...>{}(
            []<typename S, typename... A>(S&& self, A&&... args) {
                return std::forward<S>(self)(std::forward<A>(args)...);
            },
            []<typename S, typename... A>(S&& self, A&&... args) {
                throw TypeError(
                    "cannot call object of type '" +
                    impl::demangle(typeid(S).name()) + "' with the given "
                    "arguments"
                );
            },
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }
};


template <impl::py_union Self, typename... Key>
    requires (impl::UnionGetItem<Self, Key...>::enable)
struct __getitem__<Self, Key...> : Returns<typename impl::UnionGetItem<Self, Key...>::type> {
    using type = impl::UnionGetItem<Self, Key...>::type;
    static type operator()(Self self, Key... key) {
        return impl::UnionGetItem<Self, Key...>{}(
            []<typename S, typename... K>(S&& self, K&&... key) {
                return std::forward<S>(self)[std::forward<K>(key)...];
            },
            []<typename S, typename... K>(S&& self, K&&... key) {
                throw KeyError(
                    "cannot get item with the given key(s) from object of type '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            },
            std::forward<Self>(self),
            std::forward<Key>(key)...
        );
    }
};


template <impl::py_union Self, typename Value, typename... Key>
    requires (impl::UnionSetItem<Self, Value, Key...>::enable)
struct __setitem__<Self, Value, Key...> : Returns<void> {
    static void operator()(Self self, Value value, Key... key) {
        return impl::UnionSetItem<Self, Value, Key...>{}(
            []<typename S, typename V, typename... K>(S&& self, V&& value, K&&... key) {
                std::forward<S>(self)[std::forward<K>(key)...] = std::forward<V>(value);
            },
            []<typename S, typename V, typename... K>(S&& self, V&& value, K&&... key) {
                throw KeyError(
                    "cannot set item with the given key(s) on object of type '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            },
            std::forward<Self>(self),
            std::forward<Value>(value),
            std::forward<Key>(key)...
        );
    }
};


template <impl::py_union Self, typename... Key>
    requires (impl::UnionDelItem<Self, Key...>::enable)
struct __delitem__<Self, Key...> : Returns<void> {
    static void operator()(Self self, Key... key) {
        return impl::UnionDelItem<Self, Key...>{}(
            []<typename S, typename... K>(S&& self, K&&... key) {
                del(std::forward<S>(self)[std::forward<K>(key)...]);
            },
            []<typename S, typename... K>(S&& self, K&&... key) {
                throw KeyError(
                    "cannot delete item with the given key(s) from object of type '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            },
            std::forward<Self>(self),
            std::forward<Key>(key)...
        );
    }
};


template <impl::py_union Self>
    requires (
        impl::UnionHash<Self>::enable &&
        std::convertible_to<typename impl::UnionHash<Self>::type, size_t>
    )
struct __hash__<Self> : Returns<size_t> {
    static size_t operator()(Self self) {
        return impl::UnionHash<Self>{}(
            std::forward<Self>(self),
            []<typename S>(S&& self) -> size_t {
                return hash(std::forward<S>(self));
            },
            []<typename S>(S&& self) -> size_t {
                throw TypeError(
                    "unhashable type: '" + impl::demangle(typeid(S).name()) + "'"
                );
            }
        );
    }
};


template <impl::py_union Self>
    requires (
        impl::UnionLen<Self>::enable &&
        std::convertible_to<typename impl::UnionLen<Self>::type, size_t>
    )
struct __len__<Self> : Returns<size_t> {
    static size_t operator()(Self self) {
        return impl::UnionLen<Self>{}(
            []<typename S>(S&& self) -> size_t {
                return len(std::forward<S>(self));
            },
            []<typename S>(S&& self) -> size_t {
                throw TypeError(
                    "object of type '" + impl::demangle(typeid(S).name()) +
                    "' has no len()"
                );
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self, typename Key>
    requires (
        impl::UnionContains<Self, Key>::enable &&
        std::convertible_to<typename impl::UnionContains<Self, Key>::type, bool>
    )
struct __contains__<Self, Key> : Returns<bool> {
    static bool operator()(Self self, Key key) {
        return impl::UnionContains<Self, Key>{}(
            []<typename S, typename K>(S&& self, K&& key) -> bool {
                return in(std::forward<K>(key), std::forward<S>(self));
            },
            []<typename S, typename K>(S&& self, K&& key) -> bool {
                throw TypeError(
                    "argument of type '" + impl::demangle(typeid(K).name()) +
                    "' does not support .contains() checks"
                );
            },
            std::forward<Self>(self),
            std::forward<Key>(key)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionIter<Self>::enable)
struct __iter__<Self> : Returns<typename impl::UnionIter<Self>::type> {
    /// NOTE: default implementation delegates to Python, which reinterprets each value
    /// as the given type(s).  That handles all cases appropriately, with a small
    /// performance hit for the extra interpreter overhead that isn't present for
    /// static types.
};


template <impl::py_union Self> requires (impl::UnionReversed<Self>::enable)
struct __reversed__<Self> : Returns<typename impl::UnionReversed<Self>::type> {
    /// NOTE: same as `__iter__`, but returns a reverse iterator instead.
};


template <impl::py_union Self> requires (impl::UnionAbs<Self>::enable)
struct __abs__<Self> : Returns<typename impl::UnionAbs<Self>::type> {
    using type = impl::UnionAbs<Self>::type;
    static type operator()(Self self) {
        return impl::UnionAbs<Self>{}(
            []<typename S>(S&& self) {
                return abs(std::forward<S>(self));
            },
            []<typename S>(S&& self) {
                throw TypeError(
                    "bad operand type for abs(): '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionInvert<Self>::enable)
struct __invert__<Self> : Returns<typename impl::UnionInvert<Self>::type> {
    using type = impl::UnionInvert<Self>::type;
    static type operator()(Self self) {
        return impl::UnionInvert<Self>{}(
            []<typename S>(S&& self) {
                return ~std::forward<S>(self);
            },
            []<typename S>(S&& self) {
                throw TypeError(
                    "bad operand type for unary ~: '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionPos<Self>::enable)
struct __pos__<Self> : Returns<typename impl::UnionPos<Self>::type> {
    using type = impl::UnionPos<Self>::type;
    static type operator()(Self self) {
        return impl::UnionPos<Self>{}(
            []<typename S>(S&& self) {
                return +std::forward<S>(self);
            },
            []<typename S>(S&& self) {
                throw TypeError(
                    "bad operand type for unary +: '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionNeg<Self>::enable)
struct __neg__<Self> : Returns<typename impl::UnionNeg<Self>::type> {
    using type = impl::UnionNeg<Self>::type;
    static type operator()(Self self) {
        return impl::UnionNeg<Self>{}(
            []<typename S>(S&& self) {
                return -std::forward<S>(self);
            },
            []<typename S>(S&& self) {
                throw TypeError(
                    "bad operand type for unary -: '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionIncrement<Self>::enable)
struct __increment__<Self> : Returns<typename impl::UnionIncrement<Self>::type> {
    using type = impl::UnionIncrement<Self>::type;
    static type operator()(Self self) {
        return impl::UnionIncrement<Self>{}(
            []<typename S>(S&& self) {
                return ++std::forward<S>(self);
            },
            []<typename S>(S&& self) {
                throw TypeError(
                    "'" + impl::demangle(typeid(S).name()) + "' object cannot be "
                    "incremented"
                );
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionDecrement<Self>::enable)
struct __decrement__<Self> : Returns<typename impl::UnionDecrement<Self>::type> {
    using type = impl::UnionDecrement<Self>::type;
    static type operator()(Self self) {
        return impl::UnionDecrement<Self>{}(
            []<typename S>(S&& self) {
                return --std::forward<S>(self);
            },
            []<typename S>(S&& self) {
                throw TypeError(
                    "'" + impl::demangle(typeid(S).name()) + "' object cannot be "
                    "decremented"
                );
            },
            std::forward<Self>(self)
        );
    }
};


template <typename L, typename R> requires (impl::UnionLess<L, R>::enable)
struct __lt__<L, R> : Returns<typename impl::UnionLess<L, R>::type> {
    using type = impl::UnionLess<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionLess<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) < std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for <: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionLessEqual<L, R>::enable)
struct __le__<L, R> : Returns<typename impl::UnionLessEqual<L, R>::type> {
    using type = impl::UnionLessEqual<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionLessEqual<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) <= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for <=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionEqual<L, R>::enable)
struct __eq__<L, R> : Returns<typename impl::UnionEqual<L, R>::type> {
    using type = impl::UnionEqual<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionEqual<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) == std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for ==: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionNotEqual<L, R>::enable)
struct __ne__<L, R> : Returns<typename impl::UnionNotEqual<L, R>::type> {
    using type = impl::UnionNotEqual<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionNotEqual<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) != std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for !=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionGreaterEqual<L, R>::enable)
struct __ge__<L, R> : Returns<typename impl::UnionGreaterEqual<L, R>::type> {
    using type = impl::UnionGreaterEqual<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionGreaterEqual<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) >= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for >=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionGreater<L, R>::enable)
struct __gt__<L, R> : Returns<typename impl::UnionGreater<L, R>::type> {
    using type = impl::UnionGreater<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionGreater<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) > std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for >: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionAdd<L, R>::enable)
struct __add__<L, R> : Returns<typename impl::UnionAdd<L, R>::type> {
    using type = impl::UnionAdd<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionAdd<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) + std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for +: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionSub<L, R>::enable)
struct __sub__<L, R> : Returns<typename impl::UnionSub<L, R>::type> {
    using type = impl::UnionSub<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionSub<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) - std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for -: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionMul<L, R>::enable)
struct __mul__<L, R> : Returns<typename impl::UnionMul<L, R>::type> {
    using type = impl::UnionMul<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionMul<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) * std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for *: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename Base, typename Exp, typename Mod>
    requires (impl::UnionPow<Base, Exp, Mod>::enable)
struct __pow__<Base, Exp, Mod> : Returns<typename impl::UnionPow<Base, Exp, Mod>::type> {
    using type = impl::UnionPow<Base, Exp, Mod>::type;
    static type operator()(Base base, Exp exp) {
        return impl::UnionPow<Base, Exp, Mod>{}(
            []<typename B, typename E>(B&& base, E&& exp) {
                return pow(std::forward<B>(base), std::forward<E>(exp));
            },
            []<typename B, typename E>(B&& base, E&& exp) {
                throw TypeError(
                    "unsupported operand types for pow(): '" +
                    impl::demangle(typeid(B).name()) + "' and '" +
                    impl::demangle(typeid(E).name()) + "'"
                );
            },
            std::forward<Base>(base),
            std::forward<Exp>(exp)
        );
    }
    static type operator()(Base base, Exp exp, Mod mod) {
        return impl::UnionPow<Base, Exp, Mod>{}(
            []<typename B, typename E, typename M>(B&& base, E&& exp, M&& mod) {
                return pow(
                    std::forward<B>(base),
                    std::forward<E>(exp),
                    std::forward<M>(mod)
                );
            },
            []<typename B, typename E, typename M>(B&& base, E&& exp, M&& mod) {
                throw TypeError(
                    "unsupported operand types for pow(): '" +
                    impl::demangle(typeid(B).name()) + "' and '" +
                    impl::demangle(typeid(E).name()) + "' and '" +
                    impl::demangle(typeid(M).name()) + "'"
                );
            },
            std::forward<Base>(base),
            std::forward<Exp>(exp),
            std::forward<Mod>(mod)
        );
    }
};


template <typename L, typename R> requires (impl::UnionTrueDiv<L, R>::enable)
struct __truediv__<L, R> : Returns<typename impl::UnionTrueDiv<L, R>::type> {
    using type = impl::UnionTrueDiv<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionTrueDiv<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) / std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for /: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionFloorDiv<L, R>::enable)
struct __floordiv__<L, R> : Returns<typename impl::UnionFloorDiv<L, R>::type> {
    using type = impl::UnionFloorDiv<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionFloorDiv<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return floordiv(std::forward<L2>(lhs), std::forward<R2>(rhs));
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for //: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionMod<L, R>::enable)
struct __mod__<L, R> : Returns<typename impl::UnionMod<L, R>::type> {
    using type = impl::UnionMod<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionMod<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) % std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for %: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionLShift<L, R>::enable)
struct __lshift__<L, R> : Returns<typename impl::UnionLShift<L, R>::type> {
    using type = impl::UnionLShift<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionLShift<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) << std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for <<: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionRShift<L, R>::enable)
struct __rshift__<L, R> : Returns<typename impl::UnionRShift<L, R>::type> {
    using type = impl::UnionRShift<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionRShift<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) >> std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for >>: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionAnd<L, R>::enable)
struct __and__<L, R> : Returns<typename impl::UnionAnd<L, R>::type> {
    using type = impl::UnionAnd<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionAnd<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) & std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for &: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionXor<L, R>::enable)
struct __xor__<L, R> : Returns<typename impl::UnionXor<L, R>::type> {
    using type = impl::UnionXor<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionXor<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) ^ std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for ^: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionOr<L, R>::enable)
struct __or__<L, R> : Returns<typename impl::UnionOr<L, R>::type> {
    using type = impl::UnionOr<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionOr<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) | std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for |: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceAdd<L, R>::enable)
struct __iadd__<L, R> : Returns<typename impl::UnionInplaceAdd<L, R>::type> {
    using type = impl::UnionInplaceAdd<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceAdd<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) += std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for +=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceSub<L, R>::enable)
struct __isub__<L, R> : Returns<typename impl::UnionInplaceSub<L, R>::type> {
    using type = impl::UnionInplaceSub<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceSub<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) -= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for -=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceMul<L, R>::enable)
struct __imul__<L, R> : Returns<typename impl::UnionInplaceMul<L, R>::type> {
    using type = impl::UnionInplaceMul<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceMul<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) *= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for *=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename Base, typename Exp, typename Mod>
    requires (impl::UnionInplacePow<Base, Exp, Mod>::enable)
struct __ipow__<Base, Exp, Mod> : Returns<typename impl::UnionInplacePow<Base, Exp, Mod>::type> {
    using type = impl::UnionInplacePow<Base, Exp, Mod>::type;
    static type operator()(Base base, Exp exp) {
        return impl::UnionInplacePow<Base, Exp, Mod>{}(
            []<typename B, typename E>(B&& base, E&& exp) {
                return ipow(std::forward<B>(base), std::forward<E>(exp));
            },
            []<typename B, typename E>(B&& base, E&& exp) {
                throw TypeError(
                    "unsupported operand types for **=: '" +
                    impl::demangle(typeid(B).name()) + "' and '" +
                    impl::demangle(typeid(E).name()) + "'"
                );
            },
            std::forward<Base>(base),
            std::forward<Exp>(exp)
        );
    }
    static type operator()(Base base, Exp exp, Mod mod) {
        return impl::UnionInplacePow<Base, Exp, Mod>{}(
            []<typename B, typename E, typename M>(B&& base, E&& exp, M&& mod) {
                return ipow(
                    std::forward<B>(base),
                    std::forward<E>(exp),
                    std::forward<M>(mod)
                );
            },
            []<typename B, typename E, typename M>(B&& base, E&& exp, M&& mod) {
                throw TypeError(
                    "unsupported operand types for **=: '" +
                    impl::demangle(typeid(B).name()) + "' and '" +
                    impl::demangle(typeid(E).name()) + "' and '" +
                    impl::demangle(typeid(M).name()) + "'"
                );
            },
            std::forward<Base>(base),
            std::forward<Exp>(exp),
            std::forward<Mod>(mod)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceTrueDiv<L, R>::enable)
struct __itruediv__<L, R> : Returns<typename impl::UnionInplaceTrueDiv<L, R>::type> {
    using type = impl::UnionInplaceTrueDiv<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceTrueDiv<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) /= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for /=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceFloorDiv<L, R>::enable)
struct __ifloordiv__<L, R> : Returns<typename impl::UnionInplaceFloorDiv<L, R>::type> {
    using type = impl::UnionInplaceFloorDiv<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceFloorDiv<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return ifloordiv(std::forward<L2>(lhs), std::forward<R2>(rhs));
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for //=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceMod<L, R>::enable)
struct __imod__<L, R> : Returns<typename impl::UnionInplaceMod<L, R>::type> {
    using type = impl::UnionInplaceMod<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceMod<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) %= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for %=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceLShift<L, R>::enable)
struct __ilshift__<L, R> : Returns<typename impl::UnionInplaceLShift<L, R>::type> {
    using type = impl::UnionInplaceLShift<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceLShift<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) <<= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for <<=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceRShift<L, R>::enable)
struct __irshift__<L, R> : Returns<typename impl::UnionInplaceRShift<L, R>::type> {
    using type = impl::UnionInplaceRShift<L, R>::type;
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceRShift<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) >>= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for >>=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceAnd<L, R>::enable)
struct __iand__<L, R> : Returns<typename impl::UnionInplaceAnd<L, R>::type> {
    using type = impl::UnionInplaceAnd<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceAnd<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) &= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for &: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceXor<L, R>::enable)
struct __ixor__<L, R> : Returns<typename impl::UnionInplaceXor<L, R>::type> {
    using type = impl::UnionInplaceXor<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceXor<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) ^= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for ^: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceOr<L, R>::enable)
struct __ior__<L, R> : Returns<typename impl::UnionInplaceOr<L, R>::type> {
    using type = impl::UnionInplaceOr<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionInplaceOr<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                return std::forward<L2>(lhs) |= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) {
                throw TypeError(
                    "unsupported operand types for |: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


}


namespace std {

    template <typename... Types>
    struct variant_size<py::Union<Types...>> :
        std::integral_constant<size_t, sizeof...(Types)>
    {};

    template <size_t I, typename... Types> requires (I < sizeof...(Types))
    struct variant_alternative<I, py::Union<Types...>> {
        using type = py::impl::unpack_type<I, Types...>;
    };

    template <typename T, py::impl::py_union Self>
        requires (py::impl::UnionTraits<Self>::template contains<T>)
    bool holds_alternative(Self&& self) {
        return py::impl::UnionTraits<Self>::template index_of<T> == self->m_index;
    }

    template <typename T, py::impl::py_union Self>
        requires (py::impl::UnionTraits<Self>::template contains<T>)
    auto get(Self&& self) -> py::impl::qualify_lvalue<T, Self> {
        using Return = py::impl::qualify_lvalue<T, Self>;
        constexpr size_t index = py::impl::UnionTraits<Self>::template index_of<T>;
        if (index == self->m_index) {
            return reinterpret_cast<Return>(std::forward<Self>(self)->m_value);
        } else {
            throw py::TypeError(
                "bad union access: '" + py::impl::demangle(typeid(T).name()) +
                "' is not the active type"
            );
        }
    }

    template <size_t I, py::impl::py_union Self>
        requires (
            (I < py::impl::UnionTraits<Self>::n) &&
            py::impl::UnionTraits<Self>::template contains<
                typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>
            >
        )
    auto get(Self&& self) -> py::impl::qualify_lvalue<
        typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
        Self
    > {
        using Return = py::impl::qualify_lvalue<
            typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
            Self
        >;
        if (I == self->m_index) {
            return reinterpret_cast<Return>(self->m_value);
        } else {
            throw py::TypeError(
                "bad union access: '" +
                py::impl::demangle(typeid(std::remove_cvref_t<Return>).name()) +
                "' is not the active type"
            );
        }
    }

    template <typename T, py::impl::py_union Self>
        requires (py::impl::UnionTraits<Self>::template contains<T>)
    auto get_if(Self&& self) -> py::impl::qualify_pointer<T, Self> {
        return py::impl::UnionTraits<Self>::template index_of<T> == self->m_index ?
            reinterpret_cast<py::impl::qualify_pointer<T, Self>>(&self->m_value) :
            nullptr;
    }

    template <size_t I, py::impl::py_union Self>
        requires (
            (I < py::impl::UnionTraits<Self>::n) &&
            py::impl::UnionTraits<Self>::template contains<
                typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>
            >
        )
    auto get_if(Self&& self) -> py::impl::qualify_pointer<
        typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
        Self
    > {
        return I == self->m_index ?
            reinterpret_cast<py::impl::qualify_pointer<
                typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
                Self
            >>(&self->m_value) :
            nullptr;
    }

    /// TODO: std::visit?

}


#endif
