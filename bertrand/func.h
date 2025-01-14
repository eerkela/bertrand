#ifndef BERTRAND_FUNC_H
#define BERTRAND_FUNC_H

#include <vector>
#include <unordered_map>

#include "bertrand/common.h"
#include "bertrand/static_str.h"


namespace bertrand {


template <typename T>
struct ArgTraits;
template <typename... Ts>
struct args;
template <typename F, typename... Fs>
    requires (
        !std::is_reference_v<F> &&
        !(std::is_reference_v<Fs> || ...)
    )
struct chain;
template <iterable T> requires (has_size<T>)
struct ArgPack;
template <mapping_like T>
    requires (
        has_size<T> &&
        std::convertible_to<
            typename std::remove_reference_t<T>::key_type,
            std::string
        >
    )
struct KwargPack;


/* A compact bitset describing the kind (positional, keyword, optional, and/or
variadic) of an argument within a C++ parameter list. */
struct ArgKind {
    enum Flags : uint8_t {
        /// NOTE: the relative ordering of these flags is significant, as it
        /// dictates the order in which edges are stored within overload tries
        /// for the `py::Function` class.  The order should always be such that
        /// POS < OPT POS < VAR POS < KW < OPT KW < VAR KW, to ensure a stable
        /// traversal order.
        OPT                 = 0b1,
        VAR                 = 0b10,
        POS                 = 0b100,
        KW                  = 0b1000,
    } flags;

    [[nodiscard]] constexpr ArgKind(uint8_t flags = 0) noexcept :
        flags(static_cast<Flags>(flags))
    {}

    [[nodiscard]] constexpr operator uint8_t() const noexcept {
        return flags;
    }

    [[nodiscard]] constexpr bool posonly() const noexcept {
        return (flags & ~OPT) == POS;
    }

    [[nodiscard]] constexpr bool pos() const noexcept {
        return (flags & (VAR | POS)) == POS;
    }

    [[nodiscard]] constexpr bool args() const noexcept {
        return flags == (VAR | POS);
    }

    [[nodiscard]] constexpr bool kwonly() const noexcept {
        return (flags & ~OPT) == KW;
    }

    [[nodiscard]] constexpr bool kw() const noexcept {
        return (flags & (VAR | KW)) == KW;
    }

    [[nodiscard]] constexpr bool kwargs() const noexcept {
        return flags == (VAR | KW);
    }

    [[nodiscard]] constexpr bool opt() const noexcept {
        return flags & OPT;
    }

    [[nodiscard]] constexpr bool variadic() const noexcept {
        return flags & VAR;
    }
};


namespace impl {

    template <typename Arg, typename... Ts>
    struct BoundArg;

    struct args_tag {};
    struct chain_tag {};

    template <typename T>
    constexpr bool _is_args = false;
    template <typename... Ts>
    constexpr bool _is_args<args<Ts...>> = true;

    template <typename T>
    constexpr bool _is_chain = false;
    template <typename F, typename... Fs>
    constexpr bool _is_chain<chain<F, Fs...>> = true;

    template <typename T>
    constexpr bool _arg_pack = false;
    template <typename T>
    constexpr bool _arg_pack<ArgPack<T>> = true;

    template <typename T>
    constexpr bool _kwarg_pack = false;
    template <typename T>
    constexpr bool _kwarg_pack<KwargPack<T>> = true;

    constexpr bool isalpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    constexpr bool isalnum(char c) {
        return isalpha(c) || (c >= '0' && c <= '9');
    }

    template <size_t, static_str>
    constexpr bool validate_arg_name = true;
    template <size_t I, static_str Name> requires (I < Name.size())
    constexpr bool validate_arg_name<I, Name> = [] {
        return
            (isalnum(Name[I]) || Name[I] == '_') &&
            validate_arg_name<I + 1, Name>;
    }();

    template <static_str Name>
    constexpr bool _arg_name = [] {
        return 
            !Name.empty() &&
            (isalpha(Name[0]) || Name[0] == '_') &&
            validate_arg_name<1, Name>;
    }();

    template <static_str Name>
    constexpr bool _variadic_args_name = [] {
        return
            Name.size() > 1 &&
            Name[0] == '*' &&
            (isalpha(Name[1]) || Name[1] == '_') &&
            validate_arg_name<2, Name>;
    }();

    template <static_str Name>
    constexpr bool _variadic_kwargs_name = [] {
        return
            Name.size() > 2 &&
            Name[0] == '*' &&
            Name[1] == '*' &&
            (isalpha(Name[2]) || Name[2] == '_') &&
            validate_arg_name<3, Name>;
    }();

    template <typename... Vs>
    constexpr bool names_are_unique = true;
    template <typename V, typename... Vs>
    constexpr bool names_are_unique<V, Vs...> = [] {
        return (
            ArgTraits<V>::name.empty() ||
            ((ArgTraits<V>::name != ArgTraits<Vs>::name) && ...)
        ) && names_are_unique<Vs...>;
    }();

    template <typename, typename = void>
    struct detect_arg { static constexpr bool value = false; };
    template <typename T>
    struct detect_arg<T, std::void_t<typename T::_detect_arg>> {
        static constexpr bool value = true;
    };

    template <typename... Ts>
    struct ArgsBase : args_tag {};
    template <typename T, typename... Ts>
        requires (std::is_void_v<T> || (std::is_void_v<Ts> || ...))
    struct ArgsBase<T, Ts...> {};
    template <typename T, typename... Ts>
    struct ArgsBase<T, Ts...> : ArgsBase<Ts...> {
        std::conditional_t<
            std::is_lvalue_reference_v<T>,
            T,
            std::remove_reference_t<T>
        > value;
        constexpr ArgsBase(T value, Ts... ts) :
            ArgsBase<Ts...>(std::forward<Ts>(ts)...),
            value(std::forward<T>(value))
        {}
        constexpr ArgsBase(ArgsBase&& other) :
            ArgsBase<Ts...>(std::move(other)),
            value([](ArgsBase&& other) {
                if constexpr (std::is_lvalue_reference_v<T>) {
                    return other.value;
                } else {
                    return std::move(other.value);
                }
            }())
        {}
    };

}


template <typename T>
concept is_args = impl::_is_args<std::remove_cvref_t<T>>;
template <typename T>
concept is_chain = impl::_is_chain<std::remove_cvref_t<T>>;
template <typename T>
concept is_arg = impl::detect_arg<std::remove_cvref_t<T>>::value;
template <typename T>
concept arg_pack = impl::_arg_pack<std::remove_cvref_t<T>>;
template <typename T>
concept kwarg_pack = impl::_kwarg_pack<std::remove_cvref_t<T>>;
template <static_str Name>
concept arg_name =
    impl::_arg_name<Name> ||
    impl::_variadic_args_name<Name> ||
    impl::_variadic_kwargs_name<Name>;
template <static_str Name>
concept variadic_args_name =
    arg_name<Name> &&
    !impl::_arg_name<Name> &&
    impl::_variadic_args_name<Name> &&
    !impl::_variadic_kwargs_name<Name>;
template <static_str Name>
concept variadic_kwargs_name =
    arg_name<Name> &&
    !impl::_arg_name<Name> &&
    !impl::_variadic_args_name<Name> &&
    impl::_variadic_kwargs_name<Name>;


/* Save a set of input arguments for later use.  Returns an args<> container, which
stores the arguments similar to a `std::tuple`, except that it is capable of storing
references and cannot be copied or moved.  Calling the args pack as an rvalue will
perfectly forward its values to an input function, without any extra copies, and at
most 2 moves per element (one when the pack is created and another when it is consumed).

Also provides utilities for compile-time argument manipulation wherever arbitrary lists
of types may be necessary. 

WARNING: Undefined behavior can occur if an lvalue is bound that falls out of scope
before the pack is consumed.  Such values will not have their lifetimes extended in any
way, and it is the user's responsibility to ensure that this is observed at all times.
Generally speaking, ensuring that no packs are returned out of a local context is
enough to satisfy this guarantee.  Typically, this class will be consumed within the
same context in which it was created, or in a downstream one where all of the objects
are still in scope, as a way of enforcing a certain order of operations.  Note that
this guidance does not apply to rvalues and temporaries, which are stored directly
within the pack for its natural lifetime. */
template <typename... Ts>
struct args : impl::ArgsBase<Ts...> {
private:

    template <typename>
    struct _concat;
    template <typename... Us>
    struct _concat<args<Us...>> { using type = args<Ts..., Us...>; };

    template <typename... packs>
    struct _product {
        /* permute<> iterates from left to right along the packs. */
        template <typename permuted, typename...>
        struct permute { using type = permuted; };
        template <typename... permuted, typename... types, typename... rest>
        struct permute<args<permuted...>, args<types...>, rest...> {

            /* accumulate<> iterates over the prior permutations and updates them
            with the types at this index. */
            template <typename accumulated, typename...>
            struct accumulate { using type = accumulated; };
            template <typename... accumulated, typename permutation, typename... others>
            struct accumulate<args<accumulated...>, permutation, others...> {

                /* append<> iterates from top to bottom for each type. */
                template <typename appended, typename...>
                struct append { using type = appended; };
                template <typename... appended, typename U, typename... Us>
                struct append<args<appended...>, U, Us...> {
                    using type = append<
                        args<appended..., typename permutation::template append<U>>,
                        Us...
                    >::type;
                };

                /* append<> extends the accumulated output at this index. */
                using type = accumulate<
                    typename append<args<accumulated...>, types...>::type,
                    others...
                >::type;
            };

            /* accumulate<> has to rebuild the output pack at each iteration. */
            using type = permute<
                typename accumulate<args<>, permuted...>::type,
                rest...
            >::type;
        };

        /* This pack is converted to a 2D pack to initialize the recursion. */
        using type = permute<args<args<Ts>...>, packs...>::type;
    };

    template <typename out, typename...>
    struct _unique { using type = out; };
    template <typename... Vs, typename U, typename... Us>
    struct _unique<args<Vs...>, U, Us...> {
        template <typename>
        struct helper { using type = args<Vs...>; };
        template <typename U2> requires (!(std::same_as<U2, Us> || ...))
        struct helper<U2> { using type = args<Vs..., U>; };
        using type = _unique<typename helper<U>::type, Us...>::type;
    };

    template <typename>
    struct _to_value;
    template <typename... Us>
    struct _to_value<args<Us...>> {
        template <typename out, typename...>
        struct filter { using type = out; };
        template <typename... Ws, typename V, typename... Vs>
        struct filter<args<Ws...>, V, Vs...> {
            template <typename>
            struct helper { using type = args<Ws...>; };
            template <typename V2>
                requires (!(std::same_as<std::remove_cvref_t<V2>, Ws> || ...))
            struct helper<V2> {
                using type = args<Ws..., std::conditional_t<
                    (std::same_as<
                        std::remove_cvref_t<V2>,
                        std::remove_cvref_t<Vs>
                    > || ...),
                    std::remove_cvref_t<V2>,
                    V2
                >>;
            };
            using type = filter<typename helper<V>::type, Vs...>::type;
        };
        using type = filter<args<>, Us...>::type;
    };

    template <typename out, size_t I>
    struct _get_base { using type = out; };
    template <typename... Us, size_t I> requires (I < sizeof...(Ts))
    struct _get_base<impl::ArgsBase<Us...>, I> {
        using type = _get_base<
            impl::ArgsBase<Us..., unpack_type<I, Ts...>>,
            I + 1
        >::type;
    };
    template <size_t I> requires (I < sizeof...(Ts))
    using get_base = _get_base<impl::ArgsBase<>, I>::type;

    template <size_t I> requires (I < sizeof...(Ts))
    decltype(auto) forward() {
        if constexpr (std::is_lvalue_reference_v<unpack_type<I, Ts...>>) {
            return get_base<I>::value;
        } else {
            return std::move(get_base<I>::value);
        }
    }

public:
    /* The total number of arguments being stored. */
    [[nodiscard]] static constexpr size_t size() noexcept {
        return sizeof...(Ts);
    }
    [[nodiscard]] static constexpr bool empty() noexcept {
        return sizeof...(Ts) == 0;
    }

    /// TODO: eventually, delete ::n in favor of ::size().

    static constexpr size_t n = sizeof...(Ts);
    template <typename T>
    static constexpr size_t index_of = bertrand::index_of<T, Ts...>;
    template <typename T>
    static constexpr bool contains = index_of<T> != n;

    /* Evaluate a control structure's `::enable` state by inserting this pack's
    template parameters. */
    template <template <typename...> class Control>
    static constexpr bool enable = Control<Ts...>::enable;

    /* Evaluate a control structure's `::type` state by inserting this pack's
    template parameters, assuming they are valid. */
    template <template <typename...> class Control> requires (enable<Control>)
    using type = Control<Ts...>::type;

    /* Get the type at index I. */
    template <size_t I> requires (I < n)
    using at = unpack_type<I, Ts...>;

    /* Get a new pack with the type appended. */
    template <typename T>
    using append = args<Ts..., T>;

    /* Get a new pack that combines the contents of this pack with another. */
    template <is_args T>
    using concat = _concat<T>::type;

    /* Get a pack of packs containing all unique permutations of the types in this
    parameter pack and all others, returning their Cartesian product.  */
    template <is_args... packs> requires (n > 0 && ((packs::n > 0) && ...))
    using product = _product<packs...>::type;

    /* Get a new pack with exact duplicates filtered out, accounting for cvref
    qualifications. */
    using unique = _unique<args<>, Ts...>::type;

    /* Get a new pack with duplicates filtered out, replacing any types that differ
    only in cvref qualifications with an unqualified equivalent, thereby forcing a
    copy/move. */
    using to_value = _to_value<unique>::type;

    template <std::convertible_to<Ts>... Us>
    args(Us&&... args) : impl::ArgsBase<Ts...>(
        std::forward<Us>(args)...
    ) {}

    args(args&&) = default;

    args(const args&) = delete;
    args& operator=(const args&) = delete;
    args& operator=(args&&) = delete;

    /* Get the argument at index I. */
    template <size_t I> requires (I < size())
    [[nodiscard]] decltype(auto) get() && {
        if constexpr (std::is_lvalue_reference_v<unpack_type<I, Ts...>>) {
            return get_base<I>::value;
        } else {
            return std::move(get_base<I>::value);
        }
    }

    /* Calling a pack as an rvalue will perfectly forward the input arguments to an
    input function that is templated to accept them. */
    template <typename Func>
        requires (!(std::is_void_v<Ts> || ...) && std::is_invocable_v<Func, Ts...>)
    decltype(auto) operator()(Func&& func) && {
        return [&]<size_t... Is>(std::index_sequence<Is...>) {
            return func(forward<Is>()...);
        }(std::index_sequence_for<Ts...>{});
    }
};


template <typename... Ts>
args(Ts&&...) -> args<Ts...>;


/* A higher-order function that merges a sequence of component functions into a single
operation.  When called, the chain will evaluate the first function with the input
arguments, then pass the result to the next function, and so on, until the final result
is returned. */
template <typename F, typename... Fs>
    requires (
        !std::is_reference_v<F> &&
        !(std::is_reference_v<Fs> || ...)
    )
struct chain : impl::chain_tag {
private:
    F func;

public:
    /* The number of component functions in the chain. */
    [[nodiscard]] static constexpr size_t size() noexcept { return 1; }

    /* Get the type of the component function at index I. */
    template <size_t I> requires (I < size())
    using at = F;

    template <is<F> First>
    constexpr chain(First&& func) : func(std::forward<First>(func)) {}

    /* Invoke the function chain, piping the return value from the first function into
    the input for the second function, and so on. */
    template <typename... A> requires (std::is_invocable_v<F, A...>)
    constexpr decltype(auto) operator()(A&&... args) const {
        return func(std::forward<A>(args)...);
    }
    template <typename... A> requires (std::is_invocable_v<F, A...>)
    constexpr decltype(auto) operator()(A&&... args) && {
        return std::move(func)(std::forward<A>(args)...);
    }

    /* Get the component function at index I. */
    template <size_t I> requires (I < size())
    [[nodiscard]] constexpr decltype(auto) get() const noexcept { return func; }
    template <size_t I> requires (I < size())
    [[nodiscard]] constexpr decltype(auto) get() && noexcept { return std::move(func); }
};


template <typename F1, typename F2, typename... Fs>
    requires (
        !std::is_reference_v<F1> &&
        !std::is_reference_v<F2> &&
        !(std::is_reference_v<Fs> || ...)
    )
struct chain<F1, F2, Fs...> : chain<F2, Fs...> {
private:
    using base = chain<F2, Fs...>;

    F1 func;

    template <size_t I>
    struct _at { using type = base::template at<I - 1>; };
    template <>
    struct _at<0> { using type = F1; };

    template <typename R, typename...>
    struct _chainable { static constexpr bool value = true; };
    template <typename R, typename G, typename... Gs>
    struct _chainable<R, G, Gs...> {
        template <typename H>
        static constexpr bool invoke = false;
        template <typename H> requires (std::is_invocable_v<H, R>)
        static constexpr bool invoke<H> =
            _chainable<typename std::invoke_result_t<H, R>, Gs...>::value;
        static constexpr bool value = invoke<G>;
    };
    template <typename... A>
    static constexpr bool chainable =
        _chainable<typename std::invoke_result_t<F1, A...>, F2, Fs...>::value;

public:
    /* The number of component functions in the chain. */
    [[nodiscard]] static constexpr size_t size() noexcept { return base::size() + 1; }

    /* Get the type of the component function at index I. */
    template <size_t I> requires (I < size())
    using at = _at<I>::type;

    template <is<F1> First, is<F2> Next, is<Fs>... Rest>
    constexpr chain(First&& first, Next&& next, Rest&&... rest) :
        base(std::forward<Next>(next), std::forward<Rest>(rest)...),
        func(std::forward<First>(first))
    {}

    /* Invoke the function chain, piping the return value from the first function into
    the input for the second function, and so on. */
    template <typename... A> requires (std::is_invocable_v<F1, A...> && chainable<A...>)
    constexpr decltype(auto) operator()(A&&... args) const {
        return base::operator()(func(std::forward<A>(args)...));
    }
    template <typename... A> requires (std::is_invocable_v<F1, A...> && chainable<A...>)
    constexpr decltype(auto) operator()(A&&... args) && {
        return base::operator()(std::move(func)(std::forward<A>(args)...));
    }

