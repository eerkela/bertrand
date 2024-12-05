#ifndef BERTRAND_PYTHON_CORE_ARG_H
#define BERTRAND_PYTHON_CORE_ARG_H

#include "declarations.h"
#include "object.h"
#include "except.h"


namespace py {

namespace impl {

    struct ArgKind {
        enum Flags : uint8_t {
            POS                 = 0b1,
            KW                  = 0b10,
            OPT                 = 0b100,
            VARIADIC            = 0b1000,
        } flags;

        constexpr ArgKind(uint8_t flags = 0) noexcept :
            flags(static_cast<Flags>(flags))
        {}

        constexpr operator uint8_t() const noexcept {
            return flags;
        }

        constexpr bool posonly() const noexcept {
            return (flags & ~OPT) == POS;
        }

        constexpr bool pos() const noexcept {
            return (flags & (POS | VARIADIC)) == POS;
        }

        constexpr bool args() const noexcept {
            return flags == (POS | VARIADIC);
        }

        constexpr bool kwonly() const noexcept {
            return (flags & ~OPT) == KW;
        }

        constexpr bool kw() const noexcept {
            return (flags & (KW | VARIADIC)) == KW;
        }

        constexpr bool kwargs() const noexcept {
            return flags == (KW | VARIADIC);
        }

        constexpr bool opt() const noexcept {
            return flags & OPT;
        }

        constexpr bool variadic() const noexcept {
            return flags & VARIADIC;
        }
    };

    constexpr bool isalpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    constexpr bool isalnum(char c) {
        return isalpha(c) || (c >= '0' && c <= '9');
    }

    template <size_t, StaticStr>
    constexpr bool validate_arg_name = true;
    template <size_t I, StaticStr Name> requires (I < Name.size())
    constexpr bool validate_arg_name<I, Name> =
        (isalnum(Name[I]) || Name[I] == '_') && validate_arg_name<I + 1, Name>;

    template <StaticStr Name>
    constexpr bool _arg_name = !Name.empty() && (
        (isalpha(Name[0]) || Name[0] == '_') &&
        validate_arg_name<1, Name>
    );

    template <StaticStr Name>
    constexpr bool _variadic_args_name =
        Name.size() > 1 &&
        Name[0] == '*' &&
        (isalpha(Name[1]) || Name[1] == '_') &&
        validate_arg_name<2, Name>;

    template <StaticStr Name>
    constexpr bool _variadic_kwargs_name =
        Name.size() > 2 &&
        Name[0] == '*' &&
        Name[1] == '*' &&
        (isalpha(Name[2]) || Name[2] == '_') &&
        validate_arg_name<3, Name>;

    template <StaticStr Name>
    concept arg_name =
        _arg_name<Name> || _variadic_args_name<Name> || _variadic_kwargs_name<Name>;

    template <StaticStr Name>
    concept variadic_args_name =
        arg_name<Name> &&
        !_arg_name<Name> &&
        _variadic_args_name<Name> &&
        !_variadic_kwargs_name<Name>;

    template <StaticStr Name>
    concept variadic_kwargs_name =
        arg_name<Name> &&
        !_arg_name<Name> &&
        !_variadic_args_name<Name> &&
        _variadic_kwargs_name<Name>;

    template <typename T>
    struct ArgTraits;

    template <typename Arg, typename... Ts>
    struct BoundArg;

    template <typename Arg, typename... Ts>
    struct can_bind {
    private:
        template <typename... Us>
        static constexpr bool _names_are_unique = true;
        template <typename U, typename... Us>
        static constexpr bool _names_are_unique<U, Us...> =
            ((ArgTraits<U>::name != ArgTraits<Us>::name) && ...) &&
            _names_are_unique<Us...>;

