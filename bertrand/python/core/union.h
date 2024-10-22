#ifndef BERTRAND_CORE_UNION_H
#define BERTRAND_CORE_UNION_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"


namespace py {


/// TODO: when a Union is returned to Python, I should unpack it and only return the
/// active member, not the actual union itself.  That effectively means that the
/// Union object will never need to be used in Python itself, and exists mostly for the
/// benefit of C++, in order to allow it to conform to Python's dynamic typing.


template <typename... Types>
    requires (
        sizeof...(Types) > 0 &&
        (!std::is_reference_v<Types> && ...) &&
        (std::derived_from<Types, Object> && ...) &&
        impl::types_are_unique<Types...> &&
        !(std::is_const_v<Types> || ...) &&
        !(std::is_volatile_v<Types> || ...)
    )
struct Union;

template <std::derived_from<Object> T = Object>
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

    /* Variants are treated like unions. */
    template <is_variant T>
    struct UnionTraits<T> {
    private:

        template <typename>
        struct _pack;
        template <typename... Types>
        struct _pack<std::variant<Types...>> {
            using type = Pack<Types...>;
        };

    public:
        using type = T;
        using pack = _pack<std::remove_cvref_t<T>>::type;
    };

    /* Optionals are treated as variants with std::nullopt_t. */
    template <is_optional T>
    struct UnionTraits<T> {
        using type = T;
        using pack = Pack<impl::optional_type<T>, std::nullopt_t>;
    };

    /* Converting a pack to a union involves converting all C++ types to their Python
    equivalents, then deduplicating the results.  If the final pack contains only a
    single type, then that type is returned directly, rather than instantiating a new
    Union. */
    template <typename>
    struct to_union;
    template <typename... Matches>
    struct to_union<Pack<Matches...>> {
        template <typename pack>
        struct convert {
            template <typename>
            struct helper;
            template <typename... Unique>
            struct helper<Pack<Unique...>> {
                using type = Union<std::remove_cvref_t<impl::python_type<Unique>>...>;
            };
            using type = helper<typename Pack<Matches...>::deduplicate>::type;
        };
        template <typename Match>
        struct convert<Pack<Match>> { using type = Match; };
        using type = convert<typename Pack<Matches...>::deduplicate>::type;
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
            /// TODO: convert the output types to Python if returning a Union?
            using type = to_union<returns>::type;