    /* Get the component function at index I. */
    template <size_t I> requires (I < size())
    [[nodiscard]] constexpr decltype(auto) get() const noexcept {
        if constexpr (I == 0) {
            return func;
        } else {
            return base::template get<I - 1>();
        }
    }
    template <size_t I> requires (I < size())
    [[nodiscard]] constexpr decltype(auto) get() && noexcept {
        if constexpr (I == 0) {
            return std::move(func);
        } else {
            return base::template get<I - 1>();
        }
    }
};


template <typename F, typename... Fs>
chain(F, Fs...) -> chain<F, Fs...>;


template <is_chain Self, is_chain Next>
[[nodiscard]] constexpr auto operator>>(Self&& self, Next&& next) {
    return []<size_t... Is, size_t... Js>(
        std::index_sequence<Is...>,
        std::index_sequence<Js...>,
        auto&& self,
        auto&& next
    ) {
        return chain(
            std::forward<decltype(self)>(self).template get<Is>()...,
            std::forward<decltype(next)>(next).template get<Js>()...
        );
    }(
        std::make_index_sequence<std::remove_cvref_t<Self>::size()>{},
        std::make_index_sequence<std::remove_cvref_t<Next>::size()>{},
        std::forward<Self>(self),
        std::forward<Next>(next)
    );
}


template <is_chain Self, typename Next>
[[nodiscard]] constexpr auto operator>>(Self&& self, Next&& next) {
    return []<size_t... Is>(std::index_sequence<Is...>, auto&& self, auto&& next) {
        return chain(
            std::forward<decltype(self)>(self).template get<Is>()...,
            std::forward<decltype(next)>(next)
        );
    }(
        std::make_index_sequence<std::remove_cvref_t<Self>::size()>{},
        std::forward<Self>(self),
        std::forward<Next>(next)
    );
}


template <typename Prev, is_chain Self>
[[nodiscard]] constexpr auto operator>>(Prev&& prev, Self&& self) {
    return []<size_t... Is>(std::index_sequence<Is...>, auto&& prev, auto&& self) {
        return chain(
            std::forward<decltype(prev)>(prev),
            std::forward<decltype(self)>(self).template get<Is>()...
        );
    }(
        std::make_index_sequence<std::remove_cvref_t<Self>::size()>{},
        std::forward<Prev>(prev),
        std::forward<Self>(self)
    );
}


/* A family of compile-time argument annotations that represent positional and/or
keyword arguments to a Python-style function.  Modifiers can be added to an argument
to indicate its kind, such as positional-only, keyword-only, optional, bound to a
partial value, or variadic (inferred from the argument name via leading `*` or `**`
prefixes).  The default (without any modifiers) is positional-or-keyword, unbound, and
required, similar to Python.

Note that this class makes careful use of aggregate initialization to extend the
lifetime of temporaries, meaning that it is safe to use with arbitrarily-qualified
reference types.  Such references are guaranteed to remain valid for their full,
natural lifespan, as if they were declared without the enclosing `Arg<>` wrapper.  In
particular, that allows `Arg<>` annotations to be freely declared as function
arguments without interfering with existing C++ parameter passing semantics. */
template <static_str Name, typename T> requires (arg_name<Name>)
struct Arg {
private:
    template <typename, typename>
    friend struct impl::detect_arg;
    using _detect_arg = void;
    using Value = std::remove_reference_t<T>;

    /// NOTE: there's a lot of code duplication here.  This is intentional, in order to
    /// maximize the clarity of error messages, restrict the ways in which annotations
    /// can be combined, and allow aggregate initialization to handle temporaries
    /// correctly.  The alternative would be a macro, a separate template helper, or
    /// inheritance, all of which obfuscate errors, are inflexible, and/or complicate
    /// aggregate initialization.

public:
    static constexpr static_str name = Name;
    static constexpr ArgKind kind = ArgKind::POS | ArgKind::KW;
    using type = T;
    using bound_to = bertrand::args<>;
    using unbind = Arg;
    template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
    using with_name = Arg<N, T>;
    template <typename V> requires (!std::is_void_v<V>)
    using with_type = Arg<Name, V>;

    type value;

    [[nodiscard]] constexpr type operator*() && { std::forward<type>(value); }
    [[nodiscard]] constexpr Value& operator*() & { return value; }
    [[nodiscard]] constexpr Value* operator->() { return &value; }
    [[nodiscard]] constexpr const Value& operator*() const & { return value; }
    [[nodiscard]] constexpr const Value* operator->() const { return &value; }

    /* Argument rvalues are normally generated whenever a function is called.  Making
    them convertible to the underlying type means they can be used to call external
    C++ functions that are not aware of Python argument annotations, in a way that
    conforms to traits like `std::is_invocable<>`, etc. */
    [[nodiscard]] constexpr operator type() && { return std::forward<type>(value); }

    /* Conversions to other types are also allowed, as long as the argument is given as
    an rvalue and the underlying type supports it. */
    template <typename U> requires (std::convertible_to<type, U>)
    [[nodiscard]] constexpr operator U() && { return std::forward<type>(value); }

    /* Argument rvalues are also convertible to other annotated argument types, in
    order to ease the metaprogramming requirements.  This effectively means that all
    trailing annotation types are mutually interconvertible. */
    struct opt;
    struct pos;
    struct kw;

    template <typename... Vs>
    static constexpr bool can_bind = false;
    template <std::convertible_to<type> V>
    static constexpr bool can_bind<V> =
        !ArgTraits<V>::opt() &&
        !ArgTraits<V>::variadic() &&
        (ArgTraits<V>::name.empty() || ArgTraits<V>::name == name);

    /* Bind a partial value to this argument. */
    template <std::convertible_to<type>... Vs>
        requires (
            sizeof...(Vs) == 1 &&
            (!ArgTraits<Vs>::opt() && ...) &&
            (!ArgTraits<Vs>::variadic() && ...) &&
            ((ArgTraits<Vs>::name.empty() || ArgTraits<Vs>::name == name) && ...)
        )
    using bind = impl::BoundArg<Arg, Vs...>;

    template <typename U>
    [[nodiscard]] constexpr operator bind<U>() && {
        return {std::forward<type>(value)};
    }

    [[nodiscard]] constexpr operator opt() && { return {std::forward<type>(value)}; }
    template <typename U>
    [[nodiscard]] constexpr operator typename opt::template bind<U>() && {
        return {std::forward<type>(value)};
    }

    [[nodiscard]] constexpr operator pos() && { return {std::forward<type>(value)}; }
    template <typename U>
    [[nodiscard]] constexpr operator typename pos::template bind<U>() && {
        return {std::forward<type>(value)};
    }

    [[nodiscard]] constexpr operator typename pos::opt() && { return {std::forward<type>(value)}; }
    template <typename U>
    [[nodiscard]] constexpr operator typename pos::opt::template bind<U>() && {
        return {std::forward<type>(value)};
    }

    [[nodiscard]] constexpr operator kw() && { return {std::forward<type>(value)}; }
    template <typename U>
    [[nodiscard]] constexpr operator typename kw::template bind<U>() && {
        return {std::forward<type>(value)};
    }

    [[nodiscard]] constexpr operator typename kw::opt() && { return {std::forward<type>(value)}; }
    template <typename U>
    [[nodiscard]] constexpr operator typename kw::opt::template bind<U>() && {
        return {std::forward<type>(value)};
    }

    /* Marks the argument as optional. */
    struct opt {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

    public:
        static constexpr static_str name = Name;
        static constexpr ArgKind kind = Arg::kind | ArgKind::OPT;
        using type = T;
        using bound_to = bertrand::args<>;
        using unbind = opt;
        template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
        using with_name = Arg<N, T>::opt;
        template <typename V> requires (!std::is_void_v<V>)
        using with_type = Arg<Name, V>::opt;

        type value;

        [[nodiscard]] constexpr type operator*() && { std::forward<type>(value); }
        [[nodiscard]] constexpr Value& operator*() & { return value; }
        [[nodiscard]] constexpr Value* operator->() { return &value; }
        [[nodiscard]] constexpr const Value& operator*() const & { return value; }
        [[nodiscard]] constexpr const Value* operator->() const { return &value; }
        template <typename U> requires (std::convertible_to<Arg, U>)
        [[nodiscard]] constexpr operator U() && { return Arg{std::forward<type>(value)}; }

        template <typename... Vs>
        static constexpr bool can_bind = Arg::can_bind<Vs...>;

        template <std::convertible_to<type>... Vs>
            requires (
                sizeof...(Vs) == 1 &&
                (!ArgTraits<Vs>::opt() && ...) &&
                (!ArgTraits<Vs>::variadic() && ...) &&
                ((ArgTraits<Vs>::name.empty() || ArgTraits<Vs>::name == name) && ...)
            )
        using bind = impl::BoundArg<opt, Vs...>;
    };

    /* Marks the argument as positional-only. */
    struct pos {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

    public:
        static constexpr static_str name = Name;
        static constexpr ArgKind kind = ArgKind::POS;
        using type = T;
        using bound_to = bertrand::args<>;
        using unbind = pos;
        template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
        using with_name = Arg<N, T>::pos;
        template <typename V> requires (!std::is_void_v<V>)
        using with_type = Arg<Name, V>::pos;

        type value;

        [[nodiscard]] constexpr type operator*() && { std::forward<type>(value); }
        [[nodiscard]] constexpr Value& operator*() & { return value; }
        [[nodiscard]] constexpr Value* operator->() { return &value; }
        [[nodiscard]] constexpr const Value& operator*() const & { return value; }
        [[nodiscard]] constexpr const Value* operator->() const { return &value; }
        template <typename U> requires (std::convertible_to<Arg, U>)
        [[nodiscard]] constexpr operator U() && { return Arg{std::forward<type>(value)}; }

        template <typename... Vs>
        static constexpr bool can_bind = false;
        template <std::convertible_to<type> V>
        static constexpr bool can_bind<V> =
            ArgTraits<V>::posonly() &&
            !ArgTraits<V>::opt() &&
            !ArgTraits<V>::variadic() &&
            (ArgTraits<V>::name.empty() || ArgTraits<V>::name == name);

        template <std::convertible_to<type>... Vs>
            requires (
                sizeof...(Vs) == 1 &&
                (ArgTraits<Vs>::posonly() && ...) &&
                (!ArgTraits<Vs>::opt() && ...) &&
                (!ArgTraits<Vs>::variadic() && ...) &&
                ((ArgTraits<Vs>::name.empty() || ArgTraits<Vs>::name == name) && ...)
            )
        using bind = impl::BoundArg<pos, Vs...>;

        struct opt {
        private:
            template <typename, typename>
            friend struct impl::detect_arg;
            using _detect_arg = void;

        public:
            static constexpr static_str name = Name;
            static constexpr ArgKind kind = pos::kind | ArgKind::OPT;
            using type = T;
            using bound_to = bertrand::args<>;
            using unbind = opt;
            template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
            using with_name = Arg<N, T>::pos::opt;
            template <typename V> requires (!std::is_void_v<V>)
            using with_type = Arg<Name, V>::pos::opt;

            type value;

            [[nodiscard]] constexpr type operator*() && { std::forward<type>(value); }
            [[nodiscard]] constexpr Value& operator*() & { return value; }
            [[nodiscard]] constexpr Value* operator->() { return &value; }
            [[nodiscard]] constexpr const Value& operator*() const & { return value; }
            [[nodiscard]] constexpr const Value* operator->() const { return &value; }
            template <typename U> requires (std::convertible_to<Arg, U>)
            [[nodiscard]] constexpr operator U() && { return Arg{std::forward<type>(value)}; }

            template <typename... Vs>
            static constexpr bool can_bind = pos::can_bind<Vs...>;

            template <std::convertible_to<type>... Vs>
                requires (
                    sizeof...(Vs) == 1 &&
                    (ArgTraits<Vs>::posonly() && ...) &&
                    (!ArgTraits<Vs>::opt() && ...) &&
                    (!ArgTraits<Vs>::variadic() && ...) &&
                    ((ArgTraits<Vs>::name.empty() || ArgTraits<Vs>::name == name) && ...)
                )
            using bind = impl::BoundArg<opt, Vs...>;
        };
    };

    /* Marks the argument as keyword-only. */
    struct kw {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

    public:
        static constexpr static_str name = Name;
        static constexpr ArgKind kind = ArgKind::KW;
        using type = T;
        using bound_to = bertrand::args<>;
        using unbind = kw;
        template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
        using with_name = Arg<N, T>::kw;
        template <typename V> requires (!std::is_void_v<V>)
        using with_type = Arg<Name, V>::kw;

        type value;

        [[nodiscard]] constexpr type operator*() && { std::forward<type>(value); }
        [[nodiscard]] constexpr Value& operator*() & { return value; }
        [[nodiscard]] constexpr Value* operator->() { return &value; }
        [[nodiscard]] constexpr const Value& operator*() const & { return value; }
        [[nodiscard]] constexpr const Value* operator->() const { return &value; }
        template <typename U> requires (std::convertible_to<Arg, U>)
        [[nodiscard]] constexpr operator U() && { return Arg{std::forward<type>(value)}; }

        template <typename... Vs>
        static constexpr bool can_bind = false;
        template <std::convertible_to<type> V>
        static constexpr bool can_bind<V> =
            ArgTraits<V>::kw() &&
            !ArgTraits<V>::opt() &&
            !ArgTraits<V>::variadic() &&
            (ArgTraits<V>::name.empty() || ArgTraits<V>::name == name);

        template <std::convertible_to<type>... Vs>
            requires (
                sizeof...(Vs) == 1 &&
                (ArgTraits<Vs>::kw() && ...) &&
                (!ArgTraits<Vs>::opt() && ...) &&
                (!ArgTraits<Vs>::variadic() && ...) &&
                ((ArgTraits<Vs>::name.empty() || ArgTraits<Vs>::name == name) && ...)
            )
        using bind = impl::BoundArg<kw, Vs...>;

        struct opt {
        private:
            template <typename, typename>
            friend struct impl::detect_arg;
            using _detect_arg = void;

        public:
            static constexpr static_str name = Name;
            static constexpr ArgKind kind = kw::kind | ArgKind::OPT;
            using type = T;
            using bound_to = bertrand::args<>;
            using unbind = opt;
            template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
            using with_name = Arg<N, T>::kw::opt;
            template <typename V> requires (!std::is_void_v<V>)
            using with_type = Arg<Name, V>::kw::opt;

            type value;

            [[nodiscard]] constexpr type operator*() && { std::forward<type>(value); }
            [[nodiscard]] constexpr Value& operator*() & { return value; }
            [[nodiscard]] constexpr Value* operator->() { return &value; }
            [[nodiscard]] constexpr const Value& operator*() const & { return value; }
            [[nodiscard]] constexpr const Value* operator->() const { return &value; }
            template <typename U> requires (std::convertible_to<Arg, U>)
            [[nodiscard]] constexpr operator U() && { return Arg{std::forward<type>(value)}; }

            template <typename... Vs>
            static constexpr bool can_bind = kw::can_bind<Vs...>;

            template <std::convertible_to<type>... Vs>
                requires (
                    sizeof...(Vs) == 1 &&
                    (ArgTraits<Vs>::kw() && ...) &&
                    (!ArgTraits<Vs>::opt() && ...) &&
                    (!ArgTraits<Vs>::variadic() && ...) &&
                    ((ArgTraits<Vs>::name.empty() || ArgTraits<Vs>::name == name) && ...)
                )
            using bind = impl::BoundArg<opt, Vs...>;
        };
    };
};


/* Specialization for variadic positional args, whose names are prefixed by a leading
asterisk. */
template <static_str Name, typename T> requires (variadic_args_name<Name>)
struct Arg<Name, T> {
private:
    template <typename, typename>
    friend struct impl::detect_arg;
    using _detect_arg = void;

public:
    static constexpr static_str name = static_str<>::removeprefix<Name, "*">();
    static constexpr ArgKind kind = ArgKind::VAR | ArgKind::POS;
    using type = T;
    using bound_to = bertrand::args<>;
    using unbind = Arg;
    template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
    using with_name = Arg<"*" + N, T>;
    template <typename V> requires (!std::is_void_v<V>)
    using with_type = Arg<Name, V>;

    using vec = std::vector<std::conditional_t<
        std::is_lvalue_reference_v<T>,
        std::reference_wrapper<T>,
        std::remove_reference_t<T>
    >>;

    vec value;

    [[nodiscard]] constexpr vec& operator*() { return value; }
    [[nodiscard]] constexpr const vec& operator*() const { return value; }
    [[nodiscard]] constexpr vec* operator->() { return &value; }
    [[nodiscard]] constexpr const vec* operator->() const { return &value; }

    template <typename... Vs>
    static constexpr bool can_bind = false;
    template <std::convertible_to<T>... Vs>
    static constexpr bool can_bind<Vs...> =
        (ArgTraits<Vs>::posonly() && ...) &&
        (!ArgTraits<Vs>::opt() && ...) &&
        (!ArgTraits<Vs>::variadic() && ...) &&
        impl::names_are_unique<Vs...>;

    template <std::convertible_to<T>... Vs>
        requires (
            (ArgTraits<Vs>::posonly() && ...) &&
            (!ArgTraits<Vs>::opt() && ...) &&
            (!ArgTraits<Vs>::variadic() && ...) &&
            impl::names_are_unique<Vs...>
        )
    using bind = impl::BoundArg<Arg, Vs...>;

    [[nodiscard]] constexpr operator vec() && { return std::move(value); }
    template <typename U> requires (std::convertible_to<vec, U>)
    [[nodiscard]] constexpr operator U() && { return std::move(value); }
    template <typename... Us>
    [[nodiscard]] constexpr operator bind<Us...>() && {
        return {std::forward<type>(value)};
    }

    [[nodiscard]] constexpr decltype(auto) operator[](vec::size_type i) {
        return value[i];
    }
    [[nodiscard]] constexpr decltype(auto) operator[](vec::size_type i) const {
        return value[i];
    }
};


/* Specialization for variadic keyword args, whose names are prefixed by 2 leading
asterisks. */
template <static_str Name, typename T> requires (variadic_kwargs_name<Name>)
struct Arg<Name, T> {
private:
    template <typename, typename>
    friend struct impl::detect_arg;
    using _detect_arg = void;

public:
    static constexpr static_str name = static_str<>::removeprefix<Name, "**">();
    static constexpr ArgKind kind = ArgKind::VAR | ArgKind::KW;
    using type = T;
    using bound_to = bertrand::args<>;
    using unbind = Arg;
    template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
    using with_name = Arg<"**" + N, T>;
    template <typename V> requires (!std::is_void_v<V>)
    using with_type = Arg<Name, V>;

    using map = std::unordered_map<std::string, std::conditional_t<
        std::is_lvalue_reference_v<type>,
        std::reference_wrapper<type>,
        std::remove_reference_t<type>
    >>;

    map value;

    [[nodiscard]] constexpr map& operator*() { return value; }
    [[nodiscard]] constexpr const map& operator*() const { return value; }
    [[nodiscard]] constexpr map* operator->() { return &value; }
    [[nodiscard]] constexpr const map* operator->() const { return &value; }

    template <typename... Vs>
    static constexpr bool can_bind = false;
    template <std::convertible_to<T>... Vs>
    static constexpr bool can_bind<Vs...> =
        (ArgTraits<Vs>::kw() && ...) &&
        (!ArgTraits<Vs>::opt() && ...) &&
        (!ArgTraits<Vs>::variadic() && ...) &&
        impl::names_are_unique<Vs...>;

    template <std::convertible_to<T>... Vs>
        requires (
            (ArgTraits<Vs>::kw() && ...) &&
            (!ArgTraits<Vs>::opt() && ...) &&
            (!ArgTraits<Vs>::variadic() && ...) &&
            impl::names_are_unique<Vs...>
        )
    using bind = impl::BoundArg<Arg, Vs...>;

    [[nodiscard]] constexpr operator map() && { return std::move(value); }
    template <typename U> requires (std::convertible_to<map, U>)
    [[nodiscard]] constexpr operator U() && { return std::move(value); }
    template <typename... Us>
    [[nodiscard]] constexpr operator bind<Us...>() && {
        return {std::forward<type>(value)};
    }

    [[nodiscard]] constexpr decltype(auto) operator[](const std::string& key) {
        return value.at(key);
    }
    [[nodiscard]] constexpr decltype(auto) operator[](std::string&& key) {
        return value.at(std::move(key));
    }

};


/* A keyword parameter pack obtained by double-dereferencing a mapping-like container
within a Python-style function call. */
template <mapping_like T>
    requires (
        has_size<T> &&
        std::convertible_to<
            typename std::remove_reference_t<T>::key_type,
            std::string
        >
    )
struct KwargPack {
    using key_type = std::remove_reference_t<T>::key_type;
    using mapped_type = std::remove_reference_t<T>::mapped_type;

    static constexpr static_str name = "";
    static constexpr ArgKind kind = ArgKind::VAR | ArgKind::KW;
    using type = mapped_type;
    template <typename... Vs>
    using bind = KwargPack;
    using bound_to = bertrand::args<>;
    using unbind = KwargPack;
    template <static_str N>
    using with_name = KwargPack;
    template <typename V>
    using with_type = KwargPack;

    T value;

private:
    template <typename, typename>
    friend struct impl::detect_arg;
    using _detect_arg = void;

