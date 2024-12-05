#ifndef BERTRAND_CORE_UNION_H
#define BERTRAND_CORE_UNION_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"
#include "access.h"


namespace py {


template <typename... Types>
    requires (
        sizeof...(Types) > 1 &&
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
    template <typename... Matches>
    struct to_union<pack<Matches...>> {
        template <typename>
        struct convert {
            template <typename T>
            struct ToPython { using type = python_type<T>; };
            template <>
            struct ToPython<void> { using type = NoneType; };
            template <typename>
            struct helper;
            template <typename... Unique>
            struct helper<pack<Unique...>> {
                using type = Union<std::remove_cvref_t<Unique>...>;
            };
            using type = helper<
                typename pack<typename ToPython<Matches>::type...>::deduplicate
            >::type;
        };
        template <typename Match>
        struct convert<pack<Match>> { using type = Match; };
        using type = convert<typename pack<Matches...>::deduplicate>::type;
    };

    /* Raw types are converted to a pack of length 1. */
    template <typename T>
    struct UnionTraits {
        using type = T;
        using pack = py::pack<T>;
    };

    /* Unions are converted into a pack of the same length. */
    template <py_union T>
    struct UnionTraits<T> {
    private:

        template <typename>
        struct _pack;
        template <typename... Types>
        struct _pack<Union<Types...>> {
            using type = py::pack<qualify<Types, T>...>;
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
            using type = py::pack<qualify<Types, T>...>;
        };

    public:
        using type = T;
        using pack = _pack<std::remove_cvref_t<T>>::type;
    };

    template <typename...>
    struct _exhaustive { static constexpr bool enable = false; };
    template <typename Func, typename... Args> requires (py_union<Args> || ...)
    struct _exhaustive<Func, Args...> {
        template <typename>
        struct permute;
        template <typename... Permutations>
        struct permute<pack<Permutations...>> {
            template <typename>
            struct _enable { static constexpr bool enable = false; };
            template <typename... Actual> requires (std::is_invocable_v<Func, Actual...>)
            struct _enable<pack<Actual...>> {
                static constexpr bool enable = true;
                using type = std::invoke_result_t<Func, Actual...>;
            };
            template <typename...>
            static constexpr bool returns_are_identical = false;
            template <typename First, typename... Rest>
                requires (_enable<Permutations>::enable && ...)
            static constexpr bool returns_are_identical<First, Rest...> = (std::same_as<
                typename _enable<First>::type,
                typename _enable<Rest>::type
            > && ...);
            static constexpr bool enable = returns_are_identical<Permutations...>;

            template <bool>
            struct _type { using type = void; };
            template <>
            struct _type<true> {
                using type = std::common_type_t<typename _enable<Permutations>::type...>;
            };
            using type = _type<enable>::type;
        };

        template <typename...>
        struct _permutations { using type = pack<>; };
        template <typename First, typename... Rest>
        struct _permutations<First, Rest...> {
            using type = UnionTraits<First>::pack::template product<
                typename UnionTraits<Rest>::pack...
            >;
        };
        using permutations = _permutations<Args...>::type;

        static constexpr bool enable = permute<permutations>::enable;
        using type = permute<permutations>::type;
    };

    template <typename Func, typename... Args>
    static constexpr bool exhaustive = _exhaustive<Func, Args...>::enable;

    template <typename Func, typename... Args> requires (exhaustive<Func, Args...>)
    using visit_type = _exhaustive<Func, Args...>::type;

    template <typename, typename...>
    struct visit_helper;
    template <typename... Prev>
    struct visit_helper<pack<Prev...>> {
        template <typename Visitor> requires (std::is_invocable_v<Visitor, Prev...>)
        static decltype(auto) operator()(Visitor&& visitor, Prev... prev) {
            return std::forward<Visitor>(visitor)(std::forward<Prev>(prev)...);
        }
    };
    template <typename... Prev, typename Curr, typename... Next>
    struct visit_helper<pack<Prev...>, Curr, Next...> {
        template <size_t I, typename Visitor>
            requires (exhaustive<Visitor, Prev..., Curr, Next...>)
        struct VTable {
            using Return = visit_type<Visitor, Prev..., Curr, Next...>;
            static constexpr auto python() -> Return(*)(Visitor, Prev..., Curr, Next...) {
                constexpr auto callback = [](
                    Visitor visitor,
                    Prev... prev,
                    Curr curr,
                    Next... next
                ) -> Return {
                    using T = std::remove_cvref_t<Curr>::template at<I>;
                    using Q = qualify<T, Curr>;
                    if constexpr (std::is_lvalue_reference_v<Curr>) {
                        return visit_helper<pack<Prev..., Q>, Next...>{}(
                            std::forward<Visitor>(visitor),
                            std::forward<Prev>(prev)...,
                            reinterpret_cast<Q>(curr),
                            std::forward<Next>(next)...
                        );
                    } else {
                        return visit_helper<pack<Prev..., Q>, Next...>{}(
                            std::forward<Visitor>(visitor),
                            std::forward<Prev>(prev)...,
                            reinterpret_cast<std::add_rvalue_reference_t<Q>>(curr),
                            std::forward<Next>(next)...
                        );
                    }
                };
                return +callback;
            };
            static constexpr auto variant() -> Return(*)(Visitor, Prev..., Curr, Next...) {
                constexpr auto callback = [](
                    Visitor visitor,
                    Prev... prev,
                    Curr curr,
                    Next... next
                ) -> Return {
                    using T = std::variant_alternative_t<I, std::remove_cvref_t<Curr>>;
                    using Q = qualify<T, Curr>;
                    if constexpr (
                        std::is_lvalue_reference_v<Curr> ||
                        std::is_const_v<std::remove_reference_t<Curr>>
                    ) {
                        return visit_helper<pack<Prev..., Q>, Next...>{}(
                            std::forward<Visitor>(visitor),
                            std::forward<Prev>(prev)...,
                            std::get<I>(curr),
                            std::forward<Next>(next)...
                        );
                    } else {
                        return visit_helper<pack<Prev..., Q>, Next...>{}(
                            std::forward<Visitor>(visitor),
                            std::forward<Prev>(prev)...,
                            std::move(std::get<I>(curr)),
                            std::forward<Next>(next)...
                        );
                    }
                };
                return +callback;
            }
        };

        template <typename Visitor>
        static decltype(auto) operator()(
            Visitor&& visitor,
            Prev... prev,
            Curr curr,
            Next... next
        ) {
            if constexpr (py_union<Curr>) {
                constexpr auto vtable = []<size_t... Is>(std::index_sequence<Is...>) {
                    return std::array{VTable<Is, Visitor>::python()...};
                }(std::make_index_sequence<std::remove_cvref_t<Curr>::n>{});

                return vtable[curr.m_index](
                    std::forward<Visitor>(visitor),
                    std::forward<Prev>(prev)...,
                    std::forward<Curr>(curr),
                    std::forward<Next>(next)...
                );

            } else if constexpr (is_variant<Curr>) {
                constexpr auto vtable = []<size_t... Is>(std::index_sequence<Is...>) {
                    return std::array{VTable<Is, Visitor>::variant()...};
                }(std::make_index_sequence<std::variant_size_v<std::remove_cvref_t<Curr>>>{});

                return vtable[curr.index()](
                    std::forward<Visitor>(visitor),
                    std::forward<Prev>(prev)...,
                    std::forward<Curr>(curr),
                    std::forward<Next>(next)...
                );

            } else {
                return visit_helper<pack<Prev..., Curr>, Next...>{}(
                    std::forward<Visitor>(visitor),
                    std::forward<Prev>(prev)...,
                    std::forward<Curr>(curr),
                    std::forward<Next>(next)...
                );
            }
        }
    };

}  // namespace impl


/// NOTE: what would be ideal is if a simple initializer list could be supplied to
/// visit(), but that doesn't work because CTAD isn't triggered on templated function
/// arguments.  The Visitor{} type must be explicitly named, which is a bit of a pain.


/* A simple convenience struct implementing the overload pattern for the `py::visit()`
function (and any other similar cases). */
template <typename... Funcs>
struct Visitor : Funcs... { using Funcs::operator()...; };


/* Non-member `py::visit(visitor, args...)` operator, similar to `std::visit()`.  A
member version of this operator is implemented for `Union` objects, which allows for
chaining.

The `visitor` object is a temporary that is constructed from either a single function
or a set of functions enclosed in an initializer list, emulating the overload pattern.
The `args` are the arguments to be passed to the visitor, which must contain at least
one union type.  The visitor must be callable for all possible permutations of the
unions in `args`, and the return type must be consistent across each one.  The
arguments will be perfectly forwarded in the same order as they are given, with each
union unwrapped to its actual type.  The order of the unions is irrelevant, and
non-union arguments can be interspersed freely between them. */
template <typename Func, typename... Args> requires (impl::exhaustive<Func, Args...>)
decltype(auto) visit(Func&& visitor, Args&&... args) {
    return impl::visit_helper<pack<>, Args...>{}(
        std::forward<Func>(visitor),
        std::forward<Args>(args)...
    );
}


namespace impl {