            /// TODO: also, how to represent void types here?
            /// -> If a void type is present in the output, then it should be the
            /// only type present, and the return type will deduce to void.

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
                        return success(std::forward<Actual>(args)...);
                    } else {
                        return failure(std::forward<Actual>(args)...);
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
                            unpack_arg<Actual>(std::forward<Actual>(args)...)...
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
                            unpack_arg<I>(std::forward<Args>(args)...)
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
                            std::forward<Args>(args)...
                        );
                    }
                }
            };
        };
    };

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
    template <typename Base, typename Exp, typename Mod>
    using UnionPow = union_operator<__pow__>::template op<Base, Mod, Exp>;





    /// TODO: converting std::variant to py::Union may need to account for duplicate
    /// types and cv qualifiers and convert everything accordingly.  Perhaps this can
    /// also demote variants to singular Python types if they all resolve to the
    /// same type
    template <typename T>
    struct VariantToUnion { static constexpr bool enable = false; };
    template <std::convertible_to<Object>... Ts>
    struct VariantToUnion<std::variant<Ts...>> {
        static constexpr bool enable = true;
        using type = Union<std::remove_cv_t<python_type<Ts>>...>;

        template <size_t I, typename... Us>
        static constexpr bool _convertible_to = true;
        template <size_t I, typename... Us> requires (I < sizeof...(Ts))
        static constexpr bool _convertible_to<I, Us...> =
            (std::convertible_to<impl::unpack_type<I, Ts...>, Us> || ...) &&
            _convertible_to<I + 1, Us...>;

        template <typename... Us>
        static constexpr bool convertible_to = _convertible_to<0, Us...>;
    };

    template <typename T>
    struct UnionToType { static constexpr bool enable = false; };
    template <typename... Types>
    struct UnionToType<Union<Types...>> {
        static constexpr bool enable = true;

        template <typename Out, typename... Ts>
        static constexpr bool _convertible_to = true;
        template <typename Out, typename T, typename... Ts>
        static constexpr bool _convertible_to<Out, T, Ts...> =
            std::convertible_to<T, Out> &&
            _convertible_to<Out, Ts...>;

        template <typename Out>
        static constexpr bool convertible_to = _convertible_to<Out, Types...>;
    };





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

    template <typename, typename... Args>
    struct UnionCall { static constexpr bool enable = false; };
    template <py_union Self, typename... Args>
    struct UnionCall<Self, Args...> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename... Types>
            requires (__call__<qualify_lvalue<Types, Self>, Args...>::enable || ...)
        struct traits<Union<Types...>> {
            template <typename result, typename... Ts>
            struct unary { using type = result; };
            template <typename... Matches, typename T, typename... Ts>
            struct unary<Pack<Matches...>, T, Ts...> {
                template <typename>
                struct conditional { using type = Pack<Matches...>; };
                template <typename T2>
                    requires (__call__<qualify_lvalue<T2, Self>, Args...>::enable)
                struct conditional<T2> {
                    using type = Pack<
                        Matches...,
                        typename __call__<qualify_lvalue<T2, Self>, Args...>::type
                    >;
                };
                using type = unary<typename conditional<T>::type, Ts...>::type;
            };
            static constexpr bool enable = true;
            using type = to_union<typename unary<Pack<>, Types...>::type>::type;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = traits<std::remove_cvref_t<Self>>::type;

        template <typename>
        struct call;
        template <typename... Types>
        struct call<Union<Types...>> {
            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                Self self,
                Args... args
            ) {
                using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                if constexpr (__call__<S, Args...>::enable) {
                    return success(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Args>(args)...
                    );
                } else {
                    return failure(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Args>(args)...
                    );
                }
            }
        };

        template <size_t I, typename OnSuccess, typename OnFailure>
        static type exec(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Args... args
        ) {
            if (I == self->m_index) {
                return call<std::remove_cvref_t<Self>>::template exec<I>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Args>(args)...
                );
            }
            if constexpr ((I + 1) < UnionTraits<Self>::n) {
                return exec<I + 1>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Args>(args)...
                );
            } else {
                return failure(std::forward<Self>(self), std::forward<Args>(args)...);
            }
        }

        template <typename OnSuccess, typename OnFailure>
        static type operator()(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Args... args
        ) {
            return exec<0>(
                std::forward<OnSuccess>(success),
                std::forward<OnFailure>(failure),
                std::forward<Self>(self),
                std::forward<Args>(args)...
            );
        }
    };

    template <typename, typename... Key>
    struct UnionGetItem { static constexpr bool enable = false; };
    template <py_union Self, typename... Key>
    struct UnionGetItem<Self, Key...> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename... Types>
            requires (__getitem__<qualify_lvalue<Types, Self>, Key...>::enable || ...)
        struct traits<Union<Types...>> {
            template <typename result, typename... Ts>
            struct unary { using type = result; };
            template <typename... Matches, typename T, typename... Ts>
            struct unary<Pack<Matches...>, T, Ts...> {
                template <typename>
                struct conditional { using type = Pack<Matches...>; };
                template <typename T2>
                    requires (__getitem__<qualify_lvalue<T2, Self>, Key...>::enable)
                struct conditional<T2> {
                    using type = Pack<
                        Matches...,
                        typename __getitem__<qualify_lvalue<T2, Self>, Key...>::type
                    >;
                };
                using type = unary<typename conditional<T>::type, Ts...>::type;
            };
            static constexpr bool enable = true;
            using type = to_union<typename unary<Pack<>, Types...>::type>::type;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = traits<std::remove_cvref_t<Self>>::type;

        template <typename>
        struct call;
        template <typename... Types>
        struct call<Union<Types...>> {
            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                Self self,
                Key... key
            ) {
                using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                if constexpr (__getitem__<S, Key...>::enable) {
                    return success(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Key>(key)...
                    );
                } else {
                    return failure(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Key>(key)...
                    );
                }
            }
        };

        template <size_t I, typename OnSuccess, typename OnFailure>
        static type exec(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Key... key
        ) {
            if (I == self->m_index) {
                return call<std::remove_cvref_t<Self>>::template exec<I>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Key>(key)...
                );
            }
            if constexpr ((I + 1) < UnionTraits<Self>::n) {
                return exec<I + 1>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Key>(key)...
                );
            } else {
                return failure(std::forward<Self>(self), std::forward<Key>(key)...);
            }
        }

        template <typename OnSuccess, typename OnFailure>
        static type operator()(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Key... key
        ) {
            return exec<0>(
                std::forward<OnSuccess>(success),
                std::forward<OnFailure>(failure),
                std::forward<Self>(self),
                std::forward<Key>(key)...
            );
        }
    };

    template <typename, typename, typename... Key>
    struct UnionSetItem { static constexpr bool enable = false; };
    template <py_union Self, typename Value, typename... Key>
    struct UnionSetItem<Self, Value, Key...> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__setitem__<qualify_lvalue<Types, Self>, Value, Key...>::enable || ...)
        static constexpr bool match<Union<Types...>> = true;
        static constexpr bool enable = match<std::remove_cvref_t<Self>>;
        using type = void;

        template <typename>
        struct call;
        template <typename... Types>
        struct call<Union<Types...>> {
            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                Self self,
                Value value,
                Key... key
            ) {
                using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                if constexpr (__setitem__<S, Value, Key...>::enable) {
                    return success(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Value>(value),
                        std::forward<Key>(key)...
                    );
                } else {
                    return failure(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Value>(value),
                        std::forward<Key>(key)...
                    );
                }
            }
        };

        template <size_t I, typename OnSuccess, typename OnFailure>
        static type exec(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Value value,
            Key... key
        ) {
            if (I == self->m_index) {
                return call<std::remove_cvref_t<Self>>::template exec<I>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Value>(value),
                    std::forward<Key>(key)...
                );
            }
            if constexpr ((I + 1) < UnionTraits<Self>::n) {
                return exec<I + 1>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Value>(value),
                    std::forward<Key>(key)...
                );
            } else {
                return failure(
                    std::forward<Self>(self),
                    std::forward<Value>(value),
                    std::forward<Key>(key)...
                );
            }
        }

        template <typename OnSuccess, typename OnFailure>
        static type operator()(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Value value,
            Key... key
        ) {
            return exec<0>(
                std::forward<OnSuccess>(success),
                std::forward<OnFailure>(failure),
                std::forward<Self>(self),
                std::forward<Value>(value),
                std::forward<Key>(key)...
            );
        }
    };

    template <typename, typename... Key>
    struct UnionDelItem { static constexpr bool enable = false; };
    template <py_union Self, typename... Key>
    struct UnionDelItem<Self, Key...> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__delitem__<qualify_lvalue<Types, Self>, Key...>::enable || ...)
        static constexpr bool match<Union<Types...>> = true;
        static constexpr bool enable = match<std::remove_cvref_t<Self>>;
        using type = void;

        template <typename>
        struct call;
        template <typename... Types>
        struct call<Union<Types...>> {
            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                Self self,
                Key... key
            ) {
                using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                if constexpr (__delitem__<S, Key...>::enable) {
                    return success(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Key>(key)...
                    );
                } else {
                    return failure(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Key>(key)...
                    );
                }
            }
        };

        template <size_t I, typename OnSuccess, typename OnFailure>
        static type exec(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Key... key
        ) {
            if (I == self->m_index) {
                return call<std::remove_cvref_t<Self>>::template exec<I>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Key>(key)...
                );
            }
            if constexpr ((I + 1) < UnionTraits<Self>::n) {
                return exec<I + 1>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Key>(key)...
                );
            } else {
                return failure(std::forward<Self>(self), std::forward<Key>(key)...);
            }
        }

        template <typename OnSuccess, typename OnFailure>
        static type operator()(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Key... key
        ) {
            return exec<0>(
                std::forward<OnSuccess>(success),
                std::forward<OnFailure>(failure),
                std::forward<Self>(self),
                std::forward<Key>(key)...
            );
        }
    };

    template <typename>
    struct UnionHash { static constexpr bool enable = false; };
    template <py_union Self>
    struct UnionHash<Self> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__hash__<qualify_lvalue<Types, Self>>::enable || ...)
        static constexpr bool match<Union<Types...>> = true;
        static constexpr bool enable = match<std::remove_cvref_t<Self>>;
        using type = size_t;

        template <typename>
        struct call;
        template <typename... Types>
        struct call<Union<Types...>> {
            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                Self self,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                if constexpr (__hash__<S>::enable) {
                    return success(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value)
                    );
                } else {
                    return failure(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value)
                    );
                }
            }
        };

        template <size_t I, typename OnSuccess, typename OnFailure>
        static type exec(
            Self self,
            OnSuccess&& success,
            OnFailure&& failure
        ) {
            if (I == self->m_index) {
                return call<std::remove_cvref_t<Self>>::template exec<I>(
                    std::forward<Self>(self),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
            if constexpr ((I + 1) < UnionTraits<Self>::n) {
                return exec<I + 1>(
                    std::forward<Self>(self),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            } else {
                return failure(std::forward<Self>(self));
            }
        }

        template <typename OnSuccess, typename OnFailure>
        static type operator()(
            Self self,
            OnSuccess&& success,
            OnFailure&& failure
        ) {
            return exec<0>(
                std::forward<Self>(self),
                std::forward<OnSuccess>(success),
                std::forward<OnFailure>(failure)
            );
        }
    };

    template <typename>
    struct UnionLen { static constexpr bool enable = false; };
    template <py_union Self>
    struct UnionLen<Self> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__len__<qualify_lvalue<Types, Self>>::enable || ...)
        static constexpr bool match<Union<Types...>> = true;
        static constexpr bool enable = match<std::remove_cvref_t<Self>>;
        using type = size_t;

        template <typename>
        struct call;
        template <typename... Types>
        struct call<Union<Types...>> {
            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                Self self
            ) {
                using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                if constexpr (__len__<S>::enable) {
                    return success(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value)
                    );
                } else {
                    return failure(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value)
                    );
                }
            }
        };

        template <size_t I, typename OnSuccess, typename OnFailure>
        static type exec(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self
        ) {
            if (I == self->m_index) {
                return call<std::remove_cvref_t<Self>>::template exec<I>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self)
                );
            }
            if constexpr ((I + 1) < UnionTraits<Self>::n) {
                return exec<I + 1>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self)
                );
            } else {
                return failure(std::forward<Self>(self));
            }
        }

        template <typename OnSuccess, typename OnFailure>
        static type operator()(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self
        ) {
            return exec<0>(
                std::forward<OnSuccess>(success),
                std::forward<OnFailure>(failure),
                std::forward<Self>(self)
            );
        }
    };

    template <typename, typename>
    struct UnionContains { static constexpr bool enable = false; };
    template <py_union Self, typename Key>
    struct UnionContains<Self, Key> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__contains__<qualify_lvalue<Types, Self>, Key>::enable || ...)
        static constexpr bool match<Union<Types...>> = true;
        static constexpr bool enable = match<std::remove_cvref_t<Self>>;
        using type = bool;

        template <typename>
        struct call;
        template <typename... Types>
        struct call<Union<Types...>> {
            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                Self self,
                Key key
            ) {
                using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                if constexpr (__contains__<S, Key>::enable) {
                    return success(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Key>(key)
                    );
                } else {
                    return failure(
                        reinterpret_cast<S>(std::forward<Self>(self)->m_value),
                        std::forward<Key>(key)
                    );
                }
            }
        };

        template <size_t I, typename OnSuccess, typename OnFailure>
        static type exec(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Key key
        ) {
            if (I == self->m_index) {
                return call<std::remove_cvref_t<Self>>::template exec<I>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Key>(key)
                );
            }
            if constexpr ((I + 1) < UnionTraits<Self>::n) {
                return exec<I + 1>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self),
                    std::forward<Key>(key)
                );
            } else {
                return failure(std::forward<Self>(self), std::forward<Key>(key));
            }
        }

        template <typename OnSuccess, typename OnFailure>
        static type operator()(
            OnSuccess&& success,
            OnFailure&& failure,
            Self self,
            Key key
        ) {
            return exec<0>(
                std::forward<OnSuccess>(success),
                std::forward<OnFailure>(failure),
                std::forward<Self>(self),
                std::forward<Key>(key)
            );
        }
    };


    /// TODO: inplace operators might require special handling.


    template <template <typename> typename control>
    struct union_unary_inplace_operator {
        template <typename>
        struct op { static constexpr bool enable = false; };
        template <py_union Self>
        struct op<Self> {
            template <typename>
            static constexpr bool match = false;
            template <typename... Types>
                requires (control<qualify_lvalue<Types, Self>>::enable || ...)
            static constexpr bool match<Union<Types...>> = true;
            static constexpr bool enable = match<std::remove_cvref_t<Self>>;
            using type = Self;

            template <typename>
            struct call;
            template <typename... Types>
            struct call<Union<Types...>> {
                template <size_t I, typename OnSuccess, typename OnFailure>
                static type exec(
                    OnSuccess&& success,
                    OnFailure&& failure,
                    Self self
                ) {
                    using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                    if constexpr (control<S>::enable) {
                        success(reinterpret_cast<S>(
                            std::forward<Self>(self)->m_value
                        ));
                    } else {
                        failure(reinterpret_cast<S>(
                            std::forward<Self>(self)->m_value
                        ));
                    }
                    return self;
                }
            };

            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                Self self
            ) {
                if (I == self->m_index) {
                    return call<std::remove_cvref_t<Self>>::template exec<I>(
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure),
                        std::forward<Self>(self)
                    );
                }
                if constexpr ((I + 1) < UnionTraits<Self>::n) {
                    return exec<I + 1>(
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure),
                        std::forward<Self>(self)
                    );
                } else {
                    failure(std::forward<Self>(self));
                    return std::forward<Self>(self);
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                OnSuccess&& success,
                OnFailure&& failure,
                Self self
            ) {
                return exec<0>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<Self>(self)
                );
            }
        };
    };

    template <typename T>
    using UnionIncrement = union_unary_inplace_operator<__increment__>::template op<T>;
    template <typename T>
    using UnionDecrement = union_unary_inplace_operator<__decrement__>::template op<T>;



    template <template <typename, typename> typename control>
    struct union_inplace_binary_operator {
        template <typename, typename>
        struct op { static constexpr bool enable = false; };
        template <py_union L, py_union R>
        struct op<L, R> {
            template <typename, typename...>
            struct any_match { static constexpr bool enable = false; };
            template <typename L2, typename R2, typename... R2s>
            struct any_match<L2, R2, R2s...> {
                static constexpr bool enable =
                    control<qualify_lvalue<L2, L>, qualify_lvalue<R2, R>>::enable ||
                    any_match<L2, R2s...>::enable;
            };
            template <typename, typename>
            static constexpr bool match = false;
            template <typename... Ls, typename... Rs>
                requires (any_match<Ls, Rs...>::enable || ...)
            static constexpr bool match<Union<Ls...>, Union<Rs...>> = true;
            static constexpr bool enable = match<
                std::remove_cvref_t<L>,
                std::remove_cvref_t<R>
            >;
            using type = L;

            template <typename, typename>
            struct call;
            template <typename... Ls, typename... Rs>
            struct call<Union<Ls...>, Union<Rs...>> {
                template <size_t I, size_t J, typename OnSuccess, typename OnFailure>
                static type exec(
                    OnSuccess&& success,
                    OnFailure&& failure,
                    L lhs,
                    R rhs
                ) {
                    using L2 = qualify_lvalue<unpack_type<I, Ls...>, L>;
                    using R2 = qualify_lvalue<unpack_type<J, Rs...>, R>;
                    if constexpr (control<L2, R2>::enable) {
                        success(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    } else {
                        failure(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    }
                    return std::forward<L>(lhs);
                }
            };

            template <size_t I, size_t J, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                L lhs,
                R rhs
            ) {
                if (I == lhs->m_index) {
                    if (J == rhs->m_index) {
                        return call<
                            std::remove_cvref_t<L>,
                            std::remove_cvref_t<R>
                        >::template exec<I, J>(
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure),
                            std::forward<L>(lhs),
                            std::forward<R>(rhs)
                        );
                    }
                    if constexpr (I < UnionTraits<L>::n && (J + 1) < UnionTraits<R>::n) {
                        return exec<I, J + 1>(
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure),
                            std::forward<L>(lhs),
                            std::forward<R>(rhs)
                        );
                    } else {
                        failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    }
                }
                if constexpr ((I + 1) < UnionTraits<L>::n && J < UnionTraits<R>::n) {
                    return exec<I + 1, J>(
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure),
                        std::forward<L>(lhs),
                        std::forward<R>(rhs)
                    );
                } else {
                    failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    return std::forward<L>(lhs);
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                OnSuccess&& success,
                OnFailure&& failure,
                L lhs,
                R rhs
            ) {
                return exec<0, 0>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                );
            }
        };
        template <py_union L, typename R>
        struct op<L, R> {
            template <typename>
            static constexpr bool match = false;
            template <typename... Ls>
                requires (control<qualify_lvalue<Ls, L>, R>::enable || ...)
            static constexpr bool match<Union<Ls...>> = true;
            static constexpr bool enable = match<std::remove_cvref_t<L>>;
            using type = L;

            template <typename>
            struct call;
            template <typename... Ls>
            struct call<Union<Ls...>> {
                template <size_t I, typename OnSuccess, typename OnFailure>
                static type exec(
                    OnSuccess&& success,
                    OnFailure&& failure,
                    L lhs,
                    R rhs
                ) {
                    using L2 = qualify_lvalue<unpack_type<I, Ls...>, L>;
                    if constexpr (control<L2, R>::enable) {
                        success(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            std::forward<R>(rhs)
                        );
                    } else {
                        failure(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            std::forward<R>(rhs)
                        );
                    }
                    return std::forward<L>(lhs);
                }
            };

            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                L lhs,
                R rhs
            ) {
                if (I == lhs->m_index) {
                    return call<std::remove_cvref_t<L>>::template exec<I>(
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure),
                        std::forward<L>(lhs),
                        std::forward<R>(rhs)
                    );
                }
                if constexpr ((I + 1) < UnionTraits<L>::n) {
                    return exec<I + 1>(
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure),
                        std::forward<L>(lhs),
                        std::forward<R>(rhs)
                    );
                } else {
                    failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    return std::forward<L>(lhs);
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                OnSuccess&& success,
                OnFailure&& failure,
                L lhs,
                R rhs
            ) {
                return exec<0>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                );
            }
        };
        template <typename L, py_union R>
        struct op<L, R> {
            template <typename>
            static constexpr bool match = false;
            template <typename... Rs>
                requires (control<L, qualify_lvalue<Rs, R>>::enable || ...)
            static constexpr bool match<Union<Rs...>> = true;
            static constexpr bool enable = match<std::remove_cvref_t<R>>;
            using type = L;

            template <typename>
            struct call;
            template <typename... Rs>
            struct call<Union<Rs...>> {
                template <size_t J, typename OnSuccess, typename OnFailure>
                static type exec(
                    OnSuccess&& success,
                    OnFailure&& failure,
                    L lhs,
                    R rhs
                ) {
                    using R2 = qualify_lvalue<unpack_type<J, Rs...>, R>;
                    if constexpr (control<L, R2>::enable) {
                        success(
                            std::forward<L>(lhs),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    } else {
                        failure(
                            std::forward<L>(lhs),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    }
                    return std::forward<L>(lhs);
                }
            };

            template <size_t J, typename OnSuccess, typename OnFailure>
            static type exec(
                OnSuccess&& success,
                OnFailure&& failure,
                L lhs,
                R rhs
            ) {
                if (J == lhs->m_index) {
                    return call<std::remove_cvref_t<L>>::template exec<J>(
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure),
                        std::forward<L>(lhs),
                        std::forward<R>(rhs)
                    );
                }
                if constexpr ((J + 1) < UnionTraits<R>::n) {
                    return exec<J + 1>(
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure),
                        std::forward<L>(lhs),
                        std::forward<R>(rhs)
                    );
                } else {
                    failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    return std::forward<L>(lhs);
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                OnSuccess&& success,
                OnFailure&& failure,
                L lhs,
                R rhs
            ) {
                return exec<0>(
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure),
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                );
            }
        };
    };

    template <typename L, typename R>
    using UnionInplaceAdd = union_inplace_binary_operator<__iadd__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceSub = union_inplace_binary_operator<__isub__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceMul = union_inplace_binary_operator<__imul__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceTrueDiv = union_inplace_binary_operator<__itruediv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceFloorDiv = union_inplace_binary_operator<__ifloordiv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceMod = union_inplace_binary_operator<__imod__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceLShift = union_inplace_binary_operator<__ilshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceRShift = union_inplace_binary_operator<__irshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceAnd = union_inplace_binary_operator<__iand__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceXor = union_inplace_binary_operator<__ixor__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceOr = union_inplace_binary_operator<__ior__>::template op<L, R>;


    // template <typename Base, typename Exp, typename Mod>
    // using UnionInplacePow = union_inplace_ternary_operator<__ipow__>::template op<Base, Exp, Mod>;

}