    template <typename U>
    static constexpr bool can_iterate =
        yields_pairs_with<U, key_type, mapped_type> ||
        has_items<U> ||
        (has_keys<U> && has_values<U>) ||
        (yields<U, key_type> && lookup_yields<U, mapped_type, key_type>) ||
        (has_keys<U> && lookup_yields<U, mapped_type, key_type>);

    auto transform() const {
        if constexpr (yields_pairs_with<T, key_type, mapped_type>) {
            return value;

        } else if constexpr (has_items<T>) {
            return value.items();

        } else if constexpr (has_keys<T> && has_values<T>) {
            return std::ranges::views::zip(value.keys(), value.values());

        } else if constexpr (
            yields<T, key_type> && lookup_yields<T, mapped_type, key_type>
        ) {
            return std::ranges::views::transform(
                value,
                [&](const key_type& key) {
                    return std::make_pair(key, value[key]);
                }
            );

        } else {
            return std::ranges::views::transform(
                value.keys(),
                [&](const key_type& key) {
                    return std::make_pair(key, value[key]);
                }
            );
        }
    }

public:

    auto size() const { return std::ranges::size(value); }
    template <typename U = T> requires (can_iterate<U>)
    auto begin() const { return std::ranges::begin(transform()); }
    template <typename U = T> requires (can_iterate<U>)
    auto cbegin() const { return begin(); }
    template <typename U = T> requires (can_iterate<U>)
    auto end() const { return std::ranges::end(transform()); }
    template <typename U = T> requires (can_iterate<U>)
    auto cend() const { return end(); }
};


/* A positional parameter pack obtained by dereferencing an iterable container within
a Python-style function call. */
template <iterable T> requires (has_size<T>)
struct ArgPack {
private:
    template <typename, typename>
    friend struct impl::detect_arg;
    using _detect_arg = void;

public:
    static constexpr static_str name = "";
    static constexpr ArgKind kind = ArgKind::VAR | ArgKind::POS;
    using type = iter_type<T>;
    template <typename... Vs>
    using bind = ArgPack;
    using bound_to = bertrand::args<>;
    using unbind = ArgPack;
    template <static_str N>
    using with_name = ArgPack;
    template <typename V>
    using with_type = ArgPack;

    T value;

    auto size() const { return std::ranges::size(value); }
    auto begin() const { return std::ranges::begin(value); }
    auto cbegin() const { return begin(); }
    auto end() const { return std::ranges::end(value); }
    auto cend() const { return end(); }

    template <typename U = T>
        requires (mapping_like<U> && std::convertible_to<
            typename std::remove_reference_t<U>::key_type,
            std::string
        >)
    auto operator*() const {
        return KwargPack<T>{std::forward<T>(value)};
    }
};


namespace impl {

    template <typename Arg, typename T> requires (!ArgTraits<Arg>::variadic())
    struct BoundArg<Arg, T> {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;
        using Value = std::remove_reference_t<typename ArgTraits<Arg>::type>;

    public:
        static constexpr static_str name = ArgTraits<Arg>::name;
        static constexpr ArgKind kind = ArgTraits<Arg>::kind;
        using type = ArgTraits<Arg>::type;
        using bound_to = bertrand::args<T>;
        using unbind = Arg;
        template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
        using with_name = BoundArg<
            typename ArgTraits<Arg>::template with_name<N>,
            T
        >;
        template <typename V>
            requires (
                !std::is_void_v<V> &&
                std::convertible_to<typename ArgTraits<T>::type, V>
            )
        using with_type = BoundArg<
            typename ArgTraits<Arg>::template with_type<V>,
            T
        >;

        type value;

        [[nodiscard]] constexpr Value& operator*() { return value; }
        [[nodiscard]] constexpr const Value& operator*() const { return value; }
        [[nodiscard]] constexpr Value* operator->() { return &value; }
        [[nodiscard]] constexpr const Value* operator->() const { return &value; }

        template <typename U> requires (std::convertible_to<unbind, U>)
        [[nodiscard]] constexpr operator U() && {
            if constexpr (is_arg<unbind>) {
                return unbind{std::forward<type>(value)};
            } else {
                return std::forward<type>(value);
            }
        }

        template <typename... Vs>
        static constexpr bool can_bind = ArgTraits<Arg>::template can_bind<Vs...>;
        template <typename... Vs> requires (can_bind<Vs...>)
        using bind = ArgTraits<Arg>::template bind<Vs...>;
    };

    template <typename Arg, typename... Ts>
        requires (ArgTraits<Arg>::args() && sizeof...(Ts) > 0)
    struct BoundArg<Arg, Ts...> {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

        template <typename, typename...>
        struct rebind;
        template <typename... curr, typename... Vs>
        struct rebind<bertrand::args<curr...>, Vs...> {
            static constexpr bool value =
                ArgTraits<Arg>::template can_bind<curr..., Vs...>;
            using type = ArgTraits<Arg>::template bind<curr..., Vs...>;
        };

    public:
        static constexpr static_str name = ArgTraits<Arg>::name;
        static constexpr ArgKind kind = ArgTraits<Arg>::kind;
        using type = ArgTraits<Arg>::type;
        using vec = std::vector<std::conditional_t<
            std::is_lvalue_reference_v<type>,
            std::reference_wrapper<type>,
            std::remove_reference_t<type>
        >>;
        using bound_to = bertrand::args<Ts...>;
        using unbind = Arg;
        template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
        using with_name = BoundArg<
            typename ArgTraits<Arg>::template with_name<N>,
            Ts...
        >;
        template <typename V>
            requires (
                !std::is_void_v<V> &&
                (std::convertible_to<typename ArgTraits<Ts>::type, V> && ...)
            )
        using with_type = BoundArg<
            typename ArgTraits<Arg>::template with_type<V>,
            Ts...
        >;

        vec value;

        [[nodiscard]] constexpr vec& operator*() { return value; }
        [[nodiscard]] constexpr const vec& operator*() const { return value; }
        [[nodiscard]] constexpr vec* operator->() { return &value; }
        [[nodiscard]] constexpr const vec* operator->() const { return &value; }
        template <typename U> requires (std::convertible_to<unbind, U>)
        [[nodiscard]] constexpr operator U() && {
            return unbind{std::forward<type>(value)};
        }

        [[nodiscard]] constexpr decltype(auto) operator[](vec::size_type i) {
            return value[i];
        }
        [[nodiscard]] constexpr decltype(auto) operator[](vec::size_type i) const {
            return value[i];
        }

        template <typename... Vs>
        static constexpr bool can_bind = rebind<bound_to, Vs...>::value;
        template <typename... Vs> requires (can_bind<Vs...>)
        using bind = rebind<bound_to, Vs...>::type;
    };

    template <typename Arg, typename... Ts>
        requires (ArgTraits<Arg>::kwargs() && sizeof...(Ts) > 0)
    struct BoundArg<Arg, Ts...> {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

        template <typename, typename...>
        struct rebind;
        template <typename... curr, typename... Vs>
        struct rebind<bertrand::args<curr...>, Vs...> {
            static constexpr bool value =
                ArgTraits<Arg>::template can_bind<curr..., Vs...>;
            using type = ArgTraits<Arg>::template bind<curr..., Vs...>;
        };

    public:
        static constexpr static_str name = ArgTraits<Arg>::name;
        static constexpr ArgKind kind = ArgTraits<Arg>::kind;
        using type = ArgTraits<Arg>::type;
        using map = std::unordered_map<std::string, std::conditional_t<
            std::is_lvalue_reference_v<type>,
            std::reference_wrapper<type>,
            std::remove_reference_t<type>
        >>;
        using bound_to = bertrand::args<Ts...>;
        using unbind = Arg;
        template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
        using with_name = BoundArg<
            typename ArgTraits<Arg>::template with_name<N>,
            Ts...
        >;
        template <typename V>
            requires (
                !std::is_void_v<V> &&
                (std::convertible_to<typename ArgTraits<Ts>::type, V> && ...)
            )
        using with_type = BoundArg<
            typename ArgTraits<Arg>::template with_type<V>,
            Ts...
        >;

        map value;

        [[nodiscard]] constexpr map& operator*() { return value; }
        [[nodiscard]] constexpr const map& operator*() const { return value; }
        [[nodiscard]] constexpr map* operator->() { return &value; }
        [[nodiscard]] constexpr const map* operator->() const { return &value; }
        template <typename U> requires (std::convertible_to<unbind, U>)
        [[nodiscard]] constexpr operator U() && {
            return unbind{std::forward<type>(value)};
        }

        [[nodiscard]] constexpr decltype(auto) operator[](const std::string& key) {
            return value[key];
        }
        [[nodiscard]] constexpr decltype(auto) operator[](std::string&& key) {
            return value[std::move(key)];
        }

        template <typename... Vs>
        static constexpr bool can_bind = rebind<bound_to, Vs...>::value;
        template <typename... Vs> requires (can_bind<Vs...>)
        using bind = rebind<bound_to, Vs...>::type;
    };

    /* A singleton argument factory that allows keyword arguments to be constructed via
    familiar assignment syntax, which extends the lifetime of temporaries. */
    template <static_str Name>
    struct ArgFactory {
        template <typename T>
        constexpr Arg<Name, T>::kw operator=(T&& value) const {
            return {std::forward<T>(value)};
        }
    };

    struct signature_tag {};
    struct signature_partial_tag {};
    struct signature_defaults_tag {};
    struct signature_bind_tag {};
    struct def_tag {};

    template <typename R>
    struct signature_base : signature_tag {
        using Return = R;
    };

    inline std::string format_signature(
        const std::string& prefix,
        size_t max_width,
        size_t indent,
        std::vector<std::string>& components,
        size_t last_posonly,
        size_t first_kwonly
    ) {
        std::string param_open          = "(";
        std::string param_close         = ") -> ";
        std::string type_sep            = ": ";
        std::string default_sep         = " = ";
        std::string sep                 = ", ";
        std::string tab                 = std::string(indent, ' ');
        std::string line_sep            = "\n";
        std::string kwonly_sep          = "*";
        std::string posonly_sep         = "/";

        components.front() += param_open;
        components.back() = param_close + components.back();

        // add delimiters to parameters and compute hypothetical one-liner length
        size_t length = prefix.size() + components.front().size();
        if (components.size() > 2) {
            std::string& name = components[1];
            std::string& type = components[2];
            std::string& default_value = components[3];
            type = type_sep + type;
            if (!default_value.empty()) {
                default_value = default_sep + default_value;
            }
            length += name.size() + type.size() + default_value.size();
            if (length <= max_width) {
                for (size_t i = 4, end = components.size() - 1; i < end; i += 3) {
                    length += sep.size();
                    std::string& name = components[i];
                    std::string& type = components[i + 1];
                    std::string& default_value = components[i + 2];
                    name += type_sep;
                    if (!default_value.empty()) {
                        default_value = default_sep + default_value;
                    }
                    length += name.size() + type.size() + default_value.size();
                    size_t adjusted = (i - 4) / 3;
                    if (adjusted == last_posonly) {
                        length += sep.size() + posonly_sep.size();
                    } else if (adjusted == first_kwonly) {
                        length += sep.size() + kwonly_sep.size();
                    }
                }
            }
        }
        length += components.back().size();

        // if the whole signature fits on one line, return it as such
        if (length <= max_width) {
            std::string out;
            out.reserve(length);
            out += prefix;
            out += std::move(components.front());
            if (components.size() > 2) {
                size_t i = 1;
                size_t j = 0;
                if (j == first_kwonly) {
                    out += kwonly_sep + sep;
                }
                out += std::move(components[i++]);
                out += std::move(components[i++]);
                out += std::move(components[i++]);
                if (j == last_posonly) {
                    out += sep + posonly_sep;
                }
                ++j;
                for (size_t end = components.size() - 1; i < end; ++j) {
                    out += sep;
                    if (j == first_kwonly) {
                        out += kwonly_sep + sep;
                    }
                    out += std::move(components[i++]);
                    out += std::move(components[i++]);
                    out += std::move(components[i++]);
                    if (j == last_posonly) {
                        out += sep + posonly_sep;
                    }
                }
            }
            out += std::move(components.back());
            return out;
        }

        // otherwise, indent the parameters onto separate lines
        std::string out = prefix + components.front() + line_sep;
        std::string line = prefix + tab;
        if (components.size() > 2) {
            size_t i = 1;
            size_t j = 0;
            if (j == first_kwonly) {
                out += line + kwonly_sep + sep + line_sep;
            }
            std::string& name = components[i++];
            std::string& type = components[i++];
            std::string& default_value = components[i++];
            line += std::move(name);
            if (line.size() + type.size() <= max_width) {
                line += std::move(type);
            } else {
                out += std::move(line) + line_sep;
                line = prefix + tab + tab + std::move(type);
            }
            if (line.size() + default_value.size() <= max_width) {
                line += std::move(default_value);
            } else {
                out += std::move(line) + line_sep;
                line = prefix + tab + tab +
                    std::move(default_value).substr(1);  // remove leading space
            }
            out += line;
            if (j == last_posonly) {
                out += sep + line_sep + prefix + tab + posonly_sep;
            }
            for (size_t end = components.size() - 1; i < end; ++j) {
                out += sep + line_sep;
                line = prefix + tab;
                if (j == first_kwonly) {
                    out += line + kwonly_sep + sep + line_sep;
                }
                std::string& name = components[i++];
                std::string& type = components[i++];
                std::string& default_value = components[i++];
                line += std::move(name);
                if (line.size() + type.size() <= max_width) {
                    line += std::move(type);
                } else {
                    out += std::move(line) + line_sep;
                    line = prefix + tab + tab + std::move(type);
                }
                if (line.size() + default_value.size() <= max_width) {
                    line += std::move(default_value);
                } else {
                    out += std::move(line) + line_sep;
                    line = prefix + tab + tab +
                        std::move(default_value).substr(1);  // remove leading space
                }
                out += std::move(line);
                if (j == last_posonly) {
                    out += sep + line_sep + prefix + tab + posonly_sep;
                }
            }
            out += line_sep;
        }
        out += prefix + components.back();
        return out;
    }

    template <typename R, typename C, size_t I>
    struct chain_return_type { using type = R; };
    template <typename R, typename C, size_t I> requires (I < C::size())
    struct chain_return_type<R, C, I> {
        using type = chain_return_type<
            std::invoke_result_t<typename C::template at<I>, R>,
            C,
            I + 1
        >::type;
    };

}


/* A compile-time factory for binding keyword arguments with Python syntax.  constexpr
instances of this class can be used to provide an even more Pythonic syntax:

    constexpr auto x = arg<"x">;
    my_func(x = 42);
*/
template <static_str name>
    requires (
        arg_name<name> &&
        !variadic_args_name<name> &&
        !variadic_kwargs_name<name>
    )
constexpr impl::ArgFactory<name> arg {};


/* Manipulate a C++ argument annotation at compile time.  Unannotated types are
treated as anonymous, positional-only, and required in order to preserve C++ style. */
template <typename T>
struct ArgTraits {
private:
    template <static_str N>
    struct _with_name { using type = Arg<N, T>::pos; };
    template <static_str N> requires (N.empty())
    struct _with_name<N> { using type = T; };

public:
    using type                                  = T;
    static constexpr static_str name             = "";
    static constexpr ArgKind kind               = ArgKind::POS;
    static constexpr bool posonly() noexcept    { return kind.posonly(); }
    static constexpr bool pos() noexcept        { return kind.pos(); }
    static constexpr bool args() noexcept       { return kind.args(); }
    static constexpr bool kwonly() noexcept     { return kind.kwonly(); }
    static constexpr bool kw() noexcept         { return kind.kw(); }
    static constexpr bool kwargs() noexcept     { return kind.kwargs(); }
    static constexpr bool bound() noexcept      { return bound_to::n > 0; }
    static constexpr bool opt() noexcept        { return kind.opt(); }
    static constexpr bool variadic() noexcept   { return kind.variadic(); }

    template <typename... Vs>
    static constexpr bool can_bind = false;
    template <std::convertible_to<type> V>
    static constexpr bool can_bind<V> =
        ArgTraits<V>::posonly() &&
        !ArgTraits<V>::opt() &&
        !ArgTraits<V>::variadic() &&
        (ArgTraits<V>::name.empty() || ArgTraits<V>::name == name);

    template <typename... Vs> requires (can_bind<Vs...>)
    using bind                                  = impl::BoundArg<T, Vs...>;
    using bound_to                              = bertrand::args<>;
    using unbind                                = T;
    template <static_str N>
        requires (N.empty() || arg_name<N> && !static_str<>::startswith<N, "*">())
    using with_name                             = _with_name<N>::type;
    template <typename V> requires (!std::is_void_v<V>)
    using with_type                             = V;
};


/* Manipulate a C++ argument annotation at compile time.  Forwards to the annotated
type's interface where possible. */
template <is_arg T>
struct ArgTraits<T> {
private:
    using T2 = std::remove_cvref_t<T>;

    template <static_str N>
    struct _with_name { using type = T2::template with_name<N>; };
    template <static_str N> requires (N.empty())
    struct _with_name<N> { using type = T2::type; };

public:
    using type                                  = T2::type;
    static constexpr static_str name            = T2::name;
    static constexpr ArgKind kind               = T2::kind;
    static constexpr bool posonly() noexcept    { return kind.posonly(); }
    static constexpr bool pos() noexcept        { return kind.pos(); }
    static constexpr bool args() noexcept       { return kind.args(); }
    static constexpr bool kwonly() noexcept     { return kind.kwonly(); }
    static constexpr bool kw() noexcept         { return kind.kw(); }
    static constexpr bool kwargs() noexcept     { return kind.kwargs(); }
    static constexpr bool bound() noexcept      { return bound_to::n > 0; }
    static constexpr bool opt() noexcept        { return kind.opt(); }
    static constexpr bool variadic() noexcept   { return kind.variadic(); }

    template <typename... Vs>
    static constexpr bool can_bind = T2::template can_bind<Vs...>;

    template <typename... Vs> requires (can_bind<Vs...>)
    using bind                                  = T2::template bind<Vs...>;
    using unbind                                = T2::unbind;
    using bound_to                              = T2::bound_to;
    template <static_str N>
        requires (N.empty() || arg_name<N> && !static_str<>::startswith<N, "*">())
    using with_name                             = _with_name<N>::type;
    template <typename V> requires (!std::is_void_v<V>)
    using with_type                             = T2::template with_type<V>;
};


/* Introspect an annotated C++ function signature to extract compile-time type
information and allow matching functions to be called using Python-style conventions.
Also defines supporting data structures to allow for partial function application. */
template <typename T>
struct signature {
    static constexpr bool enable = false;
};


/* CTAD guide to simplify signature introspection.  Uses a dummy constructor, meaning
no work is done at runtime. */
template <typename T> requires (signature<T>::enable)
signature(const T&) -> signature<typename signature<T>::type>;


/* The canonical form of `signature`, which encapsulates all of the internal call
machinery, as much as possible of which is evaluated at compile time.  All other
specializations should redirect to this form in order to avoid reimplementing the nuts
and bolts of the function ecosystem. */
template <typename Return, typename... Args>
struct signature<Return(Args...)> : impl::signature_base<Return> {
    static constexpr bool enable = true;
    using type = Return(Args...);

    struct Partial;
    struct Defaults;
    template <typename... Values>
    struct Bind;

    /* Dummy constructor for CTAD purposes. */
    template <typename T> requires (signature<T>::enable)
    constexpr signature(const T&) noexcept {}
    constexpr signature() noexcept = default;

protected:
    template <typename...>
    static constexpr size_t _n_posonly = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _n_posonly<T, Ts...> =
        _n_posonly<Ts...> + ArgTraits<T>::posonly();

    template <typename...>
    static constexpr size_t _n_pos = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _n_pos<T, Ts...> =
        _n_pos<Ts...> + ArgTraits<T>::pos();

