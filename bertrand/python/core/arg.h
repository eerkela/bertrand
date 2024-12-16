#ifndef BERTRAND_PYTHON_CORE_ARG_H
#define BERTRAND_PYTHON_CORE_ARG_H

#include "declarations.h"
#include "object.h"
#include "except.h"


namespace py {

namespace impl {

    template <typename T>
    struct ArgTraits;

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
        _arg_name<Name> ||
        _variadic_args_name<Name> ||
        _variadic_kwargs_name<Name>;

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

    template <typename... Vs>
    static constexpr bool names_are_unique = true;
    template <typename V, typename... Vs>
    static constexpr bool names_are_unique<V, Vs...> =
        (
            impl::ArgTraits<V>::name.empty() ||
            ((impl::ArgTraits<V>::name != impl::ArgTraits<Vs>::name) && ...)
        ) &&
        names_are_unique<Vs...>;

    template <typename, typename = void>
    struct detect_arg {
        static constexpr bool value = false;
    };
    template <typename T>
    struct detect_arg<T, std::void_t<typename T::_detect_arg>> {
        static constexpr bool value = true;
    };
    template <typename T>
    concept is_arg = detect_arg<std::remove_cvref_t<T>>::value;

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
        template <typename, typename>
        friend struct detect_arg;
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

    /* A positional parameter pack obtained by dereferencing a Python object. */
    template <iterable T> requires (has_size<T>)
    struct ArgPack {
    private:
        template <typename, typename>
        friend struct detect_arg;
        using _detect_arg = void;

    public:
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

    template <typename Arg, typename... Ts>
    struct Bound;

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
template <StaticStr Name, typename T> requires (impl::arg_name<Name>)
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
    static constexpr StaticStr name = Name;
    static constexpr impl::ArgKind kind = impl::ArgKind::POS | impl::ArgKind::KW;
    using type = T;
    using bound_to = py::args<>;
    using unbind = Arg;
    type value;

    [[nodiscard]] constexpr Value& operator*() { return value; }
    [[nodiscard]] constexpr const Value& operator*() const { return value; }
    [[nodiscard]] constexpr Value* operator->() { return &value; }
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

    template <typename... Vs>
    static constexpr bool can_bind = false;
    template <std::convertible_to<type> V>
    static constexpr bool can_bind<V> =
        !impl::ArgTraits<V>::opt() &&
        !impl::ArgTraits<V>::variadic() &&
        (impl::ArgTraits<V>::name.empty() || impl::ArgTraits<V>::name == name);

    /* Bind a partial value to this argument. */
    template <std::convertible_to<type>... Vs>
        requires (
            sizeof...(Vs) == 1 &&
            (!impl::ArgTraits<Vs>::opt() && ...) &&
            (!impl::ArgTraits<Vs>::variadic() && ...) &&
            ((impl::ArgTraits<Vs>::name.empty() || impl::ArgTraits<Vs>::name == name) && ...)
        )
    using bind = impl::Bound<Arg, Vs...>;

    /* Marks the argument as optional. */
    struct opt {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

    public:
        static constexpr StaticStr name = Name;
        static constexpr impl::ArgKind kind = Arg::kind | impl::ArgKind::OPT;
        using type = T;
        using bound_to = py::args<>;
        using unbind = opt;
        type value;

        [[nodiscard]] constexpr Value& operator*() { return value; }
        [[nodiscard]] constexpr const Value& operator*() const { return value; }
        [[nodiscard]] constexpr Value* operator->() { return &value; }
        [[nodiscard]] constexpr const Value* operator->() const { return &value; }
        [[nodiscard]] constexpr operator type() && { return std::forward<type>(value); }
        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && { return std::forward<type>(value); }

        template <typename... Vs>
        static constexpr bool can_bind = Arg::can_bind<Vs...>;

        template <std::convertible_to<type>... Vs>
            requires (
                sizeof...(Vs) == 1 &&
                (!impl::ArgTraits<Vs>::opt() && ...) &&
                (!impl::ArgTraits<Vs>::variadic() && ...) &&
                ((impl::ArgTraits<Vs>::name.empty() || impl::ArgTraits<Vs>::name == name) && ...)
            )
        using bind = impl::Bound<opt, Vs...>;
    };

