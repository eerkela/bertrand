#ifndef BERTRAND_CORE_UNION_H
#define BERTRAND_CORE_UNION_H

#include "declarations.h"
#include "object.h"
#include "ops.h"


namespace bertrand {


template <meta::has_python T> requires (!std::same_as<T, NoneType>)
Union(T) -> Union<std::remove_cvref_t<obj<T>>, NoneType>;


namespace meta {

    namespace detail {

        template <typename>
        constexpr bool args_are_convertible_to_python = false;
        template <meta::has_python... Ts>
        constexpr bool args_are_convertible_to_python<bertrand::args<Ts...>> = true;

        template <typename>
        struct to_union;
        template <typename... Matches>
        struct to_union<bertrand::args<Matches...>> {
            template <typename>
            struct unique;
            template <typename M>
            struct unique<bertrand::args<M>> { using type = M; };  // single C++ type
            template <typename... Ms>
            struct unique<bertrand::args<Ms...>> {
                template <typename>
                struct to_python;
                template <typename T>
                struct to_python<bertrand::args<T>> { using type = T; };  // single Python type
                template <typename... Ts>
                struct to_python<bertrand::args<Ts...>> {
                    using type = bertrand::Union<std::remove_cvref_t<Ts>...>;  // multiple Python types
                };
                using type = to_python<
                    typename bertrand::args<meta::python_type<Ms>...>::to_value
                >::type;
            };
            using type = unique<typename bertrand::args<Matches...>::to_value>::type;
        };

    }  // namespace detail

    /* Converting a pack to a union involves converting all C++ types to their Python
    equivalents, then deduplicating the results.  If the final pack contains only a
    single type, then that type is returned directly, rather than instantiating a new
    Union. */
    template <args T> requires (detail::args_are_convertible_to_python<T>)
    using to_union = detail::to_union<std::remove_cvref_t<T>>::type;

}  // namespace meta


namespace impl {

    /* Raw types are converted to a pack of length 1. */
    template <typename T>
    struct union_traits {
        using type = T;
        using pack = args<T>;
    };

    /* Unions are converted into a pack of the same length. */
    template <meta::Union T>
    struct union_traits<T> {
    private:
        template <typename>
        struct _pack;
        template <typename... Types>
        struct _pack<Union<Types...>> {
            using type = args<meta::qualify<Types, T>...>;
        };

    public:
        using type = T;
        using pack = _pack<std::remove_cvref_t<T>>::type;
    };

    /* `std::variant`s are treated like unions. */
    template <meta::is_variant T>
    struct union_traits<T> {
    private:
        template <typename>
        struct _pack;
        template <typename... Types>
        struct _pack<std::variant<Types...>> {
            using type = args<meta::qualify<Types, T>...>;
        };

    public:
        using type = T;
        using pack = _pack<std::remove_cvref_t<T>>::type;
    };

}  // namespace impl


namespace meta {

    namespace detail {

        template <typename Func, typename... Args>
        struct exhaustive {
            template <bool>
            struct deduce { using type = void; };
            template <>
            struct deduce<true> { using type = std::invoke_result_t<Func, Args...>; };

            static constexpr bool enable = std::is_invocable_v<Func, Args...>;
            using type = deduce<enable>::type;
        };
        template <typename Func, typename... Args> requires (meta::Union<Args> || ...)
        struct exhaustive<Func, Args...> {
            // 1. Convert arguments to a 2D pack of packs representing all possible
            //    permutations of the argument types.
            template <typename...>
            struct permute { using type = bertrand::args<>; };
            template <typename First, typename... Rest>
            struct permute<First, Rest...> {
                using type = impl::union_traits<First>::pack::template product<
                    typename impl::union_traits<Rest>::pack...
                >;
            };
            using permutations = permute<Args...>::type;

            // 2. Analyze each permutation and assert that the function is invocable
            //    with the given arguments, and returns a common type for each case.
            template <typename>
            struct check;
            template <typename... permutations>
            struct check<bertrand::args<permutations...>> {

                // 2a. Determine if the function is invocable with the permuted
                //     arguments and get its return type if so.
                template <typename>
                struct invoke { static constexpr bool enable = false; };
                template <typename... A> requires (std::is_invocable_v<Func, A...>)
                struct invoke<bertrand::args<A...>> {
                    static constexpr bool enable = true;
                    using type = std::invoke_result_t<Func, A...>;
                };

                // 2b. Apply (2a) to all permutations and ensure a common return type
                //     exists.
                template <typename...>
                static constexpr bool common_return = false;
                template <typename... Ps> requires (invoke<Ps>::enable && ...)
                static constexpr bool common_return<Ps...> =
                    meta::has_common_type<typename invoke<Ps>::type...>;

                // 2c. Determine the common return type for all permutations.
                template <bool>
                struct deduce { using type = void; };
                template <>
                struct deduce<true> { using type =
                    meta::common_type<typename invoke<permutations>::type...>;
                };

                static constexpr bool enable = common_return<permutations...>;
                using type = deduce<enable>::type;
            };

            static constexpr bool enable = check<permutations>::enable;
            using type = check<permutations>::type;
        };

    }  // namespace detail

    /* A visitor function can only be applied to a set of unions if it is invocable
    with all possible permutations of the component argument types. */
    template <typename Func, typename... Args>
    static constexpr bool exhaustive = detail::exhaustive<Func, Args...>::enable;

    /* A visitor function returns the common type to which all permutation results
    are mutually convertible. */
    template <typename Func, typename... Args> requires (exhaustive<Func, Args...>)
    using visit_returns = detail::exhaustive<Func, Args...>::type;

}  // namespace meta


namespace impl {

    /* The visit() algorithm works by constructing a series of compile-time vtables
    storing function pointers for all possible permutations of the argument types.  All
    that is required at runtime is a simple index into the vtable using the union's
    current active index, similar to a virtual function call. */
    template <typename, typename...>
    struct visit_helper;
    template <typename... Prev>
    struct visit_helper<args<Prev...>> {
        // Base case: no more arguments to visit - invoke the visitor function.
        template <typename visitor> requires (std::is_invocable_v<visitor, Prev...>)
        static decltype(auto) operator()(visitor&& visit, Prev... prev) {
            return std::forward<visitor>(visit)(std::forward<Prev>(prev)...);
        }
    };
    template <typename... Prev, typename Curr, typename... Next>
    struct visit_helper<args<Prev...>, Curr, Next...> {
        // 1. construct a vtable for the current argument type
        template <size_t I, typename visitor>
            requires (meta::exhaustive<visitor, Prev..., Curr, Next...>)
        struct VTable {
            // 1a. determine the common return type for the visitor across all
            //     permutations
            using Return = meta::visit_returns<visitor, Prev..., Curr, Next...>;