    public:
        static constexpr bool can_convert = (
            std::is_convertible_v<
                Ts,
                typename ArgTraits<Arg>::type
            > && ...
        );
        static constexpr bool values_are_not_optional = (
            !ArgTraits<Ts>::opt() && ...
        );
        static constexpr bool values_are_not_variadic = (
            !ArgTraits<Ts>::variadic() && ...
        );
        static constexpr bool values_are_positional = (
            ArgTraits<Ts>::posonly() && ...
        );
        static constexpr bool values_are_keyword = (
            ArgTraits<Ts>::kw() && ...
        );
        static constexpr bool names_are_unique = _names_are_unique<Ts...>;
        static constexpr bool names_match_arg = (
            (ArgTraits<Ts>::name == ArgTraits<Arg>::name) && ...
        );
    };

    template <typename Arg, typename... Vs>
    concept can_bind_arg =
        can_bind<Arg, Vs...>::can_convert &&
        can_bind<Arg, Vs...>::values_are_not_optional &&
        can_bind<Arg, Vs...>::values_are_not_variadic && (
            (ArgTraits<Arg>::posonly() && (
                sizeof...(Vs) == 1 &&
                can_bind<Arg, Vs...>::values_are_positional
            )) ||
            (ArgTraits<Arg>::pos() && (
                sizeof...(Vs) == 1 && (
                    can_bind<Arg, Vs...>::values_are_positional ||
                    can_bind<Arg, Vs...>::names_match_arg
                )
            )) ||
            (ArgTraits<Arg>::kwonly() && (
                sizeof...(Vs) == 1 && (
                    can_bind<Arg, Vs...>::values_are_keyword &&
                    can_bind<Arg, Vs...>::names_match_arg
                )
            )) ||
            (ArgTraits<Arg>::args() && can_bind<Arg, Vs...>::values_are_positional) ||
            (ArgTraits<Arg>::kwargs() && (
                can_bind<Arg, Vs...>::values_are_keyword &&
                can_bind<Arg, Vs...>::names_are_unique
            ))
        );

    template <typename Arg>
    struct OptionalArg {
        static constexpr StaticStr name = Arg::name;
        static constexpr ArgKind kind = Arg::kind | ArgKind::OPT;
        using type = Arg::type;
        template <typename V> requires (can_bind_arg<OptionalArg, V>)
        using bind = ArgTraits<OptionalArg>::template bind<V>;
        using unbind = OptionalArg;

        type value;

        [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }

        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <StaticStr Name, typename T>
    struct PositionalArg {
        static constexpr StaticStr name = Name;
        static constexpr ArgKind kind = ArgKind::POS;
        using type = T;
        using opt = OptionalArg<PositionalArg>;
        template <typename V> requires (can_bind_arg<PositionalArg, V>)
        using bind = ArgTraits<PositionalArg>::template bind<V>;
        using unbind = PositionalArg;

        type value;

        [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }

        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <StaticStr Name, typename T>
    struct KeywordArg {
        static constexpr StaticStr name = Name;
        static constexpr ArgKind kind = ArgKind::KW;
        using type = T;
        using opt = OptionalArg<KeywordArg>;
        template <typename V> requires (can_bind_arg<KeywordArg, V>)
        using bind = ArgTraits<KeywordArg>::template bind<V>;
        using unbind = KeywordArg;

        type value;

        [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }

        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    /* A keyword parameter pack obtained by double-dereferencing a Python object. */
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
        using type = mapped_type;
        static constexpr StaticStr name = "";
        static constexpr ArgKind kind = ArgKind::KW | ArgKind::VARIADIC;

        T value;

    private:

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

    /* A positional parameter pack obtained by dereferencing a Python object. */
    template <iterable T> requires (has_size<T>)
    struct ArgPack {
        using type = iter_type<T>;
        static constexpr StaticStr name = "";
        static constexpr ArgKind kind = ArgKind::POS | ArgKind::VARIADIC;

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

    template <typename T>
    constexpr bool _arg_pack = false;
    template <typename T>
    constexpr bool _arg_pack<ArgPack<T>> = true;
    template <typename T>
    concept arg_pack = _arg_pack<std::remove_cvref_t<T>>;

