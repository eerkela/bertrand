#ifndef BERTRAND_FUNC_H
#define BERTRAND_FUNC_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"
#include "bertrand/iter.h"  // TODO: can possibly remove this dependency if I forward declare in common.h
#include "bertrand/static_str.h"

#include <unordered_map>


namespace bertrand {


#ifdef BERTRAND_MAX_ARGS
    constexpr size_t MAX_ARGS = BERTRAND_MAX_ARGS;
#else
    constexpr size_t MAX_ARGS = sizeof(size_t) * 8;
#endif


#ifdef BERTRAND_MAX_OVERLOADS
    constexpr size_t MAX_OVERLOADS = BERTRAND_MAX_OVERLOADS;
#else
    constexpr size_t MAX_OVERLOADS = 256;
#endif


#ifdef BERTRAND_OVERLOAD_CACHE
    constexpr size_t OVERLOAD_CACHE = BERTRAND_OVERLOAD_CACHE;
#else
    constexpr size_t OVERLOAD_CACHE = 128;
#endif


static_assert(MAX_ARGS > 0, "`BERTRAND_MAX_ARGS` must be positive.");
static_assert(MAX_OVERLOADS > 0, "`BERTRAND_MAX_OVERLOADS` must be positive.");
static_assert(OVERLOAD_CACHE > 0, "`BERTRAND_OVERLOAD_CACHE` must be positive.");


////////////////////////////////////
////    ARGUMENT ANNOTATIONS    ////
////////////////////////////////////


namespace impl {

    template <meta::invocable F> requires (!meta::invoke_returns<void, F>)
    struct materialize {
        meta::remove_rvalue<F> func;
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator()(this Self&& self)
            noexcept (meta::nothrow::invocable<F>)
        {
            return (std::forward<Self>(self).func());
        }
    };

}


namespace meta {

    namespace detail {

        template <typename T>
        constexpr bool materialize = false;
        template <typename T>
        constexpr bool materialize<impl::materialize<T>> = true;

    }

    template <typename F>
    concept materialization_function = invocable<F> && !invoke_returns<void, F>;

    template <typename F>
    concept materialize = detail::materialize<unqualify<F>>;

    namespace detail {

        /* Argument names must only consist of alphanumerics and underscores, following
        the leading character(s). */
        template <const auto& /* static_str */ name, size_t... Is>
        constexpr bool validate_arg_name(size_t offset, ::std::index_sequence<Is...>)
            noexcept
        {
            return ((
                impl::char_isalnum(name[offset + Is]) ||
                name[offset + Is] == '_'
            ) && ...);
        }

        /* Normal args cannot have numerics as a first character. */
        template <const auto& /* static_str */ name>
        constexpr bool valid_arg_name = false;
        template <const auto& /* static_str */ name> requires (name.size() > 0)
        constexpr bool valid_arg_name<name> =
            (impl::char_isalpha(name[0]) || name[0] == '_') &&
            validate_arg_name<name>(1, ::std::make_index_sequence<name.size() - 1>());

        /* Variadic positional args must have a single asterisk followed by a
        non-numeric. */
        template <const auto& /* static_str */ name>
        constexpr bool valid_args_name = false;
        template <const auto& /* static_str */ name> requires (name.size() > 1)
        constexpr bool valid_args_name<name> =
            name[0] == '*' &&
            (impl::char_isalpha(name[1]) || name[1] == '_') &&
            validate_arg_name<name>(2, ::std::make_index_sequence<name.size() - 2>());

        /* Variadic keyword args must have a double asterisk followed by a
        non-numeric. */
        template <const auto& /* static_str */ name>
        constexpr bool valid_kwargs_name = false;
        template <const auto& /* static_str */ name> requires (name.size() > 2)
        constexpr bool valid_kwargs_name<name> =
            name[0] == '*' &&
            name[1] == '*' &&
            (impl::char_isalpha(name[2]) || name[2] == '_') &&
            validate_arg_name<name>(3, ::std::make_index_sequence<name.size() - 3>());

        /* A special '/' separator is  */
        template <const auto& /* static_str */ name>
        constexpr bool valid_posonly_separator = false;
        template <const auto& /* static_str */ name> requires (name.size() == 1)
        constexpr bool valid_posonly_separator<name> = name[0] == '/';

        template <const auto& /* static_str */ name>
        constexpr bool valid_kwonly_separator = false;
        template <const auto& /* static_str */ name> requires (name.size() == 1)
        constexpr bool valid_kwonly_separator<name> = name[0] == '*';

        template <const auto& /* static_str */ name>
        constexpr bool valid_return_annotation = false;
        template <const auto& /* static_str */ name> requires (name.size() == 2)
        constexpr bool valid_return_annotation<name> = name[0] == '-' && name[1] == '>';

    }

    template <bertrand::static_str name>
    concept valid_arg_name = detail::valid_arg_name<name>;

    template <bertrand::static_str name>
    concept valid_args_name = detail::valid_args_name<name>;

    template <bertrand::static_str name>
    concept valid_kwargs_name = detail::valid_kwargs_name<name>;

    template <bertrand::static_str name>
    concept valid_posonly_separator = detail::valid_posonly_separator<name>;

    template <bertrand::static_str name>
    concept valid_kwonly_separator = detail::valid_kwonly_separator<name>;

    template <bertrand::static_str name>
    concept valid_return_annotation = detail::valid_return_annotation<name>;

    template <bertrand::static_str name>
    concept valid_arg =
        valid_arg_name<name> ||
        valid_args_name<name> ||
        valid_kwargs_name<name> ||
        valid_posonly_separator<name> ||
        valid_kwonly_separator<name> ||
        valid_return_annotation<name>;

    namespace detail {

        template <typename T>
        constexpr bool arg = false;

        template <typename T>
        constexpr bool arg_pack = false;

        template <typename T>
        constexpr bool kwarg_pack = false;

    }

    template <typename T>
    concept arg = detail::arg<unqualify<T>>;

    template <arg T>
    using arg_type = unqualify<T>::type;

    template <arg T>
    using partials = unqualify<T>::partials;

    template <arg T>
    constexpr const auto& arg_id = unqualify<T>::id;

    template <typename T>
    concept typed_arg = arg<T> && not_void<arg_type<T>>;

    template <typename T>
    concept partial_arg = arg<T> && partials<T>::size > 0;

    template <typename T>
    concept standard_arg = arg<T> && valid_arg_name<arg_id<T>>;

    template <typename T>
    concept variadic = arg<T> && (
        valid_args_name<arg_id<T>> ||
        valid_kwargs_name<arg_id<T>> ||
        detail::arg_pack<unqualify<T>> ||
        detail::kwarg_pack<unqualify<T>>
    );

    template <typename T>
    concept arg_pack = variadic<T> && detail::arg_pack<unqualify<T>>;

    template <typename T>
    concept kwarg_pack = variadic<T> && detail::kwarg_pack<unqualify<T>>;

    template <typename T>
    concept variadic_positional = variadic<T> && (valid_args_name<arg_id<T>> || arg_pack<T>);

    template <typename T>
    concept variadic_keyword = variadic<T> && (valid_kwargs_name<arg_id<T>> || kwarg_pack<T>);

    template <typename T>
    concept arg_separator =
        arg<T> && (valid_posonly_separator<arg_id<T>> || valid_kwonly_separator<arg_id<T>>);

    template <typename T>
    concept positional_only_separator = arg_separator<T> && valid_posonly_separator<arg_id<T>>;

    template <typename T>
    concept keyword_only_separator = arg_separator<T> && valid_kwonly_separator<arg_id<T>>;

    template <typename T>
    concept return_annotation = arg<T> && valid_return_annotation<arg_id<T>>;

    /// TODO: maybe just meta::has_value<T>, meta::value_type<T>, and meta::value_returns<R, T>,
    /// which could be shared with monads that fit the same pattern.

    template <typename T>
    concept bound_arg = arg<T> && requires(T&& t) { ::std::forward<T>(t).value(); };

    template <bound_arg T>
    using arg_value = decltype(::std::declval<T>().value());

    namespace nothrow {

        template <typename T>
        concept bound_arg = meta::bound_arg<T> && requires(T&& t) {
            { ::std::forward<T>(t).value() } noexcept;
        };

        template <nothrow::bound_arg T>
        using arg_value = decltype(::std::declval<T>().value());

    }

    namespace detail {

        template <typename T>
        constexpr bertrand::static_str arg_prefix = "";
        template <typename T>
            requires (
                meta::return_annotation<T> ||
                meta::positional_only_separator<T> ||
                meta::keyword_only_separator<T>
            )
        constexpr bertrand::static_str arg_prefix<T> = meta::arg_id<T>;
        template <meta::variadic_positional T>
        constexpr bertrand::static_str arg_prefix<T> =
            meta::arg_id<T>.template get<bertrand::slice{0, 1}>();
        template <meta::variadic_keyword T>
        constexpr bertrand::static_str arg_prefix<T> =
            meta::arg_id<T>.template get<bertrand::slice{0, 2}>();

        template <typename T>
        constexpr bertrand::static_str arg_name = "";
        template <meta::standard_arg T>
        constexpr bertrand::static_str arg_name<T> = meta::arg_id<T>;
        template <meta::variadic_positional T>
        constexpr bertrand::static_str arg_name<T> =
            meta::arg_id<T>.template get<bertrand::slice{1, bertrand::None}>();
        template <meta::variadic_keyword T>
        constexpr bertrand::static_str arg_name<T> =
            meta::arg_id<T>.template get<bertrand::slice{2, bertrand::None}>();

    }

    template <typename T>
    constexpr bertrand::static_str arg_prefix = detail::arg_prefix<T>;

    template <typename T>
    constexpr bertrand::static_str arg_name = detail::arg_name<T>;

    namespace detail {

        template <typename T>
        constexpr bool unbound_arg = meta::arg<T> && !(
            meta::bound_arg<T> || meta::arg_pack<T> || meta::kwarg_pack<T>
        );

        template <typename... Ts>
        constexpr bool proper_arg_order = true;
        template <typename T, typename... Ts>
        constexpr bool proper_arg_order<T, Ts...> = proper_arg_order<Ts...>;
        template <meta::arg_pack T, typename... Ts>
        constexpr bool proper_arg_order<T, Ts...> = (
            (meta::standard_arg<Ts> || meta::kwarg_pack<Ts>) &&
            ... &&
            proper_arg_order<Ts...>
        );
        template <meta::standard_arg T, typename... Ts>
        constexpr bool proper_arg_order<T, Ts...> = (
            (meta::standard_arg<Ts> || meta::kwarg_pack<Ts>) &&
            ... &&
            proper_arg_order<Ts...>
        );
        template <meta::kwarg_pack T, typename... Ts>
        constexpr bool proper_arg_order<T, Ts...> = sizeof...(Ts) == 0;

        template <typename... Ts>
        constexpr bool no_duplicate_names = true;
        template <typename T, typename... Ts>
        constexpr bool no_duplicate_names<T, Ts...> = (meta::arg_name<T>.empty() || (
            (meta::arg_name<T> != meta::arg_name<Ts>) && ...
        )) && no_duplicate_names<Ts...>;

    }

    template <typename... Ts>
    concept no_unbound_args = (!detail::unbound_arg<Ts> && ...);

    template <typename... Ts>
    concept proper_arg_order = detail::proper_arg_order<Ts...>;

    template <typename... Ts>
    concept no_duplicate_packs =
        (arg_pack<Ts> + ... + 0) <= 1 || (kwarg_pack<Ts> + ... + 0) <= 1;

    template <typename... Ts>
    concept no_duplicate_names = detail::no_duplicate_names<Ts...>;

    template <typename... Ts>
    concept source_args =
        (not_void<Ts> && ...) &&
        no_unbound_args<Ts...> &&
        proper_arg_order<Ts...> &&
        no_duplicate_packs<Ts...> &&
        no_duplicate_names<Ts...>;

};


namespace impl {

    /* An annotation representing an arbitrary argument in a Python-style function
    declaration. */
    template <static_str ID, typename T = void, typename V = void>
        requires (meta::valid_arg<ID>)
    struct arg;

    /* A specialization of `Arg` representing a normal positional or keyword argument.
    Such arguments can be explicitly typed and bound accordingly. */
    template <static_str ID, typename T>
        requires (meta::valid_arg<ID> && meta::valid_arg_name<ID>)
    struct arg<ID, T, void> {
        using base_type = arg;
        using type = T;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* Indexing the argument sets its `type`. */
        template <meta::not_void type> requires (meta::is_void<T>)
        constexpr auto operator[](std::type_identity<type>) && noexcept {
            return arg<ID, meta::remove_rvalue<type>, void>{};
        }

        /* Assigning to the argument binds a value to it, which is interpreted as
        either a default value if the assignment is done in the function signature, or
        as a keyword argument if done at the call site.  For arguments without an
        explicit type, the value is unconstrained, and can take any type. */
        template <typename V> requires (meta::is_void<T>)
        constexpr auto operator=(V&& val) &&
            noexcept (requires{
                {arg<ID, T, meta::remove_rvalue<V>>{std::forward<V>(val)}} noexcept;
            })
            requires (requires{
                {arg<ID, T, meta::remove_rvalue<V>>{std::forward<V>(val)}};
            })
        {
            return arg<ID, T, meta::remove_rvalue<V>>{std::forward<V>(val)};
        }

        /* Assigning to the argument binds a value to it, which is interpreted as
        either a default value if the assignment is done in the function signature, or
        as a keyword argument if done at the call site.  If the argument has an
        explicit type, then the value must be convertible to that type. */
        template <typename V>  requires (meta::not_void<T>)
        constexpr auto operator=(V&& val) &&
            noexcept (requires{
                {arg<ID, T, meta::remove_rvalue<V>>{std::forward<V>(val)}} noexcept;
            })
            requires (meta::convertible_to<V, T> && requires{
                arg<ID, T, meta::remove_rvalue<V>>{std::forward<V>(val)};
            })
        {
            return arg<ID, T, meta::remove_rvalue<V>>{std::forward<V>(val)};
        }

        /* Calling the argument annotation with a function of zero arguments sets it as
        a materialization function, which will be called to produce a bound value in
        case the result would otherwise not be a constant expression, or would be
        expensive to compute for some reason.  If the argument has an explicit type,
        then the result must be convertible to that type. */
        template <meta::materialization_function F> requires (meta::is_void<T>)
        constexpr auto operator()(F&& f) &&
            noexcept (requires{{arg<ID, T, impl::materialize<F>>{std::forward<F>(f)}} noexcept;})
            requires (requires{{arg<ID, T, impl::materialize<F>>{std::forward<F>(f)}};})
        {
            return arg<ID, T, impl::materialize<F>>{std::forward<F>(f)};
        }

        /* Calling the argument annotation with a function of zero arguments sets it as
        a materialization function, which will be called to produce a bound value in
        case the result would otherwise not be a constant expression, or would be
        expensive to compute for some reason.  If the argument has an explicit type,
        then the result must be convertible to that type. */
        template <meta::materialization_function F> requires (meta::not_void<T>)
        constexpr auto operator()(F&& f) &&
            noexcept (requires{{arg<ID, T, impl::materialize<F>>{std::forward<F>(f)}} noexcept;})
            requires (
                meta::invoke_returns<T, F> &&
                requires{{arg<ID, T, impl::materialize<F>>{std::forward<F>(f)}};}
            )
        {
            return arg<ID, T, impl::materialize<F>>{std::forward<F>(f)};
        }

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `Arg` representing a variadic positional argument.  Such
    arguments can be typed, but cannot be bound. */
    template <static_str ID, typename T>
        requires (meta::valid_arg<ID> && meta::valid_args_name<ID>)
    struct arg<ID, T, void> {
        using base_type = arg;
        using type = T;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* Indexing the argument sets its `type`. */
        template <meta::not_void type> requires (meta::is_void<T>)
        constexpr auto operator[](std::type_identity<type>) && noexcept {
            return arg<ID, meta::remove_rvalue<type>, void>{};
        }

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `Arg` representing a variadic keyword argument.  Such
    arguments can be typed, but cannot be bound. */
    template <static_str ID, typename T>
        requires (meta::valid_arg<ID> && meta::valid_kwargs_name<ID>)
    struct arg<ID, T, void> {
        using base_type = arg;
        using type = T;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* Indexing the argument sets its `type`. */
        template <meta::not_void type> requires (meta::is_void<T>)
        constexpr auto operator[](std::type_identity<type>) && noexcept {
            return arg<ID, meta::remove_rvalue<type>, void>{};
        }

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `arg` representing a positional-only separator.  Such
    arguments cannot be typed and cannot be bound. */
    template <static_str ID>
        requires (meta::valid_arg<ID> && meta::valid_posonly_separator<ID>)
    struct arg<ID, void, void> {
        using base_type = arg;
        using type = void;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `arg` representing a keyword-only separator.  Such
    arguments cannot be typed and cannot be bound. */
    template <static_str ID>
        requires (meta::valid_arg<ID> && meta::valid_kwonly_separator<ID>)
    struct arg<ID, void, void> {
        using base_type = arg;
        using type = void;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `Arg` representing a return type annotation.  Such arguments
    can be typed, but cannot be bound. */
    template <static_str ID, typename T>
        requires (meta::valid_arg<ID> && meta::valid_return_annotation<ID>)
    struct arg<ID, T, void> {
        using base_type = arg;
        using type = T;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* Indexing the argument sets its `type`. */
        template <meta::not_void type> requires (meta::is_void<T>)
        constexpr auto operator[](std::type_identity<type>) && noexcept {
            return arg<ID, meta::remove_rvalue<type>, void>{};
        }

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `arg` for arguments that have been bound to a value, but
    otherwise have no explicit type.  Most call-site arguments fall into this
    category. */
    template <static_str ID, meta::not_void V> requires (meta::valid_arg<ID>)
    struct arg<ID, void, V> {
        using base_type = arg;
        using type = void;
        using value_type = V;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        [[no_unique_address]] V m_value;

        /* Directly forward a bound value of any type. */
        template <typename Self> requires (!meta::materialize<V>)
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_value);
        }

        /* Invoke a materialization function with unconstrained return type. */
        template <typename Self> requires (meta::materialize<V>)
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).m_value()} noexcept;})
            requires (requires{{std::forward<Self>(self).m_value()};})
        {
            return (std::forward<Self>(self).m_value());
        }

        /* STL-compliant `swap()` method for argument annotations.  Defers to the
        proper swap operator for the bound value. */
        constexpr void swap(arg& other)
            noexcept (meta::lvalue<V> || meta::nothrow::swappable<V>)
            requires (meta::lvalue<V> || meta::swappable<V>)
        {
            if constexpr (meta::lvalue<V>) {
                V temp = m_value;
                std::construct_at(this, other.m_value);
                std::construct_at(&other, temp);
            } else {
                std::ranges::swap(m_value, other.m_value);
            }
        }
    };

    /* A specialization of `arg` for arguments that have both an explicit type and have
    been bound to a value.  Most signature arguments that have a default value fall into
    this category. */
    template <static_str ID, meta::not_void T, meta::not_void V> requires (meta::valid_arg<ID>)
    struct arg<ID, T, V> {
        using base_type = arg;
        using type = T;
        using value_type = V;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        [[no_unique_address]] V m_value;

        /* Implicitly convert a bound value to the expected type. */
        template <typename Self>
        [[nodiscard]] constexpr T value(this Self&& self)
            noexcept (requires{
                {std::forward<Self>(self).m_value} -> meta::nothrow::convertible_to<T>;
            })
            requires (requires{
                {std::forward<Self>(self).m_value} -> meta::convertible_to<T>;
            })
        {
            return std::forward<Self>(self).m_value;
        }

        /* Invoke a materialization function and implicitly convert to the expected
        type. */
        template <typename Self> requires (meta::materialize<V>)
        [[nodiscard]] constexpr T value(this Self&& self)
            noexcept (requires{
                {std::forward<Self>(self).m_value()} noexcept -> meta::nothrow::convertible_to<T>;
            })
            requires (requires{
                {std::forward<Self>(self).m_value()} -> meta::convertible_to<T>;
            })
        {
            return std::forward<Self>(self).m_value();
        }

        /* STL-compliant `swap()` method for argument annotations.  Defers to the
        proper swap operator for the bound value. */
        constexpr void swap(arg& other)
            noexcept (meta::lvalue<V> || meta::nothrow::swappable<V>)
            requires (meta::lvalue<V> || meta::swappable<V>)
        {
            if constexpr (meta::lvalue<V>) {
                V temp = m_value;
                std::construct_at(this, other.m_value);
                std::construct_at(&other, temp);
            } else {
                std::ranges::swap(m_value, other.m_value);
            }
        }
    };

}


namespace meta {

    namespace detail {
        template <bertrand::static_str ID, typename T, typename V>
        constexpr bool arg<impl::arg<ID, T, V>> = true;
    }

}


/* Argument creation operator.

This is a user-defined string literal operator that serves as an entry point for
Python-style calling semantics in C++.  It returns a compile-time `impl::Arg` construct
with a miniature DSL for defining function arguments according to Python semantics.
Typically, these are used in combination with the `def()` operator to inform the
compiler of a function's expected signature, like so:

    ```
    constexpr auto func = def<{"I"_[^size_t]}, "obj"_>([]<size_t I>(const auto& obj) {
        return std::get<I>(obj);
    });

    static_assert(func.call<1>(std::array{1, 2, 3}) == 2);
    static_assert(func.call<"I"_ = 1>(std::array{1, 2, 3}) == 2);
    static_assert(func.call<1>("obj"_ = std::array{1, 2, 3}) == 2);
    static_assert(func.call<"I"_ = 1>("obj"_ = std::array{1, 2, 3}) == 2);
    ```

This is a minimal example, but the syntax can be extended to support arbitrary calling
conventions and argument types.  Note that an extra initializer list of arguments can
be provided at the start of the `def<>()` call to signify explicit template parameters.
The same operator is used to define both kinds of arguments - the extra braces are
what do the promotion. */
template <static_str ID> requires (meta::valid_arg<ID>)
[[nodiscard]] constexpr auto operator""_() noexcept { return impl::arg<ID>{}; }


/* A simple helper to make it slightly easier to define `std::type_identity` objects
for a given type annotation.  Eventually, this will be deprecated in favor of static
reflection info, which should allow me to model template template parameters, concepts,
etc. */
template <meta::not_void T>
constexpr std::type_identity<T> type;


/* ADL `swap()` operator for argument annotations. */
template <meta::arg T>
constexpr void swap(T& a, T& b)
    noexcept (requires{{a.swap(b)} noexcept;})
    requires (requires{{a.swap(b)};})
{
    a.swap(b);
}


//////////////////////////////
////    ARGUMENT PACKS    ////
//////////////////////////////


namespace meta {

    namespace detail {

        template <typename T>
        constexpr bool args = false;

        template <size_t I, typename...>
        struct _extract_keywords {
            static constexpr bool hashable = false;
            static constexpr bertrand::string_map<size_t> value {};
        };
        template <size_t I, size_t... Is, auto... out>
            requires (sizeof...(out) == 0 || meta::perfectly_hashable<out...>)
        struct _extract_keywords<
            I,
            ::std::index_sequence<Is...>,
            bertrand::string_list<out...>
        > {
            static constexpr bool hashable = true;
            static constexpr bertrand::string_map<size_t, out...> value {Is...};
        };
        template <size_t I, size_t... Is, auto... out, typename T, typename... Ts>
            requires (!meta::standard_arg<T> && !meta::variadic_keyword<T>)
        struct _extract_keywords<
            I,
            ::std::index_sequence<Is...>,
            bertrand::string_list<out...>,
            T,
            Ts...
        > {
            using result = _extract_keywords<
                I + 1,
                ::std::index_sequence<out...>,
                bertrand::string_list<out...>,
                Ts...
            >;
            static constexpr bool hashable = result::hashable;
            static constexpr auto value = result::value;
        };
        template <size_t I, size_t... Is, auto... out, typename T, typename... Ts>
            requires (meta::standard_arg<T> || meta::variadic_keyword<T>)
        struct _extract_keywords<
            I,
            ::std::index_sequence<Is...>,
            bertrand::string_list<out...>,
            T,
            Ts...
        > {
            using result = _extract_keywords<
                I + 1,
                ::std::index_sequence<out..., I>,
                bertrand::string_list<out..., meta::arg_name<T>>,
                Ts...
            >;
            static constexpr bool hashable = result::hashable;
            static constexpr auto value = result::value;
        };
        template <typename... Ts>
        using extract_keywords = _extract_keywords<
            0,
            ::std::index_sequence<>,
            bertrand::string_list<>,
            Ts...
        >;

    }

    template <typename T>
    concept args = detail::args<unqualify<T>>;

    template <typename... Ts>
    concept args_spec = ((
        source_args<Ts...> &&
        detail::extract_keywords<Ts...>::hashable
    ) && ... && convertible_to<Ts, meta::remove_rvalue<Ts>>);

    template <typename... Ts>
    concept nested_args = args_spec<Ts...> && (args<Ts> || ...);

}


/* Save a set of function arguments for later use.  Returns an `args` container, which
stores the arguments similar to a `std::tuple`, except that it is capable of storing
arbitrarily-qualified types, including references.  Invoking the args pack with an
input function will call that function using the stored arguments, maintaining their
original value categories merged with those of the pack itself: lvalue packs always
produce lvalue arguments, while rvalue packs perfectly forward.

Containers of this form are used to store bound arguments for partial functions, and
will be passed as inputs to functions that define an `"*args"_` or `"**kwargs"_`
parameter, with the same basic template serving both cases.  The CTAD constructor
allows multiple packs to be easily concatenated, and the pack itself can either be
queried for specific values (obeying the same perfect forwarding semantics) or used to
invoke a downstream function, in Pythonic fashion.

NOTE: in most implementations, the C++ standard does not strictly define the order of
evaluation for function arguments, which can lead to surprising behavior if the
arguments have side effects, or depend on each other in some way.  However, this
restriction is lifted in the case of class constructors and initializer lists, which
are guaranteed to evaluate from left to right.  This class can therefore be used to
exploit that loophole by storing the arguments in a pack and immediately consuming
them, without any additional overhead besides a possible move in and out of the
argument pack (which can be optimized out in many cases due to copy elision).

WARNING: undefined behavior may occur if an lvalue is bound that falls out of scope
before the pack is consumed.  Such values will not have their lifetimes extended by
this class in any way, and it is the user's responsibility to ensure that proper
reference semantics are observed at all times.  Generally speaking, ensuring that no
packs are returned out of a local context is enough to satisfy this guarantee.
Typically, this class will be consumed within the same context in which it was created
(or a downstream one where all of the objects are still in scope), as a way of
enforcing a certain order of operations.  Note that this guidance does not apply to
rvalues, which are stored directly within the pack, extending their lifetimes. */
template <typename... Ts> requires (meta::args_spec<Ts...>)
struct args;


template <typename... Ts> requires (meta::args_spec<Ts...>)
args(Ts&&...) -> args<Ts...>;


/* ADL `swap()` operator for `args{}` instances. */
template <typename... Ts>
constexpr void swap(args<Ts...>& a, args<Ts...>& b)
    noexcept (requires{{a.swap(b)} noexcept;})
    requires (requires{{a.swap(b)};})
{
    a.swap(b);
}


namespace impl {

    template <typename...>
    struct args {
        constexpr void swap(args& other) noexcept {}

        template <size_t I, typename S> requires (false)
        constexpr decltype(auto) get(this S&& s) noexcept;  // never called

        template <typename S, typename F, typename... A>
        constexpr decltype(auto) operator()(this S&&, F&& f, A&&... a)
            noexcept (meta::nothrow::invocable<F, A...>)
            requires (meta::invocable<F, A...>)
        {
            return (std::forward<F>(f)(std::forward<A>(a)...));
        }
    };

    template <typename T, typename... Ts>
    struct args<T, Ts...> : args<Ts...> {
    private:
        using type = meta::remove_rvalue<T>;
        struct storage { type value; };

    public:
        storage m_storage;

        constexpr args(T curr, Ts... rest)
            noexcept (
                meta::nothrow::convertible_to<T, type> &&
                meta::nothrow::constructible_from<args<Ts...>, Ts...>
            )
        :
            args<Ts...>(std::forward<Ts>(rest)...),
            m_storage(std::forward<T>(curr))
        {}

        constexpr args(args&& other) = default;
        constexpr args(const args&) = default;

        constexpr args& operator=(const args& other)
            noexcept (meta::nothrow::copy_assignable<args<Ts...>> && (
                meta::lvalue<T> || meta::nothrow::copy_assignable<T> || (
                    meta::nothrow::copyable<T> && meta::nothrow::swappable<T>
                )
            ))
            requires (meta::copy_assignable<args<Ts...>> && (
                meta::lvalue<T> || meta::copy_assignable<T> || (
                    meta::copyable<T> && meta::swappable<T>
                )
            ))
        {
            args<Ts...>::operator=(other);

            if constexpr (meta::lvalue<T>) {
                std::construct_at(&m_storage, other.m_storage);

            } else if constexpr (meta::copy_assignable<T>) {
                m_storage.value = other.m_storage.value;

            } else {
                args temp(other);
                swap(temp);
            }

            return *this;
        }

        constexpr args& operator=(args&& other)
            noexcept (meta::nothrow::move_assignable<args<Ts...>> && (
                meta::lvalue<T> || meta::nothrow::move_assignable<T> ||
                meta::nothrow::swappable<T>
            ))
            requires (meta::move_assignable<args<Ts...>> && (
                meta::lvalue<T> || meta::move_assignable<T> || meta::swappable<T>
            ))
        {
            args<Ts...>::operator=(std::move(other));

            if constexpr (meta::lvalue<T>) {
                std::construct_at(&m_storage, other.m_storage);

            } else if constexpr (meta::move_assignable<T>) {
                m_storage.value = std::move(other.m_storage.value);

            } else {
                swap(other);
            }

            return *this;
        }

        constexpr void swap(args& other)
            noexcept (meta::nothrow::swappable<args<Ts...>> && meta::nothrow::swappable<T>)
            requires (meta::swappable<args<Ts...>> && meta::swappable<T>)
        {
            args<Ts...>::swap(other);
            std::ranges::swap(m_storage.value, other.m_storage.value);
        }

        template <size_t I, typename S> requires (I <= sizeof...(Ts))
        constexpr decltype(auto) get(this S&& s) noexcept {
            if constexpr (I == 0) {
                return (std::forward<S>(s).m_storage.value);
            } else {
                return (std::forward<meta::qualify<args<Ts...>, S>>(s).template get<I - 1>());
            }
        }

        template <typename S, typename F, typename... A>
        constexpr decltype(auto) operator()(this S&& s, F&& f, A&&... a)
            noexcept (requires{{std::forward<meta::qualify<args<Ts...>, S>>(s)(
                std::forward<F>(f),
                std::forward<A>(a)...,
                std::forward<S>(s).m_storage.value
            )} noexcept;})
            requires (requires{{std::forward<meta::qualify<args<Ts...>, S>>(s)(
                std::forward<F>(f),
                std::forward<A>(a)...,
                std::forward<S>(s).m_storage.value
            )};})
        {
            return (std::forward<meta::qualify<args<Ts...>, S>>(s)(
                std::forward<F>(f),
                std::forward<A>(a)...,
                std::forward<S>(s).m_storage.value
            ));
        }
    };

    template <typename...>
    struct _flatten_args;
    template <typename... out>
    struct _flatten_args<meta::pack<out...>> { using type = bertrand::args<out...>; };
    template <typename... out, typename T, typename... Ts>
    struct _flatten_args<meta::pack<out...>, T, Ts...> {
        using type = _flatten_args<meta::pack<out..., T>, Ts...>::type;
    };
    template <typename... out, meta::args T, typename... Ts>
    struct _flatten_args<meta::pack<out...>, T, Ts...> {
        template <typename>
        struct _type;
        template <size_t... Is>
        struct _type<::std::index_sequence<Is...>> {
            using type = _flatten_args<
                meta::pack<out..., decltype(::std::declval<T>().template get<Is>())...>,
                Ts...
            >::type;
        };
        using type = _type<
            ::std::make_index_sequence<meta::unqualify<T>::size()>
        >::type;
    };
    template <typename... Ts>
    using flatten_args = _flatten_args<meta::pack<>, Ts...>::type;

}


template <typename... Ts> requires (meta::args_spec<Ts...>)
struct args : impl::args<Ts...> {
    /* A nested type listing the precise types that were used to build this parameter
    pack, for posterity.  These are reported as a `meta::pack<...>` type, which
    provides a number of compile-time utilities for inspecting the types, if needed. */
    using types = meta::pack<Ts...>;
    using size_type = size_t;
    using index_type = ssize_t;

private:
    using base = impl::args<Ts...>;

    template <typename T>
    struct convert {
        template <typename... A>
        [[nodiscard]] constexpr T operator()(A&&... args)
            noexcept (requires{{T{std::forward<A>(args)...}} noexcept;})
            requires (requires{{T{std::forward<A>(args)...}};})
        {
            return T{std::forward<A>(args)...};
        }
    };

    template <bertrand::slice::normalized indices>
    struct get_slice {
        template <typename Self, size_type... Is>
        [[nodiscard]] static constexpr auto operator()(
            Self&& self,
            std::index_sequence<Is...> = std::make_index_sequence<indices.length>{}
        )
            noexcept (requires{{
                bertrand::args{std::forward<meta::qualify<base, Self>>(self).template get<
                    indices.start + Is * indices.step
                >()...}
            } noexcept;})
            requires (requires{{
                bertrand::args{std::forward<meta::qualify<base, Self>>(self).template get<
                    indices.start + Is * indices.step
                >()...}
            };})
        {
            return bertrand::args{std::forward<meta::qualify<base, Self>>(self).template get<
                indices.start + Is * indices.step
            >()...};
        }
    };

public:
    /* A compile-time minimal perfect hash table storing the names of all keyword
    argument annotations that are present in `Ts...`. */
    static constexpr string_map names = meta::detail::extract_keywords<Ts...>::value;