            // 1b. construct a function pointer for each permutation, which will
            //     reinterpret the union as the correct type and recursively call the
            //     outer visit_helper to advance to the next argument.  Separate
            //     overloads are provided for Python unions and std::variants.
            static constexpr auto python() -> Return(*)(visitor, Prev..., Curr, Next...) {
                constexpr auto callback = [](
                    visitor visit,
                    Prev... prev,
                    Curr curr,
                    Next... next
                ) -> Return {
                    using T = std::remove_cvref_t<Curr>::template at<I>;
                    using Q = meta::qualify<T, Curr>;
                    if constexpr (meta::lvalue<Curr>) {
                        T value = borrow<T>(ptr(curr));
                        return visit_helper<args<Prev..., Q>, Next...>{}(
                            std::forward<visitor>(visit),
                            std::forward<Prev>(prev)...,
                            value,
                            std::forward<Next>(next)...
                        );
                    } else {
                        return visit_helper<args<Prev..., Q>, Next...>{}(
                            std::forward<visitor>(visit),
                            std::forward<Prev>(prev)...,
                            borrow<T>(ptr(curr)),
                            std::forward<Next>(next)...
                        );
                    }
                };
                return +callback;
            };
            static constexpr auto variant() -> Return(*)(visitor, Prev..., Curr, Next...) {
                constexpr auto callback = [](
                    visitor visit,
                    Prev... prev,
                    Curr curr,
                    Next... next
                ) -> Return {
                    using T = std::variant_alternative_t<I, std::remove_cvref_t<Curr>>;
                    using Q = meta::qualify<T, Curr>;
                    if constexpr (
                        meta::lvalue<Curr> ||
                        meta::is_const<std::remove_reference_t<Curr>>
                    ) {
                        return visit_helper<args<Prev..., Q>, Next...>{}(
                            std::forward<visitor>(visit),
                            std::forward<Prev>(prev)...,
                            std::get<I>(curr),
                            std::forward<Next>(next)...
                        );
                    } else {
                        return visit_helper<args<Prev..., Q>, Next...>{}(
                            std::forward<visitor>(visit),
                            std::forward<Prev>(prev)...,
                            std::move(std::get<I>(curr)),
                            std::forward<Next>(next)...
                        );
                    }
                };
                return +callback;
            }
        };

        // 2. trigger the dispatch algorithm, unwrapping the correct union type for
        //    every argument and invoking the visitor with the correct permutation
        template <typename visitor>
        static decltype(auto) operator()(
            visitor&& visit,
            Prev... prev,
            Curr curr,
            Next... next
        ) {
            // 2a. build and invoke a vtable for `bertrand::Union` types
            if constexpr (meta::Union<Curr>) {
                constexpr auto vtable = []<size_t... Is>(std::index_sequence<Is...>) {
                    return std::array{VTable<Is, visitor>::python()...};
                }(std::make_index_sequence<std::remove_cvref_t<Curr>::size()>{});

                return vtable[curr.index()](
                    std::forward<visitor>(visit),
                    std::forward<Prev>(prev)...,
                    std::forward<Curr>(curr),
                    std::forward<Next>(next)...
                );

            // 2b. build and invoke a vtable for `std::variant` types
            } else if constexpr (meta::is_variant<Curr>) {
                constexpr auto vtable = []<size_t... Is>(std::index_sequence<Is...>) {
                    return std::array{VTable<Is, visitor>::variant()...};
                }(std::make_index_sequence<std::variant_size_v<std::remove_cvref_t<Curr>>>{});

                return vtable[curr.index()](
                    std::forward<visitor>(visit),
                    std::forward<Prev>(prev)...,
                    std::forward<Curr>(curr),
                    std::forward<Next>(next)...
                );

            // 2c. pass through non-union types as-is, without any vtables
            } else {
                return visit_helper<args<Prev..., Curr>, Next...>{}(
                    std::forward<visitor>(visit),
                    std::forward<Prev>(prev)...,
                    std::forward<Curr>(curr),
                    std::forward<Next>(next)...
                );
            }
        }
    };

}  // namespace impl


/* Non-member `bertrand::visit(visitor, args...)` operator, similar to `std::visit()`.
A member version of this operator is implemented for `Union` objects, which allows for
chaining.

The `visitor` object is a temporary that is constructed from either a single function
or a set of functions enclosed in an initializer list, emulating the overload pattern.
The `args` are the arguments to be passed to the visitor, which must contain at least
one union type.  The visitor must be callable for all possible permutations of the
unions in `args`, and the return type must be consistent across each one.  The
arguments will be perfectly forwarded in the same order as they are given, with each
union unwrapped to its actual type.  The order of the unions is irrelevant, and
non-union arguments can be interspersed freely between them. */
template <typename Func, typename... Args> requires (meta::exhaustive<Func, Args...>)
auto visit(Func&& visitor, Args&&... args) -> meta::visit_returns<Func, Args...> {
    return impl::visit_helper<bertrand::args<>, Args...>{}(
        std::forward<Func>(visitor),
        std::forward<Args>(args)...
    );
}


namespace impl {

    /* Allow implicit conversion from the union type if and only if all of its
    qualified members are convertible to that type. */
    template <meta::Union U>
    struct UnionToType {
        template <typename>
        struct convertible;
        template <typename... Types>
        struct convertible<Union<Types...>> {
            template <typename Out>
            static constexpr bool implicit =
                (std::convertible_to<meta::qualify<Types, U>, Out> && ...);
            template <typename Out>
            static constexpr bool convert =
                (meta::explicitly_convertible_to<meta::qualify<Types, U>, Out> || ...);
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
    template <meta::is_variant V>
    struct VariantToUnion {
        template <typename>
        struct convertible {
            static constexpr bool enable = false;
            using type = void;
            template <typename...>
            static constexpr bool convert = false;
        };
        template <meta::has_python... Types>
        struct convertible<std::variant<Types...>> {
            static constexpr bool enable = true;
            /// TODO: not sure if converting to Python here is the right move.  The
            /// to_union logic should do this automatically, no?
            using type = meta::to_union<args<meta::python_type<Types>...>>;
            template <typename, typename... Ts>
            static constexpr bool _convert = true;
            template <typename... To, typename T, typename... Ts>
            static constexpr bool _convert<args<To...>, T, Ts...> =
                (std::convertible_to<meta::qualify<T, V>, To> || ...) &&
                _convert<args<To...>, Ts...>;
            template <typename... Ts>
            static constexpr bool convert = _convert<args<Types...>, Ts...>;
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
            requires (sizeof...(Args) > 0 && (meta::Union<Args> || ...))
        struct op<Args...> {
        private:

            /* 1. Convert the input arguments into a sequence of packs, where unions,
            optionals, and variants are represented as packs of length > 1, then
            compute the cartesian product. */
            template <typename>
            struct _product;
            template <typename First, typename... Rest>
            struct _product<args<First, Rest...>> {
                using type = First::template product<Rest...>;
            };
            using product = _product<args<typename union_traits<Args>::pack...>>::type;

            /* 2. Produce an output pack containing all of the valid return types for
            each permutation that satisfies the control structure. */
            template <typename>
            struct traits;
            template <typename... Permutations>
            struct traits<args<Permutations...>> {
                template <typename out, typename...>
                struct _returns { using type = out; };
                template <typename... out, typename P, typename... Ps>
                struct _returns<args<out...>, P, Ps...> {
                    template <typename>
                    struct filter { using type = args<out...>; };
                    template <typename P2> requires (P2::template enable<control>)
                    struct filter<P2> {
                        using type = args<out..., typename P2::template type<control>>;
                    };
                    using type = _returns<typename filter<P>::type, Ps...>::type;
                };
                using returns = _returns<args<>, Permutations...>::type;
            };

            /* 3. Deduplicate the return types, merging those that only differ in
            cvref qualifications, which forces a copy/move when called. */
            using returns = traits<product>::returns::to_value;

        public:

            /* 4. Enable the operation if and only if at least one permutation
            satisfies the control structure, and thus has a valid return type. */
            static constexpr bool enable = returns::size() > 0;

            /* 5. If there is only one valid return type, return it directly, otherwise
            construct a new union and convert all of the results to Python. */
            using type = meta::to_union<returns>;
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

    template <typename, static_str>
    struct UnionGetAttr { static constexpr bool enable = false; };
    template <meta::Union Self, static_str Name>
    struct UnionGetAttr<Self, Name> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename... Types>
            requires (__getattr__<meta::qualify_lvalue<Types, Self>, Name>::enable || ...)
        struct traits<Union<Types...>> {
            template <typename result, typename... Ts>
            struct unary { using type = result; };
            template <typename... Matches, typename T, typename... Ts>
            struct unary<args<Matches...>, T, Ts...> {
                template <typename>
                struct conditional { using type = args<Matches...>; };
                template <typename T2>
                    requires (__getattr__<meta::qualify_lvalue<T2, Self>, Name>::enable)
                struct conditional<T2> {
                    using type = args<
                        Matches...,
                        typename __getattr__<meta::qualify_lvalue<T2, Self>, Name>::type
                    >;
                };
                using type = unary<typename conditional<T>::type, Ts...>::type;
            };
            static constexpr bool enable = true;
            using type = meta::to_union<typename unary<args<>, Types...>::type>;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = traits<std::remove_cvref_t<Self>>::type;
    };

    template <typename, static_str, typename>
    struct UnionSetAttr { static constexpr bool enable = false; };
    template <meta::Union Self, static_str Name, typename Value>
    struct UnionSetAttr<Self, Name, Value> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__setattr__<meta::qualify_lvalue<Types, Self>, Name, Value>::enable || ...)
        static constexpr bool match<Union<Types...>> = true;
        static constexpr bool enable = match<std::remove_cvref_t<Self>>;
        using type = void;
    };

    template <typename, static_str>
    struct UnionDelAttr { static constexpr bool enable = false; };
    template <meta::Union Self, static_str Name>
    struct UnionDelAttr<Self, Name> {
        template <typename>
        static constexpr bool match = false;
        template <typename... Types>
            requires (__delattr__<meta::qualify_lvalue<Types, Self>, Name>::enable || ...)
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
struct interface<Union<Types...>> : impl::union_tag {
    static constexpr size_t size() noexcept { return sizeof...(Types); }

    template <size_t I> requires (I < size())
    using at = meta::unpack_type<I, Types...>;

    template <typename T>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return (std::same_as<T, Types> || ...);
    }

    template <typename T> requires (contains<T>())
    [[nodiscard]] bool holds_alternative(this auto&& self) {
        return self.index() == meta::index_of<T, Types...>;
    }

    template <typename T> requires (contains<T>())
    [[nodiscard]] auto get(this auto&& self) -> T {
        if (self.index() != meta::index_of<T, Types...>) {
            throw TypeError(
                "bad union access: '" + type_name<T> + "' is not the active type"
            );
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<T>(ptr(self));
        } else {
            return steal<T>(release(self));
        }
    }

    template <size_t I> requires (I < size())
    [[nodiscard]] auto get(this auto&& self) -> at<I> {
        if (self.index() != I) {
            throw TypeError(
                "bad union access: '" + type_name<at<I>> + "' is not the active type"
            );
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<at<I>>(ptr(self));
        } else {
            return steal<at<I>>(release(self));
        }
    }

    template <typename T> requires (contains<T>())
    [[nodiscard]] auto get_if(this auto&& self) -> Optional<T> {
        if (self.index() != meta::index_of<T, Types...>) {
            return None;
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<T>(ptr(self));
        } else {
            return steal<T>(release(self));
        }
    }

    template <size_t I> requires (I < size())
    [[nodiscard]] auto get_if(this auto&& self) -> Optional<at<I>> {
        if (self.index() != I) {
            return None;
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<at<I>>(ptr(self));
        } else {
            return steal<at<I>>(release(self));
        }
    }

    template <typename Self, typename Func, typename... Args>
        requires (meta::exhaustive<Func, Self, Args...>)
    decltype(auto) visit(
        this Self&& self,
        Func&& visitor,
        Args&&... args
    ) {
        return bertrand::visit(
            std::forward<Func>(visitor),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }
};


template <typename... Types>
struct interface<Type<Union<Types...>>> {
private:
    using type = interface<Union<Types...>>;

public:
    static constexpr size_t size() noexcept { return type::size(); }

    template <size_t I> requires (I < size())
    using at = type::template at<I>;

    template <typename T>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return type::template contains<T>();
    }

    template <typename T, meta::inherits<type> Self> requires (contains<T>())
    [[nodiscard]] bool holds_alternative(Self&& self) {
        return std::forward<Self>(self).template holds_alternative<T>();
    }

    template <typename T, meta::inherits<type> Self> requires (contains<T>())
    [[nodiscard]] static auto get(Self&& self) -> T {
        return std::forward<Self>(self).template get<T>();
    }

    template <size_t I, meta::inherits<type> Self> requires (I < size())
    [[nodiscard]] static auto get(Self&& self) -> at<I> {
        return std::forward<Self>(self).template get<I>();
    }