    /* Marks the argument as positional-only. */
    struct pos {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

    public:
        static constexpr StaticStr name = Name;
        static constexpr impl::ArgKind kind = impl::ArgKind::POS;
        using type = T;
        using bound_to = py::args<>;
        using unbind = pos;
        type value;

        [[nodiscard]] constexpr Value& operator*() { return value; }
        [[nodiscard]] constexpr const Value& operator*() const { return value; }
        [[nodiscard]] constexpr Value* operator->() { return &value; }
        [[nodiscard]] constexpr const Value* operator->() const { return &value; }
        [[nodiscard]] constexpr operator type() && { return std::forward<type>(value); }
        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && { return std::forward<type>(value); }

        template <typename... Vs>
        static constexpr bool can_bind = false;
        template <std::convertible_to<type> V>
        static constexpr bool can_bind<V> =
            impl::ArgTraits<V>::posonly() &&
            !impl::ArgTraits<V>::opt() &&
            !impl::ArgTraits<V>::variadic() &&
            (impl::ArgTraits<V>::name.empty() || impl::ArgTraits<V>::name == name);

        template <std::convertible_to<type>... Vs>
            requires (
                sizeof...(Vs) == 1 &&
                (impl::ArgTraits<Vs>::posonly() && ...) &&
                (!impl::ArgTraits<Vs>::opt() && ...) &&
                (!impl::ArgTraits<Vs>::variadic() && ...) &&
                ((impl::ArgTraits<Vs>::name.empty() || impl::ArgTraits<Vs>::name == name) && ...)
            )
        using bind = impl::Bound<pos, Vs...>;

        struct opt {
        private:
            template <typename, typename>
            friend struct impl::detect_arg;
            using _detect_arg = void;

        public:
            static constexpr StaticStr name = Name;
            static constexpr impl::ArgKind kind = pos::kind | impl::ArgKind::OPT;
            using type = T;
            using bound_to = py::args<>;
            using unbind = opt;
            type value;

            [[nodiscard]] constexpr Value& operator*() { return value; }
            [[nodiscard]] constexpr const Value& operator*() const { return value; }
            [[nodiscard]] constexpr Value* operator->() { return &value; }
            [[nodiscard]] constexpr const Value* operator->() const { return &value; }
            [[nodiscard]] constexpr operator type() && { return std::forward<type>(value); }
            template <typename U> requires (std::convertible_to<type, U>)
            [[nodiscard]] constexpr operator U() && { return std::forward<type>(value); }

            template <typename... Vs>
            static constexpr bool can_bind = pos::can_bind<Vs...>;

            template <std::convertible_to<type>... Vs>
                requires (
                    sizeof...(Vs) == 1 &&
                    (impl::ArgTraits<Vs>::posonly() && ...) &&
                    (!impl::ArgTraits<Vs>::opt() && ...) &&
                    (!impl::ArgTraits<Vs>::variadic() && ...) &&
                    ((impl::ArgTraits<Vs>::name.empty() || impl::ArgTraits<Vs>::name == name) && ...)
                )
            using bind = impl::Bound<opt, Vs...>;
        };
    };

    /* Marks the argument as keyword-only. */
    struct kw {
    private:
        template <typename, typename>
        friend struct impl::detect_arg;
        using _detect_arg = void;

    public:
        static constexpr StaticStr name = Name;
        static constexpr impl::ArgKind kind = impl::ArgKind::KW;
        using type = T;
        using bound_to = py::args<>;
        using unbind = kw;
        type value;

        [[nodiscard]] constexpr Value& operator*() { return value; }
        [[nodiscard]] constexpr const Value& operator*() const { return value; }
        [[nodiscard]] constexpr Value* operator->() { return &value; }
        [[nodiscard]] constexpr const Value* operator->() const { return &value; }
        [[nodiscard]] constexpr operator type() && { return std::forward<type>(value); }
        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && { return std::forward<type>(value); }