    template <typename...>
    static constexpr size_t _n_kw = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _n_kw<T, Ts...> =
        _n_kw<Ts...> + ArgTraits<T>::kw();

    template <typename...>
    static constexpr size_t _n_kwonly = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _n_kwonly<T, Ts...> =
        _n_kwonly<Ts...> + ArgTraits<T>::kwonly();

    template <static_str, typename...>
    static constexpr size_t _idx = 0;
    template <static_str Name, typename T, typename... Ts>
    static constexpr size_t _idx<Name, T, Ts...> =
        ArgTraits<T>::name == Name ? 0 : _idx<Name, Ts...> + 1;

    template <typename...>
    static constexpr size_t _posonly_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _posonly_idx<T, Ts...> =
        ArgTraits<T>::posonly() ? 0 : _posonly_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _pos_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _pos_idx<T, Ts...> =
        ArgTraits<T>::pos() ? 0 : _pos_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _args_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _args_idx<T, Ts...> =
        ArgTraits<T>::args() ? 0 : _args_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _kw_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _kw_idx<T, Ts...> =
        ArgTraits<T>::kw() ? 0 : _kw_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _kwonly_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _kwonly_idx<T, Ts...> =
        ArgTraits<T>::kwonly() ? 0 : _kwonly_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _kwargs_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _kwargs_idx<T, Ts...> =
        ArgTraits<T>::kwargs() ? 0 : _kwargs_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _opt_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _opt_idx<T, Ts...> =
        ArgTraits<T>::opt() ? 0 : _opt_idx<Ts...> + 1;

    /* An single element stored in a ::Partial or ::Defaults tuple, which can be easily
    cross-referenced against the enclosing signature. */
    template <size_t I, static_str Name, typename T>
    struct Element {
        static constexpr size_t index = I;
        static constexpr static_str name = Name;
        using type = T;
        std::remove_cvref_t<type> value;
        constexpr remove_rvalue<type> get() const { return value; }
        constexpr remove_lvalue<type> get() && { return std::move(value); }
    };

    /* A temporary container describing the contents of a `*` unpacking operator at a
    function's call site.  Encloses an iterator over the unpacked container, which is
    incremented every time an argument is consumed from the pack.  If it is not empty
    by the end of the call, then we know extra arguments were given that could not be
    matched. */
    template <typename Pack> requires (ArgTraits<Pack>::args())
    struct PositionalPack {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

    public:
        static constexpr static_str name = Pack::name;
        static constexpr ArgKind kind = Pack::kind;
        using type = Pack::type;
        template <typename... Vs>
        using bind = Pack::template bind<Vs...>;
        using bound_to = Pack::bound_to;
        using unbind = Pack::unbind;
        template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
        using with_name = Pack::template with_name<N>;
        template <typename V> requires (!std::is_void_v<V>)
        using with_type = Pack::template with_type<V>;

        std::ranges::iterator_t<const Pack&> begin;
        std::ranges::sentinel_t<const Pack&> end;
        size_t size;

        PositionalPack(const Pack& pack) :
            begin(std::ranges::begin(pack)),
            end(std::ranges::end(pack)),
            size(std::ranges::size(pack))
        {}

        bool has_value() const {
            return begin != end;
        }

        decltype(auto) value() {
            decltype(auto) result = *begin;
            ++begin;
            return result;
        }

        void validate() {
            if constexpr (!signature::has_args) {
                if (begin != end) {
                    std::string message =
                        "too many arguments in positional parameter pack: ['" +
                        repr(*begin);
                    while (++begin != end) {
                        message += "', '" + repr(*begin);
                    }
                    message += "']";
                    throw std::runtime_error(message);
                }
            }
        }
    };

    /* A temporary container describing the contents of a `**` unpacking operator at a
    function's call site.  Encloses an unordered map of strings to values, which is
    destructively searched every time an argument is consumed from the pack.  If the
    map is not empty by the end of the call, then we know extra arguments were given
    that could not be matched. */
    template <typename Pack> requires (ArgTraits<Pack>::kwargs())
    struct KeywordPack {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

        struct Hash {
            using is_transparent = void;
            static constexpr size_t operator()(std::string_view str) {
                return std::hash<std::string_view>{}(str);
            }
        };

        struct Equal {
            using is_transparent = void;
            static constexpr bool operator()(std::string_view lhs, std::string_view rhs) {
                return lhs == rhs;
            }
        };

    public:
        static constexpr static_str name = Pack::name;
        static constexpr ArgKind kind = Pack::kind;
        using type = Pack::type;
        template <typename... Vs>
        using bind = Pack::template bind<Vs...>;
        using bound_to = Pack::bound_to;
        using unbind = Pack::unbind;
        template <static_str N> requires (arg_name<N> && !static_str<>::startswith<N, "*">())
        using with_name = Pack::template with_name<N>;
        template <typename V> requires (!std::is_void_v<V>)
        using with_type = Pack::template with_type<V>;

        using Map = std::unordered_map<
            std::string,
            typename Pack::mapped_type,
            Hash,
            Equal
        >;
        Map map;

        KeywordPack(const Pack& pack) :
            map([](const Pack& pack) {
                Map map;
                map.reserve(pack.size());
                for (auto&& [key, value] : pack) {
                    auto [it, inserted] = map.emplace(
                        std::forward<decltype(key)>(key),
                        std::forward<decltype(value)>(value)
                    );
                    if (!inserted) {
                        throw std::runtime_error(
                            "duplicate keyword argument: '" + it->first + "'"
                        );
                    }
                }
                return map;
            }(pack))
        {}

        auto size() const { return map.size(); }
        auto empty() const { return map.empty(); }
        auto begin() { return map.begin(); }
        auto end() { return map.end(); }

        template <typename T>
        auto extract(T&& key) {
            return map.extract(std::forward<T>(key));
        }

        void validate() {
            if constexpr (!signature::has_kwargs) {
                if (!map.empty()) {
                    auto it = map.begin();
                    auto end = map.end();
                    std::string message =
                        "unexpected keyword arguments: ['" + it->first;
                    while (++it != end) {
                        message += "', '" + it->first;
                    }
                    message += "']";
                    throw std::runtime_error(message);
                }
            }
        }
    };

    template <typename Pack>
    PositionalPack(const Pack&) -> PositionalPack<Pack>;
    template <typename Pack>
    KeywordPack(const Pack&) -> KeywordPack<Pack>;

    template <typename F, typename... A>
    static constexpr decltype(auto) invoke_with_packs(F&& func, A&&... args) {
        using src = signature<Return(A...)>;
        if constexpr (src::has_args && src::has_kwargs) {
            return []<size_t... Prev, size_t... Next>(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                auto&& func,
                auto&&... args
            ) {
                return std::forward<decltype(func)>(func)(
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    PositionalPack(unpack_arg<src::args_idx>(
                        std::forward<decltype(args)>(args)...
                    )),
                    unpack_arg<src::args_idx + 1 + Next>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    KeywordPack(unpack_arg<src::kwargs_idx>(
                        std::forward<decltype(args)>(args)...
                    ))
                );
            }(
                std::make_index_sequence<src::args_idx>{},
                std::make_index_sequence<
                    src::kwargs_idx - (src::args_idx + 1)
                >{},
                std::forward<F>(func),
                std::forward<A>(args)...
            );
        } else if constexpr (src::has_args) {
            return []<size_t... Prev, size_t... Next>(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                auto&& func,
                auto&&... args
            ) {
                return std::forward<decltype(func)>(func)(
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    PositionalPack(unpack_arg<src::args_idx>(
                        std::forward<decltype(args)>(args)...
                    )),
                    unpack_arg<src::args_idx + 1 + Next>(
                        std::forward<decltype(args)>(args)...
                    )...
                );
            }(
                std::make_index_sequence<src::args_idx>{},
                std::make_index_sequence<src::n - (src::args_idx + 1)>{},
                std::forward<F>(func),
                std::forward<A>(args)...
            );
        } else if constexpr (src::has_kwargs) {
            return []<size_t... Prev>(
                std::index_sequence<Prev...>,
                auto&& func,
                auto&&... args
            ) {
                return std::forward<decltype(func)>(func)(
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    KeywordPack(unpack_arg<src::kwargs_idx>(
                        std::forward<decltype(args)>(args)...
                    ))
                );
            }(
                std::make_index_sequence<src::kwargs_idx>{},
                std::forward<F>(func),
                std::forward<A>(args)...
            );
        } else {
            return std::forward<F>(func)(std::forward<A>(args)...);
        }
    }

    template <size_t I, typename T> requires (I < signature::n)
    static constexpr auto to_arg(T&& value) -> unpack_type<I, Args...> {
        if constexpr (is_arg<unpack_type<I, Args...>>) {
            return {std::forward<T>(value)};
        } else {
            return std::forward<T>(value);
        }
    };

public:
    static constexpr size_t n                   = sizeof...(Args);
    static constexpr size_t n_posonly           = _n_posonly<Args...>;
    static constexpr size_t n_pos               = _n_pos<Args...>;
    static constexpr size_t n_kw                = _n_kw<Args...>;
    static constexpr size_t n_kwonly            = _n_kwonly<Args...>;

    template <static_str Name>
    static constexpr bool has                   = n > _idx<Name, Args...>;
    static constexpr bool has_posonly           = n_posonly > 0;
    static constexpr bool has_pos               = n_pos > 0;
    static constexpr bool has_kw                = n_kw > 0;
    static constexpr bool has_kwonly            = n_kwonly > 0;
    static constexpr bool has_args              = n > _args_idx<Args...>;
    static constexpr bool has_kwargs            = n > _kwargs_idx<Args...>;

    template <static_str Name> requires (has<Name>)
    static constexpr size_t idx                 = _idx<Name, Args...>;
    static constexpr size_t posonly_idx         = _posonly_idx<Args...>;
    static constexpr size_t pos_idx             = _pos_idx<Args...>;
    static constexpr size_t kw_idx              = _kw_idx<Args...>;
    static constexpr size_t kwonly_idx          = _kwonly_idx<Args...>;
    static constexpr size_t args_idx            = _args_idx<Args...>;
    static constexpr size_t kwargs_idx          = _kwargs_idx<Args...>;
    static constexpr size_t opt_idx             = _opt_idx<Args...>;

    template <size_t I> requires (I < n)
    using at = unpack_type<I, Args...>;

    template <typename R>
    using with_return = signature<R(Args...)>;

    template <typename... As>
    using with_args = signature<Return(As...)>;

private:
    template <size_t>
    static constexpr bool _proper_argument_order = true;
    template <size_t I> requires (I < n)
    static constexpr bool _proper_argument_order<I> = [] {
        using T = at<I>;
        return !((
            ArgTraits<T>::posonly() && (
                (I > std::min({
                    args_idx,
                    kw_idx,
                    kwargs_idx
                })) ||
                (!ArgTraits<T>::opt() && I > opt_idx)
            )
        ) || (
            ArgTraits<T>::pos() && (
                (I > std::min({
                    args_idx,
                    kwonly_idx,
                    kwargs_idx
                })) ||
                (!ArgTraits<T>::opt() && I > opt_idx)
            )
        ) || (
            ArgTraits<T>::args() && (I > std::min(
                kwonly_idx,
                kwargs_idx
            ))
        ) || (
            ArgTraits<T>::kwonly() && (I > kwargs_idx)
        )) && _proper_argument_order<I + 1>;
    }();

    template <size_t>
    static constexpr bool _no_qualified_args = true;
    template <size_t I> requires (I < n)
    static constexpr bool _no_qualified_args<I> = [] {
        using T = ArgTraits<at<I>>::type;
        return !(
            std::is_reference_v<T> ||
            std::is_const_v<std::remove_reference_t<T>> ||
            std::is_volatile_v<std::remove_reference_t<T>>
        ) && _no_qualified_args<I + 1>;
    }();

    template <size_t>
    static constexpr bool _no_qualified_arg_annotations = true;
    template <size_t I> requires (I < n)
    static constexpr bool _no_qualified_arg_annotations<I> = [] {
        using T = at<I>;
        return !(is_arg<T> && (
            std::is_reference_v<T> ||
            std::is_const_v<std::remove_reference_t<T>> ||
            std::is_volatile_v<std::remove_reference_t<T>>
        )) && _no_qualified_arg_annotations<I + 1>;
    }();

    template <size_t>
    static constexpr bool _no_duplicate_args = true;
    template <size_t I> requires (I < n)
    static constexpr bool _no_duplicate_args<I> = [] {
        using T = at<I>;
        return !(
            (ArgTraits<T>::name != "" && I != idx<ArgTraits<T>::name>) ||
            (ArgTraits<T>::args() && I != args_idx) ||
            (ArgTraits<T>::kwargs() && I != kwargs_idx)
        ) && _no_duplicate_args<I + 1>;
    }();

public:
    /* True if a given function can be called with this signature's arguments and
    returns a compatible type, after accounting for implicit conversions. */
    template <typename Func>
    static constexpr bool invocable =
        std::is_invocable_r_v<Return, Func, Args...>;

    /* True if the arguments are given in the proper order (no positional after keyword,
    no required after optional, etc.). */
    static constexpr bool proper_argument_order =
        _proper_argument_order<0>;

    /* True if the return type lacks cvref qualifications. */
    static constexpr bool no_qualified_return = !(
        std::is_reference_v<Return> ||
        std::is_const_v<std::remove_reference_t<Return>> ||
        std::is_volatile_v<std::remove_reference_t<Return>>
    );

    /* True if the argument types lack cvref qualifications. */
    static constexpr bool no_qualified_args =
        _no_qualified_args<0>;

    /* True if none of the `Arg<>` annotations are themselves cvref-qualified. */
    static constexpr bool no_qualified_arg_annotations =
        _no_qualified_arg_annotations<0>;

    /* True if there are no duplicate parameter names and at most one variadic
    positional/keyword argument, respectively. */
    static constexpr bool no_duplicate_args =
        _no_duplicate_args<0>;

    /* A tuple holding a partial value for every bound argument in the enclosing
    parameter list.  One of these must be provided whenever a C++ function is
    invoked, and constructing one requires that the initializers match a
    sub-signature consisting only of the bound args as positional-only and
    keyword-only parameters for clarity.  The result may be empty if there are no
    bound arguments in the enclosing signature, in which case the constructor will
    be optimized out. */
    struct Partial : impl::signature_partial_tag {
    protected:
        friend signature;

        template <typename...>
        static constexpr size_t _n_posonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_posonly<T, Ts...> =
            _n_posonly<Ts...> + (ArgTraits<T>::posonly() && ArgTraits<T>::bound());

        template <typename...>
        static constexpr size_t _n_pos = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_pos<T, Ts...> =
            _n_pos<Ts...> + (ArgTraits<T>::pos() && ArgTraits<T>::bound());

        template <bool>
        static constexpr size_t _n_args = 0;
        template <>
        static constexpr size_t _n_args<true> = 
            ArgTraits<signature::at<signature::args_idx>>::bound_to::n;

        template <typename...>
        static constexpr size_t _n_kw = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kw<T, Ts...> =
            _n_kw<Ts...> + (ArgTraits<T>::kw() && ArgTraits<T>::bound());

        template <typename...>
        static constexpr size_t _n_kwonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kwonly<T, Ts...> =
            _n_kwonly<Ts...> + (ArgTraits<T>::kwonly() && ArgTraits<T>::bound());

        template <bool>
        static constexpr size_t _n_kwargs = 0;
        template <>
        static constexpr size_t _n_kwargs<true> =
            ArgTraits<signature::at<signature::kwargs_idx>>::bound_to::n;

        template <typename...>
        static constexpr size_t _posonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _posonly_idx<T, Ts...> =
            ArgTraits<T>::posonly() && ArgTraits<T>::bound() ?
                0 : 1 + _posonly_idx<Ts...>;

        template <typename...>
        static constexpr size_t _pos_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _pos_idx<T, Ts...> =
            ArgTraits<T>::pos() && ArgTraits<T>::bound() ?
                0 : 1 + _pos_idx<Ts...>;

        template <typename...>
        static constexpr size_t _kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kw_idx<T, Ts...> =
            ArgTraits<T>::kw() && ArgTraits<T>::bound() ?
                0 : 1 + _kw_idx<Ts...>;

        template <typename...>
        static constexpr size_t _kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwonly_idx<T, Ts...> =
            ArgTraits<T>::kwonly() && ArgTraits<T>::bound() ?
                0 : 1 + _kwonly_idx<Ts...>;

        /* Build a sub-signature holding only the bound arguments from the enclosing
        signature. */
        template <typename out, typename...>
        struct extract { using type = out; };
        template <typename... out, typename A, typename... As>
        struct extract<signature<void(out...)>, A, As...> {
            template <typename>
            struct sub_signature { using type = signature<void(out...)>; };
            template <typename T> requires (ArgTraits<T>::bound())
            struct sub_signature<T> {
                template <typename>
                struct extend;
                template <typename... Ps>
                struct extend<args<Ps...>> {
                    template <typename P>
                    struct to_partial { using type = P; };
                    template <typename P> requires (ArgTraits<P>::kw())
                    struct to_partial<P> {
                        using type = Arg<
                            ArgTraits<P>::name,
                            typename ArgTraits<P>::type
                        >::kw;
                    };
                    using type = signature<void(out..., typename to_partial<Ps>::type...)>;
                };
                using type = extend<typename ArgTraits<T>::bound_to>::type;
            };
            using type = extract<
                typename sub_signature<A>::type,
                As...
            >::type;
        };
        using Inner = extract<signature<void()>, Args...>::type;

        /* Build a std::tuple of Elements that hold the bound values in a way that can
        be cross-referenced with the target signature. */
        template <typename out, size_t, typename...>
        struct collect { using type = out; };
        template <typename... out, size_t I, typename A, typename... As>
        struct collect<std::tuple<out...>, I, A, As...> {
            template <typename>
            struct tuple { using type = std::tuple<out...>; };
            template <typename T> requires (ArgTraits<T>::bound())
            struct tuple<T> {
                template <typename P>
                struct to_element {
                    using type = Element<
                        I,
                        ArgTraits<A>::name,
                        typename ArgTraits<P>::type
                    >;
                };
                template <typename P> requires (ArgTraits<A>::variadic())
                struct to_element<P> {
                    using type = Element<
                        I,
                        ArgTraits<P>::name,
                        typename ArgTraits<P>::type
                    >;
                };
                template <typename>
                struct extend;
                template <typename... Ps>
                struct extend<args<Ps...>> {
                    using type = std::tuple<out..., typename to_element<Ps>::type...>;
                };
                using type = extend<typename ArgTraits<T>::bound_to>::type;
            };
            using type = collect<typename tuple<A>::type, I + 1, As...>::type;
        };
        using Tuple = collect<std::tuple<>, 0, Args...>::type;

        template <static_str Name, size_t I>
        static constexpr size_t tuple_idx = I;
        template <static_str Name, size_t I> requires (I < std::tuple_size_v<Tuple>)
        static constexpr size_t tuple_idx<Name, I> =
            std::tuple_element_t<I, Tuple>::name == Name ? I : tuple_idx<Name, I + 1>;

        template <size_t K>
        struct _at {
            using type = signature::at<std::tuple_element_t<K, Tuple>::index>;
        };
        template <size_t K>
            requires (
                signature::has_args &&
                std::tuple_element_t<K, Tuple>::index == signature::args_idx
            )
        struct _at<K> {
            using type = Inner::template at<K>;
        };
        template <size_t K>
            requires (
                signature::has_kwargs &&
                std::tuple_element_t<K, Tuple>::index == signature::kwargs_idx
            )
        struct _at<K> {
            using type = Arg<
                std::tuple_element_t<K, Tuple>::name,
                typename std::tuple_element_t<K, Tuple>::type
            >::kw;
        };

        Tuple values;