    template <typename T, meta::inherits<type> Self> requires (contains<T>())
    [[nodiscard]] static auto get_if(Self&& self) -> Optional<T> {
        return std::forward<Self>(self).template get_if<T>();
    }

    template <size_t I, meta::inherits<type> Self> requires (I < size())
    [[nodiscard]] static auto get_if(Self&& self) -> Optional<at<I>> {
        return std::forward<Self>(self).template get_if<I>();
    }

    template <meta::inherits<type> Self, typename Func, typename... Args>
        requires (meta::exhaustive<Func, Self, Args...>)
    static decltype(auto) visit(
        Self&& self,
        Func&& visitor,
        Args&&... args
    ) {
        return bertrand::visit(
            std::forward<Func>(visitor),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }
};


template <typename T>
struct interface<Optional<T>> : impl::optional_tag {
private:

    template <typename R>
    struct Result { using type = Optional<std::remove_cvref_t<meta::python_type<R>>>; };
    template <meta::Optional R>
    struct Result<R> { using type = std::remove_cvref_t<R>; };

public:
    static constexpr size_t size() noexcept { return 2; }

    template <size_t I> requires (I < size())
    using at = std::conditional_t<I == 0, T, NoneType>;

    template <typename U>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return std::same_as<U, T> || std::same_as<U, NoneType>;
    }

    template <typename U> requires (contains<U>())
    [[nodiscard]] bool holds_alternative(this auto&& self) {
        return self.index() == std::same_as<U, NoneType>;
    }

    template <typename U> requires (contains<U>())
    [[nodiscard]] U get(this auto&& self) {
        if (self.index() != std::same_as<U, NoneType>) {
            throw TypeError(
                "bad union access -> '" + type_name<U> + "' is not the active type"
            );
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<U>(ptr(self));
        } else {
            return steal<U>(release(self));
        }
    }

    template <size_t I> requires (I < size())
    [[nodiscard]] at<I> get(this auto&& self) {
        if (self.index() != I) {
            throw TypeError(
                "bad union access: '" + type_name<at<I>> + "' is not the active type"
            );
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<at<I>>(ptr(self));
        } else {
            return steal<at<I>>(release(self));
        }
    }

    template <typename U> requires (contains<U>())
    [[nodiscard]] Optional<U> get_if(this auto&& self) {
        if (self.index() != std::same_as<U, NoneType>) {
            return None;
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<U>(ptr(self));
        } else {
            return steal<U>(release(self));
        }
    }

    template <size_t I> requires (I < size())
    [[nodiscard]] auto get_if(this auto&& self) -> Optional<at<I>> {
        if (self.index() != I) {
            return None;
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<at<I>>(ptr(self));
        } else {
            return steal<at<I>>(release(self));
        }
    }

    template <typename Self, typename Func, typename... Args>
        requires (meta::exhaustive<Func, Self, Args...>)
    decltype(auto) visit(
        this Self&& self,
        Func&& visitor,
        Args&&... args
    ) {
        return bertrand::visit(
            std::forward<Func>(visitor),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }

    [[nodiscard]] bool has_value(this auto&& self) {
        return !self.index();
    }

    [[nodiscard]] T value(this auto&& self) {
        if (self.index()) {
            throw TypeError(
                "bad union access -> '" + type_name<T> + "' is not the active type"
            );
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<T>(ptr(self));
        } else {
            return steal<T>(release(self));
        }
    }

    template <std::convertible_to<T> U>
    [[nodiscard]] T value_or(this auto&& self, U&& val) {
        if (self.index()) {
            return std::forward<U>(val);
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return borrow<T>(ptr(self));
        } else {
            return steal<T>(release(self));
        }
    }

    template <std::invocable<T> F> requires (meta::has_python<std::invoke_result_t<F, T>>)
    [[nodiscard]] auto and_then(this auto&& self, F&& f)
        -> Result<std::invoke_result_t<F, T>>::type
    {
        if (self.index()) {
            return None;
        }
        if constexpr (meta::lvalue<decltype(self)>) {
            return std::forward<F>(f)(borrow<T>(ptr(self)));
        } else {
            return std::forward<F>(f)(steal<T>(release(self)));
        }
    }

    template <std::invocable<> F> requires (meta::has_python<std::invoke_result_t<F>>)
    [[nodiscard]] auto or_else(this auto&& self, F&& f)
        -> Result<std::invoke_result_t<F>>::type
    {
        if (!self.index()) {
            return None;
        }
        return std::forward<F>(f)();
    }
};


template <typename T>
struct interface<Type<Optional<T>>> {
private:
    using type = interface<Optional<T>>;

public:
    static constexpr size_t size() noexcept { return type::size(); }

    template <size_t I> requires (I < size())
    using at = type::template at<I>;

    template <typename U>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return type::template contains<U>();
    }

    template <typename U, meta::inherits<type> Self> requires (contains<U>())
    [[nodiscard]] decltype(auto) holds_alternative(Self&& self) {
        return std::forward<Self>(self).template holds_alternative<T>();
    }

    template <typename U, meta::inherits<type> Self> requires (contains<U>())
    [[nodiscard]] static decltype(auto) get(Self&& self) {
        return std::forward<Self>(self).template get<U>();
    }

    template <size_t I, meta::inherits<type> Self> requires (I < size())
    [[nodiscard]] static decltype(auto) get(Self&& self) {
        return std::forward<Self>(self).template get<I>();
    }

    template <typename U, meta::inherits<type> Self> requires (contains<U>())
    [[nodiscard]] static decltype(auto) get_if(Self&& self) {
        return std::forward<Self>(self).template get_if<U>();
    }

    template <size_t I, meta::inherits<type> Self> requires (I < size())
    [[nodiscard]] static decltype(auto) get_if(Self&& self) {
        return std::forward<Self>(self).template get_if<I>();
    }