    /* Allow implicit conversion from the union type if and only if all of its
    qualified members are convertible to that type. */
    template <py_union U>
    struct UnionToType {
        template <typename>
        struct convertible;
        template <typename... Types>
        struct convertible<Union<Types...>> {
            template <typename Out>
            static constexpr bool implicit =
                (std::convertible_to<qualify<Types, U>, Out> && ...);
            template <typename Out>
            static constexpr bool convert =
                (impl::explicitly_convertible_to<qualify<Types, U>, Out> || ...);
        };
        template <typename Out>
        static constexpr bool implicit =
            convertible<std::remove_cvref_t<U>>::template implicit<Out>;
        template <typename Out>
        static constexpr bool convert =
            convertible<std::remove_cvref_t<U>>::template convert<Out>;
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
            using type = to_union<pack<python_type<Types>...>>::type;
            template <typename, typename... Ts>
            static constexpr bool _convert = true;
            template <typename... To, typename T, typename... Ts>
            static constexpr bool _convert<pack<To...>, T, Ts...> =
                (std::convertible_to<qualify<T, V>, To> || ...) &&
                _convert<pack<To...>, Ts...>;
            template <typename... Ts>
            static constexpr bool convert = _convert<pack<Types...>, Ts...>;
        };
        static constexpr bool enable = convertible<std::remove_cvref_t<V>>::enable;
        using type = convertible<std::remove_cvref_t<V>>::type;
        template <typename... Ts>
        static constexpr bool convert = convertible<
            std::remove_cvref_t<V>
        >::template convert<Ts...>;
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
            struct _product<pack<First, Rest...>> {
                using type = First::template product<Rest...>;
            };
            using product = _product<pack<typename UnionTraits<Args>::pack...>>::type;