        template <size_t K, typename... As>
        static constexpr decltype(auto) build(As&&... args) {
            using sig = signature<void(As...)>;
            using T = std::tuple_element_t<K, Tuple>;
            if constexpr (T::name.empty() || !sig::template has<T::name>) {
                return unpack_arg<K>(std::forward<As>(args)...);
            } else {
                constexpr size_t idx = sig::template idx<T::name>;
                return unpack_arg<idx>(std::forward<As>(args)...);
            }
        }

    public:
        using type = signature;

        static constexpr size_t n               = Inner::n;
        static constexpr size_t n_posonly       = _n_posonly<Args...>;
        static constexpr size_t n_pos           = _n_pos<Args...>;
        static constexpr size_t n_args          = _n_args<signature::has_args>;
        static constexpr size_t n_kw            = _n_kw<Args...>;
        static constexpr size_t n_kwonly        = _n_kwonly<Args...>;
        static constexpr size_t n_kwargs        = _n_kwargs<signature::has_kwargs>;

        template <static_str Name>
        static constexpr bool has               = tuple_idx<Name, 0> < n;
        static constexpr bool has_posonly       = n_posonly > 0;
        static constexpr bool has_pos           = n_pos > 0;
        static constexpr bool has_args          = n_args > 0;
        static constexpr bool has_kw            = n_kw > 0;
        static constexpr bool has_kwonly        = n_kwargs > 0;
        static constexpr bool has_kwargs        = n_kwargs > 0;

        template <static_str Name> requires (has<Name>)
        static constexpr size_t idx             = tuple_idx<Name, 0>;
        static constexpr size_t posonly_idx     = _posonly_idx<Args...>;
        static constexpr size_t pos_idx         = _pos_idx<Args...>;
        static constexpr size_t args_idx        = signature::args_idx * has_args;
        static constexpr size_t kw_idx          = _kw_idx<Args...>;
        static constexpr size_t kwonly_idx      = _kwonly_idx<Args...>;
        static constexpr size_t kwargs_idx      = signature::kwargs_idx * has_kwargs;

        /* Get the recorded name of the bound argument at index K of the partial
        tuple. */
        template <size_t K> requires (K < n)
        static constexpr static_str name = std::tuple_element_t<K, Tuple>::name;

        /* Given an index into the partial tuple, find the corresponding index in
        the enclosing parameter list. */
        template <size_t K> requires (K < n)
        static constexpr size_t rfind = std::tuple_element_t<K, Tuple>::index;

        template <size_t K> requires (K < n)
        using at = _at<K>::type;

        /* Bind an argument list to the partial values to enable the constructor. */
        template <typename... As>
        using Bind = Inner::template Bind<As...>;

        template <typename... As>
            requires (
                !(arg_pack<As> || ...) &&
                !(kwarg_pack<As> || ...) &&
                Bind<As...>::proper_argument_order &&
                Bind<As...>::no_qualified_arg_annotations &&
                Bind<As...>::no_duplicate_args &&
                Bind<As...>::no_conflicting_values &&
                Bind<As...>::no_extra_positional_args &&
                Bind<As...>::no_extra_keyword_args &&
                Bind<As...>::satisfies_required_args &&
                Bind<As...>::can_convert
            )
        constexpr Partial(As&&... args) : values(
            []<size_t... Ks>(std::index_sequence<Ks...>, auto&&... args) -> Tuple {
                return {{build<Ks>(std::forward<decltype(args)>(args)...)}...};
            }(std::index_sequence_for<As...>{}, std::forward<As>(args)...)
        ) {}

        /* Get the bound value at index K of the tuple.  If the partials are
        forwarded as an lvalue, then this will either directly reference the
        internal value if the corresponding argument expects an lvalue, or a copy
        if it expects an unqualified or rvalue type.  If the partials are given as
        an rvalue instead, then the copy will instead be optimized to a move. */
        template <size_t K> requires (K < n)
        [[nodiscard]] constexpr decltype(auto) get() const {
            return std::get<K>(values).get();
        }
        template <size_t K> requires (K < n)
        [[nodiscard]] constexpr decltype(auto) get() && {
            return std::move(std::get<K>(values)).get();
        }

        /* Get the bound value associated with the named argument, if it was given
        as a keyword argument.  If the partials are forwarded as an lvalue, then
        this will either directly reference the internal value if the corresponding
        argument expects an lvalue, or a copy if it expects an unqualified or rvalue
        type.  If the partials are given as an rvalue instead, then the copy will be
        optimized to a move. */
        template <static_str Name> requires (has<Name>)
        [[nodiscard]] constexpr decltype(auto) get() const {
            return std::get<idx<Name>>(values).get();
        }
        template <static_str Name> requires (has<Name>)
        [[nodiscard]] constexpr decltype(auto) get() && {
            return std::move(std::get<idx<Name>>(values)).get();
        }

        /* Bind the given C++ arguments to produce a partial tuple in chainable fashion.
        Any existing partial arguments will be carried over whenever this method is
        called. */
        template <typename... A>
            requires (
                !(arg_pack<A> || ...) &&
                !(kwarg_pack<A> || ...) &&
                signature::Bind<A...>::proper_argument_order &&
                signature::Bind<A...>::no_qualified_arg_annotations &&
                signature::Bind<A...>::no_duplicate_args &&
                signature::Bind<A...>::no_extra_positional_args &&
                signature::Bind<A...>::no_extra_keyword_args &&
                signature::Bind<A...>::no_conflicting_values &&
                signature::Bind<A...>::can_convert
            )
        [[nodiscard]] constexpr auto bind(A&&... args) const {
            return signature::Bind<A...>::bind(
                *this,
                std::forward<A>(args)...
            );
        }
        template <typename... A>
            requires (
                !(arg_pack<A> || ...) &&
                !(kwarg_pack<A> || ...) &&
                signature::Bind<A...>::proper_argument_order &&
                signature::Bind<A...>::no_qualified_arg_annotations &&
                signature::Bind<A...>::no_duplicate_args &&
                signature::Bind<A...>::no_extra_positional_args &&
                signature::Bind<A...>::no_extra_keyword_args &&
                signature::Bind<A...>::no_conflicting_values &&
                signature::Bind<A...>::can_convert
            )
        [[nodiscard]] constexpr auto bind(A&&... args) && {
            return signature::Bind<A...>::bind(
                std::move(*this),
                std::forward<A>(args)...
            );
        }
    };

    /* Instance-level constructor for a `::Partial` tuple. */
    template <typename... A>
        requires (
            !(arg_pack<A> || ...) &&
            !(kwarg_pack<A> || ...) &&
            Partial::template Bind<A...>::proper_argument_order &&
            Partial::template Bind<A...>::no_qualified_arg_annotations &&
            Partial::template Bind<A...>::no_duplicate_args &&
            Partial::template Bind<A...>::no_conflicting_values &&
            Partial::template Bind<A...>::no_extra_positional_args &&
            Partial::template Bind<A...>::no_extra_keyword_args &&
            Partial::template Bind<A...>::satisfies_required_args &&
            Partial::template Bind<A...>::can_convert
        )
    [[nodiscard]] static constexpr Partial partial(A&&... args) {
        return Partial(std::forward<A>(args)...);
    }

    /* A tuple holding a default value for every argument in the enclosing
    parameter list that is marked as optional.  One of these must be provided
    whenever a C++ function is invoked, and constructing one requires that the
    initializers match a sub-signature consisting only of the optional args as
    keyword-only parameters for clarity.  The result may be empty if there are no
    optional arguments in the enclosing signature, in which case the constructor
    will be optimized out. */
    struct Defaults : impl::signature_defaults_tag {
    protected:
        friend signature;