    template <meta::inherits<type> Self, typename Func, typename... Args>
        requires (meta::exhaustive<Func, Self, Args...>)
    static decltype(auto) visit(
        Self&& self,
        Func&& visitor,
        Args&&... args
    ) {
        return bertrand::visit(
            std::forward<Func>(visitor),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }

    template <meta::inherits<type> Self>
    [[nodiscard]] static decltype(auto) has_value(Self&& self) {
        return std::forward<Self>(self).has_value();
    }

    template <meta::inherits<type> Self>
    [[nodiscard]] static decltype(auto) value(Self&& self) {
        return std::forward<Self>(self).value();
    }

    template <meta::inherits<type> Self, std::convertible_to<T> U>
    [[nodiscard]] static decltype(auto) value_or(Self&& self, U&& val) {
        return std::forward<Self>(self).value_or(std::forward<U>(val));
    }

    template <meta::inherits<type> Self, std::invocable<T> F>
        requires (meta::has_python<std::invoke_result_t<F, T>>)
    [[nodiscard]] static decltype(auto) and_then(Self&& self, F&& f) {
        return std::forward<Self>(self).and_then(std::forward<F>(f));
    }

    template <meta::inherits<type> Self, std::invocable<> F>
        requires (meta::has_python<std::invoke_result_t<F>>)
    [[nodiscard]] static decltype(auto) or_else(Self&& self, F&& f) {
        return std::forward<Self>(self).or_else(std::forward<F>(f));
    }
};


template <meta::python... Types>
    requires (
        sizeof...(Types) > 1 &&
        (!meta::is_qualified<Types> && ...) &&
        meta::types_are_unique<Types...> &&
        !(meta::Union<Types> || ...)
    )
struct Union : Object, interface<Union<Types...>> {
protected:
    struct infer_t {};
    struct inplace_t { size_t index; };

    template <size_t I, typename From>
    struct infer_index {
        static size_t operator()(
            From obj,
            PyTypeObject* type,
            size_t subclass_idx
        ) {
            if constexpr (meta::is<From, Union>) {
                return obj.index();
            } else {
                if (subclass_idx == sizeof...(Types)) {
                    throw TypeError(
                        "cannot convert Python object from type '" +
                        demangle(type->tp_name) + "' to type '" +
                        type_name<Union> + "'"
                    );
                }
                return subclass_idx;
            }
        }
    };
    template <size_t I, typename From> requires (I < sizeof...(Types))
    struct infer_index<I, From> {
        static size_t operator()(
            From obj,
            PyTypeObject* type,
            size_t subclass_idx
        ) {
            if constexpr (meta::is<From, Union>) {
                return obj.index();
            } else {
                using T = meta::unpack_type<I, Types...>;
                if (Type<T>().is(reinterpret_cast<PyObject*>(type))) {
                    return I;
                }
                if (subclass_idx == sizeof...(Types) && isinstance<T>(obj)) {
                    return infer_index<I + 1, From>{}(std::forward<From>(obj), type, I);
                }
                return infer_index<I + 1, From>{}(std::forward<From>(obj), type, subclass_idx);
            }
        }
    };

    size_t m_index;

    template <typename T>
    Union(T&& value, inplace_t t) :
        Object(std::forward<T>(value)),
        m_index(t.index)
    {}

    template <typename T>
    Union(T&& value, infer_t) :
        Object(std::forward<T>(value)),
        m_index(infer_index<0, T>{}(
            borrow<std::remove_cvref_t<T>>(ptr(*this)),
            Py_TYPE(Object::m_ptr),
            sizeof...(Types)
        ))
    {}

public:
    struct __python__ : cls<__python__, Union>, PyObject {
        static constexpr static_str __doc__ =
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

        template <static_str ModName>
        static Type<Union> __export__(Module<ModName>& mod);
        static Type<Union> __import__();
    };

    Union(PyObject* p, borrowed_t t) : Union(Object(p, t), infer_t{}) {}
    Union(PyObject* p, stolen_t t) : Union(Object(p, t), infer_t{}) {}

    template <typename Self = Union> requires (__initializer__<Self>::enable)
    Union(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Union(__initializer__<Self>{}(init), infer_t{})
    {}

    template <typename... Args> requires (implicit_ctor<Union>::template enable<Args...>)
    Union(Args&&... args) : Union(
        implicit_ctor<Union>{}(std::forward<Args>(args)...),
        infer_t{}
    ) {}

    template <typename... Args> requires (explicit_ctor<Union>::template enable<Args...>)
    explicit Union(Args&&... args) : Union(
        explicit_ctor<Union>{}(std::forward<Args>(args)...),
        infer_t{}
    ) {}

    /* Construct the union in-place by calling a consituent constructor. */
    template <typename T, typename... Args>
        requires ((std::same_as<T, Types> || ...) && std::constructible_from<T, Args...>)
    [[nodiscard]] static Union construct(Args&&... args) {
        return Union(
            T(std::forward<Args>(args)...),
            inplace_t{meta::index_of<T, Types...>}
        );
    }

    /* Get the index of the current active element in the union. */
    [[nodiscard]] size_t index(this auto&& self) noexcept { return self.m_index; }
};


template <typename... Ts>
struct __template__<Union<Ts...>>                           : returns<Object> {
    static Object operator()() {
        PyObject* result = PyTuple_Pack(
            sizeof...(Ts),
            ptr(Type<Ts>())...
        );
        if (result == nullptr) {
            Exception::to_python();
        }
        return steal<Object>(result);
    }
};


/* Initializer list constructor is only enabled for `Optional<T>`, and not for any
other form of union, where such a call might be ambiguous. */
template <typename T> requires (__initializer__<T>::enable)
struct __initializer__<Optional<T>>                        : returns<Optional<T>> {
    using Element = __initializer__<T>::type;
    static Optional<T> operator()(const std::initializer_list<Element>& init) {
        return Optional<T>::template construct<T>(init);
    }
};


/* Default constructor is enabled as long as at least one of the member types is
default constructible, in which case the first such type is initialized.  If NoneType
is present in the union, it is preferred over any other type. */
template <typename... Ts> requires (std::is_default_constructible_v<Ts> || ...)
struct __init__<Union<Ts...>>                               : returns<Union<Ts...>> {
    template <typename...>
    struct constructible { using type = void; };
    template <typename U, typename... Us>
    struct constructible<U, Us...> { using type = constructible<Us...>::type; };
    template <std::default_initializable U, typename... Us>
    struct constructible<U, Us...> { using type = U; };

    static Union<Ts...> operator()() {
        if constexpr ((std::same_as<NoneType, Ts> || ...)) {
            return Union<Ts...>::template construct<NoneType>();
        } else {
            return Union<Ts...>::template construct<typename constructible<Ts...>::type>();
        }
    }
};


/* Explicit constructor calls are only allowed for `Optional<T>`, and not for any
other form of union, where such a call might be ambiguous. */
template <typename T, typename... Args>
    requires (sizeof...(Args) > 0 && std::constructible_from<T, Args...>)
struct __init__<Optional<T>, Args...>                       : returns<Optional<T>> {
    static Optional<T> operator()(Args&&... args) {
        return Optional<T>::template construct<T>(std::forward<Args>(args)...);
    }
};


/* Universal conversion from any type that is convertible to one or more types within
the union.  Prefers exact matches (and therefore copy/move semantics) over secondary
conversions, and always converts to the first matching type within the union */
template <typename From, typename... Ts>
    requires (!meta::Union<From> && (std::convertible_to<From, Ts> || ...))
struct __cast__<From, Union<Ts...>>                         : returns<Union<Ts...>> {
    template <typename T, typename...>
    struct match { using type = void; };
    template <typename T, typename U, typename... Us>
    struct match<T, U, Us...> { using type = match<Us...>::type; };
    template <typename T, std::same_as<T> U, typename... Us>
    struct match<T, U, Us...> { using type = U; };