            /* 2. Produce an output pack containing all of the valid return types for
            each permutation that satisfies the control structure. */
            template <typename>
            struct traits;
            template <typename... Permutations>
            struct traits<pack<Permutations...>> {
                template <typename out, typename...>
                struct _returns { using type = out; };
                template <typename... out, typename P, typename... Ps>
                struct _returns<pack<out...>, P, Ps...> {
                    template <typename>
                    struct filter { using type = pack<out...>; };
                    template <typename P2> requires (P2::template enable<control>)
                    struct filter<P2> {
                        using type = pack<out..., typename P2::template type<control>>;
                    };
                    using type = _returns<typename filter<P>::type, Ps...>::type;
                };
                using returns = _returns<pack<>, Permutations...>::type;
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
            struct unary<pack<Matches...>, T, Ts...> {
                template <typename>
                struct conditional { using type = pack<Matches...>; };
                template <typename T2>
                    requires (__getattr__<qualify_lvalue<T2, Self>, Name>::enable)
                struct conditional<T2> {
                    using type = pack<
                        Matches...,
                        typename __getattr__<qualify_lvalue<T2, Self>, Name>::type
                    >;
                };
                using type = unary<typename conditional<T>::type, Ts...>::type;
            };
            static constexpr bool enable = true;
            using type = to_union<typename unary<pack<>, Types...>::type>::type;
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
    /// multiple call signatures, which require special wrapper traits to handle.

    template <typename Derived, typename Base>
    struct _BinaryUnionIsInstance {
        static constexpr bool enable =
            std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, Derived, Base>;

        static constexpr bool assertion =
            !(std::same_as<Derived, Object&> && std::same_as<Base, Object&>);

        static_assert(
            assertion || std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, Derived, Base>
        );
        using type = bool;
    };

    template <typename Derived, typename Base>
    struct _UnaryUnionIsSubclass {
        static constexpr bool enable =
            std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, Derived>;
        using type = bool;
    };

    template <typename Derived, typename Base>
    struct _BinaryUnionIsSubclass {
        static constexpr bool enable =
            std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, Derived, Base>;
        using type = bool;
    };

    template <typename Derived, typename Base>
    using BinaryUnionIsInstance =
        union_operator<_BinaryUnionIsInstance>::template op<Derived, Base>;
    template <typename Derived, typename Base>
    using UnaryUnionIsSubclass =
        union_operator<_UnaryUnionIsSubclass>::template op<Derived, Base>;
    template <typename Derived, typename Base>
    using BinaryUnionIsSubclass =
        union_operator<_BinaryUnionIsSubclass>::template op<Derived, Base>;

}  // namespace impl


template <typename... Types>
struct Interface<Union<Types...>> : impl::UnionTag {
    static constexpr size_t n = sizeof...(Types);

    template <size_t I> requires (I < n)
    using at = impl::unpack_type<I, Types...>;

    template <typename T> requires (std::same_as<T, Types> || ...)
    [[nodiscard]] bool holds_alternative(this auto&& self) {
        return self.m_index == impl::index_of<T, Types...>;
    }

    template <typename T> requires (std::same_as<T, Types> || ...)
    [[nodiscard]] auto get(this auto&& self) -> T {
        if (self.m_index != impl::index_of<T, Types...>) {
            throw TypeError(
                "bad union access: '" + type_name<T> + "' is not the active type"
            );
        }
        if constexpr (std::is_lvalue_reference_v<decltype(self)>) {
            return reinterpret_borrow<T>(ptr(self));
        } else {
            return reinterpret_steal<T>(release(self));
        }
    }

    template <size_t I> requires (I < n)
    [[nodiscard]] auto get(this auto&& self) -> at<I> {
        using ref = impl::qualify_lvalue<at<I>, decltype(self)>;
        if (self.m_index != I) {
            throw TypeError(
                "bad union access: '" + type_name<at<I>> + "' is not the active type"
            );
        }
        if constexpr (std::is_lvalue_reference_v<decltype(self)>) {
            return reinterpret_borrow<at<I>>(ptr(self));
        } else {
            return reinterpret_steal<at<I>>(release(self));
        }
    }

    template <typename T> requires (std::same_as<T, Types> || ...)
    [[nodiscard]] auto get_if(this auto&& self) -> Optional<T> {
        if (self.m_index != impl::index_of<T, Types...>) {
            return None;
        }
        if constexpr (std::is_lvalue_reference_v<decltype(self)>) {
            return reinterpret_borrow<T>(ptr(self));
        } else {
            return reinterpret_steal<T>(release(self));
        }
    }

    template <size_t I> requires (I < n)
    [[nodiscard]] auto get_if(this auto&& self) -> Optional<at<I>> {
        if (self.m_index != I) {
            return None;
        }
        if constexpr (std::is_lvalue_reference_v<decltype(self)>) {
            return reinterpret_borrow<at<I>>(ptr(self));
        } else {
            return reinterpret_steal<at<I>>(release(self));
        }
    }

    template <typename Self, typename Func, typename... Args>
        requires (impl::exhaustive<Func, Self, Args...>)
    decltype(auto) visit(
        this Self&& self,
        Func&& visitor,
        Args&&... args
    ) {
        return py::visit(
            std::forward<Func>(visitor),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }
};


template <typename... Types>
struct Interface<Type<Union<Types...>>> {
private:
    using type = Interface<Union<Types...>>;

public:
    static constexpr size_t n = type::n;

    template <size_t I> requires (I < n)
    using at = type::template at<I>;