    template <typename T>
    constexpr bool _kwarg_pack = false;
    template <typename T>
    constexpr bool _kwarg_pack<KwargPack<T>> = true;
    template <typename T>
    concept kwarg_pack = _kwarg_pack<std::remove_cvref_t<T>>;

}


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a `py::Function`.  Uses aggregate initialization to extend the lifetime of
temporaries.  Can also be used to indicate structural members in a `py::Union` or
`py::Intersection` type, or named members in a `py::Tuple` type. */
template <StaticStr Name, typename T> requires (impl::arg_name<Name>)
struct Arg {
    static constexpr StaticStr name = Name;
    static constexpr impl::ArgKind kind = impl::ArgKind::POS | impl::ArgKind::KW;
    using type = T;
    using pos = impl::PositionalArg<Name, T>;
    using kw = impl::KeywordArg<Name, T>;
    using opt = impl::OptionalArg<Arg>;
    template <typename V> requires (impl::can_bind_arg<Arg, V>)
    using bind = impl::ArgTraits<Arg>::template bind<V>;
    using unbind = impl::ArgTraits<Arg>::unbind;

    type value;

    [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
        return value;
    }
    [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
        return value;
    }
    [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
        return &value;
    }
    [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
        return &value;
    }

    /* Argument rvalues are normally generated whenever a function is called.  Making
    them convertible to the underlying type means they can be used to call external
    C++ functions that are not aware of Python argument annotations. */
    [[nodiscard]] constexpr operator type() && {
        if constexpr (std::is_lvalue_reference_v<type>) {
            return value;
        } else {
            return std::move(value);
        }
    }

    /* Conversions to other types are also allowed, as long as the underlying type
    supports it. */
    template <typename U> requires (std::convertible_to<type, U>)
    [[nodiscard]] constexpr operator U() && {
        if constexpr (std::is_lvalue_reference_v<type>) {
            return value;
        } else {
            return std::move(value);
        }
    }
};


namespace impl {

    /* A singleton argument factory that allows arguments to be constructed via
    familiar assignment syntax, which extends the lifetime of temporaries. */
    template <StaticStr Name> requires (!Name.empty())
    struct ArgFactory {
        template <typename T>
        constexpr Arg<Name, T> operator=(T&& value) const {
            return {std::forward<T>(value)};
        }
    };

    template <typename T>
    constexpr bool _is_arg = false;
    template <typename T>
    constexpr bool _is_arg<OptionalArg<T>> = true;
    template <typename Arg, typename... Ts>
    constexpr bool _is_arg<BoundArg<Arg, Ts...>> = true;
    template <StaticStr Name, typename T>
    constexpr bool _is_arg<PositionalArg<Name, T>> = true;
    template <StaticStr Name, typename T>
    constexpr bool _is_arg<Arg<Name, T>> = true;
    template <StaticStr Name, typename T>
    constexpr bool _is_arg<KeywordArg<Name, T>> = true;
    template <typename T>
    constexpr bool _is_arg<ArgPack<T>> = true;
    template <typename T>
    constexpr bool _is_arg<KwargPack<T>> = true;
    template <typename T>
    concept is_arg = _is_arg<std::remove_cvref_t<T>>;

    /* Inspect a C++ argument at compile time.  Normalizes unannotated types to
    positional-only arguments to maintain C++ style. */
    template <typename T>
    struct ArgTraits {
        using type                                  = T;
        static constexpr StaticStr name             = "";
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

        template <typename... Vs> requires (can_bind_arg<T, Vs...>)
        using bind                                  = BoundArg<T, Vs...>;
        using unbind                                = T;
        using bound_to                              = py::args<>;
    };

    /* Inspect a C++ argument at compile time.  Forwards to the annotated type's
    interface where possible. */
    template <is_arg T>
    struct ArgTraits<T> {
    private:
        template <typename U>
        struct _bound {
            template <typename... Vs>
            using bind = BoundArg<U, Vs...>;
            using unbind = U;
            using types = args<>;
        };
        template <typename U, typename... Us>
        struct _bound<BoundArg<U, Us...>> {
            template <typename... Vs>
            using bind = BoundArg<U, Us..., Vs...>;
            using unbind = U;
            using types = args<Us...>;
        };