    template <typename T, typename...>
    struct convert { using type = void; };
    template <typename T, typename U, typename... Us>
    struct convert<T, U, Us...> { using type = convert<Us...>::type; };
    template <typename T, typename U, typename... Us> requires (std::convertible_to<T, U>)
    struct convert<T, U, Us...> { using type = U; };

    static Union<Ts...> operator()(From from) {
        using F = std::remove_cvref_t<meta::python_type<From>>;
        if constexpr (!std::is_void_v<typename match<F, Ts...>::type>) {
            return Union<Ts...>::template construct<typename match<F, Ts...>::type>(
                std::forward<From>(from)
            );
        } else {
            return Union<Ts...>::template construct<typename convert<From, Ts...>::type>(
                std::forward<From>(from)
            );
        }
    }
};


/* Universal implicit conversion from a union to any type for which ALL elements of the
union are implicitly convertible.  This covers conversions to `std::variant` provided
all types are accounted for, as well as to `std::optional` and pointer types whereby
`NoneType` is convertible to `std::nullopt` and `nullptr`, respectively. */
template <meta::Union From, typename To>
    requires (!meta::Union<To> && impl::UnionToType<From>::template implicit<To>)
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From from) {
        return std::forward<From>(from).visit([](auto&& value) -> To {
            return std::forward<decltype(value)>(value);
        });
    }
};


template <meta::is_variant T> requires (impl::VariantToUnion<T>::enable)
struct __cast__<T> : returns<typename impl::VariantToUnion<T>::type> {};
template <meta::is_optional T> requires (meta::has_python<meta::optional_type<T>>)
struct __cast__<T> : returns<
    Optional<meta::python_type<std::remove_cv_t<meta::optional_type<T>>>>
> {};
template <meta::has_python T> requires (meta::python<T> || (
    meta::has_cpp<meta::python_type<T>> && std::same_as<
        std::remove_cv_t<T>,
        meta::cpp_type<meta::python_type<std::remove_cv_t<T>>>
    >
))
struct __cast__<T*> : returns<
    Optional<meta::python_type<std::remove_cv_t<T>>>
> {};
template <meta::is_shared_ptr T> requires (meta::has_python<meta::shared_ptr_type<T>>)
struct __cast__<T> : returns<
    Optional<meta::python_type<std::remove_cv_t<meta::shared_ptr_type<T>>>>
> {};
template <meta::is_unique_ptr T> requires (meta::has_python<meta::unique_ptr_type<T>>)
struct __cast__<T> : returns<
    Optional<meta::python_type<std::remove_cv_t<meta::unique_ptr_type<T>>>>
> {};


template <meta::is_variant From, std::derived_from<Object> To>
    requires (impl::VariantToUnion<From>::template convert<To>)
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From value) {
        return std::visit(
            []<typename T>(T&& value) -> To {
                return std::forward<T>(value);
            },
            std::forward<From>(value)
        );
    }
};


template <meta::is_variant From, typename... Ts>
    requires (impl::VariantToUnion<From>::template convert<Ts...>)
struct __cast__<From, Union<Ts...>>                         : returns<Union<Ts...>> {
    template <typename T, typename...>
    struct match { using type = void; };
    template <typename T, typename U, typename... Us>
    struct match<T, U, Us...> { using type = match<Us...>::type; };
    template <typename T, std::same_as<T> U, typename... Us>
    struct match<T, U, Us...> { using type = U; };

    template <typename T, typename...>
    struct convert { using type = void; };
    template <typename T, typename U, typename... Us>
    struct convert<T, U, Us...> { using type = convert<Us...>::type; };
    template <typename T, typename U, typename... Us> requires (std::convertible_to<T, U>)
    struct convert<T, U, Us...> { using type = U; };