template <typename... Types>
struct Interface<Union<Types...>> : impl::UnionTag {};
template <typename... Types>
struct Interface<Type<Union<Types...>>> : impl::UnionTag {};


template <typename... Types>
    requires (
        sizeof...(Types) > 0 &&
        (!std::is_reference_v<Types> && ...) &&
        (std::derived_from<Types, Object> && ...) &&
        impl::types_are_unique<Types...> &&
        !(std::is_const_v<Types> || ...) &&
        !(std::is_volatile_v<Types> || ...)
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
        /// TODO: this would need some special handling for argument annotations
        /// denoting structural fields, as well as a way to instantiate them
        /// accordingly.
        Object result = reinterpret_steal<Object>(PyTuple_Pack(
            sizeof...(Ts),
            ptr(Type<Ts>())...
        ));
        if (result.is(nullptr)) {
            Exception::to_python();
        }
        return result;
    }
};


// template <impl::py_union Derived, typename Base>
//     requires (
//         /// TODO: should this be disabled if the base type is also a union?
//         true
//     )  /// TODO: should be enabled if one or more members supports it
// struct __isinstance__<Derived, Base>                        : Returns<bool> {
//     static constexpr bool operator()(Derived obj) {
//         /// TODO: if Derived is a member of the union or a superclass of a member,
//         /// then this can devolve to a simple index check.  Otherwise, it should
//         /// extract the underlying object and recur.
//     }
//     static constexpr bool operator()(Derived obj, Base base) {
//         /// TODO: this should only be enabled if one or more members supports it with
//         /// the given base type.
//     }
// };