        template <typename... Vs>
        static constexpr bool can_bind = false;
        template <std::convertible_to<type> V>
        static constexpr bool can_bind<V> =
            impl::ArgTraits<V>::kw() &&
            !impl::ArgTraits<V>::opt() &&
            !impl::ArgTraits<V>::variadic() &&
            (impl::ArgTraits<V>::name.empty() || impl::ArgTraits<V>::name == name);

        template <std::convertible_to<type>... Vs>
            requires (
                sizeof...(Vs) == 1 &&
                (impl::ArgTraits<Vs>::kw() && ...) &&
                (!impl::ArgTraits<Vs>::opt() && ...) &&
                (!impl::ArgTraits<Vs>::variadic() && ...) &&
                ((impl::ArgTraits<Vs>::name.empty() || impl::ArgTraits<Vs>::name == name) && ...)
            )
        using bind = impl::Bound<kw, Vs...>;

        struct opt {
        private:
            template <typename, typename>
            friend struct impl::detect_arg;
            using _detect_arg = void;

        public:
            static constexpr StaticStr name = Name;
            static constexpr impl::ArgKind kind = kw::kind | impl::ArgKind::OPT;
            using type = T;
            using bound_to = py::args<>;
            using unbind = opt;
            type value;

            [[nodiscard]] constexpr Value& operator*() { return value; }
            [[nodiscard]] constexpr const Value& operator*() const { return value; }
            [[nodiscard]] constexpr Value* operator->() { return &value; }
            [[nodiscard]] constexpr const Value* operator->() const { return &value; }
            [[nodiscard]] constexpr operator type() && { return std::forward<type>(value); }
            template <typename U> requires (std::convertible_to<type, U>)
            [[nodiscard]] constexpr operator U() && { return std::forward<type>(value); }

            template <typename... Vs>
            static constexpr bool can_bind = kw::can_bind<Vs...>;

            template <std::convertible_to<type>... Vs>
                requires (
                    sizeof...(Vs) == 1 &&
                    (impl::ArgTraits<Vs>::kw() && ...) &&
                    (!impl::ArgTraits<Vs>::opt() && ...) &&
                    (!impl::ArgTraits<Vs>::variadic() && ...) &&
                    ((impl::ArgTraits<Vs>::name.empty() || impl::ArgTraits<Vs>::name == name) && ...)
                )
            using bind = impl::Bound<opt, Vs...>;
        };
    };
};


/* Specialization for variadic positional args, whose names are prefixed by a leading
asterisk. */
template <StaticStr Name, typename T> requires (impl::variadic_args_name<Name>)
struct Arg<Name, T> {
private:
    template <typename, typename>
    friend struct impl::detect_arg;
    using _detect_arg = void;

public:
    static constexpr StaticStr name = Name;
    static constexpr impl::ArgKind kind = impl::ArgKind::POS | impl::ArgKind::VARIADIC;
    using type = T;
    using vec = std::vector<std::conditional_t<
        std::is_lvalue_reference_v<T>,
        std::reference_wrapper<T>,
        std::remove_reference_t<T>
    >>;
    using bound_to = py::args<>;
    using unbind = Arg;
    vec value;

    [[nodiscard]] constexpr vec& operator*() { return value; }
    [[nodiscard]] constexpr const vec& operator*() const { return value; }
    [[nodiscard]] constexpr vec* operator->() { return &value; }
    [[nodiscard]] constexpr const vec* operator->() const { return &value; }
    [[nodiscard]] constexpr operator vec() && { return std::move(value); }
    template <typename U> requires (std::convertible_to<vec, U>)
    [[nodiscard]] constexpr operator U() && { return std::move(value); }
    [[nodiscard]] constexpr decltype(auto) operator[](vec::size_type i) {
        return value[i];
    }
    [[nodiscard]] constexpr decltype(auto) operator[](vec::size_type i) const {
        return value[i];
    }

    template <typename... Vs>
    static constexpr bool can_bind = false;
    template <std::convertible_to<T>... Vs>
    static constexpr bool can_bind<Vs...> =
        (impl::ArgTraits<Vs>::posonly() && ...) &&
        (!impl::ArgTraits<Vs>::opt() && ...) &&
        (!impl::ArgTraits<Vs>::variadic() && ...) &&
        impl::names_are_unique<Vs...>;