    static Union<Ts...> operator()(From value) {
        return std::visit(
            []<typename T>(T&& value) -> Union<Ts...> {
                using F = std::remove_cvref_t<meta::python_type<T>>;
                if constexpr (!std::is_void_v<typename match<F, Ts...>::type>) {
                    return Union<Ts...>::template construct<typename match<F, Ts...>::type>(
                        std::forward<T>(value)
                    );
                } else {
                    return Union<Ts...>::template construct<typename convert<T, Ts...>::type>(
                        std::forward<T>(value)
                    );
                }
            },
            std::forward<From>(value)
        );
    }
};


template <meta::is_optional From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<meta::qualify<meta::optional_type<From>, From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : returns<Union<Ts...>> {
    using T = meta::qualify<meta::optional_type<From>, From>;

    template <typename T, typename...>
    struct match { using type = void; };
    template <typename T, typename U, typename... Us>
    struct match<T, U, Us...> { using type = match<Us...>::type; };
    template <typename T, std::same_as<T> U, typename... Us>
    struct match<T, U, Us...> { using type = U; };

    template <typename T, typename...>
    struct convert { using type = void; };
    template <typename T, typename U, typename... Us>
    struct convert<T, U, Us...> { using type = convert<Us...>::type; };
    template <typename T, typename U, typename... Us> requires (std::convertible_to<T, U>)
    struct convert<T, U, Us...> { using type = U; };

    static Union<Ts...> operator()(From from) {
        if (from.has_value()) {
            using F = std::remove_cvref_t<meta::python_type<T>>;
            if constexpr (!std::is_void_v<typename match<F, Ts...>::type>) {
                return Union<Ts...>::template construct<typename match<F, Ts...>::type>(
                    *std::forward<From>(from)
                );
            } else {
                return Union<Ts...>::template construct<typename convert<T, Ts...>::type>(
                    *std::forward<From>(from)
                );
            }
        } else {
            return Union<Ts...>::template construct<NoneType>();
        }
    }
};


template <meta::is_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<std::add_lvalue_reference_t<std::remove_pointer_t<From>>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : returns<Union<Ts...>> {
    using T = std::add_lvalue_reference_t<std::remove_pointer_t<From>>;

    template <typename T, typename...>
    struct match { using type = void; };
    template <typename T, typename U, typename... Us>
    struct match<T, U, Us...> { using type = match<Us...>::type; };
    template <typename T, std::same_as<T> U, typename... Us>
    struct match<T, U, Us...> { using type = U; };

    template <typename T, typename...>
    struct convert { using type = void; };
    template <typename T, typename U, typename... Us>
    struct convert<T, U, Us...> { using type = convert<Us...>::type; };
    template <typename T, typename U, typename... Us> requires (std::convertible_to<T, U>)
    struct convert<T, U, Us...> { using type = U; };

    static Union<Ts...> operator()(From from) {
        if (from) {
            using F = std::remove_cvref_t<meta::python_type<T>>;
            if constexpr (!std::is_void_v<typename match<F, Ts...>::type>) {
                return Union<Ts...>::template construct<typename match<F, Ts...>::type>(
                    *from
                );
            } else {
                return Union<Ts...>::template construct<typename convert<T, Ts...>::type>(
                    *from
                );
            }
        } else {
            return Union<Ts...>::template construct<NoneType>();
        }
    }
};


template <meta::is_shared_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<std::add_lvalue_reference_t<meta::shared_ptr_type<From>>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : returns<Union<Ts...>> {
    using T = std::add_lvalue_reference_t<meta::shared_ptr_type<From>>;

    template <typename T, typename...>
    struct match { using type = void; };
    template <typename T, typename U, typename... Us>
    struct match<T, U, Us...> { using type = match<Us...>::type; };
    template <typename T, std::same_as<T> U, typename... Us>
    struct match<T, U, Us...> { using type = U; };

    template <typename T, typename...>
    struct convert { using type = void; };
    template <typename T, typename U, typename... Us>
    struct convert<T, U, Us...> { using type = convert<Us...>::type; };
    template <typename T, typename U, typename... Us> requires (std::convertible_to<T, U>)
    struct convert<T, U, Us...> { using type = U; };

    static Union<Ts...> operator()(From from) {
        if (from) {
            using F = std::remove_cvref_t<meta::python_type<T>>;
            if constexpr (!std::is_void_v<typename match<F, Ts...>::type>) {
                return Union<Ts...>::template construct<typename match<F, Ts...>::type>(
                    *from
                );
            } else {
                return Union<Ts...>::template construct<typename convert<T, Ts...>::type>(
                    *from
                );
            }
        } else {
            return Union<Ts...>::template construct<NoneType>();
        }
    }
};


template <meta::is_unique_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<std::add_lvalue_reference_t<meta::unique_ptr_type<From>>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : returns<Union<Ts...>> {
    using T = std::add_lvalue_reference_t<meta::unique_ptr_type<From>>;

    template <typename T, typename...>
    struct match { using type = void; };
    template <typename T, typename U, typename... Us>
    struct match<T, U, Us...> { using type = match<Us...>::type; };
    template <typename T, std::same_as<T> U, typename... Us>
    struct match<T, U, Us...> { using type = U; };

    template <typename T, typename...>
    struct convert { using type = void; };
    template <typename T, typename U, typename... Us>
    struct convert<T, U, Us...> { using type = convert<Us...>::type; };
    template <typename T, typename U, typename... Us> requires (std::convertible_to<T, U>)
    struct convert<T, U, Us...> { using type = U; };

    static Union<Ts...> operator()(From from) {
        if (from) {
            using F = std::remove_cvref_t<meta::python_type<T>>;
            if constexpr (!std::is_void_v<typename match<F, Ts...>::type>) {
                return Union<Ts...>::template construct<typename match<F, Ts...>::type>(
                    *from
                );
            } else {
                return Union<Ts...>::template construct<typename convert<T, Ts...>::type>(
                    *from
                );
            }
        } else {
            return Union<Ts...>::template construct<NoneType>();
        }
    }
};


/// NOTE: all other operations are only enabled if one or more members of the union
/// support them, and will return a new union type holding all of the valid results,
/// or a standard type if the resulting union would be a singleton.


template <typename Derived, typename Base>
    requires (meta::Union<Derived> || meta::Union<Base>)
struct __isinstance__<Derived, Base> : returns<bool> {
    template <typename>
    struct unary;
    template <typename... Types>
    struct unary<Union<Types...>> {
        static bool operator()(auto&& value) {
            return (isinstance<Types>(std::forward<decltype(value)>(value)) || ...);
        }
    };