    public:
        using type                                  = std::remove_cvref_t<T>::type;
        static constexpr StaticStr name             = std::remove_cvref_t<T>::name;
        static constexpr ArgKind kind               = std::remove_cvref_t<T>::kind;
        static constexpr bool posonly() noexcept    { return kind.posonly(); }
        static constexpr bool pos() noexcept        { return kind.pos(); }
        static constexpr bool args() noexcept       { return kind.args(); }
        static constexpr bool kwonly() noexcept     { return kind.kwonly(); }
        static constexpr bool kw() noexcept         { return kind.kw(); }
        static constexpr bool kwargs() noexcept     { return kind.kwargs(); }
        static constexpr bool bound() noexcept      { return bound_to::n > 0; }
        static constexpr bool opt() noexcept        { return kind.opt(); }
        static constexpr bool variadic() noexcept   { return kind.variadic(); }

        template <typename... Vs> requires (can_bind_arg<T, Vs...>)
        using bind                                  = _bound<std::remove_cvref_t<T>>::template bind<Vs...>;
        using unbind                                = _bound<std::remove_cvref_t<T>>::unbind;
        using bound_to                              = _bound<std::remove_cvref_t<T>>::types;
    };

    template <is_arg Arg, typename T>
        requires (
            !ArgTraits<Arg>::bound() &&
            !variadic_args_name<ArgTraits<Arg>::name> &&
            !variadic_kwargs_name<ArgTraits<Arg>::name>
        )
    struct BoundArg<Arg, T> {
        static constexpr StaticStr name = ArgTraits<Arg>::name;
        static constexpr ArgKind kind = ArgTraits<Arg>::kind;
        using type = ArgTraits<Arg>::type;
        using unbind = Arg;

        type value;

        [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }

        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <is_arg Arg, typename... Ts>
        requires (
            !ArgTraits<Arg>::bound() &&
            variadic_args_name<ArgTraits<Arg>::name> &&
            !variadic_kwargs_name<ArgTraits<Arg>::name>
        )
    struct BoundArg<Arg, Ts...> {
        static constexpr StaticStr name = ArgTraits<Arg>::name;
        static constexpr impl::ArgKind kind = ArgKind::POS | ArgKind::VARIADIC;
        using type = std::conditional_t<
            std::is_lvalue_reference_v<typename ArgTraits<Arg>::type>,
            std::reference_wrapper<typename ArgTraits<Arg>::type>,
            std::remove_reference_t<typename ArgTraits<Arg>::type>
        >;
        using unbind = Arg;

        std::vector<type> value;

        [[nodiscard]] std::vector<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::vector<type>& operator*() const {
            return value;
        }
        [[nodiscard]] std::vector<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::vector<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator std::vector<type>() && {
            return std::move(value);
        }
    };

    template <is_arg Arg, typename... Ts>
        requires (
            !ArgTraits<Arg>::bound() &&
            !variadic_args_name<ArgTraits<Arg>::name> &&
            variadic_kwargs_name<ArgTraits<Arg>::name>
        )
    struct BoundArg<Arg, Ts...> {
        static constexpr StaticStr name = ArgTraits<Arg>::name;
        static constexpr ArgKind kind = ArgKind::KW | ArgKind::VARIADIC;
        using type = std::conditional_t<
            std::is_lvalue_reference_v<typename ArgTraits<Arg>::type>,
            std::reference_wrapper<typename ArgTraits<Arg>::type>,
            std::remove_reference_t<typename ArgTraits<Arg>::type>
        >;
        using unbind = Arg;

        std::unordered_map<std::string, type> value;

        [[nodiscard]] constexpr std::unordered_map<std::string, type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::unordered_map<std::string, type>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::unordered_map<std::string, type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::unordered_map<std::string, type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator std::unordered_map<std::string, type>() && {
            return std::move(value);
        }
    };

}


template <StaticStr Name, typename T> requires (impl::variadic_args_name<Name>)
struct Arg<Name, T> {
    static constexpr StaticStr name = Name;
    static constexpr impl::ArgKind kind = impl::ArgKind::POS | impl::ArgKind::VARIADIC;
    using type = std::conditional_t<
        std::is_lvalue_reference_v<T>,
        std::reference_wrapper<T>,
        std::remove_reference_t<T>
    >;
    template <typename... Vs> requires (impl::can_bind_arg<Arg, Vs...>)
    using bind = impl::ArgTraits<Arg>::template bind<Vs...>;
    using unbind = Arg;