    /* The number of arguments contained within the pack, as an unsigned integer. */
    [[nodiscard]] static constexpr size_type size() noexcept { return sizeof...(Ts); }

    /* The number of arguments contained within the pack, as a signed integer. */
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }

    /* True if the pack contains no arguments.  False otherwise. */
    [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }

    /* CTAD constructor saves a pack of arguments for later use, retaining proper
    lvalue/rvalue categories and cv qualifiers in the template signature.  If another
    pack is present in the arguments, then it will be automatically flattened, such
    that the result represents the concatenation of each pack.  This also means that
    argument packs can never be directly nested.  If this behavior is needed for some
    reason, it can be disabled by encapsulating the arguments in another type, which
    makes it inaccessible in deduction. */
    [[nodiscard]] constexpr args(Ts... args)
        noexcept (requires{{base(std::forward<Ts>(args)...)} noexcept;})
    :
        base(std::forward<Ts>(args)...)
    {}

    /* Copying an `args{}` container will copy all of its contents.  Note that lvalues
    are trivially copied, meaning the new container will reference the exact same
    objects as the original, and will not extend their lifetimes in any way. */
    [[nodiscard]] constexpr args(const args&) = default;

    /* Moving an `args{}` container will transfer all of its contents.  Note that
    lvalues are trivially moved, meaning the new container will reference the exact
    same objects as the original, and will not extend their lifetimes in any way. */
    [[nodiscard]] constexpr args(args&&) = default;

    /* Copy assigning to an `args{}` container will reassign all of its contents to
    copies of the invoming values.  Note that lvalues will be trivially rebound,
    meaning no changes will occur to the referenced data.  Instead, the reference
    address may change to reflect the assignment. */
    constexpr args& operator=(const args& other)
        noexcept (meta::nothrow::copy_assignable<base>)
        requires (meta::copy_assignable<base>)
    {
        if (&other != this) {
            base::operator=(other);
        }
        return *this;
    }

    /* Move assigning to an `args{}` container will reassign all of its contents to
    the incoming values.  Note that lvalues will be trivially rebound, meaning no
    changes will occur to the referenced data.  Instead, the reference address may
    change to reflect the assignment. */
    constexpr args& operator=(args&& other)
        noexcept (meta::nothrow::move_assignable<base>)
        requires (meta::move_assignable<base>)
    {
        if (&other != this) {
            base::operator=(std::move(other));
        }
        return *this;
    }

    /* STL-compatible `swap()` method for `args{}` instances. */ 
    constexpr void swap(args& other)
        noexcept (meta::nothrow::swappable<base>)
        requires (meta::swappable<base>)
    {
        if (&other != this) {
            base::swap(other);
        }
    }

    /* Check to see whether the argument pack contains a named keyword argument.  This
    overload applies when the argument name is known at compile time and can be
    statically resolved. */
    template <static_str name> requires (meta::valid_arg_name<name>)
    [[nodiscard]] static constexpr bool contains() noexcept
        requires (names.template contains<name>())
    {
        return names.template contains<name>();
    }

    /* Check to see whether the argument pack contains a named keyword argument.  This
    overload applies when the argument name is known at compile time, but cannot be
    statically resolved, and the pack permits runtime-only keyword arguments (e.g.
    the contents of a keyword unpacking operator), and the underlying container
    supports a matching `contains()` method. */
    template <static_str name> requires (meta::valid_arg_name<name>)
    [[nodiscard]] constexpr bool contains() const
        noexcept (requires{{get<names.template get<"">()>().template contains<name>()} noexcept;})
        requires (!names.template contains<name>() && names.template contains<"">())
    {
        return get<names.template get<"">()>().template contains<name>();
    }

    /* Get the argument at index I, perfectly forwarding it according to the pack's
    current cvref qualifications.  This means that if the pack is supplied as an
    lvalue, then all arguments will be forwarded as lvalues, regardless of their
    status in the template signature.  If the pack is an rvalue, then the arguments
    will be perfectly forwarded according to their original categories.  If the pack
    is cv qualified, then the result will be forwarded with those same qualifiers. */
    template <index_type I, typename Self> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
        return (std::forward<meta::qualify<base, Self>>(self).template get<
            impl::normalize_index<ssize(), I>()
        >());
    }

    /* Slice the argument pack at compile time, returning a new pack with only the
    included indices.  The stored values will be perfectly forwarded if possible,
    according to the cvref qualifications of the current pack. */
    template <bertrand::slice slice, typename Self>
    [[nodiscard]] constexpr auto get(this Self&& self)
        noexcept (requires{{get_slice<slice.normalize(ssize())>::operator()(
            std::forward<Self>(self)
        )} noexcept;})
        requires (requires{{get_slice<slice.normalize(ssize())>::operator()(
            std::forward<Self>(self)
        )};})
    {
        return get_slice<slice.normalize(ssize())>::operator()(
            std::forward<Self>(self)
        );
    }

    /* Get a keyword argument with a particular name, perfectly forwarding it according
    to the pack's current cvref qualifications.  This means that if the pack is
    supplied as an lvalue, then all arguments will be forwarded as lvalues, regardless
    of their status in the template signature.  If the pack is an rvalue, then the
    arguments will be perfectly forwarded according to their original categories.  If
    the pack is cv qualified, then the result will be forwarded with those same
    qualifiers.

    This overload applies when the argument name can be statically resolved, and the
    entire lookup can be optimized away at compile time. */
    template <static_str name, typename Self> requires (meta::valid_arg_name<name>)
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{
            std::forward<meta::qualify<base, Self>>(
                self
            ).template get<names.template get<name>()>().value()
        } noexcept;})
        requires (names.template contains<name>() && requires{{
            std::forward<meta::qualify<base, Self>>(
                self
            ).template get<names.template get<name>()>().value()
        };})
    {
        return (std::forward<meta::qualify<base, Self>>(
            self
        ).template get<names.template get<name>()>().value());
    }

    /* Get a keyword argument with a particular name, perfectly forwarding it according
    to the pack's current cvref qualifications.  This means that if the pack is
    supplied as an lvalue, then all arguments will be forwarded as lvalues, regardless
    of their status in the template signature.  If the pack is an rvalue, then the
    arguments will be perfectly forwarded according to their original categories.  If
    the pack is cv qualified, then the result will be forwarded with those same
    qualifiers.

    This overload applies when the argument name cannot be statically resolved, which
    occurs when the pack contains runtime-only keyword arguments (e.g. the contents
    of a keyword unpacking operator).  In this case, the underlying container for the
    runtime arguments will be subscripted with the given name, and may throw an
    exception if the argument is not found, though the exact behavior is dependent on
    the container itself.  The keyword unpacking operator ensures that an operator of
    this form is available and returns an appropriate value type, but does not specify
    any other behavior. */
    template <static_str name, typename Self> requires (meta::valid_arg_name<name>)
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{
            std::forward<meta::qualify<base, Self>>(
                self
            ).template get<names.template get<"">()>().template get<name>()
        } noexcept;})
        requires (!names.template contains<name>() && names.template contains<"">())
    {
        return (std::forward<meta::qualify<base, Self>>(
            self
        ).template get<names.template get<"">()>().template get<name>());
    }

    /* Get a keyword argument with a particular name, if it is present in the argument
    pack.  If available, a method of this form may be cheaper than separate
    `contains()` and `get()` calls, since it fuses the lookups.

    This overload applies when the argument name can be statically resolved, in which
    case this method is always valid and returns a `bertrand::Optional` that holds the
    perfectly-forwarded result.  The optional will never be in the empty state in this
    case; it is provided as an optional for consistency with the runtime case. */
    template <static_str name, typename Self> requires (meta::valid_arg_name<name>)
    [[nodiscard]] constexpr auto get_if(this Self&& self)
        noexcept (requires{{
            Optional{std::forward<meta::qualify<base, Self>>(
                self
            ).template get<names.template index<name>()>().value()}
        } noexcept;})
        requires (names.template contains<name>() && requires{{
            Optional{std::forward<meta::qualify<base, Self>>(
                self
            ).template get<names.template index<name>()>().value()}
        };})
    {
        return Optional{std::forward<meta::qualify<base, Self>>(
            self
        ).template get<names.template index<name>()>().value()};
    }

    /* Get a keyword argument with a particular name, if it is present in the argument
    pack.  If available, a method of this form may be cheaper than separate
    `contains()` and `get()` calls, since it fuses the lookups.

    This overload applies when the argument name cannot be statically resolved, and the
    argument pack permits runtime-only keyword arguments (e.g. the contents of a
    keyword unpacking operator), and the underlying container supports a matching
    `get_if()` method.  In this case, the return type will match that of the
    underlying container, which may be an `Optional`, `std::optional`, or any other
    type. */
    template <static_str name, typename Self> requires (meta::valid_arg_name<name>)
    [[nodiscard]] constexpr decltype(auto) get_if(this Self&& self)
        noexcept (requires{{
            std::forward<meta::qualify<base, Self>>(
                self
            ).template get<names.template get<"">()>().template get_if<name>()
        } noexcept;})
        requires (!names.template contains<name>() && names.template contains<"">())
    {
        return (std::forward<meta::qualify<base, Self>>(
            self
        ).template get<names.template get<"">()>().template get_if<name>());
    }

    /* Invoking a pack will perfectly forward the saved arguments to a target function
    according to the pack's current cvref qualifications.  This means that if the pack
    is supplied as an lvalue, then all arguments will be forwarded as lvalues,
    regardless of their status in the template signature.  If the pack is an rvalue,
    then the arguments will be perfectly forwarded according to their original
    categories.  If the pack is cv qualified, then the arguments will be forwarded with
    those same qualifiers. */
    template <typename Self, typename F>
    constexpr decltype(auto) operator()(this Self&& self, F&& f)
        noexcept (requires{
            {std::forward<meta::qualify<base, Self>>(self)(std::forward<F>(f))} noexcept;
        })
        requires (requires{{std::forward<meta::qualify<base, Self>>(self)(std::forward<F>(f))};})
    {
        return (std::forward<meta::qualify<base, Self>>(self)(std::forward<F>(f)));
    }

    /* Implicitly convert the `args` pack to any container that can be constructed by
    forwarding the arguments within the pack.  When used in combination with a variadic
    positional or keyword argument, an operator of this form allows the user to
    customize the storage interface for the arguments, like so:
    
        ```
        constexpr auto func = def<"*args"_[^int]>([](std::array<int, 3> args) {
            return args[1];
        });

        static_assert(func(1, 2, 3) == 2);  // fine, implicit conversion is allowed
        // static_assert(func(1, 2, 3, 4) == 2);  <- ill-formed, since the function
        //                                           expects exactly 3 variadic arguments
        ```

    Note that such a conversion is not strictly required, and if omitted, the `args`
    pack will simply be forwarded directly.  This can be more efficient for basic
    usage, but if the conversion would have been required anyways, then doing it in the
    signature as shown is by far the most expressive. */
    template <typename T, typename Self>
    [[nodiscard]] constexpr operator T(this Self&& self)
        noexcept (requires{{std::forward<Self>(self)(convert<T>{})} noexcept;})
        requires (requires{{std::forward<Self>(self)(convert<T>{})};})
    {
        return std::forward<Self>(self)(convert<T>{});
    }
};


template <typename... Ts> requires (meta::nested_args<Ts...>)
struct args<Ts...> : impl::flatten_args<Ts...> {
private:
    using base = impl::flatten_args<Ts...>;

    /* When the argument list is complete, use it to initialize the parent class. */
    template <size_t I, typename... As> requires (I >= sizeof...(As))
    static constexpr base flatten(As&&... args)
        noexcept (meta::nothrow::constructible_from<base, As...>)
    {
        return {std::forward<As>(args)...};
    }

    /* Arguments other than nested arg packs get forwarded as-is. */
    template <size_t I, typename... As>
        requires (I < sizeof...(As) && !meta::args<meta::unpack_type<I, As...>>)
    static constexpr base flatten(As&&... args)
        noexcept (requires{{flatten<I + 1>(std::forward<As>(args)...)} noexcept;})
    {
        return flatten<I + 1>(std::forward<As>(args)...);
    }

    /* Helper expands the pack into the argument list in-place, and then proceeds to
    the next argument. */
    template <size_t... prev, size_t... curr, size_t... next, typename... As>
    static constexpr base _flatten(
        std::index_sequence<prev...>,
        std::index_sequence<curr...>,
        std::index_sequence<next...>,
        As&&... args
    )
        noexcept (requires{{flatten<sizeof...(prev) + sizeof...(curr)>(
            meta::unpack_arg<prev>(std::forward<As>(args)...)...,
            meta::unpack_arg<sizeof...(prev)>(
                std::forward<As>(args)...
            ).template get<curr>()...,
            meta::unpack_arg<sizeof...(prev) + 1 + next>(std::forward<As>(args)...)...
        )} noexcept;})
    {
        return flatten<sizeof...(prev) + sizeof...(curr)>(
            meta::unpack_arg<prev>(std::forward<As>(args)...)...,
            meta::unpack_arg<sizeof...(prev)>(
                std::forward<As>(args)...
            ).template get<curr>()...,
            meta::unpack_arg<sizeof...(prev) + 1 + next>(std::forward<As>(args)...)...
        );
    }

    /* Argument packs are flattened into their constituent types. */
    template <size_t I, typename... As>
        requires (I < sizeof...(As) && meta::args<meta::unpack_type<I, As...>>)
    static constexpr base flatten(As&&... args)
        noexcept (requires{{_flatten(
            std::make_index_sequence<I>(),
            std::make_index_sequence<meta::unqualify<meta::unpack_type<I, As...>>::size()>(),
            std::make_index_sequence<sizeof...(As) - (I + 1)>(),
            std::forward<As>(args)...
        )} noexcept;})
    {
        return _flatten(
            std::make_index_sequence<I>(),
            std::make_index_sequence<meta::unqualify<meta::unpack_type<I, As...>>::size()>(),
            std::make_index_sequence<sizeof...(As) - (I + 1)>(),
            std::forward<As>(args)...
        );
    }

public:
    /* CTAD Constructor saves a pack of arguments for later use, retaining proper
    lvalue/rvalue categories and cv qualifiers in the template signature.  If another
    pack is present in the arguments, then it will be automatically flattened, such
    that the result represents the concatenation of each pack.  This also means that
    argument packs can never be directly nested.  If this behavior is needed for some
    reason, it can be disabled by encapsulating the arguments in another type, making
    it inaccessible in deduction. */
    template <typename... As>
    constexpr args(As&&... args)
        noexcept (requires{{base(flatten<0>(std::forward<As>(args)...))} noexcept;})
    :
        base(flatten<0>(std::forward<As>(args)...))
    {}
};


//////////////////////////////
////    COMPREHENSIONS    ////
//////////////////////////////



namespace meta {

    namespace detail {

        template <typename T>
        constexpr bool unpackable = true;

        template <typename T>
        constexpr bool unpack_view = false;

        template <typename T>
        constexpr bool comprehension = false;

        template <typename T>
        constexpr bool use_structured_comprehension = false;
        template <meta::iterable T>
        constexpr bool use_structured_comprehension<T> =
            !meta::iterable<meta::yield_type<T>> && meta::tuple_like<meta::yield_type<T>>;

        template <typename T>
        constexpr bool use_standard_comprehension = false;
        template <meta::iterable T>
        constexpr bool use_standard_comprehension<T> =
            meta::iterable<meta::yield_type<T>> || !meta::tuple_like<meta::yield_type<T>>;

        template <typename T>
        constexpr bool use_direct_comprehension = !meta::iterable<T> && meta::tuple_like<T>;

        template <typename F, typename T, typename>
        constexpr bool _decompose = false;
        template <typename F, meta::tuple_like T, size_t... Is>
            requires (meta::tuple_size<T> == sizeof...(Is))
        constexpr bool _decompose<F, T, ::std::index_sequence<Is...>> =
            meta::invocable<F, meta::get_type<T, Is>...>;
        template <typename F, meta::tuple_like T>
        constexpr bool decompose =
            _decompose<meta::as_lvalue<F>, T, ::std::make_index_sequence<meta::tuple_size<T>>>;

    }

    template <typename T>
    concept unpackable =
        (tuple_like<T> || (iterable<T> && has_size<T>)) && detail::unpackable<unqualify<T>>;

    template <typename T>
    concept unpack_view = detail::unpack_view<unqualify<T>>;

    template <typename T>
    concept comprehension = detail::comprehension<unqualify<T>>;

    template <typename T>
    concept iterable_comprehension = comprehension<T> && iterable<T>;

    template <typename T>
    concept tuple_comprehension = comprehension<T> && tuple_like<T>;

    template <typename F, typename T>
    concept comprehension_func =
        (detail::use_structured_comprehension<T> && detail::decompose<F, meta::yield_type<T>>) ||
        (detail::use_standard_comprehension<T> && meta::invocable<F, meta::yield_type<T>>) ||
        (detail::use_direct_comprehension<T> && detail::decompose<F, T>);

    namespace detail {

        template <typename T>
        concept kwarg_like =
            meta::mapping_like<T> &&
            meta::has_size<T> &&
            meta::convertible_to<typename meta::unqualify<T>::key_type, ::std::string>;

        template <typename T>
        concept kwarg_yield = kwarg_like<T> && (
            meta::iterable<T> &&
            meta::structured_with<
                meta::yield_type<T>,
                typename meta::unqualify<T>::key_type,
                typename meta::unqualify<T>::mapped_type
            >
        );

        template <typename T>
        concept kwarg_items = kwarg_like<T> && (
            meta::has_items<meta::items_type<T>> &&
            meta::structured_with<
                meta::items_type<T>,
                typename meta::unqualify<T>::key_type,
                typename meta::unqualify<T>::mapped_type
            >
        );

        template <typename T>
        concept kwarg_keys_and_values = kwarg_like<T> && (
            meta::has_keys<T> &&
            meta::has_values<T> &&
            meta::yields<meta::keys_type<T>, typename meta::unqualify<T>::key_type> &&
            meta::yields<meta::values_type<T>, typename meta::unqualify<T>::mapped_type>
        );

        template <typename T>
        concept kwarg_yield_and_lookup = kwarg_like<T> && (
            meta::yields<T, typename meta::unqualify<T>::key_type> &&
            meta::index_returns<
                typename meta::unqualify<T>::mapped_type,
                T,
                meta::yield_type<T>
            >
        );

        template <typename T>
        concept kwarg_keys_and_lookup = kwarg_like<T> && (
            meta::has_keys<T> &&
            meta::yields<meta::keys_type<T>, typename meta::unqualify<T>::key_type> &&
            meta::index_returns<
                typename meta::unqualify<T>::mapped_type,
                T,
                meta::yield_type<meta::keys_type<T>>
            >
        );

    }

    template <typename T>
    concept unpack_to_kwargs = detail::kwarg_like<T> && (
        detail::kwarg_yield<T> ||
        detail::kwarg_items<T> ||
        detail::kwarg_keys_and_values<T> ||
        detail::kwarg_yield_and_lookup<T> ||
        detail::kwarg_keys_and_lookup<T>
    );

}


namespace impl {

    /// TODO: kwarg_pack -> unpack_kwargs or just kwargs

    /* A keyword parameter pack obtained by double-dereferencing a mapping-like
    container within a Python-style function call.  The template machinery recognizes
    this as if it were an `"*<anonymous>"_(container)` argument defined in the
    signature, if such an expression were well-formed.  Because that is outlawed by the
    DSL syntax and cannot appear at the call site, this class fills that gap and allows
    the same internals to be reused.  */
    template <meta::unpack_to_kwargs T>
    struct kwarg_pack {
        using key_type = meta::unqualify<T>::key_type;
        using mapped_type = meta::unqualify<T>::mapped_type;
        using type = mapped_type;
        static constexpr static_str id = "";

    private:
        struct hash {
            using is_transparent = void;
            static constexpr size_t operator()(std::string_view str)
                noexcept (requires{{bertrand::hash(str)} noexcept;})
            {
                return bertrand::hash(str);
            }
        };

        struct equal {
            using is_transparent = void;
            static constexpr bool operator()(
                std::string_view lhs,
                std::string_view rhs
            ) noexcept (requires{{lhs == rhs} noexcept;}) {
                return lhs == rhs;
            }
        };

        /// TODO: eventually, I should try to replace the `unordered_map` with a
        /// `Map<...>` type under my control for consistency.

        using Map = std::unordered_map<std::string, mapped_type, hash, equal>;
        Map m_data;

    public:
        template <meta::is<T> C>
        explicit constexpr kwarg_pack(C&& container) {
            if constexpr (meta::has_size<C>) {
                m_data.reserve(std::ranges::size(container));
            }
            if constexpr (meta::detail::kwarg_yield<C>) {
                for (auto&& [key, value] : std::forward<C>(container)) {
                    m_data.emplace(
                        std::forward<decltype(key)>(key),
                        std::forward<decltype(value)>(value)
                    );
                }

            } else if constexpr (meta::detail::kwarg_items<C>) {
                for (auto&& [key, value] : std::forward<C>(container).items()) {
                    m_data.emplace(
                        std::forward<decltype(key)>(key),
                        std::forward<decltype(value)>(value)
                    );
                }

            } else if constexpr (meta::detail::kwarg_keys_and_values<C>) {
                for (auto&& [key, value] : zip(container.keys(), container.values())) {
                    m_data.emplace(
                        std::forward<decltype(key)>(key),
                        std::forward<decltype(value)>(value)
                    );
                }

            } else if constexpr (meta::detail::kwarg_yield_and_lookup<C>) {
                for (auto& key : container) {
                    m_data.emplace(key, container[key]);
                }

            } else {
                for (auto& key : container.keys()) {
                    m_data.emplace(key, container[key]);
                }
            }
        }

        constexpr void swap(kwarg_pack& other)
            noexcept (requires{{std::ranges::swap(m_data, other.m_data)} noexcept;})
            requires (requires{{std::ranges::swap(m_data, other.m_data)};})
        {
            std::ranges::swap(m_data, other.m_data);
        }

        [[nodiscard]] constexpr auto& data() noexcept { return m_data; }
        [[nodiscard]] constexpr const auto& data() const noexcept { return m_data; }
        [[nodiscard]] constexpr auto size() const noexcept { return m_data.size(); }
        [[nodiscard]] constexpr auto ssize() const noexcept { return std::ranges::ssize(m_data); }
        [[nodiscard]] constexpr bool empty() const noexcept { return m_data.empty(); }
        [[nodiscard]] constexpr auto begin() noexcept { return m_data.begin(); }
        [[nodiscard]] constexpr auto begin() const noexcept { return m_data.begin(); }
        [[nodiscard]] constexpr auto end() noexcept { return m_data.end(); }
        [[nodiscard]] constexpr auto end() const noexcept { return m_data.end(); }

        template <static_str key>
        [[nodiscard]] constexpr Optional<mapped_type> pop() {
            auto it = m_data.find(key);
            if (it == m_data.end()) {
                return None;
            }
            Optional<mapped_type> result{std::move(it->second)};
            m_data.erase(it);
            return result;
        }

        constexpr void validate() {
            if (!empty()) {
                if consteval {
                    static constexpr static_str msg = "unexpected keyword arguments";
                    throw TypeError(msg);
                } else {
                    auto it = m_data.begin();
                    auto end = m_data.end();
                    std::string message = "unexpected keyword arguments: ['";
                    message += repr(it->first);
                    while (++it != end) {
                        message += "', '" + repr(it->first);
                    }
                    message += "']";
                    throw TypeError(message);
                }
            }
        }

        template <static_str key>
        [[nodiscard]] constexpr bool contains() const
            noexcept (requires{{m_data.contains(key)} noexcept;})
        {
            return m_data.contains(key);
        }

        template <static_str key>
        [[nodiscard]] constexpr auto& get() {
            auto it = m_data.find(key);
            if (it == m_data.end()) {
                static constexpr static_str msg =
                    "missing keyword argument: '" + key + "'";
                throw KeyError(msg);
            }
            return it->second;
        }

        template <static_str key>
        [[nodiscard]] constexpr const auto& get() const {
            auto it = m_data.find(key);
            if (it == m_data.end()) {
                static constexpr static_str msg =
                    "missing keyword argument: '" + key + "'";
                throw KeyError(msg);
            }
            return it->second;
        }

        template <static_str key>
        [[nodiscard]] constexpr Optional<meta::as_lvalue<mapped_type>> get_if()
            noexcept (requires{
                {m_data.find(key) == m_data.end()} noexcept;
                {Optional<meta::as_lvalue<mapped_type>>{m_data.find(key)->second}} noexcept;
            })
        {
            auto it = m_data.find(key);
            if (it == m_data.end()) {
                return None;
            }
            return {it->second};
        }

        template <static_str key>
        [[nodiscard]] constexpr Optional<meta::as_const_ref<mapped_type>> get_if() const
            noexcept (requires{
                {m_data.find(key) == m_data.end()} noexcept;
                {Optional<meta::as_const_ref<mapped_type>>{m_data.find(key)->second}} noexcept;
            })
        {
            auto it = m_data.find(key);
            if (it == m_data.end()) {
                return None;
            }
            return {it->second};
        }
    };

    template <meta::unpack_to_kwargs T>
    kwarg_pack(T&&) -> kwarg_pack<T>;

    template <typename T>
    struct view_size_type { using type = meta::unqualify<decltype(meta::tuple_size<T>)>; };
    template <meta::has_size T>
    struct view_size_type<T> { using type = meta::size_type<T>; };

    template <typename F, typename args, typename its, typename Is, typename... A>
    struct _invoke_comprehension { static constexpr bool enable = false; };
    template <
        typename F,
        typename... args,
        typename... its,
        size_t... Is
    > requires (sizeof...(its) > 0 && meta::invocable<meta::as_lvalue<F>, args...>)
    struct _invoke_comprehension<
        F,
        meta::pack<args...>,
        meta::pack<its...>,
        std::index_sequence<Is...>
    > {
        using type = meta::invoke_type<meta::as_lvalue<F>, args...>;
        static constexpr bool enable = meta::not_void<type>;
        static constexpr size_t N = sizeof...(its);
        using storage = impl::args<its...>;
        using indices = std::index_sequence<Is...>;
    };
    template <
        typename F,
        typename... out,
        typename... its,
        size_t... Is,
        typename A,
        typename... As
    > requires (meta::iterable_comprehension<A>)
    struct _invoke_comprehension<
        F,
        meta::pack<out...>,
        meta::pack<its...>,
        std::index_sequence<Is...>,
        A,
        As...
    > : _invoke_comprehension<
        F,
        meta::pack<out..., meta::yield_type<A>>,
        meta::pack<its..., meta::begin_type<A>>,
        std::index_sequence<Is..., sizeof...(out)>,
        As...
    > {};
    template <
        typename F,
        typename... out,
        typename... its,
        size_t... Is,
        typename A,
        typename... As
    > requires (!meta::iterable_comprehension<A>)
    struct _invoke_comprehension<
        F,
        meta::pack<out...>,
        meta::pack<its...>,
        std::index_sequence<Is...>,
        A,
        As...
    > : _invoke_comprehension<
        F,
        meta::pack<out..., meta::as_lvalue<A>>,
        meta::pack<its...>,
        std::index_sequence<Is...>,
        As...
    >
    {};
    template <typename F, typename... A>
    using invoke_comprehension = _invoke_comprehension<
        F,
        meta::pack<>,
        meta::pack<>,
        std::index_sequence<>,
        A...
    >;
    template <typename F, typename... A>
    concept valid_comprehension = invoke_comprehension<F, A...>::enable;

    template <size_t, typename...>
    constexpr size_t comprehension_idx = 0;
    template <size_t I, typename A, typename... Args>
        requires (!meta::iterable_comprehension<A> && I > 0)
    constexpr size_t comprehension_idx<I, A, Args...> = comprehension_idx<I - 1, Args...>;
    template <size_t I, typename A, typename... Args>
        requires (meta::iterable_comprehension<A> && I > 0)
    constexpr size_t comprehension_idx<I, A, Args...> = comprehension_idx<I - 1, Args...> + 1;


}


/// TODO: meta::viewable should only check for iterable/tuple-like, and the extra
/// detail:: control point should only apply to the dereference operator.


/// TODO: maybe the indexing operator should take another view of booleans, and
/// filter the view to only those that return true.  In the background, this would
/// simply equate to a comprehension that returns an optional, relying on the
/// flattening logic to remove the empty values, or using a special iterator that
/// could have better performance than a naive comprehension.


/// TODO: unpack operator should be swappable and assignable?  Or the assignment
/// operator could act as an output iterator.  That's probably the best.  Together
/// with the above:

/*
    std::array mask {true, false, true}
    (*c)[*mask] = 2;
*/


/// TODO: maybe tuple unpack() proxies store the underlying tuple as an optional,
/// in order to allow iteration, even if it only yields a single element.




/* Base comprehension type, which is returned by the `*` operator for an iterable
or tuple-like container.

This is just a basic interface for iteration and tuple-like decomposition, which
can be interpreted as either an argument pack or SIMD-style expression template,
depending on where the operator was applied.  When provided as an argument to a
`def()` function, the view will be unpacked into the function's argument list.
Otherwise, it exposes all the elementary math operators for SIMD-style chains, as
well as structured comprehensions via the `->*` operator.

Applying the unary `*` operator to a view promotes it to a keyword argument pack,
assuming the underlying container can be viewed as such. */
template <meta::unpackable C>
struct unpack {
    using size_type = impl::view_size_type<C>::type;
    using index_type = meta::as_signed<size_type>;
    using container_type = meta::unqualify<C>;
    using value_type = meta::yield_type<C>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const_ref<value_type>;
    using pointer = meta::as_pointer<value_type>;
    using const_pointer = meta::as_pointer<meta::as_const<value_type>>;
    using iterator = meta::begin_type<C>;
    using const_iterator = meta::begin_type<meta::as_const<C>>;

    /// TODO: id is an empty string?  Or just handle this within the concepts
    /// directly.

    meta::remove_rvalue<C> container;