    template <typename T, impl::inherits<type> Self>
        requires (std::same_as<T, Types> || ...)
    [[nodiscard]] bool holds_alternative(Self&& self) {
        return std::forward<Self>(self).template holds_alternative<T>();
    }

    template <typename T, impl::inherits<type> Self>
        requires (std::same_as<T, Types> || ...)
    [[nodiscard]] static auto get(Self&& self) -> T {
        return std::forward<Self>(self).template get<T>();
    }

    template <size_t I, impl::inherits<type> Self>
        requires (I < n)
    [[nodiscard]] static auto get(Self&& self) -> at<I> {
        return std::forward<Self>(self).template get<I>();
    }

    template <typename T, impl::inherits<type> Self>
        requires (std::same_as<T, Types> || ...)
    [[nodiscard]] static auto get_if(Self&& self) -> Optional<T> {
        return std::forward<Self>(self).template get_if<T>();
    }

    template <size_t I, impl::inherits<type> Self>
        requires (I < n)
    [[nodiscard]] static auto get_if(Self&& self) -> Optional<at<I>> {
        return std::forward<Self>(self).template get_if<I>();
    }

    template <impl::inherits<type> Self, typename Func, typename... Args>
        requires (impl::exhaustive<Func, Self, Args...>)
    static decltype(auto) visit(
        Self&& self,
        Func&& visitor,
        Args&&... args
    ) {
        return py::visit(
            std::forward<Func>(visitor),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }
};


template <typename... Types>
    requires (
        sizeof...(Types) > 1 &&
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
the same operations as `std::variant`, but as member functions rather than free
functions, so as to avoid modifying standard library behavior.  The only
exception is `py::visit()` and its member equivalent, which differ from
`std::visit` in that they can accept arbitrary arguments, which are perfectly
forwarded to the visitor after unwrapping any unions and/or variants.  This is
done to reduce reliance on lambda captures, which can be cumbersome, and have
with performance implications.  For the member `visit()` function, the first
argument to the visitor is always the actual type of the invoking union.

Unions also specialize each of the built-in control structures to forward to
the active type, which gives them the same interface as a generic `Object`.
Each operator will be enabled if and only if at least one member of the union
supports it, and will produce a new union if multiple results are possible.
This leads to an automatic narrowing behavior, whereby operating on a union
will progressively reduce the number of possible results until only one
remains.  If the active member does not support a given operation, then a
corresponding Python error will be raised at runtime, as if the operation had
been attempted from Python directly.  As in Python, it is the user's
responsibility to determine the active type of the union before performing any
operations on it if they wish to avoid this behavior.

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
Unions have no special syntax in Python, and are created automatically when
using the `|` operator to combine several types.  They can be used in type
hints, function annotations, and variable declarations, and will be preserved
when translating to C++.

Union types exist mostly for the benefit of C++ code, where they are needed to
blend the dynamic nature of Python with the static nature of C++.  For example:

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

Python functions that accept or return unions using Python-style type hints
will automatically be translated into C++ functions using the corresponding
`py::Union<...>` types when bindings are generated.  Note that due to the
interaction between unions and optionals, a `None` type hint as a member of the
union in Python syntax will be transformed into a nested optional type in C++,
as described above.

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

        template <StaticStr ModName>
        static Type<Union> __export__(Module<ModName>& mod);
        static Type<Union> __import__();
    };