// template <typename Derived, impl::py_union Base>
//     requires (true)  /// TODO: should be enabled if one or more members supports it
// struct __isinstance__<Derived, Base>                        : Returns<bool> {
//     static constexpr bool operator()(Derived obj) {
//         /// TODO: returns true if any of the types match
//     }
//     static constexpr bool operator()(Derived obj, Base base) {
//         /// TODO: only enabled if one or more members supports it
//     }
// };


// template <impl::inherits<impl::OptionalTag> Derived, typename Base>
//     requires (__isinstance__<impl::wrapped_type<Derived>, Base>::enable)
// struct __isinstance__<Derived, Base>                        : Returns<bool> {
//     static constexpr bool operator()(Derived obj) {
//         if (obj->m_value.is(None)) {
//             return impl::is<Base, NoneType>;
//         } else {
//             return isinstance<Base>(
//                 reinterpret_cast<impl::wrapped_type<Derived>>(
//                     std::forward<Derived>(obj)->m_value
//                 )
//             );
//         }
//     }
//     template <typename T = impl::wrapped_type<Derived>>
//         requires (std::is_invocable_v<__isinstance__<T, Base>, T, Base>)
//     static constexpr bool operator()(Derived obj, Base&& base) {
//         if (obj->m_value.is(None)) {
//             return false;  /// TODO: ???
//         } else {
//             return isinstance(
//                 reinterpret_cast<impl::wrapped_type<Derived>>(
//                     std::forward<Derived>(obj)->m_value
//                 ),
//                 std::forward<Base>(base)
//             );
//         }
//     }
// };