    /* If the underlying container is tuple-like, then a corresponding `get()` method
    will be exposed on the unpack proxy. */
    template <size_t I, typename Self> requires (meta::tuple_like<C>)
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{
            {meta::tuple_get<I>(std::forward<Self>(self).container)} noexcept;
        })
        requires (requires{
            {meta::tuple_get<I>(std::forward<Self>(self).container)};
        })
    {
        return (meta::tuple_get<I>(std::forward<Self>(self).container));
    }

    /* All unpack proxies have a definite size, which may be known at compile time if
    the underlying container is tuple-like. */
    [[nodiscard]] static constexpr size_type size() noexcept
        requires (meta::tuple_like<C>)
    {
        return meta::tuple_size<C>;
    }

    /* All unpack proxies have a definite size, which may be known at compile time if
    the underlying container is tuple-like. */
    [[nodiscard]] constexpr size_type size() const
        noexcept (requires{{std::ranges::size(container)} noexcept;})
        requires (!meta::tuple_like<C> && requires{{std::ranges::size(container)};})
    {
        return std::ranges::size(container);
    }

    /* The proxy's size, as a signed integer. */
    [[nodiscard]] static constexpr index_type ssize() noexcept
        requires (meta::tuple_like<C>)
    {
        return index_type(meta::tuple_size<C>);
    }

    /* The proxy's size, as a signed integer. */
    [[nodiscard]] constexpr index_type ssize() const
        noexcept (requires{{std::ranges::ssize(container)} noexcept;})
        requires (!meta::tuple_like<C> && requires{{std::ranges::ssize(container)};})
    {
        return std::ranges::ssize(container);
    }

    /* `true` if the underlying container is empty, `false` otherwise. */
    [[nodiscard]] static constexpr bool empty() noexcept
        requires (meta::tuple_like<C>)
    {
        return meta::tuple_size<C> == 0;
    }

    /* `true` if the underlying container is empty, `false` otherwise. */
    [[nodiscard]] constexpr bool empty() const
        noexcept (requires{{std::ranges::empty(container)} noexcept;})
        requires (!meta::tuple_like<C> && requires{{std::ranges::empty(container)};})
    {
        return std::ranges::empty(container);
    }

    /* Forward the underlying container's `begin()` iterator, if it has one. */
    [[nodiscard]] constexpr auto begin()
        noexcept (requires{{std::ranges::begin(container)} noexcept;})
        requires (requires{{std::ranges::begin(container)};})
    {
        return std::ranges::begin(container);
    }

    /* Forward the underlying container's `begin()` iterator, if it has one. */
    [[nodiscard]] constexpr auto begin() const
        noexcept (requires{{std::ranges::begin(container)} noexcept;})
        requires (requires{{std::ranges::begin(container)};})
    {
        return std::ranges::begin(container);
    }

    /* Forward the underlying container's `cbegin()` iterator, if it has one. */
    [[nodiscard]] constexpr auto cbegin() const
        noexcept (requires{{std::ranges::cbegin(container)} noexcept;})
        requires (requires{{std::ranges::cbegin(container)};})
    {
        return std::ranges::cbegin(container);
    }

    /* Forward the underlying container's `end()` iterator, if it has one. */
    [[nodiscard]] constexpr auto end()
        noexcept (requires{{std::ranges::end(container)} noexcept;})
        requires (requires{{std::ranges::end(container)};})
    {
        return std::ranges::end(container);
    }

    /* Forward the underlying container's `end()` iterator, if it has one. */
    [[nodiscard]] constexpr auto end() const
        noexcept (requires{{std::ranges::end(container)} noexcept;})
        requires (requires{{std::ranges::end(container)};})
    {
        return std::ranges::end(container);
    }

    /* Forward the underlying container's `cend()` iterator, if it has one. */
    [[nodiscard]] constexpr auto cend() const
        noexcept (requires{{std::ranges::cend(container)} noexcept;})
        requires (requires{{std::ranges::cend(container)};})
    {
        return std::ranges::cend(container);
    }

    /* Implicitly convert the unpack proxy to any container type that has a suitable
    `std::from_range` constructor. */
    template <typename V>
    [[nodiscard]] constexpr operator V() const
        noexcept (requires{{V(std::from_range, *this)} noexcept;})
        requires (requires{{V(std::from_range, *this)};})
    {
        return V(std::from_range, *this);
    }

    /* Implicitly convert the unpack proxy to any container type that has a suitable
    constructor taking a `begin()`/`end()` iterator pair. */
    template <typename V>
    [[nodiscard]] constexpr operator V() const
        noexcept (requires{{V(begin(), end())} noexcept;})
        requires (!requires{{V(std::from_range, *this)};} && requires{{V(begin(), end())};})
    {
        return V(begin(), end());
    }

    /// TODO: next() and validate() are only used within the function call logic,
    /// and only in the runtime case.  It might be best to move them into a
    /// temporary container that is only used within the binding logic.

    /// TODO: next() is unsafe, and should be made into a private helper, along
    /// with validate().  That would also help with the effort to make this into an
    /// entry point for comprehensions, since the user might start interacting
    /// with these types with some regularity.

    // [[nodiscard]] constexpr decltype(auto) next() noexcept {
    //     decltype(auto) value = *m_begin;
    //     ++m_begin;
    //     return value;
    // }

    // void constexpr validate() {
    //     if (!empty()) {
    //         if consteval {
    //             static constexpr static_str msg =
    //                 "too many arguments in positional parameter pack";
    //             throw TypeError(msg);
    //         } else {
    //             std::string message =
    //                 "too many arguments in positional parameter pack: ['" +
    //                 repr(*m_begin);
    //             while (++m_begin != m_end) {
    //                 message += "', '" + repr(*m_begin);
    //             }
    //             message += "']";
    //             throw TypeError(message);
    //         }
    //     }
    // }

    /* Dereference a positional pack to promote it to a key/value pack.  This
    allows Python-style double unpack operators to be used with supported
    containers when calling `def` statements, e.g.:

        ```
        auto func = def<"**kwargs"_[type<int>]>([](auto&& kwargs) {
            std::cout << kwargs["a"] << ", " << kwargs["b"] << '\n';
            return Map{std::forward<decltype(kwargs)>(kwargs)};
        })

        Map<std::string, int> in {{"a", 1}, {"b", 2}};
        Map out = func(**in);  // prints "1, 2"
        assert(in == out);
        ```

    Enabling this operator must be done explicitly, by specializing the
    `meta::detail::unpack<T>` flag for a given container. */
    [[nodiscard]] constexpr auto operator*() &&
        noexcept (requires{{impl::kwarg_pack<C>{std::forward<C>(container)}} noexcept;})
        requires (
            meta::unpack_to_kwargs<C> &&
            requires{{impl::kwarg_pack<C>{std::forward<C>(container)}};}
        )
    {
        return impl::kwarg_pack<C>{std::forward<C>(container)};
    }
};


/* CTAD constructor provides an explicit alternative to the unary `*` operator for
container unpacking and expression templates in C++, which is less likely to conflict
with other uses of the `*` operator, at the expense of verbosity and Python symmetry. */
template <typename C>
unpack(C&&) -> unpack<C>;


/// TODO: if a comprehension function returns another comprehension, then it
/// should be flattened into the resulting iterator.  This allows filtering and
/// expansion within a nested comprehension, just like Python's nested for and
/// if comprehensions.


/* A composition of one or more iterable `view`s and/or nested comprehensions which
applies a function `F` elementwise over the zipped contents.  The resulting
comprehension can be iterated over to execute the function, or implicitly
converted to any container that has a suitable `std::from_range` or
`begin()`/`end()` constructor.  Additionally, all comprehensions support SIMD-style
elementary math operators with respect to other comprehensions or views, which
return nested comprehensions that act as expression templates, fusing all
operations into a single loop over the result.

Note that if views of different sizes are provided, then the comprehension's
final size will be the minimum of all inputs.  If non-view or comprehension
arguments are provided, they will be broadcasted to that size.  If the function
is not callable with the elementwise argument types, then the comprehension will
fail to compile. */
template <typename F, typename... A> requires (impl::valid_comprehension<F, A...>)
struct comprehension {
    using size_type = size_t;
    using index_type = ssize_t;
    using function_type = meta::unqualify<F>;
    using value_type = impl::invoke_comprehension<F, A...>::type;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const_ref<value_type>;
    using pointer = meta::as_pointer<value_type>;
    using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

private:
    static constexpr size_type N = impl::invoke_comprehension<F, A...>::N;
    using indices = impl::invoke_comprehension<F, A...>::indices;

    template <typename T>
    constexpr void get_length(T&&) noexcept {}
    template <meta::iterable_comprehension T>
    constexpr void get_length(T&& v)
        noexcept (requires{{std::forward<T>(v).size()} noexcept;})
    {
        if (auto n = std::forward<T>(v).size(); n < length) {
            length = n;
        }
    }

public:
    size_type length = std::numeric_limits<size_type>::max();
    meta::remove_rvalue<F> func;
    bertrand::args<A...> args;

    [[nodiscard]] constexpr comprehension(F func, A... args)
        noexcept (requires{
            {meta::remove_rvalue<F>(std::forward<F>(func))} noexcept;
            {bertrand::args<A...>(std::forward<A>(args)...)} noexcept;
            {(get_length(std::forward<A>(args)), ...)} noexcept;
        })
        requires (requires{
            {meta::remove_rvalue<F>(std::forward<F>(func))};
            {bertrand::args<A...>(std::forward<A>(args)...)};
            {(get_length(std::forward<A>(args)), ...)};
        })
    :
        func(std::forward<F>(func)),
        args(std::forward<A>(args)...)
    {
        (get_length(std::forward<A>(args)), ...);
    }

    [[nodiscard]] constexpr size_type size() const noexcept { return length; }
    [[nodiscard]] constexpr index_type ssize() const noexcept { return index_type(size()); }
    [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }

    /* A forward iterator that evaluates the comprehension for each element. */
    struct iterator {
        using storage_type = impl::invoke_comprehension<F, A...>::storage;
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = comprehension::value_type;
        using reference = comprehension::reference;
        using pointer = comprehension::pointer;

        comprehension* self;
        size_type length;
        storage_type iterators;

    private:
        template <typename S, size_type I>
        constexpr decltype(auto) get(this S&& s)
            noexcept (meta::iterable_comprehension<meta::unpack_type<I, A...>> ?
                requires{
                    {*s.iterators.template get<impl::comprehension_idx<I, A...>>()} noexcept;
                } : requires{
                    {s.self->args.template get<I>()} noexcept;
                }
            )
        {
            if constexpr (meta::iterable_comprehension<meta::unpack_type<I, A...>>) {
                return (*s.iterators.template get<impl::comprehension_idx<I, A...>>());
            } else {
                return (s.self->args.template get<I>());
            }
        }

        template <typename S, size_type... Is>
        constexpr decltype(auto) call(this S&& s, std::index_sequence<Is...>)
            noexcept (requires{{s.self->func(s.template get<Is>()...)} noexcept;})
            requires (requires{{s.self->func(s.template get<Is>()...)};})
        {
            return (s.self->func((s.template get<Is>())...));
        }

        template <size_type... Is>
        constexpr void advance(std::index_sequence<Is...>)
            noexcept (requires{{(++(iterators.template get<Is>()), ...)} noexcept;})
            requires (requires{{(++(iterators.template get<Is>()), ...)};})
        {
            (++(iterators.template get<Is>()), ...);
            --length;
        }

    public:
        constexpr decltype(auto) operator*()
            noexcept (requires{{call(std::index_sequence_for<A...>{})} noexcept;})
            requires (requires{{call(std::index_sequence_for<A...>{})};})
        {
            return (call(std::index_sequence_for<A...>{}));
        }

        constexpr decltype(auto) operator*() const
            noexcept (requires{{call(std::index_sequence_for<A...>{})} noexcept;})
            requires (requires{{call(std::index_sequence_for<A...>{})};})
        {
            return (call(std::index_sequence_for<A...>{}));
        }

        constexpr iterator& operator++()
            noexcept (requires{{advance(std::make_index_sequence<N>{})} noexcept;})
            requires (requires{{advance(std::make_index_sequence<N>{})};})
        {
            advance(std::make_index_sequence<N>{});
            return *this;
        }

        [[nodiscard]] constexpr iterator operator++(int)
            noexcept (requires{
                {iterator(*this)} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {iterator(*this)};
                {++*this};
            })
        {
            iterator temp = *this;
            ++*this;
            return temp;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const iterator& self,
            impl::sentinel
        ) noexcept {
            return self.length == 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            impl::sentinel,
            const iterator& self
        ) noexcept {
            return self.length == 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const iterator& self,
            impl::sentinel
        ) noexcept {
            return self.length != 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            impl::sentinel,
            const iterator& self
        ) noexcept {
            return self.length != 0;
        }
    };

private:

    template <size_type... Is>
    constexpr auto init(std::index_sequence<Is...>)
        noexcept (requires{
            {iterator{this, size(), {args.template get<Is>().begin()...}}} noexcept;
        })
        requires (requires{
            {iterator{this, size(), {args.template get<Is>().begin()...}}};
        })
    {
        return iterator{this, size(), {args.template get<Is>().begin()...}};
    }

public:
    [[nodiscard]] constexpr auto begin()
        noexcept (requires{{init(indices{})} noexcept;})
        requires (requires{{init(indices{})};})
    {
        return init(indices{});
    }

    [[nodiscard]] static constexpr impl::sentinel end() noexcept { return {}; }

    template <typename V>
    [[nodiscard]] constexpr operator V()
        noexcept (requires{{V(std::from_range, *this)} noexcept;})
        requires (requires{{V(std::from_range, *this)};})
    {
        return V(std::from_range, *this);
    }

    template <typename V>
    [[nodiscard]] constexpr operator V()
        noexcept (requires{{V(begin(), end())} noexcept;})
        requires (!requires{{V(std::from_range, *this)};} && requires{{V(begin(), end())};})
    {
        return V(begin(), end());
    }
};


/* Specialization for nested comprehensions, which will be flattened into the final
range.  This allows comprehensions to model Python-style nested `for` and `if`
statements */
template <typename F, typename... A>
    requires (
        impl::valid_comprehension<F, A...> &&
        meta::comprehension<typename impl::invoke_comprehension<F, A...>::type>
    )
struct comprehension<F, A...> {
    using wrapped = impl::invoke_comprehension<F, A...>::type;
    using size_type = size_t;
    using index_type = ssize_t;
    using function_type = meta::unqualify<F>;
    using value_type = meta::unqualify<wrapped>::value_type;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const_ref<value_type>;
    using pointer = meta::as_pointer<value_type>;
    using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

private:
    static constexpr size_type N = impl::invoke_comprehension<F, A...>::N;
    using indices = impl::invoke_comprehension<F, A...>::indices;

    template <typename T>
    constexpr void get_length(T&&) noexcept {}
    template <meta::iterable_comprehension T>
    constexpr void get_length(T&& v)
        noexcept (requires{{std::forward<T>(v).size()} noexcept;})
    {
        if (auto n = std::forward<T>(v).size(); n < length) {
            length = n;
        }
    }

public:
    size_type length = std::numeric_limits<size_type>::max();
    meta::remove_rvalue<F> func;
    bertrand::args<A...> args;

    [[nodiscard]] constexpr comprehension(F func, A... args)
        noexcept (requires{
            {meta::remove_rvalue<F>(std::forward<F>(func))} noexcept;
            {bertrand::args<A...>(std::forward<A>(args)...)} noexcept;
            {(get_length(std::forward<A>(args)), ...)} noexcept;
        })
        requires (requires{
            {meta::remove_rvalue<F>(std::forward<F>(func))};
            {bertrand::args<A...>(std::forward<A>(args)...)};
            {(get_length(std::forward<A>(args)), ...)};
        })
    :
        func(std::forward<F>(func)),
        args(std::forward<A>(args)...)
    {
        (get_length(std::forward<A>(args)), ...);
    }

    [[nodiscard]] constexpr size_type size() const noexcept { return length; }
    [[nodiscard]] constexpr index_type ssize() const noexcept { return index_type(size()); }
    [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }

    /* A forward iterator that evaluates the comprehension for each element. */
    struct iterator {
        using storage_type = impl::invoke_comprehension<F, A...>::storage;
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = comprehension::value_type;
        using reference = comprehension::reference;
        using pointer = comprehension::pointer;

        comprehension* self;
        size_type length;
        storage_type iterators;
        [[no_unique_address]] union cache_type {
            [[no_unique_address]] struct type {
                wrapped inner;
                meta::unqualify<wrapped>::iterator iter = inner.begin();
                size_type length = inner.size();
            } value;
            constexpr ~cache_type() noexcept {};
        } cache;

        template <typename... Iters>
        constexpr iterator(
            comprehension* self,
            size_type length,
            Iters&&... iterators
        )
            noexcept (requires{
                {storage_type(std::forward<Iters>(iterators)...)} noexcept;
                {advance(std::make_index_sequence<N>{})} noexcept;
            })
            requires (requires{
                {storage_type(std::forward<Iters>(iterators)...)};
                {advance(std::make_index_sequence<N>{})};
            })
        :
            self(self),
            length(length),
            iterators(std::forward<Iters>(iterators)...),
            cache{}
        {
            if (length > 0) {
                advance(std::make_index_sequence<N>{});
            }
        }

        constexpr iterator(const iterator& other)
            noexcept (requires{
                {storage_type(other.iterators)} noexcept;
                {std::construct_at(&cache.value, other.cache.value)} noexcept;
            })
            requires (requires{
                {storage_type(other.iterators)};
                {std::construct_at(&cache.value, other.cache.value)};
            })
        :
            self(other.self),
            length(other.length),
            iterators(other.iterators),
            cache{}
        {
            if (length > 0) {
                std::construct_at(&cache.value, other.cache.value);
            }
        }

        constexpr iterator(iterator&& other)
            noexcept (requires{
                {storage_type(std::move(other.iterators))} noexcept;
                {std::construct_at(
                    &cache.value,
                    std::move(other.cache.value)
                )} noexcept;
                {std::destroy_at(&other.cache.value)} noexcept;
            })
            requires (requires{
                {storage_type(std::move(other.iterators))};
                {std::construct_at(&cache.value, std::move(other.cache.value))};
                {std::destroy_at(&other.cache.value)};
            })
        :
            self(other.self),
            length(other.length),
            iterators(std::move(other.iterators)),
            cache{}
        {
            if (length > 0) {
                std::construct_at(&cache.value, std::move(other.cache.value));
                std::destroy_at(&other.cache.value);
                other.length = 0;  // invalidate the moved-from iterator
            }
        }

        constexpr iterator& operator=(const iterator& other)
            noexcept (requires{
                {std::destroy_at(&cache.value)} noexcept;
                {iterators = other.iterators} noexcept;
                {std::construct_at(&cache.value, other.cache.value)} noexcept;
            })
            requires (requires{
                {std::destroy_at(&cache.value)};
                {iterators = other.iterators};
                {std::construct_at(&cache.value, other.cache.value)};
            })
        {
            if (this != other) {
                if (length > 0) {
                    std::destroy_at(&cache.value);
                }
                self = other.self;
                length = other.length;
                iterators = other.iterators;
                if (length > 0) {
                    std::construct_at(&cache.value, other.cache.value);
                }
            }
            return *this;
        }

        constexpr iterator& operator=(iterator&& other)
            noexcept (requires{
                {std::destroy_at(&cache.value)} noexcept;
                {iterators = std::move(other.iterators)} noexcept;
                {std::construct_at(
                    &cache.value,
                    std::move(other.cache.value)
                )} noexcept;
            })
            requires (requires{
                {std::destroy_at(&cache.value)};
                {iterators = std::move(other.iterators)};
                {std::construct_at(&cache.value, std::move(other.cache.value))};
            })
        {
            if (this != other) {
                if (length > 0) {
                    std::destroy_at(&cache.value);
                }
                self = other.self;
                length = other.length;
                iterators = std::move(other.iterators);
                if (length > 0) {
                    std::construct_at(&cache.value, std::move(other.cache.value));
                    std::destroy_at(&other.cache.value);
                    other.length = 0;  // invalidate the moved-from iterator
                }
            }
            return *this;
        }

        constexpr ~iterator() noexcept {}  // destructors are called during iteration

    private:
        template <typename S, size_type I>
        constexpr decltype(auto) get(this S&& s)
            noexcept (meta::iterable_comprehension<meta::unpack_type<I, A...>> ?
                requires{
                    {*s.iterators.template get<impl::comprehension_idx<I, A...>>()} noexcept;
                } : requires{
                    {s.self->args.template get<I>()} noexcept;
                }
            )
        {
            if constexpr (meta::iterable_comprehension<meta::unpack_type<I, A...>>) {
                return (*s.iterators.template get<impl::comprehension_idx<I, A...>>());
            } else {
                return (s.self->args.template get<I>());
            }
        }

        template <typename S, size_type... Is>
        constexpr void call(this S&& s, std::index_sequence<Is...>)
            noexcept (requires{{std::construct_at(
                &s.cache.value,
                s.self->func((s.template get<Is>())...)
            )} noexcept;})
            requires (requires{{std::construct_at(
                &s.cache.value,
                s.self->func((s.template get<Is>())...)
            )};})
        {
            std::construct_at(
                &s.cache.value,
                s.self->func((s.template get<Is>())...)
            );
        }

        template <size_type... Is>
        constexpr void advance(std::index_sequence<Is...>)
            noexcept (requires{
                {call(std::index_sequence_for<A...>{})} noexcept;
                {(++(iterators.template get<Is>()), ...)} noexcept;
                {std::destroy_at(&cache.value)} noexcept;
            })
            requires (requires{
                {call(std::index_sequence_for<A...>{})};
                {(++(iterators.template get<Is>()), ...)};
                {std::destroy_at(&cache.value)};
            })
        {
            // compute the value for the current iterator state
            call(std::index_sequence_for<A...>{});

            // advance the input iterators, which always stay one step ahead
            (++(iterators.template get<Is>()), ...);

            // if the cached comprehension is empty, repeat and decrement length
            // until we find a non-empty one or reach the end of the outer
            // comprehension
            while (cache.value.length == 0) {
                std::destroy_at(&cache.value);
                --length;
                if (length == 0) {
                    break;
                }
                call(std::index_sequence_for<A...>{});
                (++(iterators.template get<Is>()), ...);
            }
        }

    public:
        constexpr decltype(auto) operator*()
            noexcept (requires{{*cache.value().iter} noexcept;})
            requires (requires{{*cache.value().iter};})
        {
            return (*cache.value.iter);
        }

        constexpr decltype(auto) operator*() const
            noexcept (requires{{*cache.value().iter} noexcept;})
            requires (requires{{*cache.value().iter};})
        {
            return (*cache.value.iter);
        }

        constexpr auto* operator->()
            noexcept (requires{{cache.value().iter.operator->()} noexcept;})
            requires (requires{{cache.value().iter.operator->()};})
        {
            return cache.value.iter.operator->();
        }

        constexpr auto* operator->() const
            noexcept (requires{{cache.value().iter.operator->()} noexcept;})
            requires (requires{{cache.value().iter.operator->()};})
        {
            return cache.value.iter.operator->();
        }