    size_t m_index = 0;

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


/* Initializer list constructor is only enabled for `Optional<T>`, and not for any
other form of union, where such a call might be ambiguous. */
template <typename T> requires (__initializer__<T>::enable)
struct __initializer__<Optional<T>>                        : Returns<Optional<T>> {
    using Element = __initializer__<T>::type;
    static Optional<T> operator()(const std::initializer_list<Element>& init) {
        Optional<T> result = reinterpret_steal<Optional<T>>(release(T(init)));
        result.m_index = 0;
        return result;
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
            Union<Ts...> result = reinterpret_borrow<Union<Ts...>>(ptr(None));
            result.m_index = none_idx<0, Ts...>;
            return result;
        } else {
            constexpr size_t index = idx<0, Ts...>;
            Union<Ts...> result = reinterpret_steal<Union<Ts...>>(
                release(impl::unpack_type<index>())
            );
            result.m_index = index;
            return result;
        }
    }
};


/* Explicit constructor calls are only allowed for `Optional<T>`, and not for any
other form of union, where such a call might be ambiguous. */
template <typename T, typename... Args>
    requires (sizeof...(Args) > 0 && std::constructible_from<T, Args...>)
struct __init__<Optional<T>, Args...>                : Returns<Optional<T>> {
    static Optional<T> operator()(Args&&... args) {
        Optional<T> result = reinterpret_steal<Optional<T>>(
            release(T(std::forward<Args>(args)...))
        );
        result.m_index = 0;
        return result;
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
        std::same_as<std::remove_cvref_t<impl::python_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<From, U> ? 0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
            constexpr size_t idx = match_idx<0, Ts...>;
            Union<Ts...> result = reinterpret_steal<Union<Ts...>>(
                release(impl::unpack_type<idx>(std::forward<From>(from)))
            );
            result.m_index = idx;
            return result;
        } else {
            constexpr size_t idx = convert_idx<0, Ts...>;
            Union<Ts...> result = reinterpret_steal<Union<Ts...>>(
                release(impl::unpack_type<idx>(std::forward<From>(from)))
            );
            result.m_index = idx;
            return result;
        }
    }
};


/* Universal implicit conversion from a union to any type for which ALL elements of the
union are implicitly convertible.  This covers conversions to `std::variant` provided
all types are accounted for, as well as to `std::optional` and pointer types whereby
`NoneType` is convertible to `std::nullopt` and `nullptr`, respectively. */
template <impl::py_union From, typename To>
    requires (!impl::py_union<To> && impl::UnionToType<From>::template implicit<To>)
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(From from) {
        return std::forward<From>(from).visit([](auto&& value) -> To {
            return std::forward<decltype(value)>(value);
        });
    }
};


/* Unversal explicit conversion from a union to any type for which ANY elements of the
union are explicitly convertible.  This can potentially raise an error if the actual
type contained within the union does not support the conversion. */
template <impl::py_union From, typename To>
    requires (!impl::py_union<To> && impl::UnionToType<From>::template convert<To>)
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(From from) {
        return std::forward<From>(from).visit([](auto&& value) -> To {
            if constexpr (__explicit_cast__<decltype(value), To>::enable) {
                return static_cast<To>(std::forward<decltype(value)>(value));
            } else {
                throw TypeError(
                    "cannot convert from '" + type_name<decltype(value)> + "' to '" +
                    type_name<To> + "'"
                );
            }
        });
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
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cvref_t<impl::python_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<From, U> ? 0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From value) {
        return std::visit(
            []<typename T>(T&& value) -> Union<Ts...> {
                if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                    constexpr size_t idx = match_idx<0, Ts...>;
                    Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                        impl::unpack_type<idx, Ts...>(std::forward<T>(value))
                    ));
                    result.m_index = idx;
                    return result;
                } else {
                    constexpr size_t idx = convert_idx<0, Ts...>;
                    Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                        impl::unpack_type<idx, Ts...>(std::forward<T>(value))
                    ));
                    result.m_index = idx;
                    return result;
                }
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
        std::same_as<std::remove_cvref_t<impl::python_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<From, U> ? 0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from.has_value()) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                constexpr size_t idx = match_idx<0, Ts...>;
                Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                    impl::unpack_type<idx, Ts...>(from.value())
                ));
                result.m_index = idx;
                return result;
            } else {
                constexpr size_t idx = convert_idx<0, Ts...>;
                Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                    impl::unpack_type<idx, Ts...>(from.value())
                ));
                result.m_index = idx;
                return result;
            }
        } else {
            Union<Ts...> result = reinterpret_borrow<Union<Ts...>>(ptr(None));
            result.m_index = impl::index_of<NoneType, Ts...>;
            return result;
        }
    }
};


template <impl::is_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<std::remove_pointer_t<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cvref_t<impl::python_type<std::remove_pointer_t<From>>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<std::remove_pointer_t<From>, U> ?
            0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                constexpr size_t idx = match_idx<0, Ts...>;
                Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                    impl::unpack_type<idx, Ts...>(*from)
                ));
                result.m_index = idx;
                return result;
            } else {
                constexpr size_t idx = convert_idx<0, Ts...>;
                Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                    impl::unpack_type<idx, Ts...>(*from)
                ));
                result.m_index = idx;
                return result;
            }
        } else {
            Union<Ts...> result = reinterpret_borrow<Union<Ts...>>(ptr(None));
            result.m_index = impl::index_of<NoneType, Ts...>;
            return result;
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
        std::same_as<std::remove_cvref_t<impl::python_type<impl::shared_ptr_type<From>>>, U> ?
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
                constexpr size_t idx = match_idx<0, Ts...>;
                Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                    impl::unpack_type<idx, Ts...>(*from)
                ));
                result.m_index = idx;
                return result;
            } else {
                constexpr size_t idx = convert_idx<0, Ts...>;
                Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                    impl::unpack_type<idx, Ts...>(*from)
                ));
                result.m_index = idx;
                return result;
            }
        } else {
            Union<Ts...> result = reinterpret_borrow<Union<Ts...>>(ptr(None));
            result.m_index = impl::index_of<NoneType, Ts...>;
            return result;
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
        std::same_as<std::remove_cvref_t<impl::python_type<impl::unique_ptr_type<From>>>, U> ?
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
                constexpr size_t idx = match_idx<0, Ts...>;
                Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                    impl::unpack_type<idx, Ts...>(*from)
                ));
                result.m_index = idx;
                return result;
            } else {
                constexpr size_t idx = convert_idx<0, Ts...>;
                Union<Ts...> result = reinterpret_steal<Union<Ts...>>(release(
                    impl::unpack_type<idx, Ts...>(*from)
                ));
                result.m_index = idx;
                return result;
            }
        } else {
            Union<Ts...> result = reinterpret_borrow<Union<Ts...>>(ptr(None));
            result.m_index = impl::index_of<NoneType, Ts...>;
            return result;
        }
    }
};


/// NOTE: all other operations are only enabled if one or more members of the union
/// support them, and will return a new union type holding all of the valid results,
/// or a standard type if the resulting union would be a singleton.


template <typename Derived, typename Base>
    requires (impl::py_union<Derived> || impl::py_union<Base>)
struct __isinstance__<Derived, Base> : Returns<bool> {
    template <typename>
    struct unary;
    template <typename... Types>
    struct unary<Union<Types...>> {
        static bool operator()(auto&& value) {
            return (isinstance<Types>(std::forward<decltype(value)>(value)) || ...);
        }
    };