    template <std::convertible_to<T>... Vs>
        requires (
            (impl::ArgTraits<Vs>::posonly() && ...) &&
            (!impl::ArgTraits<Vs>::opt() && ...) &&
            (!impl::ArgTraits<Vs>::variadic() && ...) &&
            impl::names_are_unique<Vs...>
        )
    using bind = impl::Bound<Arg, Vs...>;
};


/* Specialization for variadic keyword args, whose names are prefixed by 2 leading
asterisks. */
template <StaticStr Name, typename T> requires (impl::variadic_kwargs_name<Name>)
struct Arg<Name, T> {
private:
    template <typename, typename>
    friend struct impl::detect_arg;
    using _detect_arg = void;

public:
    static constexpr StaticStr name = Name;
    static constexpr impl::ArgKind kind = impl::ArgKind::KW | impl::ArgKind::VARIADIC;
    using type = T;
    using map = std::unordered_map<std::string, std::conditional_t<
        std::is_lvalue_reference_v<type>,
        std::reference_wrapper<type>,
        std::remove_reference_t<type>
    >>;
    using bound_to = py::args<>;
    using unbind = Arg;
    map value;

    [[nodiscard]] constexpr map& operator*() { return value; }
    [[nodiscard]] constexpr const map& operator*() const { return value; }
    [[nodiscard]] constexpr map* operator->() { return &value; }
    [[nodiscard]] constexpr const map* operator->() const { return &value; }
    [[nodiscard]] constexpr operator map() && { return std::move(value); }
    template <typename U> requires (std::convertible_to<map, U>)
    [[nodiscard]] constexpr operator U() && { return std::move(value); }
    [[nodiscard]] constexpr decltype(auto) operator[](const std::string& key) {
        return value[key];
    }
    [[nodiscard]] constexpr decltype(auto) operator[](std::string&& key) {
        return value[std::move(key)];
    }

    template <typename... Vs>
    static constexpr bool can_bind = false;
    template <std::convertible_to<T>... Vs>
    static constexpr bool can_bind<Vs...> =
        (impl::ArgTraits<Vs>::kw() && ...) &&
        (!impl::ArgTraits<Vs>::opt() && ...) &&
        (!impl::ArgTraits<Vs>::variadic() && ...) &&
        impl::names_are_unique<Vs...>;

    template <std::convertible_to<T>... Vs>
        requires (
            (impl::ArgTraits<Vs>::kw() && ...) &&
            (!impl::ArgTraits<Vs>::opt() && ...) &&
            (!impl::ArgTraits<Vs>::variadic() && ...) &&
            impl::names_are_unique<Vs...>
        )
    using bind = impl::Bound<Arg, Vs...>;
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

        template <typename... Vs>
        static constexpr bool can_bind = false;
        template <std::convertible_to<type> V>
        static constexpr bool can_bind<V> =
            impl::ArgTraits<V>::posonly() &&
            !impl::ArgTraits<V>::opt() &&
            !impl::ArgTraits<V>::variadic() &&
            (impl::ArgTraits<V>::name.empty() || impl::ArgTraits<V>::name == name);

        template <typename... Vs> requires (can_bind<Vs...>)
        using bind                                  = Bound<T, Vs...>;
        using bound_to                              = py::args<>;
        using unbind                                = T;
    };

    /* Inspect a C++ argument at compile time.  Forwards to the annotated type's
    interface where possible. */
    template <is_arg T>
    struct ArgTraits<T> {
    private:
        using T2 = std::remove_cvref_t<T>;

    public:
        using type                                  = T2::type;
        static constexpr StaticStr name             = T2::name;
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
    };

    template <typename Arg, typename T> requires (!ArgTraits<Arg>::variadic())
    struct Bound<Arg, T> {
    private:
        template <typename, typename>
        friend struct detect_arg;
        using _detect_arg = void;
        using Value = std::remove_reference_t<typename ArgTraits<Arg>::type>;