// template <typename Derived, impl::inherits<impl::OptionalTag> Base>
//     requires (
//         __issubclass__<Derived, impl::wrapped_type<Base>>::enable &&
//         !impl::inherits<Derived, impl::OptionalTag>
//     )
// struct __isinstance__<Derived, Base>                         : Returns<bool> {
//     static constexpr bool operator()(Derived&& obj) {
//         if constexpr (impl::dynamic<Derived>) {
//             return
//                 obj.is(None) ||
//                 isinstance<impl::wrapped_type<Base>>(std::forward<Derived>(obj));
//         } else {
//             return
//                 impl::none_like<Derived> ||
//                 isinstance<impl::wrapped_type<Base>>(std::forward<Derived>(obj));
//         }
//     }
//     template <typename T = impl::wrapped_type<Base>>
//         requires (std::is_invocable_v<__isinstance__<Derived, T>, Derived, T>)
//     static constexpr bool operator()(Derived&& obj, Base base) {
//         if (base->m_value.is(None)) {
//             return false;  /// TODO: ???
//         } else {
//             return isinstance(
//                 std::forward<Derived>(obj),
//                 reinterpret_cast<impl::wrapped_type<Derived>>(
//                     std::forward<Base>(base)->m_value
//                 )
//             );
//         }
//     }
// };