    static constexpr bool operator()(Derived obj) {
        if constexpr (impl::py_union<Derived>) {
            return visit(
                []<typename T>(T&& value) -> bool {
                    if constexpr (impl::py_union<Base>) {
                        return unary<std::remove_cvref_t<Base>>{}(std::forward<T>(value));
                    } else {
                        return isinstance<Base>(std::forward<T>(value));
                    }
                },
                std::forward<Derived>(obj)
            );
        } else {
            return unary<std::remove_cvref_t<Base>>{}(std::forward<Derived>(obj));
        }
    }

    template <typename D, typename B>
        requires (impl::BinaryUnionIsInstance<D, B>::enable)
    static constexpr bool operator()(D&& obj, B&& base) {
        return visit(
            []<typename D2, typename B2>(D2&& obj, B2&& base) -> bool {
                if constexpr (std::is_invocable_r_v<bool, __isinstance__<D2, B2>, D2, B2>) {
                    return isinstance(std::forward<D2>(obj), std::forward<B2>(base));
                } else {
                    throw TypeError(
                        "isinstance() arg 2 must be a type, a tuple of types, a "
                        "union, or an object that implements __instancecheck__(), "
                        "not: " + repr(base)
                    );
                }
            },
            std::forward<D>(obj),
            std::forward<B>(base)
        );
    }
};


template <typename Derived, typename Base>
    requires (impl::py_union<Derived> || impl::py_union<Base>)
struct __issubclass__<Derived, Base> : Returns<bool> {
    template <typename, typename>
    struct nullary;
    template <typename... Ds, typename... Bs>
    struct nullary<Union<Ds...>, Union<Bs...>> {
        template <typename D>
        struct helper {
            static constexpr bool operator()() { return (issubclass<D, Bs>() || ...); }
        };
        static constexpr bool operator()() { return (helper<Ds>{}() && ...); }
    };
    template <typename... Ds, typename B>
    struct nullary<Union<Ds...>, B> {
        static constexpr bool operator()() { return (issubclass<Ds, B>() && ...); }
    };
    template <typename D, typename... Bs>
    struct nullary<D, Union<Bs...>> {
        static constexpr bool operator()() { return (issubclass<D, Bs>() || ...); }
    };

    static constexpr bool operator()() {
        return nullary<Derived, Base>{}();
    }

    template <typename>
    struct unary;
    template <typename... Types>
    struct unary<Union<Types...>> {
        template <typename T>
        struct helper {
            static constexpr bool operator()(auto&& value) {
                if constexpr (std::is_invocable_r_v<
                    bool,
                    __issubclass__<T, decltype(value)>,
                    decltype(value)
                >) {
                    return issubclass<T>(std::forward<decltype(value)>(value));
                } else {
                    return false;
                }
            }
        };

        static bool operator()(auto&& value) {
            return (helper<Types>(std::forward<decltype(value)>(value)) || ...);
        }
    };

    template <typename D> requires (impl::UnaryUnionIsSubclass<D, Base>::enable)
    static constexpr bool operator()(D&& obj) {
        if constexpr (impl::py_union<D>) {
            return visit(
                []<typename T>(T&& value) -> bool {
                    if constexpr (impl::py_union<Base>) {
                        return unary<std::remove_cvref_t<Base>>{}(std::forward<T>(value));
                    } else {
                        if constexpr (std::is_invocable_r_v<
                            bool,
                            __issubclass__<T, Base>,
                            T
                        >) {
                            return issubclass<Base>(std::forward<T>(value));
                        } else {
                            throw TypeError(
                                "unary issubclass<" + type_name<Base> +
                                ">() is not enabled for argument of type '" +
                                type_name<T> + "'"
                            );
                        }
                    }
                },
                std::forward<D>(obj)
            );
        } else {
            return unary<std::remove_cvref_t<Base>>{}(std::forward<D>(obj));
        }
    }

    template <typename D, typename B>
        requires (impl::BinaryUnionIsSubclass<D, B>::enable)
    static constexpr bool operator()(D&& obj, B&& base) {
        return visit(
            []<typename D2, typename B2>(D2&& obj, B2&& base) -> bool {
                if constexpr (std::is_invocable_r_v<bool, __issubclass__<D2, B2>, D2, B2>) {
                    return issubclass(std::forward<D2>(obj), std::forward<B2>(base));
                } else {
                    throw TypeError(
                        "binary issubclass() requires a type as the first argument "
                        "and a type, a tuple of types, a union, or an object that "
                        "implements __subclasscheck__() as the second argument"
                    );
                }
            },
            std::forward<D>(obj),
            std::forward<B>(base)
        );
    }
};


template <impl::py_union Self, StaticStr Name>
    requires (impl::UnionGetAttr<Self, Name>::enable)
struct __getattr__<Self, Name> : Returns<typename impl::UnionGetAttr<Self, Name>::type> {
    using type = impl::UnionGetAttr<Self, Name>::type;
    static type operator()(Self self) {
        return std::forward<Self>(self).visit([]<typename T>(T&& value) -> type {
            if constexpr (__getattr__<T, Name>::enable) {
                return getattr<Name>(std::forward<T>(value));
            } else {
                throw AttributeError(
                    "'" + type_name<T> + "' object has no attribute '" + Name + "'"
                );
            }
        });
    }
};


template <impl::py_union Self, StaticStr Name, typename Value>
    requires (impl::UnionSetAttr<Self, Name, Value>::enable)