    public:
        static constexpr StaticStr name = ArgTraits<Arg>::name;
        static constexpr ArgKind kind = ArgTraits<Arg>::kind;
        using type = ArgTraits<Arg>::type;
        using bound_to = ArgTraits<Arg>::bound_to;
        using unbind = Arg;
        type value;

        [[nodiscard]] constexpr Value& operator*() { return value; }
        [[nodiscard]] constexpr const Value& operator*() const { return value; }
        [[nodiscard]] constexpr Value* operator->() { return &value; }
        [[nodiscard]] constexpr const Value* operator->() const { return &value; }
        [[nodiscard]] constexpr operator type() && { return std::forward<type>(value); }
        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && { return std::forward<type>(value); }

        template <typename... Vs>
        static constexpr bool can_bind = ArgTraits<Arg>::template can_bind<Vs...>;
        template <typename... Vs> requires (can_bind<Vs...>)
        using bind = ArgTraits<Arg>::template bind<Vs...>;
    };

    template <typename Arg, typename... Ts> requires (ArgTraits<Arg>::args())
    struct Bound<Arg, Ts...> {
    private:
        template <typename, typename>
        friend struct detect_arg;
        using _detect_arg = void;

        template <typename, typename...>
        struct rebind;
        template <typename... curr, typename... Vs>
        struct rebind<py::args<curr...>, Vs...> {
            static constexpr bool value =
                ArgTraits<Arg>::template can_bind<curr..., Vs...>;
            using type = ArgTraits<Arg>::template bind<curr..., Vs...>;
        };

    public:
        static constexpr StaticStr name = ArgTraits<Arg>::name;
        static constexpr impl::ArgKind kind = ArgTraits<Arg>::kind;
        using type = ArgTraits<Arg>::type;
        using vec = std::vector<std::conditional_t<
            std::is_lvalue_reference_v<type>,
            std::reference_wrapper<type>,
            std::remove_reference_t<type>
        >>;
        using bound_to = py::args<Ts...>;
        using unbind = Arg;
        vec value;

        [[nodiscard]] constexpr vec& operator*() { return value; }
        [[nodiscard]] constexpr const vec& operator*() const { return value; }
        [[nodiscard]] constexpr vec* operator->() { return &value; }
        [[nodiscard]] constexpr const vec* operator->() const { return &value; }
        [[nodiscard]] constexpr operator vec() && { return std::move(value); }
        template <typename U> requires (std::convertible_to<vec, U>)
        [[nodiscard]] constexpr operator U() && { return std::move(value); }
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

    template <typename Arg, typename... Ts> requires (ArgTraits<Arg>::kwargs())
    struct Bound<Arg, Ts...> {
    private:
        template <typename, typename>
        friend struct detect_arg;
        using _detect_arg = void;

        template <typename, typename...>
        struct rebind;
        template <typename... curr, typename... Vs>
        struct rebind<py::args<curr...>, Vs...> {
            static constexpr bool value =
                ArgTraits<Arg>::template can_bind<curr..., Vs...>;
            using type = ArgTraits<Arg>::template bind<curr..., Vs...>;
        };

    public:
        static constexpr StaticStr name = ArgTraits<Arg>::name;
        static constexpr impl::ArgKind kind = ArgTraits<Arg>::kind;
        using type = ArgTraits<Arg>::type;
        using map = std::unordered_map<std::string, std::conditional_t<
            std::is_lvalue_reference_v<type>,
            std::reference_wrapper<type>,
            std::remove_reference_t<type>
        >>;
        using bound_to = py::args<Ts...>;
        using unbind = Arg;
        map value;

        [[nodiscard]] constexpr map& operator*() { return value; }
        [[nodiscard]] constexpr const map& operator*() const { return value; }
        [[nodiscard]] constexpr map* operator->() { return &value; }
        [[nodiscard]] constexpr const map* operator->() const { return &value; }
        [[nodiscard]] constexpr operator map() && { return std::move(value); }
        template <typename U> requires (std::convertible_to<map, U>)
        [[nodiscard]] constexpr operator U() && { return std::move(value); }
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

}


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