// template <typename Derived, impl::inherits<impl::OptionalTag> Base>
// struct __issubclass__<Derived, Base>                         : Returns<bool> {
//     using Wrapped = std::remove_reference_t<Base>::__wrapped__;
//     static constexpr bool operator()() {
//         return impl::none_like<Derived> || issubclass<Derived, Wrapped>();
//     }
//     template <typename T = Wrapped>
//         requires (std::is_invocable_v<__issubclass__<Derived, T>, Derived>)
//     static constexpr bool operator()(Derived&& obj) {
//         if constexpr (impl::dynamic<Derived>) {
//             return
//                 obj.is(None) ||
//                 issubclass<Wrapped>(std::forward<Derived>(obj));
//         } else {
//             return
//                 impl::none_like<Derived> ||
//                 issubclass<Wrapped>(std::forward<Derived>(obj));
//         }
//     }
//     template <typename T = Wrapped>
//         requires (std::is_invocable_v<__issubclass__<Derived, T>, Derived, T>)
//     static constexpr bool operator()(Derived&& obj, Base base) {
//         if (base.is(None)) {
//             return false;
//         } else {
//             return issubclass(std::forward<Derived>(obj), base.value());
//         }
//     }
// };



/// TODO: isinstance() and issubclass() can be optimized in some cases to just call
/// std::holds_alternative<T>() on the underlying object, rather than needing to
/// invoke Python.  This can be done if the type that is checked against is a
/// supertype of any of the members of the union, in which case the type check can be
/// reduced to just a simple comparison against the index.


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
    requires (
        !impl::py_union<To> &&
        impl::UnionToType<std::remove_cvref_t<From>>::template convertible_to<To>
    )
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
template <impl::has_python T> requires (impl::python<T> || std::same_as<
    std::remove_cv_t<T>,
    impl::cpp_type<impl::python_type<std::remove_cv_t<T>>>
>)
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


