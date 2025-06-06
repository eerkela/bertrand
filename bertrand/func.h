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
    struct arg_tag {};
    struct partial_tag {};
    struct template_arg_tag {};
    struct standard_arg_tag : arg_tag {};
    struct variadic_tag : arg_tag {};
    struct var_args_tag : variadic_tag {};
    struct var_kwargs_tag : variadic_tag {};
    struct arg_pack_tag : var_args_tag {};
    struct kwarg_pack_tag : var_kwargs_tag {};
    struct arg_sep_tag : arg_tag {};
    struct posonly_sep_tag : arg_sep_tag {};
    struct kwonly_sep_tag : arg_sep_tag {};
    struct return_annotation_tag : arg_tag {};
}


namespace meta {

    template <typename T>
    concept arg = inherits<T, impl::arg_tag>;

    template <typename T>
    concept partial_arg = arg<T> && inherits<T, impl::partial_tag>;

    template <typename T>
    concept template_arg = arg<T> && inherits<T, impl::template_arg_tag>;

    template <typename T>
    concept standard_arg = arg<T> && inherits<T, impl::standard_arg_tag>;

    template <typename T>
    concept variadic = arg<T> && inherits<T, impl::variadic_tag>;

    template <typename T>
    concept variadic_positional = variadic<T> && inherits<T, impl::var_args_tag>;

    template <typename T>
    concept variadic_keyword = variadic<T> && inherits<T, impl::var_kwargs_tag>;

    template <typename T>
    concept arg_pack = variadic<T> && inherits<T, impl::arg_pack_tag>;

    template <typename T>
    concept kwarg_pack = variadic<T> && inherits<T, impl::kwarg_pack_tag>;

    template <typename T>
    concept arg_separator = arg<T> && inherits<T, impl::arg_sep_tag>;

    template <typename T>
    concept positional_only_separator = arg_separator<T> && inherits<T, impl::posonly_sep_tag>;

    template <typename T>
    concept keyword_only_separator = arg_separator<T> && inherits<T, impl::kwonly_sep_tag>;

    template <typename T>
    concept return_annotation = arg<T> && inherits<T, impl::return_annotation_tag>;

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

    template <typename T>
    concept typed_arg = arg<T> && not_void<typename meta::unqualify<T>::type>;

    template <arg T>
    using arg_type = unqualify<T>::type;

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

    template <meta::arg T>
    constexpr const auto& arg_id = unqualify<T>::id;

    template <arg T>
    using partials = meta::unqualify<T>::partials;

    namespace detail {

        template <typename T>
        constexpr bertrand::static_str arg_prefix = "";
        template <meta::return_annotation T>
        constexpr bertrand::static_str arg_prefix<T> = meta::arg_id<T>;
        template <meta::positional_only_separator T>
        constexpr bertrand::static_str arg_prefix<T> = meta::arg_id<T>;
        template <meta::keyword_only_separator T>
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
    struct arg<ID, T, void> : impl::standard_arg_tag {
        using type = T;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* Indexing the argument sets its `type`. */
        template <meta::not_void V> requires (meta::is_void<T>)
        constexpr auto operator[](std::type_identity<V> type) && noexcept {
            return arg<ID, meta::remove_rvalue<V>, void>{};
        }