struct __setattr__<Self, Name, Value> : Returns<void> {
    static void operator()(Self self, Value value) {
        std::forward<Self>(self).visit([]<typename T>(T&& self, Value value) -> void {
            if constexpr (__setattr__<T, Name, Value>::enable) {
                setattr<Name>(
                    std::forward<T>(self),
                    std::forward<Value>(value)
                );
            } else {
                throw AttributeError(
                    "cannot set attribute '" + Name + "' on object of type '" +
                    type_name<T> + "'"
                );
            }
        }, std::forward<Value>(value));
    }
};


template <impl::py_union Self, StaticStr Name>
    requires (impl::UnionDelAttr<Self, Name>::enable)
struct __delattr__<Self, Name> : Returns<void> {
    static void operator()(Self self) {
        std::forward<Self>(self).visit([]<typename T>(T&& self) -> void {
            if constexpr (__delattr__<T, Name>::enable) {
                delattr<Name>(std::forward<Self>(self));
            } else {
                throw AttributeError(
                    "cannot delete attribute '" + Name + "' on object of type '" +
                    type_name<T> + "'"
                );
            }
        }, std::forward<Self>(self));
    }
};


template <impl::py_union Self>
struct __repr__<Self> : Returns<std::string> {
    static std::string operator()(Self self) {
        return std::forward<Self>(self).visit([]<typename T>(T&& self) -> std::string {
            return repr(std::forward<T>(self));
        });
    }
};


template <impl::py_union Self, typename... Args>
    requires (impl::UnionCall<Self, Args...>::enable)