        constexpr iterator& operator++()
            noexcept (requires{
                {++cache.value.iter} noexcept;
                {std::destroy_at(&cache.value)} noexcept;
                {advance(std::make_index_sequence<N>{})} noexcept;
            })
            requires (requires{
                {++cache.value.iter};
                {std::destroy_at(&cache.value)};
                {advance(std::make_index_sequence<N>{})};
            })
        {
            --cache.value.length;
            if (cache.value.length > 0) {
                ++cache.value.iter;
            } else {
                --length;
                if (length > 0) {
                    std::destroy_at(&cache.value);
                    advance(std::make_index_sequence<N>{});
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr iterator operator++(int)
            noexcept (requires{
                {iterator(*this)} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {iterator(*this)};
                {++*this};
            })
        {
            iterator temp = *this;
            ++*this;
            return temp;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const iterator& self,
            impl::sentinel
        ) noexcept {
            return self.length == 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            impl::sentinel,
            const iterator& self
        ) noexcept {
            return self.length == 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const iterator& self,
            impl::sentinel
        ) noexcept {
            return self.length != 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            impl::sentinel,
            const iterator& self
        ) noexcept {
            return self.length != 0;
        }
    };

private:

    template <size_type... Is>
    constexpr auto init(std::index_sequence<Is...>)
        noexcept (requires{
            {iterator{this, size(), {args.template get<Is>().begin()...}}} noexcept;
        })
        requires (requires{
            {iterator{this, size(), {args.template get<Is>().begin()...}}};
        })
    {
        return iterator{this, size(), {args.template get<Is>().begin()...}};
    }

public:
    [[nodiscard]] constexpr auto begin()
        noexcept (requires{{init(indices{})} noexcept;})
        requires (requires{{init(indices{})};})
    {
        return init(indices{});
    }

    [[nodiscard]] static constexpr impl::sentinel end() noexcept { return {}; }

    template <typename V>
    [[nodiscard]] constexpr operator V()
        noexcept (requires{{V(std::from_range, *this)} noexcept;})
        requires (requires{{V(std::from_range, *this)};})
    {
        return V(std::from_range, *this);
    }

    template <typename V>
    [[nodiscard]] constexpr operator V()
        noexcept (requires{{V(begin(), end())} noexcept;})
        requires (!requires{{V(std::from_range, *this)};} && requires{{V(begin(), end())};})
    {
        return V(begin(), end());
    }
};


template <typename F, typename... A>
comprehension(F&&, A&&...) -> comprehension<F, A...>;


namespace impl {

    template <typename F, meta::tuple_like T, size_t... Is>
        requires (meta::tuple_size<T> == sizeof...(Is))
    constexpr decltype(auto) call_tuple(F&& f, T&& t, std::index_sequence<Is...>)
        noexcept (requires{
            {std::forward<F>(f)(meta::tuple_get<Is>(std::forward<T>(t))...)} noexcept;
        })
        requires (requires{
            {std::forward<F>(f)(meta::tuple_get<Is>(std::forward<T>(t))...)};
        })
    {
        return (std::forward<F>(f)(meta::tuple_get<Is>(std::forward<T>(t))...));
    }

    /* A wrapper around a function `F` that expects a tuple-like argument of type `T`
    and decomposes it into its individual components before forwarding to the wrapped
    function.  This is used to create structured comprehensions. */
    template <typename F>
    struct decompose {
        meta::remove_rvalue<F> func;

        template <typename Self, meta::tuple_like T>
        constexpr decltype(auto) operator()(this Self&& self, T&& t)
            noexcept (requires{{call_tuple(
                std::forward<Self>(self).func,
                std::forward<T>(t),
                std::make_index_sequence<meta::tuple_size<T>>{}
            )} noexcept;})
            requires (requires{{call_tuple(
                std::forward<Self>(self).func,
                std::forward<T>(t),
                std::make_index_sequence<meta::tuple_size<T>>{}
            )};})
        {
            return (call_tuple(
                std::forward<Self>(self).func,
                std::forward<T>(t),
                std::make_index_sequence<meta::tuple_size<T>>{}
            ));
        }
    };

    template <typename F>
    decompose(F&&) -> decompose<F>;

    /* When used on iterable containers that yield non-iterable, tuple-like types,
    `operator->*` will generate a comprehension that decomposes each element when
    calling the supplied function. */
    template <meta::unpackable T, meta::comprehension_func<T> F>
        requires (meta::detail::use_structured_comprehension<T>)
    [[nodiscard]] constexpr auto comprehend(T&& container, F&& func)
        noexcept (requires{{bertrand::comprehension{
            decompose{std::forward<F>(func)},
            unpack(std::forward<T>(container))
        }} noexcept;})
        requires (requires{{bertrand::comprehension{
            decompose{std::forward<F>(func)},
            unpack(std::forward<T>(container))
        }};})
    {
        return bertrand::comprehension{
            decompose{std::forward<F>(func)},
            unpack(std::forward<T>(container))
        };
    }

    /* When used on iterable containers that yield iterable or non-tuple-like types,
    `operator->*` will generate a comprehension that accepts the yield type
    directly. */
    template <meta::unpackable T, meta::comprehension_func<T> F>
        requires (meta::detail::use_standard_comprehension<T>)
    [[nodiscard]] constexpr auto comprehend(T&& container, F&& func)
        noexcept (requires{{bertrand::comprehension{
            std::forward<F>(func),
            unpack(std::forward<T>(container))
        }} noexcept;})
        requires (requires{{bertrand::comprehension{
            std::forward<F>(func),
            unpack(std::forward<T>(container))
        }};})
    {
        return bertrand::comprehension{
            std::forward<F>(func),
            unpack(std::forward<T>(container))
        };
    }

    /* When used on non-iterable, tuple-like containers, `operator->*` will decompose
    the container elements and directly call the supplied function with its contents. */
    template <meta::unpackable T, meta::comprehension_func<T> F>
        requires (meta::detail::use_direct_comprehension<T>)
    [[nodiscard]] constexpr decltype(auto) comprehend(T&& container, F&& func)
        noexcept (requires{{call_tuple(
            std::forward<F>(func),
            std::forward<T>(container),
            std::make_index_sequence<meta::tuple_size<T>>{}
        )} noexcept;})
        requires (requires{{call_tuple(
            std::forward<F>(func),
            std::forward<T>(container),
            std::make_index_sequence<meta::tuple_size<T>>{}
        )};})
    {
        return (call_tuple(
            std::forward<F>(func),
            std::forward<T>(container),
            std::make_index_sequence<meta::tuple_size<T>>{}
        ));
    }

    /* Iterate over all elements of an input range and discard the results.  This is
    used to invoke a modified assignment comprehension, so that it eagerly evaluates
    an elementwise assignment function, which operates by side effect. */
    template <meta::iterable R>
    constexpr void exhaust(R&& r)
        noexcept (requires{
            {std::ranges::begin(r) != std::ranges::end(r)} noexcept;
            {*std::ranges::begin(r)} noexcept;
            {++std::ranges::begin(r)} noexcept;
        })
        requires (requires{
            {std::ranges::begin(r) != std::ranges::end(r)};
            {*std::ranges::begin(r)};
            {++std::ranges::begin(r)};
        })
    {
        auto it = std::ranges::begin(r);
        auto end = std::ranges::end(r);
        while (it != end) {
            *it;
            ++it;
        }
    }

    /// NOTE: all elementary math operators are enabled between comprehensions in
    /// order to simulate SIMD-style expression templates.  Note that no actual SIMD
    /// primitives are explicitly called - these expressions simply fuse the loops and
    /// allow the  auto-vectorizer to make that decision behind the scenes, which often
    /// leads to SIMD codegen anyways.

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator<(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Less{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Less{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Less{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator<=(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::LessEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::LessEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::LessEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator==(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Equal{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Equal{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Equal{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator!=(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::NotEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::NotEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::NotEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator>=(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::GreaterEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::GreaterEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::GreaterEqual{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator>(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Greater{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Greater{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Greater{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator<=>(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Spaceship{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Spaceship{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Spaceship{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension T>
    constexpr auto operator+(T&& self)
        noexcept (requires{{comprehension{
            impl::Pos{},
            std::forward<T>(self)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Pos{},
            std::forward<T>(self)
        }};})
    {
        return comprehension{
            impl::Pos{},
            std::forward<T>(self)
        };
    }

    template <meta::iterable_comprehension T>
    constexpr auto operator-(T&& self)
        noexcept (requires{{comprehension{
            impl::Neg{},
            std::forward<T>(self)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Neg{},
            std::forward<T>(self)
        }};})
    {
        return comprehension{
            impl::Neg{},
            std::forward<T>(self)
        };
    }

    template <meta::iterable_comprehension T>
    constexpr void operator++(T&& self)
        noexcept (requires{{exhaust(comprehension{
            impl::PreIncrement{},
            std::forward<T>(self)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::PreIncrement{},
            std::forward<T>(self)
        })};})
    {
        exhaust(comprehension{
            impl::PreIncrement{},
            std::forward<T>(self)
        });
    }

    template <meta::iterable_comprehension T>
    constexpr void operator++(T&& self, int)
        noexcept (requires{{exhaust(comprehension{
            impl::PostIncrement{},
            std::forward<T>(self)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::PostIncrement{},
            std::forward<T>(self)
        })};})
    {
        exhaust(comprehension{
            impl::PostIncrement{},
            std::forward<T>(self)
        });
    }

    template <meta::iterable_comprehension T>
    constexpr void operator--(T&& self)
        noexcept (requires{{exhaust(comprehension{
            impl::PreDecrement{},
            std::forward<T>(self)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::PreDecrement{},
            std::forward<T>(self)
        })};})
    {
        exhaust(comprehension{
            impl::PreDecrement{},
            std::forward<T>(self)
        });
    }

    template <meta::iterable_comprehension T>
    constexpr void operator--(T&& self, int)
        noexcept (requires{{exhaust(comprehension{
            impl::PostDecrement{},
            std::forward<T>(self)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::PostDecrement{},
            std::forward<T>(self)
        })};})
    {
        exhaust(comprehension{
            impl::PostDecrement{},
            std::forward<T>(self)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator+(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Add{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Add{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Add{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator+=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceAdd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceAdd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceAdd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator-(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Subtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Subtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Subtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator-=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceSubtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceSubtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceSubtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator*(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Multiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Multiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Multiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator*=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceMultiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceMultiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceMultiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator/(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Divide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Divide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Divide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator/=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceDivide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceDivide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceDivide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator%(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::Modulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::Modulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::Modulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator%=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceModulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceModulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceModulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator<<(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::LeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::LeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::LeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator<<=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceLeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceLeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceLeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator>>(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::RightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::RightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::RightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator>>=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceRightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceRightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceRightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator|(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::BitwiseOr{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::BitwiseOr{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::BitwiseOr{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator|=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceBitwiseOr{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceBitwiseOr{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceBitwiseOr{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator&(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::BitwiseAnd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::BitwiseAnd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::BitwiseAnd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator&=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceBitwiseAnd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceBitwiseAnd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceBitwiseAnd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

    template <typename L, typename R>
        requires (meta::iterable_comprehension<L> || meta::iterable_comprehension<R>)
    [[nodiscard]] constexpr auto operator^(L&& lhs, R&& rhs)
        noexcept (requires{{comprehension{
            impl::BitwiseXor{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (requires{{comprehension{
            impl::BitwiseXor{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }};})
    {
        return comprehension{
            impl::BitwiseXor{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::iterable_comprehension L, typename R>
    constexpr void operator^=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust(comprehension{
            impl::InplaceBitwiseXor{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (requires{{exhaust(comprehension{
            impl::InplaceBitwiseXor{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })};})
    {
        exhaust(comprehension{
            impl::InplaceBitwiseXor{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
    }

}


namespace meta {

    namespace detail {

        template <typename C>
        constexpr bool unpack_view<bertrand::unpack<C>> = true;

        template <typename C>
        constexpr bool comprehension<bertrand::unpack<C>> = true;
        template <typename F, typename... A>
        constexpr bool comprehension<bertrand::comprehension<F, A...>> = true;

        template <typename... Ts>
        constexpr bool args<bertrand::args<Ts...>> = true;
        template <typename... Ts>
        constexpr bool args<impl::args<Ts...>> = true;

        // template <typename T>
        // constexpr bool arg<impl::arg_pack<T>> = true;
        template <typename T>
        constexpr bool arg<impl::kwarg_pack<T>> = true;

        // template <typename T>
        // constexpr bool arg_pack<impl::arg_pack<T>> = true;

        template <typename T>
        constexpr bool kwarg_pack<impl::kwarg_pack<T>> = true;

    }

}


template <meta::unpackable T>
[[nodiscard]] constexpr auto operator*(T&& container)
    noexcept (requires{{unpack(std::forward<T>(container))} noexcept;})
    requires (requires{{unpack(std::forward<T>(container))};})
{
    return unpack(std::forward<T>(container));
}


template <meta::unpackable T, meta::comprehension_func<T> F>
constexpr decltype(auto) operator->*(T&& container, F&& func)
    noexcept (requires{{impl::comprehend(
        std::forward<T>(container),
        std::forward<F>(func))
    } noexcept;})
    requires (requires{{impl::comprehend(
        std::forward<T>(container),
        std::forward<F>(func))
    };})
{
    return (impl::comprehend(std::forward<T>(container), std::forward<F>(func)));
}


/////////////////////////
////    SIGNATURE    ////
/////////////////////////


namespace impl {
    struct signature_tag {};

    /* Internal check to ensure partial arguments store only the minimal necessary
    information. */
    template <typename A, typename... Ts>
    constexpr bool check_partial = (
        (meta::unqualified<A> && meta::arg<A> && !meta::partial_arg<A>) && ... && (
            meta::not_void<Ts> &&
            !meta::rvalue<Ts> &&
            !meta::arg<Ts>
        ));
    template <meta::variadic_keyword A, typename... Ts>
    constexpr bool check_partial<A, Ts...> = (
        (meta::unqualified<A> && meta::arg<A> && !meta::partial_arg<A>) && ... && (
            !meta::rvalue<Ts> &&
            !meta::partial_arg<Ts> &&
            meta::standard_arg<Ts> &&
            meta::bound_arg<Ts>
        ));

    /* A subclass of `Arg` that categorizes it as having one or more partially-applied
    values when stored within an `impl::signature` object.  This changes nothing about
    the argument itself, but triggers the function call machinery to always insert a
    particular value that may only be known at runtime, and is stored within the
    signature itself.  Calling a signature's `.bind()` method equates to marking one or
    more of its arguments with this type, and `.clear()` strips all such wrappers from
    the signature's arguments. */
    template <typename Arg, typename... Ts> requires (check_partial<Arg, Ts...>)
    struct partial : Arg {
        using partials = meta::pack<Ts...>;
    };

    template <typename, typename, typename...>
    struct _as_partial;
    template <typename A, typename... Ps, typename... Ts>
    struct _as_partial<A, meta::pack<Ps...>, Ts...> {
        using type = partial<A, Ps..., Ts...>;
    };
    template <meta::arg A, typename... Ts>
    using as_partial = _as_partial<
        typename meta::unqualify<A>::base_type,
        meta::partials<A>,
        meta::remove_rvalue<Ts>...
    >::type;

    /* Mark a runtime argument as a partial, which will store a list of types inside
    the signature, to be used at runtime for binding.  If the argument already has
    partial values, then the argument list will be extended with the new values. */
    template <typename... Ts, typename A> requires (sizeof...(Ts) > 0)
    constexpr auto make_partial(const A& arg)
        noexcept (requires{{as_partial<A, Ts...>{arg}} noexcept;})
        requires (requires{{as_partial<A, Ts...>{arg}};})
    {
        return as_partial<A, Ts...>{arg};
    }

    /* Template arguments must store partial values internally in order to work with
    the binding logic.  As soon as a `bind()` call has finished executing, the partial
    values will be concatenated to form the final `templates{}` container, and each
    `partial_t<>` annotation will be demoted to `partial<>`, to minimize binary size
    and keep template errors concise. */
    template <typename Arg, typename... Ts> requires (check_partial<Arg, Ts...>)
    struct partial_t : partial<Arg, Ts...> {
        using base_type = partial<Arg, Ts...>;
        impl::args<Ts...> partial_values;
        template <meta::is<Arg> A>
        constexpr partial_t(A&& arg, Ts... ts)
            noexcept (requires{
                {base_type(std::forward<A>(arg))} noexcept;
                {impl::args<Ts...>{std::forward<Ts>(ts)...}} noexcept;
            })
        :
            base_type(std::forward<A>(arg)),
            partial_values(std::forward<Ts>(ts)...)
        {}
    };

    template <typename, typename, typename...>
    struct _as_partial_t;
    template <typename A, typename... Ps, typename... Ts>
    struct _as_partial_t<A, meta::pack<Ps...>, Ts...> {
        using type = partial_t<A, Ps..., Ts...>;
    };
    template <meta::arg A, typename... Ts>
    using as_partial_t = _as_partial_t<
        typename meta::unqualify<A>::base_type,
        meta::partials<A>,
        meta::remove_rvalue<Ts>...
    >::type;

    /* Counts the number of partial arguments to the right of `I` in `A...`.  This
    allows translation from indices in a partial signature to values in a corresponding
    `args{}` container or argument list. */
    template <size_t, typename...>
    constexpr size_t partial_idx = 0;
    template <size_t I, typename A, typename... Args> requires (!meta::partial_arg<A> && I > 0)
    constexpr size_t partial_idx<I, A, Args...> = partial_idx<I - 1, Args...>;
    template <size_t I, typename A, typename... Args> requires (meta::partial_arg<A> && I > 0)
    constexpr size_t partial_idx<I, A, Args...> =
        partial_idx<I - 1, Args...> + meta::partials<A>::size;

    /* Form a `bertrand::args{}` container to back a set of partial arguments.  Calling
    the `extract_partial` helper is only used when constructing a `templates{}`
    container, to extract partial values from `partial_t` wrappers. */
    template <typename, typename...>
    struct _extract_partial;
    template <typename... out>
    struct _extract_partial<meta::pack<out...>> {
        using type = bertrand::args<out...>;
        static constexpr type operator()(auto&&... args)
            noexcept (requires{{type{std::forward<decltype(args)>(args)...}} noexcept;})
        {
            return {std::forward<decltype(args)>(args)...};
        }
    };
    template <typename... out, typename A, typename... As> requires (meta::partial_arg<A>)
    struct _extract_partial<meta::pack<out...>, A, As...> {
        using type = _extract_partial<
            typename meta::pack<out...>::template concat<meta::partials<A>>,
            As...
        >::type;

    private:
        static constexpr size_t idx = partial_idx<sizeof...(out), out...>;

        template <size_t... prev, size_t... parts, size_t... next>
        static constexpr type extract(
            std::index_sequence<prev...>,
            std::index_sequence<parts...>,
            std::index_sequence<next...>,
            auto&&... args
        )
            noexcept (requires{{_extract_partial<
                typename meta::pack<out...>::template concat<meta::partials<A>>,
                As...
            >{}(
                meta::unpack_arg<prev>(std::forward<decltype(args)>(args)...)...,
                meta::unpack_arg<sizeof...(prev)>(
                    std::forward<decltype(args)>(args)...
                ).partial_values.template get<parts>()...,
                meta::unpack_arg<sizeof...(prev) + 1 + next>(
                    std::forward<decltype(args)>(args)...
                )...
            )} noexcept;})
        {
            return _extract_partial<
                typename meta::pack<out...>::template concat<meta::partials<A>>,
                As...
            >{}(
                meta::unpack_arg<prev>(std::forward<decltype(args)>(args)...)...,
                meta::unpack_arg<sizeof...(prev)>(
                    std::forward<decltype(args)>(args)...
                ).partial_values.template get<parts>()...,
                meta::unpack_arg<sizeof...(prev) + 1 + next>(
                    std::forward<decltype(args)>(args)...
                )...
            );
        }

    public:
        static constexpr type operator()(auto&&... args)
            noexcept (requires{{extract(
                std::make_index_sequence<idx>(),
                std::make_index_sequence<meta::partials<A>::size>(),
                std::make_index_sequence<sizeof...(args) - (idx + 1)>(),
                std::forward<decltype(args)>(args)...
            )} noexcept;})
        {
            return extract(
                std::make_index_sequence<idx>(),
                std::make_index_sequence<meta::partials<A>::size>(),
                std::make_index_sequence<sizeof...(args) - (idx + 1)>(),
                std::forward<decltype(args)>(args)...
            );
        }
    };
    template <typename... out, typename A, typename... As> requires (!meta::partial_arg<A>)
    struct _extract_partial<meta::pack<out...>, A, As...> {
        using type = _extract_partial<meta::pack<out...>, As...>::type;

    private:
        static constexpr size_t idx = partial_idx<sizeof...(out), out...>;

        template <size_t... prev, size_t... parts, size_t... next>
        static constexpr type extract(
            std::index_sequence<prev...>,
            std::index_sequence<next...>,
            auto&&... args
        )
            noexcept (requires{{_extract_partial<meta::pack<out...>, As...>{}(
                meta::unpack_arg<prev>(std::forward<decltype(args)>(args)...)...,
                meta::unpack_arg<sizeof...(prev) + 1 + next>(
                    std::forward<decltype(args)>(args)...
                )...
            )} noexcept;})
        {
            return _extract_partial<meta::pack<out...>, As...>{}(
                meta::unpack_arg<prev>(std::forward<decltype(args)>(args)...)...,
                meta::unpack_arg<sizeof...(prev) + 1 + next>(
                    std::forward<decltype(args)>(args)...
                )...
            );
        }

    public:
        static constexpr type operator()(auto&&... args)
            noexcept (requires{{extract(
                std::make_index_sequence<idx>(),
                std::make_index_sequence<sizeof...(args) - (idx + 1)>(),
                std::forward<decltype(args)>(args)...
            )} noexcept;})
        {
            return extract(
                std::make_index_sequence<idx>(),
                std::make_index_sequence<sizeof...(args) - (idx + 1)>(),
                std::forward<decltype(args)>(args)...
            );
        }
    };
    template <typename... As> requires (meta::arg<As> && ...)
    using extract_partial = _extract_partial<meta::pack<>, As...>;

    /* A brace-initializable container for a sequence of arguments that forms the
    explicit template signature for a function.  Arguments within this container must
    be provided as template arguments when the function is called. */
    template <meta::unqualified... Args> requires (meta::arg<Args> && ...)
    struct templates {
        using types = meta::pack<Args...>;
        using args_type = impl::args<Args...>;
        using partial_type = extract_partial<Args...>::type;
        using size_type = size_t;
        using index_type = ssize_t;

        [[nodiscard]] static constexpr size_type size() noexcept { return sizeof...(Args); }
        [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }
        [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }

        args_type args;
        partial_type partial;

        template <meta::inherits<Args>... As>
        constexpr templates(As&&... args)
            noexcept (requires{
                {args_type{std::forward<As>(args)...}} noexcept;
                {partial_type{extract_partial<Args...>{}(std::forward<As>(args)...)}} noexcept;
            })
        :
            args(std::forward<As>(args)...),
            partial(extract_partial<Args...>{}(std::forward<As>(args)...))
        {}

        template <size_type I>
        [[nodiscard]] static constexpr size_type index() noexcept {
            return partial_idx<I, Args...>;
        }

    private:
        template <size_type I, size_type... Is, typename... Ts>
        constexpr auto _make_partial(std::index_sequence<Is...>, Ts&&... ts) const
            noexcept (requires{{as_partial_t<meta::unpack_type<I, Args...>, Ts...>{
                args.template get<I>(),
                partial.template get<partial_idx<I, Args...> + Is>()...,
                std::forward<Ts>(ts)...
            }} noexcept;})
            requires (requires{{as_partial_t<meta::unpack_type<I, Args...>, Ts...>{
                args.template get<I>(),
                partial.template get<partial_idx<I, Args...> + Is>()...,
                std::forward<Ts>(ts)...
            }};})
        {
            return as_partial_t<meta::unpack_type<I, Args...>, Ts...>{
                args.template get<I>(),
                partial.template get<partial_idx<I, Args...> + Is>()...,
                std::forward<Ts>(ts)...
            };
        }

    public:
        template <size_type I, typename... Ts> requires (I < size())
        constexpr auto make_partial(Ts&&... ts) const
            noexcept (requires{{_make_partial<I>(
                std::make_index_sequence<meta::partials<meta::unpack_type<I, Args...>>::size>(),
                std::forward<Ts>(ts)...
            )} noexcept;})
            requires (requires{{_make_partial<I>(
                std::make_index_sequence<meta::partials<meta::unpack_type<I, Args...>>::size>(),
                std::forward<Ts>(ts)...
            )};})
        {
            return _make_partial<I>(
                std::make_index_sequence<meta::partials<meta::unpack_type<I, Args...>>::size>(),
                std::forward<Ts>(ts)...
            );
        }
    };

    template <meta::arg... A>
    templates(A...) -> templates<A...>;

    /* Strip any partial values from an argument. */
    template <meta::arg A> requires (meta::partial_arg<A>)
    constexpr decltype(auto) remove_partial(A&& arg)
        noexcept (requires{
            {std::forward<meta::qualify<typename meta::unqualify<A>::base_type, A>>(arg)} noexcept;
        })
        requires (requires{
            {std::forward<meta::qualify<typename meta::unqualify<A>::base_type, A>>(arg)};
        })
    {
        return (std::forward<meta::qualify<typename meta::unqualify<A>::base_type, A>>(arg));
    }

    /* Strip any partial values from an argument. */
    template <meta::arg A> requires (!meta::partial_arg<A>)
    constexpr decltype(auto) remove_partial(A&& arg) noexcept {
        return (std::forward<A>(arg));
    }

}


namespace meta {

    template <typename T>
    concept signature = inherits<T, impl::signature_tag>;

    namespace detail {

        template <typename... Ts>
        constexpr bool arg<impl::partial<Ts...>> = true;
        template <typename... Ts>
        constexpr bool arg<impl::partial_t<Ts...>> = true;

        enum class bind_error : uint8_t {
            ok,
            bad_type,
            conflicting_values,
            missing_required,
            extra_positional,
            extra_keyword,
        };

    }

    template <detail::bind_error E>
    concept bind_error = E == detail::bind_error::ok || (
        E != detail::bind_error::bad_type &&
        E != detail::bind_error::conflicting_values &&
        E != detail::bind_error::missing_required &&
        E != detail::bind_error::extra_positional &&
        E != detail::bind_error::extra_keyword
    );

}


namespace impl {

    /* A compact bitset describing the kind (positional, keyword, optional, and/or
    variadic) of an argument within a C++ parameter list. */
    struct arg_kind {
        enum Flags : uint8_t {
            /// NOTE: the relative ordering of these flags is significant, as it
            /// dictates the order in which edges are stored within overload tries for
            /// the `py::Function` class.  The order should always be such that
            /// POS < OPT POS < VAR POS < KW < OPT KW < VAR KW (repeated for untyped
            /// arguments) to ensure a stable traversal order.
            OPT                 = 0b1,
            VAR                 = 0b10,
            POS                 = 0b100,
            KW                  = 0b1000,
            UNTYPED             = 0b10000,
            RUNTIME             = 0b100000,
        } flags;

        [[nodiscard]] constexpr arg_kind(uint8_t flags = 0) noexcept :
            flags(static_cast<Flags>(flags))
        {}

        [[nodiscard]] constexpr operator uint8_t() const noexcept {
            return flags;
        }

        [[nodiscard]] constexpr bool optional() const noexcept {
            return flags & OPT;
        }

        [[nodiscard]] constexpr bool variadic() const noexcept {
            return flags & VAR;
        }

        [[nodiscard]] constexpr bool typed() const noexcept {
            return !(flags & UNTYPED);
        }

        [[nodiscard]] constexpr bool untyped() const noexcept {
            return flags & UNTYPED;
        }

        [[nodiscard]] constexpr bool compile_time() const noexcept {
            return !(flags & RUNTIME);
        }

        [[nodiscard]] constexpr bool run_time() const noexcept {
            return flags & RUNTIME;
        }

        [[nodiscard]] constexpr bool pos() const noexcept {
            return (flags & (VAR | POS)) == POS;
        }

        [[nodiscard]] constexpr bool kw() const noexcept {
            return (flags & (VAR | KW)) == KW;
        }

        [[nodiscard]] constexpr bool pos_or_kw() const noexcept {
            return (flags & (VAR | POS | KW)) == (POS | KW);
        }

        [[nodiscard]] constexpr bool posonly() const noexcept {
            return (flags & (VAR | POS | KW)) == POS;
        }

        [[nodiscard]] constexpr bool kwonly() const noexcept {
            return (flags & (VAR | POS | KW)) == KW;
        }

        [[nodiscard]] constexpr bool args() const noexcept {
            return (flags & (VAR | POS | KW)) == (VAR | POS);
        }

        [[nodiscard]] constexpr bool kwargs() const noexcept {
            return (flags & (VAR | POS | KW)) == (VAR | KW);
        }

        [[nodiscard]] constexpr bool sentinel() const noexcept {
            return (flags & (POS | KW)) == 0;
        }
    };

    /* A collection of arguments representing a valid Python-style signature.  This
    will analyze the arguments upon construction and throw a compile-time `SyntaxError`
    if the signature is invalid in some way, with a contextual message that indicates
    the error as well as its location in the signature, for easy debugging.  These
    errors include:

        -   Duplicate argument names.
        -   Required positional arguments after optional.
        -   Proper ordering of positional-only and keyword-only separators, variadic
            arguments, and return annotations.
        -   Duplicate separators, variadic arguments, or return annotations.
        -   Untyped return annotations.

    If no errors are encountered, then the resulting `signature_info` object will
    contain the following information:

        -   The starting index and number of each category of argument.
        -   An array of `param` structs aligned to the argument list, which compactly
            describe the status of each argument in the signature, for use during
            argument binding and signature indexing/iteration.
    */
    template <const auto& /* templates */ Spec, const auto&... Args>
    struct signature_info {
    private:
        template <typename A>
        static constexpr static_str display_arg = meta::arg_id<A>;
        template <meta::bound_arg A>
        static constexpr static_str display_arg<A> = meta::arg_id<A> + " = ...";

        template <size_t... Is>
        static consteval auto _ctx_str(std::index_sequence<Is...>) noexcept {
            if constexpr (Spec.size()) {
                return (
                    "def<" + string_wrapper<", ">::join<
                        display_arg<decltype(Spec.args.template get<Is>())>...
                    >() + ">(" + string_wrapper<", ">::join<
                        display_arg<decltype(Args)>...
                    >() + ")"
                );
            } else {
                return (
                    "def(" + string_wrapper<", ">::join<
                        display_arg<decltype(Args)>...
                    >() + ")"
                );
            }
        }

        template <size_t... Is>
        static consteval auto _ctx_arrow(std::index_sequence<Is...>) noexcept {
            return (
                static_str<4>{' '} +  // "def<"
                ... +
                static_str<display_arg<decltype(get<Is>())>.size() + 2>{' '}  // ", " or ">("
            ) + "^";
        }

    public:
        /* Total number of argument annotations that were passed in. */
        static constexpr size_t N = Spec.size() + sizeof...(Args);

        /* Indentation used for displaying contextual error messages. */
        static constexpr static_str<4> indent {' '};

        /* A context string to be included in error messages, to assist with
        debugging.  Consists only of argument ids and default value placeholders. */
        static constexpr static_str ctx_str = _ctx_str(
            std::make_index_sequence<Spec.size()>{}
        );

        /* An arrow to a particular index of the string, to be included one line below
        `ctx_str` in error messages, to assist with debugging. */
        template <size_t I>
        static constexpr static_str ctx_arrow = _ctx_arrow(std::make_index_sequence<I>{});

        /* Get the argument at index I, correcting for the split between template
        arguments and runtime arguments. */
        template <size_t I> requires (I < N)
        [[nodiscard]] static constexpr decltype(auto) get() noexcept {
            if constexpr (I < Spec.size()) {
                return (Spec.args.template get<I>());
            } else {
                return (meta::unpack_value<I - Spec.size(), Args...>);
            }
        }

        /* A simple struct describing a single parameter in a signature in type-erased
        form. */
        struct param {
            std::string_view name;
            size_t index;
            size_t partial;
            impl::arg_kind kind;

            /// TODO: in C++26, this could also include a std::meta_info field for the
            /// type.
        };

        /* Signatures consist of 2 sections, the first for explicit template arguments
        and the second for runtime arguments. */
        struct list {
            /* [1] positional-only */
            struct {
                size_t offset = N;  // start index relative to start of section.  N = missing
                size_t n = 0;  // # of positional-only args.  Separator occurs at this index.
            } posonly;

            /* [2] variadic positional */
            struct {
                size_t offset = N;  // index relative to start of section.  N = missing
            } args;

            /* [3] positional-or-keyword */
            struct {
                size_t offset = 0;  // start index relative to start of section.
                size_t n = 0;  // # of positional-or-keyword arguments
            } pos_or_kw;

            /* [4] keyword-only */
            struct {
                size_t offset = N;  // start index relative to start of section.  N = missing.
                size_t n = 0;  // # of keyword-only arguments
            } kwonly;

            /* [5] variadic keyword */
            struct {
                size_t offset = N;  // index relative to start of section.  N = missing
            } kwargs;
        };

        list compile_time;
        list run_time;

        /* [6] return annotation */
        struct {
            size_t idx = N;  // N = missing
        } ret;

        /* A packed array of kinds for each argument, incorporating context from the
        rest of the signature. */
        using params_t = std::array<param, N>;
        params_t params;

        template <size_t... Is> requires (sizeof...(Is) == N)
        consteval signature_info(std::index_sequence<Is...>) {
            (parse<Is>(get<Is>()), ...);
        }

    private:
        template <const auto& name, size_t... Is>
        static consteval void duplicate_names(std::index_sequence<Is...>) {
            if constexpr (
                !name.empty() && ((name == meta::arg_name<decltype(get<Is>())>) || ...)
            ) {
                static constexpr static_str msg =
                    "duplicate argument '" + name + "'\n\n" + indent + ctx_str + "\n" +
                    indent + ctx_arrow<sizeof...(Is)>;
                throw SyntaxError(msg);
            }
        }

        template <size_t offset, size_t... Is>
        static consteval void required_after_optional(std::index_sequence<Is...>) {
            if constexpr ((meta::bound_arg<decltype(get<offset + Is>())> || ...)) {
                static constexpr static_str msg =
                    "required arguments cannot follow optional ones\n\n" +
                    indent + ctx_str + "\n" + indent + ctx_arrow<sizeof...(Is)>;
                throw SyntaxError(msg);
            };
        }

        template <size_t I>
        consteval void after_return_annotation() {
            if (ret.idx < N) {
                static constexpr static_str msg =
                    "arguments cannot follow return annotation\n\n" + indent +
                    ctx_str + "\n" + indent + ctx_arrow<I>;
                throw SyntaxError(msg);
            }
        }

        /* [1] standard arguments */
        template <size_t I, meta::arg A>
        consteval void parse(const A& arg) {
            duplicate_names<meta::arg_name<A>>(std::make_index_sequence<I>{});
            after_return_annotation<I>();

            list& sec = I < Spec.size() ? compile_time : run_time;

            // arguments must come before variadic keyword packs in the same section
            if (sec.kwargs.offset < N) {
                static constexpr static_str msg =
                    "arguments cannot follow variadic keyword pack\n\n" + indent +
                    ctx_str + "\n" + indent + ctx_arrow<I>;
                throw SyntaxError(msg);
            }

            // if the argument comes after this section's '*', then it is keyword-only
            if (sec.args.offset < N || sec.kwonly.offset < N) {
                ++sec.kwonly.n;
                params[I].kind = impl::arg_kind::KW;
                if constexpr (meta::bound_arg<A>) {
                    params[I].kind = params[I].kind | impl::arg_kind::OPT;
                }

            // otherwise, parse it as positional or keyword.  If a positional-only
            // separator is encountered later, then this will be moved to the posonly
            // portion of this section instead.
            } else {
                ++sec.pos_or_kw.n;
                params[I].kind = impl::arg_kind::POS | impl::arg_kind::KW;
                if constexpr (meta::bound_arg<A>) {
                    params[I].kind = params[I].kind | impl::arg_kind::OPT;
                } else if constexpr (I < Spec.size()) {
                    required_after_optional<0>(std::make_index_sequence<I>{});
                } else {
                    required_after_optional<Spec.size()>(
                        std::make_index_sequence<I - Spec.size()>{}
                    );
                }
            }

            params[I].name = meta::arg_name<A>;
            params[I].index = I;
            params[I].partial = meta::partials<A>::size;
            if constexpr (!meta::typed_arg<A>) {
                params[I].kind = params[I].kind | impl::arg_kind::UNTYPED;
            }
            if constexpr (I >= Spec.size()) {
                params[I].kind = params[I].kind | impl::arg_kind::RUNTIME;
            }
        }

        /* [2] positional-only separator ("/") */
        template <size_t I, meta::positional_only_separator A>
        consteval void parse(const A& arg) {
            duplicate_names<meta::arg_name<A>>(std::make_index_sequence<I>{});
            after_return_annotation<I>();

            list& sec = I < Spec.size() ? compile_time : run_time;

            // positional-only separator must come before '*'
            if (sec.kwonly.offset < N || sec.args.offset < N || sec.kwargs.offset < N) {
                static constexpr static_str msg =
                    "positional-only separator '/' must be ahead of '*'\n\n" + indent +
                    ctx_str + "\n" + indent + ctx_arrow<I>;
                throw SyntaxError(msg);
            }

            // there can only be one positional-only separator
            if (sec.posonly.offset < N) {
                static constexpr static_str msg =
                    "signature cannot contain more than one positional-only separator "
                    "'/'\n\n" + indent + ctx_str + "\n" + indent + ctx_arrow<I>;
                throw SyntaxError(msg);
            }

            // pos_or_kw -> posonly
            sec.posonly.offset = 0;
            sec.posonly.n = sec.pos_or_kw.n;
            sec.pos_or_kw.offset = I + 1;
            sec.pos_or_kw.n = 0;

            params[I].name = meta::arg_name<A>;
            params[I].index = I;
            params[I].partial = meta::partials<A>::size;
            params[I].kind = 0;
            size_t start = 0;
            if constexpr (!meta::typed_arg<A>) {
                params[I].kind = impl::arg_kind::UNTYPED;
            }
            if constexpr (I >= Spec.size()) {
                sec.pos_or_kw.offset -= Spec.size();
                start = Spec.size();
                params[I].kind = impl::arg_kind::RUNTIME;
            }

            // strip kw flag from previous arguments in this section
            for (size_t i = start; i < I; ++i) {
                params[i].kind = params[i].kind & ~impl::arg_kind::KW;
            }
        }

        /* [3] keyword-only separator ("*") or variadic positional ("*args"). */
        template <size_t I, typename A>
            requires (meta::keyword_only_separator<A> || meta::variadic_positional<A>)
        consteval void parse(const A& arg) {
            duplicate_names<meta::arg_name<A>>(std::make_index_sequence<I>{});
            after_return_annotation<I>();

            list& sec = I < Spec.size() ? compile_time : run_time;

            // keyword-only separator must come before variadic keyword arguments
            if (sec.kwargs.offset < N) {
                static constexpr static_str msg =
                    "'*' must be ahead of '**'\n\n" + indent + ctx_str + "\n" +
                    indent + ctx_arrow<I>;
                throw SyntaxError(msg);
            }

            // there can only be one keyword-only separator or variadic positional pack
            if (sec.kwonly.offset < N || sec.args.offset < N) {
                static constexpr static_str msg =
                    "signature cannot contain more than one '*'\n\n" + indent +
                    ctx_str + "\n" + indent + ctx_arrow<I>;
                throw SyntaxError(msg);
            }

            // subsequent arguments populate the keyword-only section
            sec.kwonly.offset = I + 1;
            if constexpr (meta::keyword_only_separator<A>) {
                params[I].kind = 0;
            } else {
                sec.args.offset = I;
                if constexpr (I >= Spec.size()) {
                    sec.args.offset -= Spec.size();
                }
                params[I].kind = impl::arg_kind::VAR | impl::arg_kind::POS;
            }

            params[I].name = meta::arg_name<A>;
            params[I].index = I;
            params[I].partial = meta::partials<A>::size;
            if constexpr (!meta::typed_arg<A>) {
                params[I].kind = params[I].kind | impl::arg_kind::UNTYPED;
            }
            if constexpr (I >= Spec.size()) {
                sec.kwonly.offset -= Spec.size();
                params[I].kind = params[I].kind | impl::arg_kind::RUNTIME;
            }
        }

        /* [4] variadic keyword arguments ("**kwargs") */
        template <size_t I, meta::variadic_keyword A>
        consteval void parse(const A& arg) {
            duplicate_names<meta::arg_name<A>>(std::make_index_sequence<I>{});
            after_return_annotation<I>();

            list& sec = I < Spec.size() ? compile_time : run_time;

            // there can only be one variadic keyword pack
            if (sec.kwargs.offset < N) {
                static constexpr static_str msg =
                    "signature cannot contain more than one variadic keyword pack "
                    "'**'\n\n" + indent + ctx_str + "\n" + indent + ctx_arrow<I>;
                throw SyntaxError(msg);
            }

            sec.kwargs.offset = I;
            params[I].name = meta::arg_name<A>;
            params[I].index = I;
            params[I].partial = meta::partials<A>::size;
            params[I].kind = impl::arg_kind::VAR | impl::arg_kind::KW;
            if constexpr (!meta::typed_arg<A>) {
                params[I].kind = params[I].kind | impl::arg_kind::UNTYPED;
            }
            if constexpr (I >= Spec.size()) {
                sec.kwargs.offset -= Spec.size();
                params[I].kind = params[I].kind | impl::arg_kind::RUNTIME;
            }
        }

        /* [5] return annotations ("->") */
        template <size_t I, meta::return_annotation A>
        consteval void parse(const A& arg) {
            duplicate_names<meta::arg_name<A>>(std::make_index_sequence<I>{});

            // return annotation cannot be a template argument
            if constexpr (I < Spec.size()) {
                static constexpr static_str msg =
                    "return annotation must not be a template argument\n\n" +
                    indent + ctx_str + "\n" + indent + ctx_arrow<I>;
                throw SyntaxError(msg);
            }

            // there can only be one return annotation
            if (ret.idx < N) {
                throw SyntaxError(
                    "signature cannot contain more than one return annotation"
                );
            }

            // return annotations are meaningless unless they are typed
            if constexpr (meta::typed_arg<A>) {
                ret.idx = I;
            } else {
                throw SyntaxError("return annotation must be typed");
            }

            params[I].name = meta::arg_name<A>;
            params[I].index = I;
            params[I].partial = meta::partials<A>::size;
            params[I].kind = impl::arg_kind::RUNTIME;
        }
    };

    /// TODO: The 2 remaining big feature pieces are comparison operators between signatures,
    /// which will allow for topological sorting, and a way to convert the signature into a
    /// string representation that can be used in error messages, or to generate .pyi
    /// interface files for C++ functions.  Then, the C++ side of functions will be done,
    /// and I can possibly convert all existing operators into `def` statements.

    /* Represents a completed Python-like signature composed of the templated argument
    annotations.  An enabled `bertrand::signature<F>` class always inherits from this
    type to avoid re-implementing the complex function internals. */
    template <templates Spec, meta::arg auto... Args>
    struct signature : signature_tag {
        static constexpr bool enable = true;
        using size_type = size_t;
        using index_type = ssize_t;
        using info_type = impl::signature_info<Spec, Args...>;
        using partial_type = impl::extract_partial<decltype(Args)...>::type;
        using bind_error = meta::detail::bind_error;

        /* Stores the validated indices and parsed kinds for each element of the
        templated argument signature.  The constructor may issue a compile-time
        `SyntaxError` if the argument list is malformed in some way. */
        static constexpr info_type info {std::make_index_sequence<info_type::N>{}};

        /* The total number of argument annotations (template + runtime) that are
        included in this signature, as an unsigned integer. */
        [[nodiscard]] static constexpr size_type size() noexcept { return info_type::N; }

        /* The total number of argument annotations (template + runtime) that are
        included in this signature, as a nsigned integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }

        /* True if the signature is completely empty, containing no parameters.  Such
        a function would by definition accept zero arguments and return an
        indeterminate type.  For the vast majority of functions, this will be false. */
        [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }

    private:
        static constexpr auto names = []<
            size_type... c_posonly,
            size_type... c_pos_or_kw,
            size_type... c_kwonly,
            size_type... c_args,
            size_type... c_kwargs,
            size_type... posonly,
            size_type... pos_or_kw,
            size_type... kwonly,
            size_type... args,
            size_type... kwargs
        >(
            std::index_sequence<c_posonly...>,
            std::index_sequence<c_pos_or_kw...>,
            std::index_sequence<c_kwonly...>,
            std::index_sequence<c_args...>,
            std::index_sequence<c_kwargs...>,
            std::index_sequence<posonly...>,
            std::index_sequence<pos_or_kw...>,
            std::index_sequence<kwonly...>,
            std::index_sequence<args...>,
            std::index_sequence<kwargs...>
        ) {
            return string_map<
                size_type,
                meta::arg_name<decltype(Spec.args.template get<
                    info.compile_time.posonly.offset + c_posonly
                >())>...,
                meta::arg_name<decltype(Spec.args.template get<
                    info.compile_time.pos_or_kw.offset + c_pos_or_kw
                >())>...,
                meta::arg_name<decltype(Spec.args.template get<
                    info.compile_time.kwonly.offset + c_kwonly
                >())>...,
                meta::arg_name<decltype(Spec.args.template get<
                    info.compile_time.args.offset + c_args
                >())>...,
                meta::arg_name<decltype(Spec.args.template get<
                    info.compile_time.kwargs.offset + c_kwargs
                >())>...,
                meta::arg_name<decltype(meta::unpack_value<
                    info.run_time.posonly.offset + posonly,
                    Args...
                >)>...,
                meta::arg_name<decltype(meta::unpack_value<
                    info.run_time.pos_or_kw.offset + pos_or_kw,
                    Args...
                >)>...,
                meta::arg_name<decltype(meta::unpack_value<
                    info.run_time.kwonly.offset + kwonly,
                    Args...
                >)>...,
                meta::arg_name<decltype(meta::unpack_value<
                    info.run_time.args.offset + args,
                    Args...
                >)>...,
                meta::arg_name<decltype(meta::unpack_value<
                    info.run_time.kwargs.offset + kwargs,
                    Args...
                >)>...
            >{
                (info.compile_time.posonly.offset + c_posonly)...,
                (info.compile_time.pos_or_kw.offset + c_pos_or_kw)...,
                (info.compile_time.kwonly.offset + c_kwonly)...,
                (info.compile_time.args.offset + c_args)...,
                (info.compile_time.kwargs.offset + c_kwargs)...,
                (Spec.size() + info.run_time.posonly.offset + posonly)...,
                (Spec.size() + info.run_time.pos_or_kw.offset + pos_or_kw)...,
                (Spec.size() + info.run_time.kwonly.offset + kwonly)...,
                (Spec.size() + info.run_time.args.offset + args)...,
                (Spec.size() + info.run_time.kwargs.offset + kwargs)...
            };
        }(
            std::make_index_sequence<info.compile_time.posonly.n>{},
            std::make_index_sequence<info.compile_time.pos_or_kw.n>{},
            std::make_index_sequence<info.compile_time.kwonly.n>{},
            std::make_index_sequence<info.compile_time.args.offset < size()>{},
            std::make_index_sequence<info.compile_time.kwargs.offset < size()>{},
            std::make_index_sequence<info.run_time.posonly.n>{},
            std::make_index_sequence<info.run_time.pos_or_kw.n>{},
            std::make_index_sequence<info.run_time.kwonly.n>{},
            std::make_index_sequence<info.run_time.args.offset < size()>{},
            std::make_index_sequence<info.run_time.kwargs.offset < size()>{}
        );

        template <size_type I>
        struct param {
            static constexpr auto arg = info.template get<I>();
            static constexpr size_type partial = info.params[I].partial;
            static constexpr size_type index = I;
            static constexpr static_str name = meta::arg_name<decltype(arg)>;
            static constexpr impl::arg_kind kind = info.params[I].kind;
            static constexpr bool has_value() noexcept {
                return partial > 0 || kind.optional();
            }
        };

        template <typename Self, size_type I>
        struct access {
            static constexpr auto arg = info.template get<I>();
            static constexpr size_type partial = info.params[I].partial;
            static constexpr size_type index = I;
            static constexpr static_str name = meta::arg_name<decltype(arg)>;
            static constexpr impl::arg_kind kind = info.params[I].kind;
            meta::remove_rvalue<Self> signature;

            static constexpr bool has_value() noexcept {
                return partial > 0 || kind.optional();
            }

            static constexpr decltype(auto) value()
                noexcept (requires{{arg.value()} noexcept;})
                requires (partial == 0 && kind.optional())
            {
                return arg.value();
            }

            static constexpr decltype(auto) value()
                noexcept (requires{
                    {Spec.partial.template get<Spec.template index<I>()>()} noexcept;
                })
                requires (partial > 0 && I < Spec.size() && !kind.variadic() && requires{
                    {Spec.partial.template get<Spec.template index<I>()>()};
                })
            {
                return (Spec.partial.template get<Spec.template index<I>()>());
            }

            static constexpr decltype(auto) value()
                noexcept (requires{{template_args(std::make_index_sequence<partial>{})} noexcept;})
                requires (partial > 0 && I < Spec.size() && kind.variadic() && requires{
                    {template_args(std::make_index_sequence<partial>{})};
                })
            {
                return (template_args(std::make_index_sequence<partial>{}));
            }

            template <typename S> requires (partial > 0 && I >= Spec.size() && !kind.variadic())
            constexpr decltype(auto) value(this S&& self)
                noexcept (requires{{std::forward<S>(self).signature.partial.template get<
                    impl::partial_idx<I - Spec.size(), decltype(Args)...>
                >()} noexcept;})
                requires (requires{{std::forward<S>(self).signature.partial.template get<
                    impl::partial_idx<I - Spec.size(), decltype(Args)...>
                >()};})
            {
                return (std::forward<S>(self).signature.partial.template get<
                    impl::partial_idx<I - Spec.size(), decltype(Args)...>
                >());
            }

            template <typename S> requires (partial > 0 && I >= Spec.size() && kind.variadic())
            constexpr decltype(auto) value(this S&& self)
                noexcept (requires{{runtime_args(std::make_index_sequence<partial>{})} noexcept;})
                requires (requires{{runtime_args(std::make_index_sequence<partial>{})};})
            {
                return (std::forward<S>(self).signature.partial.template get<
                    impl::partial_idx<I - Spec.size(), decltype(Args)...>
                >());
            }

        private:

            template <size_type... parts> requires (I < Spec.size())
            static constexpr decltype(auto) template_args(std::index_sequence<parts...>)
                noexcept (requires{{bertrand::args{
                    Spec.partial.template get<Spec.template index<I>() + parts>()...
                }} noexcept;})
                requires (requires{{bertrand::args{
                    Spec.partial.template get<Spec.template index<I>() + parts>()...
                }};})
            {
                return bertrand::args{
                    Spec.partial.template get<Spec.template index<I>() + parts>()...
                };
            }

            template <typename S, size_type... parts> requires (I < Spec.size())
            constexpr decltype(auto) runtime_args(this S&& self, std::index_sequence<parts...>)
                noexcept (requires{{bertrand::args{
                    Spec.partial.template get<Spec.template index<I>() + parts>()...
                }} noexcept;})
                requires (requires{{bertrand::args{
                    Spec.partial.template get<Spec.template index<I>() + parts>()...
                }};})
            {
                return bertrand::args{
                    std::forward<S>(self).signature.partial.template get<
                        impl::partial_idx<I - Spec.size(), decltype(Args)...> + parts
                    >()...
                };
            }
        };

    public:
        using key_type = std::string_view;
        using mapped_type = info_type::param;
        using value_type = info_type::params_t::value_type;
        using reference = info_type::params_t::const_reference;
        using const_reference = info_type::params_t::const_reference;
        using pointer = info_type::params_t::const_pointer;
        using const_pointer = info_type::params_t::const_pointer;
        using iterator = info_type::params_t::const_iterator;
        using const_iterator = info_type::params_t::const_iterator;
        using reverse_iterator = info_type::params_t::const_reverse_iterator;
        using const_reverse_iterator = info_type::params_t::const_reverse_iterator;
        using slice = impl::slice<iterator>;
        using const_slice = impl::slice<const_iterator>;

        partial_type partial;

        /* Construct a signature with a given set of partial arguments.  The arguments
        must exactly match the specification given in the template list. */
        template <typename... Ts>
        [[nodiscard]] constexpr signature(Ts&&... partial)
            noexcept (requires{{partial_type(std::forward<Ts>(partial)...)} noexcept;})
            requires (requires{{partial_type(std::forward<Ts>(partial)...)};})
        :
            partial(std::forward<Ts>(partial)...)
        {}

        /* A pointer to the read-only parameter array being used for indexing and
        iteration over the signature.  Each element of the array is a homogenous
        descriptor type that contains the name, index, number of partially-applied
        values, and kind (positional-only, variadic, optional, typed, etc.) of each
        parameter. */
        [[nodiscard]] static constexpr pointer data() noexcept {
            return info.params.data();
        }

        /* Iterate over the parameter array. */
        [[nodiscard]] static constexpr iterator begin() noexcept {
            return info.params.begin();
        }
        [[nodiscard]] static constexpr iterator cbegin() noexcept {
            return info.params.cbegin();
        }
        [[nodiscard]] static constexpr iterator end() noexcept {
            return info.params.end();
        }
        [[nodiscard]] static constexpr iterator cend() noexcept {
            return info.params.cend();
        }
        [[nodiscard]] static constexpr reverse_iterator rbegin() noexcept {
            return info.params.rbegin();
        }
        [[nodiscard]] static constexpr reverse_iterator crbegin() noexcept {
            return info.params.crbegin();
        }
        [[nodiscard]] static constexpr reverse_iterator rend() noexcept {
            return info.params.rend();
        }
        [[nodiscard]] static constexpr reverse_iterator crend() noexcept {
            return info.params.crend();
        }

        /* Check whether the signature contains a given argument by name. */
        template <static_str name>
        [[nodiscard]] static constexpr bool contains() noexcept {
            return names.template contains<name>();
        }

        /* Check whether the signature contains a given argument by name. */
        [[nodiscard]] static constexpr bool contains(std::string_view name) noexcept {
            return names.contains(name);
        }

        /* Search the signature for an argument with the given name, returning an
        iterator to that parameter if it is present, or an `end()` iterator
        otherwise. */
        template <static_str name>
        [[nodiscard]] static constexpr iterator find() noexcept {
            if constexpr (contains<name>()) {
                return info.params.begin() + names.template get<name>();
            } else {
                return info.params.end();
            }
        }

        /* Search the signature for an argument with the given name, returning an
        iterator to that parameter if it is present, or an `end()` iterator
        otherwise. */
        [[nodiscard]] static constexpr iterator find(std::string_view kw) noexcept {
            if (const auto it = names.find(kw); it != names.end()) {
                return info.params.begin() + it->second;
            } else {
                return info.params.end();
            }
        }

        /* Return a parameter descriptor for the parameter at index I, applying
        Python-style wraparound for negative indices.  Fails to compile if the index
        would be out of range after normalization.

        Note that the parameter descriptor returned by this method is more detailed
        than those obtained by simple indexing or iteration, due to its ability to
        return a non-homogenous type.  In addition to the information provided by a
        simple parameter, these also grant access to the stored partial or default
        value for the parameter, if one exists, as well as the parameter's precise
        type if it was created with a type annotation. */
        template <index_type I, typename Self> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr auto get(this Self&& self)
            noexcept (requires{{access<
                Self,
                size_type(impl::normalize_index<ssize(), I>())
            >{std::forward<Self>(self)}} noexcept;})
            requires (requires{{access<
                Self,
                size_type(impl::normalize_index<ssize(), I>())
            >{std::forward<Self>(self)}};})
        {
            return access<
                Self,
                size_type(impl::normalize_index<ssize(), I>())
            >{std::forward<Self>(self)};
        }

        /* Return a parameter descriptor for a named argument, assuming it is present
        in the signature.  Fails to compile otherwise.
        
        Note that the parameter descriptor returned by this method is more detailed
        than those obtained by simple indexing or iteration, due to its ability to
        return a non-homogenous type.  In addition to the information provided by a
        simple parameter, these also grant access to the stored partial or default
        value for the parameter, if one exists, as well as the parameter's precise
        type if it was created with a type annotation. */
        template <static_str name, typename Self> requires (contains<name>())
        [[nodiscard]] constexpr auto get(this Self&& self)
            noexcept (requires{{access<Self, names.template get<name>()>{
                std::forward<Self>(self)
            }} noexcept;})
            requires (requires{{access<Self, names.template get<name>()>{
                std::forward<Self>(self)
            }};})
        {
            return access<Self, names.template get<name>()>{std::forward<Self>(self)};
        }

        /* Index into the signature, returning an `Optional<T>`, where `T` is a simple
        parameter descriptor.  An empty optional indicates that the index was out of
        range after normalization. */
        [[nodiscard]] static constexpr Optional<reference> get(index_type idx)
            noexcept (requires{{Optional<reference>{
                info.params[idx + (ssize() * (idx < 0))]
            }} noexcept;})
            requires (requires{{Optional<reference>{
                info.params[idx + (ssize() * (idx < 0))]
            }};})
        {
            index_type i = idx + (ssize() * (idx < 0));
            if (i < 0 || i >= ssize()) {
                return None;
            } else {
                return {info.params[i]};
            }
        }

        /* Search the signature for an argument with the given name.  Returns an
        `Optional<T>`, where `T` is a simple parameter descriptor.  An empty optional
        indicates that the argument name is not present in the signature. */
        [[nodiscard]] static constexpr Optional<reference> get(std::string_view name)
            noexcept (requires{{Optional<reference>{info.params[names[name]]}} noexcept;})
            requires (requires{{Optional<reference>{info.params[names[name]]}};})
        {
            if (const auto it = names.find(name); it != names.end()) {
                return {info.params[it->second]};
            } else {
                return None;
            }
        }

        /* Index into the signature, returning a simple parameter descriptor that
        includes its name, index, numer of partial values, and kind.  May throw an
        `IndexError` if the index is out of bounds after normalization.  Use
        `contains()` or `get()` to avoid the error. */
        [[nodiscard]] static constexpr reference operator[](index_type idx)
            noexcept (requires{
                {info.params[impl::normalize_index(ssize(), idx)]} noexcept;
            })
        {
            return info.params[impl::normalize_index(ssize(), idx)];
        }

        /* Slice the signature, returning a view over a specific subset of the
        parameter list. */
        [[nodiscard]] constexpr slice operator[](bertrand::slice s)
            noexcept (requires{
                {slice{info.params, s.normalize(ssize())}} noexcept;
            })
        {
            return {*this, s.normalize(ssize())};
        }

        /* Search the signature for an argument with the given name.  Returns a simple
        parameter descriptor that includes its name, index in the signature, number of
        partial values, and kind.  May throw a `KeyError` if the keyword name is not
        present.  Use `contains()` or `get()` to avoid the error. */
        [[nodiscard]] static constexpr reference operator[](
            std::string_view kw
        ) noexcept (requires{{info.params[names[kw]]} noexcept;}) {
            return info.params[names[kw]];
        }

        /* Return an iterator to the specified index of the signature, applying
        Python-style wraparound if necessary.  If the index is out of range after
        normalizing, then an `end()` iterator will be returned instead. */
        [[nodiscard]] static constexpr iterator at(index_type idx) noexcept {
            index_type i = idx + (ssize() * (idx < 0));
            return (i < 0 || i >= ssize()) ? end() : info.params.begin() + i;
        }

        /// TODO: to_string(), which should be able to produce strings in many forms.
        /// It's possible that these strings would be used in the constexpr exceptions
        /// issued by the binding logic.

    private:
        // Count the number of positional values to the right of `J` in `A...`.
        // Returns zero if no positional arguments remain in `A...`.  Note that a
        // positional unpacking operator counts as a single positional argument.
        template <typename...>
        static constexpr size_type _consume_positional = 0;
        template <typename A, typename... As> requires (!meta::arg<A> || meta::arg_pack<A>)
        static constexpr size_type _consume_positional<A, As...> = _consume_positional<As...> + 1;
        template <size_type, typename, typename... A>
        static constexpr size_type _unpack_positional = 0;
        template <size_type J, size_type... Js, typename... A>
        static constexpr size_type _unpack_positional<J, std::index_sequence<Js...>, A...> =
            _consume_positional<meta::unpack_type<J + Js, A...>...>;
        template <size_type J, typename... A> requires (J <= sizeof...(A))
        static constexpr size_type consume_positional =
            _unpack_positional<J, std::make_index_sequence<sizeof...(A) - J>, A...>;

        // Check to see if there is a keyword with a specific name to the right of `J`
        // in `A...`.  If such an argument exists, then returns its index in `A...`,
        // otherwise returns `sizeof...(A)`.  Note that a keyword unpacking operator
        // will always match if it is present.
        template <const auto&, typename...>
        static constexpr size_type _consume_keyword = 0;
        template <const auto& name, typename A, typename... As>
            requires (!(meta::bound_arg<A> && meta::arg_name<A> == name) && !meta::kwarg_pack<A>)
        static constexpr size_type _consume_keyword<name, A, As...> =
            _consume_keyword<name, As...> + 1;
        template <const auto&, size_type, typename, typename...>
        static constexpr size_type _unpack_keyword = 0;
        template <const auto& name, size_type J, size_type... Js, typename... A>
        static constexpr size_type _unpack_keyword<name, J, std::index_sequence<Js...>, A...> =
            _consume_keyword<name, meta::unpack_type<J + Js, A...>...> + J;
        template <size_type I, size_type J, typename... A>
            requires (I < size() && J <= sizeof...(A))
        static constexpr size_type consume_keyword = _unpack_keyword<
            meta::arg_name<decltype(info.template get<I>())>,
            J,
            std::make_index_sequence<sizeof...(A) - J>,
            A...
        >;

        // Get the current `partial_type` index given an in-flight list of bound
        // arguments `args...`.  This is always equal to the sum of the partial sizes
        // for the first `sizeof...(args)` elements of `Args...`.
        template <size_type I> requires (I >= Spec.size())
        static constexpr size_type target_partial =
            impl::partial_idx<I - Spec.size(), decltype(Args)...>;

        // Get the current source index for an in-flight list of bound arguments
        // `args...`.  This is always equal to the sum of the partial sizes for the
        // first `sizeof...(args)` elements of `args...`, which can then be used to
        // index into an in-flight argument list for specialize<...>::bind.
        template <const auto&... Ts>
        static constexpr size_type source_partial =
            impl::partial_idx<sizeof...(Ts), decltype(Ts)...>;

        // Attempt to match a positional argument to the right of `J` in `A...`,
        // assuming the target argument at index `I` allows positional binding and has
        // no partial value.
        template <size_type I, size_type J, typename... A> requires (J <= sizeof...(A))
        static constexpr bool dispatch_positional =
            info.params[I].kind.pos() &&
            info.params[I].partial == 0 &&
            consume_positional<J, A...> > 0;

        // Attempt to match a keyword argument to the right of `J` in `A...`,
        // assuming the target argument at index `I` allows keyword binding, and has
        // no partial value.
        template <size_type I, size_type J, typename... A> requires (J <= sizeof...(A))
        static constexpr bool dispatch_keyword =
            (
                info.params[I].kind.kwonly() ||
                (info.params[I].kind.pos_or_kw() && !dispatch_positional<I, J, A...>)
            ) &&
            info.params[I].partial == 0 &&
            consume_keyword<I, J, A...> < sizeof...(A);

        // Attempt to match against all remaining positional arguments to the right of
        // `J` in `A...`, assuming that the target argument at index `I` allows
        // variadic positional binding.
        template <size_type I, size_type J, typename... A> requires (J <= sizeof...(A))
        static constexpr bool dispatch_args =
            info.params[I].kind.args() && consume_positional<J, A...> > 0;

        // Attempt to match against all remaining keyword arguments to the right of
        // `J` in `A...`, assuming that the target argument at index `I` allows
        // variadic keyword binding.
        template <size_type I, size_type J, typename... A> requires (J <= sizeof...(A))
        static constexpr bool dispatch_kwargs =
            info.params[I].kind.kwargs() && consume_positional<J, A...> < (sizeof...(A) - J);

        // Check to see whether an argument list `A...` satisfies some portion of the
        // signature listed as `param<I>` types.  This is a recursive algorithm with a
        // single `::error<A...>` entry point, which evaluates to a `bind_error` enum
        // with a value of `::ok` if the argument list is valid, or a specific error
        // code if not.  The result is checked as a template constraint before
        // initiating the binding process, which surfaces the error at compile time in
        // a SFINAE-friendly way.
        template <bool exhaustive, param...>
        struct validate {
            // no leftover arguments - all good.
            template <typename...>
            static constexpr bind_error error = bind_error::ok;

            // remove positional/keyword unpacking operators if they are still present
            template <typename A, typename... As>
                requires (meta::arg_pack<A> || meta::kwarg_pack<A>)
            static constexpr bind_error error<A, As...> = error<As...>;

            // if any other arguments are present, then they are by definition extras.
            // The type of the first such argument determines whether it is classified
            // as extra positional or extra keyword.
            template <typename A, typename... As>
                requires (!meta::arg_pack<A> && !meta::kwarg_pack<A>)
            static constexpr bind_error error<A, As...> = !meta::arg<A> ?
                bind_error::extra_positional :
                bind_error::extra_keyword;
        };
        template <bool exhaustive, param curr, param... rest>
        struct validate<exhaustive, curr, rest...> {
        private:
            // Apply a type check against `curr`, assuming it is typed.
            template <typename T>
            static constexpr bool type_check = true;
            template <typename T> requires (curr.kind.typed() && !meta::arg<T>)
            static constexpr bool type_check<T> =
                meta::convertible_to<T, meta::arg_type<decltype(curr.arg)>>;
            template <typename T> requires (curr.kind.typed() && meta::bound_arg<T>)
            static constexpr bool type_check<T> =
                meta::convertible_to<meta::arg_value<T>, meta::arg_type<decltype(curr.arg)>>;
            template <typename T> requires (curr.kind.typed() && meta::arg_pack<T>)
            static constexpr bool type_check<T> =
                meta::convertible_to<meta::yield_type<T>, meta::arg_type<decltype(curr.arg)>>;
            template <typename T> requires (curr.kind.typed() && meta::kwarg_pack<T>)
            static constexpr bool type_check<T> = meta::convertible_to<
                meta::get_type<T, curr.name>,
                meta::arg_type<decltype(curr.arg)>
            >;

            // Check to see whether any keyword arguments in `A...` are already
            // contained as partial variadic keyword arguments.  If so, then a
            // conflicting value error will occur.  Note that this only applies to
            // variadic keywords, since all other keywords are stored anonymously in
            // `partial_type`.
            template <typename...>
            static constexpr bool conflicts_with_partial = false;
            template <typename A, typename... As> requires (curr.kind.kwargs())
            static constexpr bool conflicts_with_partial<A, As...> =
                partial_type::template contains<meta::arg_name<A>>() ||
                Spec.partial.template contains<meta::arg_name<A>>() ||
                conflicts_with_partial<As...>;

            // (1) consume a positional argument or yield from a positional pack,
            // checking for conflicts with later keywords and possible type mismatches.
            template <typename A, typename... As>
            static constexpr bind_error positional =
                (curr.kind.kw() && _consume_keyword<curr.name, As...> < sizeof...(As)) ?
                    bind_error::conflicting_values :
                    bind_error::bad_type;
            template <typename A, typename... As>
                requires (
                    !meta::arg_pack<A> && type_check<A> &&
                    !(curr.kind.kw() && _consume_keyword<curr.name, As...> < sizeof...(As))
                )
            static constexpr bind_error positional<A, As...> =
                validate<exhaustive, rest...>::template error<As...>;  // consume positional
            template <typename A, typename... As>
                requires (
                    meta::arg_pack<A> && type_check<A> &&
                    !(curr.kind.kw() && _consume_keyword<curr.name, As...> < sizeof...(As))
                )
            static constexpr bind_error positional<A, As...> =
                validate<exhaustive, rest...>::template error<A, As...>;  // replace arg pack

            // (2) consume a keyword argument or search a keyword pack, checking for
            // possible type mismatches.
            template <typename, typename, typename... A>
            static constexpr bind_error keyword = bind_error::bad_type;
            template <size_type... prev, size_type... next, typename... A>
                requires (
                    !meta::kwarg_pack<meta::unpack_type<sizeof...(prev), A...>> &&
                    type_check<meta::unpack_type<sizeof...(prev), A...>>
                )
            static constexpr bind_error keyword<
                std::index_sequence<prev...>,
                std::index_sequence<next...>,
                A...
            > = validate<exhaustive, rest...>::template error<
                meta::unpack_type<prev, A...>...,
                meta::unpack_type<sizeof...(prev) + 1 + next, A...>...  // consume keyword
            >;
            template <size_type... prev, size_type... next, typename... A>
                requires (
                    meta::kwarg_pack<meta::unpack_type<sizeof...(prev), A...>> &&
                    type_check<meta::unpack_type<sizeof...(prev), A...>>
                )
            static constexpr bind_error keyword<
                std::index_sequence<prev...>,
                std::index_sequence<next...>,
                A...
            > = validate<exhaustive, rest...>::template error<
                meta::unpack_type<prev, A...>...,
                meta::unpack_type<sizeof...(prev), A...>,  // replace keyword pack
                meta::unpack_type<sizeof...(prev) + 1 + next, A...>...
            >;

            // (3) consume all remaining positional arguments to populate a variadic
            // positional pack, checking for possible type mismatches.
            template <typename, typename, typename...>
            static constexpr bind_error args = bind_error::bad_type;
            template <size_type... pos, size_type... kw, typename... A>
                requires (type_check<meta::unpack_type<pos, A...>> && ...)
            static constexpr bind_error args<
                std::index_sequence<pos...>,
                std::index_sequence<kw...>,
                A...
            > = validate<exhaustive, rest...>::template error<
                meta::unpack_type<sizeof...(pos) + kw, A...>...
            >;

            // (4) consume all remaining keyword arguments to populate a variadic
            // keyword pack, checking for conflicts with existing partial values and
            // possible type mismatches.
            template <typename, typename, typename...>
            static constexpr bind_error kwargs = bind_error::bad_type;
            template <size_type... pos, size_type... kw, typename... A>
                requires (conflicts_with_partial<meta::unpack_type<sizeof...(pos) + kw, A...>...>)
            static constexpr bind_error kwargs<
                std::index_sequence<pos...>,
                std::index_sequence<kw...>,
                A...
            > = bind_error::conflicting_values;
            template <size_type... pos, size_type... kw, typename... A>
                requires (
                    !conflicts_with_partial<meta::unpack_type<sizeof...(pos) + kw, A...>...> &&
                    ... &&
                    type_check<meta::unpack_type<sizeof...(pos) + kw, A...>>
                )
            static constexpr bind_error kwargs<
                std::index_sequence<pos...>,
                std::index_sequence<kw...>,
                A...
            > = validate<exhaustive, rest...>::template error<
                meta::unpack_type<pos, A...>...
            >;

            // (5) if the argument is partial or does not contain a match in `A...`,
            // then we have to interpret it according to `exhaustive`.  If `true`,
            // then unmatched, non-variadic arguments without default values are
            // considered errors, otherwise they are simply ignored.  Partial values
            // are always ignored, after checking for possible conflicts.
            template <typename... A>
            static constexpr bind_error unmatched = (
                exhaustive &&
                !(curr.partial > 0 || curr.kind.optional() || curr.kind.variadic())
            ) ?
                bind_error::missing_required :
                bind_error::conflicting_values;
            template <typename... A>
                requires ((
                    !exhaustive ||
                    curr.partial > 0 ||
                    curr.kind.optional() ||
                    curr.kind.variadic() ||
                    curr.kind.sentinel()
                ) && (!curr.kind.kw() || _consume_keyword<curr.name, A...> == sizeof...(A)))
            static constexpr bind_error unmatched<A...> =
                validate<exhaustive, rest...>::template error<A...>;

        public:
            template <typename... A>
            static constexpr bind_error error = unmatched<A...>;

            template <typename... A> requires (dispatch_positional<curr.index, 0, A...>)
            static constexpr bind_error error<A...> = positional<A...>;

            template <typename... A> requires (dispatch_keyword<curr.index, 0, A...>)
            static constexpr bind_error error<A...> = keyword<
                std::make_index_sequence<_consume_keyword<curr.name, A...>>,
                std::make_index_sequence<sizeof...(A) - (_consume_keyword<curr.name, A...> + 1)>,
                A...
            >;

            template <typename... A> requires (dispatch_args<curr.index, 0, A...>)
            static constexpr bind_error error<A...> = args<
                std::make_index_sequence<_consume_positional<A...>>,
                std::make_index_sequence<sizeof...(A) - _consume_positional<A...>>,
                A...
            >;

            template <typename... A> requires (dispatch_kwargs<curr.index, 0, A...>)
            static constexpr bind_error error<A...> = kwargs<
                std::make_index_sequence<_consume_positional<A...>>,
                std::make_index_sequence<sizeof...(A) - _consume_positional<A...>>,
                A...
            >;
        };

        template <typename, typename...>
        static constexpr bind_error _bind_templates = bind_error::ok;
        template <size_type... Is, typename... A>
        static constexpr bind_error _bind_templates<std::index_sequence<Is...>, A...> =
            validate<false, param<Is>{}...>::template error<A...>;

        template <typename, typename...>
        static constexpr bind_error _bind_params = bind_error::ok;
        template <size_type... Is, typename... A>
        static constexpr bind_error _bind_params<std::index_sequence<Is...>, A...> =
            validate<false, param<Spec.size() + Is>{}...>::template error<A...>;

        template <typename, typename...>
        static constexpr bind_error _call_templates = bind_error::ok;
        template <size_type... Is, typename... A>
        static constexpr bind_error _call_templates<std::index_sequence<Is...>, A...> =
            validate<true, param<Is>{}...>::template error<A...>;

        template <typename, typename...>
        static constexpr bind_error _call_params = bind_error::ok;
        template <size_type... Is, typename... A>
        static constexpr bind_error _call_params<std::index_sequence<Is...>, A...> =
            validate<true, param<Spec.size() + Is>{}...>::template error<A...>;

        /* Recursive case: parse an explicit template parameter list `T...` that was
        provided to the signature's `bind()` method.  Evaluates to a function object
        that can be invoked to complete the binding for a set of runtime arguments
        `A...`. */
        template <size_type I, auto... T>
        struct _bind;

        // (1) partials and unmatched arguments are inserted as-is.  Partial
        // keywords are also checked for conflicts with existing values.
        template <typename, typename, auto...>
        struct bind_default;
        template <size_type... prev, size_type... next, auto... T>
        struct bind_default<std::index_sequence<prev...>, std::index_sequence<next...>, T...> {
            using type = _bind<
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                Spec.args.template get<sizeof...(prev)>(),
                meta::unpack_value<sizeof...(prev) + next, T...>...
            >;
        };
        template <size_type I, auto... T>
        struct _bind : bind_default<
            std::make_index_sequence<I>,
            std::make_index_sequence<sizeof...(T) - I>,
            T...
        >::type {};

        // (2) matching positional arguments are bound and then forwarded, after
        // checking for a type mismatch or conflicting keyword.
        template <typename, typename, auto...>
        struct bind_pos;
        template <size_type... prev, size_type... next, auto... T>
        struct bind_pos<std::index_sequence<prev...>, std::index_sequence<next...>, T...> {
            using type = _bind<
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                Spec.template make_partial<sizeof...(prev)>(
                    meta::unpack_value<sizeof...(prev), T...>
                ),
                meta::unpack_value<sizeof...(prev) + 1 + next, T...>...
            >;
        };
        template <size_type I, auto... T>
            requires (I < Spec.size() && dispatch_positional<I, I, decltype(T)...>)
        struct _bind<I, T...> : bind_pos<
            std::make_index_sequence<I>,
            std::make_index_sequence<sizeof...(T) - (I + 1)>,
            T...
        >::type {};

        // (3) keywords must be removed from their current location, unwrapped to
        // their underlying values, bound, and then forwarded.
        template <typename, typename, typename, auto...>
        struct bind_kw;
        template <size_type... prev, size_type... middle, size_type... next, auto... T>
        struct bind_kw<
            std::index_sequence<prev...>,
            std::index_sequence<middle...>,
            std::index_sequence<next...>,
            T...
        > {
            using type = _bind<
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                Spec.template make_partial<sizeof...(prev)>(
                    meta::unpack_value<
                        consume_keyword<sizeof...(prev), sizeof...(prev), decltype(T)...>,
                        T...
                    >.value()
                ),
                meta::unpack_value<sizeof...(prev) + middle, T...>...,
                meta::unpack_value<
                    consume_keyword<sizeof...(prev), sizeof...(prev), decltype(T)...> + 1 + next,
                    T...
                >...
            >;
        };
        template <size_type I, auto... T>
            requires (I < Spec.size() && dispatch_keyword<I, I, decltype(T)...>)
        struct _bind<I, T...> : bind_kw<
            std::make_index_sequence<I>,
            std::make_index_sequence<consume_keyword<I, I, decltype(T)...> - I>,
            std::make_index_sequence<sizeof...(T) - (consume_keyword<I, I, decltype(T)...> + 1)>,
            T...
        >::type {};

        // (4) variadic positional arguments will consume all remaining positional
        // parameters, and concatenate them with any existing partial values.  The
        // result then gets inserted as a single argument.
        template <typename, typename, typename, auto...>
        struct bind_args;
        template <size_type... prev, size_type... pos, size_type... kw, auto... T>
        struct bind_args<
            std::index_sequence<prev...>,
            std::index_sequence<pos...>,
            std::index_sequence<kw...>,
            T...
        > {
            using type = _bind<
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                Spec.template make_partial<sizeof...(prev)>(
                    meta::unpack_value<sizeof...(prev) + pos, T...>...
                ),
                meta::unpack_value<sizeof...(prev) + sizeof...(pos) + kw, T...>...
            >;
        };
        template <size_type I, auto... T>
            requires (I < Spec.size() && dispatch_args<I, I, decltype(T)...>)
        struct _bind<I, T...> : bind_args<
            std::make_index_sequence<I>,
            std::make_index_sequence<consume_positional<I, decltype(T)...>>,
            std::make_index_sequence<sizeof...(T) - (I + consume_positional<I, decltype(T)...>)>,
            T...
        >::type {};

        // (5) variadic keyword arguments will consume all remaining keyword
        // parameters, assert that they do not conflict with any existing partials,
        // and then concatenate them.  The result then gets inserted as a single
        // argument.
        template <typename, typename, typename, auto...>
        struct bind_kwargs;
        template <size_type... prev, size_type... pos, size_type... kw, auto... T>
        struct bind_kwargs<
            std::index_sequence<prev...>,
            std::index_sequence<pos...>,
            std::index_sequence<kw...>,
            T...
        > {
            using type = _bind<
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                Spec.template make_partial<sizeof...(prev)>(
                    meta::unpack_value<sizeof...(prev) + sizeof...(pos) + kw, T...>...
                ),
                meta::unpack_value<sizeof...(prev) + pos, T...>...
            >;
        };
        template <size_type I, auto... T>
            requires (I < Spec.size() && dispatch_kwargs<I, I, decltype(T)...>)
        struct _bind<I, T...> : bind_kwargs<
            std::make_index_sequence<I>,
            std::make_index_sequence<consume_positional<I, decltype(T)...>>,
            std::make_index_sequence<sizeof...(T) - (I + consume_positional<I, decltype(T)...>)>,
            T...
        >::type {};

        /* Base case: once all parameters in `Spec` have been matched, `T...` will
        contain a valid initializer for the template portion of a new (partial)
        signature object. */
        template <auto... T>
        struct _bind<Spec.size(), T...> {
            /* A function object that partially applies a given argument list to the
            enclosing signature.  This works by building up the `Ts...` pack with
            modified annotations for all the runtime arguments in `Args`..., such that
            the resulting partial signature is formed by the concatenation of the
            explicit template arguments `T...` and `Ts...`. */
            template <auto... Ts>
            struct fn {
            private:
                static constexpr size_type I = Spec.size() + sizeof...(Ts);
                static constexpr size_type J = source_partial<Ts...>;
                static constexpr param<I> curr;

                template <
                    size_type... prev,
                    size_type... parts,
                    size_type... next,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) bind_partial(
                    std::index_sequence<prev...>,
                    std::index_sequence<parts...>,
                    std::index_sequence<next...>,
                    P&& partial,
                    A&&... args
                )
                    noexcept (requires{{fn<Ts..., curr.arg>{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    )} noexcept;})
                    requires (requires{{fn<Ts..., curr.arg>{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    )};})
                {
                    return (fn<Ts..., curr.arg>{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    ));
                }

                template <typename P, typename... A>
                static constexpr decltype(auto) bind_pos(P&& partial, A&&... args)
                    noexcept (requires{{fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<J, A...>>(curr.arg)
                    >{}(std::forward<P>(partial), std::forward<A>(args)...)} noexcept;})
                    requires (requires{{fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<J, A...>>(curr.arg)
                    >{}(std::forward<P>(partial), std::forward<A>(args)...)};})
                {
                    return (fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<J, A...>>(curr.arg)
                    >{}(std::forward<P>(partial), std::forward<A>(args)...));
                }

                template <
                    size_type... prev,
                    size_type... middle,
                    size_type... next,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) bind_kw(
                    std::index_sequence<prev...>,
                    std::index_sequence<middle...>,
                    std::index_sequence<next...>,
                    P&& partial,
                    A&&... args
                )
                    noexcept (requires{{fn<
                        Ts...,
                        impl::make_partial<
                            meta::arg_value<meta::unpack_type<consume_keyword<I, J, A...>, A...>>
                        >(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, J, A...>>(
                            std::forward<A>(args)...
                        ).value(),
                        meta::unpack_arg<J + middle>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, J, A...> + 1 + next>(
                            std::forward<A>(args)...
                        )...
                    )} noexcept;})
                    requires (requires{{fn<
                        Ts...,
                        impl::make_partial<
                            meta::arg_value<meta::unpack_type<consume_keyword<I, J, A...>, A...>>
                        >(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, J, A...>>(
                            std::forward<A>(args)...
                        ).value(),
                        meta::unpack_arg<J + middle>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, J, A...> + 1 + next>(
                            std::forward<A>(args)...
                        )...
                    )};})
                {
                    return (fn<
                        Ts...,
                        impl::make_partial<
                            meta::arg_value<meta::unpack_type<consume_keyword<I, J, A...>, A...>>
                        >(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, J, A...>>(
                            std::forward<A>(args)...
                        ).value(),
                        meta::unpack_arg<J + middle>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, J, A...> + 1 + next>(
                            std::forward<A>(args)...
                        )...
                    ));
                }

                template <
                    size_type... prev,
                    size_type... parts,
                    size_type... pos,
                    size_type... kw,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) bind_args(
                    std::index_sequence<prev...>,
                    std::index_sequence<parts...>,
                    std::index_sequence<pos...>,
                    std::index_sequence<kw...>,
                    P&& partial,
                    A&&... args
                )
                    noexcept (requires{{fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<J + pos, A...>...>(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<J + pos>(std::forward<A>(args)...)...,
                        meta::unpack_arg<J + sizeof...(pos) + kw>(std::forward<A>(args)...)...
                    )} noexcept;})
                    requires (requires{{fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<J + pos, A...>...>(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<J + pos>(std::forward<A>(args)...)...,
                        meta::unpack_arg<J + sizeof...(pos) + kw>(std::forward<A>(args)...)...
                    )};})
                {
                    return (fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<J + pos, A...>...>(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<J + pos>(std::forward<A>(args)...)...,
                        meta::unpack_arg<J + sizeof...(pos) + kw>(std::forward<A>(args)...)...
                    ));
                }

                template <
                    size_type... prev,
                    size_type... parts,
                    size_type... pos,
                    size_type... kw,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) bind_kwargs(
                    std::index_sequence<prev...>,
                    std::index_sequence<parts...>,
                    std::index_sequence<pos...>,
                    std::index_sequence<kw...>,
                    P&& partial,
                    A&&... args
                )
                    noexcept (requires{{fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<
                            J + sizeof...(pos) + kw,
                            A...
                        >...>(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<J + pos>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<J + sizeof...(pos) + kw>(std::forward<A>(args)...)...
                    )} noexcept;})
                    requires (requires{{fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<
                            J + sizeof...(pos) + kw,
                            A...
                        >...>(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<J + pos>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<J + sizeof...(pos) + kw>(std::forward<A>(args)...)...
                    )};})
                {
                    return (fn<
                        Ts...,
                        impl::make_partial<meta::unpack_type<
                            J + sizeof...(pos) + kw,
                            A...
                        >...>(curr.arg)
                    >{}(
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<J + pos>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<J + sizeof...(pos) + kw>(std::forward<A>(args)...)...
                    ));
                }

            public:
                // [0] non-partial arguments are perfectly forwarded.
                template <typename P, typename... A>
                    requires (
                        curr.partial == 0 &&
                        !dispatch_positional<I, J, A...> &&
                        !dispatch_keyword<I, J, A...> &&
                        !dispatch_args<I, J, A...> &&
                        !dispatch_kwargs<I, J, A...>
                    )
                static constexpr decltype(auto) operator()(P&& partial, A&&... args)
                    noexcept (requires{{fn<Ts..., curr.arg>{}(
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{fn<Ts..., curr.arg>{}(
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (fn<Ts..., curr.arg>{}(
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                // [1] partial arguments are perfectly forwarded, along with their
                // current partial value(s).
                template <typename P, typename... A>
                    requires (
                        curr.partial > 0 &&
                        !dispatch_positional<I, J, A...> &&
                        !dispatch_keyword<I, J, A...> &&
                        !dispatch_args<I, J, A...> &&
                        !dispatch_kwargs<I, J, A...>
                    )
                static constexpr decltype(auto) operator()(P&& partial, A&&... args)
                    noexcept (requires{{bind_partial(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{bind_partial(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (bind_partial(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                // [2] matching positional arguments are marked in the final template
                // signature and then trivially forwarded.
                template <typename P, typename... A> requires (dispatch_positional<I, J, A...>)
                static constexpr decltype(auto) operator()(P&& partial, A&&... args)
                    noexcept (requires{{bind_pos(
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{bind_pos(
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (bind_pos(
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                // [3] keywords must be removed from their current location, unwrapped
                // to their underlying values, marked in the final template signature,
                // and then forwarded.
                template <typename P, typename... A> requires (dispatch_keyword<I, J, A...>)
                static constexpr decltype(auto) operator()(P&& partial, A&&... args)
                    noexcept (requires{{bind_kw(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consume_keyword<I, J, A...> - J>{},
                        std::make_index_sequence<sizeof...(A) - (consume_keyword<I, J, A...> + 1)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{bind_kw(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consume_keyword<I, J, A...> - J>{},
                        std::make_index_sequence<sizeof...(A) - (consume_keyword<I, J, A...> + 1)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (bind_kw(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consume_keyword<I, J, A...> - J>{},
                        std::make_index_sequence<sizeof...(A) - (consume_keyword<I, J, A...> + 1)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                // [5] variadic positional arguments will consume all remaining
                // positional parameters and concatenate them with any existing
                // partial values.  The result then gets inserted as a single argument.
                template <typename P, typename... A> requires (dispatch_args<I, 0, A...>)
                static constexpr decltype(auto) operator()(P&& partial, A&&... args)
                    noexcept (requires{{bind_args(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<consume_positional<J, A...>>{},
                        std::make_index_sequence<sizeof...(A) - (J + consume_positional<J, A...>)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{bind_args(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<consume_positional<J, A...>>{},
                        std::make_index_sequence<sizeof...(A) - (J + consume_positional<J, A...>)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (bind_args(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<consume_positional<J, A...>>{},
                        std::make_index_sequence<sizeof...(A) - (J + consume_positional<J, A...>)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                // [6] variadic keyword arguments will consume all remaining
                // keyword parameters, assert that they do not conflict with any
                // existing partials, and then concatenate them.  The result then gets
                // inserted as a single argument.
                template <typename P, typename... A> requires (dispatch_kwargs<I, J, A...>)
                static constexpr decltype(auto) operator()(P&& partial, A&&... args)
                    noexcept (requires{{bind_kwargs(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<consume_positional<J, A...>>{},
                        std::make_index_sequence<sizeof...(A) - (J + consume_positional<J, A...>)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{bind_kwargs(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<consume_positional<J, A...>>{},
                        std::make_index_sequence<sizeof...(A) - (J + consume_positional<J, A...>)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (bind_kwargs(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<consume_positional<J, A...>>{},
                        std::make_index_sequence<sizeof...(A) - (J + consume_positional<J, A...>)>{},
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }
            };

            /* Base case: once all `Args...` have been bound and inserted into `Ts...`,
            then `A...` must represent a valid initializer for the completed partial
            signature. */
            template <auto... Ts> requires (sizeof...(Ts) == sizeof...(Args))
            struct fn<Ts...> {
                using type = signature<
                    templates<typename meta::unqualify<decltype(T)>::base_type...>{T...},
                    Ts...
                >;
                template <typename P, typename... A>
                static constexpr decltype(auto) operator()(P&& partial, A&&... args)
                    noexcept (requires{{type{std::forward<A>(args)...}} noexcept;})
                    requires (requires{{type{std::forward<A>(args)...}};})
                {
                    return (type{std::forward<A>(args)...});
                }
            };
        };

        /* Recursive case: parse an explicit template parameter list `T...` that was
        provided to the signature's call operator.  Evaluates to a function object
        that can be invoked to complete the call for a particular function `F` and
        set of runtime arguments `A...`. */
        template <size_type I, size_type J, auto... T>
        struct _call;

        // (1) insert partial or default value, or raise a missing required error.
        // Partial keywords are also checked for conflicts with existing values.
        template <typename, typename, typename, size_type, auto...>
        struct call_default;
        template <size_type... prev, size_type... parts, size_type... next, size_type I, auto... T>
            requires (
                sizeof...(parts) == 0 &&
                (info.params[I].kind.sentinel() || info.params[I].kind.variadic())
            )
        struct call_default<
            std::index_sequence<prev...>,
            std::index_sequence<parts...>,
            std::index_sequence<next...>,
            I,
            T...
        > {
            using type = _call<I + 1, sizeof...(prev), T...>;  // no change to `T...`
        };
        template <size_type... prev, size_type... parts, size_type... next, size_type I, auto... T>
            requires (sizeof...(parts) == 0 && info.params[I].kind.optional())
        struct call_default<
            std::index_sequence<prev...>,
            std::index_sequence<parts...>,
            std::index_sequence<next...>,
            I,
            T...
        > {
            using type = _call<
                I + 1,
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                Spec.template get<I>().value(),
                meta::unpack_value<sizeof...(prev) + next, T...>...
            >;
        };
        template <size_type... prev, size_type... parts, size_type... next, size_type I, auto... T>
            requires (sizeof...(parts) > 0)
        struct call_default<
            std::index_sequence<prev...>,
            std::index_sequence<parts...>,
            std::index_sequence<next...>,
            I,
            T...
        > {
            using type = _call<
                I + 1,
                sizeof...(prev) + sizeof...(parts),
                meta::unpack_value<prev, T...>...,
                Spec.partial.template get<Spec.template index<I>() + parts>()...,
                meta::unpack_value<sizeof...(prev) + next, T...>...
            >;
        };
        template <size_type I, size_type J, auto... T>
        struct _call : call_default<
            std::make_index_sequence<J>,
            std::make_index_sequence<info.params[I].partial>,
            std::make_index_sequence<sizeof...(T) - J>,
            I,
            T...
        >::type {};

        // (2) matching positional arguments are trivially forwarded
        template <size_type I, size_type J, auto... T>
            requires (I < Spec.size() && dispatch_positional<I, J, decltype(T)...>)
        struct _call<I, J, T...> : _call<I + 1, J + 1, T...> {};

        // (3) keywords must be removed from their current location, unwrapped, and
        // then inserted in the current position.
        template <typename, typename, typename, size_type, auto...>
        struct call_kw;
        template <size_type... prev, size_type... middle, size_type... next, size_type I, auto... T>
        struct call_kw<
            std::index_sequence<prev...>,
            std::index_sequence<middle...>,
            std::index_sequence<next...>,
            I,
            T...
        > {
            using type = _call<
                I + 1,
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                meta::unpack_value<
                    consume_keyword<I, sizeof...(prev), decltype(T)...>,
                    T...
                >.value(),
                meta::unpack_value<I + middle, T...>...,
                meta::unpack_value<
                    consume_keyword<I, sizeof...(prev), decltype(T)...> + 1 + next,
                    T...
                >...
            >;
        };
        template <size_type I, size_type J, auto... T>
            requires (I < Spec.size() && dispatch_keyword<I, J, decltype(T)...>)
        struct _call<I, J, T...> : call_kw<
            std::make_index_sequence<I>,
            std::make_index_sequence<consume_keyword<I, J, decltype(T)...> - J>,
            std::make_index_sequence<sizeof...(T) - (consume_keyword<I, J, decltype(T)...> + 1)>,
            I,
            T...
        >::type {};

        // (4) variadic positional arguments will consume all remaining positional
        // parameters, and concatenate them with any existing partial values.  The
        // result then gets inserted as a single argument. 
        template <typename, typename, typename, typename, size_type, auto...>
        struct call_args;
        template <
            size_type... prev,
            size_type... parts,
            size_type... pos,
            size_type... kw,
            size_type I,
            auto... T
        > requires (sizeof...(parts) == 0)
        struct call_args<
            std::index_sequence<prev...>,
            std::index_sequence<parts...>,
            std::index_sequence<pos...>,
            std::index_sequence<kw...>,
            I,
            T...
        > {
            using type = _call<
                I + 1,
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                bertrand::args{meta::unpack_value<sizeof...(prev) + pos, T...>...},
                meta::unpack_value<sizeof...(prev) + sizeof...(pos) + kw, T...>...
            >;
        };
        template <
            size_type... prev,
            size_type... parts,
            size_type... pos,
            size_type... kw,
            size_type I,
            auto... T
        > requires (sizeof...(parts) > 0)
        struct call_args<
            std::index_sequence<prev...>,
            std::index_sequence<parts...>,
            std::index_sequence<pos...>,
            std::index_sequence<kw...>,
            I,
            T...
        > {
            using type = _call<
                I + 1,
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                bertrand::args{
                    Spec.partial.template get<Spec.template index<I>() + parts>()...,
                    meta::unpack_value<sizeof...(prev) + pos, T...>...
                },
                meta::unpack_value<sizeof...(prev) + sizeof...(pos) + kw, T...>...
            >;
        };
        template <size_type I, size_type J, auto... T>
            requires (dispatch_args<I, J, decltype(T)...>)
        struct _call<I, J, T...> : call_args<
            std::make_index_sequence<J>,
            std::make_index_sequence<info.params[I].partial>,
            std::make_index_sequence<consume_positional<J, decltype(T)...>>,
            std::make_index_sequence<sizeof...(T) - (J + consume_positional<J, decltype(T)...>)>,
            I,
            T...
        >::type {};

        // (5) variadic keyword arguments will consume all remaining keyword
        // parameters, assert that they do not conflict with any existing partials,
        // and then concatenate them.  The result then gets inserted as a single
        // argument.
        template <typename, typename, typename, typename, size_type, auto...>
        struct call_kwargs;
        template <
            size_type... prev,
            size_type... parts,
            size_type... pos,
            size_type... kw,
            size_type I,
            auto... T
        > requires (sizeof...(parts) == 0)
        struct call_kwargs<
            std::index_sequence<prev...>,
            std::index_sequence<parts...>,
            std::index_sequence<pos...>,
            std::index_sequence<kw...>,
            I,
            T...
        > {
            using type = _call<
                I + 1,
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                args{meta::unpack_value<sizeof...(prev) + sizeof...(pos) + kw, T...>...},
                meta::unpack_value<sizeof...(prev) + pos, T...>...
            >;
        };
        template <
            size_type... prev,
            size_type... parts,
            size_type... pos,
            size_type... kw,
            size_type I,
            auto... T
        > requires (sizeof...(parts) > 0)
        struct call_kwargs<
            std::index_sequence<prev...>,
            std::index_sequence<parts...>,
            std::index_sequence<pos...>,
            std::index_sequence<kw...>,
            I,
            T...
        > {
            using type = _call<
                I + 1,
                sizeof...(prev) + 1,
                meta::unpack_value<prev, T...>...,
                args{
                    Spec.partial.template get<Spec.template index<I>() + parts>()...,
                    meta::unpack_value<sizeof...(prev) + sizeof...(pos) + kw, T...>...
                },
                meta::unpack_value<sizeof...(prev) + pos, T...>...
            >;
        };
        template <size_type I, size_type J, auto... T>
            requires (I < Spec.size() && dispatch_kwargs<I, J, decltype(T)...>)
        struct _call<I, J, T...> : call_kwargs<
            std::make_index_sequence<I>,
            std::make_index_sequence<info.params[I].partial>,
            std::make_index_sequence<consume_positional<J, decltype(T)...>>,
            std::make_index_sequence<sizeof...(T) - (I + consume_positional<J, decltype(T)...>)>,
            I,
            T...
        >::type {};

        /* Base case: once all parameters in `Spec` have been matched, `T...` will
        contain a valid explicit template parameter list for `F`. */
        template <size_type _, auto... T>
        struct _call<Spec.size(), _, T...> {
            /* A function object that invokes an external function according to the
            signature's semantics, using the explicit template parameters in `T...` and
            a runtime argument list supplied at the call site.  This works by scanning
            `Args...` from left to right and updating the argument list `A...` in-place
            at every iteration, such that by the end, it represents a valid call site
            for the function `F`. */
            template <size_type I = Spec.size(), size_type delta = 0>
            struct fn {
            private:
                /// NOTE: `delta` is necessary to account for separators in `Args...`,
                /// which are ignored during binding, but would otherwise cause `J` to
                /// overestimate the current position in `A...`.
                static constexpr size_type J = I - Spec.size() - delta;
                static constexpr param<I> curr;

                template <
                    size_type... prev,
                    size_type... next,
                    typename F,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) call_default(
                    std::index_sequence<prev...>,
                    std::index_sequence<next...>,
                    F&& func,
                    P&& partial,
                    A&&... args
                )
                    noexcept (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        curr.arg.value(),
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    )} noexcept;})
                    requires (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        curr.arg.value(),
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    )};})
                {
                    return (fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        curr.arg.value(),
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    ));
                }

                template <
                    size_type... prev,
                    size_type... parts,
                    size_type... next,
                    typename F,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) call_partial(
                    std::index_sequence<prev...>,
                    std::index_sequence<parts...>,
                    std::index_sequence<next...>,
                    F&& func,
                    P&& partial,
                    A&&... args
                )
                    noexcept (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    )} noexcept;})
                    requires (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    )};})
                {
                    return (fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        std::forward<P>(partial).template get<target_partial<I> + parts>()...,
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    ));
                }

                template <
                    size_type... prev,
                    size_type... next,
                    typename F,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) call_arg_pack(
                    std::index_sequence<prev...>,
                    std::index_sequence<next...>,
                    F&& func,
                    P&& partial,
                    A&&... args
                )
                    noexcept (requires{
                        {fn{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<sizeof...(prev) + 1 + next>(std::forward<A>(args)...)...
                        )} noexcept;
                        {fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<sizeof...(prev)>(std::forward<A>(args)...).next(),
                            meta::unpack_arg<sizeof...(prev)>(std::forward<A>(args)...),
                            meta::unpack_arg<sizeof...(prev) + 1 + next>(std::forward<A>(args)...)...
                        )} noexcept;
                    })
                    requires (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<sizeof...(prev)>(std::forward<A>(args)...).next(),
                        meta::unpack_arg<sizeof...(prev)>(std::forward<A>(args)...),
                        meta::unpack_arg<sizeof...(prev) + 1 + next>(std::forward<A>(args)...)...
                    )};})
                {
                    if (meta::unpack_arg<I>(std::forward<A>(args)...).empty()) {
                        if constexpr (requires{fn{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<sizeof...(prev) + 1 + next>(std::forward<A>(args)...)...
                        );}) {
                            return (fn{}(
                                std::forward<F>(func),
                                std::forward<P>(partial),
                                meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                                meta::unpack_arg<sizeof...(prev) + 1 + next>(
                                    std::forward<A>(args)...
                                )...
                            ));
                        } else {
                            static constexpr static_str msg =
                                "No value for required positional argument '" +
                                curr.name + "' at index " + static_str<>::from_int<I - Spec.size()> +
                                " (expected from positional parameter pack)";
                            throw TypeError(msg);
                        }
                    } else {
                        return (fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<sizeof...(prev)>(std::forward<A>(args)...).next(),
                            meta::unpack_arg<sizeof...(prev)>(std::forward<A>(args)...),
                            meta::unpack_arg<sizeof...(prev) + 1 + next>(std::forward<A>(args)...)...
                        ));
                    }
                }

                template <
                    size_type... prev,
                    size_type... middle,
                    size_type... next,
                    typename F,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) call_kw(
                    std::index_sequence<prev...>,
                    std::index_sequence<middle...>,
                    std::index_sequence<next...>,
                    F&& func,
                    P&& partial,
                    A&&... args
                )
                    noexcept (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, sizeof...(prev), A...>>(
                            std::forward<A>(args)...
                        ).value(),
                        meta::unpack_arg<sizeof...(prev) + middle>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, sizeof...(prev), A...> + 1 + next>(
                            std::forward<A>(args)...
                        )...
                    )} noexcept;})
                    requires (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, sizeof...(prev), A...>>(
                            std::forward<A>(args)...
                        ).value(),
                        meta::unpack_arg<sizeof...(prev) + middle>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, sizeof...(prev), A...> + 1 + next>(
                            std::forward<A>(args)...
                        )...
                    )};})
                {
                    return (fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, sizeof...(prev), A...>>(
                            std::forward<A>(args)...
                        ).value(),
                        meta::unpack_arg<sizeof...(prev) + middle>(std::forward<A>(args)...)...,
                        meta::unpack_arg<consume_keyword<I, sizeof...(prev), A...> + 1 + next>(
                            std::forward<A>(args)...
                        )...
                    ));
                }

                /// TODO: I think the index calculation for kwarg packs is off.

                template <
                    size_type... prev,
                    size_type... next,
                    typename F,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) call_kwarg_pack(
                    std::index_sequence<prev...>,
                    std::index_sequence<next...>,
                    F&& func,
                    P&& partial,
                    A&&... args
                )
                    noexcept (
                        requires{{fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<sizeof...(prev)>(
                                std::forward<A>(args)...
                            ).template pop<curr.name>().value(),
                            meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                        )} noexcept;} &&
                        requires{{fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            curr.arg.value(),
                            meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                        )} noexcept;}
                    )
                    requires (
                        curr.kind.optional() &&
                        requires{fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<sizeof...(prev)>(
                                std::forward<A>(args)...
                            ).template pop<curr.name>().value(),
                            meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                        );} &&
                        requires{fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            curr.arg.value(),
                            meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                        );}
                    )
                {
                    if (auto val = meta::unpack_arg<sizeof...(prev)>(
                        std::forward<A>(args)...
                    ).template pop<curr.name>(); val.has_value()) {
                        return (fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            std::move(val).value(),
                            meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                        ));
                    } else {
                        return (fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            curr.arg.value(),
                            meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                        ));
                    }
                }
                template <
                    size_type... prev,
                    size_type... next,
                    typename F,
                    typename P,
                    typename... A
                >
                static constexpr decltype(auto) call_kwarg_pack(
                    std::index_sequence<prev...>,
                    std::index_sequence<next...>,
                    F&& func,
                    P&& partial,
                    A&&... args
                )
                    requires (!curr.kind.optional() && requires{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<sizeof...(prev)>(
                            std::forward<A>(args)...
                        ).template pop<curr.name>().value(),
                        meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                    );})
                {
                    if (auto val = meta::unpack_arg<sizeof...(prev)>(
                        std::forward<A>(args)...
                    ).template pop<curr.name>(); val.has_value()) {
                        return (fn<I + 1, delta>{}(
                            std::forward<F>(func),
                            std::forward<P>(partial),
                            meta::unpack_arg<prev>(std::forward<A>(args)...)...,
                            std::move(val).value(),
                            meta::unpack_arg<sizeof...(prev) + next>(std::forward<A>(args)...)...
                        ));
                    } else {
                        static constexpr static_str msg =
                            "No value for required keyword argument '" + curr.name +
                            "' at index " + static_str<>::from_int<I - Spec.size()> +
                            " (expected from keyword parameter pack)";
                        throw TypeError(msg);
                    }
                }


                /// TODO: when binding a variadic positional or keyword argument,
                /// unpacking operators of that kind will always be removed and subsumed
                /// into the final argument list.  For positional parameter packs,
                /// this can probably be converted into full static type info by just
                /// doing a recursive loop up to MAX_ARGS, with a conditional empty
                /// check at each stage, and a special error at the end if the
                /// length of the argument pack exceeds MAX_ARGS.


            public:
                // [0] non-partial arguments are guaranteed by validate<...> to have
                // default values, or be sentinels like posonly/kwonly separators or
                // return annotations.
                template <typename F, typename P, typename... A>
                    requires (
                        curr.partial == 0 &&
                        curr.kind.sentinel() &&
                        !dispatch_positional<I, J, A...> &&
                        !dispatch_keyword<I, J, A...> &&
                        !dispatch_args<I, J, A...> &&
                        !dispatch_kwargs<I, J, A...>
                    )
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{fn<I + 1, delta + 1>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{fn<I + 1, delta + 1>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (fn<I + 1, delta + 1>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }
                template <typename F, typename P, typename... A>
                    requires (
                        curr.partial == 0 &&
                        !curr.kind.sentinel() &&
                        !dispatch_positional<I, J, A...> &&
                        !dispatch_keyword<I, J, A...> &&
                        !dispatch_args<I, J, A...> &&
                        !dispatch_kwargs<I, J, A...>
                    )
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{call_default(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{call_default(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (call_default(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                // [1] partial arguments are perfectly forwarded, along with their
                // current partial value(s).
                template <typename F, typename P, typename... A>
                    requires (
                        curr.partial > 0 &&
                        !dispatch_positional<I, J, A...> &&
                        !dispatch_keyword<I, J, A...> &&
                        !dispatch_args<I, J, A...> &&
                        !dispatch_kwargs<I, J, A...>
                    )
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{call_partial(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{call_partial(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (call_partial(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<curr.partial>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                // [2] matching positional arguments can be trivially forwarded or
                // provided by a positional unpacking operator
                template <typename F, typename P, typename... A>
                    requires (
                        dispatch_positional<I, J, A...> &&
                        !meta::arg_pack<meta::unpack_type<J, A...>>
                    )
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (fn<I + 1, delta>{}(
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }
                template <typename F, typename P, typename... A>
                    requires (
                        dispatch_positional<I, J, A...> &&
                        meta::arg_pack<meta::unpack_type<J, A...>>
                    )
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{call_arg_pack(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J + 1>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{call_arg_pack(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J + 1>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (call_arg_pack(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J + 1>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                // [3] keywords must be removed from their current location, unwrapped
                // to their underlying values, and then forwarded.  They may also be
                // provided by a keyword unpacking operator.
                template <typename F, typename P, typename... A>
                    requires (
                        dispatch_keyword<I, J, A...> &&
                        !meta::kwarg_pack<meta::unpack_type<consume_keyword<I, J, A...>, A...>>
                    )
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{call_kw(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consume_keyword<I, J, A...> - J>{},
                        std::make_index_sequence<sizeof...(A) - (consume_keyword<I, J, A...> + 1)>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{call_kw(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consume_keyword<I, J, A...> - J>{},
                        std::make_index_sequence<sizeof...(A) - (consume_keyword<I, J, A...> + 1)>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (call_kw(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consume_keyword<I, J, A...> - J>{},
                        std::make_index_sequence<sizeof...(A) - (consume_keyword<I, J, A...> + 1)>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }
                template <typename F, typename P, typename... A>
                    requires (
                        dispatch_keyword<I, J, A...> &&
                        meta::kwarg_pack<meta::unpack_type<consume_keyword<I, J, A...>, A...>>
                    )
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{call_kwarg_pack(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )} noexcept;})
                    requires (requires{{call_kwarg_pack(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    )};})
                {
                    return (call_kwarg_pack(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<F>(func),
                        std::forward<P>(partial),
                        std::forward<A>(args)...
                    ));
                }

                /// TODO: overloads for variadic positional and keyword arguments

            };

            /* Base case: once all `Args...` have been matched and inserted into the
            runtime argument list `A...`, then the combination of `T...` and `A...`
            must represent a valid call site for the underlying function `F`.
            Conversion to a specific return type is done if the signature defines a
            return annotation. */
            template <size_type delta> requires (info.ret.idx >= size())
            struct fn<size(), delta> {
                /// TODO: this base case will have to validate and remove positional/keyword
                /// packs at the same time.

                // If no template arguments are supplied, then we call the function
                // normally by perfectly forwarding.
                template <typename F, typename P, typename... A>
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{
                        std::forward<F>(func)(std::forward<A>(args)...)
                    } noexcept;})
                    requires (sizeof...(T) == 0 && requires{{
                        std::forward<F>(func)(std::forward<A>(args)...)
                    };})
                {
                    return (std::forward<F>(func)(std::forward<A>(args)...));
                }

                // If one or more explicit template arguments are given, then we
                // manually call the function's `operator()` with the provided
                // templates
                template <typename F, typename P, typename... A>
                static constexpr decltype(auto) operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{
                        std::forward<F>(func).template operator()<T...>(
                            std::forward<A>(args)...
                        )
                    } noexcept;})
                    requires (sizeof...(T) > 0 && requires{{
                        std::forward<F>(func).template operator()<T...>(
                            std::forward<A>(args)...
                        )
                    };})
                {
                    return (std::forward<F>(func).template operator()<T...>(
                        std::forward<A>(args)...
                    ));
                }
            };
            template <size_type delta> requires (info.ret.idx < size())
            struct fn<size(), delta> {
                using type = meta::arg_type<decltype(info.template get<info.ret.idx>())>;

                /// TODO: this base case will have to validate and remove positional/keyword
                /// packs at the same time.

                // If no template arguments are supplied, then we call the function
                // normally by perfectly forwarding.
                template <typename F, typename P, typename... A>
                static constexpr type operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{
                        std::forward<F>(func)(std::forward<A>(args)...)
                    } noexcept;})
                    requires (sizeof...(T) == 0 && requires{{
                        std::forward<F>(func)(std::forward<A>(args)...)
                    };})
                {
                    return std::forward<F>(func)(std::forward<A>(args)...);
                }

                // If one or more explicit template arguments are given, then we
                // manually call the function's `operator()` with the provided
                // templates
                template <typename F, typename P, typename... A>
                static constexpr type operator()(F&& func, P&& partial, A&&... args)
                    noexcept (requires{{
                        std::forward<F>(func).template operator()<T...>(
                            std::forward<A>(args)...
                        )
                    } noexcept;})
                    requires (sizeof...(T) > 0 && requires{{
                        std::forward<F>(func).template operator()<T...>(
                            std::forward<A>(args)...
                        )
                    };})
                {
                    return std::forward<F>(func).template operator()<T...>(
                        std::forward<A>(args)...
                    );
                }
            };
        };

        template <size_type... Is>
        static constexpr auto _clear(std::index_sequence<Is...>) noexcept {
            return signature<
                {impl::remove_partial(Spec.args.template get<Is>())...},
                impl::remove_partial(Args)...
            >{};
        }

    public:
        /* Validate a `bind()` argument list against this signature's explicit template
        parameters.  Evaluates to a `bind_error` enum that can be used in conjunction
        with the `meta::bind_error` concept to constrain templates. */
        template <typename... A>
        static constexpr bind_error bind_templates =
            _bind_templates<std::make_index_sequence<Spec.size()>, A...>;

        /* Validate a `bind()` argument list against this signature's runtime function
        parameters.  Evaluates to a `bind_error` enum that can be used in conjunction
        with the `meta::bind_error` concept to constrain templates. */
        template <typename... A>
        static constexpr bind_error bind_params =
            _bind_params<std::make_index_sequence<sizeof...(Args)>, A...>;

        /* Validate an `operator()` argument list against this signature's explicit
        template parameters.  Evaluates to a `bind_error` enum that can be used in
        conjunction with the `meta::bind_error` concept to constrain templates. */
        template <typename... A>
        static constexpr bind_error call_templates =
            _call_templates<std::make_index_sequence<Spec.size()>, A...>;

        /* Validate an `operator()` argument list against this signature's runtime
        function parameters.  Evaluates to a `bind_error` enum that can be used in
        conjunction with the `meta::bind_error` concept to constrain templates. */
        template <typename... A>
        static constexpr bind_error call_params =
            _call_params<std::make_index_sequence<sizeof...(Args)>, A...>;

        /* Check to see whether the arguments can be used to invoke the underlying
        function as a visitor. */
        template <auto... T>
        struct specialize {
            template <typename Self, typename F, typename... A>
            static constexpr bool visit = meta::visit<
                typename _call<0, 0, T...>::template fn<>,
                F,
                decltype(std::declval<Self>().partial),
                A...
            >;
        };

        /* The total number of partial arguments that are currently bound to the
        signature. */
        [[nodiscard]] static constexpr size_type n_partial() noexcept {
            return (Spec.partials.size() + ... + meta::partials<decltype(Args)>::size);
        }

        /* Strip all bound partial arguments from the signature. */
        [[nodiscard]] static constexpr auto clear() noexcept {
            return _clear(std::make_index_sequence<Spec.size()>{});
        }

        /* Bind a set of partial arguments to this signature, returning a new signature
        that merges the provided values with any existing partial arguments.  All
        arguments will be perfectly forwarded if possible, allowing this method to
        be efficiently chained without intermediate copies. */
        template <auto... T, typename Self, typename... A>
            requires (
                meta::source_args<decltype(T)...> &&
                meta::source_args<A...> &&
                (!meta::arg_pack<decltype(T)> && ...) &&
                (!meta::kwarg_pack<decltype(T)> && ...) &&
                (!meta::arg_pack<A> && ...) &&
                (!meta::kwarg_pack<A> && ...) &&
                meta::bind_error<bind_templates<decltype(T)...>> &&
                meta::bind_error<bind_params<A...>>
            )
        [[nodiscard]] constexpr decltype(auto) bind(this Self&& self, A&&... args)
            noexcept (requires{{typename _bind<0, T...>::template fn<>{}(
                std::forward<Self>(self).partial,
                std::forward<A>(args)...
            )} noexcept;})
            requires (requires{{typename _bind<0, T...>::template fn<>{}(
                std::forward<Self>(self).partial,
                std::forward<A>(args)...
            )};})
        {
            return (typename _bind<0, T...>::template fn<>{}(
                std::forward<Self>(self).partial,
                std::forward<A>(args)...
            ));
        }

        /* Invoke a function with the partial arguments stored in this signature,
        merging with any explicit template parameters or runtime arguments provided at
        the call site. */
        template <auto... T, typename Self, typename F, typename... A>
            requires (
                meta::source_args<decltype(T)...> &&
                meta::source_args<A...> &&
                meta::bind_error<call_templates<decltype(T)...>> &&
                meta::bind_error<call_params<A...>> &&
                specialize<T...>::template visit<Self, F, A...>
            )
        [[nodiscard]] constexpr decltype(auto) operator()(
            this Self&& self,
            F&& func,
            A&&... args
        )
            noexcept (requires{{impl::visit(
                typename _call<0, 0, T...>::template fn<>{},
                std::forward<F>(func),
                std::forward<Self>(self).partial,
                std::forward<A>(args)...
            )} noexcept;})
        {
            return (impl::visit(
                typename _call<0, 0, T...>::template fn<>{},
                std::forward<F>(func),
                std::forward<Self>(self).partial,
                std::forward<A>(args)...
            ));
            /// TODO: when I finish with the call logic, delete this.
            // return (typename _call<0, 0, T...>::template fn<>{}(
            //     std::forward<F>(func),
            //     std::forward<Self>(self).partial,
            //     std::forward<A>(args)...
            // ));
        }
    };

    /* Attempt to deduce the signature of a callable of arbitrary type.  This helper
    allows `def()` to be used without providing an explicit signature, provided a
    specialization of this class exists to inform the compiler of the proper signature.

    Specializations must provide a `::type` alias that refers to a specialization of
    `impl::signature<...>`, which will be treated as if it were an explicit signature
    for functions of type `F`.  Users may also provide a call operator that accepts
    an instance of `F` and returns a wrapper function that will be stored within the
    `def()` as a convenience.

    If no specialization is found for a given type `F`, then the default behavior is
    to build a function that accepts arbitrary C++ positional-only arguments and
    perfectly forwards them to the underlying function, assuming it to be a template
    and/or overload set.  A built-in specialization exists for any `F` whose call
    operator is trivially introspectable (meaning it has no overloads or template
    parameters), which will mirror those arguments in the resulting signature, again
    according to C++ positional-only syntax.  A second built-in specialization covers
    `def()` functions themselves, whose signatures will always be implicitly propagated
    if necessary. */
    template <typename F>
    struct detect_signature {
    private:

        struct wrapper {
        private:

            template <const auto& T, typename indices, typename Func>
            struct call;
            template <const auto& T, size_t... Is, typename Func>
            struct call<T, std::index_sequence<Is...>, Func> {
                meta::as_lvalue<Func> func;

                constexpr decltype(auto) operator()(auto&&... args) &&
                    noexcept (requires{
                        {std::forward<Func>(func).template operator()<
                            T.template get<Is>()...
                        >(std::forward<decltype(args)>(args)...)} noexcept;
                    })
                    requires (requires{{
                        std::forward<Func>(func).template operator()<
                            T.template get<Is>()...
                        >(std::forward<decltype(args)>(args)...)
                    };})
                {
                    return (std::forward<Func>(func).template operator()<
                        T.template get<Is>()...
                    >(std::forward<decltype(args)>(args)...));
                }
            };

        public:
            meta::remove_rvalue<F> func;

            /* If no template arguments are supplied, then we call the function
            normally. */
            template <meta::args auto T, typename S, meta::args A> requires (T.empty())
            constexpr decltype(auto) operator()(this S&& self, A&& args)
                noexcept (requires{{std::forward<A>(args)(std::forward<S>(self).func)} noexcept;})
                requires (requires{{std::forward<A>(args)(std::forward<S>(self).func)};})
            {
                return (std::forward<A>(args)(std::forward<S>(self).func));
            }

            /* If template arguments are given, attempt to specialize the call operator
            before calling. */
            template <meta::args auto T, typename S, meta::args A> requires (!T.empty())
            constexpr decltype(auto) operator()(this S&& self, A&& args)
                noexcept (requires{{
                    std::forward<A>(args)(call<
                        T,
                        std::make_index_sequence<T.size()>,
                        decltype(std::forward<S>(self).func)
                    >{self.func})} noexcept;
                })
                requires (requires{{
                    std::forward<A>(args)(call<
                        T,
                        std::make_index_sequence<T.size()>,
                        decltype(std::forward<S>(self).func)
                    >{self.func})
                };})
            {
                return std::forward<A>(args)(call<
                    T,
                    std::make_index_sequence<T.size()>,
                    decltype(std::forward<S>(self).func)
                >{self.func});
            }
        };

    public:
        /* The default signature allows any function-like object with an
        arbitrarily-templated call operator. */
        using type = signature<{"*t"_}, "*a"_>;

        /* Produce a wrapper for a function `F` that accepts arbitrary template and
        runtime arguments and perfectly forwards them to the wrapped function. */
        static constexpr wrapper operator()(F func)
            noexcept (meta::nothrow::convertible_to<F, meta::remove_rvalue<F>>)
            requires (meta::convertible_to<F, meta::remove_rvalue<F>>)
        {
            return {std::forward<F>(func)};
        }
    };

    /* Specialization for trivially-introspectable function objects, whose signatures
    can be statically deduced. */
    template <meta::has_call_operator F>
    struct detect_signature<F> {
    private:
        using traits = meta::call_operator<F>;

        template <typename... Args>
        struct expand_arguments {
            template <typename>
            struct _type;
            template <size_t... Is>
            struct _type<std::index_sequence<Is...>> {
                using type = signature<
                    {},
                    operator""_<"_" + static_str<>::from_int<Is>>()[type<Args>]...,
                    "/"_,
                    "->"_[type<typename traits::return_type>]
                >;
            };
            using type = _type<std::index_sequence_for<Args...>>::type;
        };

    public:
        using type = traits::args::template eval<expand_arguments>::type;
    };


    /// TODO: format_signature is a monster, and should be simplified and possibly
    /// incorporated into the signature class itself.

    inline std::string format_signature(
        const std::string& prefix,
        size_t max_width,
        size_t indent,
        std::vector<std::string>& components,
        size_t last_posonly,
        size_t first_kwonly
    ) {
        constexpr std::string param_open        = "(";
        constexpr std::string param_close       = ") -> ";
        constexpr std::string type_sep          = ": ";
        constexpr std::string default_sep       = " = ";
        constexpr std::string sep               = ", ";
        std::string tab                         = std::string(indent, ' ');
        constexpr std::string line_sep          = "\n";
        constexpr std::string kwonly_sep        = "*";
        constexpr std::string posonly_sep       = "/";

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


    // /* Backs the pure-C++ `signature` class in a way that prevents unnecessary code
    // duplication between specializations.  Python signatures can extend this base to
    // avoid reimplementing C++ function logic internally. */
    // template <typename Param, typename Return, typename... Args>
    // struct CppSignature {
    //     /// TODO: revisit to_string to pull it out of the signature<> specializations and
    //     /// reduce code duplication.  It should also always produce valid C++ and/or Python
    //     /// source code, and if defaults are not given, they will be replaced with ellipsis
    //     /// in the Python style, such that the output can be written to a .pyi file.

    //     /* Produce a string representation of this signature for debugging purposes.  The
    //     provided `prefix` will be prepended to each output line, and if `max_width` is
    //     provided, then the algorithm will attempt to wrap the output to that width, with
    //     each parameter indented on a separate line.  If a single parameter exceeds the
    //     maximum width, then it will be wrapped onto multiple lines with an additional level
    //     of indentation for the extra lines.  Note that the maximum width is not a hard
    //     limit; individual components can exceed it, but never on the same line as another
    //     component.

    //     The output from this method is directly written to a .pyi file when bindings are
    //     generated, allowing static type checkers to validate C++ function signatures and
    //     provide high-quality syntax highlighting/autocompletion. */
    //     [[nodiscard]] static constexpr std::string to_string(
    //         const std::string& name,
    //         const std::string& prefix = "",
    //         size_t max_width = std::numeric_limits<size_t>::max(),
    //         size_t indent = 4
    //     ) {
    //         std::vector<std::string> components;
    //         components.reserve(size() * 3 + 2);
    //         components.emplace_back(name);

    //         /// TODO: appending the demangled type name is probably wrong, since it doesn't
    //         /// always yield valid Python source code.  Instead, I should probably try to
    //         /// convert the type to Python and return its qualified name?  That way, the
    //         /// .pyi file would be able to import the type correctly.  That will need some
    //         /// work, and demangling might be another option to the method that directs it
    //         /// to do this.  I'll probably have to revisit that when I actually try to
    //         /// build the .pyi files, and can test more directly.

    //         size_t last_posonly = std::numeric_limits<size_t>::max();
    //         size_t first_kwonly = std::numeric_limits<size_t>::max();
    //         []<size_t I = 0>(
    //             this auto&& self,
    //             auto&& defaults,
    //             std::vector<std::string>& components,
    //             size_t& last_posonly,
    //             size_t& first_kwonly
    //         ) {
    //             if constexpr (I < size()) {
    //                 using T = CppSignature::at<I>;
    //                 if constexpr (meta::arg_traits<T>::args()) {
    //                     components.emplace_back(std::string("*" + meta::arg_traits<T>::name));
    //                 } else if constexpr (meta::arg_traits<T>::kwargs()) {
    //                     components.emplace_back(std::string("**" + meta::arg_traits<T>::name));
    //                 } else {
    //                     if constexpr (meta::arg_traits<T>::posonly()) {
    //                         last_posonly = I;
    //                     } else if constexpr (meta::arg_traits<T>::kwonly() && !CppSignature::has_args) {
    //                         if (first_kwonly == std::numeric_limits<size_t>::max()) {
    //                             first_kwonly = I;
    //                         }
    //                     }
    //                     components.emplace_back(std::string(meta::arg_traits<T>::name));
    //                 }
    //                 components.emplace_back(
    //                     std::string(demangle<typename meta::arg_traits<T>::type>())
    //                 );
    //                 if constexpr (meta::arg_traits<T>::opt()) {
    //                     components.emplace_back("...");
    //                 } else {
    //                     components.emplace_back("");
    //                 }
    //                 std::forward<decltype(self)>(self).template operator()<I + 1>(
    //                     std::forward<decltype(defaults)>(defaults),
    //                     components
    //                 );
    //             }
    //         }(components);

    //         if constexpr (meta::is_void<Return>) {
    //             components.emplace_back("None");
    //         } else {
    //             components.emplace_back(std::string(demangle<Return>()));
    //         }

    //         return impl::format_signature(
    //             prefix,
    //             max_width,
    //             indent,
    //             components,
    //             last_posonly,
    //             first_kwonly
    //         );
    //     }

    //     template <meta::inherits<Defaults> D>
    //     [[nodiscard]] static constexpr std::string to_string(
    //         const std::string& name,
    //         D&& defaults,
    //         const std::string& prefix = "",
    //         size_t max_width = std::numeric_limits<size_t>::max(),
    //         size_t indent = 4
    //     ) {
    //         std::vector<std::string> components;
    //         components.reserve(size() * 3 + 2);
    //         components.emplace_back(name);

    //         size_t last_posonly = std::numeric_limits<size_t>::max();
    //         size_t first_kwonly = std::numeric_limits<size_t>::max();
    //         []<size_t I = 0>(
    //             this auto&& self,
    //             auto&& defaults,
    //             std::vector<std::string>& components,
    //             size_t& last_posonly,
    //             size_t& first_kwonly
    //         ) {
    //             if constexpr (I < size()) {
    //                 using T = CppSignature::at<I>;
    //                 if constexpr (meta::arg_traits<T>::args()) {
    //                     components.emplace_back(std::string("*" + meta::arg_traits<T>::name));
    //                 } else if constexpr (meta::arg_traits<T>::kwargs()) {
    //                     components.emplace_back(std::string("**" + meta::arg_traits<T>::name));
    //                 } else {
    //                     if constexpr (meta::arg_traits<T>::posonly()) {
    //                         last_posonly = I;
    //                     } else if constexpr (meta::arg_traits<T>::kwonly() && !CppSignature::has_args) {
    //                         if (first_kwonly == std::numeric_limits<size_t>::max()) {
    //                             first_kwonly = I;
    //                         }
    //                     }
    //                     components.emplace_back(std::string(meta::arg_traits<T>::name));
    //                 }
    //                 components.emplace_back(
    //                     std::string(demangle<typename meta::arg_traits<T>::type>())
    //                 );
    //                 if constexpr (meta::arg_traits<T>::opt()) {
    //                     components.emplace_back(repr(
    //                         defaults.template get<Defaults::template find<I>>()
    //                     ));
    //                 } else {
    //                     components.emplace_back("");
    //                 }
    //                 std::forward<decltype(self)>(self).template operator()<I + 1>(
    //                     std::forward<decltype(defaults)>(defaults),
    //                     components
    //                 );
    //             }
    //         }(components);

    //         if constexpr (meta::is_void<Return>) {
    //             components.emplace_back("None");
    //         } else {
    //             components.emplace_back(std::string(demangle<Return>()));
    //         }

    //         return impl::format_signature(
    //             prefix,
    //             max_width,
    //             indent,
    //             components,
    //             last_posonly,
    //             first_kwonly
    //         );
    //     }
    // };

    // template <typename R, typename C, size_t I>
    // struct chain_return_type { using type = R; };
    // template <typename R, typename C, size_t I> requires (I < C::size())
    // struct chain_return_type<R, C, I> {
    //     using type = chain_return_type<
    //         std::invoke_result_t<typename C::template at<I>, R>,
    //         C,
    //         I + 1
    //     >::type;
    // };

    // /* If this control structure is enabled, the unary `call()` operator will accept
    // functions that may have partial and/or default arguments and call them directly,
    // rather than requiring the creation of a separate defaults tuple.  This allows
    // `call()` to be used on `def` statements and other bertrand function objects. */
    // template <typename F>
    // struct call_passthrough { static constexpr bool enable = false; };
    // template <meta::def F>
    // struct call_passthrough<F> { static constexpr bool enable = true; };
    // template <meta::chain F>
    //     requires (call_passthrough<
    //         typename std::remove_cvref_t<F>::template at<0>
    //     >::enable)
    // struct call_passthrough<F> { static constexpr bool enable = true; };

}


///////////////////
////    DEF    ////
///////////////////


namespace impl {
    struct chain_tag {};

    template <meta::not_void F, meta::unqualified Sig> requires (meta::signature<Sig>)
    struct def;

    /* A simple convenience struct implementing the overload pattern for a finite
    set of function objects. */
    template <meta::unqualified... Fs> requires (meta::not_void<Fs> && ...)
    struct overloads : public Fs... {
        using size_type = size_t;
        using index_type = ssize_t;

        /* Records the type of each function, in the same order they were given. */
        using types = meta::pack<Fs...>;

        /* The number of overloads within the set, as an unsigned integer. */
        [[nodiscard]] static constexpr size_type size() noexcept { return sizeof...(Fs); }

        /* The number of overloads within the set, as a signed integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }

        /* True if the overload set is empty.  This is almost always false. */
        [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }

        /* Get the function at index I, perfectly forwarding it according to the pack's
        current cvref qualifications. */
        template <index_type I, typename Self> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return (std::forward<meta::qualify<
                meta::unpack_type<impl::normalize_index<ssize(), I>, Fs...>,
                Self
            >>(self));
        }

        /* Visitors inherit the call operators of each function, forming an overload
        set. */
        using Fs::operator()...;
    };

    template <typename... Fs>
    overloads(Fs...) -> overloads<Fs...>;

}


namespace meta {

    template <typename T>
    concept chain = inherits<T, impl::chain_tag>;

    namespace detail {

        template <typename T>
        constexpr bool overloads = false;
        template <typename... Fs>
        constexpr bool overloads<impl::overloads<Fs...>> = true;

        template <typename T>
        constexpr bool def = false;
        template <typename F, typename Sig>
        constexpr bool def<impl::def<F, Sig>> = true;

    }

    template <typename T>
    concept overloads = detail::overloads<unqualify<T>>;

    template <typename T>
    concept def = detail::def<unqualify<T>>;

}


namespace impl {

    template <typename F, typename... Fs>
    struct _chain : impl::chain_tag {
    protected:
        using type = meta::remove_rvalue<F>;
        struct storage { type value; };

        template <typename Self, typename... A>
        static constexpr bool invoke_chain = meta::invocable<
            decltype(std::declval<Self>().m_storage.value),
            A...
        >;

        template <size_t I, typename Self>
        static constexpr decltype(auto) get(Self&& self) noexcept {
            return (std::forward<Self>(self).m_storage.value);
        }

        template <typename Self, typename... A> requires (invoke_chain<Self, A...>)
        static constexpr decltype(auto) operator()(Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).m_storage.value(std::forward<A>(args)...)} noexcept;
            })
        {
            return (std::forward<Self>(self).m_storage.value(std::forward<A>(args)...));
        }

    public:
        storage m_storage;

        constexpr _chain(F func)
            noexcept (meta::nothrow::convertible_to<F, type>)
        :
            m_storage(std::forward<F>(func))
        {}

        constexpr _chain(const _chain&) = default;
        constexpr _chain(_chain&&) = default;

        constexpr _chain& operator=(const _chain& other)
            noexcept (meta::lvalue<F> || meta::nothrow::copy_assignable<F> || (
                meta::nothrow::copyable<F> && meta::nothrow::swappable<F>
            ))
            requires (meta::lvalue<F> || meta::copy_assignable<F> || (
                meta::copyable<F> && meta::swappable<F>
            ))
        {
            if constexpr (meta::lvalue<F>) {
                std::construct_at(&m_storage, other.m_storage);

            } else if constexpr (meta::copy_assignable<F>) {
                m_storage.value = other.m_storage.value;

            } else {
                _chain temp(other);
                swap(temp);
            }

            return *this;
        }

        constexpr _chain& operator=(_chain&& other)
            noexcept (
                meta::lvalue<F> ||
                meta::nothrow::move_assignable<F> || 
                meta::nothrow::swappable<F>
            )
            requires (
                meta::lvalue<F> ||
                meta::nothrow::move_assignable<F> ||
                meta::swappable<F>
            )
        {
            if constexpr (meta::lvalue<F>) {
                std::construct_at(&m_storage, other.m_storage);

            } else if constexpr (meta::move_assignable<F>) {
                m_storage.value = std::move(other.m_storage.value);

            } else {
                swap(other);
            }

            return *this;
        }

        constexpr void swap(_chain& other)
            noexcept (meta::nothrow::swappable<F>)
            requires (meta::swappable<F>)
        {
            std::ranges::swap(m_storage.value, other.m_storage.value);
        }
    };
    template <typename F1, typename F2, typename... Fs>
    struct _chain<F1, F2, Fs...> : _chain<F2, Fs...> {
    protected:
        using type = meta::remove_rvalue<F1>;
        struct storage { type value; };

        template <typename Self, typename... A>
        static constexpr bool invoke_chain = false;
        template <typename Self, typename... A>
            requires (meta::invocable<decltype(std::declval<Self>().m_storage.value), A...>)
        static constexpr bool invoke_chain<Self, A...> =
            _chain<F2, Fs...>::template invoke_chain<
                meta::qualify<_chain<F2, Fs...>, Self>,
                meta::invoke_type<decltype(std::declval<Self>().m_storage.value), A...>
            >;

        template <size_t I, typename Self>
        static constexpr decltype(auto) get(Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).m_storage.value);
            } else {
                return (_chain<F2, Fs...>::template get<I - 1>(std::forward<
                    meta::qualify<_chain<F2, Fs...>, Self>
                >(self)));
            }
        }

        template <typename Self, typename... A> requires (invoke_chain<Self, A...>)
        static constexpr decltype(auto) operator()(Self&& self, A&&... args)
            noexcept (requires{{_chain<F2, Fs...>::operator()(
                std::forward<meta::qualify<_chain<F2, Fs...>, Self>>(self),
                std::forward<Self>(self).m_storage.value(std::forward<A>(args)...)
            )} noexcept;})
        {
            return (_chain<F2, Fs...>::operator()(
                std::forward<meta::qualify<_chain<F2, Fs...>, Self>>(self),
                std::forward<Self>(self).m_storage.value(std::forward<A>(args)...)
            ));
        }

    public:
        storage m_storage;

        constexpr _chain(F1 func, F2 next, Fs... rest)
            noexcept (
                meta::nothrow::convertible_to<F1, type> &&
                meta::nothrow::constructible_from<_chain<F2, Fs...>, F2, Fs...>
            )
        :
            _chain<F2, Fs...>(std::forward<F2>(next), std::forward<Fs>(rest)...),
            m_storage(std::forward<F1>(func))
        {}

        constexpr _chain(const _chain&) = default;
        constexpr _chain(_chain&&) = default;

        constexpr _chain& operator=(const _chain& other)
            noexcept (meta::nothrow::copy_assignable<_chain<F2, Fs...>> && (
                meta::lvalue<F1> || meta::nothrow::copy_assignable<F1> || (
                    meta::nothrow::copyable<F1> && meta::nothrow::swappable<F1>
                )
            ))
            requires (meta::copy_assignable<_chain<F2, Fs...>> && (
                meta::lvalue<F1> || meta::copy_assignable<F1> || (
                    meta::copyable<F1> && meta::swappable<F1>
                )
            ))
        {
            _chain<F2, Fs...>::operator=(other);

            if constexpr (meta::lvalue<F1>) {
                std::construct_at(&m_storage, other.m_storage);

            } else if constexpr (meta::copy_assignable<F1>) {
                m_storage.value = other.m_storage.value;

            } else {
                _chain temp(other);
                swap(temp);
            }

            return *this;
        }

        constexpr _chain& operator=(_chain&& other)
            noexcept (
                meta::nothrow::move_assignable<_chain<F2, Fs...>> && (
                    meta::lvalue<F1> || meta::nothrow::move_assignable<F1> ||
                    meta::nothrow::swappable<F1>
                )
            )
            requires (meta::move_assignable<_chain<F2, Fs...>> && (
                meta::lvalue<F1> || meta::move_assignable<F1> || meta::swappable<F1>
            ))
        {
            _chain<F2, Fs...>::operator=(std::move(other));

            if constexpr (meta::lvalue<F1>) {
                std::construct_at(&m_storage, other.m_storage);

            } else if constexpr (meta::move_assignable<F1>) {
                m_storage.value = std::move(other.m_storage.value);

            } else {
                swap(other);
            }

            return *this;
        }

        constexpr void swap(_chain& other)
            noexcept (meta::nothrow::swappable<_chain<F2, Fs...>> && meta::nothrow::swappable<F1>)
            requires (meta::swappable<_chain<F2, Fs...>> && meta::swappable<F1>)
        {
            _chain<F2, Fs...>::swap(other);
            std::ranges::swap(m_storage.value, other.m_storage.value);
        }
    };

    /* A higher-order function that merges a sequence of component functions into a
    single operation.  When called, the chain will evaluate the first function with the
    input arguments, then pass the result to the next function, and so on, until all
    functions have been evaluated.  If the first function supports partial binding or
    explicit template parameters, then they will be transparently forwarded to the
    chain's public interface as well. */
    template <meta::not_void F1, meta::not_void F2, meta::not_void... Fs>
    struct chain : impl::_chain<F1, F2, Fs...> {
        using functions = meta::pack<F1, F2, Fs...>;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        using base = impl::_chain<F1, F2, Fs...>;

        template <size_type... Is, typename Self>
        static constexpr auto _clear(std::index_sequence<Is...>, Self&& self)
            noexcept (requires{{impl::chain{
                base::template get<0>(std::forward<Self>(self)).clear(),
                base::template get<1 + Is>(std::forward<Self>(self))...
            }} noexcept;})
            requires (requires{{impl::chain{
                base::template get<0>(std::forward<Self>(self)).clear(),
                base::template get<1 + Is>(std::forward<Self>(self))...
            }};})
        {
            return impl::chain{
                base::template get<0>(std::forward<Self>(self)).clear(),
                base::template get<1 + Is>(std::forward<Self>(self))...
            };
        }

        template <auto... T, size_type... Is, typename Self, typename... A>
        static constexpr auto _bind(
            std::index_sequence<Is...>,
            Self&& self,
            A&&... args
        )
            noexcept (requires{{impl::chain{
                base::template get<0>(std::forward<Self>(self)).template bind<T...>(
                    std::forward<A>(args)...
                ),
                base::template get<1 + Is>(std::forward<Self>(self))...
            }} noexcept;})
            requires (requires{{impl::chain{
                base::template get<0>(std::forward<Self>(self)).template bind<T...>(
                    std::forward<A>(args)...
                ),
                base::template get<1 + Is>(std::forward<Self>(self))...
            }};})
        {
            return impl::chain{
                base::template get<0>(std::forward<Self>(self)).template bind<T...>(
                    std::forward<A>(args)...
                ),
                base::template get<1 + Is>(std::forward<Self>(self))...
            };
        }

        template <size_type... Is, typename Self, typename Other>
        static constexpr auto append(
            std::index_sequence<Is...>,
            Self&& self,
            Other&& other
        )
            noexcept (requires{{impl::chain{
                base::template get<Is>(std::forward<Self>(self))...,
                std::forward<Other>(other)
            }} noexcept;})
            requires (requires{{impl::chain{
                base::template get<Is>(std::forward<Self>(self))...,
                std::forward<Other>(other)
            }};})
        {
            return impl::chain{
                base::template get<Is>(std::forward<Self>(self))...,
                std::forward<Other>(other)
            };
        }

        template <size_type... Is, size_type... Js, typename Self, typename Other>
        static constexpr auto append(
            std::index_sequence<Is...>,
            std::index_sequence<Js...>,
            Self&& self,
            Other&& other
        )
            noexcept (requires{{impl::chain{
                base::template get<Is>(std::forward<Self>(self))...,
                std::forward<Other>(other).template get<Js>()...
            }} noexcept;})
            requires (requires{{impl::chain{
                base::template get<Is>(std::forward<Self>(self))...,
                std::forward<Other>(other).template get<Js>()...
            }};})
        {
            return impl::chain{
                base::template get<Is>(std::forward<Self>(self))...,
                std::forward<Other>(other).template get<Js>()...
            };
        }

    public:
        /* The number of component functions in the chain, as an unsigned integer. */
        [[nodiscard]] static constexpr size_type size() noexcept { return sizeof...(Fs) + 2; }

        /* The number of component functions in the chain, as a signed integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }

        /* CTAD constructor saves a sequence of functions, retaining proper
        lvalue/rvalue ctagories and cv qualifiers in the template signature.  Note that
        no checks are made to ensure that the functions form a valid chain, as doing so
        in a way that allows overloads and templates requires a complete argument list
        for evaluation.  Such checks are thus deferred to the call operator itself. */
        [[nodiscard]] constexpr chain(F1 func, F2 next, Fs... rest)
            noexcept (requires{{base(
                std::forward<F1>(func),
                std::forward<F2>(next),
                std::forward<Fs>(rest)...
            )} noexcept;})
        :
            base(std::forward<F1>(func), std::forward<F2>(next), std::forward<Fs>(rest)...)
        {}

        /* Copying a `chain{}` will copy all of its functions.  Note that lvalues are
        trivially copied, meaning the new chain will reference the exact same objects
        as the original, and will not extend their lifetimes in any way. */
        [[nodiscard]] constexpr chain(const chain&) = default;

        /* Moving a `chain{}` will transfer all of its contents.  Note that lvalues are
        trivially moved, meaning the new chain will reference the exact same objects as
        the original, and will not extend their lifetimes in any way. */
        [[nodiscard]] constexpr chain(chain&&) = default;

        /* Copy assigning to a `chain{}` will reassign all of its contents to copies of
        the incoming values.  Note that lvalues will be trivially rebound, meaning no
        chainges will occur to the referenced data.  Instead, the reference address may
        change to reflect the assignment. */
        constexpr chain& operator=(const chain& other)
            noexcept (meta::nothrow::copy_assignable<base>)
            requires (meta::copy_assignable<base>)
        {
            if (&other != this) {
                base::operator=(other);
            }
            return *this;
        }

        /* Move assigning to a `chain{}` will reassign all of its contents to the
        incoming values.  Note that lvalues will be trivially rebound, meaning no
        changes will occur to the referenced data.  Instead, the reference address may
        change to reflect the assignment. */
        constexpr chain& operator=(chain&& other)
            noexcept (meta::nothrow::move_assignable<base>)
            requires (meta::move_assignable<base>)
        {
            if (&other != this) {
                base::operator=(std::move(other));
            }
            return *this;
        }

        /* STL-compatible `swap()` method for `chain{}` functions. */
        constexpr void swap(chain& other)
            noexcept (meta::nothrow::swappable<base>)
            requires (meta::swappable<base>)
        {
            if (&other != this) {
                base::swap(other);
            }
        }

        /* Get the component function at index `I`, applying Python-style wraparound
        for negative indices.  Fails to compile if the index is out of bounds after
        normalization. */
        template <index_type I, typename Self> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return (base::template get<impl::normalize_index<ssize(), I>()>(
                std::forward<Self>(self)
            ));
        }

        /* Remove any partial values from the first function in the chain, assuming it
        supports partial binding.  Fails to compile otherwise.  Note that no other
        functions in the chain are affected, and they will be perfectly forwarded to
        construct the result, possibly as lvalues. */
        template <typename Self>
        [[nodiscard]] constexpr auto clear(this Self&& self)
            noexcept (requires{{_clear(
                std::make_index_sequence<size() - 1>{},
                std::forward<Self>(self)
            )} noexcept;})
            requires (requires{{_clear(
                std::make_index_sequence<size() - 1>{},
                std::forward<Self>(self)
            )};})
        {
            return _clear(
                std::make_index_sequence<size() - 1>{},
                std::forward<Self>(self)
            );
        }

        /* Bind one or more partial values to the first function in the chain, assuming
        it supports partial binding.  Fails to compile otherwise.  Note that no other
        functions in the chain are affected, and they will be perfectly forwarded to
        construct the result, possibly as lvalues. */
        template <auto... T, typename Self, typename... A>
        [[nodiscard]] constexpr auto bind(this Self&& self, A&&... args)
            noexcept (requires{{_bind<T...>(
                std::make_index_sequence<size() - 1>{},
                std::forward<Self>(self),
                std::forward<A>(args)...
            )} noexcept;})
            requires (requires{{_bind<T...>(
                std::make_index_sequence<size() - 1>{},
                std::forward<Self>(self),
                std::forward<A>(args)...
            )};})
        {
            return _bind<T...>(
                std::make_index_sequence<size() - 1>{},
                std::forward<Self>(self),
                std::forward<A>(args)...
            );
        }

        /* Invoke the function chain, piping the return value from the first function
        into the input for the second function, and so on until all functions have
        been evaluated. */
        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{base::operator()(
                std::forward<Self>(self),
                std::forward<A>(args)...
            )} noexcept;})
            requires (base::template invoke_chain<Self, A...>)
        {
            return (base::operator()(
                std::forward<Self>(self),
                std::forward<A>(args)...
            ));
        }

        /* Invoke the function chain, piping the return value from the first function
        into the input for the second function, and so on until all functions have
        been evaluated.  This overload participates in overload resolution if and only
        if explicit template parameters are provided. */
        template <auto... T, typename Self, typename... A> requires (sizeof...(T) > 0)
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{_chain<F2, Fs...>::operator()(
                std::forward<meta::qualify<_chain<F2, Fs...>, Self>>(self),
                std::forward<Self>(self).m_storage.value.template operator()<T...>(
                    std::forward<A>(args)...
                )
            )} noexcept;})
            requires (requires{
                {std::forward<Self>(self).template get<0>().template operator()<T...>(
                    std::forward<A>(args)...
                )};
            } && _chain<F2, Fs...>::template invoke_chain<
                meta::qualify<_chain<F2, Fs...>, Self>,
                decltype(std::forward<Self>(self).template get<0>().template operator()<T...>(
                    std::forward<A>(args)...
                ))
            >)
        {
            return (_chain<F2, Fs...>::operator()(
                std::forward<meta::qualify<_chain<F2, Fs...>, Self>>(self),
                std::forward<Self>(self).m_storage.value.template operator()<T...>(
                    std::forward<A>(args)...
                )
            ));
        }

        /* Equivalent to invoking the function chain directly, but easier to spell
        in the case where the function expects explicit template parameters. */
        template <typename Self, typename... A>
        constexpr decltype(auto) call(this Self&& self, A&&... args)
            noexcept (requires{{base::operator()(
                std::forward<Self>(self),
                std::forward<A>(args)...
            )} noexcept;})
            requires (base::template invoke_chain<Self, A...>)
        {
            return (base::operator()(
                std::forward<Self>(self),
                std::forward<A>(args)...
            ));
        }

        /* Equivalent to invoking the function chain directly, but easier to spell
        in the case where the function expects explicit template parameters.  This
        overload participates in overload resolution if and only if explicit template
        parameters are provided. */
        template <auto... T, typename Self, typename... A> requires (sizeof...(T) > 0)
        constexpr decltype(auto) call(this Self&& self, A&&... args)
            noexcept (requires{{_chain<F2, Fs...>::operator()(
                std::forward<meta::qualify<_chain<F2, Fs...>, Self>>(self),
                std::forward<Self>(self).m_storage.value.template operator()<T...>(
                    std::forward<A>(args)...
                )
            )} noexcept;})
            requires (requires{
                {std::forward<Self>(self).template get<0>().template operator()<T...>(
                    std::forward<A>(args)...
                )};
            } && _chain<F2, Fs...>::template invoke_chain<
                meta::qualify<_chain<F2, Fs...>, Self>,
                decltype(std::forward<Self>(self).template get<0>().template operator()<T...>(
                    std::forward<A>(args)...
                ))
            >)
        {
            return (_chain<F2, Fs...>::operator()(
                std::forward<meta::qualify<_chain<F2, Fs...>, Self>>(self),
                std::forward<Self>(self).m_storage.value.template operator()<T...>(
                    std::forward<A>(args)...
                )
            ));
        }

        /* Extend the chain with another function, which will be called with a single
        argument representing the output from the current chain.  Produces a new chain
        whose component functions represent the perfectly-forwarded contents of this
        chain. */
        template <typename Self, typename G>
        [[nodiscard]] constexpr auto operator>>(this Self&& self, G&& func)
            noexcept (requires{{append(
                std::make_index_sequence<size()>{},
                std::forward<Self>(self),
                std::forward<G>(func)
            )} noexcept;})
            requires (requires{{append(
                std::make_index_sequence<size()>{},
                std::forward<Self>(self),
                std::forward<G>(func)
            )};})
        {
            return append(
                std::make_index_sequence<size()>{},
                std::forward<Self>(self),
                std::forward<G>(func)
            );
        }

        /* Concatenate two function chains, where the second chain will be called with
        a single argument representing the output from the current chain.  Produces a
        new chain whose component functions represent the perfectly-forwarded contents
        of this chain and the second chain. */
        template <typename Self, meta::chain G>
        [[nodiscard]] constexpr auto operator>>(this Self&& self, G&& other)
            noexcept (requires{{append(
                std::make_index_sequence<size()>{},
                std::make_index_sequence<meta::unqualify<G>::size()>{},
                std::forward<Self>(self),
                std::forward<G>(other)
            )} noexcept;})
            requires (requires{{append(
                std::make_index_sequence<size()>{},
                std::make_index_sequence<meta::unqualify<G>::size()>{},
                std::forward<Self>(self),
                std::forward<G>(other)
            )};})
        {
            return append(
                std::make_index_sequence<size()>{},
                std::make_index_sequence<meta::unqualify<G>::size()>{},
                std::forward<Self>(self),
                std::forward<G>(other)
            );
        }
    };

    /* CTAD constructor allows chained function types to be inferred from an
    initializer. */
    template <typename... Fs>
    chain(Fs&&...) -> chain<meta::remove_rvalue<Fs>...>;

    template <meta::chain C>
    struct detect_signature<C> {
        using type = detect_signature<
            typename meta::unqualify<C>::functions::template at<0>
        >::type;
    };

    /* A function object produced by the `bertrand::def()` operator.  This is a thin
    wrapper that stores a Python-style signature in addition to an underlying C++
    function.  When called, it will invoke the C++ function according to the
    signature's semantics, including possible partial and default values, keyword and
    variadic arguments, and container unpacking. */
    template <meta::not_void F, meta::unqualified Sig> requires (meta::signature<Sig>)
    struct def {
        using size_type = size_t;
        using index_type = ssize_t;
        using signature_type = Sig;
        using function_type = F;

    private:

        template <auto... T>
        struct partial {
            template <typename Self, typename... A>
            using func = def<
                meta::remove_rvalue<decltype(std::declval<Self>().func)>,
                meta::unqualify<decltype(std::declval<Self>().signature.template bind<T...>(
                    std::declval<A>()...
                ))>
            >;
        };

        template <typename Self>
        using remove_partial = def<
            meta::remove_rvalue<decltype(std::declval<Self>().func)>,
            meta::unqualify<decltype(std::declval<Self>().signature.clear())>
        >;

        template <size_type... Is, typename Self, typename Other>
        static constexpr auto append(
            std::index_sequence<Is...>,
            Self&& self,
            Other&& other
        )
            noexcept (requires{{chain<
                Self,
                decltype(std::forward<Other>(other).template get<Is>())...
            >{
                std::forward<Self>(self),
                std::forward<Other>(other).template get<Is>()...
            }} noexcept;})
            requires (requires{{chain<
                Self,
                decltype(std::forward<Other>(other).template get<Is>())...
            >{
                std::forward<Self>(self),
                std::forward<Other>(other).template get<Is>()...
            }};})
        {
            return chain<
                Self,
                decltype(std::forward<Other>(other).template get<Is>())...
            >{
                std::forward<Self>(self),
                std::forward<Other>(other).template get<Is>()...
            };
        }

    public:
        function_type func;
        signature_type signature;

        /* The total number of partial arguments that have been bound to this
        function. */
        [[nodiscard]] static constexpr size_type n_partial() noexcept {
            return signature_type::n_partial();
        }

        /* Return a new `def` wrapper without any partial arguments, resetting the
        function to its unbound state. */
        template <typename Self>
        [[nodiscard]] constexpr auto clear(this Self&& self)
            noexcept (requires{{remove_partial<Self>{
                std::forward<Self>(self).func,
                std::forward<Self>(self).signature.clear()
            }} noexcept;})
            requires (requires{{remove_partial<Self>{
                std::forward<Self>(self).func,
                std::forward<Self>(self).signature.clear()
            }};})
        {
            return remove_partial<Self>{
                std::forward<Self>(self).func,
                std::forward<Self>(self).signature.clear()
            };
        }

        /* Bind one or more partial values to the function's arguments, as if calling
        the function directly.  Returns a new `def` wrapper with the partial values
        applied, perfectly forwarding both the underlying function and any existing
        partial values.  Note that no attempt is made to extend the lifetime of lvalue
        partials, so it is up to the user to ensure proper lifetimes.  On the other
        hand, rvalues and prvalues will be stored internally, extending their lifetime
        to match that of the `def` wrapper itself. */
        template <auto... T, typename Self, typename... A>
            requires (
                meta::source_args<decltype(T)...> &&
                meta::source_args<A...> &&
                (!meta::arg_pack<decltype(T)> && ...) &&
                (!meta::kwarg_pack<decltype(T)> && ...) &&
                (!meta::arg_pack<A> && ...) &&
                (!meta::kwarg_pack<A> && ...) &&
                meta::bind_error<Sig::template bind_templates<decltype(T)...>> &&
                meta::bind_error<Sig::template bind_params<A...>>
            )
        [[nodiscard]] constexpr auto bind(this Self&& self, A&&... args)
            noexcept (requires{{typename partial<T...>::template func<Self, A...>{
                std::forward<Self>(self).func,
                std::forward<Self>(self).signature.template bind<T...>(std::forward<A>(args)...)
            }} noexcept;})
            requires (requires{{typename partial<T...>::template func<Self, A...>{
                std::forward<Self>(self).func,
                std::forward<Self>(self).signature.template bind<T...>(std::forward<A>(args)...)
            }};})
        {
            return typename partial<T...>::template func<Self, A...>{
                std::forward<Self>(self).func,
                std::forward<Self>(self).signature.template bind<T...>(std::forward<A>(args)...)
            };
        }

        /* Invoke the function with the provided arguments, parsing them according to
        the enclosed signature.  Partial and default values will be inserted as needed,
        and keyword arguments will be resolved statically as part of the call logic.
        Container unpacking operators are also allowed for supported container types,
        and any unions that are present in the arguments will be automatically visited
        during invocation.  Explicit template parameters may be provided by calling
        this method as a template operator, or by using the `call()` method. */
        template <auto... T, typename Self, typename... A>
            requires (
                meta::source_args<decltype(T)...> &&
                meta::source_args<A...> &&
                meta::bind_error<Sig::template call_templates<decltype(T)...>> &&
                meta::bind_error<Sig::template call_params<A...>>
            )
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<Self>(self).signature.template operator()<T...>(
                std::forward<Self>(self).func,
                std::forward<A>(args)...
            )} noexcept;})
            requires (signature_type::template specialize<T...>::template visit<
                decltype(std::forward<Self>(self).signature),
                decltype(std::forward<Self>(self).func),
                A...
            >)
        {
            return (std::forward<Self>(self).signature.template operator()<T...>(
                std::forward<Self>(self).func,
                std::forward<A>(args)...
            ));
        }

        /* Identical to `operator()`, but with a more easily spellable name when
        calling the function with explicit template parameters. */
        template <auto... T, typename Self, typename... A>
            requires (
                meta::source_args<decltype(T)...> &&
                meta::source_args<A...> &&
                meta::bind_error<Sig::template call_templates<decltype(T)...>> &&
                meta::bind_error<Sig::template call_params<A...>>
            )
        constexpr decltype(auto) call(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<Self>(self).signature.template operator()<T...>(
                std::forward<Self>(self).func,
                std::forward<A>(args)...
            )} noexcept;})
            requires (signature_type::template specialize<T...>::template visit<
                decltype(std::forward<Self>(self).signature),
                decltype(std::forward<Self>(self).func),
                A...
            >)
        {
            return (std::forward<Self>(self).signature.template operator()<T...>(
                std::forward<Self>(self).func,
                std::forward<A>(args)...
            ));
        }

        /* Form a function chain starting with this function. */
        template <typename Self, typename Other> requires (!meta::chain<Other>)
        [[nodiscard]] constexpr auto operator>>(this Self&& self, Other&& other)
            noexcept (requires{
                {chain{std::forward<Self>(self), std::forward<Other>(other)}} noexcept;
            })
            requires (requires{
                {chain{std::forward<Self>(self), std::forward<Other>(other)}};
            })
        {
            return chain{std::forward<Self>(self), std::forward<Other>(other)};
        }

        /* Append this function to the beginning of a function chain. */
        template <typename Self, meta::chain Other>
        [[nodiscard]] constexpr auto operator>>(this Self&& self, Other&& other)
            noexcept (requires{{append(
                std::make_index_sequence<meta::unqualify<Other>::size()>{},
                std::forward<Self>(self),
                std::forward<Other>(other)
            )} noexcept;})
            requires (requires{{append(
                std::make_index_sequence<meta::unqualify<Other>::size()>{},
                std::forward<Self>(self),
                std::forward<Other>(other)
            )};})
        {
            return append(
                std::make_index_sequence<meta::unqualify<Other>::size()>{},
                std::forward<Self>(self),
                std::forward<Other>(other)
            );
        }
    };

    template <typename F, const auto& T, const auto&... A>
    struct _make_signature { using type = signature<T, A...>; };
    template <typename F, const auto& T, const auto&... A>
        requires (T.empty() && sizeof...(A) == 0)
    struct _make_signature<F, T, A...> { using type = detect_signature<F>::type; };
    template <typename F, templates T, const auto&... A>
    using make_signature = _make_signature<F, T, A...>::type;

    template <typename F, templates T, const auto&... A>
    using make_def = def<meta::remove_rvalue<F>, make_signature<F, T, A...>>;

    template <meta::def F>
    struct detect_signature<F> {
        using type = meta::unqualify<F>::signature_type;
    };

    // static_assert(meta::signature<signature<{}, "x"_>>);

}


/* Create a Python-style function in pure C++.

This operator accepts one or more C++ functions, which may be lambda expressions,
function pointers, and/or function objects, and returns an `impl::def` wrapper that
applies Python semantics to the overall signature.  Explicit template parameters
may be provided to customize the signature according to Python syntax, allowing
keyword arguments, default values, variadic arguments, and optional types.

    ```
    constexpr auto f = def<"x"_, ("y"_[^int] = 2)>([](int x, int y) {
        return x - y;
    });
    static_assert(f(1) == -1);
    static_assert(f("y"_ = 2, "x"_ = 1) == -1);
    static_assert(f.bind("x"_ = 1)(2) == -1);
    ```

If no explicit template parameters are provided, the signature will be inferred from
the functions themselves, replicating their existing C++ semantics as closely as
possible, including overloads and templates.

    ```
    constexpr auto f = def(
        [](int x, int y) { return x + y; },  // (1)
        [](double x, double y) { return x - y; },  // (2)
        [](auto x, auto y) { return x * y; }  // (3)
    );
    static_assert(f(1, 2) == 3);  // (1)
    static_assert(f(1.0, 2.0) == -1.0);  // (2)
    static_assert(f(2, 4.0) == 8.0);  // (3)
    ```

A leading initializer list can be supplied as a template to define functions that
require explicit template parameters, such as `std::get<I>()` and others.

    ```
    constexpr auto f = def<{"I"_ = 1}, "obj"_>([]<size_t I>(auto&& obj) {
        return std::get<I>(std::forward<decltype(obj)>(obj));
    });
    static_assert(f.template call<("I"_ = 2)>(std::tuple{1, 2, 3}) == 3);
    ```

Note that explicit template parameters defined in this way support all the same
features as their runtime counterparts, meaning they can be supplied as keyword
arguments, variadic arguments, and so on.  They can also be bound to partial
values, allowing for partial application at both compile time and runtime.

    ```
    constexpr auto f = def<{"I"_ = 1}, "obj"_>([]<size_t I>(auto&& obj) {
        return std::get<I>(std::forward<decltype(obj)>(obj));
    });
    static_assert(f.template bind<("I"_ = 2)>()(std::tuple{1, 2, 3}) == 3);
    static_assert(f.bind(std::tuple{1, 2, 3}).template call<2>() == 3);
    ```

See "Functions" in the API documentation for more details on how Bertrand functions may
be defined and used. */
template <meta::arg auto... A, typename F>
[[nodiscard]] constexpr auto def(F&& f)
    noexcept (requires{{impl::make_def<F, {}, A...>{
        std::forward<F>(f),
        impl::make_signature<F, {}, A...>{}
    }} noexcept;})
    requires (requires{{impl::make_def<F, {}, A...>{
        std::forward<F>(f),
        impl::make_signature<F, {}, A...>{}
    }};})
{
    return impl::make_def<F, {}, A...>{
        std::forward<F>(f),
        impl::make_signature<F, {}, A...>{}
    };
}


/* Create a Python-style function in pure C++.

This operator accepts one or more C++ functions, which may be lambda expressions,
function pointers, and/or function objects, and returns an `impl::def` wrapper that
applies Python semantics to the overall signature.  Explicit template parameters
may be provided to customize the signature according to Python syntax, allowing
keyword arguments, default values, variadic arguments, and optional types.

    ```
    constexpr auto f = def<"x"_, ("y"_[^int] = 2)>([](int x, int y) {
        return x - y;
    });
    static_assert(f(1) == -1);
    static_assert(f("y"_ = 2, "x"_ = 1) == -1);
    static_assert(f.bind("x"_ = 1)(2) == -1);
    ```

If no explicit template parameters are provided, the signature will be inferred from
the functions themselves, replicating their existing C++ semantics as closely as
possible, including overloads and templates.

    ```
    constexpr auto f = def(
        [](int x, int y) { return x + y; },  // (1)
        [](double x, double y) { return x - y; },  // (2)
        [](auto x, auto y) { return x * y; }  // (3)
    );
    static_assert(f(1, 2) == 3);  // (1)
    static_assert(f(1.0, 2.0) == -1.0);  // (2)
    static_assert(f(2, 4.0) == 8.0);  // (3)
    ```

A leading initializer list can be supplied as a template to define functions that
require explicit template parameters, such as `std::get<I>()` and others.

    ```
    constexpr auto f = def<{"I"_ = 1}, "obj"_>([]<size_t I>(auto&& obj) {
        return std::get<I>(std::forward<decltype(obj)>(obj));
    });
    static_assert(f.template call<("I"_ = 2)>(std::tuple{1, 2, 3}) == 3);
    ```

Note that explicit template parameters defined in this way support all the same
features as their runtime counterparts, meaning they can be supplied as keyword
arguments, variadic arguments, and so on.  They can also be bound to partial
values, allowing for partial application at both compile time and runtime.

    ```
    constexpr auto f = def<{"I"_ = 1}, "obj"_>([]<size_t I>(auto&& obj) {
        return std::get<I>(std::forward<decltype(obj)>(obj));
    });
    static_assert(f.template bind<("I"_ = 2)>()(std::tuple{1, 2, 3}) == 3);
    static_assert(f.bind(std::tuple{1, 2, 3}).template call<2>() == 3);
    ```

See "Functions" in the API documentation for more details on how Bertrand functions may
be defined and used. */
template <impl::templates T, meta::arg auto... A, typename F> requires (!T.empty())
[[nodiscard]] constexpr auto def(F&& f)
    noexcept (requires{{impl::make_def<F, T, A...>{
        std::forward<F>(f),
        impl::make_signature<F, T, A...>{}
    }} noexcept;})
    requires (requires{{impl::make_def<F, T, A...>{
        std::forward<F>(f),
        impl::make_signature<F, T, A...>{}
    }};})
{
    return impl::make_def<F, T, A...>{
        std::forward<F>(f),
        impl::make_signature<F, T, A...>{}
    };
}


/* Create a Python-style function in pure C++.

This operator accepts one or more C++ functions, which may be lambda expressions,
function pointers, and/or function objects, and returns an `impl::def` wrapper that
applies Python semantics to the overall signature.  Explicit template parameters
may be provided to customize the signature according to Python syntax, allowing
keyword arguments, default values, variadic arguments, and optional types.

    ```
    constexpr auto f = def<"x"_, ("y"_[^int] = 2)>([](int x, int y) {
        return x - y;
    });
    static_assert(f(1) == -1);
    static_assert(f("y"_ = 2, "x"_ = 1) == -1);
    static_assert(f.bind("x"_ = 1)(2) == -1);
    ```

If no explicit template parameters are provided, the signature will be inferred from
the functions themselves, replicating their existing C++ semantics as closely as
possible, including overloads and templates.

    ```
    constexpr auto f = def(
        [](int x, int y) { return x + y; },  // (1)
        [](double x, double y) { return x - y; },  // (2)
        [](auto x, auto y) { return x * y; }  // (3)
    );
    static_assert(f(1, 2) == 3);  // (1)
    static_assert(f(1.0, 2.0) == -1.0);  // (2)
    static_assert(f(2, 4.0) == 8.0);  // (3)
    ```

A leading initializer list can be supplied as a template to define functions that
require explicit template parameters, such as `std::get<I>()` and others.

    ```
    constexpr auto f = def<{"I"_ = 1}, "obj"_>([]<size_t I>(auto&& obj) {
        return std::get<I>(std::forward<decltype(obj)>(obj));
    });
    static_assert(f.template call<("I"_ = 2)>(std::tuple{1, 2, 3}) == 3);
    ```

Note that explicit template parameters defined in this way support all the same
features as their runtime counterparts, meaning they can be supplied as keyword
arguments, variadic arguments, and so on.  They can also be bound to partial
values, allowing for partial application at both compile time and runtime.

    ```
    constexpr auto f = def<{"I"_ = 1}, "obj"_>([]<size_t I>(auto&& obj) {
        return std::get<I>(std::forward<decltype(obj)>(obj));
    });
    static_assert(f.template bind<("I"_ = 2)>()(std::tuple{1, 2, 3}) == 3);
    static_assert(f.bind(std::tuple{1, 2, 3}).template call<2>() == 3);
    ```

See "Functions" in the API documentation for more details on how Bertrand functions may
be defined and used. */
template <meta::arg auto... A, typename... F> requires (sizeof...(F) > 1)
[[nodiscard]] constexpr auto def(F&&... fs)
    noexcept (requires{{impl::make_def<impl::overloads<meta::unqualify<F>...>, {}, A...>{
        impl::overloads<meta::unqualify<F>...>{std::forward<F>(fs)...},
        impl::make_signature<impl::overloads<meta::unqualify<F>...>, {}, A...>{}
    }} noexcept;})
    requires (requires{{impl::make_def<impl::overloads<meta::unqualify<F>...>, {}, A...>{
        impl::overloads<meta::unqualify<F>...>{std::forward<F>(fs)...},
        impl::make_signature<impl::overloads<meta::unqualify<F>...>, {}, A...>{}
    }};})
{
    return impl::make_def<impl::overloads<meta::unqualify<F>...>, {}, A...>{
        impl::overloads<meta::unqualify<F>...>{std::forward<F>(fs)...},
        impl::make_signature<impl::overloads<meta::unqualify<F>...>, {}, A...>{}
    };
}


/* Create a Python-style function in pure C++.

This operator accepts one or more C++ functions, which may be lambda expressions,
function pointers, and/or function objects, and returns an `impl::def` wrapper that
applies Python semantics to the overall signature.  Explicit template parameters
may be provided to customize the signature according to Python syntax, allowing
keyword arguments, default values, variadic arguments, and optional types.

    ```
    constexpr auto f = def<"x"_, ("y"_[^int] = 2)>([](int x, int y) {
        return x - y;
    });
    static_assert(f(1) == -1);
    static_assert(f("y"_ = 2, "x"_ = 1) == -1);
    static_assert(f.bind("x"_ = 1)(2) == -1);
    ```

If no explicit template parameters are provided, the signature will be inferred from
the functions themselves, replicating their existing C++ semantics as closely as
possible, including overloads and templates.

    ```
    constexpr auto f = def(
        [](int x, int y) { return x + y; },  // (1)
        [](double x, double y) { return x - y; },  // (2)
        [](auto x, auto y) { return x * y; }  // (3)
    );
    static_assert(f(1, 2) == 3);  // (1)
    static_assert(f(1.0, 2.0) == -1.0);  // (2)
    static_assert(f(2, 4.0) == 8.0);  // (3)
    ```

A leading initializer list can be supplied as a template to define functions that
require explicit template parameters, such as `std::get<I>()` and others.

    ```
    constexpr auto f = def<{"I"_ = 1}, "obj"_>([]<size_t I>(auto&& obj) {
        return std::get<I>(std::forward<decltype(obj)>(obj));
    });
    static_assert(f.template call<("I"_ = 2)>(std::tuple{1, 2, 3}) == 3);
    ```

Note that explicit template parameters defined in this way support all the same
features as their runtime counterparts, meaning they can be supplied as keyword
arguments, variadic arguments, and so on.  They can also be bound to partial
values, allowing for partial application at both compile time and runtime.

    ```
    constexpr auto f = def<{"I"_ = 1}, "obj"_>([]<size_t I>(auto&& obj) {
        return std::get<I>(std::forward<decltype(obj)>(obj));
    });
    static_assert(f.template bind<("I"_ = 2)>()(std::tuple{1, 2, 3}) == 3);
    static_assert(f.bind(std::tuple{1, 2, 3}).template call<2>() == 3);
    ```

See "Functions" in the API documentation for more details on how Bertrand functions may
be defined and used. */
template <impl::templates T, meta::arg auto... A, typename... F>
    requires (!T.empty() && sizeof...(F) > 1)
[[nodiscard]] constexpr auto def(F&&... fs)
    noexcept (requires{{impl::make_def<impl::overloads<meta::unqualify<F>...>, T, A...>{
        impl::overloads<meta::unqualify<F>...>{std::forward<F>(fs)...},
        impl::make_signature<impl::overloads<meta::unqualify<F>...>, T, A...>{}
    }} noexcept;})
    requires (requires{{impl::make_def<impl::overloads<meta::unqualify<F>...>, T, A...>{
        impl::overloads<meta::unqualify<F>...>{std::forward<F>(fs)...},
        impl::make_signature<impl::overloads<meta::unqualify<F>...>, T, A...>{}
    }};})
{
    return impl::make_def<impl::overloads<meta::unqualify<F>...>, T, A...>{
        impl::overloads<meta::unqualify<F>...>{std::forward<F>(fs)...},
        impl::make_signature<impl::overloads<meta::unqualify<F>...>, T, A...>{}
    };
}













// inline constexpr auto f = def(
//     [](int x, int y) { return x + y; },
//     [](double x, double y) { return x - y; }
// );
// static_assert(f(1.0, 2.0) == -1.0);

inline constexpr auto f2 = def<{"z"_ = 1}, "x"_, "y"_>([]<int z>(int x, int y) {
    return x + y + z;
});
static_assert(f2.bind<("z"_ = 2)>()(2, "y"_ = 1) == 5);

inline constexpr auto f3 = f2 >> [](int result) { return result * 2; };
static_assert(f3.bind("y"_ = 1).clear().template call<2>(2, 1) == 10);



static_assert(std::tuple{1, 2, 3} ->* def([](int x, int y, int z) {
    return x + y + z;
}) == 6);


}  // namespace bertrand


/* Specializing `std::tuple_size`, `std::tuple_element`, and `std::get` allows
saved arguments, tuple comprehensions, signatures, overload sets, and function chains
to be decomposed using structured bindings. */
namespace std {

    ////////////////////
    ////    ARGS    ////
    ////////////////////

    template <bertrand::meta::args T>
    struct tuple_size<T> : std::integral_constant<
        typename std::remove_cvref_t<T>::size_type,
        std::remove_cvref_t<T>::size()
    > {};

    template <size_t I, bertrand::meta::args T> requires (I < std::tuple_size<T>::value)
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <auto... A, bertrand::meta::args T>
        requires (!bertrand::meta::static_str<decltype(A)> || ...)
    [[nodiscard]] constexpr decltype(auto) get(T&& args)
        noexcept (requires{{std::forward<T>(args).template get<A...>()} noexcept;})
        requires (requires{{std::forward<T>(args).template get<A...>()};})
    {
        return (std::forward<T>(args).template get<A...>());
    }

    template <bertrand::static_str... S, bertrand::meta::args T>
    [[nodiscard]] constexpr decltype(auto) get(T&& args)
        noexcept (requires{{std::forward<T>(args).template get<S...>()} noexcept;})
        requires (requires{{std::forward<T>(args).template get<S...>()};})
    {
        return (std::forward<T>(args).template get<S...>());
    }

    //////////////////////////////
    ////    COMPREHENSIONS    ////
    //////////////////////////////

    /// TODO: restrict to views and not comprehensions?
    template <bertrand::meta::comprehension T>
    struct tuple_size<T> : std::tuple_size<typename std::remove_cvref_t<T>::container_type> {};

    template <size_t I, bertrand::meta::comprehension T> requires (I < std::tuple_size<T>::value)
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <auto... A, bertrand::meta::comprehension T>
    [[nodiscard]] constexpr decltype(auto) get(T&& comprehension)
        noexcept (requires{{std::forward<T>(comprehension).template get<A...>()} noexcept;})
        requires (requires{{std::forward<T>(comprehension).template get<A...>()};})
    {
        return (std::forward<T>(comprehension).template get<A...>());
    }

    //////////////////////////
    ////    SIGNATURES    ////
    //////////////////////////

    template <bertrand::meta::signature T>
    struct tuple_size<T> : std::integral_constant<
        typename std::remove_cvref_t<T>::size_type,
        std::remove_cvref_t<T>::size()
    > {};

    template <size_t I, bertrand::meta::signature T> requires (I < std::tuple_size<T>::value)
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <auto... A, bertrand::meta::signature T>
        requires (!bertrand::meta::static_str<decltype(A)> || ...)
    [[nodiscard]] constexpr decltype(auto) get(T&& sig)
        noexcept (requires{{std::forward<T>(sig).template get<A...>()} noexcept;})
        requires (requires{{std::forward<T>(sig).template get<A...>()};})
    {
        return (std::forward<T>(sig).template get<A...>());
    }

    template <bertrand::static_str... S, bertrand::meta::signature T>
    [[nodiscard]] constexpr decltype(auto) get(T&& sig)
        noexcept (requires{{std::forward<T>(sig).template get<S...>()} noexcept;})
        requires (requires{{std::forward<T>(sig).template get<S...>()};})
    {
        return (std::forward<T>(sig).template get<S...>());
    }

    ////////////////////////
    ////    OVERLOADS   ////
    ////////////////////////

    template <bertrand::meta::overloads T>
    struct tuple_size<T> : std::integral_constant<
        typename std::remove_cvref_t<T>::size_type,
        std::remove_cvref_t<T>::size()
    > {};

    template <size_t I, bertrand::meta::overloads T> requires (I < std::tuple_size<T>::value)
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <auto... A, bertrand::meta::overloads T>
    [[nodiscard]] constexpr decltype(auto) get(T&& overloads)
        noexcept (requires{{std::forward<T>(overloads).template get<A...>()} noexcept;})
        requires (requires{{std::forward<T>(overloads).template get<A...>()};})
    {
        return (std::forward<T>(overloads).template get<A...>());
    }

    //////////////////////
    ////    CHAINS    ////
    //////////////////////

    template <bertrand::meta::chain T>
    struct tuple_size<T> : std::integral_constant<
        typename std::remove_cvref_t<T>::size_type,
        std::remove_cvref_t<T>::size()
    > {};

    template <size_t I, bertrand::meta::chain T> requires (I < std::tuple_size<T>::value)
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <auto... A, bertrand::meta::chain T>
    [[nodiscard]] constexpr decltype(auto) get(T&& chain)
        noexcept (requires{{std::forward<T>(chain).template get<A...>()} noexcept;})
        requires (requires{{std::forward<T>(chain).template get<A...>()};})
    {
        return (std::forward<T>(chain).template get<A...>());
    }

}


// /* The dereference operator can be used to emulate Python container unpacking when
// calling a Python-style function from C++.

// A single unpacking operator passes the contents of an iterable container as positional
// arguments to a function.  Unlike Python, only one such operator is allowed per call,
// and it must be the last positional argument in the parameter list.  This allows the
// compiler to ensure that the container's value type is minimally convertible to each of
// the remaining positional arguments ahead of time, even though the number of arguments
// cannot be determined until runtime.  Thus, if any arguments are missing or extras are
// provided, the call will raise an exception similar to Python, rather than failing
// statically at compile time.  This can be avoided by using standard positional and
// keyword arguments instead, which can be fully verified at compile time, or by including
// variadic positional arguments in the function signature, which will consume any
// remaining arguments according to Python semantics.

// A second unpacking operator promotes the arguments into keywords, and can only be used
// if the container is mapping-like, meaning it possess both `::key_type` and
// `::mapped_type` aliases, and that indexing it with an instance of the key type returns
// a value of the mapped type.  The actual unpacking is robust, and will attempt to use
// iterators over the container to produce key-value pairs, either directly through
// `begin()` and `end()` or by calling the `.items()` method if present, followed by
// zipping `.keys()` and `.values()` if both exist, and finally by iterating over the keys
// and indexing into the container.  Similar to the positional unpacking operator, only
// one of these may be present as the last keyword argument in the parameter list, and a
// compile-time check is made to ensure that the mapped type is convertible to any missing
// keyword arguments that are not explicitly provided at the call site.

// In both cases, the extra runtime complexity results in a small performance degradation
// over a typical function call, which is minimized as much as possible. */
// template <bertrand::meta::iterable T> requires (bertrand::meta::unpack_operator<T>)
// [[nodiscard]] constexpr auto operator*(T&& value) {
//     return bertrand::arg_pack{std::forward<T>(value)};
// }


// /* Apply a C++ range adaptor to a container via the comprehension operator.  This is
// similar to the C++-style `|` operator for chaining range adaptors, but uses the `->*`
// operator to avoid conflicts with other operator overloads and apply higher precedence
// than typical binary operators. */
// template <typename T, typename V>
//     requires (
//         bertrand::meta::comprehension_operator<T> &&
//         bertrand::meta::unpackable<T, V>
//     )
// [[nodiscard]] constexpr auto operator->*(T&& value, V&& view) {
//     return std::views::all(std::forward<T>(value)) | std::forward<V>(view);
// }


// /* Generate a C++ range adaptor that approximates a Python-style list comprehension.
// This is done by piping a function in place of a C++ range adaptor, which will be
// applied to each element in the sequence.  The function must be callable with the
// container's value type, and may return any type.

// If the function returns another range adaptor, then the adaptor's output will be
// flattened into the parent range, similar to a nested `for` loop within a Python
// comprehension.  Returning a range with no elements will effectively filter out the
// current element, similar to a Python `if` clause within a comprehension.

// Here's an example:

//     std::vector vec = {1, 2, 3, 4, 5};
//     std::vector new_vec = vec->*[](int x) {
//         return std::views::repeat(x, x % 2 ? 0 : x);
//     };
//     for (int x : new_vec) {
//         std::cout << x << ", ";  // 2, 2, 4, 4, 4, 4,
//     }

// This is functionally equivalent to `std::views::transform()` in C++ and uses that
// implementation under the hood.  The only difference is the added logic for flattening
// nested ranges, which is extremely lightweight. */
// template <typename T, typename F>
//     requires (
//         bertrand::meta::comprehension_operator<T> &&
//         bertrand::meta::transformable<T, F>
//     )
// [[nodiscard]] constexpr auto operator->*(T&& value, F&& func) {
//     return bertrand::comprehension{std::forward<T>(value), std::forward<F>(func)};
// }


#endif  // BERTRAND_FUNC_H