        /* Assigning to the argument binds a value to it, which is interpreted as either
        a default value if the assignment is done in the function signature, or as a
        keyword argument if done at the call site.  For arguments without an explicit
        type, the value is unconstrained, and can take any type. */
        template <typename V> requires (meta::is_void<T>)
        constexpr auto operator=(V&& val) &&
            noexcept (noexcept(arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)}))
            requires (requires{arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)};})
        {
            return arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)};
        }

        /* Assigning to the argument binds a value to it, which is interpreted as either
        a default value if the assignment is done in the function signature, or as a
        keyword argument if done at the call site.  If the argument has an explicit type,
        then the value must be convertible to that type, or be a materialization function
        of zero arguments that returns a convertible result. */
        template <typename V>  requires (meta::not_void<T>)
        constexpr auto operator=(V&& val) &&
            noexcept (noexcept(arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)}))
            requires (meta::convertible_to<V, T> && requires{
                arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)};
            })
        {
            return arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)};
        }

        /* Assigning to the argument binds a value to it, which is interpreted as either
        a default value if the assignment is done in the function signature, or as a
        keyword argument if done at the call site.  If the argument has an explicit type,
        then the value must be convertible to that type, or be a materialization function
        of zero arguments that returns a convertible result. */
        template <typename V>  requires (meta::not_void<T>)
        constexpr auto operator=(V&& val) &&
            noexcept (noexcept(arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)}))
            requires (
                !meta::convertible_to<V, T> &&
                meta::invoke_returns<T, V> &&
                requires{arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)};}
            )
        {
            return arg<ID, T, meta::remove_rvalue<V>>{{}, std::forward<V>(val)};
        }

        /* Calling the argument is identical to assignment, as a workaround for core
        language limitations in both Python and C++ when it comes to template parameter
        syntax. */
        template <typename V>
        constexpr auto operator()(V&& val) &&
            noexcept (noexcept(std::move(*this) = std::forward<V>(val)))
            requires (requires{std::move(*this) = std::forward<V>(val);})
        {
            return std::move(*this) = std::forward<V>(val);
        }

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `Arg` representing a variadic positional argument.  Such
    arguments can be typed, but cannot be bound. */
    template <static_str ID, typename T>
        requires (meta::valid_arg<ID> && meta::valid_args_name<ID>)
    struct arg<ID, T, void> : impl::var_args_tag {
        using type = T;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* Indexing the argument sets its `type`. */
        template <meta::not_void V> requires (meta::is_void<T>)
        constexpr auto operator[](std::type_identity<V> type) && noexcept {
            return arg<ID, meta::remove_rvalue<V>, void>{};
        }

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `Arg` representing a variadic keyword argument.  Such
    arguments can be typed, but cannot be bound. */
    template <static_str ID, typename T>
        requires (meta::valid_arg<ID> && meta::valid_kwargs_name<ID>)
    struct arg<ID, T, void> : impl::var_kwargs_tag {
        using type = T;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* Indexing the argument sets its `type`. */
        template <meta::not_void V> requires (meta::is_void<T>)
        constexpr auto operator[](std::type_identity<V> type) && noexcept {
            return arg<ID, meta::remove_rvalue<V>, void>{};
        }

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    /* A specialization of `arg` representing a positional-only separator.  Such
    arguments cannot be typed and cannot be bound. */
    template <static_str ID>
        requires (meta::valid_arg<ID> && meta::valid_posonly_separator<ID>)
    struct arg<ID, void, void> : impl::posonly_sep_tag {
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
    struct arg<ID, void, void> : impl::kwonly_sep_tag {
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
    struct arg<ID, T, void> : impl::return_annotation_tag {
        using type = T;
        using value_type = void;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        /* Indexing the argument sets its `type`. */
        template <meta::not_void V> requires (meta::is_void<T>)
        constexpr auto operator[](std::type_identity<V> type) && noexcept {
            return arg<ID, meta::remove_rvalue<V>, void>{};
        }

        /* STL-compliant `swap()` method for argument annotations.  Does nothing for
        unbound arguments. */
        constexpr void swap(arg&) noexcept {}
    };

    template <meta::arg T>
    struct choose_arg_base { using type = impl::standard_arg_tag; };
    template <meta::variadic_positional T>
    struct choose_arg_base<T> { using type = impl::var_args_tag; };
    template <meta::arg_pack T>
    struct choose_arg_base<T> { using type = impl::arg_pack_tag; };
    template <meta::variadic_keyword T>
    struct choose_arg_base<T> { using type = impl::var_kwargs_tag; };
    template <meta::kwarg_pack T>
    struct choose_arg_base<T> { using type = impl::kwarg_pack_tag; };
    template <meta::positional_only_separator T>
    struct choose_arg_base<T> { using type = impl::posonly_sep_tag; };
    template <meta::keyword_only_separator T>
    struct choose_arg_base<T> { using type = impl::kwonly_sep_tag; };
    template <meta::return_annotation T>
    struct choose_arg_base<T> { using type = impl::return_annotation_tag; };

    /* A specialization of `arg` for arguments that have been bound to a value, but
    otherwise have no explicit type.  Most call-site arguments fall into this
    category. */
    template <static_str ID, meta::not_void V> requires (meta::valid_arg<ID>)
    struct arg<ID, void, V> : choose_arg_base<arg<ID, void, void>>::type {
        using type = void;
        using value_type = V;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        [[no_unique_address]] V m_value;

        /* Unconstrained arguments never invoke materialization functions. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_value);
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
    struct arg<ID, T, V> : choose_arg_base<arg<ID, T, void>>::type {
        using type = T;
        using value_type = V;
        using partials = meta::pack<>;
        static constexpr const auto& id = ID;

        [[no_unique_address]] V m_value;

        /* If the argument is explicitly typed and the value is convertible, prefer
        implicit conversions. */
        template <typename Self>
        [[nodiscard]] constexpr T value(this Self&& self)
            noexcept (requires{
                { std::forward<Self>(self).m_value } -> meta::nothrow::convertible_to<T>;
            })
            requires (requires{
                { std::forward<Self>(self).m_value } -> meta::convertible_to<T>;
            })
        {
            return std::forward<Self>(self).m_value;
        }

        /* If the argument is explicitly typed and the value is not immediately convertible
        to that type, then it must be a materialization function that must be invoked. */
        template <typename Self>
        [[nodiscard]] constexpr T value(this Self&& self)
            noexcept (requires{
                {
                    std::forward<Self>(self).m_value()
                } noexcept -> meta::nothrow::convertible_to<T>;
            })
            requires (!requires{
                { std::forward<Self>(self).m_value } -> meta::convertible_to<T>;
            } && requires{
                { std::forward<Self>(self).m_value() } -> meta::convertible_to<T>;
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
    requires (requires{a.swap(b);})
{
    a.swap(b);
}


//////////////////////////////
////    ARGUMENT PACKS    ////
//////////////////////////////


namespace impl {
    struct args_tag {};
}


namespace meta {

    template <typename T>
    concept args = inherits<T, impl::args_tag>;

    namespace detail {

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
    requires (requires{a.swap(b);})
{
    a.swap(b);
}


namespace impl {

    template <typename...>
    struct args : args_tag {
    protected:
        template <typename S, typename F, typename... A>
        static constexpr decltype(auto) operator()(S&&, F&& f, A&&... a)
            noexcept (meta::nothrow::invocable<F, A...>)
            requires (meta::invocable<F, A...>)
        {
            return (std::forward<F>(f)(std::forward<A>(a)...));
        }

    public:
        constexpr void swap(args& other) noexcept {}
    };

    template <typename T, typename... Ts>
    struct args<T, Ts...> : args<Ts...> {
    protected:
        using type = meta::remove_rvalue<T>;
        struct storage {
            type value;
        };

        template <size_t I, typename S>
        static constexpr decltype(auto) get(S&& s) noexcept {
            if constexpr (I == 0) {
                return (std::forward<meta::qualify<args, S>>(s).m_storage.value);
            } else {
                return (args<Ts...>::template get<I - 1>(std::forward<S>(s)));
            }
        }

        template <typename S, typename F, typename... A>
        static constexpr decltype(auto) operator()(S&& s, F&& f, A&&... a)
            noexcept (noexcept(args<Ts...>::operator()(
                std::forward<S>(s),
                std::forward<F>(f),
                std::forward<A>(a)...,
                std::forward<meta::qualify<args, S>>(s).m_storage.value
            )))
            requires (requires{args<Ts...>::operator()(
                std::forward<S>(s),
                std::forward<F>(f),
                std::forward<A>(a)...,
                std::forward<meta::qualify<args, S>>(s).m_storage.value
            );})
        {
            return (args<Ts...>::operator()(
                std::forward<S>(s),
                std::forward<F>(f),
                std::forward<A>(a)...,
                std::forward<meta::qualify<args, S>>(s).m_storage.value
            ));
        }

    public:
        storage m_storage;

        template <typename A, typename... As>
        constexpr args(A&& curr, As&&... rest)
            noexcept (
                meta::nothrow::convertible_to<A, type> &&
                meta::nothrow::constructible_from<args<Ts...>, As...>
            )
        :
            args<Ts...>(std::forward<As>(rest)...),
            m_storage(std::forward<A>(curr))
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
private:
    using base = impl::args<Ts...>;

    template <typename T>
    struct convert {
        template <typename... A>
        [[nodiscard]] constexpr T operator()(A&&... args)
            noexcept (noexcept(T{std::forward<A>(args)...}))
            requires (requires{T{std::forward<A>(args)...};})
        {
            return T{std::forward<A>(args)...};
        }
    };

    template <bertrand::slice::normalized indices>
    struct get_slice {
        template <typename Self, size_t... Is>
        [[nodiscard]] static constexpr auto operator()(
            Self&& self,
            std::index_sequence<Is...> = std::make_index_sequence<indices.length>{}
        )
            noexcept (requires{{
                bertrand::args{std::forward<Self>(self).template get<
                    indices.start + Is * indices.step
                >()...}
            } noexcept;})
            requires (requires{bertrand::args{std::forward<Self>(self).template get<
                indices.start + Is * indices.step
            >()...};})
        {
            return bertrand::args{std::forward<Self>(self).template get<
                indices.start + Is * indices.step
            >()...};
        }
    };

public:
    /* A nested type listing the precise types that were used to build this parameter
    pack, for posterity.  These are reported as a `meta::pack<...>` type, which
    provides a number of compile-time utilities for inspecting the types, if needed. */
    using types = meta::pack<Ts...>;

    /* A compile-time minimal perfect hash table storing the names of all keyword
    argument annotations that are present in `Ts...`. */
    static constexpr string_map names = meta::detail::extract_keywords<Ts...>::value;

    /* The number of arguments contained within the pack, as an unsigned integer. */
    [[nodiscard]] static constexpr size_t size() noexcept {
        return sizeof...(Ts);
    }

    /* The number of arguments contained within the pack, as a signed integer. */
    [[nodiscard]] static constexpr ssize_t ssize() noexcept {
        return static_cast<ssize_t>(sizeof...(Ts));
    }

    /* True if the pack contains no arguments.  False otherwise. */
    [[nodiscard]] static constexpr bool empty() noexcept {
        return (sizeof...(Ts) == 0);
    }

    /* CTAD Constructor saves a pack of arguments for later use, retaining proper
    lvalue/rvalue categories and cv qualifiers in the template signature.  If another
    pack is present in the arguments, then it will be automatically flattened, such
    that the result represents the concatenation of each pack.  This also means that
    argument packs can never be directly nested.  If this behavior is needed for some
    reason, it can be disabled by encapsulating the arguments in another type, making
    it inaccessible in deduction. */
    [[nodiscard]] constexpr args(Ts... args)
        noexcept (noexcept(base(std::forward<Ts>(args)...)))
    :
        base(std::forward<Ts>(args)...)
    {}

    /* Copying an `args{}` container will copy all of its contents.  Note that lvalues
    are trivially copied, meaning the new container will reference the exact same
    objects as the original, and will not extend their lifetimes in any way. */
    [[nodiscard]] constexpr args(const args& other) = default;

    /* Moving an `args{}` container will transfer all of its contents.  Note that
    lvalues are trivially moved, meaning the new container will reference the exact
    same objects as the original, and will not extend their lifetimes in any way. */
    [[nodiscard]] constexpr args(args&& other) = default;

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

    /* STL-compatible swap() method for args{} instances. */ 
    constexpr void swap(args& other)
        noexcept (meta::nothrow::swappable<base>)
        requires (meta::swappable<base>)
    {
        if (&other != this) {
            base::swap(other);
        }
    }

    /* Get the argument at index I, perfectly forwarding it according to the pack's
    current cvref qualifications.  This means that if the pack is supplied as an
    lvalue, then all arguments will be forwarded as lvalues, regardless of their
    status in the template signature.  If the pack is an rvalue, then the arguments
    will be perfectly forwarded according to their original categories.  If the pack
    is cv qualified, then the result will be forwarded with those same qualifiers. */
    template <size_t I, typename Self> requires (I < types::size)
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
        return (base::template get<I>(std::forward<Self>(self)));
    }

    /* Slice the argument pack at compile time, returning a new pack with only the
    included indices.  The stored values will be perfectly forwarded if possible,
    according to the cvref qualifications of the current pack. */
    template <bertrand::slice slice, typename Self>
    [[nodiscard]] constexpr auto get(this Self&& self)
        noexcept (requires{{get_slice<slice.normalize(ssize())>::operator()(
            std::forward<Self>(self)
        )} noexcept;})
        requires (requires{get_slice<slice.normalize(ssize())>::operator()(
            std::forward<Self>(self)
        );})
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
            base::template get<names.template get<name>()>(
                std::forward<Self>(self)
            ).value()} noexcept;
        })
        requires (names.template contains<name>() && requires{
            base::template get<names.template get<name>()>(
                std::forward<Self>(self)
            ).value();
        })
    {
        return (base::template get<names.template get<name>()>(
            std::forward<Self>(self)
        ).value());
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
            base::template get<names.template get<"">()>(
                std::forward<Self>(self)
            ).template get<name>()} noexcept;
        })
        requires (!names.template contains<name>() && names.template contains<"">())
    {
        return (base::template get<names.template get<"">()>(
            std::forward<Self>(self)
        ).template get<name>());
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
            Optional{base::template get<names.template index<name>()>(
                std::forward<Self>(self)
            ).value()}} noexcept;
        })
        requires (names.template contains<name>() && requires{
            Optional{base::template get<names.template index<name>()>(
                std::forward<Self>(self)
            ).value()};
        })
    {
        return Optional{base::template get<names.template index<name>()>(
            std::forward<Self>(self)
        ).value()};
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
            base::template get<names.template get<"">()>(
                std::forward<Self>(self)
            ).template get_if<name>()} noexcept;
        })
        requires (!names.template contains<name>() && names.template contains<"">())
    {
        return (base::template get<names.template get<"">()>(
            std::forward<Self>(self)
        ).template get_if<name>());
    }

    /* Check to see whether the argument pack contains a named keyword argument.  This
    overload applies when the argument name is known at compile time and can be
    statically resolved. */
    template <static_str name> requires (meta::valid_arg_name<name>)
    [[nodiscard]] constexpr bool contains() const noexcept
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
        noexcept (noexcept(get<names.template get<"">()>().template contains<name>()))
        requires (!names.template contains<name>() && names.template contains<"">())
    {
        return get<names.template get<"">()>().template contains<name>();
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
        noexcept (noexcept(base::operator()(
            std::forward<Self>(self),
            std::forward<F>(f)
        )))
        requires (requires{base::operator()(
            std::forward<Self>(self),
            std::forward<F>(f)
        );})
    {
        return (base::operator()(std::forward<Self>(self), std::forward<F>(f)));
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
        noexcept (noexcept(std::forward<Self>(self)(convert<T>{})))
        requires (requires{std::forward<Self>(self)(convert<T>{});})
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
        noexcept (noexcept(flatten<I + 1>(std::forward<As>(args)...)))
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
        noexcept (noexcept(flatten<sizeof...(prev) + sizeof...(curr)>(
            meta::unpack_arg<prev>(std::forward<As>(args)...)...,
            meta::unpack_arg<sizeof...(prev)>(
                std::forward<As>(args)...
            ).template get<curr>()...,
            meta::unpack_arg<sizeof...(prev) + 1 + next>(std::forward<As>(args)...)...
        )))
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
        noexcept (noexcept(_flatten(
            std::make_index_sequence<I>(),
            std::make_index_sequence<meta::unqualify<meta::unpack_type<I, As...>>::size()>(),
            std::make_index_sequence<sizeof...(As) - (I + 1)>(),
            std::forward<As>(args)...
        )))
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
        noexcept (noexcept(base(flatten<0>(std::forward<As>(args)...))))
    :
        base(flatten<0>(std::forward<As>(args)...))
    {}
};


namespace meta {

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

    /* A keyword parameter pack obtained by double-dereferencing a mapping-like
    container within a Python-style function call.  The template machinery recognizes
    this as if it were an `"*<anonymous>"_(container)` argument defined in the
    signature, if such an expression were well-formed.  Because that is outlawed by the
    DSL syntax and cannot appear at the call site, this class fills that gap and allows
    the same internals to be reused.  */
    template <meta::unpack_to_kwargs T>
    struct kwarg_pack : impl::kwarg_pack_tag {
        using key_type = meta::unqualify<T>::key_type;
        using mapped_type = meta::unqualify<T>::mapped_type;
        using type = mapped_type;
        static constexpr static_str id = "";

    private:
        struct hash {
            using is_transparent = void;
            static constexpr size_t operator()(std::string_view str)
                noexcept (noexcept(bertrand::hash(str)))
            {
                return bertrand::hash(str);
            }
        };

        struct equal {
            using is_transparent = void;
            static constexpr bool operator()(
                std::string_view lhs,
                std::string_view rhs
            ) noexcept (noexcept(lhs == rhs)) {
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
            requires (requires{std::ranges::swap(m_data, other.m_data);})
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
            noexcept (noexcept(m_data.contains(key)))
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

    /* A positional parameter pack obtained by dereferencing an iterable container
    within a Python-style function call.  The template machinery recognizes this as if
    it were an "**<anonymous>"_(container) argument defined in the signature, if such
    an expression were well-formed.  Because that is outlawed by the DSL syntax and
    cannot appear at the call site, this class fills the gap and allows the same
    internals to be reused.

    Dereferencing an `arg_pack` pack promotes it to a `kwarg_pack`, mirroring Python's
    double unpack operator. */
    template <meta::iterable T>
    struct arg_pack : impl::arg_pack_tag {
        using container_type = meta::remove_rvalue<T>;
        using begin_type = meta::begin_type<container_type>;
        using end_type = meta::end_type<container_type>;
        using type = meta::yield_type<T>;
        static constexpr static_str id = "";

    private:
        container_type m_data;
        begin_type m_begin;
        end_type m_end;

    public:
        template <meta::is<T> C>
        explicit constexpr arg_pack(C&& data) :
            m_data(std::forward<C>(data)),
            m_begin(std::ranges::begin(m_data)),
            m_end(std::ranges::end(m_data))
        {}

        constexpr void swap(arg_pack& other)
            noexcept (requires{
                {std::ranges::swap(m_data, other.m_data)} noexcept;
                {std::ranges::swap(m_begin, other.m_begin)} noexcept;
                {std::ranges::swap(m_end, other.m_end)} noexcept;
            })
            requires (requires{
                std::ranges::swap(m_data, other.m_data);
                std::ranges::swap(m_begin, other.m_begin);
                std::ranges::swap(m_end, other.m_end);
            })
        {
            std::ranges::swap(m_data, other.m_data);
            std::ranges::swap(m_begin, other.m_begin);
            std::ranges::swap(m_end, other.m_end);
        }

        [[nodiscard]] constexpr container_type& data() noexcept { return m_data; }
        [[nodiscard]] constexpr const container_type& data() const noexcept { return m_data; }
        [[nodiscard]] constexpr begin_type& begin() noexcept { return m_begin; }
        [[nodiscard]] constexpr begin_type begin() const noexcept { return m_begin; }
        [[nodiscard]] constexpr end_type& end() noexcept { return m_end; }
        [[nodiscard]] constexpr end_type end() const noexcept { return m_end; }

        [[nodiscard]] constexpr bool empty() const noexcept (noexcept(m_begin == m_end)) {
            return m_begin == m_end;
        }

        [[nodiscard]] constexpr decltype(auto) next() noexcept {
            decltype(auto) value = *m_begin;
            ++m_begin;
            return value;
        }

        void constexpr validate() {
            if (!empty()) {
                if consteval {
                    static constexpr static_str msg =
                        "too many arguments in positional parameter pack";
                    throw TypeError(msg);
                } else {
                    std::string message =
                        "too many arguments in positional parameter pack: ['" +
                        repr(*m_begin);
                    while (++m_begin != m_end) {
                        message += "', '" + repr(*m_begin);
                    }
                    message += "']";
                    throw TypeError(message);
                }
            }
        }

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
            noexcept (requires{{kwarg_pack<T>{
                std::forward<container_type>(m_data)
            }} noexcept;})
            requires (
                meta::unpack_to_kwargs<T> &&
                requires{kwarg_pack<T>{std::forward<container_type>(m_data)};}
            )
        {
            return kwarg_pack<T>{std::forward<container_type>(m_data)};
        }
    };

    template <meta::iterable T>
    arg_pack(T&&) -> arg_pack<T>;

}


/////////////////////////
////    SIGNATURE    ////
/////////////////////////


namespace impl {
    struct signature_tag {};

    /* A wrapper that identifies the arguments stored in a templates{} helper, so that
    downstream analysis can specialize accordingly.  No behavior is changed. */
    template <meta::arg Arg> requires (!meta::template_arg<Arg>)
    struct TemplateArg : Arg, template_arg_tag {};

    template <meta::arg T>
    struct as_template_arg { using type = TemplateArg<T>; };
    template <meta::template_arg T>
    struct as_template_arg<T> { using type = T; };

    /* A brace-initializable container for a sequence of arguments that forms the
    explicit template signature for a function.  Arguments within this container must
    be provided as template arguments when the function is called. */
    template <meta::arg... Args>
    struct templates : as_template_arg<Args>::type... {
        static constexpr size_t size = sizeof...(Args);
        static constexpr size_t n_partial = (meta::partials<Args>::size + ... + 0);

        template <size_t I> requires (I < size)
        [[nodiscard]] constexpr const auto& get() const noexcept {
            using type = TemplateArg<meta::unpack_type<I, Args...>>;
            return static_cast<meta::as_const_ref<type>>(*this);
        }
    };

    template <meta::arg... Args>
    templates(Args&&...) -> templates<meta::unqualify<Args>...>;

    /* A subclass of `Arg` that categorizes it as having one or more partially-applied
    values when stored within an `impl::signature` object.  This changes nothing about
    the argument itself, but triggers the function call machinery to always insert a
    particular value that may only be known at runtime, and is stored within the
    signature itself.  Calling a signature's `.bind()` method equates to marking one or
    more of its arguments with this type, and `.clear()` strips all such wrappers from
    the signature's arguments. */
    template <meta::unqualified Arg, typename... Ts> requires (meta::arg<Arg>)
    struct partial : Arg, impl::partial_tag {
        using arg_type = Arg;
        using partials = meta::pack<Ts...>;
    };

    /* Template arguments must store partial values internally in order to ensure they
    are encoded at compile time. */
    template <meta::unqualified Arg, typename... Ts> requires (meta::template_arg<Arg>)
    struct partial<Arg, Ts...> : Arg, impl::partial_tag {
        using arg_type = Arg;
        using partials = meta::pack<Ts...>;
        bertrand::args<Ts...> partial_values;
    };

    template <typename...>
    struct _make_partial;
    template <meta::arg A, typename... Ps, typename... Ts>
    struct _make_partial<A, meta::pack<Ps...>, Ts...> {
        using type = partial<A, Ps..., Ts...>;
    };

    /* Mark a runtime argument as a partial, which will store a list of types inside
    the signature, to be used at runtime for binding.  If the argument already has
    partial values, then the argument list will be extended with the new values. */
    template <typename... Ts, meta::arg Arg>
    constexpr auto make_partial(Arg&& arg)
        noexcept (requires{{typename _make_partial<
            meta::unqualify<Arg>,
            meta::partials<Arg>,
            Ts...
        >::type{std::forward<Arg>(arg)}} noexcept;})
        requires (requires{
            typename _make_partial<
                meta::unqualify<Arg>,
                meta::partials<Arg>,
                Ts...
            >::type{std::forward<Arg>(arg)};
        })
    {
        return typename _make_partial<
            meta::unqualify<Arg>,
            meta::partials<Arg>,
            Ts...
        >::type{std::forward<Arg>(arg)};
    }

    /* Mark a template argument as a partial, which will store a list of values inside
    the signature, to be used at compile time for binding.  If the argument already has
    partial values, then the argument list will be extended with the new values. */
    template <meta::template_arg Arg, typename... Ts>
    constexpr auto make_partial(Arg&& arg, Ts&&... ts)
        noexcept (requires{{typename _make_partial<
            meta::unqualify<Arg>,
            meta::partials<Arg>,
            Ts...
        >::type{
            std::forward<Arg>(arg),
            bertrand::args{std::forward<Ts>(ts)...}
        }} noexcept;})
        requires (requires{
            typename _make_partial<
                meta::unqualify<Arg>,
                meta::partials<Arg>,
                Ts...
            >::type{
                std::forward<Arg>(arg),
                bertrand::args{std::forward<Ts>(ts)...}
            };
        })
    {
        return typename _make_partial<
            meta::unqualify<Arg>,
            meta::partials<Arg>,
            Ts...
        >::type{std::forward<Arg>(arg), bertrand::args{std::forward<Ts>(ts)...}};
    }

    /* Form a partial from an argument and a list of values.  If the argument already
    has partial values, then the argument list will be extended with the new values. */
    template <meta::template_arg Arg, typename... Ts> requires (meta::partial_arg<Arg>)
    constexpr auto make_partial(Arg&& arg, Ts&&... ts)
        noexcept (requires{{typename _make_partial<
            meta::unqualify<Arg>,
            meta::partials<Arg>,
            Ts...
        >::type{
            std::forward<Arg>(arg),
            bertrand::args{std::forward<Arg>(arg).partial_values, std::forward<Ts>(ts)...}
        }} noexcept;})
        requires (requires{
            typename _make_partial<
                meta::unqualify<Arg>,
                meta::partials<Arg>,
                Ts...
            >::type{
                std::forward<Arg>(arg),
                bertrand::args{std::forward<Arg>(arg).partial_values, std::forward<Ts>(ts)...}
            };
        })
    {
        return typename _make_partial<
            meta::unqualify<Arg>,
            meta::partials<Arg>,
            Ts...
        >::type{
            std::forward<Arg>(arg),
            bertrand::args{std::forward<Arg>(arg).partial_values, std::forward<Ts>(ts)...}
        };
    }

    /* Strip any partial values from an argument. */
    template <meta::arg Arg>
    constexpr decltype(auto) remove_partial(Arg&& arg) noexcept {
        if constexpr (meta::partial_arg<Arg>) {
            return (std::forward<
                meta::qualify<typename meta::unqualify<Arg>::arg_type, Arg>
            >(arg));
        } else {
            return (std::forward<Arg>(arg));
        }
    }

    /* Form a `bertrand::args{}` container to back the runtime partial arguments, for
    later use in signature binding logic. */
    template <typename out, const auto&...>
    struct _extract_partial { using type = out::template eval<bertrand::args>; };
    template <typename out, const auto& A, const auto&... Args>
        requires (meta::partial_arg<decltype(A)>)
    struct _extract_partial<out, A, Args...> {
        using type = _extract_partial<
            typename out::template concat<meta::partials<decltype(A)>>,
            Args...
        >::type;
    };
    template <typename out, const auto& A, const auto&... Args>
        requires (!meta::partial_arg<decltype(A)>)
    struct _extract_partial<out, A, Args...> {
        using type = _extract_partial<out, Args...>::type;
    };
    template <const auto&... Args> requires (meta::arg<decltype(Args)> && ...)
    using extract_partial = _extract_partial<meta::pack<>, Args...>::type;

}


namespace meta {

    template <typename T>
    concept signature = inherits<T, impl::signature_tag>;

    namespace detail {

        enum class bind_error : uint8_t {
            none,
            failed_type_check,
            conflicting_values,
            missing_required,
            extra_positional,
            extra_keyword,
        };

    }

    template <detail::bind_error E>
    concept bind_error =
        E == detail::bind_error::failed_type_check ||
        E == detail::bind_error::conflicting_values ||
        E == detail::bind_error::missing_required ||
        E == detail::bind_error::extra_positional ||
        E == detail::bind_error::extra_keyword;

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
            OPT                 = 0b10,
            VAR                 = 0b100,
            POS                 = 0b1000,
            KW                  = 0b10000,
            UNTYPED             = 0b100000,
            RUNTIME             = 0b1000000,
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
    template <const auto& Spec, const auto&... Args>
    struct signature_info {
    private:
        template <typename A>
        static constexpr static_str display_arg = meta::arg_id<A>;
        template <meta::bound_arg A>
        static constexpr static_str display_arg<A> = meta::arg_id<A> + " = ...";

        template <size_t... Is>
        static consteval auto _ctx_str(std::index_sequence<Is...>) noexcept {
            if constexpr (Spec.size) {
                return (
                    "def<" + string_wrapper<", ">::join<
                        display_arg<decltype(Spec.template get<Is>())>...
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
        static constexpr size_t N = Spec.size + sizeof...(Args);

        /* Indentation used for displaying contextual error messages. */
        static constexpr static_str<4> indent {' '};

        /* A context string to be included in error messages, to assist with
        debugging.  Consists only of argument ids and default value placeholders. */
        static constexpr static_str ctx_str = _ctx_str(
            std::make_index_sequence<Spec.size>{}
        );

        /* An arrow to a particular index of the string, to be included one line below
        `ctx_str` in error messages, to assist with debugging. */
        template <size_t I>
        static constexpr static_str ctx_arrow = _ctx_arrow(std::make_index_sequence<I>{});

        /* Get the argument at index I, correcting for the split between template
        arguments and runtime arguments. */
        template <size_t I> requires (I < N)
        [[nodiscard]] static constexpr const auto& get() noexcept {
            if constexpr (I < Spec.size) {
                return Spec.template get<I>();
            } else {
                return meta::unpack_value<I, Args...>;
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

            list& sec = I < Spec.size ? compile_time : run_time;

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
                } else if constexpr (I < Spec.size) {
                    required_after_optional<0>(std::make_index_sequence<I>{});
                } else {
                    required_after_optional<Spec.size>(
                        std::make_index_sequence<I - Spec.size>{}
                    );
                }
            }

            params[I].name = meta::arg_name<A>;
            params[I].index = I;
            params[I].partial = meta::partials<A>::size;
            if constexpr (!meta::typed_arg<A>) {
                params[I].kind = params[I].kind | impl::arg_kind::UNTYPED;
            }
            if constexpr (I >= Spec.size) {
                params[I].kind = params[I].kind | impl::arg_kind::RUNTIME;
            }
        }

        /* [2] positional-only separator ("/") */
        template <size_t I, meta::positional_only_separator A>
        consteval void parse(const A& arg) {
            duplicate_names<meta::arg_name<A>>(std::make_index_sequence<I>{});
            after_return_annotation<I>();

            list& sec = I < Spec.size ? compile_time : run_time;

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
            if constexpr (I >= Spec.size) {
                sec.pos_or_kw.offset -= Spec.size;
                start = Spec.size;
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

            list& sec = I < Spec.size ? compile_time : run_time;

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
                if constexpr (I >= Spec.size) {
                    sec.args.offset -= Spec.size;
                }
                params[I].kind = impl::arg_kind::VAR | impl::arg_kind::POS;
            }

            params[I].name = meta::arg_name<A>;
            params[I].index = I;
            params[I].partial = meta::partials<A>::size;
            if constexpr (!meta::typed_arg<A>) {
                params[I].kind = params[I].kind | impl::arg_kind::UNTYPED;
            }
            if constexpr (I >= Spec.size) {
                sec.kwonly.offset -= Spec.size;
                params[I].kind = params[I].kind | impl::arg_kind::RUNTIME;
            }
        }

        /* [4] variadic keyword arguments ("**kwargs") */
        template <size_t I, meta::variadic_keyword A>
        consteval void parse(const A& arg) {
            duplicate_names<meta::arg_name<A>>(std::make_index_sequence<I>{});
            after_return_annotation<I>();

            list& sec = I < Spec.size ? compile_time : run_time;

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
            if constexpr (I >= Spec.size) {
                sec.kwargs.offset -= Spec.size;
                params[I].kind = params[I].kind | impl::arg_kind::RUNTIME;
            }
        }

        /* [5] return annotations ("->") */
        template <size_t I, meta::return_annotation A>
        consteval void parse(const A& arg) {
            duplicate_names<meta::arg_name<A>>(std::make_index_sequence<I>{});

            // return annotation cannot be a template argument
            if constexpr (I < Spec.size) {
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

        [[nodiscard]] static constexpr size_type size() noexcept {
            return Spec.size + sizeof...(Args);
        }

        [[nodiscard]] static constexpr index_type ssize() noexcept {
            return index_type(size());
        }

        [[nodiscard]] static constexpr bool empty() noexcept {
            return size() == 0;
        }

    private:
        using info_type = impl::signature_info<Spec, Args...>;
        static constexpr info_type info {std::make_index_sequence<size()>{}};

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
                meta::arg_name<decltype(Spec.template get<
                    info.compile_time.posonly.offset + c_posonly
                >())>...,
                meta::arg_name<decltype(Spec.template get<
                    info.compile_time.pos_or_kw.offset + c_pos_or_kw
                >())>...,
                meta::arg_name<decltype(Spec.template get<
                    info.compile_time.kwonly.offset + c_kwonly
                >())>...,
                meta::arg_name<decltype(Spec.template get<
                    info.compile_time.args.offset + c_args
                >())>...,
                meta::arg_name<decltype(Spec.template get<
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
                (Spec.size + info.run_time.posonly.offset + posonly)...,
                (Spec.size + info.run_time.pos_or_kw.offset + pos_or_kw)...,
                (Spec.size + info.run_time.kwonly.offset + kwonly)...,
                (Spec.size + info.run_time.args.offset + args)...,
                (Spec.size + info.run_time.kwargs.offset + kwargs)...
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

        using bind_error = meta::detail::bind_error;
        using partial_type = impl::extract_partial<Args...>;
        partial_type m_partial;

        template <size_type I> requires (I >= Spec.size)
        static constexpr size_type partial_idx() noexcept {
            size_type result = 0;
            for (size_type i = Spec.size; i < I; ++i) {
                result += info.params[i].partial;
            }
            return result;
        }

        template <typename Self, index_type I> requires (impl::valid_index<ssize(), I>)
        struct param {
            static constexpr size_type index =
                size_type(impl::normalize_index<ssize(), I>());

        private:
            static constexpr const auto& arg = info.template get<index>();

        public:
            static constexpr const auto& name = meta::arg_name<decltype(arg)>;
            static constexpr size_type partial = meta::partials<decltype(arg)>::size;
            static constexpr impl::arg_kind kind = info.params[index].kind;
            meta::remove_rvalue<Self> signature;

            static constexpr bool has_value() noexcept {
                return
                    meta::partial_arg<decltype(arg)> ||
                    meta::bound_arg<decltype(arg)>;
            }

            template <typename S>
            constexpr decltype(auto) value(this S&& self) noexcept
                requires (has_value() && meta::partial_arg<decltype(arg)>)
            {
                if constexpr (meta::template_arg<decltype(arg)>) {
                    return (arg.partial_values);
                } else {
                    return (std::forward<S>(self).signature.m_partial.template get<
                        partial_idx<index>()
                    >());
                }
            }

            static constexpr decltype(auto) value()
                noexcept (requires{{arg.value()} noexcept;})
                requires (has_value() && !meta::partial_arg<decltype(arg)>)
            {
                return arg.value();
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

        /* Construct a signature with a given set of partial arguments.  The arguments
        must exactly match the specification given in the template list. */
        template <typename... Ts>
        [[nodiscard]] constexpr signature(Ts&&... partial)
            noexcept (noexcept(partial_type(std::forward<Ts>(partial)...)))
            requires (requires{partial_type(std::forward<Ts>(partial)...);})
        :
            m_partial(std::forward<Ts>(partial)...)
        {}

        [[nodiscard]] static constexpr pointer data() noexcept {
            return info.params.data();
        }
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

        /* Search the signature for an argument with the given name.  Returns an
        iterator to that argument if it is present, or an `end()` iterator otherwise. */
        template <static_str name>
        [[nodiscard]] static constexpr iterator find() noexcept {
            if constexpr (contains<name>()) {
                return info.params.begin() + names.template get<name>();
            } else {
                return info.params.end();
            }
        }

        /* Search the signature for an argument with the given name.  Returns an
        iterator to that argument if it is present, or an `end()` iterator otherwise. */
        [[nodiscard]] static constexpr iterator find(std::string_view kw) noexcept {
            if (const auto it = names.find(kw); it != names.end()) {
                return info.params.begin() + it->second;
            } else {
                return info.params.end();
            }
        }

        /* Return a parameter descriptor for the parameter at index I, applying
        Python-style wraparound for negative indices.  Fails to compile if the index
        would be out of range after normalization.  Note that the parameter descriptor
        returned by this method differs from that of simple indexing or iteration in
        that it also encodes the true partial or default value with the proper type,
        if the argument has such a value. */
        template <index_type I, typename Self> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr auto get(this Self&& self)
            noexcept (requires{{param<Self, I>{std::forward<Self>(self)}} noexcept;})
            requires (requires{param<Self, I>{std::forward<Self>(self)};})
        {
            return param<Self, I>{std::forward<Self>(self)};
        }

        /* Return a parameter descriptor for a named argument, assuming it is present
        in the signature.  Fails to compile otherwise.  Note that the parameter
        descriptor returned by this method differs from that of simple indexing or
        iteration in that it also encodes the true partial or default value with the
        proper type, if the argument has such a value. */
        template <static_str name, typename Self> requires (contains<name>())
        [[nodiscard]] constexpr auto get(this Self&& self)
            noexcept (requires{{param<Self, index_type(names.template get<name>())>{
                std::forward<Self>(self)
            }} noexcept;})
            requires (requires{param<Self, index_type(names.template get<name>())>{
                std::forward<Self>(self)
            };})
        {
            return param<Self, index_type(names.template get<name>())>{
                std::forward<Self>(self)
            };
        }

        /* Index into the signature, returning an `Optional<T>`, where `T` is a
        parameter descriptor that includes its name, index in the signature, and kind.
        The empty state indicates that the index was out of range after
        normalization. */
        [[nodiscard]] static constexpr Optional<reference> get(index_type idx)
            noexcept (requires{{Optional<reference>{
                info.params[idx + (ssize() * (idx < 0))]
            }} noexcept;})
            requires (requires{Optional<reference>{
                info.params[idx + (ssize() * (idx < 0))]
            };})
        {
            index_type i = idx + (ssize() * (idx < 0));
            if (i < 0 || i >= ssize()) {
                return None;
            } else {
                return {info.params[i]};
            }
        }

        /* Search the signature for an argument with the given name.  Returns an
        `Optional<T>`, where `T` is a parameter descriptor that includes the argument's
        name, index in the signature, and kind.  The empty state indicates that the
        argument name is not present in the signature. */
        [[nodiscard]] static constexpr Optional<reference> get(std::string_view name)
            noexcept (requires{{Optional<reference>{info.params[names[name]]}} noexcept;})
            requires (requires{Optional<reference>{info.params[names[name]]};})
        {
            if (const auto it = names.find(name); it != names.end()) {
                return {info.params[it->second]};
            } else {
                return None;
            }
        }

        /* Index into the signature, returning a parameter descriptor that includes
        its name, index in the signature, and kind.  May throw an `IndexError` if the
        index is out of bounds after normalization.  Use `get()` and `get_if()` to
        avoid the error. */
        [[nodiscard]] static constexpr const auto& operator[](index_type idx)
            noexcept (noexcept(info.params[impl::normalize_index(ssize(), idx)]))
        {
            return info.params[impl::normalize_index(ssize(), idx)];
        }

        /* Slice the signature, returning a view over a specific subset of the
        parameter list. */
        [[nodiscard]] constexpr slice operator[](bertrand::slice s)
            noexcept (noexcept(slice{info.params, s.normalize(ssize())}))
        {
            return {*this, s.normalize(ssize())};
        }

        /* Search the signature for an argument with the given name.  Returns a
        parameter descriptor that includes its name, index in the signature, and
        kind.  May throw a `KeyError` if the keyword name is not present.  Use `get()`
        and `get_if()` to avoid the error. */
        [[nodiscard]] static constexpr const auto& operator[](
            std::string_view kw
        ) noexcept (noexcept(info.params[names[kw]])) {
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
        /* Recur over a set of explicit template parameters that were provided at a
        function's call site, or which are being partially applied to the function's
        signature, represented by the member `::call` and `::bind` types, respectively.
        Both evaluate to a properly-specialized function object that can be called with
        additional runtime arguments to complete the specified operation. */
        template <size_type I, auto... T>
        struct specialize {
        private:
            static constexpr const auto& curr = Spec.template get<I>();
            static constexpr auto param = info.params[I];
            static constexpr bool typed = meta::typed_arg<decltype(curr)>;
            static constexpr bool has_partial = meta::partial_arg<decltype(curr)>;
            static constexpr bool has_default = !has_partial && meta::bound_arg<decltype(curr)>;

            // apply a type check against `curr`, assuming it is typed and the index is
            // in range.
            template <size_type J>
            static constexpr bool type_check = true;
            template <size_type J> requires (typed && J < sizeof...(T) && !meta::arg<
                decltype(meta::unpack_value<J, T...>)
            >)
            static constexpr bool type_check<J> = meta::convertible_to<
                decltype(meta::unpack_value<I, T...>),
                meta::arg_type<decltype(curr)>
            >;
            template <size_type J> requires (typed && J < sizeof...(T) && meta::bound_arg<
                decltype(meta::unpack_value<J, T...>)
            >)
            static constexpr bool type_check<J> = meta::convertible_to<
                meta::arg_value<decltype(meta::unpack_value<I, T...>)>,
                meta::arg_type<decltype(curr)>
            >;

            // Check to see whether any keyword arguments to the right of `I` are
            // already contained as partial variadic keyword arguments.  If so, then
            // a conflicting value error will occur.
            template <size_type...>
            static constexpr bool conflicts_with_partial = false;
            template <size_type... Is> requires (has_partial && param.kind.kwargs())
            static constexpr bool conflicts_with_partial<Is...> = (
                (curr.partial_values.contains<
                    meta::arg_name<decltype(meta::unpack_value<I + Is, T...>)
                >()) || ...
            );

            // Count the number of unprocessed template parameters in `T...` which can
            // bind to `curr` as a positional argument.  Returns zero if no positional
            // arguments remain to be bound.
            template <size_type J>
            static constexpr size_type _consume_positional = 0;
            template <size_type J> requires (J < sizeof...(T) &&
                !meta::arg<decltype(meta::unpack_value<J, T...>)>
            )
            static constexpr size_type _consume_positional<J> = _consume_positional<J + 1> + 1;
            static constexpr size_type consume_positional = _consume_positional<I>;

            // Check to see if there is an unprocessed template parameter with the same
            // name as `curr`, which can bind to it as a keyword argument.  If such an
            // argument exists, then returns its index in the template parameter list,
            // otherwise returns `sizeof...(T)`.
            template <size_type J>
            static constexpr size_type _consume_keyword = J;
            template <size_type J> requires (J < sizeof...(T) && !(
                meta::standard_arg<decltype(meta::unpack_value<J, T...>)> &&
                param.name == meta::arg_name<decltype(meta::unpack_value<J, T...>)>
            ))
            static constexpr size_type _consume_keyword<J> =  _consume_keyword<J + 1>;
            static constexpr size_type consume_keyword = _consume_keyword<I>;

            template <typename, typename>
            struct bind_forward;
            template <size_type... prev, size_type... next>
            struct bind_forward<std::index_sequence<prev...>, std::index_sequence<next...>> :
                specialize<
                    I + 1,
                    meta::unpack_value<prev, T...>...,
                    curr,
                    meta::unpack_value<I + next, T...>...
                >::bind
            {};

            template <typename, typename>
            struct bind_positional {  // terminate recursion
                static constexpr bind_error error = bind_error::failed_type_check;
            };
            template <size_type... prev, size_type... next> requires (type_check<I>)
            struct bind_positional<std::index_sequence<prev...>, std::index_sequence<next...>> :
                specialize<
                    I + 1,
                    meta::unpack_value<prev, T...>...,
                    impl::make_partial(curr, meta::unpack_value<I, T...>),
                    meta::unpack_value<I + 1 + next, T...>...
                >::bind
            {};

            template <typename, typename, typename>
            struct bind_keyword {  // terminate recursion
                static constexpr bind_error error = bind_error::failed_type_check;
            };
            template <size_type... prev, size_type... middle, size_type... next>
                requires (type_check<consume_keyword>)
            struct bind_keyword<
                std::index_sequence<prev...>,
                std::index_sequence<middle...>,
                std::index_sequence<next...>
            > :
                specialize<
                    I + 1,
                    meta::unpack_value<prev, T...>...,
                    impl::make_partial(
                        curr,
                        meta::unpack_value<consume_keyword, T...>
                    ),
                    meta::unpack_value<I + middle, T...>...,
                    meta::unpack_value<consume_keyword + 1 + next, T...>...
                >::bind
            {};

            template <typename, typename, typename>
            struct bind_variadic {  // terminate recursion
                static constexpr bind_error error = bind_error::failed_type_check;
            };
            template <size_type... prev, size_type... pos, size_type... kw>
                requires (conflicts_with_partial<kw...>)
            struct bind_variadic<
                std::index_sequence<prev...>,
                std::index_sequence<pos...>,
                std::index_sequence<kw...>
            > {
                static constexpr bind_error error = bind_error::conflicting_values;
            };
            template <size_type... prev, size_type... pos, size_type... kw>
                requires (param.kind.args() && ... && type_check<I + pos>)
            struct bind_variadic<
                std::index_sequence<prev...>,
                std::index_sequence<pos...>,
                std::index_sequence<kw...>
            > :
                specialize<
                    I + 1,
                    meta::unpack_value<prev, T...>...,
                    impl::make_partial(
                        curr,
                        meta::unpack_value<I + pos, T...>...
                    ),
                    meta::unpack_value<I + sizeof...(pos) + kw, T...>...
                >::bind
            {};
            template <size_type... prev, size_type... pos, size_type... kw>
                requires (
                    (param.kind.kwargs() && !conflicts_with_partial<kw...>) &&
                    ... &&
                    type_check<I + sizeof...(pos) + kw>
                )
            struct bind_variadic<
                std::index_sequence<prev...>,
                std::index_sequence<pos...>,
                std::index_sequence<kw...>
            > :
                specialize<
                    I + 1,
                    meta::unpack_value<prev, T...>...,
                    impl::make_partial(
                        curr,
                        meta::unpack_value<I + sizeof...(pos) + kw, T...>...
                    ),
                    meta::unpack_value<I + pos, T...>...
                >::bind
            {};

            // [0] base case - forward curr if it is already partial or without a match
            template <const auto& curr>
            struct _bind : bind_forward<
                std::make_index_sequence<I>,
                std::make_index_sequence<sizeof...(T) - I>
            > {};

            // [1] prefer to bind positional arguments if available
            template <const auto& curr>
                requires (!has_partial && consume_positional > 0 && (
                    param.kind.posonly() ||
                    (param.kind.pos_or_kw() && consume_keyword == sizeof...(T))
                ))
            struct _bind<curr> : bind_positional<
                std::make_index_sequence<I>,
                std::make_index_sequence<sizeof...(T) - (I + 1)>
            > {};

            // [2] otherwise, check for an unprocessed keyword argument
            template <const auto& curr>
                requires (!has_partial && consume_keyword < sizeof...(T) && (
                    param.kind.kwonly() ||
                    (param.kind.pos_or_kw() && consume_positional == 0)
                ))
            struct _bind<curr> : bind_keyword<
                std::make_index_sequence<I>,
                std::make_index_sequence<consume_keyword - I>,
                std::make_index_sequence<sizeof...(T) - (consume_keyword + 1)>
            > {};

            // [3] if both positional and keyword arguments are available, then we
            // have a conflict.
            template <const auto& curr>
                requires (
                    !has_partial && param.kind.pos_or_kw() &&
                    consume_positional > 0 && consume_keyword < sizeof...(T)
                )
            struct _bind<curr> {
                static constexpr bind_error error = bind_error::conflicting_values;
            };

            // [4] variadic arguments will consume all remaining arguments of their
            // respective kind, and append them to any existing partial values.
            template <const auto& curr> requires (I < sizeof...(T) && param.kind.variadic())
            struct _bind<curr> : bind_variadic<
                std::make_index_sequence<I>,
                std::make_index_sequence<consume_positional>,
                std::make_index_sequence<sizeof...(T) - (I + consume_positional)>
            > {};

        public:
            /* Interpret the template arguments as a partial signature.  This will
            recur over the explicit template parameters and form a new signature which
            contains them as partially-applied values.  The type returned by `::bind`
            includes a `::status` member that indicates whether the binding was
            successful, and can be invoked to insert values for the runtime portion of
            the function signature if so, which completes the partial. */
            using bind = _bind<curr>;

        private:
            template <typename, typename>
            struct call_default {  // terminate recursion
                static constexpr bind_error error = bind_error::missing_required;
            };
            template <size_type... prev, size_type... next> requires (has_partial)
            struct call_default<std::index_sequence<prev...>, std::index_sequence<next...>> :
                specialize<
                    I + 1,
                    meta::unpack_value<prev, T...>...,
                    curr.partial_values.template get<0>(),
                    meta::unpack_value<I + next, T...>...
                >::call
            {};
            template <size_type... prev, size_type... next> requires (!has_partial && has_default)
            struct call_default<std::index_sequence<prev...>, std::index_sequence<next...>> :
                specialize<
                    I + 1,
                    meta::unpack_value<prev, T...>...,
                    curr.value(),
                    meta::unpack_value<I + next, T...>...
                >::call
            {};

            template <size_type J>
            struct call_positional {  // terminate recursion
                static constexpr bind_error error = bind_error::failed_type_check;
            };
            template <size_type J> requires (type_check<J>)
            struct call_positional<J> : specialize<I + 1, T...>::call {};  // trivially forward

            template <typename, typename, typename>
            struct call_keyword {  // terminate recursion
                static constexpr bind_error error = bind_error::failed_type_check;
            };
            template <size_type... prev, size_type... middle, size_type... next>
                requires (type_check<consume_keyword>)
            struct call_keyword<
                std::index_sequence<prev...>,
                std::index_sequence<middle...>,
                std::index_sequence<next...>
            > :
                specialize<
                    I + 1,
                    meta::unpack_value<prev, T...>...,
                    meta::unpack_value<consume_keyword, T...>.value(),
                    meta::unpack_value<I + middle, T...>...,
                    meta::unpack_value<consume_keyword + 1 + next, T...>...
                >::call
            {};

            /// TODO: call_variadic would have to insert partials and then consume
            /// keywords at the call site.

            // [0] base case - insert default if available, otherwise raise an error
            template <const auto& curr>
            struct _call : call_default<
                std::make_index_sequence<I>,
                std::make_index_sequence<sizeof...(T) - I>
            > {};

            // [1] prefer to bind positional arguments if available
            template <const auto& curr>
                requires (!has_partial && consume_positional > 0 && (
                    param.kind.posonly() ||
                    (param.kind.pos_or_kw() && consume_keyword == sizeof...(T))
                ))
            struct _call<curr> : call_positional<I> {};

            // [2] otherwise, check for an unprocessed keyword argument
            template <const auto& curr>
                requires (!has_partial && consume_keyword < sizeof...(T) && (
                    param.kind.kwonly() ||
                    (param.kind.pos_or_kw() && consume_positional == 0)
                ))
            struct _call<curr> : call_keyword<
                std::make_index_sequence<I>,
                std::make_index_sequence<consume_keyword - I>,
                std::make_index_sequence<sizeof...(T) - (consume_keyword + 1)>
            > {};

            // [3] if both positional and keyword arguments are available, then we
            // have a conflict.
            template <const auto& curr>
                requires (
                    !has_partial && param.kind.pos_or_kw() &&
                    consume_positional > 0 && consume_keyword < sizeof...(T)
                )
            struct _call<curr> {
                static constexpr bind_error error = bind_error::conflicting_values;
            };

            /// TODO: variadic arguments



        public:
            /* Interpret the template arguments as a call signature.  This will recur
            over the explicit template parameters and reorder them in-place according
            to the enclosing signature's intended semantics, including the insertion
            of partial or default values where appropriate.  The type returned by
            `::call` includes a `::status` member that indicates whether the binding
            was successful, and can be invoked as a visitor to insert values for the
            runtime portion of the function signature if so, which completes the
            invocation. */
            using call = _call<curr>;
        };

        /* By the time we reach the base case, `T...` will contain all the template
        parameters needed for an indicated operation, which are guaranteed to match
        or exceed `Spec.size`, which is the number of explicit template parameters that
        the function is defined as expecting.  If they exceed that amount, then it
        indicates extra unmatched arguments that should be treated as an error.
        Otherwise, `T...` holds all the context necessary to invoke the specified
        operation, including as a visitor, which allows it to automatically unwrap
        monadic union types, as if the user had called `bertrand::visit()`
        internally. */
        template <auto... T>
        struct specialize<Spec.size, T...> {
        private:

            template <typename>
            static constexpr bind_error _error = bind_error::none;
            template <size_type... Is> requires (sizeof...(Is) > 0)
            static constexpr bind_error _error<std::index_sequence<Is...>> =
                (!meta::arg<decltype(meta::unpack_value<Spec.size + Is, T...>)> || ...) ?
                    bind_error::extra_positional :
                    bind_error::extra_keyword;
            static constexpr bind_error error =
                _error<std::make_index_sequence<sizeof...(T) - Spec.size>>;

        public:
            struct bind {
                static constexpr bind_error error = specialize::error;

                /// TODO: provide a call operator that continues binding the runtime
                /// arguments.

            };

            struct call {
                static constexpr bind_error error = specialize::error;

                /// TODO: provide a call operator that continues binding the runtime
                /// arguments.  Note that this will need to account for missing
                /// arguments and argument packs in a way that ::bind will not.

            };
        };

        template <size_type... Is>
        static constexpr auto _clear(std::index_sequence<Is...>) noexcept {
            return signature<
                {impl::remove_partial(Spec.template get<Is>())...},
                impl::remove_partial(Args)...
            >{};
        }

    public:
        /* The total number of partial arguments that are currently bound to the
        signature. */
        [[nodiscard]] static constexpr size_type n_partial() noexcept {
            return (Spec.n_partial + ... + meta::partials<decltype(Args)>::size);
        }

        /* Strip all bound partial arguments from the signature. */
        [[nodiscard]] static constexpr auto clear() noexcept {
            return _clear(std::make_index_sequence<Spec.size>{});
        }

        /* Bind a set of partial arguments to this signature, returning a new signature
        that merges the given values with any existing partial arguments.  All
        arguments will be perfectly forwarded if possible, allowing this method to
        be efficiently chained without intermediate copies. */
        template <auto... T, typename Self, typename... A>
            requires (
                meta::source_args<decltype(T)...> &&
                meta::source_args<A...> &&
                !meta::bind_error<specialize<0, T...>::bind::error>
            )
        [[nodiscard]] constexpr decltype(auto) bind(this Self&& self, A&&... args)
            noexcept (requires{{typename specialize<0, T...>::bind{}(
                std::forward<Self>(self).m_partial,
                std::forward<A>(args)...
            )} noexcept;})
            requires (requires{typename specialize<0, T...>::bind{}(
                std::forward<Self>(self).m_partial,
                std::forward<A>(args)...
            );})
        {
            return (typename specialize<0, T...>::bind{}(
                std::forward<Self>(self).m_partial,
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
                !meta::bind_error<specialize<0, T...>::call::error>
            )
        [[nodiscard]] constexpr decltype(auto) operator()(
            this Self&& self,
            F&& func,
            A&&... args
        )
            noexcept (requires{{typename specialize<0, T...>::call{}(
                std::forward<F>(func),
                std::forward<Self>(self).m_partial,
                std::forward<A>(args)...
            )} noexcept;})
            requires (requires{typename specialize<0, T...>::call{}(
                std::forward<F>(func),
                std::forward<Self>(self).m_partial,
                std::forward<A>(args)...
            );})
        {
            return (typename specialize<0, T...>::call{}(
                std::forward<F>(func),
                std::forward<Self>(self).m_partial,
                std::forward<A>(args)...
            ));
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
                    requires (requires{
                        std::forward<Func>(func).template operator()<
                            T.template get<Is>()...
                        >(std::forward<decltype(args)>(args)...);
                    })
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
                requires (requires{std::forward<A>(args)(std::forward<S>(self).func);})
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
                requires (requires{
                    std::forward<A>(args)(call<
                        T,
                        std::make_index_sequence<T.size()>,
                        decltype(std::forward<S>(self).func)
                    >{self.func});
                })
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

    /// TODO: another specialization of `detect_signature` for functions that have a
    /// trivially-introspectable signature, and another for defs that perfectly
    /// forwards that information.

    /// auto f1 = def([](int x, int y) { return x + y; });
    /// auto f2 = def(f1);
    /// auto f3 = def(f1 >> println);








    static constexpr auto x = "x"_;
    static constexpr auto z = "z"_(2);
    static constexpr auto p1 = impl::partial<meta::unqualify<decltype(x)>, int>{x};
    static constexpr auto p2 = impl::partial<meta::unqualify<decltype(z)>, decltype("z"_ = 2)>{z};


    inline constexpr signature<{}, p1, "/"_, "y"_, "*args"_, p2> sig{1, "z"_ = 2};
    inline constexpr auto param = sig["x"];
    static_assert(sig["x"].index == 0);
    static_assert(sig.n_partial() == 2);
    static_assert(sig.clear().n_partial() == 0);
    static_assert(sig.get<-1>().has_value());
    static_assert(sig.get(-1).value().name == "z");
    static_assert(sig.get("x").value().kind.posonly());





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


    /* Backs the pure-C++ `signature` class in a way that prevents unnecessary code
    duplication between specializations.  Python signatures can extend this base to
    avoid reimplementing C++ function logic internally. */
    template <typename Param, typename Return, typename... Args>
    struct CppSignature {
        static constexpr bool enable = true;
        static constexpr bool python = false;
        static constexpr bool convertible_to_python = false;

        /* Normalized function type, which can be used to specialize `std::function`
        and/or `bertrand::Function` (assuming all types are unqualified Python
        wrappers). */
        using type = Return(Args...);

        template <typename... Values>
        struct Bind;

    protected:
        /* A flat array of Param objects whose indices are aligned to the enclosing
        parameter list. */
        using PositionalTable = std::array<Param, sizeof...(Args)>;
        static constexpr auto positional_table =
            []<size_t... Is>(std::index_sequence<Is...>) {
                return PositionalTable{Param::template create<Is, Args...>()...};
            }(std::index_sequence_for<Args...>{});

        /* In order to avoid superfluous compile errors, the perfect keyword hash map
        should not be created unless the signature is well-formed. */
        template <typename, size_t, typename...>
        struct get_names {
            using type = static_map<const Param&>;
            static constexpr type operator()(auto&&... callbacks) {
                return {};
            }
        };
        template <typename... out, size_t I, typename... Ts>
            requires (
                sizeof...(Ts) == 0 &&
                I < MAX_ARGS &&
                meta::perfectly_hashable<meta::arg_traits<out>::name...>
            )
        struct get_names<args<out...>, I, Ts...> {
            using type = static_map<const Param&, meta::arg_traits<out>::name...>;
            static constexpr type operator()(const auto&... callbacks) {
                return {callbacks...};
            }
        };
        template <typename... out, size_t I, typename T, typename... Ts>
        struct get_names<args<out...>, I, T, Ts...> {
            template <typename>
            struct filter {
                using type = args<out...>;
                static constexpr auto operator()(const auto&... callbacks) noexcept {
                    return get_names<type, I + 1, Ts...>{}(callbacks...);
                }
            };
            template <typename U> requires (!meta::arg_traits<U>::name.empty())
            struct filter<U> {
                using type = args<out..., U>;
                static constexpr auto operator()(const auto&... callbacks) noexcept {
                    return get_names<type, I + 1, Ts...>{}(
                        callbacks...,
                        positional_table[I]
                    );
                }
            };
            using type = get_names<typename filter<T>::type, I + 1, Ts...>::type;
            static constexpr auto operator()(const auto&... callbacks) noexcept {
                return filter<T>{}(callbacks...);
            }
        };

        /* A compile-time minimal perfect hash table mapping argument names to Param
        objects shared with the positional table. */
        using NameTable = get_names<args<>, 0, Args...>::type;
        static constexpr NameTable name_table = get_names<args<>, 0, Args...>{}();

        template <typename out, typename...>
        struct _Unbind;
        template <typename R, typename... out, typename... As>
        struct _Unbind<R(out...), As...> { using type = signature<R(out...)>; };
        template <typename R, typename... out, typename A, typename... As>
        struct _Unbind<R(out...), A, As...> {
            using type = _Unbind<R(
                out...,
                typename meta::arg_traits<A>::unbind
            ), As...>::type;
        };

        template <bool>
        struct get_generic { using type = void; };
        template <>
        struct get_generic<true> {
            using type = impl::consistent_generic_type<Return, Args...>;
        };

        /// TODO: if I get smarter about how and when I check invocable<>, then this
        /// explicit specialization might not be necessary?

        template <typename Func>
        static constexpr bool _invocable = std::is_invocable_r_v<Return, Func, Args...>;
        template <meta::make_def Func>
        static constexpr bool _invocable<Func> = true;  // not enough information yet

        /// TODO: to_arg<I>(...) may need to account for generic arguments somehow.
        /// -> It may not be necessary though if I'm going to end up passing the actual
        /// values straight to the underlying C++ function.

        /* Converts a raw C++ value into the corresponding argument annotation from the
        enclosing signature. */
        template <size_t I> requires (I < sizeof...(Args))
        static constexpr auto to_arg(auto&& value) -> meta::unpack_type<I, Args...> {
            if constexpr (meta::arg<meta::unpack_type<I, Args...>>) {
                return {std::forward<decltype(value)>(value)};
            } else {
                return std::forward<decltype(value)>(value);
            }
        };

    public:
        [[nodiscard]] static constexpr size_t size() noexcept { return sizeof...(Args); }
        [[nodiscard]] static constexpr bool empty() noexcept { return !sizeof...(Args); }
        [[nodiscard]] static auto begin() noexcept { return positional_table.begin(); }
        [[nodiscard]] static auto cbegin() noexcept { return positional_table.cbegin(); }
        [[nodiscard]] static auto rbegin() noexcept { return positional_table.rbegin(); }
        [[nodiscard]] static auto crbegin() noexcept { return positional_table.crbegin(); }
        [[nodiscard]] static auto end() noexcept { return positional_table.end(); }
        [[nodiscard]] static auto cend() noexcept { return positional_table.cend(); }
        [[nodiscard]] static auto rend() noexcept { return positional_table.rend(); }
        [[nodiscard]] static auto crend() noexcept { return positional_table.crend(); }

        static constexpr size_t n_posonly           = impl::n_posonly<Args...>;
        static constexpr size_t n_pos               = impl::n_pos<Args...>;
        static constexpr size_t n_kw                = impl::n_kw<Args...>;
        static constexpr size_t n_kwonly            = impl::n_kwonly<Args...>;
        static constexpr bool has_posonly           = impl::has_posonly<Args...>;
        static constexpr bool has_pos               = impl::has_pos<Args...>;
        static constexpr bool has_kw                = impl::has_kw<Args...>;
        static constexpr bool has_kwonly            = impl::has_kwonly<Args...>;
        static constexpr bool has_args              = impl::has_args<Args...>;
        static constexpr bool has_kwargs            = impl::has_kwargs<Args...>;
        static constexpr size_t posonly_idx         = impl::posonly_idx<Args...>;
        static constexpr size_t pos_idx             = impl::pos_idx<Args...>;
        static constexpr size_t kw_idx              = impl::kw_idx<Args...>;
        static constexpr size_t kwonly_idx          = impl::kwonly_idx<Args...>;
        static constexpr size_t args_idx            = impl::args_idx<Args...>;
        static constexpr size_t kwargs_idx          = impl::kwargs_idx<Args...>;
        static constexpr size_t opt_idx             = impl::opt_idx<Args...>;

        template <size_t I> requires (I < size())
        using at = meta::unpack_type<I, Args...>;

        template <typename R>
        using with_return = signature<R(Args...)>;

        template <typename... A>
        using with_args = signature<Return(A...)>;

        /* Check whether a given positional index is within the bounds of the enclosing
        signature. */
        template <size_t I>
        [[nodiscard]] static constexpr bool contains() noexcept { return I < size(); }
        [[nodiscard]] static constexpr bool contains(size_t i) noexcept { return i < size(); }

        /* Check whether a given argument name is present within the enclosing signature. */
        template <static_str Key>
        [[nodiscard]] static constexpr bool contains() noexcept {
            return NameTable::template contains<Key>();
        }
        template <typename T> requires (NameTable::template hashable<T>)
        [[nodiscard]] static constexpr bool contains(T&& key) noexcept {
            return name_table.contains(std::forward<T>(key));
        }

        /* Look up the callback object associated with the argument at index I if it is
        within range.  Fails to compile otherwise. */
        template <size_t I> requires (I < size())
        [[nodiscard]] static constexpr const Param& get() noexcept {
            return positional_table[I];
        }

        /* Look up the callback object associated with the argument at index I if it is
        within range.  Throws an `IndexError` otherwise. */
        [[nodiscard]] static constexpr const Param& get(size_t i) {
            if (i < size()) {
                return positional_table[i];
            }
            throw IndexError(std::to_string(i));
        }

        /* Look up the callback object associated with the named argument if it is present
        within the signature.  Fails to compile otherwise. */
        template <static_str Key> requires (contains<Key>())
        [[nodiscard]] static constexpr const Param& get() noexcept {
            return name_table.template get<Key>();
        }

        /* Look up the callback object associated with the named argument if it is present
        within the signature.  Throws a `KeyError` otherwise. */
        template <typename T> requires (NameTable::template hashable<T>)
        [[nodiscard]] static constexpr const Param& get(T&& key) {
            if (const Param* result = name_table[std::forward<T>(key)]) {
                return *result;
            }
            throw KeyError(key);
        }

        /* Get a pointer to the callback object for a given argument.  Returns nullptr if
        the index is out of range. */
        [[nodiscard]] static constexpr const Param* operator[](size_t i) noexcept {
            return i < size() ? &positional_table[i] : nullptr;
        }

        /* Get a pointer to the callback object for the named argument.  Returns nullptr if
        the name is not recognized. */
        template <typename T> requires (NameTable::template hashable<T>)
        [[nodiscard]] static constexpr const Param* operator[](T&& key) noexcept {
            return name_table[std::forward<T>(key)];
        }

        /* Find the index corresponding to the named argument. */
        template <static_str Key> requires (contains<Key>())
        [[nodiscard]] static constexpr size_t index() noexcept {
            return get<Key>().index;
        }
        template <typename T> requires (NameTable::template hashable<T>)
        [[nodiscard]] static constexpr size_t index(T&& key) {
            return get(std::forward<T>(key)).index;
        }

        /* True if a given function can be called with this signature's arguments and
        returns a compatible type, after accounting for implicit conversions. */
        template <typename Func>
        static constexpr bool invocable = _invocable<Func>;

        /* True if the number of arguments is less than or equal to `MAX_ARGS`. */
        static constexpr bool within_arg_limit = impl::within_arg_limit<Args...>;

        /* True if the arguments are given in the proper order (no positional after keyword,
        no required after optional, etc.). */
        static constexpr bool proper_argument_order = impl::proper_argument_order<Args...>;

        /* True if the return type lacks cvref qualifications. */
        static constexpr bool no_qualified_return = !meta::is_qualified<Return>;

        /* True if the argument types lack cvref qualifications. */
        static constexpr bool no_qualified_args = impl::no_qualified_args<Args...>;

        /* True if none of the `Arg<>` annotations are themselves cvref-qualified. */
        static constexpr bool no_qualified_arg_annotations =
            impl::no_qualified_arg_annotations<Args...>;

        /* True if there are no duplicate parameter names and at most one variadic
        positional/keyword argument, respectively. */
        static constexpr bool no_duplicate_args = impl::no_duplicate_args<Args...>;

        /* True if the signature contains generic return/argument types. */
        static constexpr bool generic =
            meta::generic<Return> || (meta::arg_traits<Args>::generic() || ...);

        /* True if all generic return/argument types in the signature are of the same
        family. */
        static constexpr bool generics_are_consistent =
            impl::generics_are_consistent<Return, Args...>;

        /* If both `::generic` and `::generics_are_consistent` are true, evaluates to
        the constraint type that should be applied when binding to this function.
        Otherwise, evaluates to `void`. */
        using generic_type = get_generic<generic && generics_are_consistent>::type;

        /* A bitmask with a 1 in the position of all of the required arguments in the
        parameter list.

        Each callback stores an index within the enclosing parameter list, which can be
        transformed into a one-hot encoded mask that will be progressively joined as each
        argument is processed.  The result can then be compared to this constant to quickly
        determine if all required arguments have been accounted for.  If that comparison
        fails, then further bitwise inspection can be done to determine exactly which
        arguments are missing, as well as their names for a comprehensive error message.

        Note that this mask effectively limits the maximum number of arguments that a
        function can accept, to an amount determined by the `MAX_ARGS` build flag. */
        static constexpr bitset<MAX_ARGS> required = impl::required<Args...>;

        /* A tuple holding a default value for every argument in the enclosing
        parameter list that is marked as optional.  One of these must be provided
        whenever a C++ function is invoked, and constructing one requires that the
        initializers match a sub-signature consisting only of the optional args as
        keyword-only parameters for clarity.  The result may be empty if there are no
        optional arguments in the enclosing signature, in which case the constructor
        will be optimized out. */
        struct Defaults : impl::signature_defaults_tag {
        protected:
            friend CppSignature;
            using Inner = impl::defaults_signature<Args...>;
            using Tuple = impl::defaults_tuple<Args...>;

            Tuple values;

            template <size_t, typename>
            static constexpr size_t _find = 0;
            template <size_t I, typename T, typename... Ts>
            static constexpr size_t _find<I, std::tuple<T, Ts...>> =
                I == T::index ? 0 : _find<I, std::tuple<Ts...>> + 1;

            template <typename D, size_t I>
            static constexpr bool _copy = I == D::size();
            template <typename D, size_t I> requires (I < std::tuple_size_v<Tuple>)
            static constexpr bool _copy<D, I> = [] {
                if constexpr (I < D::size()) {
                    using T = CppSignature::at<std::tuple_element_t<I, Tuple>::index>;
                    using U = D::template at<I>;
                    return std::same_as<
                        typename meta::arg_traits<T>::unbind,
                        typename meta::arg_traits<U>::unbind
                    > && _copy<D, I + 1>;
                } else {
                    return false;
                }
            }();
            template <meta::inherits<impl::signature_defaults_tag> D>
            static constexpr bool copy = _copy<std::remove_cvref_t<D>, 0>;

            template <size_t J, typename... A>
            static constexpr decltype(auto) build(A&&... args) {
                using T = std::tuple_element_t<J, Tuple>;
                constexpr size_t idx = impl::arg_idx<T::name, A...>;
                return meta::unpack_arg<idx>(std::forward<A>(args)...);
            }

        public:
            using type                              = signature<Return(Args...)>;
            static constexpr size_t n_posonly       = impl::n_opt_posonly<Args...>;
            static constexpr size_t n_pos           = impl::n_opt_pos<Args...>;
            static constexpr size_t n_kw            = impl::n_opt_kw<Args...>;
            static constexpr size_t n_kwonly        = impl::n_opt_kwonly<Args...>;
            static constexpr bool has_posonly       = impl::has_opt_posonly<Args...>;
            static constexpr bool has_pos           = impl::has_opt_pos<Args...>;
            static constexpr bool has_kw            = impl::has_opt_kw<Args...>;
            static constexpr bool has_kwonly        = impl::has_opt_kwonly<Args...>;
            static constexpr size_t posonly_idx     = impl::opt_posonly_idx<Args...>;
            static constexpr size_t pos_idx         = impl::opt_pos_idx<Args...>;
            static constexpr size_t kw_idx          = impl::opt_kw_idx<Args...>;
            static constexpr size_t kwonly_idx      = impl::opt_kwonly_idx<Args...>;

            /* The total number of optional arguments in the enclosing signature. */
            [[nodiscard]] static constexpr size_t size() noexcept { return Inner::size(); }
            [[nodiscard]] static constexpr bool empty() noexcept { return Inner::empty(); }

            /* Check whether a given index is within the bounds of the default value
            tuple. */
            template <size_t I>
            [[nodiscard]] static constexpr bool contains() noexcept { return I < size(); }
            [[nodiscard]] static constexpr bool contains(size_t i) noexcept {return i < size(); }

            /* Check whether the named argument is contained within the default value
            tuple. */
            template <static_str Key>
            [[nodiscard]] static constexpr bool contains() noexcept {
                return Inner::template contains<Key>();
            }
            template <typename T> requires (NameTable::template hashable<T>)
            [[nodiscard]] static constexpr bool contains(T&& key) noexcept {
                return Inner::contains(std::forward<T>(key));
            }
        
            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed.  If the defaults container
            is used as an lvalue, then this will either directly reference the internal
            value if the corresponding argument expects an lvalue, or a copy if it
            expects an unqualified or rvalue type.  If the defaults container is given
            as an rvalue instead, then the copy will be optimized to a move. */
            template <size_t J> requires (J < size())
            constexpr decltype(auto) get() const {
                return std::get<J>(values).get();
            }
            template <size_t J> requires (J < size())
            constexpr decltype(auto) get() && {
                return std::move(std::get<J>(values)).get();
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional.  If the defaults container is used as an lvalue, then
            this will either directly reference the internal value if the corresponding
            argument expects an lvalue, or a copy if it expects an unqualified or
            rvalue type.  If the defaults container is given as an rvalue instead, then
            the copy will be optimized to a move. */
            template <static_str Name> requires (contains<Name>())
            constexpr decltype(auto) get() const {
                return std::get<index<Name>()>(values).get();
            }
            template <static_str Name> requires (contains<Name>())
            constexpr decltype(auto) get() && {
                return std::move(std::get<index<Name>()>(values)).get();
            }

            /* Get the index of a named argument within the default values tuple. */
            template <static_str Key> requires (contains<Key>())
            [[nodiscard]] static constexpr size_t index() noexcept {
                return Inner::template index<Key>();
            }
            template <typename T> requires (NameTable::template hashable<T>)
            [[nodiscard]] static constexpr size_t index(T&& key) {
                return Inner::index(std::forward<T>(key));
            }

            /* Given an index into the enclosing signature, find the corresponding index
            in the defaults tuple if the corresponding argument is marked as optional. */
            template <size_t I> requires (meta::arg_traits<typename CppSignature::at<I>>::opt())
            static constexpr size_t find = _find<I, Tuple>;

            /* Given an index into the defaults tuple, find the corresponding index in
            the enclosing parameter list. */
            template <size_t J> requires (J < size())
            static constexpr size_t rfind = std::tuple_element_t<J, Tuple>::index;

            template <size_t J> requires (J < size())
            using at = CppSignature::at<rfind<J>>;

            /* Bind an argument list to the default values to enable the constructor. */
            template <typename... A>
            using Bind = Inner::template Bind<A...>;

            template <typename... A>
                requires (
                    !(meta::arg_traits<A>::variadic() || ...) &&
                    !(meta::arg_traits<A>::generic() || ...) &&
                    Bind<A...>::proper_argument_order &&
                    Bind<A...>::no_qualified_arg_annotations &&
                    Bind<A...>::no_duplicate_args &&
                    Bind<A...>::no_conflicting_values &&
                    Bind<A...>::no_extra_positional_args &&
                    Bind<A...>::no_extra_keyword_args &&
                    Bind<A...>::satisfies_required_args &&
                    Bind<A...>::can_convert
                )
            constexpr Defaults(A&&... args) : values(
                []<size_t... Js>(std::index_sequence<Js...>, auto&&... args) -> Tuple {
                    return {{build<Js>(std::forward<decltype(args)>(args)...)}...};
                }(std::index_sequence_for<A...>{}, std::forward<A>(args)...)
            ) {}

            template <meta::inherits<impl::signature_defaults_tag> D> requires (copy<D>)
            constexpr Defaults(D&& other) :
                values([]<size_t... Js>(std::index_sequence<Js...>, auto&& other) -> Tuple {
                    return {{std::forward<decltype(other)>(other).template get<Js>()}...};
                }(
                    std::make_index_sequence<std::remove_cvref_t<D>::size()>{},
                    std::forward<decltype(other)>(other)
                ))
            {}
        };

        /* Instance-level constructor for a `::Defaults` tuple. */
        template <typename... A>
            requires (
                !(meta::arg_traits<A>::variadic() || ...) &&
                !(meta::arg_traits<A>::generic() || ...) &&
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

        /* A tuple holding a partial value for every bound argument in the enclosing
        parameter list.  One of these must be provided whenever a C++ function is
        invoked, and constructing one requires that the initializers match a
        sub-signature consisting only of the bound args as positional-only and
        keyword-only parameters for clarity.  The result may be empty if there are no
        bound arguments in the enclosing signature, in which case the constructor will
        be optimized out. */
        struct Partial : impl::signature_partial_tag {
        protected:
            friend CppSignature;
            using Inner = impl::partial_signature<Args...>;
            using Tuple = impl::partial_tuple<Args...>;

            Tuple values;

            template <size_t K, typename... A>
            static constexpr decltype(auto) build(A&&... args) {
                using T = std::tuple_element_t<K, Tuple>;
                constexpr size_t idx = impl::arg_idx<T::name, A...>;
                if constexpr (!T::name.empty() && idx < sizeof...(A)) {
                    return meta::unpack_arg<idx>(std::forward<A>(args)...);
                } else {
                    return meta::unpack_arg<K>(std::forward<A>(args)...);
                }
            }

        public:
            using type                              = signature<Return(Args...)>;
            static constexpr size_t n_posonly       = impl::n_partial_posonly<Args...>;
            static constexpr size_t n_pos           = impl::n_partial_pos<Args...>;
            static constexpr size_t n_args          = impl::n_partial_args<Args...>;
            static constexpr size_t n_kw            = impl::n_partial_kw<Args...>;
            static constexpr size_t n_kwonly        = impl::n_partial_kwonly<Args...>;
            static constexpr size_t n_kwargs        = impl::n_partial_kwargs<Args...>;
            static constexpr bool has_posonly       = impl::has_partial_posonly<Args...>;
            static constexpr bool has_pos           = impl::has_partial_pos<Args...>;
            static constexpr bool has_args          = n_args > 0;
            static constexpr bool has_kw            = impl::has_partial_kw<Args...>;
            static constexpr bool has_kwonly        = impl::has_partial_kwonly<Args...>;
            static constexpr bool has_kwargs        = n_kwargs > 0;
            static constexpr size_t posonly_idx     = impl::partial_posonly_idx<Args...>;
            static constexpr size_t pos_idx         = impl::partial_pos_idx<Args...>;
            static constexpr size_t args_idx        = has_args ? CppSignature::args_idx : CppSignature::size();
            static constexpr size_t kw_idx          = impl::partial_kw_idx<Args...>;
            static constexpr size_t kwonly_idx      = impl::partial_kwonly_idx<Args...>;
            static constexpr size_t kwargs_idx      = has_kwargs ? CppSignature::kwargs_idx : CppSignature::size();

            /* The total number of partial arguments within the enclosing signature. */
            [[nodiscard]] static constexpr size_t size() noexcept { return Inner::size(); }
            [[nodiscard]] static constexpr bool empty() noexcept { return Inner::empty(); }

            /* Check whether a given index is within the bounds of the partial value
            tuple. */
            template <size_t I>
            [[nodiscard]] static constexpr bool contains() noexcept { return I < size(); }
            [[nodiscard]] static constexpr bool contains(size_t i) noexcept {return i < size(); }

            /* Check whether the named argument is contained within the partial value
            tuple. */
            template <static_str Key>
            [[nodiscard]] static constexpr bool contains() noexcept {
                return Inner::template contains<Key>();
            }
            template <typename T> requires (NameTable::template hashable<T>)
            [[nodiscard]] static constexpr bool contains(T&& key) noexcept {
                return Inner::contains(std::forward<T>(key));
            }

            /* Get the bound value at index K of the tuple.  If the partials are
            forwarded as an lvalue, then this will either directly reference the
            internal value if the corresponding argument expects an lvalue, or a copy
            if it expects an unqualified or rvalue type.  If the partials are given as
            an rvalue instead, then the copy will instead be optimized to a move. */
            template <size_t K> requires (K < size())
            [[nodiscard]] constexpr decltype(auto) get() const {
                return std::get<K>(values).get();
            }
            template <size_t K> requires (K < size())
            [[nodiscard]] constexpr decltype(auto) get() && {
                return std::move(std::get<K>(values)).get();
            }

            /* Get the bound value associated with the named argument, if it was given
            as a keyword argument.  If the partials are forwarded as an lvalue, then
            this will either directly reference the internal value if the corresponding
            argument expects an lvalue, or a copy if it expects an unqualified or rvalue
            type.  If the partials are given as an rvalue instead, then the copy will be
            optimized to a move. */
            template <static_str Name> requires (contains<Name>())
            [[nodiscard]] constexpr decltype(auto) get() const {
                return std::get<index<Name>()>(values).get();
            }
            template <static_str Name> requires (contains<Name>())
            [[nodiscard]] constexpr decltype(auto) get() && {
                return std::move(std::get<index<Name>()>(values)).get();
            }

            /* Get the index of a named argument within the partial values tuple. */
            template <static_str Key> requires (contains<Key>())
            [[nodiscard]] static constexpr size_t index() noexcept {
                return Inner::template index<Key>();
            }
            template <typename T> requires (NameTable::template hashable<T>)
            [[nodiscard]] static constexpr size_t index(T&& key) {
                return Inner::index(std::forward<T>(key));
            }

            /* Get the recorded name of the bound argument at index K of the partial
            tuple.  This may be empty if the argument was given as positional rather
            than keyword. */
            template <size_t K> requires (K < size())
            static constexpr static_str name = std::tuple_element_t<K, Tuple>::name;

            /* Given an index into the partial tuple, find the corresponding index in
            the enclosing parameter list. */
            template <size_t K> requires (K < size())
            static constexpr size_t rfind = std::tuple_element_t<K, Tuple>::index;

            template <size_t K> requires (K < size())
            using at = std::conditional_t<
                std::tuple_element_t<K, Tuple>::index == CppSignature::args_idx ||
                std::tuple_element_t<K, Tuple>::index == CppSignature::kwargs_idx,
                typename Inner::template at<K>,
                CppSignature::at<std::tuple_element_t<K, Tuple>::index>
            >;

            /* Bind an argument list to the partial values to enable the constructor. */
            template <typename... A>
            using Bind = Inner::template Bind<A...>;

            template <typename... A>
                requires (
                    !(meta::arg_traits<A>::variadic() || ...) &&
                    !(meta::arg_traits<A>::generic() || ...) &&
                    Bind<A...>::proper_argument_order &&
                    Bind<A...>::no_qualified_arg_annotations &&
                    Bind<A...>::no_duplicate_args &&
                    Bind<A...>::no_conflicting_values &&
                    Bind<A...>::no_extra_positional_args &&
                    Bind<A...>::no_extra_keyword_args &&
                    Bind<A...>::satisfies_required_args &&
                    Bind<A...>::can_convert
                )
            constexpr Partial(A&&... args) : values(
                []<size_t... Ks>(std::index_sequence<Ks...>, auto&&... args) -> Tuple {
                    return {{build<Ks>(std::forward<decltype(args)>(args)...)}...};
                }(std::index_sequence_for<A...>{}, std::forward<A>(args)...)
            ) {}

            /* Produce a new partial object with the given arguments in addition to any
            existing partial arguments.  This method is chainable, and the arguments will
            be interpreted as if they were passed to the signature's call operator.  They
            cannot include positional or keyword parameter packs. */
            template <typename Self, typename... A>
                requires (
                    !(meta::arg_traits<A>::variadic() || ...) &&
                    !(meta::arg_traits<A>::generic() || ...) &&
                    CppSignature::Bind<A...>::proper_argument_order &&
                    CppSignature::Bind<A...>::no_qualified_arg_annotations &&
                    CppSignature::Bind<A...>::no_duplicate_args &&
                    CppSignature::Bind<A...>::no_extra_positional_args &&
                    CppSignature::Bind<A...>::no_extra_keyword_args &&
                    CppSignature::Bind<A...>::no_conflicting_values &&
                    CppSignature::Bind<A...>::can_convert
                )
            [[nodiscard]] constexpr auto bind(this Self&& self, A&&... args) {
                return CppSignature::Bind<A...>::template merge<0, 0, 0>::bind(
                    std::forward<Self>(self),
                    std::forward<A>(args)...
                );
            }

            /* Unbind any partial arguments that have been accumulated thus far. */
            [[nodiscard]] static constexpr auto unbind() noexcept {
                return typename CppSignature::Unbind::Partial{};
            }
        };

        /* Instance-level constructor for a `::Partial` tuple. */
        template <typename... A>
            requires (
                !(meta::arg_traits<A>::variadic() || ...) &&
                !(meta::arg_traits<A>::generic() || ...) &&
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
        struct Bind : impl::signature_bind_tag {
            static constexpr size_t n_pos           = impl::n_pos<Values...>;
            static constexpr size_t n_kw            = impl::n_kw<Values...>;
            static constexpr bool has_pos           = impl::has_pos<Values...>;
            static constexpr bool has_args          = impl::has_args<Values...>;
            static constexpr bool has_kw            = impl::has_kw<Values...>;
            static constexpr bool has_kwargs        = impl::has_kwargs<Values...>;
            static constexpr size_t args_idx        = impl::args_idx<Values...>;
            static constexpr size_t kw_idx          = impl::kw_idx<Values...>;
            static constexpr size_t kwargs_idx      = impl::kwargs_idx<Values...>;

            /* The total number of bound arguments. */
            [[nodiscard]] static constexpr size_t size() noexcept {
                return sizeof...(Values);
            }
            [[nodiscard]] static constexpr bool empty() noexcept {
                return !sizeof...(Values);
            }

            /* Check whether a given index is within the bounds of the default value
            tuple. */
            template <size_t I>
            [[nodiscard]] static constexpr bool contains() noexcept {
                return I < size();
            }
            [[nodiscard]] static constexpr bool contains(size_t i) noexcept {
                return i < size();
            }

            /* Check whether the named argument is contained within the default value
            tuple. */
            template <static_str Key>
            [[nodiscard]] static constexpr bool contains() noexcept {
                return impl::arg_idx<Key, Values...> < size();
            }

            /* Get the index of a named argument within the default values tuple. */
            template <static_str Key> requires (contains<Key>())
            [[nodiscard]] static constexpr size_t index() noexcept {
                return impl::arg_idx<Key, Values...>;
            }

            template <size_t I> requires (I < size())
            using at = meta::unpack_type<I, Values...>;

        protected:
            friend Partial;

            template <size_t I, size_t K>
            static constexpr bool _in_partial = false;
            template <size_t I, size_t K> requires (K < Partial::size())
            static constexpr bool _in_partial<I, K> =
                I == Partial::template rfind<K> || _in_partial<I, K + 1>;
            template <size_t I>
            static constexpr bool in_partial = _in_partial<I, 0>;

            template <size_t I, size_t>
            static constexpr bool _no_extra_positional_args = true;
            template <size_t I, size_t J>
                requires (J < std::min({args_idx, kw_idx, kwargs_idx}))
            static constexpr bool _no_extra_positional_args<I, J> = [] {
                return
                    I < std::min(CppSignature::kwonly_idx, CppSignature::kwargs_idx) &&
                    _no_extra_positional_args<I + 1, J + !in_partial<I>>;
            }();

            template <size_t>
            static constexpr bool _no_extra_keyword_args = true;
            template <size_t J>
                requires (
                    J < kwargs_idx &&
                    !CppSignature::contains<meta::arg_traits<at<J>>::name>()
                )
            static constexpr bool _no_extra_keyword_args<J> =
                CppSignature::has_kwargs && _no_extra_keyword_args<J + 1>;
            template <size_t J>
                requires (
                    J < kwargs_idx &&
                    CppSignature::contains<meta::arg_traits<at<J>>::name>()
                )
            static constexpr bool _no_extra_keyword_args<J> = [] {
                return
                    meta::arg_traits<CppSignature::at<
                        CppSignature::index<meta::arg_traits<at<J>>::name>()
                    >>::kw() &&
                    _no_extra_keyword_args<J + 1>;
            }();

            template <size_t, size_t>
            static constexpr bool _no_conflicting_values = true;
            template <size_t I, size_t J> requires (I < CppSignature::size() && J < size())
            static constexpr bool _no_conflicting_values<I, J> = [] {
                using T = CppSignature::at<I>;

                constexpr bool kw_conflicts_with_partial =
                    meta::arg_traits<at<J>>::kw() &&
                    Partial::template contains<meta::arg_traits<at<J>>::name>();

                constexpr bool kw_conflicts_with_positional =
                    !in_partial<I> && !meta::arg_traits<T>::name.empty() && (
                        meta::arg_traits<T>::posonly() || J < std::min(kw_idx, kwargs_idx)
                    ) && contains<meta::arg_traits<T>::name>();

                return
                    !kw_conflicts_with_partial &&
                    !kw_conflicts_with_positional &&
                    _no_conflicting_values<
                        has_args && J == args_idx ? std::min({
                            CppSignature::args_idx + 1,
                            CppSignature::kwonly_idx,
                            CppSignature::kwargs_idx
                        }) : I + 1,
                        CppSignature::has_args && I == CppSignature::args_idx ? std::min({
                            kw_idx,
                            kwargs_idx
                        }) : J + !in_partial<I>
                    >;
            }();

            template <size_t I, size_t J>
            static constexpr bool _satisfies_required_args = true;
            template <size_t I, size_t J> requires (I < CppSignature::size())
            static constexpr bool _satisfies_required_args<I, J> = [] {
                return (
                    in_partial<I> ||
                    meta::arg_traits<CppSignature::at<I>>::opt() ||
                    meta::arg_traits<CppSignature::at<I>>::variadic() ||
                    (
                        meta::arg_traits<CppSignature::at<I>>::pos() &&
                        J < std::min(kw_idx, kwargs_idx)
                    ) || (
                        meta::arg_traits<CppSignature::at<I>>::kw() &&
                        contains<meta::arg_traits<CppSignature::at<I>>::name>()
                    )
                ) && _satisfies_required_args<
                    has_args && J == args_idx ?
                        std::min(CppSignature::kwonly_idx, CppSignature::kwargs_idx) :
                        I + 1,
                    CppSignature::has_args && I == CppSignature::args_idx ?
                        std::min(kw_idx, kwargs_idx) :
                        J + !in_partial<I>
                >;
            }();

            template <size_t, size_t>
            static constexpr bool _can_convert = true;
            template <size_t I, size_t J> requires (I < CppSignature::size() && J < size())
            static constexpr bool _can_convert<I, J> = [] {
                if constexpr (meta::arg_traits<CppSignature::at<I>>::args()) {
                    constexpr size_t source_kw = std::min(kw_idx, kwargs_idx);
                    return
                        []<size_t... Js>(std::index_sequence<Js...>) {
                            return (meta::convertible_to<
                                typename meta::arg_traits<at<J + Js>>::type,
                                typename meta::arg_traits<CppSignature::at<I>>::type
                            > && ...);
                        }(std::make_index_sequence<J < source_kw ? source_kw - J : 0>{}) &&
                        _can_convert<I + 1, source_kw>;

                } else if constexpr (meta::arg_traits<CppSignature::at<I>>::kwargs()) {
                    return
                        []<size_t... Js>(std::index_sequence<Js...>) {
                            return ((
                                CppSignature::contains<meta::arg_traits<at<kw_idx + Js>>::name>() ||
                                meta::convertible_to<
                                    typename meta::arg_traits<at<kw_idx + Js>>::type,
                                    typename meta::arg_traits<CppSignature::at<I>>::type
                                >
                            ) && ...);
                        }(std::make_index_sequence<size() - kw_idx>{}) &&
                        _can_convert<I + 1, J>;

                } else if constexpr (in_partial<I>) {
                    return _can_convert<I + 1, J>;

                } else if constexpr (meta::arg_traits<at<J>>::posonly()) {
                    return meta::convertible_to<
                        typename meta::arg_traits<at<J>>::type,
                        typename meta::arg_traits<CppSignature::at<I>>::type
                    > && _can_convert<I + 1, J + 1>;

                } else if constexpr (meta::arg_traits<at<J>>::kw()) {
                    constexpr static_str name = meta::arg_traits<at<J>>::name;
                    if constexpr (CppSignature::contains<name>()) {
                        if constexpr (!meta::convertible_to<
                            typename meta::arg_traits<at<J>>::type,
                            typename meta::arg_traits<CppSignature::at<CppSignature::index<name>()>>::type
                        >) {
                            return false;
                        };
                    }
                    return _can_convert<I + 1, J + 1>;

                } else if constexpr (meta::arg_traits<at<J>>::args()) {
                    constexpr size_t target_kw =
                        std::min(CppSignature::kwonly_idx, CppSignature::kwargs_idx);
                    return
                        []<size_t... Is>(std::index_sequence<Is...>) {
                            return (
                                (
                                    in_partial<I + Is> || meta::convertible_to<
                                        typename meta::arg_traits<at<J>>::type,
                                        typename meta::arg_traits<CppSignature::at<I + Is>>::type
                                    >
                                ) && ...
                            );
                        }(std::make_index_sequence<I < target_kw ? target_kw - I : 0>{}) &&
                        _can_convert<target_kw, J + 1>;

                } else if constexpr (meta::arg_traits<at<J>>::kwargs()) {
                    static constexpr size_t cutoff =
                        std::min({args_idx, kwonly_idx, kwargs_idx});
                    static constexpr size_t target_kw = has_args ?
                        CppSignature::kwonly_idx :
                        []<size_t... Ks>(std::index_sequence<Ks...>) {
                            return std::max(
                                CppSignature::kw_idx,
                                n_posonly + (0 + ... + (std::tuple_element_t<
                                    Ks,
                                    typename Partial::Tuple
                                >::target_idx < cutoff))
                            );
                        }(std::make_index_sequence<Partial::size()>{});
                    return
                        []<size_t... Is>(std::index_sequence<Is...>) {
                            return ((
                                in_partial<target_kw + Is> || contains<
                                    meta::arg_traits<CppSignature::at<target_kw + Is>>::name
                                >() || meta::convertible_to<
                                    typename meta::arg_traits<at<J>>::type,
                                    typename meta::arg_traits<CppSignature::at<target_kw + Is>>::type
                                >
                            ) && ...);
                        }(std::make_index_sequence<CppSignature::size() - target_kw>{}) &&
                        _can_convert<I, J + 1>;

                } else {
                    static_assert(false, "invalid argument kind");
                    return false;
                }
            }();

            template <size_t I, size_t K>
            static constexpr bool use_partial = false;
            template <size_t I, size_t K> requires (K < Partial::size())
            static constexpr bool use_partial<I, K> = Partial::template rfind<K> == I;

            template <size_t I, size_t J, size_t K>
            struct merge {
            private:
                using T = CppSignature::at<I>;
                static constexpr static_str name = meta::arg_traits<T>::name;

                template <size_t K2>
                static constexpr bool use_partial = false;
                template <size_t K2> requires (K2 < Partial::size())
                static constexpr bool use_partial<K2> = Partial::template rfind<K2> == I;

                template <size_t K2>
                static constexpr size_t consecutive = 0;
                template <size_t K2> requires (K2 < Partial::size())
                static constexpr size_t consecutive<K2> = 
                    Partial::template rfind<K2> == I ? consecutive<K2 + 1> + 1 : 0;

                template <typename... A>
                static constexpr size_t pos_range = 0;
                template <typename A, typename... As>
                static constexpr size_t pos_range<A, As...> =
                    meta::arg_traits<A>::pos() ? pos_range<As...> + 1 : 0;

                template <typename... A>
                static constexpr void assert_no_kwargs_conflict(A&&... args) {
                    if constexpr (!name.empty() && impl::kwargs_idx<A...> < sizeof...(A)) {
                        auto&& pack = meta::unpack_arg<impl::kwargs_idx<A...>>(
                            std::forward<A>(args)...
                        );
                        if (auto node = pack.extract(name)) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + static_str<>::from_int<I>
                            );
                        }
                    }
                }

                template <typename F>
                static constexpr decltype(auto) forward(auto&& value) {
                    if constexpr (meta::make_def<F>) {
                        if constexpr (meta::arg<decltype(value)>) {
                            return *std::forward<decltype(value)>(value);
                        } else {
                            return std::forward<decltype(value)>(value);
                        }
                    } else {
                        return to_arg<I>(std::forward<decltype(value)>(value));
                    }
                }

                template <size_t... Prev, size_t... Next, typename F>
                static constexpr decltype(auto) forward_partial(
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
                        meta::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        forward<F>(std::forward<decltype(parts)>(
                            parts
                        ).template get<K>()),
                        meta::unpack_arg<J + Next>(
                            std::forward<decltype(args)>(args)...
                        )...
                    );
                }

                template <size_t... Prev, size_t... Next, typename F>
                static constexpr decltype(auto) forward_default(
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
                        meta::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        forward<F>(std::forward<decltype(defaults)>(
                            defaults
                        ).template get<Defaults::template find<I>>()),
                        meta::unpack_arg<J + Next>(
                            std::forward<decltype(args)>(args)...
                        )...
                    );
                }

                template <size_t... Prev, size_t... Next, typename F>
                static constexpr decltype(auto) forward_positional(
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
                        meta::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        forward<F>(meta::unpack_arg<J>(
                            std::forward<decltype(args)>(args)...
                        )),
                        meta::unpack_arg<J + 1 + Next>(
                            std::forward<decltype(args)>(args)...
                        )...
                    );
                }

                template <size_t... Prev, size_t... Next, size_t... Rest, typename F>
                static constexpr decltype(auto) forward_keyword(
                    std::index_sequence<Prev...>,
                    std::index_sequence<Next...>,
                    std::index_sequence<Rest...>,
                    auto&& parts,
                    auto&& defaults,
                    F&& func,
                    auto&&... args
                ) {
                    constexpr size_t idx = impl::arg_idx<name, decltype(args)...>;
                    return merge<I + 1, J + 1, K>{}(
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(defaults)>(defaults),
                        std::forward<decltype(func)>(func),
                        meta::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        forward<F>(meta::unpack_arg<idx>(
                            std::forward<decltype(args)>(args)...
                        )),
                        meta::unpack_arg<J + Next>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        meta::unpack_arg<idx + 1 + Rest>(
                            std::forward<decltype(args)>(args)...
                        )...
                    );
                }

                template <size_t... Prev, size_t... Next, typename F>
                static constexpr decltype(auto) forward_from_pos_pack(
                    std::index_sequence<Prev...>,
                    std::index_sequence<Next...>,
                    auto&& parts,
                    auto&& defaults,
                    F&& func,
                    auto&&... args
                ) {
                    auto&& pack = meta::unpack_arg<impl::args_idx<decltype(args)...>>(
                        std::forward<decltype(args)>(args)...
                    );
                    return merge<I + 1, J + 1, K>{}(
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(defaults)>(defaults),
                        std::forward<decltype(func)>(func),
                        meta::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        forward<F>(pack.value()),
                        meta::unpack_arg<J + Next>(
                            std::forward<decltype(args)>(args)...
                        )...
                    );
                }

                template <size_t... Prev, size_t... Next, typename F>
                static constexpr decltype(auto) forward_from_kw_pack(
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
                        meta::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        forward<F>(std::forward<typename meta::arg_traits<T>::type>(
                            node.mapped()
                        )),
                        meta::unpack_arg<J + Next>(
                            std::forward<decltype(args)>(args)...
                        )...
                    );
                }

                template <size_t... Prev, size_t... Next, typename F>
                static constexpr decltype(auto) drop_empty_pack(
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
                        meta::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        meta::unpack_arg<J + 1 + Next>(
                            std::forward<decltype(args)>(args)...
                        )...
                    );
                }

                template <typename P, typename... A>
                static auto variadic_positional(P&& parts, A&&... args) {
                    static constexpr size_t cutoff = std::min(
                        impl::kw_idx<A...>,
                        impl::kwargs_idx<A...>
                    );
                    static constexpr size_t diff = J < cutoff ? cutoff - J : 0;

                    // allocate variadic positional array
                    using vec = std::vector<typename meta::arg_traits<T>::type>;
                    vec out;
                    if constexpr (diff) {
                        if constexpr (impl::args_idx<A...> < sizeof...(A)) {
                            out.reserve(
                                consecutive<K> + 
                                (diff - 1) +
                                meta::unpack_arg<impl::args_idx<A...>>(
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
                                impl::args_idx<A...> < sizeof...(A) &&
                                J2 == impl::args_idx<A...>
                            ) {
                                auto&& pack = meta::unpack_arg<J2>(
                                    std::forward<decltype(args)>(args)...
                                );
                                out.insert(out.end(), pack.begin, pack.end);
                            } else {
                                out.emplace_back(meta::unpack_arg<J2>(
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
                        typename meta::arg_traits<T>::type
                    >;
                    map out;
                    if constexpr (impl::kwargs_idx<A...> < sizeof...(A)) {
                        out.reserve(
                            consecutive<K> +
                            (diff - 1) +
                            meta::unpack_arg<impl::kwargs_idx<A...>>(
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
                                impl::kwargs_idx<A...> < sizeof...(A) &&
                                J2 == impl::kwargs_idx<A...>
                            ) {
                                auto&& pack = meta::unpack_arg<J2>(
                                    std::forward<decltype(args)>(args)...
                                );
                                auto it = pack.begin();
                                auto end = pack.end();
                                while (it != end) {
                                    // postfix++ required to increment before invalidation
                                    auto node = pack.extract(it++);
                                    auto rc = out.insert(node);
                                    if (!rc.inserted) {
                                        throw TypeError(
                                            "duplicate value for parameter '" +
                                            node.key() + "'"
                                        );
                                    }
                                }
                            } else {
                                out.emplace(
                                    std::string(meta::arg_traits<meta::unpack_type<J2, A...>>::name),
                                    *meta::unpack_arg<J2>(
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
                    if constexpr (meta::arg_traits<T>::args()) {
                        static constexpr size_t cutoff = impl::kw_idx<A...>;
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
                                meta::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                std::forward<decltype(parts)>(
                                    parts
                                ).template get<K + Ks>()...,
                                meta::unpack_arg<cutoff + Next>(
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

                    } else if constexpr (meta::arg_traits<T>::kwargs()) {
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
                                meta::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                arg<Partial::template name<K + Ks>> =
                                    std::forward<decltype(parts)>(
                                        parts
                                    ).template get<K + Ks>()...,
                                meta::unpack_arg<J + Next>(
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
                                meta::arg_traits<T>::pos() &&
                                meta::arg_traits<T>::kw() &&
                                (impl::kw_idx<A...> == sizeof...(A) || J < impl::kw_idx<A...>)
                            )) {
                                return merge<I + 1, J + 1, K + 1>::bind(
                                    std::forward<decltype(parts)>(parts),
                                    meta::unpack_arg<Prev>(
                                        std::forward<decltype(args)>(args)...
                                    )...,
                                    std::forward<decltype(parts)>(
                                        parts
                                    ).template get<K>(),
                                    meta::unpack_arg<J + Next>(
                                        std::forward<decltype(args)>(args)...
                                    )...
                                );
                            } else {
                                return merge<I + 1, J + 1, K + 1>::bind(
                                    std::forward<decltype(parts)>(parts),
                                    meta::unpack_arg<Prev>(
                                        std::forward<decltype(args)>(args)...
                                    )...,
                                    arg<name> = std::forward<decltype(parts)>(
                                        parts
                                    ).template get<K>(),
                                    meta::unpack_arg<J + Next>(
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
                static constexpr decltype(auto) operator()(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    if constexpr (meta::arg_traits<T>::posonly()) {
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
                        } else if constexpr (J < sizeof...(A) && J == impl::args_idx<A...>) {
                            auto&& pack = meta::unpack_arg<J>(std::forward<A>(args)...);
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
                        } else if constexpr (meta::arg_traits<T>::opt()) {
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
                            throw TypeError(
                                "no match for positional-only parameter at index " +
                                static_str<>::from_int<I>
                            );
                        } else {
                            throw TypeError(
                                "no match for positional-only parameter '" + name +
                                "' at index " + static_str<>::from_int<I>
                            );
                        }

                    } else if constexpr (meta::arg_traits<T>::pos()) {
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
                        } else if constexpr (J < sizeof...(A) && J == impl::args_idx<A...>) {
                            auto&& pack = meta::unpack_arg<J>(std::forward<A>(args)...);
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
                        } else if constexpr (impl::arg_idx<name, A...> < sizeof...(A)) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            constexpr size_t idx = impl::arg_idx<name, A...>;
                            return forward_keyword(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<idx - J>{},
                                std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (impl::kwargs_idx<A...> < sizeof...(A)) {
                            auto&& pack = meta::unpack_arg<impl::kwargs_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            if (auto node = pack.extract(std::string_view(name))) {
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
                                if constexpr (meta::arg_traits<T>::opt()) {
                                    return forward_default(
                                        std::make_index_sequence<J>{},
                                        std::make_index_sequence<sizeof...(A) - J>{},
                                        std::forward<P>(parts),
                                        std::forward<D>(defaults),
                                        std::forward<F>(func),
                                        std::forward<A>(args)...
                                    );
                                } else {
                                    throw TypeError(
                                        "no match for parameter '" + name +
                                        "' at index " + static_str<>::from_int<I>
                                    );
                                }
                            }
                        } else if constexpr (meta::arg_traits<T>::opt()) {
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
                            throw TypeError(
                                "no match for parameter '" + name +
                                "' at index " + static_str<>::from_int<I>
                            );
                        }

                    } else if constexpr (meta::arg_traits<T>::kw()) {
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
                        } else if constexpr (impl::arg_idx<name, A...> < sizeof...(A)) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            constexpr size_t idx = impl::arg_idx<name, A...>;
                            return forward_keyword(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<idx - J>{},
                                std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (impl::kwargs_idx<A...> < sizeof...(A)) {
                            auto&& pack = meta::unpack_arg<impl::kwargs_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            if (auto node = pack.extract(std::string_view(name))) {
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
                                if constexpr (meta::arg_traits<T>::opt()) {
                                    return forward_default(
                                        std::make_index_sequence<J>{},
                                        std::make_index_sequence<sizeof...(A) - J>{},
                                        std::forward<P>(parts),
                                        std::forward<D>(defaults),
                                        std::forward<F>(func),
                                        std::forward<A>(args)...
                                    );
                                } else {
                                    throw TypeError(
                                        "no match for parameter '" + name +
                                        "' at index " + static_str<>::from_int<I>
                                    );
                                }
                            }
                        } else if constexpr (meta::arg_traits<T>::opt()) {
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
                            throw TypeError(
                                "no match for keyword-only parameter '" + name +
                                "' at index " + static_str<>::from_int<I>
                            );
                        }

                    } else if constexpr (meta::arg_traits<T>::args()) {
                        if constexpr (meta::make_def<F>) {
                            return []<size_t... Prev, size_t... Next>(
                                std::index_sequence<Prev...>,
                                std::index_sequence<Next...>,
                                auto&& parts,
                                auto&& defaults,
                                auto&& func,
                                auto&&... args
                            ) {
                                if constexpr (use_partial<K>) {
                                    return merge<I, J + 1, K + 1>{}(
                                        std::forward<decltype(parts)>(parts),
                                        std::forward<decltype(defaults)>(defaults),
                                        std::forward<decltype(func)>(func),
                                        meta::unpack_arg<Prev>(
                                            std::forward<decltype(args)>(args)...
                                        )...,
                                        forward<F>(std::forward<decltype(parts)>(
                                            parts
                                        ).template get<K>()),
                                        meta::unpack_arg<J + Next>(
                                            std::forward<decltype(args)>(args)...
                                        )...
                                    );
                                } else if constexpr (J == impl::args_idx<decltype(args)...>) {
                                    auto&& pack = meta::unpack_arg<J>(
                                        std::forward<decltype(args)>(args)...
                                    );
                                    if (pack.has_value()) {
                                        if constexpr (std::is_invocable_v<
                                            F,
                                            decltype(args)...,
                                            decltype(pack.value())
                                        >) {
                                            return merge<I, J + 1, K>{}(
                                                std::forward<decltype(parts)>(parts),
                                                std::forward<decltype(defaults)>(defaults),
                                                std::forward<decltype(func)>(func),
                                                meta::unpack_arg<Prev>(
                                                    std::forward<decltype(args)>(args)...
                                                )...,
                                                forward<F>(pack.value()),
                                                meta::unpack_arg<J + Next>(
                                                    std::forward<decltype(args)>(args)...
                                                )...
                                            );
                                        } else {
                                            std::string message =
                                                "too many arguments in positional parameter "
                                                "pack: ['" + repr(pack.value());
                                            while (pack.has_value()) {
                                                message += ", '" + repr(pack.value());
                                            }
                                            message += "']";
                                            throw TypeError(message);
                                        }
                                    } else {
                                        return drop_empty_pack(
                                            std::index_sequence<Prev...>(),
                                            std::index_sequence<Next...>(),
                                            std::forward<decltype(parts)>(parts),
                                            std::forward<decltype(defaults)>(defaults),
                                            std::forward<decltype(func)>(func),
                                            std::forward<decltype(args)>(args)...
                                        );
                                    }
                                } else {
                                    static constexpr size_t cutoff = std::min(
                                        impl::kw_idx<decltype(args)...>,
                                        impl::kwargs_idx<decltype(args)...>
                                    );
                                    if constexpr (J < cutoff) {
                                        return merge<I, J + 1, K>{}(
                                            std::forward<decltype(parts)>(parts),
                                            std::forward<decltype(defaults)>(defaults),
                                            std::forward<decltype(func)>(func),
                                            meta::unpack_arg<Prev>(
                                                std::forward<decltype(args)>(args)...
                                            )...,
                                            forward<F>(meta::unpack_arg<J>(
                                                std::forward<decltype(args)>(args)...
                                            )),
                                            meta::unpack_arg<J + Next>(
                                                std::forward<decltype(args)>(args)...
                                            )...
                                        );
                                    } else {
                                        return merge<I + 1, J, K>{}(
                                            std::forward<decltype(parts)>(parts),
                                            std::forward<decltype(defaults)>(defaults),
                                            std::forward<decltype(func)>(func),
                                            meta::unpack_arg<Prev>(
                                                std::forward<decltype(args)>(args)...
                                            )...,
                                            meta::unpack_arg<J + Next>(
                                                std::forward<decltype(args)>(args)...
                                            )...
                                        );
                                    }
                                }
                            }(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<sizeof...(A) - J>{},
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        } else {
                            static constexpr size_t cutoff = std::min(
                                impl::kw_idx<A...>,
                                impl::kwargs_idx<A...>
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
                                    meta::unpack_arg<Prev>(
                                        std::forward<decltype(args)>(args)...
                                    )...,
                                    to_arg<I>(variadic_positional(
                                        std::forward<decltype(parts)>(parts),
                                        std::forward<decltype(args)>(args)...
                                    )),
                                    meta::unpack_arg<cutoff + Next>(
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
                        }

                    } else if constexpr (meta::arg_traits<T>::kwargs()) {
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
                                meta::unpack_arg<Prev>(
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
            struct merge<CppSignature::size(), J, K> {
            private:
                /* Convert a terminal argument list into an equivalent partial signature,
                wherein the arguments are bound to their corresponding values in the
                enclosing signature. */
                template <typename... As>
                struct sig {
                    // elementwise traversal metafunction
                    template <typename out, size_t, size_t>
                    struct advance { using type = out; };
                    template <typename... out, size_t I2, size_t J2>
                        requires (I2 < CppSignature::size())
                    struct advance<Return(out...), I2, J2> {
                        template <typename>
                        struct maybe_bind;

                        template <typename T> requires (meta::arg_traits<T>::posonly())
                        struct maybe_bind<T> {
                            // If no matching partial exists, forward the unbound arg
                            template <size_t J3>
                            struct append {
                                using type = advance<Return(out..., T), I2 + 1, J3>::type;
                            };
                            // Otherwise, bind the partial and advance
                            template <size_t J3> requires (J3 < impl::kw_idx<As...>)
                            struct append<J3> {
                                using S = meta::unpack_type<J3, As...>;
                                using B = meta::arg_traits<T>::template bind<S>;
                                using type = advance<Return(out..., B), I2 + 1, J3 + 1>::type;
                            };
                            using type = append<J2>::type;
                        };

                        template <typename T> requires (meta::arg_traits<T>::pos() && meta::arg_traits<T>::kw())
                        struct maybe_bind<T> {
                            // If no matching partial exists, forward the unbound arg
                            template <size_t J3>
                            struct append {
                                using type = advance<Return(out..., T), I2 + 1, J3>::type;
                            };
                            // If a partial positional arg exists, bind it and advance
                            template <size_t J3> requires (J3 < impl::kw_idx<As...>)
                            struct append<J3> {
                                using S = meta::unpack_type<J3, As...>;
                                using B = meta::arg_traits<T>::template bind<S>;
                                using type = advance<Return(out..., B), I2 + 1, J3 + 1>::type;
                            };
                            // If a partial keyword arg exists, bind it and advance
                            template <size_t J3> requires (
                                J3 >= impl::kw_idx<As...> &&
                                impl::arg_idx<meta::arg_traits<T>::name, As...> < sizeof...(As)
                            )
                            struct append<J3> {
                                static constexpr static_str name = meta::arg_traits<T>::name;
                                static constexpr size_t idx = impl::arg_idx<name, As...>;
                                using S = meta::unpack_type<idx, As...>;
                                using B = meta::arg_traits<T>::template bind<S>;
                                using type = advance<Return(out..., B), I2 + 1, J3>::type;
                            };
                            using type = append<J2>::type;
                        };

                        template <typename T> requires (meta::arg_traits<T>::kwonly())
                        struct maybe_bind<T> {
                            // If no matching partial exists, forward the unbound arg
                            template <size_t J3>
                            struct append {
                                using type = advance<Return(out..., T), I2 + 1, J3>::type;
                            };
                            // If a partial keyword arg exists, bind it and advance
                            template <size_t J3>
                                requires (
                                    impl::arg_idx<meta::arg_traits<T>::name, As...> < sizeof...(As)
                                )
                            struct append<J3> {
                                static constexpr static_str name = meta::arg_traits<T>::name;
                                static constexpr size_t idx = impl::arg_idx<name, As...>;
                                using S = meta::unpack_type<idx, As...>;
                                using B = meta::arg_traits<T>::template bind<S>;
                                using type = advance<Return(out..., B), I2 + 1, J3>::type;
                            };
                            using type = append<J2>::type;
                        };

                        template <typename T> requires (meta::arg_traits<T>::args())
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
                                    using B = meta::arg_traits<T>::template bind<r2, r2s...>;
                                    using type = advance<Return(out..., B), I2 + 1, J3>::type;
                                };
                                using type = collect<result>::type;
                            };
                            template <typename... result, size_t J3>
                                requires (J3 < impl::kw_idx<As...>)
                            struct append<args<result...>, J3> {
                                // Append remaining partial positional args to the output pack
                                using type = append<
                                    args<result..., meta::unpack_type<J3, As...>>,
                                    J3 + 1
                                >::type;
                            };
                            using type = append<args<>, J2>::type;
                        };

                        template <typename T> requires (meta::arg_traits<T>::kwargs())
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
                                    using B = meta::arg_traits<T>::template bind<r2, r2s...>;
                                    using type = advance<Return(out..., B), I2 + 1, J3>::type;
                                };
                                using type = collect<result>::type;
                            };
                            template <typename... result, size_t J3> requires (J3 < sizeof...(As))
                            struct append<args<result...>, J3> {
                                // If the keyword arg is in the target signature, ignore
                                template <typename S>
                                struct collect {
                                    using type = args<result...>;
                                };
                                // Otherwise, append it to the output pack and continue
                                template <typename S>
                                    requires (!CppSignature::template contains<meta::arg_traits<S>::name>())
                                struct collect<S> {
                                    using type = args<result..., S>;
                                };
                                using type = append<
                                    typename collect<meta::unpack_type<J3, As...>>::type,
                                    J3 + 1
                                >::type;
                            };
                            // Start at the beginning of the partial keywords
                            using type = append<args<>, impl::kw_idx<As...>>::type;
                        };

                        // Feed in the unbound argument and return a possibly bound equivalent
                        using type = maybe_bind<
                            typename meta::arg_traits<CppSignature::at<I2>>::unbind
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
                static constexpr decltype(auto) operator()(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    // validate and remove positional parameter packs
                    if constexpr (impl::args_idx<A...> < sizeof...(A)) {
                        static constexpr size_t idx = impl::args_idx<A...>;
                        if constexpr (!CppSignature::has_args) {
                            auto&& pack = meta::unpack_arg<idx>(std::forward<A>(args)...);
                            pack.validate();
                        }
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
                                meta::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                meta::unpack_arg<idx + 1 + Next>(
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
                    } else if constexpr (impl::kwargs_idx<A...> < sizeof...(A)) {
                        static constexpr size_t idx = impl::kwargs_idx<A...>;
                        if constexpr (!CppSignature::has_kwargs) {
                            auto&& pack = meta::unpack_arg<idx>(std::forward<A>(args)...);
                            pack.validate();
                        }
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
                                meta::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                meta::unpack_arg<idx + 1 + Next>(
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
            static constexpr bool proper_argument_order =
                impl::proper_argument_order<Values...>;

            static constexpr bool no_qualified_arg_annotations =
                impl::no_qualified_arg_annotations<Values...>;

            static constexpr bool no_duplicate_args =
                impl::no_duplicate_args<Values...>;

            static constexpr bool no_extra_positional_args =
                !has_pos || CppSignature::has_args || _no_extra_positional_args<0, 0>;

            static constexpr bool no_extra_keyword_args =
                !has_kw || CppSignature::has_kwargs || _no_extra_keyword_args<kw_idx>;

            static constexpr bool no_conflicting_values =
                _no_conflicting_values<0, 0>;

            static constexpr bool can_convert =
                _can_convert<0, 0>;

            static constexpr bool satisfies_required_args =
                _satisfies_required_args<0, 0>;

            /// TODO: satisfies generic args, and does not possess any generic args as input.
            /// The same should be applied to all subsequent locations where arguments
            /// are checked.

            /* Invoke a C++ function from C++ using Python-style arguments. */
            template <meta::inherits<Partial> P, meta::inherits<Defaults> D, typename F>
                requires (
                    CppSignature::invocable<F> &&
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
                return impl::invoke_with_packs(
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

        /* Unbinding a signature strips any partial arguments that have been encoded
        thus far and returns a new signature without them. */
        using Unbind = _Unbind<Return(), Args...>::type;

        /* Call a C++ function from C++ using Python-style arguments. */
        template <
            meta::inherits<Partial> P,
            meta::inherits<Defaults> D,
            typename F,
            typename... A
        >
            requires (
                invocable<F> &&
                !(meta::arg_traits<A>::generic() || ...) &&
                Bind<A...>::proper_argument_order &&
                Bind<A...>::no_qualified_arg_annotations &&
                Bind<A...>::no_duplicate_args &&
                Bind<A...>::no_extra_positional_args &&
                Bind<A...>::no_extra_keyword_args &&
                Bind<A...>::no_conflicting_values &&
                Bind<A...>::can_convert &&
                Bind<A...>::satisfies_required_args
            )
        static constexpr Return operator()(
            P&& partial,
            D&& defaults,
            F&& func,
            A&&... args
        ) {
            return Bind<A...>{}(
                std::forward<P>(partial),
                std::forward<D>(defaults),
                std::forward<F>(func),
                std::forward<A>(args)...
            );
        }

        /// TODO: capture() is no longer necessary?  Just use make_def() instead?

        /* Capture a C++ function and generate a new function object that exactly matches
        the enclosing signature, without any partial arguments.  A function of this form
        can be used to initialize a `def` statement that delegates to an external C++
        function with an arbitrary signature, as long as it can be invoked with the
        enclosing arguments and returns a compatible type. */
        template <typename F> requires (invocable<F>)
        [[nodiscard]] static constexpr auto capture(F&& func) {
            struct Func {
                std::remove_cvref_t<F> func;
                constexpr Return operator()(
                    typename meta::arg_traits<Args>::unbind... args
                ) const {
                    return func(
                        std::forward<typename meta::arg_traits<Args>::unbind>(args)...
                    );
                }
            };
            return Func{std::forward<F>(func)};
        }

        /// TODO: revisit to_string to pull it out of the signature<> specializations and
        /// reduce code duplication.  It should also always produce valid C++ and/or Python
        /// source code, and if defaults are not given, they will be replaced with ellipsis
        /// in the Python style, such that the output can be written to a .pyi file.

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
        [[nodiscard]] static constexpr std::string to_string(
            const std::string& name,
            const std::string& prefix = "",
            size_t max_width = std::numeric_limits<size_t>::max(),
            size_t indent = 4
        ) {
            std::vector<std::string> components;
            components.reserve(size() * 3 + 2);
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
                if constexpr (I < size()) {
                    using T = CppSignature::at<I>;
                    if constexpr (meta::arg_traits<T>::args()) {
                        components.emplace_back(std::string("*" + meta::arg_traits<T>::name));
                    } else if constexpr (meta::arg_traits<T>::kwargs()) {
                        components.emplace_back(std::string("**" + meta::arg_traits<T>::name));
                    } else {
                        if constexpr (meta::arg_traits<T>::posonly()) {
                            last_posonly = I;
                        } else if constexpr (meta::arg_traits<T>::kwonly() && !CppSignature::has_args) {
                            if (first_kwonly == std::numeric_limits<size_t>::max()) {
                                first_kwonly = I;
                            }
                        }
                        components.emplace_back(std::string(meta::arg_traits<T>::name));
                    }
                    components.emplace_back(
                        std::string(demangle<typename meta::arg_traits<T>::type>())
                    );
                    if constexpr (meta::arg_traits<T>::opt()) {
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

            if constexpr (meta::is_void<Return>) {
                components.emplace_back("None");
            } else {
                components.emplace_back(std::string(demangle<Return>()));
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

        template <meta::inherits<Defaults> D>
        [[nodiscard]] static constexpr std::string to_string(
            const std::string& name,
            D&& defaults,
            const std::string& prefix = "",
            size_t max_width = std::numeric_limits<size_t>::max(),
            size_t indent = 4
        ) {
            std::vector<std::string> components;
            components.reserve(size() * 3 + 2);
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
                if constexpr (I < size()) {
                    using T = CppSignature::at<I>;
                    if constexpr (meta::arg_traits<T>::args()) {
                        components.emplace_back(std::string("*" + meta::arg_traits<T>::name));
                    } else if constexpr (meta::arg_traits<T>::kwargs()) {
                        components.emplace_back(std::string("**" + meta::arg_traits<T>::name));
                    } else {
                        if constexpr (meta::arg_traits<T>::posonly()) {
                            last_posonly = I;
                        } else if constexpr (meta::arg_traits<T>::kwonly() && !CppSignature::has_args) {
                            if (first_kwonly == std::numeric_limits<size_t>::max()) {
                                first_kwonly = I;
                            }
                        }
                        components.emplace_back(std::string(meta::arg_traits<T>::name));
                    }
                    components.emplace_back(
                        std::string(demangle<typename meta::arg_traits<T>::type>())
                    );
                    if constexpr (meta::arg_traits<T>::opt()) {
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

            if constexpr (meta::is_void<Return>) {
                components.emplace_back("None");
            } else {
                components.emplace_back(std::string(demangle<Return>()));
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

    /* If this control structure is enabled, the unary `call()` operator will accept
    functions that may have partial and/or default arguments and call them directly,
    rather than requiring the creation of a separate defaults tuple.  This allows
    `call()` to be used on `def` statements and other bertrand function objects. */
    template <typename F>
    struct call_passthrough { static constexpr bool enable = false; };
    template <meta::def F>
    struct call_passthrough<F> { static constexpr bool enable = true; };
    template <meta::chain F>
        requires (call_passthrough<
            typename std::remove_cvref_t<F>::template at<0>
        >::enable)
    struct call_passthrough<F> { static constexpr bool enable = true; };

}


/* The canonical form of `signature`, which encapsulates all of the internal call
machinery, as much as possible of which is evaluated at compile time.  All other
specializations should redirect to this form in order to avoid reimplementing the nuts
and bolts of the function ecosystem. */
template <typename Return, typename... Args>
struct signature<Return(Args...)> :
    impl::CppSignature<impl::CppParam, Return, Args...>
{};


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
template <meta::has_call_operator T>
struct signature<T> : signature<decltype(&std::remove_reference_t<T>::operator())> {};
template <meta::def T>
struct signature<T> : std::remove_reference_t<T>::Signature {};
template <meta::chain T>
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


/////////////////////////////////////
////    CHAIN + COMPREHENSION    ////
/////////////////////////////////////


namespace impl {
    struct Chain_tag {};
    struct Comprehension_tag {};
}


namespace meta {

    template <typename T>
    concept Chain = inherits<T, impl::Chain_tag>;

    template <typename T>
    concept Comprehension = inherits<T, impl::Comprehension_tag>;

    template <typename T>
    concept unpack_operator = detail::enable_unpack_operator<meta::unqualify<T>>;

    template <typename T>
    concept comprehension_operator =
        detail::enable_comprehension_operator<meta::unqualify<T>>;

    /// TODO: not entirely sure if viewable and transformable are necessary.

    template <typename Range, typename View>
    concept viewable = requires(Range r, View v) { ::std::views::all(r) | v; };

    template <typename Range, typename Func>
    concept transformable = !viewable<Range, Func> && requires(Range r, Func f) {
        ::std::views::transform(r, f);
    };

}




/* A range adaptor returned by the `->*` operator, which allows for Python-style
iterator comprehensions in C++.  This is essentially equivalent to a
`std::views::transform_view`, but with the caveat that any function which returns
another view will have its result flattened into the output, allowing for nested
comprehensions and filtering according to Python semantics. */
template <typename Range, typename Func> requires (meta::transformable<Range, Func>)
struct comprehension :
    std::ranges::view_interface<comprehension<Range, Func>>,
    impl::comprehension_tag
{
private:
    using View = decltype(std::views::transform(
        std::declval<Range>(),
        std::declval<Func>()
    ));
    using Value = std::ranges::range_value_t<View>;

    View view;

    template <typename>
    struct iterator {
        using Begin = std::ranges::iterator_t<const View>;
        using End = std::ranges::sentinel_t<const View>;
    };
    template <typename Value> requires (std::ranges::view<std::remove_cvref_t<Value>>)
    struct iterator<Value> {
        struct Begin {
            using iterator_category = std::input_iterator_tag;
            using difference_type = std::ranges::range_difference_t<const Value>;
            using value_type = std::ranges::range_value_t<const Value>;
            using pointer = value_type*;
            using reference = value_type&;

            using Iter = std::ranges::iterator_t<const View>;
            using End = std::ranges::sentinel_t<const View>;
            Iter iter;
            End end;
            struct Inner {
                using Iter = std::ranges::iterator_t<const Value>;
                using End = std::ranges::sentinel_t<const Value>;
                Value curr;
                Iter iter = std::ranges::begin(curr);
                End end = std::ranges::end(curr);
            } inner;

            Begin(Iter&& begin, End&& end) :
                iter(std::move(begin)),
                end(std::move(end)),
                inner([](Iter& begin, End& end) -> Inner {
                    while (begin != end) {
                        Inner curr = {*begin};
                        if (curr.iter != curr.end) {
                            return curr;
                        }
                        ++begin;
                    }
                    return {};
                }(this->iter, this->end))
            {}

            Begin& operator++() {
                if (++inner.iter == inner.end) {
                    while (++iter != end) {
                        inner.curr = *iter;
                        inner.iter = std::ranges::begin(inner.curr);
                        inner.end = std::ranges::end(inner.curr);
                        if (inner.iter != inner.end) {
                            break;
                        }
                    }
                }
                return *this;
            }

            Begin operator++(int) {
                Begin copy = *this;
                ++(*this);
                return copy;
            }

            decltype(auto) operator*() const { return *inner.iter; }
            decltype(auto) operator->() const { return inner.iter.operator->(); }

            friend bool operator==(const Begin& self, sentinel) {
                return self.iter == self.end;
            }

            friend bool operator==(sentinel, const Begin& self) {
                return self.iter == self.end;
            }

            friend bool operator!=(const Begin& self, sentinel) {
                return self.iter != self.end;
            }

            friend bool operator!=(sentinel, const Begin& self) {
                return self.iter != self.end;
            }
        };
        using End = sentinel;
    };

    using Begin = iterator<Value>::Begin;
    using End = iterator<Value>::End;

public:
    comprehension() = default;
    comprehension(Range range, Func func) :
        view(std::views::transform(
            std::forward<Range>(range),
            std::forward<Func>(func)
        ))
    {}

    [[nodiscard]] Begin begin() const {
        if constexpr (std::ranges::view<std::remove_cvref_t<Value>>) {
            return {std::ranges::begin(view), std::ranges::end(view)};
        } else {
            return std::ranges::begin(view);
        }
    }

    [[nodiscard]] End end() const {
        if constexpr (std::ranges::view<std::remove_cvref_t<Value>>) {
            return {};
        } else {
            return std::ranges::end(view);
        }
    }

    /* Implicitly convert the comprehension into any type that can be constructed from
    the iterator pair. */
    template <typename T> requires (std::constructible_from<T, Begin, End>)
    [[nodiscard]] operator T() const { return T(begin(), end()); }
};


template <typename Range, typename Func> requires (meta::transformable<Range, Func>)
comprehension(Range&&, Func&&) -> comprehension<Range, Func>;


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

    template <meta::is<F> First>
    constexpr chain(First&& func) : func(std::forward<First>(func)) {}

    /* Invoke the function chain, piping the return value from the first function into
    the input for the second function, and so on. */
    template <typename Self, typename... A> requires (std::invocable<F, A...>)
    constexpr decltype(auto) operator()(this Self&& self, A&&... args) {
        return std::forward<Self>(self).func(std::forward<A>(args)...);
    }

    /* Get the component function at index I. */
    template <size_t I, typename Self> requires (I < size())
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
        return std::forward<Self>(self).func;
    }
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
        template <typename H> requires (std::invocable<H, R>)
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

    template <meta::is<F1> First, meta::is<F2> Next, meta::is<Fs>... Rest>
    constexpr chain(First&& first, Next&& next, Rest&&... rest) :
        base(std::forward<Next>(next), std::forward<Rest>(rest)...),
        func(std::forward<First>(first))
    {}

    /* Invoke the function chain, piping the return value from the first function into
    the input for the second function, and so on. */
    template <typename Self, typename... A>
        requires (std::invocable<F1, A...> && chainable<A...>)
    constexpr decltype(auto) operator()(this Self&& self, A&&... args) {
        return static_cast<meta::qualify<base, Self>>(std::forward<Self>(self))(
            std::forward<Self>(self).func(std::forward<A>(args)...)
        );
    }

    /* Get the component function at index I. */
    template <size_t I, typename Self> requires (I < size())
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
        if constexpr (I == 0) {
            return std::forward<Self>(self).func;
        } else {
            using parent = meta::qualify<base, Self>;
            return static_cast<parent>(std::forward<Self>(self)).template get<I - 1>();
        }
    }
};


template <typename F, typename... Fs>
chain(F, Fs...) -> chain<F, Fs...>;


template <meta::chain Self, meta::chain Next>
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


template <meta::chain Self, typename Next>
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


template <typename Prev, meta::chain Self>
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


namespace meta {

    /* A template constraint that controls whether the `bertrand::call()` operator is
    enabled for a given C++ function and argument list. */
    template <typename F, typename... Args>
    concept callable =
        bertrand::signature<F>::enable &&
        bertrand::signature<F>::within_arg_limit &&
        bertrand::signature<F>::proper_argument_order &&
        bertrand::signature<F>::no_qualified_arg_annotations &&
        bertrand::signature<F>::no_duplicate_args &&
        bertrand::signature<F>::template Bind<Args...>::proper_argument_order &&
        bertrand::signature<F>::template Bind<Args...>::no_qualified_arg_annotations &&
        bertrand::signature<F>::template Bind<Args...>::no_duplicate_args &&
        bertrand::signature<F>::template Bind<Args...>::no_conflicting_values &&
        bertrand::signature<F>::template Bind<Args...>::no_extra_positional_args &&
        bertrand::signature<F>::template Bind<Args...>::no_extra_keyword_args &&
        bertrand::signature<F>::template Bind<Args...>::satisfies_required_args &&
        bertrand::signature<F>::template Bind<Args...>::can_convert;

    /* A template constraint that controls whether the `bertrand::def()` operator is
    enabled for a given C++ function and argument list. */
    template <typename F, typename... Args>
    concept partially_callable =
        bertrand::signature<F>::enable &&
        !(meta::arg_traits<Args>::variadic() || ...) &&
        bertrand::signature<F>::proper_argument_order &&
        bertrand::signature<F>::no_qualified_arg_annotations &&
        bertrand::signature<F>::no_duplicate_args &&
        bertrand::signature<F>::template Bind<Args...>::proper_argument_order &&
        bertrand::signature<F>::template Bind<Args...>::no_qualified_arg_annotations &&
        bertrand::signature<F>::template Bind<Args...>::no_duplicate_args &&
        bertrand::signature<F>::template Bind<Args...>::no_conflicting_values &&
        bertrand::signature<F>::template Bind<Args...>::no_extra_positional_args &&
        bertrand::signature<F>::template Bind<Args...>::no_extra_keyword_args &&
        bertrand::signature<F>::template Bind<Args...>::can_convert;

    /* A template constraint that controls whether the `bertrand::Function()` type can
    be instantiated with a given Python-compatible signature. */
    template <typename F>
    concept py_function =
        bertrand::signature<F>::enable &&
        bertrand::signature<F>::python &&
        bertrand::signature<F>::within_arg_limit &&
        bertrand::signature<F>::proper_argument_order &&
        bertrand::signature<F>::no_qualified_args &&
        bertrand::signature<F>::no_qualified_arg_annotations &&
        bertrand::signature<F>::no_duplicate_args;

}


///////////////////
////    DEF    ////
///////////////////


namespace impl {
    struct def_tag {};
}


namespace meta {

    template <typename T>
    concept def = inherits<T, impl::def_tag>;

}


/// TODO: def() becomes a function that returns an impl::def object, which takes an
/// explicit signature, possibly encoding the partial arguments at the same time, so
/// that the implementation is just impl::def<sig, F>, which is as simple as it gets.


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename F, typename... Args>
    requires (
        meta::callable<F, Args...> && (
            impl::call_passthrough<F>::enable ||
            (signature<F>::Partial::empty() && signature<F>::Defaults::empty())
        )
    )
constexpr decltype(auto) call(F&& func, Args&&... args) {
    if constexpr (impl::call_passthrough<F>::enable) {
        return std::forward<F>(func)(std::forward<Args>(args)...);
    } else {
        return typename signature<F>::template Bind<Args...>{}(
            typename signature<F>::Partial{},
            typename signature<F>::Defaults{},
            std::forward<F>(func),
            std::forward<Args>(args)...
        );
    }
}


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename F, typename... Args>
    requires (meta::callable<F, Args...> && signature<F>::Partial::empty())
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
    requires (meta::callable<F, Args...> && signature<F>::Partial::empty())
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
        signature<Func>::enable &&
        signature<Func>::Partial::empty() &&
        meta::partially_callable<Func, Args...>
    )
struct def : impl::def_tag {
private:
    template <typename, typename, size_t>
    struct bind_type;
    template <typename... out, typename sig, size_t I>
    struct bind_type<args<out...>, sig, I> { using type = def<Func, out...>; };
    template <typename... out, typename sig, size_t I> requires (I < sig::size())
    struct bind_type<args<out...>, sig, I> {
        template <typename T>
        struct filter { using type = args<out...>; };
        template <typename T> requires (meta::arg_traits<T>::bound())
        struct filter<T> {
            template <typename>
            struct extend;
            template <typename... bound>
            struct extend<args<bound...>> { using type = args<out..., bound...>; };
            using type = extend<typename meta::arg_traits<T>::bound_to>::type;
        };
        using type = bind_type<
            typename filter<typename sig::template at<I>>::type,
            sig,
            I + 1
        >::type;
    };

public:
    using Function = std::remove_cvref_t<Func>;
    using Partial = decltype(
        std::declval<typename signature<Function>::Partial>().bind(std::declval<Args>()...)
    );
    using Signature = Partial::type;
    using Defaults = Signature::Defaults;

    Defaults defaults;
    Function func;
    Partial partial;

    /* Allows access to the template constraints and underlying implementation for the
    call operator. */
    template <typename... A>
    using Bind = Signature::template Bind<A...>;

    template <meta::is<Func> F> requires (signature<F>::Defaults::empty())
    constexpr def(F&& func, Args... args) :
        defaults(),
        func(std::forward<F>(func)),
        partial(std::forward<Args>(args)...)
    {}

    template <meta::is<Func> F>
    constexpr def(const Defaults& defaults, F&& func, Args... args) :
        defaults(defaults),
        func(std::forward<F>(func)),
        partial(std::forward<Args>(args)...)
    {}

    template <meta::is<Func> F>
    constexpr def(Defaults&& defaults, F&& func, Args... args) :
        defaults(std::move(defaults)),
        func(std::forward<F>(func)),
        partial(std::forward<Args>(args)...)
    {}

    template <meta::convertible_to<Defaults> D, meta::is<Func> F, meta::convertible_to<Partial> P>
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
    template <size_t I, typename Self> requires (I < Partial::size())
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) {
        return std::forward<Self>(self).partial.template get<I>();
    }

    /* Get the partial value of the named argument if it is present. */
    template <static_str name, typename Self> requires (Partial::template contains<name>())
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) {
        return std::forward<Self>(self).partial.template get<name>();
    }

    /* Invoke the function, applying the semantics of the inferred signature. */
    template <typename Self, typename... A>
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
    constexpr decltype(auto) operator()(this Self&& self, A&&... args) {
        return Bind<A...>{}(
            std::forward<Self>(self).partial,
            std::forward<Self>(self).defaults,
            std::forward<Self>(self).func,
            std::forward<A>(args)...
        );
    }

    /* Generate a partial function with the given arguments filled in.  The method can
    be chained - any existing partial arguments will be carried over to the result, and
    will not be considered when binding the new arguments. */
    template <typename Self, typename... A>
        requires (
            !(meta::arg_traits<A>::variadic() || ...) &&
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::can_convert
        )
    [[nodiscard]] constexpr auto bind(this Self&& self, A&&... args) {
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
            std::make_index_sequence<sig::Partial::size()>{},
            std::forward<Self>(self).defaults,
            std::forward<Self>(self).func,
            std::forward<Self>(self).partial.bind(std::forward<A>(args)...)
        );
    }

    /* Clear any partial arguments that have been accumulated thus far, returning a new
    function object without them. */
    template <typename Self>
    [[nodiscard]] constexpr def<Func> unbind(this Self&& self) {
        return def<Func>(std::forward<Self>(self).defaults, std::forward<Self>(self).func);
    }
};


template <typename F, typename... A>
    requires (
        signature<F>::Defaults::empty() &&
        signature<F>::Partial::empty() &&
        meta::partially_callable<F, A...>
    )
def(F, A&&...) -> def<F, A...>;


template <typename F, typename... A>
    requires (
        signature<F>::Partial::empty() &&
        meta::partially_callable<F, A...>
    )
def(typename signature<F>::Defaults&&, F, A&&...) -> def<F, A...>;


template <typename F, typename... A>
    requires (
        signature<F>::Partial::empty() &&
        meta::partially_callable<F, A...>
    )
def(const typename signature<F>::Defaults&, F, A&&...) -> def<F, A...>;


template <typename L, typename R>
    requires (
        (meta::inherits<L, impl::def_tag> || meta::inherits<R, impl::def_tag>) &&
        (!meta::inherits<L, impl::chain_tag> && !meta::inherits<R, impl::chain_tag>) &&
        signature<L>::enable &&
        std::invocable<R, typename signature<L>::Return>
    )
[[nodiscard]] constexpr auto operator>>(L&& lhs, R&& rhs) {
    return chain(std::forward<L>(lhs)) >> std::forward<R>(rhs);
}


/* A helper for defining introspectable wrappers around generic lambdas and other
functions whose signatures cannot be trivially inferred at compile time.  This function
allows users to manually specify a valid function signature, with the result being
introspectable by both `betrand::signature<>` and `bertrand::def<>` in equal measure.
For example:

    constexpr auto func = bertrand::make_def<int(Arg<"x", int>, Arg<"y", int>)>(
        [](auto&& x, auto&& y) {
            return *x + *y;
        }
    );
    static_assert(func(arg<"y"> = 2, arg<"x"> = 1) == 3);

This is unnecessary if the function signature can be inferred using CTAD, in which
case the user can directly instantiate a `def` object to reduce code duplication:

    constexpr bertrand::def func([](Arg<"x", int> x, Arg<"y", int> y) {
        return *x + *y;
    });

As a result, this helper should only be used when the function signature cannot be
inferred, or if the user wishes to manually specify a signature that is different from
that of the underlying function (assuming that it can still be called with the provided
arguments). */
template <meta::normalized_signature Sig, typename F, typename... Args>
    requires (
        signature<Sig>::enable &&
        !signature<Sig>::has_kwonly &&
        !signature<Sig>::has_kwargs &&
        signature<Sig>::Defaults::empty() &&
        signature<Sig>::Partial::empty() &&
        meta::partially_callable<Sig, Args...>
    )
constexpr auto make_def(F&& func, Args&&... args) {
    return def(
        impl::make_def<Sig, F>{std::forward<F>(func)},
        std::forward<Args>(args)...
    );
}


/* A helper for defining introspectable wrappers around generic lambdas and other
functions whose signatures cannot be trivially inferred at compile time.  This function
allows users to manually specify a valid function signature, with the result being
introspectable by both `betrand::signature<>` and `bertrand::def<>` in equal measure.
For example:

    constexpr auto func = bertrand::make_def<int(Arg<"x", int>, Arg<"y", int>)>(
        [](auto&& x, auto&& y) {
            return *x + *y;
        }
    );
    static_assert(func(arg<"y"> = 2, arg<"x"> = 1) == 3);

This is unnecessary if the function signature can be inferred using CTAD, in which
case the user can directly instantiate a `def` object to reduce code duplication:

    constexpr bertrand::def func([](Arg<"x", int> x, Arg<"y", int> y) {
        return *x + *y;
    });

As a result, this helper should only be used when the function signature cannot be
inferred, or if the user wishes to manually specify a signature that is different from
that of the underlying function (assuming that it can still be called with the provided
arguments). */
template <meta::normalized_signature Sig, typename F, typename... Args>
    requires (
        signature<Sig>::enable &&
        !signature<Sig>::has_kwonly &&
        !signature<Sig>::has_kwargs &&
        signature<Sig>::Partial::empty() &&
        meta::partially_callable<Sig, Args...>
    )
constexpr auto make_def(
    const typename signature<Sig>::Defaults& defaults,
    F&& func,
    Args&&... args
) {
    return def(
        defaults,
        impl::make_def<Sig, F>{std::forward<F>(func)},
        std::forward<Args>(args)...
    );
}


/* A helper for defining introspectable wrappers around generic lambdas and other
functions whose signatures cannot be trivially inferred at compile time.  This function
allows users to manually specify a valid function signature, with the result being
introspectable by both `betrand::signature<>` and `bertrand::def<>` in equal measure.
For example:

    constexpr auto func = bertrand::make_def<int(Arg<"x", int>, Arg<"y", int>)>(
        [](auto&& x, auto&& y) {
            return *x + *y;
        }
    );
    static_assert(func(arg<"y"> = 2, arg<"x"> = 1) == 3);

This is unnecessary if the function signature can be inferred using CTAD, in which
case the user can directly instantiate a `def` object to reduce code duplication:

    constexpr bertrand::def func([](Arg<"x", int> x, Arg<"y", int> y) {
        return *x + *y;
    });

As a result, this helper should only be used when the function signature cannot be
inferred, or if the user wishes to manually specify a signature that is different from
that of the underlying function (assuming that it can still be called with the provided
arguments). */
template <meta::normalized_signature Sig, typename F, typename... Args>
    requires (
        signature<Sig>::enable &&
        !signature<Sig>::has_kwonly &&
        !signature<Sig>::has_kwargs &&
        signature<Sig>::Partial::empty() &&
        meta::partially_callable<Sig, Args...>
    )
constexpr auto make_def(
    typename signature<Sig>::Defaults&& defaults,
    F&& func,
    Args&&... args
) {
    return def(
        defaults,
        impl::make_def<Sig, F>{std::forward<F>(func)},
        std::forward<Args>(args)...
    );
}


/* Make the inner template type for a `make_def()` statement introspectable by
`bertrand::signature<>`. */
template <meta::make_def T>
struct signature<T> : meta::make_def_signature<T> {};


}  // namespace bertrand


/* Specializing `std::tuple_size`, `std::tuple_element`, and `std::get` allows
saved arguments, function chains, `def` objects, and signature tuples to be decomposed
using structured bindings. */
namespace std {

    template <bertrand::meta::args T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    template <bertrand::meta::chain T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    template <bertrand::meta::def T>
    struct tuple_size<T> :
        std::integral_constant<size_t, bertrand::signature<T>::size()>
    {};

    template <bertrand::meta::signature T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    template <bertrand::meta::signature_defaults T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    template <bertrand::meta::signature_partial T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    template <size_t I, bertrand::meta::args T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = std::remove_cvref_t<T>::template at<I>;
    };

    template <size_t I, bertrand::meta::chain T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::meta::def T>
        requires (I < bertrand::signature<T>::size())
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::meta::signature T>
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::meta::signature_defaults T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::meta::signature_partial T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, bertrand::meta::args T>
        requires (!bertrand::meta::lvalue<T> && I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr decltype(auto) get(T&& args) noexcept {
        return std::forward<T>(args).template get<I>();
    }

    template <size_t I, bertrand::meta::chain T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr decltype(auto) get(T&& chain) {
        return std::forward<T>(chain).template get<I>();
    }

    template <size_t I, bertrand::meta::def T>
        requires (I < bertrand::signature<T>::size())
    [[nodiscard]] constexpr decltype(auto) get(T&& def) noexcept {
        return std::forward<T>(def).template get<I>();
    }

    template <size_t I, bertrand::meta::signature T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr decltype(auto) get(T&& signature) noexcept {
        return std::forward<T>(signature).template get<I>();
    }

    template <size_t I, bertrand::meta::signature_defaults T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr decltype(auto) get(T&& defaults) noexcept {
        return std::forward<T>(defaults).template get<I>();
    }

    template <size_t I, bertrand::meta::signature_partial T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr decltype(auto) get(T&& partial) noexcept {
        return std::forward<T>(partial).template get<I>();
    }

    template <bertrand::static_str name, bertrand::meta::def T>
        requires (bertrand::signature<T>::template contains<name>())
    [[nodiscard]] constexpr decltype(auto) get(T&& def) noexcept {
        return std::forward<T>(def).template get<name>();
    }

    template <bertrand::static_str name, bertrand::meta::signature T>
        requires (std::remove_cvref_t<T>::template contains<name>())
    [[nodiscard]] constexpr decltype(auto) get(T&& signature) noexcept {
        return std::forward<T>(signature).template get<name>();
    }

    template <bertrand::static_str name, bertrand::meta::signature_defaults T>
        requires (std::remove_cvref_t<T>::template contains<name>())
    [[nodiscard]] constexpr decltype(auto) get(T&& defaults) noexcept {
        return std::forward<T>(defaults).template get<name>();
    }

    template <bertrand::static_str name, bertrand::meta::signature_partial T>
        requires (std::remove_cvref_t<T>::template contains<name>())
    [[nodiscard]] constexpr decltype(auto) get(T&& partial) noexcept {
        return std::forward<T>(partial).template get<name>();
    }

}


/* The dereference operator can be used to emulate Python container unpacking when
calling a Python-style function from C++.

A single unpacking operator passes the contents of an iterable container as positional
arguments to a function.  Unlike Python, only one such operator is allowed per call,
and it must be the last positional argument in the parameter list.  This allows the
compiler to ensure that the container's value type is minimally convertible to each of
the remaining positional arguments ahead of time, even though the number of arguments
cannot be determined until runtime.  Thus, if any arguments are missing or extras are
provided, the call will raise an exception similar to Python, rather than failing
statically at compile time.  This can be avoided by using standard positional and
keyword arguments instead, which can be fully verified at compile time, or by including
variadic positional arguments in the function signature, which will consume any
remaining arguments according to Python semantics.

A second unpacking operator promotes the arguments into keywords, and can only be used
if the container is mapping-like, meaning it possess both `::key_type` and
`::mapped_type` aliases, and that indexing it with an instance of the key type returns
a value of the mapped type.  The actual unpacking is robust, and will attempt to use
iterators over the container to produce key-value pairs, either directly through
`begin()` and `end()` or by calling the `.items()` method if present, followed by
zipping `.keys()` and `.values()` if both exist, and finally by iterating over the keys
and indexing into the container.  Similar to the positional unpacking operator, only
one of these may be present as the last keyword argument in the parameter list, and a
compile-time check is made to ensure that the mapped type is convertible to any missing
keyword arguments that are not explicitly provided at the call site.

In both cases, the extra runtime complexity results in a small performance degradation
over a typical function call, which is minimized as much as possible. */
template <bertrand::meta::iterable T> requires (bertrand::meta::unpack_operator<T>)
[[nodiscard]] constexpr auto operator*(T&& value) {
    return bertrand::arg_pack{std::forward<T>(value)};
}


/* Apply a C++ range adaptor to a container via the comprehension operator.  This is
similar to the C++-style `|` operator for chaining range adaptors, but uses the `->*`
operator to avoid conflicts with other operator overloads and apply higher precedence
than typical binary operators. */
template <typename T, typename V>
    requires (
        bertrand::meta::comprehension_operator<T> &&
        bertrand::meta::viewable<T, V>
    )
[[nodiscard]] constexpr auto operator->*(T&& value, V&& view) {
    return std::views::all(std::forward<T>(value)) | std::forward<V>(view);
}


/* Generate a C++ range adaptor that approximates a Python-style list comprehension.
This is done by piping a function in place of a C++ range adaptor, which will be
applied to each element in the sequence.  The function must be callable with the
container's value type, and may return any type.

If the function returns another range adaptor, then the adaptor's output will be
flattened into the parent range, similar to a nested `for` loop within a Python
comprehension.  Returning a range with no elements will effectively filter out the
current element, similar to a Python `if` clause within a comprehension.

Here's an example:

    std::vector vec = {1, 2, 3, 4, 5};
    std::vector new_vec = vec->*[](int x) {
        return std::views::repeat(x, x % 2 ? 0 : x);
    };
    for (int x : new_vec) {
        std::cout << x << ", ";  // 2, 2, 4, 4, 4, 4,
    }

This is functionally equivalent to `std::views::transform()` in C++ and uses that
implementation under the hood.  The only difference is the added logic for flattening
nested ranges, which is extremely lightweight. */
template <typename T, typename F>
    requires (
        bertrand::meta::comprehension_operator<T> &&
        bertrand::meta::transformable<T, F>
    )
[[nodiscard]] constexpr auto operator->*(T&& value, F&& func) {
    return bertrand::comprehension{std::forward<T>(value), std::forward<F>(func)};
}


namespace bertrand {

    template <typename T>
    constexpr bool meta::detail::enable_unpack_operator<std::vector<T>> = true;
    template <typename T>
    constexpr bool meta::detail::enable_comprehension_operator<std::vector<T>> = true;

    inline void test() {
        constexpr def sub(
            { arg<"x"> = 10, arg<"y"> = 2 },
            [](Arg<"x", int>::opt x, Arg<"y", int>::opt y) {
                return *x - *y;
            }
        );
        constexpr auto div = make_def<int(Arg<"x", int>, Arg<"y", int>::opt)>(
            { arg<"y"> = 2 },
            [](auto&& x, auto&& y) {
                return x / y;
            }
        );
        static_assert(sub(10, 2) == 8);

        // constexpr auto chain = sub >> div.bind(arg<"y"> = 2) >> div;
        constexpr auto chain = sub >> div.bind(arg<"y"> = 2) >> [](auto&& x) {
            return std::forward<decltype(x)>(x);
        };
        static_assert(chain(10, 2) == 4);
        static_assert(chain.template get<1>().defaults.size() == 1);

        std::vector vec = {1, 2, 3};
        std::vector<int> new_vec = vec->*[](int x) { return x * 2; };
        auto view = vec->*std::views::transform([](int x) { return x * 2; });
        auto result = sub(*vec);
        for (int x : vec->*[](int x) { return x * 2; }) {
            std::cout << x << std::endl;
        }
    }

}


#endif  // BERTRAND_FUNC_H