template <impl::is_variant From, typename... Ts>
    requires (impl::VariantToUnion<From>::template convertible_to<Ts...>)
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <typename T>
    struct convert {
        static Union<Ts...> operator()(const T& value) {
            return impl::construct<Union<Ts...>>(
                impl::python_type<T>(value)
            );
        }
        static Union<Ts...> operator()(T&& value) {
            return impl::construct<Union<Ts...>>(
                impl::python_type<T>(std::move(value))
            );
        }
    };
    struct Visitor : convert<Ts>... { using convert<Ts>::operator()...; };
    static Union<Ts...> operator()(From value) {
        return std::visit(Visitor{}, std::forward<From>(value));
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
        static void exec(Self self, Value&& value) {
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
    static void exec(Self self, Value&& value) {
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

    static void operator()(Self self, Value&& value) {
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
            []<typename S, typename... A>(S&& self, A&&... args) -> type {
                return std::forward<S>(self)(std::forward<A>(args)...);
            },
            []<typename S, typename... A>(S&& self, A&&... args) -> type {
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
            []<typename S, typename... K>(S&& self, K&&... key) -> type {
                return std::forward<S>(self)[std::forward<K>(key)...];
            },
            []<typename S, typename... K>(S&& self, K&&... key) -> type {
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


template <impl::py_union Self> requires (impl::UnionHash<Self>::enable)
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


template <impl::py_union Self> requires (impl::UnionLen<Self>::enable)
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


template <impl::py_union Self, typename Key> requires (impl::UnionContains<Self, Key>::enable)
struct __contains__<Self, Key> : Returns<bool> {
    static bool operator()(Self self, Key key) {
        return impl::UnionContains<Self, Key>{}(
            []<typename S, typename K>(S&& self, K&& key) -> bool {
                return std::forward<S>(self).contains(std::forward<K>(key));
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
            []<typename S>(S&& self) -> type {
                return abs(std::forward<S>(self));
            },
            []<typename S>(S&& self) -> type {
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
            []<typename S>(S&& self) -> type {
                return ~std::forward<S>(self);
            },
            []<typename S>(S&& self) -> type {
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
            []<typename S>(S&& self) -> type {
                return +std::forward<S>(self);
            },
            []<typename S>(S&& self) -> type {
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
            []<typename S>(S&& self) -> type {
                return -std::forward<S>(self);
            },
            []<typename S>(S&& self) -> type {
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
struct __increment__<Self> : Returns<Self> {
    using type = impl::UnionIncrement<Self>::type;
    static type operator()(Self self) {
        return impl::UnionIncrement<Self>{}(
            []<typename S>(S&& self) -> void {
                ++std::forward<S>(self);
            },
            []<typename S>(S&& self) -> void {
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
struct __decrement__<Self> : Returns<Self> {
    using type = impl::UnionDecrement<Self>::type;
    static type operator()(Self self) {
        return impl::UnionDecrement<Self>{}(
            []<typename S>(S&& self) -> void {
                --std::forward<S>(self);
            },
            []<typename S>(S&& self) -> void {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) < std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) <= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) == std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) != std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) >= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) > std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) + std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) - std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) * std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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


template <typename L, typename R> requires (impl::UnionPow<L, R>::enable)
struct __pow__<L, R> : Returns<typename impl::UnionPow<L, R>::type> {
    using type = impl::UnionPow<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionPow<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return pow(std::forward<L2>(lhs), std::forward<R2>(rhs));
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for **: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionTrueDiv<L, R>::enable)
struct __truediv__<L, R> : Returns<typename impl::UnionTrueDiv<L, R>::type> {
    using type = impl::UnionTrueDiv<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionTrueDiv<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) / std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return floordiv(std::forward<L2>(lhs), std::forward<R2>(rhs));
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) % std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) << std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) >> std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) & std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) ^ std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) | std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
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
struct __iadd__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceAdd<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) += std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __isub__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceSub<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) -= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __imul__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceMul<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) *= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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


template <typename L, typename R> requires (impl::UnionInplacePow<L, R>::enable)
struct __ipow__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplacePow<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                ipow(std::forward<L2>(lhs), std::forward<R2>(rhs));
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for **=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


template <typename L, typename R> requires (impl::UnionInplaceTrueDiv<L, R>::enable)
struct __itruediv__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceTrueDiv<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) /= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __ifloordiv__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceFloorDiv<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                ifloordiv(std::forward<L2>(lhs), std::forward<R2>(rhs));
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __imod__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceMod<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) %= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __ilshift__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceLShift<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) <<= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __irshift__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceRShift<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) >>= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __iand__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceAnd<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) &= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __ixor__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceXor<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) ^= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
struct __ior__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceOr<L, R>{}(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) |= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
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