        template <typename...>
        static constexpr size_t _n_posonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_posonly<T, Ts...> =
            _n_posonly<Ts...> + (ArgTraits<T>::posonly() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_pos = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_pos<T, Ts...> =
            _n_pos<Ts...> + (ArgTraits<T>::pos() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_kw = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kw<T, Ts...> =
            _n_kw<Ts...> + (ArgTraits<T>::kw() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_kwonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kwonly<T, Ts...> =
            _n_kwonly<Ts...> + (ArgTraits<T>::kwonly() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _posonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _posonly_idx<T, Ts...> =
            ArgTraits<T>::posonly() && ArgTraits<T>::opt() ?
                0 : _posonly_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _pos_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _pos_idx<T, Ts...> =
            ArgTraits<T>::pos() && ArgTraits<T>::opt() ?
                0 : _pos_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kw_idx<T, Ts...> =
            ArgTraits<T>::kw() && ArgTraits<T>::opt() ?
                0 : _kw_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwonly_idx<T, Ts...> =
            ArgTraits<T>::kwonly() && ArgTraits<T>::opt() ?
                0 : _kwonly_idx<Ts...> + 1;

        template <size_t, typename>
        static constexpr size_t _find = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _find<I, std::tuple<T, Ts...>> =
            (I == T::index) ? 0 : 1 + _find<I, std::tuple<Ts...>>;

        /* Build a sub-signature holding only the arguments marked as optional from
        the enclosing signature. */
        template <typename out, typename...>
        struct extract { using type = out; };
        template <typename... out, typename A, typename... As>
        struct extract<signature<void(out...)>, A, As...> {
            template <typename>
            struct sub_signature { using type = signature<void(out...)>; };
            template <typename T> requires (ArgTraits<T>::opt())
            struct sub_signature<T> {
                template <typename D>
                struct to_default { using type = D; };
                template <typename D> requires (ArgTraits<D>::opt())
                struct to_default<D> {
                    using type = Arg<
                        ArgTraits<D>::name,
                        typename ArgTraits<D>::type
                    >::kw;
                };
                using type = signature<void(out..., typename to_default<T>::type)>;
            };
            using type = extract<typename sub_signature<A>::type, As...>::type;
        };
        using Inner = extract<signature<void()>, Args...>::type;

        /* Build a std::tuple of Elements to hold the default values themselves. */
        template <typename out, size_t, typename...>
        struct collect { using type = out; };
        template <typename... out, size_t I, typename A, typename... As>
        struct collect<std::tuple<out...>, I, A, As...> {
            template <typename>
            struct tuple { using type = std::tuple<out...>; };
            template <typename T> requires (ArgTraits<T>::opt())
            struct tuple<T> {
                using type = std::tuple<
                    out...,
                    Element<
                        I,
                        ArgTraits<T>::name,
                        typename ArgTraits<T>::type
                    >
                >;
            };
            using type = collect<typename tuple<A>::type, I + 1, As...>::type;
        };
        using Tuple = collect<std::tuple<>, 0, Args...>::type;

        Tuple values;

        template <size_t J, typename... As>
        static constexpr decltype(auto) build(As&&... args) {
            using T = std::tuple_element_t<J, Tuple>;
            constexpr size_t idx = signature<void(As...)>::template idx<T::name>;
            return unpack_arg<idx>(std::forward<As>(args)...);
        }

        template <typename D, size_t I>
        static constexpr bool _copy = I == D::n;
        template <typename D, size_t I> requires (I < std::tuple_size_v<Tuple>)
        static constexpr bool _copy<D, I> = [] {
            if constexpr (I < D::n) {
                using T = signature::at<std::tuple_element_t<I, Tuple>::index>;
                using U = D::template at<I>;
                return std::same_as<
                    typename ArgTraits<T>::unbind,
                    typename ArgTraits<U>::unbind
                > && _copy<D, I + 1>;
            } else {
                return false;
            }
        }();
        template <inherits<impl::signature_defaults_tag> D>
        static constexpr bool copy = _copy<std::remove_cvref_t<D>, 0>;

    public:
        using type = signature;

        static constexpr size_t n               = Inner::n;
        static constexpr size_t n_posonly       = _n_posonly<Args...>;
        static constexpr size_t n_pos           = _n_pos<Args...>;
        static constexpr size_t n_kw            = _n_kw<Args...>;
        static constexpr size_t n_kwonly        = _n_kwonly<Args...>;

        template <static_str Name>
        static constexpr bool has               = Inner::template has<Name>;
        static constexpr bool has_posonly       = n_posonly > 0;
        static constexpr bool has_pos           = n_pos > 0;
        static constexpr bool has_kw            = n_kw > 0;
        static constexpr bool has_kwonly        = n_kwonly > 0;

        template <static_str Name> requires (has<Name>)
        static constexpr size_t idx             = Inner::template idx<Name>;
        static constexpr size_t posonly_idx     = _posonly_idx<Args...>;
        static constexpr size_t pos_idx         = _pos_idx<Args...>;
        static constexpr size_t kw_idx          = _kw_idx<Args...>;
        static constexpr size_t kwonly_idx      = _kwonly_idx<Args...>;

        /* Given an index into the enclosing signature, find the corresponding index
        in the defaults tuple if that index is marked as optional. */
        template <size_t I> requires (ArgTraits<typename signature::at<I>>::opt())
        static constexpr size_t find = _find<I, Tuple>;

        /* Given an index into the defaults tuple, find the corresponding index in
        the enclosing parameter list. */
        template <size_t J> requires (J < n)
        static constexpr size_t rfind = std::tuple_element_t<J, Tuple>::index;

        template <size_t J> requires (J < n)
        using at = signature::at<rfind<J>>;

        /* Bind an argument list to the default values to enable the constructor. */
        template <typename... As>
        using Bind = Inner::template Bind<As...>;

        template <typename... As>
            requires (
                !(arg_pack<As> || ...) &&
                !(kwarg_pack<As> || ...) &&
                Bind<As...>::proper_argument_order &&
                Bind<As...>::no_qualified_arg_annotations &&
                Bind<As...>::no_duplicate_args &&
                Bind<As...>::no_conflicting_values &&
                Bind<As...>::no_extra_positional_args &&
                Bind<As...>::no_extra_keyword_args &&
                Bind<As...>::satisfies_required_args &&
                Bind<As...>::can_convert
            )
        constexpr Defaults(As&&... args) : values(
            []<size_t... Js>(std::index_sequence<Js...>, auto&&... args) -> Tuple {
                return {{build<Js>(std::forward<decltype(args)>(args)...)}...};
            }(std::index_sequence_for<As...>{}, std::forward<As>(args)...)
        ) {}

        template <inherits<impl::signature_defaults_tag> D> requires (copy<D>)
        constexpr Defaults(D&& other) :
            values([]<size_t... Js>(std::index_sequence<Js...>, auto&& other) -> Tuple {
                return {{std::forward<decltype(other)>(other).template get<Js>()}...};
            }(
                std::make_index_sequence<std::remove_cvref_t<D>::n>{},
                std::forward<decltype(other)>(other)
            ))
        {}

        /* Get the default value at index I of the tuple.  Use find<> to correlate
        an index from the enclosing signature if needed.  If the defaults container
        is used as an lvalue, then this will either directly reference the internal
        value if the corresponding argument expects an lvalue, or a copy if it
        expects an unqualified or rvalue type.  If the defaults container is given
        as an rvalue instead, then the copy will be optimized to a move. */
        template <size_t J> requires (J < n)
        constexpr decltype(auto) get() const {
            return std::get<J>(values).get();
        }
        template <size_t J> requires (J < n)
        constexpr decltype(auto) get() && {
            return std::move(std::get<J>(values)).get();
        }

        /* Get the default value associated with the named argument, if it is
        marked as optional.  If the defaults container is used as an lvalue, then
        this will either directly reference the internal value if the corresponding
        argument expects an lvalue, or a copy if it expects an unqualified or
        rvalue type.  If the defaults container is given as an rvalue instead, then
        the copy will be optimized to a move. */
        template <static_str Name> requires (has<Name>)
        constexpr decltype(auto) get() const {
            return std::get<idx<Name>>(values).get();
        }
        template <static_str Name> requires (has<Name>)
        constexpr decltype(auto) get() && {
            return std::move(std::get<idx<Name>>(values)).get();
        }
    };

    /* Instance-level constructor for a `::Defaults` tuple. */
    template <typename... A>
        requires (
            !(arg_pack<A> || ...) &&
            !(kwarg_pack<A> || ...) &&
            Defaults::template Bind<A...>::proper_argument_order &&
            Defaults::template Bind<A...>::no_qualified_arg_annotations &&
            Defaults::template Bind<A...>::no_duplicate_args &&
            Defaults::template Bind<A...>::no_conflicting_values &&
            Defaults::template Bind<A...>::no_extra_positional_args &&
            Defaults::template Bind<A...>::no_extra_keyword_args &&
            Defaults::template Bind<A...>::satisfies_required_args &&
            Defaults::template Bind<A...>::can_convert
        )
    [[nodiscard]] static constexpr Defaults defaults(A&&... args) {
        return Defaults(std::forward<A>(args)...);
    }

private:
    template <typename... Values>
    struct _Bind : impl::signature_bind_tag {
    protected:
        using src = signature<Return(Values...)>;

        template <size_t I, size_t K>
        static constexpr bool _in_partial = false;
        template <size_t I, size_t K> requires (K < Partial::n)
        static constexpr bool _in_partial<I, K> =
            I == Partial::template rfind<K> || _in_partial<I, K + 1>;
        template <size_t I>
        static constexpr bool in_partial = _in_partial<I, 0>;

        template <size_t, size_t>
        static constexpr bool _no_extra_positional_args = true;
        template <size_t I, size_t J>
            requires (J < std::min({
                src::args_idx,
                src::kw_idx,
                src::kwargs_idx
            }))
        static constexpr bool _no_extra_positional_args<I, J> = [] {
            return
                I < std::min(signature::kwonly_idx, signature::kwargs_idx) &&
                _no_extra_positional_args<
                    I + 1,
                    J + !in_partial<I>
                >;
        }();

        template <size_t>
        static constexpr bool _no_extra_keyword_args = true;
        template <size_t J> requires (J < src::kwargs_idx)
        static constexpr bool _no_extra_keyword_args<J> = [] {
            using T = src::template at<J>;
            return
                signature::has<ArgTraits<T>::name> &&
                _no_extra_keyword_args<J + 1>;
        }();

        template <size_t, size_t>
        static constexpr bool _no_conflicting_values = true;
        template <size_t I, size_t J> requires (I < signature::n && J < src::n)
        static constexpr bool _no_conflicting_values<I, J> = [] {
            using T = signature::at<I>;
            using U = src::template at<J>;

            constexpr bool kw_conflicts_with_partial =
                ArgTraits<U>::kw() &&
                Partial::template has<ArgTraits<U>::name>;

            constexpr bool kw_conflicts_with_positional =
                !in_partial<I> && !ArgTraits<T>::name.empty() && (
                    ArgTraits<T>::posonly() ||
                    J < std::min(src::kw_idx, src::kwargs_idx)
                ) && src::template has<ArgTraits<T>::name>;

            return
                !kw_conflicts_with_partial &&
                !kw_conflicts_with_positional &&
                _no_conflicting_values<
                    src::has_args && J == src::args_idx ? std::min({
                        signature::args_idx + 1,
                        signature::kwonly_idx,
                        signature::kwargs_idx
                    }) : I + 1,
                    signature::has_args && I == signature::args_idx ? std::min({
                        src::kw_idx,
                        src::kwargs_idx
                    }) : J + !in_partial<I>
                >;
        }();

        template <size_t I, size_t J>
        static constexpr bool _satisfies_required_args = true;
        template <size_t I, size_t J> requires (I < signature::n)
        static constexpr bool _satisfies_required_args<I, J> = [] {
            return (
                in_partial<I> ||
                ArgTraits<signature::at<I>>::opt() ||
                ArgTraits<signature::at<I>>::variadic() ||
                (
                    ArgTraits<signature::at<I>>::pos() &&
                        J < std::min(src::kw_idx, src::kwargs_idx)
                ) || (
                    ArgTraits<signature::at<I>>::kw() &&
                        src::template has<ArgTraits<signature::at<I>>::name>
                )
            ) && _satisfies_required_args<
                src::has_args && J == src::args_idx ?
                    std::min(signature::kwonly_idx, signature::kwargs_idx) :
                    I + 1,
                signature::has_args && I == signature::args_idx ?
                    std::min(src::kw_idx, src::kwargs_idx) :
                    J + !in_partial<I>
            >;
        }();

        template <size_t, size_t>
        static constexpr bool _can_convert = true;
        template <size_t I, size_t J> requires (I < signature::n && J < src::n)
        static constexpr bool _can_convert<I, J> = [] {
            if constexpr (ArgTraits<signature::at<I>>::args()) {
                constexpr size_t source_kw =
                    std::min(src::kw_idx, src::kwargs_idx);
                return
                    []<size_t... Js>(std::index_sequence<Js...>) {
                        return (std::convertible_to<
                            typename ArgTraits<typename src::template at<J + Js>>::type,
                            typename ArgTraits<signature::at<I>>::type
                        > && ...);
                    }(std::make_index_sequence<J < source_kw ? source_kw - J : 0>{}) &&
                    _can_convert<I + 1, source_kw>;

            } else if constexpr (ArgTraits<signature::at<I>>::kwargs()) {
                return
                    []<size_t... Js>(std::index_sequence<Js...>) {
                        return ((
                            signature::has<ArgTraits<
                                typename src::template at<src::kw_idx + Js>
                            >::name> || std::convertible_to<
                                typename ArgTraits<
                                    typename src::template at<src::kw_idx + Js>
                                >::type,
                                typename ArgTraits<signature::at<I>>::type
                            >
                        ) && ...);
                    }(std::make_index_sequence<src::n - src::kw_idx>{}) &&
                    _can_convert<I + 1, J>;

            } else if constexpr (in_partial<I>) {
                return _can_convert<I + 1, J>;

            } else if constexpr (ArgTraits<typename src::template at<J>>::posonly()) {
                return std::convertible_to<
                    typename ArgTraits<typename src::template at<J>>::type,
                    typename ArgTraits<signature::at<I>>::type
                > && _can_convert<I + 1, J + 1>;

            } else if constexpr (ArgTraits<typename src::template at<J>>::kw()) {
                constexpr static_str name = ArgTraits<typename src::template at<J>>::name;
                if constexpr (signature::has<name>) {
                    constexpr size_t idx = signature::idx<name>;
                    if constexpr (!std::convertible_to<
                        typename ArgTraits<typename src::template at<J>>::type,
                        typename ArgTraits<signature::at<idx>>::type
                    >) {
                        return false;
                    };
                }
                return _can_convert<I + 1, J + 1>;

            } else if constexpr (ArgTraits<typename src::template at<J>>::args()) {
                constexpr size_t target_kw =
                    std::min(signature::kwonly_idx, signature::kwargs_idx);
                return
                    []<size_t... Is>(std::index_sequence<Is...>) {
                        return (
                            (
                                in_partial<I + Is> || std::convertible_to<
                                    typename ArgTraits<
                                        typename src::template at<J>
                                    >::type,
                                    typename ArgTraits<signature::at<I + Is>>::type
                                >
                            ) && ...
                        );
                    }(std::make_index_sequence<I < target_kw ? target_kw - I : 0>{}) &&
                    _can_convert<target_kw, J + 1>;

            } else if constexpr (ArgTraits<typename src::template at<J>>::kwargs()) {
                constexpr size_t transition = std::min({
                    src::args_idx,
                    src::kwonly_idx,
                    src::kwargs_idx
                });
                constexpr size_t target_kw = src::has_args ?
                    signature::kwonly_idx :
                    []<size_t... Ks>(std::index_sequence<Ks...>) {
                        return std::max(
                            signature::kw_idx,
                            src::n_posonly + (0 + ... + (
                                std::tuple_element_t<
                                    Ks,
                                    typename Partial::Tuple
                                >::target_idx < transition
                            ))
                        );
                    }(std::make_index_sequence<Partial::n>{});
                return
                    []<size_t... Is>(std::index_sequence<Is...>) {
                        return ((
                            in_partial<target_kw + Is> || src::template has<
                                ArgTraits<signature::at<target_kw + Is>>::name
                            > || std::convertible_to<
                                typename ArgTraits<typename src::template at<J>>::type,
                                typename ArgTraits<signature::at<target_kw + Is>>::type
                            >
                        ) && ...);
                    }(std::make_index_sequence<signature::n - target_kw>{}) &&
                    _can_convert<I, J + 1>;

            } else {
                static_assert(false, "invalid argument kind");
                return false;
            }
        }();

        template <size_t I, size_t K>
        static constexpr bool use_partial = false;
        template <size_t I, size_t K> requires (K < Partial::n)
        static constexpr bool use_partial<I, K> = Partial::template rfind<K> == I;

        template <size_t I, size_t J, size_t K>
        struct merge {
        private:
            using T = signature::at<I>;
            static constexpr static_str name = ArgTraits<T>::name;

            template <size_t K2>
            static constexpr bool use_partial = false;
            template <size_t K2> requires (K2 < Partial::n)
            static constexpr bool use_partial<K2> = Partial::template rfind<K2> == I;

            template <size_t K2>
            static constexpr size_t consecutive = 0;
            template <size_t K2> requires (K2 < Partial::n)
            static constexpr size_t consecutive<K2> = 
                Partial::template rfind<K2> == I ? consecutive<K2 + 1> + 1 : 0;

            template <typename... A>
            static constexpr size_t pos_range = 0;
            template <typename A, typename... As>
            static constexpr size_t pos_range<A, As...> =
                ArgTraits<A>::pos() ? pos_range<As...> + 1 : 0;

            template <typename... A>
            static constexpr void assert_no_kwargs_conflict(A&&... args) {
                constexpr static_str name = ArgTraits<signature::at<I>>::name;
                if constexpr (!name.empty() && _kwargs_idx<A...> < sizeof...(A)) {
                    auto&& pack = unpack_arg<_kwargs_idx<A...>>(
                        std::forward<A>(args)...
                    );
                    auto node = pack.extract(name);
                    if (node) {
                        throw std::runtime_error(
                            "conflicting value for parameter '" + name +
                            "' at index " + static_str<>::from_int<I>
                        );
                    }
                }
            }

            template <size_t... Prev, size_t... Next, typename F>
            static constexpr std::invoke_result_t<F, Args...> forward_partial(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                auto&& parts,
                auto&& defaults,
                F&& func,
                auto&&... args
            ) {
                return merge<I + 1, J + 1, K + 1>{}(
                    std::forward<decltype(parts)>(parts),
                    std::forward<decltype(defaults)>(defaults),
                    std::forward<decltype(func)>(func),
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    to_arg<I>(std::forward<decltype(parts)>(
                        parts
                    ).template get<K>()),
                    unpack_arg<J + Next>(
                        std::forward<decltype(args)>(args)...
                    )...
                );
            }

            template <size_t... Prev, size_t... Next, typename F>
            static constexpr std::invoke_result_t<F, Args...> forward_default(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                auto&& parts,
                auto&& defaults,
                F&& func,
                auto&&... args
            ) {
                return merge<I + 1, J + 1, K>{}(
                    std::forward<decltype(parts)>(parts),
                    std::forward<decltype(defaults)>(defaults),
                    std::forward<decltype(func)>(func),
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    to_arg<I>(std::forward<decltype(defaults)>(
                        defaults
                    ).template get<Defaults::template find<I>>()),
                    unpack_arg<J + Next>(
                        std::forward<decltype(args)>(args)...
                    )...
                );
            }

            template <size_t... Prev, size_t... Next, typename F>
            static constexpr std::invoke_result_t<F, Args...> forward_positional(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                auto&& parts,
                auto&& defaults,
                F&& func,
                auto&&... args
            ) {
                return merge<I + 1, J + 1, K>{}(
                    std::forward<decltype(parts)>(parts),
                    std::forward<decltype(defaults)>(defaults),
                    std::forward<decltype(func)>(func),
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    to_arg<I>(unpack_arg<J>(
                        std::forward<decltype(args)>(args)...
                    )),
                    unpack_arg<J + 1 + Next>(
                        std::forward<decltype(args)>(args)...
                    )...
                );
            }

            template <size_t... Prev, size_t... Next, size_t... Last, typename F>
            static constexpr std::invoke_result_t<F, Args...> forward_keyword(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                std::index_sequence<Last...>,
                auto&& parts,
                auto&& defaults,
                F&& func,
                auto&&... args
            ) {
                constexpr size_t idx = _idx<name, decltype(args)...>;
                return merge<I + 1, J + 1, K>{}(
                    std::forward<decltype(parts)>(parts),
                    std::forward<decltype(defaults)>(defaults),
                    std::forward<decltype(func)>(func),
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    to_arg<I>(unpack_arg<idx>(
                        std::forward<decltype(args)>(args)...
                    )),
                    unpack_arg<J + Next>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    unpack_arg<idx + 1 + Last>(
                        std::forward<decltype(args)>(args)...
                    )...
                );
            }

            template <size_t... Prev, size_t... Next, typename F>
            static constexpr std::invoke_result_t<F, Args...> forward_from_pos_pack(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                auto&& parts,
                auto&& defaults,
                F&& func,
                auto&&... args
            ) {
                auto&& pack = unpack_arg<_args_idx<decltype(args)...>>(
                    std::forward<decltype(args)>(args)...
                );
                return merge<I + 1, J + 1, K>{}(
                    std::forward<decltype(parts)>(parts),
                    std::forward<decltype(defaults)>(defaults),
                    std::forward<decltype(func)>(func),
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    to_arg<I>(pack.value()),
                    unpack_arg<J + Next>(
                        std::forward<decltype(args)>(args)...
                    )...
                );
            }

            template <size_t... Prev, size_t... Next, typename F>
            static constexpr std::invoke_result_t<F, Args...> forward_from_kw_pack(
                auto&& node,
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                auto&& parts,
                auto&& defaults,
                F&& func,
                auto&&... args
            ) {
                return merge<I + 1, J + 1, K>{}(
                    std::forward<decltype(parts)>(parts),
                    std::forward<decltype(defaults)>(defaults),
                    std::forward<decltype(func)>(func),
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    to_arg<I>(std::forward<typename ArgTraits<T>::type>(
                        node.mapped()
                    )),
                    unpack_arg<J + Next>(
                        std::forward<decltype(args)>(args)...
                    )...
                );
            }

            template <size_t... Prev, size_t... Next, typename F>
            static constexpr std::invoke_result_t<F, Args...> drop_empty_pack(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                auto&& parts,
                auto&& defaults,
                F&& func,
                auto&&... args
            ) {
                return merge{}(
                    std::forward<decltype(parts)>(parts),
                    std::forward<decltype(defaults)>(defaults),
                    std::forward<decltype(func)>(func),
                    unpack_arg<Prev>(
                        std::forward<decltype(args)>(args)...
                    )...,
                    unpack_arg<J + 1 + Next>(
                        std::forward<decltype(args)>(args)...
                    )...
                );
            }

            template <typename P, typename... A>
            static auto variadic_positional(P&& parts, A&&... args) {
                static constexpr size_t cutoff = std::min(_kw_idx<A...>, _kwargs_idx<A...>);
                static constexpr size_t diff = J < cutoff ? cutoff - J : 0;

                // allocate variadic positional array
                using vec = std::vector<typename ArgTraits<T>::type>;
                vec out;
                if constexpr (diff) {
                    if constexpr (_args_idx<A...> < sizeof...(A)) {
                        out.reserve(
                            consecutive<K> + 
                            (diff - 1) +
                            unpack_arg<_args_idx<A...>>(
                                std::forward<A>(args)...
                            ).size()
                        );
                    } else {
                        out.reserve(consecutive<K> + diff);
                    }
                }

                // consume partial args
                []<size_t... Ks>(
                    std::index_sequence<Ks...>,
                    vec& out,
                    auto&& parts
                ) {
                    (out.emplace_back(std::forward<decltype(parts)>(
                        parts
                    ).template get<K + Ks>()), ...);
                }(
                    std::make_index_sequence<consecutive<K>>{},
                    out,
                    std::forward<P>(parts)
                );

                // consume source args + parameter packs
                []<size_t J2 = J>(this auto&& self, vec& out, auto&&... args) {
                    if constexpr (J2 < cutoff) {
                        if constexpr (
                            _args_idx<A...> < sizeof...(A) &&
                            J2 == _args_idx<A...>
                        ) {
                            auto&& pack = unpack_arg<J2>(
                                std::forward<decltype(args)>(args)...
                            );
                            out.insert(out.end(), pack.begin, pack.end);
                        } else {
                            out.emplace_back(unpack_arg<J2>(
                                std::forward<decltype(args)>(args)...
                            ));
                            std::forward<decltype(self)>(self).template operator()<J2 + 1>(
                                out,
                                std::forward<decltype(args)>(args)...
                            );
                        }
                    }
                }(out, std::forward<A>(args)...);

                return out;
            }

            template <typename P, typename... A>
            static auto variadic_keywords(P&& parts, A&&... args) {
                constexpr size_t diff = sizeof...(A) - J;

                // allocate variadic keyword map
                using map = std::unordered_map<
                    std::string,
                    typename ArgTraits<T>::type
                >;
                map out;
                if constexpr (_kwargs_idx<A...> < sizeof...(A)) {
                    out.reserve(
                        consecutive<K> +
                        (diff - 1) +
                        unpack_arg<_kwargs_idx<A...>>(
                            std::forward<A>(args)...
                        ).size()
                    );
                } else {
                    out.reserve(consecutive<K> + diff);
                }

                // consume partial kwargs
                []<size_t... Ks>(
                    std::index_sequence<Ks...>,
                    map& out,
                    auto&& parts
                ) {
                    (out.emplace(
                        std::string(Partial::template name<K + Ks>),
                        std::forward<decltype(parts)>(
                            parts
                        ).template get<K + Ks>()
                    ), ...);
                }(
                    std::make_index_sequence<consecutive<K>>{},
                    out,
                    std::forward<P>(parts)
                );

                // consume source kwargs + parameter packs
                []<size_t J2 = J>(this auto&& self, map& out, auto&&... args) {
                    if constexpr (J2 < sizeof...(A)) {
                        if constexpr (
                            _kwargs_idx<A...> < sizeof...(A) &&
                            J2 == _kwargs_idx<A...>
                        ) {
                            auto&& pack = unpack_arg<J2>(
                                std::forward<decltype(args)>(args)...
                            );
                            auto it = pack.begin();
                            auto end = pack.end();
                            while (it != end) {
                                // postfix++ required to increment before invalidation
                                auto node = pack.extract(it++);
                                auto rc = out.insert(node);
                                if (!rc.inserted) {
                                    throw std::runtime_error(
                                        "duplicate value for parameter '" +
                                        node.key() + "'"
                                    );
                                }
                            }
                        } else {
                            out.emplace(
                                std::string(ArgTraits<unpack_type<J2, A...>>::name),
                                *unpack_arg<J2>(
                                    std::forward<decltype(args)>(args)...
                                )
                            );
                            std::forward<decltype(self)>(self).template operator()<J2 + 1>(
                                out,
                                std::forward<decltype(args)>(args)...
                            );
                        }
                    }
                }(out, std::forward<A>(args)...);

                return out;
            }

        public:
            /* Produce a partial argument tuple for the enclosing signature using the
            built-up arguments from prior recursive calls.  Implements the `.bind()`
            method for partial functions, which is fully chainable, with existing
            partial arguments being folded in on prior recursive calls, and the return
            type being described above. */
            template <typename P, typename... A>
            static constexpr auto bind(P&& parts, A&&... args) {
                if constexpr (ArgTraits<T>::args()) {
                    static constexpr size_t cutoff = _kw_idx<A...>;
                    return []<size_t... Prev, size_t... Ks, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Ks...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&&... args
                    ) {
                        return merge<
                            I + 1,
                            cutoff + consecutive<K>,
                            K + consecutive<K>
                        >::bind(
                            std::forward<decltype(parts)>(parts),
                            unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            std::forward<decltype(parts)>(
                                parts
                            ).template get<K + Ks>()...,
                            unpack_arg<cutoff + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consecutive<K>>{},
                        std::make_index_sequence<sizeof...(A) - cutoff>{},
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return []<size_t... Prev, size_t... Ks, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Ks...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&&... args
                    ) {
                        return merge<
                            I + 1,
                            sizeof...(A) + consecutive<K>,
                            K + consecutive<K>
                        >::bind(
                            std::forward<decltype(parts)>(parts),
                            unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            arg<Partial::template name<K + Ks>> =
                                std::forward<decltype(parts)>(
                                    parts
                                ).template get<K + Ks>()...,
                            unpack_arg<J + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consecutive<K>>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );

                } else if constexpr (use_partial<K>) {
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&&... args
                    ) {
                        constexpr static_str name = Partial::template name<K>;
                        // demote keywords in the original partial into positional
                        // arguments in the new partial if the next source arg is
                        // positional and the target arg can be both positional or
                        // keyword
                        if constexpr (name.empty() || (
                            ArgTraits<T>::pos() &&
                            ArgTraits<T>::kw() &&
                            (_kw_idx<A...> == sizeof...(A) || J < _kw_idx<A...>)
                        )) {
                            return merge<I + 1, J + 1, K + 1>::bind(
                                std::forward<decltype(parts)>(parts),
                                unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                std::forward<decltype(parts)>(
                                    parts
                                ).template get<K>(),
                                unpack_arg<J + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...
                            );
                        } else {
                            return merge<I + 1, J + 1, K + 1>::bind(
                                std::forward<decltype(parts)>(parts),
                                unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                arg<name> = std::forward<decltype(parts)>(
                                    parts
                                ).template get<K>(),
                                unpack_arg<J + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...
                            );
                        }
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                } else {
                    return merge<I + 1, J + 1, K>::bind(
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                }
            }

            /* Invoking a C++ function involves a 3-way merge of the partial arguments,
            source arguments, and default values, in that order of precedence.  By the
            end, the parameters are guaranteed to exactly match the enclosing
            signature, such that it can be passed to a matching function with the
            intended semantics.  This is done by inserting, removing, and reordering
            parameters from the argument list at compile time using index sequences and
            fold expressions, which can be inlined into the final call. */
            template <typename P, typename D, typename F, typename... A>
            static constexpr std::invoke_result_t<F, Args...> operator()(
                P&& parts,
                D&& defaults,
                F&& func,
                A&&... args
            ) {
                if constexpr (ArgTraits<T>::posonly()) {
                    if constexpr (use_partial<K>) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        return forward_partial(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (J < pos_range<A...>) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        return forward_positional(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (J < sizeof...(A) && J == _args_idx<A...>) {
                        auto&& pack = unpack_arg<J>(std::forward<A>(args)...);
                        if (pack.has_value()) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            return forward_from_pos_pack(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<sizeof...(A) - J>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        } else {
                            return drop_empty_pack(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                    } else if constexpr (ArgTraits<T>::opt()) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        return forward_default(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (name.empty()) {
                        throw std::runtime_error(
                            "no match for positional-only parameter at index " +
                            static_str<>::from_int<I>
                        );
                    } else {
                        throw std::runtime_error(
                            "no match for positional-only parameter '" + name +
                            "' at index " + static_str<>::from_int<I>
                        );
                    }

                } else if constexpr (ArgTraits<T>::pos()) {
                    if constexpr (use_partial<K>) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        return forward_partial(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (J < pos_range<A...>) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        return forward_positional(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (J < sizeof...(A) && J == _args_idx<A...>) {
                        auto&& pack = unpack_arg<J>(std::forward<A>(args)...);
                        if (pack.has_value()) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            return forward_from_pos_pack(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<sizeof...(A) - J>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        } else {
                            return drop_empty_pack(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                    } else if constexpr (_idx<name, A...> < sizeof...(A)) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        constexpr size_t idx = _idx<name, A...>;
                        return forward_keyword(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<idx - J>{},
                            std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (_kwargs_idx<A...> < sizeof...(A)) {
                        auto&& pack = unpack_arg<_kwargs_idx<A...>>(
                            std::forward<A>(args)...
                        );
                        auto node = pack.extract(std::string_view(name));
                        if (node) {
                            return forward_from_kw_pack(
                                std::move(node),
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<sizeof...(A) - J>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        } else {
                            if constexpr (ArgTraits<T>::opt()) {
                                return forward_default(
                                    std::make_index_sequence<J>{},
                                    std::make_index_sequence<sizeof...(A) - J>{},
                                    std::forward<P>(parts),
                                    std::forward<D>(defaults),
                                    std::forward<F>(func),
                                    std::forward<A>(args)...
                                );
                            } else {
                                throw std::runtime_error(
                                    "no match for parameter '" + name +
                                    "' at index " + static_str<>::from_int<I>
                                );
                            }
                        }
                    } else if constexpr (ArgTraits<T>::opt()) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        return forward_default(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else {
                        throw std::runtime_error(
                            "no match for parameter '" + name +
                            "' at index " + static_str<>::from_int<I>
                        );
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    if constexpr (use_partial<K>) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        return forward_partial(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (_idx<name, A...> < sizeof...(A)) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        constexpr size_t idx = _idx<name, A...>;
                        return forward_keyword(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<idx - J>{},
                            std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (_kwargs_idx<A...> < sizeof...(A)) {
                        auto&& pack = unpack_arg<_kwargs_idx<A...>>(
                            std::forward<A>(args)...
                        );
                        auto node = pack.extract(std::string_view(name));
                        if (node) {
                            return forward_from_kw_pack(
                                std::move(node),
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<sizeof...(A) - J>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        } else {
                            if constexpr (ArgTraits<T>::opt()) {
                                return forward_default(
                                    std::make_index_sequence<J>{},
                                    std::make_index_sequence<sizeof...(A) - J>{},
                                    std::forward<P>(parts),
                                    std::forward<D>(defaults),
                                    std::forward<F>(func),
                                    std::forward<A>(args)...
                                );
                            } else {
                                throw std::runtime_error(
                                    "no match for parameter '" + name +
                                    "' at index " + static_str<>::from_int<I>
                                );
                            }
                        }
                    } else if constexpr (ArgTraits<T>::opt()) {
                        assert_no_kwargs_conflict(std::forward<A>(args)...);
                        return forward_default(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    } else {
                        throw std::runtime_error(
                            "no match for keyword-only parameter '" + name +
                            "' at index " + static_str<>::from_int<I>
                        );
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    static constexpr size_t cutoff = std::min(
                        _kw_idx<A...>,
                        _kwargs_idx<A...>
                    );
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        return merge<I + 1, J + 1, K + consecutive<K>>{}(
                            std::forward<decltype(parts)>(parts),
                            std::forward<decltype(defaults)>(defaults),
                            std::forward<decltype(func)>(func),
                            unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            to_arg<I>(variadic_positional(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(args)>(args)...
                            )),
                            unpack_arg<cutoff + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - cutoff>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return []<size_t... Prev>(
                        std::index_sequence<Prev...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        return merge<I + 1, J + 1, K + consecutive<K>>{}(
                            std::forward<decltype(parts)>(parts),
                            std::forward<decltype(defaults)>(defaults),
                            std::forward<decltype(func)>(func),
                            unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            to_arg<I>(variadic_keywords(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(args)>(args)...
                            ))
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );

                } else {
                    static_assert(false, "invalid argument kind");
                }
            }
        };
        template <size_t J, size_t K>
        struct merge<signature::n, J, K> {
        private:
            /* Convert a terminal argument list into an equivalent partial signature,
            wherein the arguments are bound to their corresponding values in the
            enclosing signature. */
            template <typename... As>
            struct sig {
                using src = signature<Return(As...)>;

                // elementwise traversal metafunction
                template <typename out, size_t, size_t>
                struct advance { using type = out; };
                template <typename... out, size_t I2, size_t J2>
                    requires (I2 < signature::n)
                struct advance<Return(out...), I2, J2> {
                    template <typename>
                    struct maybe_bind;

                    template <typename T> requires (ArgTraits<T>::posonly())
                    struct maybe_bind<T> {
                        // If no matching partial exists, forward the unbound arg
                        template <size_t J3>
                        struct append {
                            using type = advance<Return(out..., T), I2 + 1, J3>::type;
                        };
                        // Otherwise, bind the partial and advance
                        template <size_t J3> requires (J3 < src::kw_idx)
                        struct append<J3> {
                            using S = src::template at<J3>;
                            using B = ArgTraits<T>::template bind<S>;
                            using type = advance<Return(out..., B), I2 + 1, J3 + 1>::type;
                        };
                        using type = append<J2>::type;
                    };

                    template <typename T> requires (ArgTraits<T>::pos() && ArgTraits<T>::kw())
                    struct maybe_bind<T> {
                        // If no matching partial exists, forward the unbound arg
                        template <size_t J3>
                        struct append {
                            using type = advance<Return(out..., T), I2 + 1, J3>::type;
                        };
                        // If a partial positional arg exists, bind it and advance
                        template <size_t J3> requires (J3 < src::kw_idx)
                        struct append<J3> {
                            using S = src::template at<J3>;
                            using B = ArgTraits<T>::template bind<S>;
                            using type = advance<Return(out..., B), I2 + 1, J3 + 1>::type;
                        };
                        // If a partial keyword arg exists, bind it and advance
                        template <size_t J3> requires (
                            J3 >= src::kw_idx &&
                            src::template has<ArgTraits<T>::name>
                        )
                        struct append<J3> {
                            static constexpr static_str name = ArgTraits<T>::name;
                            static constexpr size_t idx = src::template idx<name>;
                            using S = src::template at<idx>;
                            using B = ArgTraits<T>::template bind<S>;
                            using type = advance<Return(out..., B), I2 + 1, J3>::type;
                        };
                        using type = append<J2>::type;
                    };

                    template <typename T> requires (ArgTraits<T>::kwonly())
                    struct maybe_bind<T> {
                        // If no matching partial exists, forward the unbound arg
                        template <size_t J3>
                        struct append {
                            using type = advance<Return(out..., T), I2 + 1, J3>::type;
                        };
                        // If a partial keyword arg exists, bind it and advance
                        template <size_t J3>
                            requires (src::template has<ArgTraits<T>::name>)
                        struct append<J3> {
                            static constexpr static_str name = ArgTraits<T>::name;
                            static constexpr size_t idx = src::template idx<name>;
                            using S = src::template at<idx>;
                            using B = ArgTraits<T>::template bind<S>;
                            using type = advance<Return(out..., B), I2 + 1, J3>::type;
                        };
                        using type = append<J2>::type;
                    };

                    template <typename T> requires (ArgTraits<T>::args())
                    struct maybe_bind<T> {
                        // Recur until there are no more partial positional args to bind
                        template <typename result, size_t J3>
                        struct append {
                            template <typename>
                            struct collect;
                            // If no matching partials exist, forward the unbound arg
                            template <>
                            struct collect<args<>> {
                                using type = advance<Return(out..., T), I2 + 1, J3>::type;
                            };
                            // Otherwise, bind the collected partials and advance
                            template <typename r2, typename... r2s>
                            struct collect<args<r2, r2s...>> {
                                using B = ArgTraits<T>::template bind<r2, r2s...>;
                                using type = advance<Return(out..., B), I2 + 1, J3>::type;
                            };
                            using type = collect<result>::type;
                        };
                        template <typename... result, size_t J3> requires (J3 < src::kw_idx)
                        struct append<args<result...>, J3> {
                            // Append remaining partial positional args to the output pack
                            using type = append<
                                args<result..., typename src::template at<J3>>,
                                J3 + 1
                            >::type;
                        };
                        using type = append<args<>, J2>::type;
                    };

                    template <typename T> requires (ArgTraits<T>::kwargs())
                    struct maybe_bind<T> {
                        // Recur until there are no more partial keyword args to bind
                        template <typename result, size_t J3>
                        struct append {
                            template <typename>
                            struct collect;
                            // If no matching partials exist, forward the unbound arg
                            template <>
                            struct collect<args<>> {
                                using type = advance<Return(out..., T), I2 + 1, J3>::type;
                            };
                            // Otherwise, bind the collected partials without advancing
                            template <typename r2, typename... r2s>
                            struct collect<args<r2, r2s...>> {
                                using B = ArgTraits<T>::template bind<r2, r2s...>;
                                using type = advance<Return(out..., B), I2 + 1, J3>::type;
                            };
                            using type = collect<result>::type;
                        };
                        template <typename... result, size_t J3> requires (J3 < src::n)
                        struct append<args<result...>, J3> {
                            // If the keyword arg is in the target signature, ignore
                            template <typename S>
                            struct collect {
                                using type = args<result...>;
                            };
                            // Otherwise, append it to the output pack and continue
                            template <typename S>
                                requires (!signature::template has<ArgTraits<S>::name>)
                            struct collect<S> {
                                using type = args<result..., S>;
                            };
                            using type = append<
                                typename collect<typename src::template at<J3>>::type,
                                J3 + 1
                            >::type;
                        };
                        // Start at the beginning of the partial keywords
                        using type = append<args<>, src::kw_idx>::type;
                    };

                    // Feed in the unbound argument and return a possibly bound equivalent
                    using type = maybe_bind<
                        typename ArgTraits<signature::at<I2>>::unbind
                    >::type;
                };

                // Start with an empty signature, which will be built up into an
                // equivalent of the enclosing signature through elementwise binding
                using type = signature<typename advance<Return(), 0, 0>::type>;
            };

        public:
            template <typename P, typename... As>
            static constexpr auto bind(P&& parts, As&&... args) {
                return typename sig<As...>::type::Partial{std::forward<As>(args)...};
            }

            template <typename P, typename D, typename F, typename... A>
            static constexpr std::invoke_result_t<F, Args...> operator()(
                P&& parts,
                D&& defaults,
                F&& func,
                A&&... args
            ) {
                // validate and remove positional parameter packs
                if constexpr (_args_idx<A...> < sizeof...(A)) {
                    constexpr size_t idx = _args_idx<A...>;
                    auto&& pack = unpack_arg<idx>(std::forward<A>(args)...);
                    pack.validate();
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        return merge{}(
                            std::forward<decltype(parts)>(parts),
                            std::forward<decltype(defaults)>(defaults),
                            std::forward<decltype(func)>(func),
                            unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            unpack_arg<idx + 1 + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );

                // validate and remove keyword parameter packs
                } else if constexpr (_kwargs_idx<A...> < sizeof...(A)) {
                    constexpr size_t idx = _kwargs_idx<A...>;
                    auto&& pack = unpack_arg<idx>(std::forward<A>(args)...);
                    pack.validate();
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        return merge{}(
                            std::forward<decltype(parts)>(parts),
                            std::forward<decltype(defaults)>(defaults),
                            std::forward<decltype(func)>(func),
                            unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            unpack_arg<idx + 1 + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );

                // call the function with the final argument list
                } else {
                    return std::forward<F>(func)(std::forward<A>(args)...);
                }
            }
        };

    public:
        static constexpr size_t n               = src::n;
        static constexpr size_t n_pos           = src::n_pos;
        static constexpr size_t n_kw            = src::n_kw;

        template <static_str Name>
        static constexpr bool has               = src::template has<Name>;
        static constexpr bool has_pos           = src::has_pos;
        static constexpr bool has_args          = src::has_args;
        static constexpr bool has_kw            = src::has_kw;
        static constexpr bool has_kwargs        = src::has_kwargs;

        template <static_str Name> requires (has<Name>)
        static constexpr size_t idx             = src::template idx<Name>;
        static constexpr size_t args_idx        = src::args_idx;
        static constexpr size_t kw_idx          = src::kw_idx;
        static constexpr size_t kwargs_idx      = src::kwargs_idx;

        template <size_t I> requires (I < n)
        using at = src::template at<I>;

        static constexpr bool proper_argument_order =
            src::proper_argument_order;

        static constexpr bool no_qualified_arg_annotations =
            src::no_qualified_arg_annotations;

        static constexpr bool no_duplicate_args =
            src::no_duplicate_args;

        static constexpr bool no_extra_positional_args =
            signature::has_args || !src::has_posonly ||
            _no_extra_positional_args<0, 0>;

        static constexpr bool no_extra_keyword_args =
            signature::has_kwargs || _no_extra_keyword_args<src::kw_idx>;

        static constexpr bool no_conflicting_values =
            _no_conflicting_values<0, 0>;

        static constexpr bool can_convert =
            _can_convert<0, 0>;

        static constexpr bool satisfies_required_args =
            _satisfies_required_args<0, 0>;

        /* Produce a new partial object with the given arguments in addition to any
        existing partial arguments.  This method is chainable, and the arguments will
        be interpreted as if they were passed to the signature's call operator.  They
        cannot include positional or keyword parameter packs. */
        template <inherits<Partial> P>
            requires (
                !src::has_args &&
                !src::has_kwargs &&
                proper_argument_order &&
                no_qualified_arg_annotations &&
                no_duplicate_args &&
                no_extra_positional_args &&
                no_extra_keyword_args &&
                no_conflicting_values &&
                can_convert
            )
        static constexpr auto bind(P&& parts, Values... args) {
            return merge<0, 0, 0>::bind(
                std::forward<P>(parts),
                std::forward<Values>(args)...
            );
        }

        /* Invoke a C++ function from C++ using Python-style arguments. */
        template <inherits<Partial> P, inherits<Defaults> D, typename F>
            requires (
                signature::invocable<F> &&
                proper_argument_order &&
                no_qualified_arg_annotations &&
                no_duplicate_args &&
                no_extra_positional_args &&
                no_extra_keyword_args &&
                no_conflicting_values &&
                can_convert &&
                satisfies_required_args
            )
        static constexpr Return operator()(
            P&& parts,
            D&& defaults,
            F&& func,
            Values... args
        ) {
            return invoke_with_packs(
                [](
                    auto&& parts,
                    auto&& defaults,
                    auto&& func,
                    auto&&... args
                ) {
                    return merge<0, 0, 0>{}(
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(defaults)>(defaults),
                        std::forward<decltype(func)>(func),
                        std::forward<decltype(args)>(args)...
                    );
                },
                std::forward<P>(parts),
                std::forward<D>(defaults),
                std::forward<F>(func),
                std::forward<Values>(args)...
            );
        }
    };

    template <typename out, typename...>
    struct _unbind { using type = out; };
    template <typename R, typename... out, typename A, typename... As>
    struct _unbind<signature<R(out...)>, A, As...> {
        using type = _unbind<
            signature<R(out..., typename ArgTraits<A>::unbind)>,
            As...
        >::type;
    };

public:
    /* Bind a C++ argument list to the enclosing signature, inserting default values
    and partial arguments where necessary.  This enables and implements the signature's
    pure C++ call operator as a 3-way, compile-time merge between the partial
    arguments, default values, and given source arguments, provided they fulfill the
    enclosing signature.  Additionally, bound arguments can be saved and encoded into a
    partial signature in a chainable fashion, using the same infrastructure to simulate
    a normal function call at every step.  Any existing partial arguments will be
    folded into the resulting signature, facilitating higher-order function composition
    (currying, etc.) that can be done entirely at compile time. */
    template <typename... Values>
    struct Bind : _Bind<Values...> {};
    template <typename... Values>
        requires (
            !(arg_pack<Values> || ...) &&
            !(kwarg_pack<Values> || ...) &&
            _Bind<Values...>::proper_argument_order &&
            _Bind<Values...>::no_qualified_arg_annotations &&
            _Bind<Values...>::no_duplicate_args &&
            _Bind<Values...>::no_extra_positional_args &&
            _Bind<Values...>::no_extra_keyword_args &&
            _Bind<Values...>::no_conflicting_values &&
            _Bind<Values...>::can_convert
        )
    struct Bind<Values...> : _Bind<Values...> {
        /* `Bind<Args...>::partial` produces a partial signature with the bound
        arguments filled in.  This is only available if the arguments are well-formed
        and partially satisfy the enclosing signature. */
        using partial =
            std::remove_cvref_t<decltype(_Bind<Values...>::template merge<0, 0, 0>::bind(
                std::declval<Partial>(),
                std::declval<Values>()...
            ))>;
    };

    /* Bind the given C++ arguments to produce a partial tuple in chainable fashion.
    Any existing partial arguments will be carried over whenever this method is
    called. */
    template <inherits<Partial> P, typename... A>
        requires (
            !(arg_pack<A> || ...) &&
            !(kwarg_pack<A> || ...) &&
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::can_convert
        )
    [[nodiscard]] static constexpr auto bind(P&& partial, A&&... args) {
        return Bind<A...>::bind(
            std::forward<P>(partial),
            std::forward<A>(args)...
        );
    }

    /* Unbinding a signature strips any partial arguments that have been encoded
    thus far and returns a new signature without them. */
    using Unbind = _unbind<signature<Return()>, Args...>::type;

    /* Unbind any partial arguments that have been accumulated thus far. */
    [[nodiscard]] static constexpr auto unbind() noexcept {
        return typename Unbind::Partial{};
    }

    /* Produce a string representation of this signature for debugging purposes.  The
    provided `prefix` will be prepended to each output line, and if `max_width` is
    provided, then the algorithm will attempt to wrap the output to that width, with
    each parameter indented on a separate line.  If a single parameter exceeds the
    maximum width, then it will be wrapped onto multiple lines with an additional level
    of indentation for the extra lines.  Note that the maximum width is not a hard
    limit; individual components can exceed it, but never on the same line as another
    component.

    The output from this method is directly written to a .pyi file when bindings are
    generated, allowing static type checkers to validate C++ function signatures and
    provide high-quality syntax highlighting/autocompletion. */
    [[nodiscard]] static std::string to_string(
        const std::string& name,
        const std::string& prefix = "",
        size_t max_width = std::numeric_limits<size_t>::max(),
        size_t indent = 4
    ) {
        std::vector<std::string> components;
        components.reserve(n * 3 + 2);
        components.emplace_back(name);

        /// TODO: appending the demangled type name is probably wrong, since it doesn't
        /// always yield valid Python source code.  Instead, I should probably try to
        /// convert the type to Python and return its qualified name?  That way, the
        /// .pyi file would be able to import the type correctly.  That will need some
        /// work, and demangling might be another option to the method that directs it
        /// to do this.  I'll probably have to revisit that when I actually try to
        /// build the .pyi files, and can test more directly.

        size_t last_posonly = std::numeric_limits<size_t>::max();
        size_t first_kwonly = std::numeric_limits<size_t>::max();
        []<size_t I = 0>(
            this auto&& self,
            auto&& defaults,
            std::vector<std::string>& components,
            size_t& last_posonly,
            size_t& first_kwonly
        ) {
            if constexpr (I < n) {
                using T = signature::at<I>;
                if constexpr (ArgTraits<T>::args()) {
                    components.emplace_back(std::string("*" + ArgTraits<T>::name));
                } else if constexpr (ArgTraits<T>::kwargs()) {
                    components.emplace_back(std::string("**" + ArgTraits<T>::name));
                } else {
                    if constexpr (ArgTraits<T>::posonly()) {
                        last_posonly = I;
                    } else if constexpr (ArgTraits<T>::kwonly() && !signature::has_args) {
                        if (first_kwonly == std::numeric_limits<size_t>::max()) {
                            first_kwonly = I;
                        }
                    }
                    components.emplace_back(std::string(ArgTraits<T>::name));
                }
                components.emplace_back(
                    std::string(type_name<typename ArgTraits<T>::type>)
                );
                if constexpr (ArgTraits<T>::opt()) {
                    components.emplace_back("...");
                } else {
                    components.emplace_back("");
                }
                std::forward<decltype(self)>(self).template operator()<I + 1>(
                    std::forward<decltype(defaults)>(defaults),
                    components
                );
            }
        }(components);

        if constexpr (std::is_void_v<Return>) {
            components.emplace_back("None");
        } else {
            components.emplace_back(std::string(type_name<Return>));
        }

        return impl::format_signature(
            prefix,
            max_width,
            indent,
            components,
            last_posonly,
            first_kwonly
        );
    }

    template <inherits<Defaults> D>
    [[nodiscard]] static std::string to_string(
        const std::string& name,
        D&& defaults,
        const std::string& prefix = "",
        size_t max_width = std::numeric_limits<size_t>::max(),
        size_t indent = 4
    ) {
        std::vector<std::string> components;
        components.reserve(n * 3 + 2);
        components.emplace_back(name);

        size_t last_posonly = std::numeric_limits<size_t>::max();
        size_t first_kwonly = std::numeric_limits<size_t>::max();
        []<size_t I = 0>(
            this auto&& self,
            auto&& defaults,
            std::vector<std::string>& components,
            size_t& last_posonly,
            size_t& first_kwonly
        ) {
            if constexpr (I < n) {
                using T = signature::at<I>;
                if constexpr (ArgTraits<T>::args()) {
                    components.emplace_back(std::string("*" + ArgTraits<T>::name));
                } else if constexpr (ArgTraits<T>::kwargs()) {
                    components.emplace_back(std::string("**" + ArgTraits<T>::name));
                } else {
                    if constexpr (ArgTraits<T>::posonly()) {
                        last_posonly = I;
                    } else if constexpr (ArgTraits<T>::kwonly() && !signature::has_args) {
                        if (first_kwonly == std::numeric_limits<size_t>::max()) {
                            first_kwonly = I;
                        }
                    }
                    components.emplace_back(std::string(ArgTraits<T>::name));
                }
                components.emplace_back(
                    std::string(type_name<typename ArgTraits<T>::type>)
                );
                if constexpr (ArgTraits<T>::opt()) {
                    components.emplace_back(repr(
                        defaults.template get<Defaults::template find<I>>()
                    ));
                } else {
                    components.emplace_back("");
                }
                std::forward<decltype(self)>(self).template operator()<I + 1>(
                    std::forward<decltype(defaults)>(defaults),
                    components
                );
            }
        }(components);

        if constexpr (std::is_void_v<Return>) {
            components.emplace_back("None");
        } else {
            components.emplace_back(std::string(type_name<Return>));
        }

        return impl::format_signature(
            prefix,
            max_width,
            indent,
            components,
            last_posonly,
            first_kwonly
        );
    }
};


/// NOTE: bertrand::signature<> contains all of the logic necessary to introspect and
/// invoke C++ functions with Python-style conventions.  By default, it is enabled for
/// all trivially-introspectable function types, meaning that the underlying function
/// does not accept template parameters or participate in an overload set.  However, it
/// is still possible to support these cases by specializing `bertrand::signature<>`
/// for the desired function types, and then redirecting to a canonical signature via
/// inheritance.  Doing so will allow a non-trivial function to be used as the
/// initializer for a `bertrand::def` statement.
template <typename R, typename... A>
struct signature<R(A...) noexcept>                          : signature<R(A...)> {};
template <typename R, typename... A>
struct signature<R(*)(A...)>                                : signature<R(A...)> {};
template <typename R, typename... A>
struct signature<R(*)(A...) noexcept>                       : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...)>                             : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) &>                           : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) noexcept>                    : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) & noexcept>                  : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) const>                       : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) const &>                     : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) const noexcept>              : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) const & noexcept>            : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) volatile>                    : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) volatile &>                  : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) volatile noexcept>           : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) volatile & noexcept>         : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) const volatile>              : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) const volatile &>            : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) const volatile noexcept>     : signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct signature<R(C::*)(A...) const volatile & noexcept>   : signature<R(A...)> {};
template <has_call_operator T>
struct signature<T> : signature<decltype(&std::remove_reference_t<T>::operator())> {};
template <inherits<impl::def_tag> T>
struct signature<T> : std::remove_reference_t<T>::Signature {};
template <inherits<impl::chain_tag> T>
    requires (signature<typename std::remove_reference_t<T>::template at<0>>::enable)
struct signature<T> : signature<
    typename std::remove_reference_t<T>::template at<0>
>::template with_return<
    typename impl::chain_return_type<
        typename signature<typename std::remove_reference_t<T>::template at<0>>::Return,
        std::remove_cvref_t<T>,
        1
    >::type
> {};


/* A template constraint that controls whether the `bertrand::call()` operator is
enabled for a given C++ function and argument list. */
template <typename F, typename... Args>
concept callable =
    signature<F>::enable &&
    signature<F>::args_fit_within_bitset &&
    signature<F>::proper_argument_order &&
    signature<F>::no_qualified_arg_annotations &&
    signature<F>::no_duplicate_args &&
    signature<F>::template Bind<Args...>::proper_argument_order &&
    signature<F>::template Bind<Args...>::no_qualified_arg_annotations &&
    signature<F>::template Bind<Args...>::no_duplicate_args &&
    signature<F>::template Bind<Args...>::no_conflicting_values &&
    signature<F>::template Bind<Args...>::no_extra_positional_args &&
    signature<F>::template Bind<Args...>::no_extra_keyword_args &&
    signature<F>::template Bind<Args...>::satisfies_required_args &&
    signature<F>::template Bind<Args...>::can_convert;


/* A template constraint that controls whether the `bertrand::def()` operator is
enabled for a given C++ function and argument list. */
template <typename F, typename... Args>
concept partially_callable =
    signature<F>::enable &&
    !(arg_pack<Args> || ...) &&
    !(kwarg_pack<Args> || ...) &&
    signature<F>::proper_argument_order &&
    signature<F>::no_qualified_arg_annotations &&
    signature<F>::no_duplicate_args &&
    signature<F>::template Bind<Args...>::proper_argument_order &&
    signature<F>::template Bind<Args...>::no_qualified_arg_annotations &&
    signature<F>::template Bind<Args...>::no_duplicate_args &&
    signature<F>::template Bind<Args...>::no_conflicting_values &&
    signature<F>::template Bind<Args...>::no_extra_positional_args &&
    signature<F>::template Bind<Args...>::no_extra_keyword_args &&
    signature<F>::template Bind<Args...>::can_convert;


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename F, typename... Args>
    requires (
        callable<F, Args...> &&
        signature<F>::Partial::n == 0 &&
        signature<F>::Defaults::n == 0
    )
constexpr decltype(auto) call(F&& func, Args&&... args) {
    return typename signature<F>::template Bind<Args...>{}(
        typename signature<F>::Partial{},
        typename signature<F>::Defaults{},
        std::forward<F>(func),
        std::forward<Args>(args)...
    );
}


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename F, typename... Args>
    requires (
        callable<F, Args...> &&
        signature<F>::Partial::n == 0
    )
constexpr decltype(auto) call(
    const typename signature<F>::Defaults& defaults,
    F&& func,
    Args&&... args
) {
    return typename signature<F>::template Bind<Args...>{}(
        typename signature<F>::Partial{},
        defaults,
        std::forward<F>(func),
        std::forward<Args>(args)...
    );
}


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename F, typename... Args>
    requires (
        callable<F, Args...> &&
        signature<F>::Partial::n == 0
    )
constexpr decltype(auto) call(
    typename signature<F>::Defaults&& defaults,
    F&& func,
    Args&&... args
) {
    return typename signature<F>::template Bind<Args...>{}(
        typename signature<F>::Partial{},
        std::move(defaults),
        std::forward<F>(func),
        std::forward<Args>(args)...
    );
}


/* Construct a partial function object that captures a C++ function and a subset of its
arguments, which can be used to invoke the function later with the remaining arguments.
Arguments and default values are given in the same style as `call()`, and will be
stored internally within the partial object, forcing a copy in the case of lvalue
inputs.  When the partial is called, an additional copy may be made if the function
expects a temporary or rvalue reference, so as not to modify the stored arguments.  If
the partial is called as an rvalue (by moving it, for example), then the second copy
can be avoided, and the stored arguments will be moved directly into the function call.

Note that the function signature cannot contain any template parameters (including auto
arguments), as the function signature must be known unambiguously at compile time to
implement the required matching.

The returned partial is a thin proxy that only implements the call operator and a
handful of introspection methods.  It also allows transparent access to the decorated
function via the `*` and `->` operators. */
template <typename Func, typename... Args>
    requires (
        signature<Func>::Partial::n == 0 &&
        partially_callable<Func, Args...>
    )
struct def : impl::def_tag {
private:
    template <typename, typename, size_t>
    struct bind_type;
    template <typename... out, typename sig, size_t I>
    struct bind_type<args<out...>, sig, I> { using type = def<Func, out...>; };
    template <typename... out, typename sig, size_t I> requires (I < sig::n)
    struct bind_type<args<out...>, sig, I> {
        template <typename T>
        struct filter { using type = bind_type<args<out...>, sig, I + 1>::type; };
        template <typename T> requires (ArgTraits<T>::bound())
        struct filter<T> {
            template <typename>
            struct extend;
            template <typename... bound>
            struct extend<args<bound...>> {
                using type = bind_type<args<out..., bound...>, sig, I + 1>::type;
            };
            using type = extend<typename ArgTraits<T>::bound_to>::type;
        };
        using type = filter<typename sig::template at<I>>::type;
    };

public:
    using Function = std::remove_cvref_t<Func>;
    using Partial = signature<Function>::template Bind<Args...>::partial;
    using Signature = Partial::type;
    using Defaults = Signature::Defaults;

    Defaults defaults;
    Function func;
    Partial partial;

    /* Allows access to the template constraints and underlying implementation for the
    call operator. */
    template <typename... A>
    using Bind = Signature::template Bind<A...>;

    template <is<Func> F> requires (signature<F>::Defaults::n == 0)
    constexpr def(F&& func, Args... args) :
        defaults(),
        func(std::forward<F>(func)),
        partial(std::forward<Args>(args)...)
    {}

    template <is<Func> F>
    constexpr def(const Defaults& defaults, F&& func, Args... args) :
        defaults(defaults),
        func(std::forward<F>(func)),
        partial(std::forward<Args>(args)...)
    {}

    template <is<Func> F>
    constexpr def(Defaults&& defaults, F&& func, Args... args) :
        defaults(std::move(defaults)),
        func(std::forward<F>(func)),
        partial(std::forward<Args>(args)...)
    {}

    template <std::convertible_to<Defaults> D, is<Func> F, std::convertible_to<Partial> P>
    constexpr def(D&& defaults, F&& func, P&& partial) :
        defaults(std::forward<D>(defaults)),
        func(std::forward<F>(func)),
        partial(std::forward<P>(partial))
    {}

    /* Dereference a `def` object to access the underlying function. */
    [[nodiscard]] constexpr Function&& operator*() && noexcept { return std::move(func); }
    [[nodiscard]] constexpr Function& operator*() & noexcept { return func; }
    [[nodiscard]] constexpr Function* operator->() noexcept { return &func; }
    [[nodiscard]] constexpr const Function& operator*() const noexcept { return func; }
    [[nodiscard]] constexpr const Function* operator->() const noexcept { return &func; }

    /* Get the partial value at index I if it is within range. */
    template <size_t I> requires (I < Partial::n)
    [[nodiscard]] constexpr decltype(auto) get() const {
        return partial.template get<I>();
    }
    template <size_t I> requires (I < Partial::n)
    [[nodiscard]] constexpr decltype(auto) get() && {
        return std::move(partial).template get<I>();
    }

    /* Get the partial value of the named argument if it is present. */
    template <static_str name> requires (Partial::template has<name>)
    [[nodiscard]] constexpr decltype(auto) get() const {
        return partial.template get<name>();
    }
    template <static_str name> requires (Partial::template has<name>)
    [[nodiscard]] constexpr decltype(auto) get() && {
        return std::move(partial).template get<name>();
    }

    /* Invoke the function, applying the semantics of the inferred signature. */
    template <typename... A>
        requires (
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::satisfies_required_args &&
            Bind<A...>::can_convert
        )
    constexpr decltype(auto) operator()(A&&... args) const {
        return Bind<A...>{}(
            partial,
            defaults,
            func,
            std::forward<A>(args)...
        );
    }
    template <typename... A>
        requires (
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::satisfies_required_args &&
            Bind<A...>::can_convert
        )
    constexpr decltype(auto) operator()(A&&... args) && {
        return Bind<A...>{}(
            std::move(partial),
            std::move(defaults),
            std::move(func),
            std::forward<A>(args)...
        );
    }

    /* Generate a partial function with the given arguments filled in.  The method can
    be chained - any existing partial arguments will be carried over to the result, and
    will not be considered when binding the new arguments. */
    template <typename... A>
        requires (
            !(arg_pack<A> || ...) &&
            !(kwarg_pack<A> || ...) &&
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::can_convert
        )
    [[nodiscard]] constexpr auto bind(A&&... args) const {
        using sig = std::remove_cvref_t<
            decltype(partial.bind(std::forward<A>(args)...))
        >::type;
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            auto&& defaults,
            auto&& func,
            auto&& partial
        ) {
            return typename bind_type<bertrand::args<>, sig, 0>::type(
                std::forward<decltype(defaults)>(defaults),
                std::forward<decltype(func)>(func),
                std::forward<decltype(partial)>(partial)
            );
        }(
            std::make_index_sequence<sig::Partial::n>{},
            defaults,
            func,
            partial.bind(std::forward<A>(args)...)
        );
    }
    template <typename... A>
        requires (
            !(arg_pack<A> || ...) &&
            !(kwarg_pack<A> || ...) &&
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::can_convert
        )
    [[nodiscard]] constexpr auto bind(A&&... args) && {
        using sig = std::remove_cvref_t<
            decltype(partial.bind(std::forward<A>(args)...))
        >::type;
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            auto&& defaults,
            auto&& func,
            auto&& partial
        ) {
            return typename bind_type<bertrand::args<>, sig, 0>::type(
                std::forward<decltype(defaults)>(defaults),
                std::forward<decltype(func)>(func),
                std::forward<decltype(partial)>(partial)
            );
        }(
            std::make_index_sequence<sig::Partial::n>{},
            std::move(defaults),
            std::move(func),
            std::move(partial).bind(std::forward<A>(args)...)
        );
    }

    /* Clear any partial arguments that have been accumulated thus far, returning a new
    function object without them. */
    [[nodiscard]] constexpr def<Func> unbind() const {
        return def<Func>(defaults, func);
    }
    [[nodiscard]] constexpr def<Func> unbind() && {
        return def<Func>(std::move(defaults), std::move(func));
    }
};


template <typename F, typename... A>
    requires (
        signature<F>::Defaults::n == 0 &&
        signature<F>::Partial::n == 0 &&
        partially_callable<F, A...>
    )
def(F, A&&...) -> def<F, A...>;
template <typename F, typename... A>
    requires (
        signature<F>::Partial::n == 0 &&
        partially_callable<F, A...>
    )
def(typename signature<F>::Defaults&&, F, A&&...) -> def<F, A...>;
template <typename F, typename... A>
    requires (
        signature<F>::Partial::n == 0 &&
        partially_callable<F, A...>
    )
def(const typename signature<F>::Defaults&, F, A&&...) -> def<F, A...>;


template <typename L, typename R>
    requires (
        (inherits<L, impl::def_tag> || inherits<R, impl::def_tag>) &&
        (!inherits<L, impl::chain_tag> && !inherits<R, impl::chain_tag>) &&
        signature<L>::enable &&
        std::is_invocable_v<R, typename signature<L>::Return>
    )
[[nodiscard]] constexpr auto operator>>(L&& lhs, R&& rhs) {
    return chain(std::forward<L>(lhs)) >> std::forward<R>(rhs);
}


}


/* Specializing `std::tuple_size`, `std::tuple_element`, and `std::get` allows
argument packs, function chains, `def` objects, and signature tuples to be decomposed
using structured bindings. */
namespace std {

    template <bertrand::is_args T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    template <bertrand::is_chain T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    template <bertrand::inherits<bertrand::impl::def_tag> T>
    struct tuple_size<T> :
        std::integral_constant<size_t, bertrand::signature<T>::n>
    {};

    template <bertrand::inherits<bertrand::impl::signature_partial_tag> T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::n>
    {};

    template <bertrand::inherits<bertrand::impl::signature_defaults_tag> T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::n>
    {};

    template <size_t I, bertrand::is_args T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = std::remove_cvref_t<T>::template at<I>;
    };

    template <size_t I, bertrand::is_chain T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::inherits<bertrand::impl::def_tag> T>
        requires (I < bertrand::signature<T>::n)
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::inherits<bertrand::impl::signature_partial_tag> T>
        requires (I < std::remove_cvref_t<T>::n)
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::inherits<bertrand::impl::signature_defaults_tag> T>
        requires (I < std::remove_cvref_t<T>::n)
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::is_args T>
        requires (
            !std::is_lvalue_reference_v<T> &&
            I < std::remove_cvref_t<T>::size()
        )
    [[nodiscard]] constexpr decltype(auto) get(T&& args) noexcept {
        return std::forward<T>(args).template get<I>();
    }

    template <size_t I, bertrand::is_chain T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr decltype(auto) get(T&& chain) {
        return std::forward<T>(chain).template get<I>();
    }

    template <size_t I, bertrand::inherits<bertrand::impl::def_tag> T>
        requires (I < bertrand::signature<T>::n)
    [[nodiscard]] constexpr decltype(auto) get(T&& def) noexcept {
        return std::forward<T>(def).template get<I>();
    }

    template <size_t I, bertrand::inherits<bertrand::impl::signature_partial_tag> T>
        requires (I < std::remove_cvref_t<T>::n)
    [[nodiscard]] constexpr decltype(auto) get(T&& partial) noexcept {
        return std::forward<T>(partial).template get<I>();
    }

    template <size_t I, bertrand::inherits<bertrand::impl::signature_defaults_tag> T>
        requires (I < std::remove_cvref_t<T>::n)
    [[nodiscard]] constexpr decltype(auto) get(T&& defaults) noexcept {
        return std::forward<T>(defaults).template get<I>();
    }

    template <bertrand::static_str name, bertrand::inherits<bertrand::impl::def_tag> T>
        requires (bertrand::signature<T>::template has<name>)
    [[nodiscard]] constexpr decltype(auto) get(T&& def) noexcept {
        return std::forward<T>(def).template get<name>();
    }

    template <bertrand::static_str name, bertrand::inherits<bertrand::impl::signature_partial_tag> T>
        requires (std::remove_cvref_t<T>::template has<name>)
    [[nodiscard]] constexpr decltype(auto) get(T&& signature) noexcept {
        return std::forward<T>(signature).template get<name>();
    }

    template <bertrand::static_str name, bertrand::inherits<bertrand::impl::signature_defaults_tag> T>
        requires (std::remove_cvref_t<T>::template has<name>)
    [[nodiscard]] constexpr decltype(auto) get(T&& signature) noexcept {
        return std::forward<T>(signature).template get<name>();
    }

}


namespace bertrand {


inline void test() {
    constexpr auto a = arg<"a">;
    constexpr auto b = arg<"b">;
    constexpr auto n = arg<"n">;
    constexpr auto d = arg<"d">;

    constexpr def sub(
        { b = 2 },
        [](Arg<"a", int> x, Arg<"b", int>::opt y) {
            return *x - *y;
        }
    );
    constexpr def div(
        { d = 2 },
        [](Arg<"n", int> x, Arg<"d", int>::opt y) {
            return *x / *y;
        }
    );

    constexpr def do_double(
        {arg<"x"> = 2},
        [](Arg<"x", int>::opt x) {
            return *x * 2;
        }
    );


    static_assert(do_double() == 4);

    constexpr auto partial = sub >> div.bind(d = 2) >> [](auto&& x) {
        return x + 1;
    };
    static_assert(partial(a = 10) == 5);  // (10 - 2) / 2 + 1 == 5
}


}


#endif  // BERTRAND_FUNC_H