    std::vector<type> value;

    [[nodiscard]] std::vector<type>& operator*() {
        return value;
    }
    [[nodiscard]] constexpr const std::vector<type>& operator*() const {
        return value;
    }
    [[nodiscard]] std::vector<type>* operator->() {
        return &value;
    }
    [[nodiscard]] constexpr const std::vector<type>* operator->() const {
        return &value;
    }

    [[nodiscard]] constexpr operator std::vector<type>() && {
        return std::move(value);
    }
};


template <StaticStr Name, typename T> requires (impl::variadic_kwargs_name<Name>)
struct Arg<Name, T> {
    static constexpr StaticStr name = Name;
    static constexpr impl::ArgKind kind = impl::ArgKind::KW | impl::ArgKind::VARIADIC;
    using type = std::conditional_t<
        std::is_lvalue_reference_v<T>,
        std::reference_wrapper<T>,
        std::remove_reference_t<T>
    >;
    template <typename... Vs> requires (impl::can_bind_arg<Arg, Vs...>)
    using bind = impl::ArgTraits<Arg>::template bind<Vs...>;
    using unbind = Arg;

    std::unordered_map<std::string, T> value;

    [[nodiscard]] constexpr std::unordered_map<std::string, T>& operator*() {
        return value;
    }
    [[nodiscard]] constexpr const std::unordered_map<std::string, T>& operator*() const {
        return value;
    }
    [[nodiscard]] constexpr std::unordered_map<std::string, T>* operator->() {
        return &value;
    }
    [[nodiscard]] constexpr const std::unordered_map<std::string, T>* operator->() const {
        return &value;
    }

    [[nodiscard]] constexpr operator std::unordered_map<std::string, T>() && {
        return std::move(value);
    }
};


/* A compile-time factory for binding keyword arguments with Python syntax.  constexpr
instances of this class can be used to provide an even more Pythonic syntax:

    constexpr auto x = py::arg<"x">;
    my_func(x = 42);
*/
template <StaticStr name>
constexpr impl::ArgFactory<name> arg {};


/* Dereference operator is used to emulate Python container unpacking when calling a
`py::Function` object.

A single unpacking operator passes the contents of an iterable container as positional
arguments to a function.  Unlike Python, only one such operator is allowed per call,
and it must be the last positional argument in the parameter list.  This allows the
type checker to ensure that the container's value type is minimally convertible to each
of the remaining positional arguments ahead of time, although in most cases, the number
of arguments cannot be determined until runtime.  Thus, if any arguments are missing or
extras are provided, the call will raise an exception similar to Python, rather than
failing statically at compile time.  This can be avoided by using standard positional
and keyword arguments instead, which can be fully verified at compile time, or by
including variadic positional arguments in the function signature, which will fully
consume any remaining arguments according to Python semantics.

A second unpacking operator promotes the arguments into keywords, and can only be used
if the container is dict-like, meaning it possess both `::key_type` and `::mapped_type`
aliases, and that indexing it with an instance of the key type returns a value of the
mapped type.  The actual unpacking is robust, and does not depend on the container
returning key-value pairs, although it will prefer them if so, followed by the result
of the `.items()` method if present, or by zipping `.keys()` and `.values()` if both
exist, and finally by iterating over the keys and indexing the container.  Similar to
the positional unpacking operator, only one of these may be present as the last keyword
argument in the parameter list, and a compile-time check is made to ensure that the
mapped type is convertible to any missing keyword arguments that are not explicitly
provided.

In both cases, the extra runtime complexity results in a small performance degradation
over a typical function call, which is minimized as much as possible. */
template <impl::inherits<Object> Self> requires (impl::iterable<Self>)
[[nodiscard]] auto operator*(Self&& self) {
    return impl::ArgPack<Self>{std::forward<Self>(self)};
}


/// TODO: these will require a Python side as well for use in Python


}


#endif  // BERTRAND_PYTHON_CORE_ARG_H