struct __call__<Self, Args...> : Returns<typename impl::UnionCall<Self, Args...>::type> {
    using type = impl::UnionCall<Self, Args...>::type;
    static type operator()(Self self, Args&&... args) {
        return visit(
            []<typename S, typename... A>(S&& self, A&&... args) -> type {
                if constexpr (__call__<S, A...>::enable) {
                    return std::forward<S>(self)(std::forward<A>(args)...);
                } else {
                    throw TypeError(
                        "cannot call object of type '" + type_name<S> +
                        "' with the given arguments"
                    );
                }
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
        return visit(
            []<typename S, typename... K>(S&& self, K&&... key) -> type {
                if constexpr (__getitem__<S, K...>::enable) {
                    return std::forward<S>(self)[std::forward<K>(key)...];
                } else {
                    throw KeyError(
                        "cannot get item with the given key(s) from object of type '" +
                        type_name<S> + "'"
                    );
                }
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
        return visit(
            []<typename S, typename V, typename... K>(
                S&& self,
                V&& value,
                K&&... key
            ) -> void {
                if constexpr (__setitem__<S, V, K...>::enable) {
                    std::forward<S>(self)[std::forward<K>(key)...] = std::forward<V>(value);
                } else {
                    throw KeyError(
                        "cannot set item with the given key(s) on object of type '" +
                        type_name<S> + "'"
                    );
                }
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
        return visit(
            []<typename S, typename... K>(S&& self, K&&... key) -> void {
                if constexpr (__delitem__<S, K...>::enable) {
                    del(std::forward<S>(self)[std::forward<K>(key)...]);
                } else {
                    throw KeyError(
                        "cannot delete item with the given key(s) from object of type '" +
                        type_name<S> + "'"
                    );
                }
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
        return visit(
            []<typename S>(S&& self) -> size_t {
                if constexpr (__hash__<S>::enable) {
                    return hash(std::forward<S>(self));
                } else {
                    throw TypeError(
                        "unhashable type: '" + type_name<S> + "'"
                    );
                }
            },
            std::forward<Self>(self)
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
        return visit(
            []<typename S>(S&& self) -> size_t {
                if constexpr (__len__<S>::enable) {
                    return len(std::forward<S>(self));
                } else {
                    throw TypeError(
                        "object of type '" + type_name<S> + "' has no len()"
                    );
                }
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
        return visit(
            []<typename S, typename K>(S&& self, K&& key) -> bool {
                if constexpr (__contains__<S, K>::enable) {
                    return in(std::forward<K>(key), std::forward<S>(self));
                } else {
                    throw TypeError(
                        "argument of type '" + type_name<K> +
                        "' does not support .in() checks"
                    );
                }
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
        return visit(
            []<typename S>(S&& self) -> type {
                if constexpr (__abs__<S>::enable) {
                    return abs(std::forward<S>(self));
                } else {
                    throw TypeError(
                        "bad operand type for abs(): '" + type_name<S> + "'"
                    );
                }
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionInvert<Self>::enable)
struct __invert__<Self> : Returns<typename impl::UnionInvert<Self>::type> {
    using type = impl::UnionInvert<Self>::type;
    static type operator()(Self self) {
        return visit(
            []<typename S>(S&& self) -> type {
                if constexpr (__invert__<S>::enable) {
                    return ~std::forward<S>(self);
                } else {
                    throw TypeError(
                        "bad operand type for unary ~: '" + type_name<S> + "'"
                    );
                }
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionPos<Self>::enable)
struct __pos__<Self> : Returns<typename impl::UnionPos<Self>::type> {
    using type = impl::UnionPos<Self>::type;
    static type operator()(Self self) {
        return visit(
            []<typename S>(S&& self) -> type {
                if constexpr (__pos__<S>::enable) {
                    return +std::forward<S>(self);
                } else {
                    throw TypeError(
                        "bad operand type for unary +: '" + type_name<S> + "'"
                    );
                }
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionNeg<Self>::enable)
struct __neg__<Self> : Returns<typename impl::UnionNeg<Self>::type> {
    using type = impl::UnionNeg<Self>::type;
    static type operator()(Self self) {
        return visit(
            []<typename S>(S&& self) -> type {
                if constexpr (__neg__<S>::enable) {
                    return -std::forward<S>(self);
                } else {
                    throw TypeError(
                        "bad operand type for unary -: '" + type_name<S> + "'"
                    );
                }
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionIncrement<Self>::enable)
struct __increment__<Self> : Returns<typename impl::UnionIncrement<Self>::type> {
    using type = impl::UnionIncrement<Self>::type;
    static type operator()(Self self) {
        return visit(
            []<typename S>(S&& self) -> type {
                if constexpr (__increment__<S>::enable) {
                    return ++std::forward<S>(self);
                } else {
                    throw TypeError(
                        "'" + type_name<S> + "' object cannot be incremented"
                    );
                }
            },
            std::forward<Self>(self)
        );
    }
};


template <impl::py_union Self> requires (impl::UnionDecrement<Self>::enable)
struct __decrement__<Self> : Returns<typename impl::UnionDecrement<Self>::type> {
    using type = impl::UnionDecrement<Self>::type;
    static type operator()(Self self) {
        return visit(
            []<typename S>(S&& self) -> type {
                if constexpr (__decrement__<S>::enable) {
                    return --std::forward<S>(self);
                } else {
                    throw TypeError(
                        "'" + type_name<S> + "' object cannot be decremented"
                    );
                }
            },
            std::forward<Self>(self)
        );
    }
};


template <typename L, typename R> requires (impl::UnionLess<L, R>::enable)
struct __lt__<L, R> : Returns<typename impl::UnionLess<L, R>::type> {
    using type = impl::UnionLess<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__lt__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) < std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for <: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__le__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) <= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for <=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__eq__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) == std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for ==: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__ne__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) != std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for !=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__ge__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) >= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for >=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__gt__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) > std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for >: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__add__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) + std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for +: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__sub__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) - std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for -: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__mul__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) * std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for *: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename B, typename E>(B&& base, E&& exp) -> type {
                if constexpr (__pow__<B, E>::enable) {
                    return pow(std::forward<B>(base), std::forward<E>(exp));
                } else {
                    throw TypeError(
                        "unsupported operand types for pow(): '" + type_name<B> +
                        "' and '" + type_name<E> + "'"
                    );
                }
            },
            std::forward<Base>(base),
            std::forward<Exp>(exp)
        );
    }
    static type operator()(Base base, Exp exp, Mod mod) {
        return visit(
            []<typename B, typename E, typename M>(
                    B&& base,
                    E&& exp,
                    M&& mod
                ) -> type {
                if constexpr (__pow__<B, E, M>::enable) {
                    return pow(
                        std::forward<B>(base),
                        std::forward<E>(exp),
                        std::forward<M>(mod)
                    );
                } else {
                    throw TypeError(
                        "unsupported operand types for pow(): '" + type_name<B> +
                        "' and '" + type_name<E> + "' and '" + type_name<M> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__truediv__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) / std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for /: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__floordiv__<L2, R2>::enable) {
                    return floordiv(std::forward<L2>(lhs), std::forward<R2>(rhs));
                } else {
                    throw TypeError(
                        "unsupported operand types for floordiv(): '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__mod__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) % std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for %: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__lshift__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) << std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for <<: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__rshift__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) >> std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for >>: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__and__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) & std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for &: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__xor__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) ^ std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for ^: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__or__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) | std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for |: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__iadd__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) += std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for +=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__isub__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) -= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for -=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__imul__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) *= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for *=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename B, typename E>(B&& base, E&& exp) -> type {
                if constexpr (__ipow__<B, E>::enable) {
                    return ipow(std::forward<B>(base), std::forward<E>(exp));
                } else {
                    throw TypeError(
                        "unsupported operand types for ipow(): '" + type_name<B> +
                        "' and '" + type_name<E> + "'"
                    );
                }
            },
            std::forward<Base>(base),
            std::forward<Exp>(exp)
        );
    }
    static type operator()(Base base, Exp exp, Mod mod) {
        return visit(
            []<typename B, typename E, typename M>(
                    B&& base,
                    E&& exp,
                    M&& mod
                ) -> type {
                if constexpr (__ipow__<B, E, M>::enable) {
                    return ipow(
                        std::forward<B>(base),
                        std::forward<E>(exp),
                        std::forward<M>(mod)
                    );
                } else {
                    throw TypeError(
                        "unsupported operand types for ipow(): '" + type_name<B> +
                        "' and '" + type_name<E> + "' and '" + type_name<M> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__itruediv__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) /= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for /=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__ifloordiv__<L2, R2>::enable) {
                    return ifloordiv(std::forward<L2>(lhs), std::forward<R2>(rhs));
                } else {
                    throw TypeError(
                        "unsupported operand types for ifloordiv(): '" +
                        type_name<L2> + "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__imod__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) %= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for %=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__ilshift__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) <<= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for <<=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__irshift__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) >>= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for >>=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__iand__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) &= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for &=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__ixor__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) ^= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for ^=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
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
        return visit(
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                if constexpr (__ior__<L2, R2>::enable) {
                    return std::forward<L2>(lhs) |= std::forward<R2>(rhs);
                } else {
                    throw TypeError(
                        "unsupported operand types for |=: '" + type_name<L2> +
                        "' and '" + type_name<R2> + "'"
                    );
                }
            },
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        );
    }
};


}


#endif