    static constexpr bool operator()(Derived obj) {
        if constexpr (meta::Union<Derived>) {
            return visit(
                []<typename T>(T&& value) -> bool {
                    if constexpr (meta::Union<Base>) {
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
    requires (meta::Union<Derived> || meta::Union<Base>)
struct __issubclass__<Derived, Base> : returns<bool> {
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
        if constexpr (meta::Union<D>) {
            return visit(
                []<typename T>(T&& value) -> bool {
                    if constexpr (meta::Union<Base>) {
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


template <meta::Union Self, static_str Name>
    requires (impl::UnionGetAttr<Self, Name>::enable)
struct __getattr__<Self, Name> : returns<typename impl::UnionGetAttr<Self, Name>::type> {
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


template <meta::Union Self, static_str Name>
    requires (__getattr__<Self, Name>::enable)
struct __hasattr__<Self, Name> : returns<bool> {
    static bool operator()(auto&& self) {
        /// TODO: Python 3.13 introduces `PyObject_HasAttrWithError()`, which is
        /// more robust when it comes to error handling.
        return PyObject_HasAttr(ptr(self), impl::template_string<Name>());
    }
};


template <meta::Union Self, static_str Name, typename Value>
    requires (impl::UnionSetAttr<Self, Name, Value>::enable)
struct __setattr__<Self, Name, Value> : returns<void> {
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


template <meta::Union Self, static_str Name>
    requires (impl::UnionDelAttr<Self, Name>::enable)
struct __delattr__<Self, Name> : returns<void> {
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


template <meta::Union Self>
struct __repr__<Self> : returns<std::string> {
    static std::string operator()(Self self) {
        return std::forward<Self>(self).visit([]<typename T>(T&& self) -> std::string {
            return repr(std::forward<T>(self));
        });
    }
};


template <meta::Union Self, typename... Args>
    requires (impl::UnionCall<Self, Args...>::enable)
struct __call__<Self, Args...> : returns<typename impl::UnionCall<Self, Args...>::type> {
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


template <meta::Union Self, typename... Key>
    requires (impl::UnionGetItem<Self, Key...>::enable)
struct __getitem__<Self, Key...> : returns<typename impl::UnionGetItem<Self, Key...>::type> {
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


template <meta::Union Self, typename Value, typename... Key>
    requires (impl::UnionSetItem<Self, Value, Key...>::enable)
struct __setitem__<Self, Value, Key...> : returns<void> {
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


template <meta::Union Self, typename... Key>
    requires (impl::UnionDelItem<Self, Key...>::enable)
struct __delitem__<Self, Key...> : returns<void> {
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


template <meta::Union Self>
    requires (
        impl::UnionHash<Self>::enable &&
        std::convertible_to<typename impl::UnionHash<Self>::type, size_t>
    )
struct __hash__<Self> : returns<size_t> {
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


template <meta::Union Self>
    requires (
        impl::UnionLen<Self>::enable &&
        std::convertible_to<typename impl::UnionLen<Self>::type, size_t>
    )
struct __len__<Self> : returns<size_t> {
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


template <meta::Union Self, typename Key>
    requires (
        impl::UnionContains<Self, Key>::enable &&
        std::convertible_to<typename impl::UnionContains<Self, Key>::type, bool>
    )
struct __contains__<Self, Key> : returns<bool> {
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


template <meta::Union Self> requires (impl::UnionIter<Self>::enable)
struct __iter__<Self> : returns<typename impl::UnionIter<Self>::type> {
    /// NOTE: default implementation delegates to Python, which reinterprets each value
    /// as the given type(s).  That handles all cases appropriately, with a small
    /// performance hit for the extra interpreter overhead that isn't present for
    /// static types.
};


template <meta::Union Self> requires (impl::UnionReversed<Self>::enable)
struct __reversed__<Self> : returns<typename impl::UnionReversed<Self>::type> {
    /// NOTE: same as `__iter__`, but returns a reverse iterator instead.
};


template <meta::Union Self> requires (impl::UnionAbs<Self>::enable)
struct __abs__<Self> : returns<typename impl::UnionAbs<Self>::type> {
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


template <meta::Union Self> requires (impl::UnionInvert<Self>::enable)
struct __invert__<Self> : returns<typename impl::UnionInvert<Self>::type> {
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


template <meta::Union Self> requires (impl::UnionPos<Self>::enable)
struct __pos__<Self> : returns<typename impl::UnionPos<Self>::type> {
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


template <meta::Union Self> requires (impl::UnionNeg<Self>::enable)
struct __neg__<Self> : returns<typename impl::UnionNeg<Self>::type> {
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


template <meta::Union Self> requires (impl::UnionIncrement<Self>::enable)
struct __increment__<Self> : returns<typename impl::UnionIncrement<Self>::type> {
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


template <meta::Union Self> requires (impl::UnionDecrement<Self>::enable)
struct __decrement__<Self> : returns<typename impl::UnionDecrement<Self>::type> {
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
struct __lt__<L, R> : returns<typename impl::UnionLess<L, R>::type> {
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
struct __le__<L, R> : returns<typename impl::UnionLessEqual<L, R>::type> {
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
struct __eq__<L, R> : returns<typename impl::UnionEqual<L, R>::type> {
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
struct __ne__<L, R> : returns<typename impl::UnionNotEqual<L, R>::type> {
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
struct __ge__<L, R> : returns<typename impl::UnionGreaterEqual<L, R>::type> {
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
struct __gt__<L, R> : returns<typename impl::UnionGreater<L, R>::type> {
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
struct __add__<L, R> : returns<typename impl::UnionAdd<L, R>::type> {
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
struct __sub__<L, R> : returns<typename impl::UnionSub<L, R>::type> {
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
struct __mul__<L, R> : returns<typename impl::UnionMul<L, R>::type> {
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
struct __pow__<Base, Exp, Mod> : returns<typename impl::UnionPow<Base, Exp, Mod>::type> {
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
struct __truediv__<L, R> : returns<typename impl::UnionTrueDiv<L, R>::type> {
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
struct __floordiv__<L, R> : returns<typename impl::UnionFloorDiv<L, R>::type> {
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
struct __mod__<L, R> : returns<typename impl::UnionMod<L, R>::type> {
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
struct __lshift__<L, R> : returns<typename impl::UnionLShift<L, R>::type> {
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
struct __rshift__<L, R> : returns<typename impl::UnionRShift<L, R>::type> {
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
struct __and__<L, R> : returns<typename impl::UnionAnd<L, R>::type> {
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
struct __xor__<L, R> : returns<typename impl::UnionXor<L, R>::type> {
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
struct __or__<L, R> : returns<typename impl::UnionOr<L, R>::type> {
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
struct __iadd__<L, R> : returns<typename impl::UnionInplaceAdd<L, R>::type> {
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
struct __isub__<L, R> : returns<typename impl::UnionInplaceSub<L, R>::type> {
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
struct __imul__<L, R> : returns<typename impl::UnionInplaceMul<L, R>::type> {
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
struct __ipow__<Base, Exp, Mod> : returns<typename impl::UnionInplacePow<Base, Exp, Mod>::type> {
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
struct __itruediv__<L, R> : returns<typename impl::UnionInplaceTrueDiv<L, R>::type> {
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
struct __ifloordiv__<L, R> : returns<typename impl::UnionInplaceFloorDiv<L, R>::type> {
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
struct __imod__<L, R> : returns<typename impl::UnionInplaceMod<L, R>::type> {
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
struct __ilshift__<L, R> : returns<typename impl::UnionInplaceLShift<L, R>::type> {
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
struct __irshift__<L, R> : returns<typename impl::UnionInplaceRShift<L, R>::type> {
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
struct __iand__<L, R> : returns<typename impl::UnionInplaceAnd<L, R>::type> {
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
struct __ixor__<L, R> : returns<typename impl::UnionInplaceXor<L, R>::type> {
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
struct __ior__<L, R> : returns<typename impl::UnionInplaceOr<L, R>::type> {
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


}  // namespace bertrand


namespace std {

    template <typename T, bertrand::meta::Union U>
        requires (std::remove_cvref_t<U>::template contains<T>())
    [[nodiscard]] bool holds_alternative(U&& u) noexcept {
        return std::forward<U>(u).template holds_alternative<T>();
    }

    template <typename T, bertrand::meta::Union U>
        requires (std::remove_cvref_t<U>::template contains<T>())
    [[nodiscard]] auto get(U&& u) {
        return std::forward<U>(u).template get<T>();
    }

    template <size_t I, bertrand::meta::Union U>
        requires (I < std::remove_cvref_t<U>::size())
    [[nodiscard]] auto get(U&& u) {
        return std::forward<U>(u).template get<I>();
    }

    template <typename T, bertrand::meta::Union U>
        requires (std::remove_cvref_t<U>::template contains<T>())
    [[nodiscard]] auto get_if(U&& u) {
        return std::forward<U>(u).template get_if<T>();
    }

    template <size_t I, bertrand::meta::Union U>
        requires (I < std::remove_cvref_t<U>::size())
    [[nodiscard]] auto get_if(U&& u) {
        return std::forward<U>(u).template get_if<I>();
    }

}


#endif
