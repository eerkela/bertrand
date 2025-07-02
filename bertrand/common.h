#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <variant>

#if __has_include(<stdfloat>)
    #include <stdfloat>
#endif


#ifdef _WIN32
    #include <windows.h>
    #include <errhandlingapi.h>
    #include <intrin.h>
#elifdef __unix__
    #include <sys/mman.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <sys/sysctl.h>
    #endif
#endif


// required for demangling
#if defined(__GNUC__) || defined(__clang__)
    #include <cxxabi.h>
    #include <cstdlib>
#elif defined(_MSC_VER)
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib, "dbghelp.lib")
#endif


namespace bertrand {


#ifdef _WIN32
    constexpr bool WINDOWS = true;
    constexpr bool UNIX = false;
    constexpr bool APPLE = false;
#elifdef __unix__
    constexpr bool WINDOWS = false;
    constexpr bool UNIX = true;
    #ifdef __APPLE__
        constexpr bool APPLE = true;
    #else
        constexpr bool APPLE = false;
    #endif
#else
    constexpr bool WINDOWS = false;
    constexpr bool UNIX = false;
    constexpr bool APPLE = false;
#endif


#ifdef _MSVC_LANG
    #define CXXSTD _MSVC_LANG
#else
    #define CXXSTD __cplusplus
#endif


#ifdef BERTRAND_DEBUG
    constexpr bool DEBUG = true;
#else
    constexpr bool DEBUG = false;
#endif


#ifdef BERTRAND_TEMPLATE_RECURSION_LIMIT
    constexpr size_t TEMPLATE_RECURSION_LIMIT = BERTRAND_TEMPLATE_RECURSION_LIMIT;
#else
    constexpr size_t TEMPLATE_RECURSION_LIMIT = 8192;
#endif


static_assert(
    TEMPLATE_RECURSION_LIMIT > 0,
    "Template recursion limit must be positive."
);


namespace impl {
    struct wrapper_tag {};

    /* Check to see if applying Python-style wraparound to a compile-time index would
    yield a valid index into a container of a given size.  Returns false if the
    index would be out of bounds after normalizing. */
    template <ssize_t size, ssize_t I>
    concept valid_index = ((I + size * (I < 0)) >= 0) && ((I + size * (I < 0)) < size);

    /* Apply Python-style wraparound to a compile-time index. Fails to compile if the
    index would be out of bounds after normalizing. */
    template <ssize_t size, ssize_t I> requires (valid_index<size, I>)
    constexpr ssize_t normalize_index() noexcept { return I + size * (I < 0); }

}


struct NoneType;


namespace meta {

    /// NOTE: many of these concepts delegate immediately to a constexpr bool flag
    /// behind the detail:: namespace.  This is intentional, in order to allow for
    /// possible specialization and to limit the depth of template error messages as
    /// much as possible.  This is only done where it would not destroy useful context,
    /// in order to reduce visual noise during debugging.

    /////////////////////////////
    ////    QUALIFICATION    ////
    /////////////////////////////

    template <typename T>
    using forward = decltype(::std::forward<T>(::std::declval<T>()));

    template <typename T>
    using unqualify = ::std::remove_cvref_t<T>;

    template <typename T>
    using remove_reference = ::std::remove_reference_t<T>;

    namespace detail {

        template <typename L, typename R>
        constexpr bool is = ::std::same_as<unqualify<L>, unqualify<R>>;

        /// NOTE: this forces `std::derived_from` to not be instantiated if is<L, R>
        /// is true, which prevents issues when inherits<> is used within an incomplete
        /// class, such as a mixin.
        template <typename L, typename R>
        constexpr bool inherits = ::std::derived_from<unqualify<L>, unqualify<R>>;
        template <typename L, typename R> requires (is<L, R>)
        constexpr bool inherits<L, R> = true;

    }

    template <typename L, typename R>
    concept is = detail::is<L, R>;

    template <typename L, typename R>
    concept inherits = detail::inherits<L, R>;

    namespace detail {

        template <typename T>
        constexpr bool lvalue = ::std::is_lvalue_reference_v<T>;

        template <typename T>
        constexpr bool rvalue = ::std::is_rvalue_reference_v<T>;

        template <typename T>
        constexpr bool pointer = ::std::is_pointer_v<T>;

    }

    template <typename T>
    concept lvalue = detail::lvalue<T>;

    template <typename T>
    using as_lvalue = ::std::add_lvalue_reference_t<T>;

    template <typename T>
    using remove_lvalue = ::std::conditional_t<lvalue<T>, remove_reference<T>, T>;

    template <typename T>
    concept rvalue = detail::rvalue<T>;

    template <typename T>
    using as_rvalue = ::std::add_rvalue_reference_t<T>;

    template <typename T>
    using remove_rvalue = ::std::conditional_t<rvalue<T>, remove_reference<T>, T>;

    template <typename T>
    concept reference = lvalue<T> || rvalue<T>;

    template <typename T>
    concept pointer = detail::pointer<T>;

    template <typename T>
    using as_pointer = ::std::add_pointer_t<T>;

    template <typename T>
    using remove_pointer = ::std::remove_pointer_t<T>;

    namespace detail {

        template <typename T>
        constexpr bool is_const = ::std::is_const_v<remove_reference<T>>;

        template <typename T>
        constexpr bool is_volatile = ::std::is_volatile_v<remove_reference<T>>;

    }

    template <typename T>
    concept is_const = detail::is_const<T>;

    template <typename T>
    concept not_const = !detail::is_const<T>;

    template <typename T>
    using as_const = ::std::conditional_t<
        lvalue<T>,
        as_lvalue<::std::add_const_t<remove_reference<T>>>,
        ::std::conditional_t<
            rvalue<T>,
            as_rvalue<::std::add_const_t<remove_reference<T>>>,
            ::std::add_const_t<T>
        >
    >;

    template <typename T>
    using remove_const = ::std::conditional_t<
        is_const<T>,
        ::std::conditional_t<
            lvalue<T>,
            as_lvalue<::std::remove_const_t<remove_reference<T>>>,
            ::std::conditional_t<
                rvalue<T>,
                as_rvalue<::std::remove_const_t<remove_reference<T>>>,
                ::std::remove_const_t<remove_reference<T>>
            >
        >,
        T
    >;

    template <typename T>
    using as_const_ref = as_const<as_lvalue<T>>;

    template <typename T>
    concept is_volatile = detail::is_volatile<T>;

    template <typename T>
    concept not_volatile = !detail::is_volatile<T>;

    template <typename T>
    using as_volatile = ::std::conditional_t<
        lvalue<T>,
        as_lvalue<::std::add_volatile_t<remove_reference<T>>>,
        ::std::conditional_t<
            rvalue<T>,
            as_rvalue<::std::add_volatile_t<remove_reference<T>>>,
            ::std::add_volatile_t<remove_reference<T>>
        >
    >;

    template <typename T>
    using remove_volatile = ::std::conditional_t<
        is_volatile<T>,
        ::std::conditional_t<
            lvalue<T>,
            as_lvalue<::std::remove_volatile_t<remove_reference<T>>>,
            ::std::conditional_t<
                rvalue<T>,
                as_rvalue<::std::remove_volatile_t<remove_reference<T>>>,
                ::std::remove_volatile_t<remove_reference<T>>
            >
        >,
        T
    >;

    namespace detail {
        template <typename T>
        constexpr bool is_void = ::std::is_void_v<T>;

        template <typename T>
        constexpr bool None = meta::inherits<T, bertrand::NoneType>;
    }

    template <typename T>
    concept is_void = detail::is_void<unqualify<T>>;

    template <typename T>
    concept not_void = !detail::is_void<T>;

    template <typename T>
    concept None = detail::None<unqualify<T>>;

    namespace detail {
        template <typename L, typename R>
        struct qualify { using type = L; };
        template <typename L, typename R>
        struct qualify<L, const R> { using type = const L; };
        template <typename L, typename R>
        struct qualify<L, volatile R> { using type = volatile L; };
        template <typename L, typename R>
        struct qualify<L, const volatile R> { using type = const volatile L; };
        template <typename L, typename R>
        struct qualify<L, R&> { using type = L&; };
        template <typename L, typename R>
        struct qualify<L, const R&> { using type = const L&; };
        template <typename L, typename R>
        struct qualify<L, volatile R&> { using type = volatile L&; };
        template <typename L, typename R>
        struct qualify<L, const volatile R&> { using type = const volatile L&; };
        template <typename L, typename R>
        struct qualify<L, R&&> { using type = L&&; };
        template <typename L, typename R>
        struct qualify<L, const R&&> { using type = const L&&; };
        template <typename L, typename R>
        struct qualify<L, volatile R&&> { using type = volatile L&&; };
        template <typename L, typename R>
        struct qualify<L, const volatile R&&> { using type = const volatile L&&; };
        template <meta::is_void L, typename R>
        struct qualify<L, R> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, const R> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, volatile R> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, const volatile R> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, R&> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, const R&> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, volatile R&> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, const volatile R&> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, R&&> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, const R&&> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, volatile R&&> { using type = L; };
        template <meta::is_void L, typename R>
        struct qualify<L, const volatile R&&> { using type = L; };
    }

    template <typename L, typename R>
    using qualify = detail::qualify<L, R>::type;

    template <typename T>
    concept qualified = is_const<T> || is_volatile<T> || reference<T>;

    template <typename T>
    concept unqualified = !qualified<T>;

    namespace detail {
        template <typename T, typename U>
        constexpr bool more_qualified_than = true;
        template <typename T, typename U>
        constexpr bool more_qualified_than<T, const U> = meta::is_const<T>;
        template <typename T, typename U>
        constexpr bool more_qualified_than<T, volatile U> = meta::is_volatile<T>;
        template <typename T, typename U>
        constexpr bool more_qualified_than<T, const volatile U> =
            meta::is_const<T> && meta::is_volatile<T>;
    }

    template <typename T, typename U>
    concept more_qualified_than =
        !::std::same_as<remove_reference<T>, remove_reference<U>> &&
        detail::more_qualified_than<remove_reference<T>, remove_reference<U>>;

    /////////////////////
    ////    PACKS    ////
    /////////////////////

    template <typename... Ts>
    struct pack;

    namespace detail {

        template <typename T>
        constexpr bool is_pack = false;
        template <typename... Ts>
        constexpr bool is_pack<pack<Ts...>> = true;

        template <typename Search, size_t I, typename... Ts>
        constexpr size_t index_of = 0;
        template <typename Search, size_t I, typename T, typename... Ts>
        constexpr size_t index_of<Search, I, T, Ts...> =
            ::std::same_as<Search, T> ? 0 : index_of<Search, I + 1, Ts...> + 1;

        template <size_t I, typename... Ts>
        struct unpack_type;
        template <size_t I, typename T, typename... Ts>
        struct unpack_type<I, T, Ts...> { using type = unpack_type<I - 1, Ts...>::type; };
        template <typename T, typename... Ts>
        struct unpack_type<0, T, Ts...> { using type = T; };

        template <size_t I, auto V, auto... Vs>
        constexpr auto unpack_value = unpack_value<I - 1, Vs...>;
        template <auto V, auto... Vs>
        constexpr auto unpack_value<0, V, Vs...> = V;

        template <size_t I, typename T, typename... Ts>
        constexpr decltype(auto) unpack_arg(T&& curr, Ts&&... next) noexcept {
            if constexpr (I == 0) {
                return (::std::forward<T>(curr));
            } else {
                return (unpack_arg<I - 1>(::std::forward<Ts>(next)...));
            }
        }

        template <typename out, typename...>
        struct concat { using type = out; };
        template <typename... out, typename... curr, typename... next>
        struct concat<pack<out...>, pack<curr...>, next...> {
            using type = concat<pack<out..., curr...>, next...>::type;
        };

        template <typename out, typename...>
        struct reverse { using type = out; };
        template <typename... out, typename T, typename... Ts>
        struct reverse<pack<out...>, T, Ts...> {
            using type = reverse<pack<T, out...>, Ts...>::type;
        };

        template <size_t N, typename... Ts>
        struct repeat {
            template <typename out, size_t I, typename... Us>
            struct expand { using type = out; };
            template <typename... out, size_t I, typename U, typename... Us>
            struct expand<pack<out...>, I, U, Us...> {
                using type = expand<pack<out..., U>, I - 1, U, Us...>::type;
            };
            template <typename... out, typename U, typename... Us>
            struct expand<pack<out...>, 0, U, Us...> {
                using type = expand<pack<out...>, N, Us...>::type;
            };
            using type = expand<pack<>, N, Ts...>::type;
        };

        template <typename... packs>
        struct product { using type = pack<>; };
        template <typename... first, typename... rest>
        struct product<pack<first...>, rest...> {
            /* permute<> iterates from left to right along the packs. */
            template <typename out, typename...>
            struct permute { using type = out; };
            template <typename... out, typename... curr, typename... next>
            struct permute<pack<out...>, pack<curr...>, next...> {
    
                /* accumulate<> iterates over the prior permutations and updates them
                with the types at this index. */
                template <typename result, typename...>
                struct accumulate { using type = result; };
                template <typename... result, typename P, typename... Ps>
                struct accumulate<pack<result...>, P, Ps...> {

                    /* extend<> iterates over the alternatives for the current pack,
                    appending them to the accumulated output. */
                    template <typename prev, typename...>
                    struct extend { using type = prev; };
                    template <typename... prev, typename U, typename... Us>
                    struct extend<pack<prev...>, U, Us...> {
                        using type = extend<
                            pack<prev..., typename P::template append<U>>,
                            Us...
                        >::type;
                    };

                    using type = accumulate<
                        typename extend<pack<result...>, curr...>::type,
                        Ps...
                    >::type;
                };

                /* accumulate<> has to rebuild the output pack at each iteration. */
                using type = permute<
                    typename accumulate<pack<>, out...>::type,
                    next...
                >::type;
            };

            /* The first pack is converted to a 2D pack to initialize the recursion. */
            using type = permute<pack<pack<first>...>, rest...>::type;
        };

        template <template <typename> class F, typename...>
        constexpr bool boolean_value = true;
        template <template <typename> class F, typename U, typename... Us>
        constexpr bool boolean_value<F, U, Us...> =
            requires{ static_cast<bool>(F<U>::value); } && boolean_value<F, Us...>;

        template <template <typename> class F, typename out, typename...>
        struct filter { using type = out; };
        template <template <typename> class F, typename... out, typename U, typename... Us>
        struct filter<F, pack<out...>, U, Us...> {
            using type = filter<F, pack<out...>, Us...>::type;
        };
        template <template <typename> class F, typename... out, typename U, typename... Us>
            requires (F<U>::value)
        struct filter<F, pack<out...>, U, Us...> {
            using type = filter<F, pack<out..., U>, Us...>::type;
        };

        template <template <typename, typename> class F, typename...>
        constexpr bool left_foldable = false;
        template <template <typename, typename> class F, typename T>
        constexpr bool left_foldable<F, T> = true;
        template <
            template <typename, typename> class F,
            typename L,
            typename R,
            typename... Ts
        >
        constexpr bool left_foldable<F, L, R, Ts...> =
            requires{typename F<L, R>;} && left_foldable<F, F<L, R>, Ts...>;

        template <template <typename, typename> class F, typename P>
        constexpr bool right_foldable = false;
        template <template <typename, typename> class F, typename... Ts>
        constexpr bool right_foldable<F, pack<Ts...>> = left_foldable<F, Ts...>;

        template <typename...>
        struct fold {
            template <template <typename, typename> class F>
            using type = void;
        };
        template <typename out, typename... next>
        struct fold<out, next...> {
            template <template <typename, typename> class F>
            using type = out;
        };
        template <typename out, typename curr, typename... next>
        struct fold<out, curr, next...> {
            template <template <typename, typename> class F>
            using type = fold<F<out, curr>, next...>::template type<F>;
        };

        template <typename...>
        constexpr bool unique = true;
        template <typename T, typename... Ts>
        constexpr bool unique<T, Ts...> =
            (!::std::same_as<T, Ts> && ... && unique<Ts...>);

        template <typename out, typename...>
        struct to_unique { using type = out; };
        template <typename... out, typename T, typename... Ts>
        struct to_unique<pack<out...>, T, Ts...> {
            using type = to_unique<pack<out..., T>, Ts...>::type;
        };
        template <typename... out, typename T, typename... Ts>
            requires (::std::same_as<T, out> || ...)
        struct to_unique<pack<out...>, T, Ts...> {
            using type = to_unique<pack<out...>, Ts...>::type;
        };

        template <typename...>
        constexpr bool consolidated = true;
        template <typename T, typename... Ts>
        constexpr bool consolidated<T, Ts...> =
            (!meta::is<T, Ts> && ...) && consolidated<Ts...>;

        template <typename out, typename...>
        struct consolidate { using type = out; };
        template <typename... out, typename U, typename... Us>
        struct consolidate<pack<out...>, U, Us...> {
            using type = consolidate<pack<out...>, Us...>::type;
        };
        template <typename... out, typename U, typename... Us>
            requires ((!meta::is<U, out> && ...) && (!meta::is<U, Us> && ...))
        struct consolidate<pack<out...>, U, Us...> {
            using type = consolidate<pack<out..., U>, Us...>::type;
        };
        template <typename... out, typename U, typename... Us>
        requires ((!meta::is<U, out> && ...) && (meta::is<U, Us> || ...))
        struct consolidate<pack<out...>, U, Us...> {
            using type = consolidate<pack<out..., unqualify<U>>, Us...>::type;
        };

    }

    /* Concept is satisfied only when `T` is an arbitrarily qualified */
    template <typename T>
    concept is_pack = detail::is_pack<unqualify<T>>;

    /* Get the count of a particular type within a parameter pack.  Returns zero if
    the type is not present. */
    template <typename Search, typename... Ts>
    constexpr size_t count_of = (::std::same_as<Search, Ts> + ... + 0);

    /* Get the index of a particular type within a parameter pack.  Returns the pack's
    size if the type is not present. */
    template <typename Search, typename... Ts>
    constexpr size_t index_of = detail::index_of<Search, 0, Ts...>;

    /* Get the type at a particular index of a parameter pack. */
    template <ssize_t I, typename... Ts> requires (impl::valid_index<sizeof...(Ts), I>)
    using unpack_type = detail::unpack_type<
        impl::normalize_index<sizeof...(Ts), I>(),
        Ts...
    >::type;

    /* Unpack the non-type template parameter at a particular index of a parameter
    pack. */
    template <ssize_t I, auto... Vs> requires (impl::valid_index<sizeof...(Vs), I>)
    constexpr auto unpack_value = detail::unpack_value<
        impl::normalize_index<sizeof...(Vs), I>(),
        Vs...
    >;

    /* Index into a parameter pack and perfectly forward a single item. */
    template <ssize_t I, typename... Ts> requires (impl::valid_index<sizeof...(Ts), I>)
    constexpr decltype(auto) unpack_arg(Ts&&... args) noexcept {
        return detail::unpack_arg<impl::normalize_index<sizeof...(Ts), I>()>(
            ::std::forward<Ts>(args)...
        );
    }

    /* Return a pack with the merged contents of all constituent packs. */
    template <is_pack... packs>
    using concat = detail::concat<pack<>, unqualify<packs>...>::type;

    /* Return a pack containing the types in `Ts...`, but in the opposite order. */
    template <typename... Ts>
    using reverse = detail::reverse<pack<>, Ts...>::type;

    /* Return a pack containing `N` consecutive repetitions for each type in
    `Ts...`. */
    template <size_t N, typename... Ts>
    using repeat = detail::repeat<N, Ts...>::type;

    /* Get a pack of packs containing all unique permutations of the component packs,
    returning their Cartesian product.  */
    template <is_pack... packs>
    using product = detail::product<unqualify<packs>...>::type;

    /* Return a pack containing only those types from `Ts...` that satisfy the
    `F<T>::value` condition. */
    template <template <typename> class F, typename... Ts>
        requires (detail::boolean_value<F, Ts...>)
    using filter = detail::filter<F, pack<>, Ts...>::type;

    /* Apply a pairwise template template reduction parameter over the contents of a
    non-empty pack, accumulating results from left to right into a collapsed value. */
    template <template <typename, typename> class F, typename... Ts>
        requires (detail::left_foldable<F, Ts...>)
    using fold_left = detail::fold<Ts...>::template type<F>;

    /* Apply a pairwise template template reduction parameter over the contents of a
    non-empty pack, accumulating results from right to left into a collapsed value. */
    template <template <typename, typename> class F, typename... Ts>
        requires (detail::right_foldable<F, reverse<Ts...>>)
    using fold_right = reverse<Ts...>::template eval<detail::fold>::template type<F>;

    /* Concept that is satisfied iff every `T` occurs exactly once in `Ts...`. */
    template <typename... Ts>
    concept unique = detail::unique<Ts...>;

    /* Filter out any exact duplicates from `Ts...`, returning a `meta::pack<Us...>`
    where `Us...` contain only the unique types. */
    template <typename... Ts>
    using to_unique = detail::to_unique<pack<>, Ts...>::type;

    /* Concept that is satisfied iff every `T` has no other cvref-qualified equivalents
    in `Ts...`. */
    template <typename... Ts>
    concept consolidated = detail::consolidated<Ts...>;

    /* Filter out duplicates and replace any types from `Ts...` that differ only in
    cvref qualifications with an unqualified equivalent.  Returns `meta::pack<Us...>`,
    where `Us...` describe the consolidated types. */
    template <typename... Ts>
    using consolidate = detail::consolidate<pack<>, Ts...>::type;

    /* A generic list of types for use in template metaprogramming.  Provides a simple
    set of higher-order metafunctions to make operating on sequences of types slightly
    easier. */
    template <typename... Ts>
    struct pack {
        using size_type = size_t;
        using index_type = ssize_t;

        /* The total number of arguments being stored, as an unsigned integer. */
        static constexpr size_type size() noexcept { return sizeof...(Ts); }

        /* The total number of arguments being stored, as a signed integer. */
        static constexpr index_type ssize() noexcept { return index_type(size()); }

        /* True if the pack stores no types. */
        static constexpr bool empty() noexcept { return size() == 0; }

        /* Check to see whether a particular type is present within the pack. */
        template <typename T>
        static constexpr bool contains() noexcept {
            return meta::index_of<T, Ts...> < size();
        }

        /* Member equivalent for `meta::count_of<T, Ts...>`. */
        template <typename T>
        static constexpr size_type count() {
            return meta::count_of<T, Ts...>;
        }

        /* Member equivalent for `meta::index_of<T, Ts...>`. */
        template <typename T>
        static constexpr size_t index() noexcept {
            return meta::index_of<T, Ts...>;
        }

        /* Member equivalent for `meta::unpack_type<I, Ts...>` (pack indexing). */
        template <index_type I> requires (impl::valid_index<ssize(), I>)
        using at = meta::unpack_type<I, Ts...>;

        /* Expand the pack, instantiating a template template parameter with its
        contents. */
        template <template <typename...> class F>
            requires (requires{typename F<Ts...>;})
        using eval = F<Ts...>;

        /* Map a template template parameter over the given arguments, returning a new
        pack containing the transformed result */
        template <template <typename> class F> requires (requires{typename pack<F<Ts>...>;})
        using map = pack<F<Ts>...>;

        /* Get a new pack with one or more types appended after the current contents. */
        template <typename... Us>
        using append = pack<Ts..., Us...>;

        /* Member equivalent for `meta::concat<pack, packs...>`. */
        template <typename... packs> requires (requires{typename meta::concat<pack, packs...>;})
        using concat = meta::concat<pack, packs...>;

        /* Member equivalent for `meta::repeat<N, Ts...>`. */
        template <size_type N>
        using repeat = meta::repeat<N, Ts...>;

        /* Member equivalent for `meta::product<pack<Ts...>, Ps...>` */
        template <typename... Ps> requires (requires{typename meta::product<pack, Ps...>;})
        using product = meta::product<pack, Ps...>;

        /* Member equivalent for `meta::filter<F, Ts...>`. */
        template <template <typename> class F>
            requires (requires{typename meta::filter<F, Ts...>;})
        using filter = meta::filter<F, Ts...>;

        /* Member equivalent for `meta::fold_left<F, Ts...>`. */
        template <template <typename, typename> class F>
            requires (requires{typename meta::fold_left<F, Ts...>;})
        using fold_left = meta::fold_left<F, Ts...>;

        /* Member equivalent for `meta::fold_right<F, Ts...>`. */
        template <template <typename, typename> class F>
            requires (requires{typename meta::fold_right<F, Ts...>;})
        using fold_right = meta::fold_right<F, Ts...>;
    };

    //////////////////////////
    ////    PRIMITIVES    ////
    //////////////////////////

    namespace detail {

        template <typename T>
        constexpr bool integer = ::std::integral<unqualify<T>>;
        template <typename T>
        constexpr bool signed_integer = ::std::signed_integral<unqualify<T>>;
        template <typename T>
        constexpr bool unsigned_integer = ::std::unsigned_integral<unqualify<T>>;
        template <typename T>
        constexpr bool boolean = meta::is<unqualify<T>, bool>;
        template <typename T>
        constexpr bool floating = ::std::floating_point<unqualify<T>>;
        template <typename T>
        constexpr bool numeric = integer<T> || floating<T>;

        template <typename T>
        struct as_signed {
            static constexpr bool enable = false;
        };
        template <typename T>
        struct as_unsigned {
            static constexpr bool enable = false;
        };

    }

    template <typename T> requires (detail::as_signed<T>::enable)
    using as_signed = qualify<typename detail::as_signed<unqualify<T>>::type, T>;

    template <typename T> requires (detail::as_signed<T>::enable)
    [[nodiscard]] constexpr auto to_signed(T value)
        noexcept (requires{{static_cast<as_signed<T>>(value)} noexcept;})
        requires (requires{{static_cast<as_signed<T>>(value)};})
    {
        return static_cast<as_signed<T>>(value);
    }

    template <typename T> requires (detail::as_unsigned<T>::enable)
    using as_unsigned = qualify<typename detail::as_unsigned<T>::type, T>;

    template <typename T> requires (detail::as_unsigned<T>::enable)
    [[nodiscard]] constexpr auto to_signed(T value)
        noexcept (requires{{static_cast<as_unsigned<T>>(value)} noexcept;})
        requires (requires{{static_cast<as_unsigned<T>>(value)};})
    {
        return static_cast<as_unsigned<T>>(value);
    }

    template <typename T>
    concept numeric = detail::numeric<T>;

    template <typename T>
    concept integer = numeric<T> && detail::integer<T>;

    template <typename T>
    concept boolean = integer<T> && detail::boolean<T>;

    template <typename T>
    concept signed_integer = integer<T> && detail::signed_integer<T>;

    template <typename T>
    concept unsigned_integer = integer<T> && detail::unsigned_integer<T>;

    template <typename T>
    concept floating = numeric<T> && detail::floating<T>;

    namespace detail {

        template <meta::integer T>
        constexpr size_t integer_size = sizeof(unqualify<T>) * 8;
        template <meta::boolean T>
        constexpr size_t integer_size<T> = 1;

        template <typename T> requires (requires{typename ::std::make_signed_t<T>;})
        struct as_signed<T> {
            static constexpr bool enable = true;
            using type = ::std::make_signed_t<T>;
        };
        template <meta::signed_integer T>
            requires (!requires{typename ::std::make_signed_t<T>;})
        struct as_signed<T> {
            static constexpr bool enable = true;
            using type = T;
        };

    }

    template <integer T>
    constexpr size_t integer_size = detail::integer_size<T>;

    template <typename T>
    concept int8 = signed_integer<T> && integer_size<T> == 8;

    template <typename T>
    concept int16 = signed_integer<T> && integer_size<T> == 16;

    template <typename T>
    concept int32 = signed_integer<T> && integer_size<T> == 32;

    template <typename T>
    concept int64 = signed_integer<T> && integer_size<T> == 64;

    template <typename T>
    concept int128 = signed_integer<T> && integer_size<T> == 128;

    namespace detail {

        template <typename T> requires (requires{typename ::std::make_unsigned_t<T>;})
        struct as_unsigned<T> {
            static constexpr bool enable = true;
            using type = ::std::make_unsigned_t<T>;
        };
        template <meta::unsigned_integer T>
            requires (!requires{typename ::std::make_unsigned_t<T>;})
        struct as_unsigned<T> {
            static constexpr bool enable = true;
            using type = T;
        };

    }

    template <typename T>
    concept uint8 = unsigned_integer<T> && integer_size<T> == 8;

    template <typename T>
    concept uint16 = unsigned_integer<T> && integer_size<T> == 16;

    template <typename T>
    concept uint32 = unsigned_integer<T> && integer_size<T> == 32;

    template <typename T>
    concept uint64 = unsigned_integer<T> && integer_size<T> == 64;

    template <typename T>
    concept uint128 = unsigned_integer<T> && integer_size<T> == 128;

    namespace detail {

        template <meta::floating T>
        constexpr size_t float_mantissa_size =
            ::std::numeric_limits<meta::unqualify<T>>::digits;

        template <meta::floating T>
        constexpr size_t float_exponent_size =
            sizeof(unqualify<T>) * 8 - float_mantissa_size<T>;

        /// NOTE: the default x86 long double is an 80-bit format, but on other
        /// platforms it may be a true 128-bit quadruple-precision float, which makes
        /// it difficult to use portably.
        template <meta::floating T>
            requires (meta::is<T, long double> && float_mantissa_size<T> == 64)
        constexpr size_t float_exponent_size<T> = 15;  // x86 long double

    }

    template <floating T>
    constexpr size_t float_mantissa_size = detail::float_mantissa_size<T>;

    template <floating T>
    constexpr size_t float_exponent_size = detail::float_exponent_size<T>;

    template <floating T>
    constexpr size_t float_exponent_bias =
        size_t(size_t(1) << (float_exponent_size<T> - size_t(1))) - size_t(1);

    template <floating T>
    constexpr size_t float_size = float_exponent_size<T> + float_mantissa_size<T>;

    template <typename T>
    concept float8 = floating<T> && float_size<T> == 8;

    template <typename T>
    concept float16 = floating<T> && float_size<T> == 16;

    template <typename T>
    concept float32 = floating<T> && float_size<T> == 32;

    template <typename T>
    concept float64 = floating<T> && float_size<T> == 64;

    template <typename T>
    concept float80 = floating<T> && float_size<T> == 80;

    template <typename T>
    concept float128 = floating<T> && float_size<T> == 128;

    namespace detail {

        template <typename T>
        constexpr bool string_literal = requires(T t) {
            []<size_t N>(const char(&)[N]){}(t);
        };

        template <typename T>
        constexpr size_t string_literal_size = sizeof(T) - 1;

    }

    template <typename T>
    concept string_literal = detail::string_literal<T>;

    template <string_literal T>
    constexpr size_t string_literal_size = detail::string_literal_size<T>;

    namespace detail {

        template <typename T>
        constexpr bool raw_array = ::std::is_array_v<remove_reference<T>>;

        template <typename T>
        constexpr bool raw_bounded_array = ::std::is_bounded_array_v<remove_reference<T>>;

        template <typename T>
        constexpr bool raw_unbounded_array = ::std::is_unbounded_array_v<remove_reference<T>>;

    }

    template <typename T>
    concept raw_array = detail::raw_array<T>;

    template <typename T>
    concept raw_bounded_array = detail::raw_bounded_array<T>;

    template <typename T>
    concept raw_unbounded_array = detail::raw_unbounded_array<T>;

    namespace detail {

        template <typename T>
        constexpr bool raw_union = ::std::is_union_v<unqualify<T>>;

        template <typename T>
        constexpr bool scoped_enum = ::std::is_scoped_enum_v<unqualify<T>>;

        template <typename T>
        constexpr bool unscoped_enum = ::std::is_enum_v<unqualify<T>> && !scoped_enum<T>;

    }

    template <typename T>
    concept raw_union = detail::raw_union<T>;

    template <typename T>
    concept scoped_enum = detail::scoped_enum<T>;

    template <typename T>
    concept unscoped_enum = detail::unscoped_enum<T>;

    template <typename T>
    concept is_enum = unscoped_enum<T> || scoped_enum<T>;

    template <is_enum T>
    using enum_type = ::std::underlying_type_t<unqualify<T>>;

    namespace detail {

        template <typename T>
        constexpr bool is_empty = ::std::is_empty_v<unqualify<T>>;

        template <typename T>
        constexpr bool is_virtual = ::std::is_polymorphic_v<unqualify<T>>;

        template <typename T>
        constexpr bool is_abstract = ::std::is_abstract_v<unqualify<T>>;

        template <typename T>
        constexpr bool is_final = ::std::is_final_v<unqualify<T>>;

        template <typename T>
        constexpr bool is_aggregate = ::std::is_aggregate_v<unqualify<T>>;

    }

    template <typename T>
    concept is_empty = detail::is_empty<T>;

    template <typename T>
    concept not_empty = !detail::is_empty<T>;

    template <typename T>
    concept is_virtual = detail::is_virtual<T>;

    template <typename T>
    concept not_virtual = !detail::is_virtual<T>;

    template <typename T>
    concept is_abstract = detail::is_abstract<T>;

    template <typename T>
    concept not_abstract = !detail::is_abstract<T>;

    template <typename T>
    concept is_final = detail::is_final<T>;

    template <typename T>
    concept not_final = !detail::is_final<T>;

    template <typename T>
    concept is_aggregate = detail::is_aggregate<T>;

    template <typename T>
    concept not_aggregate = !detail::is_aggregate<T>;

    ////////////////////////////
    ////    CONSTRUCTION    ////
    ////////////////////////////

    namespace detail {

        template <typename T, typename... Args>
        constexpr bool constructible_from = ::std::constructible_from<T, Args...>;

        template <typename T, typename... Args>
        constexpr bool nothrow_constructible_from =
            ::std::is_nothrow_constructible_v<T, Args...>;

        template <typename T>
        constexpr bool default_constructible = ::std::default_initializable<T>;

        template <typename T>
        constexpr bool nothrow_default_constructible =
            ::std::is_nothrow_default_constructible_v<T>;

        template <typename T, typename... Args>
        constexpr bool trivially_constructible =
            ::std::is_trivially_constructible_v<T, Args...>;

    }

    template <typename T, typename... Args>
    concept constructible_from = detail::constructible_from<T, Args...>;

    template <typename T, typename... Args>
    concept implicitly_constructible_from =
        constructible_from<T, Args...> && requires(Args... args) {
            [](Args... args) -> T { return {::std::forward<Args>(args)...}; };
        };

    template <typename T, typename... Args>
    concept trivially_constructible = detail::trivially_constructible<T, Args...>;

    template <typename T>
    concept default_constructible = detail::default_constructible<T>;

    namespace nothrow {

        template <typename T, typename... Args>
        concept constructible_from =
            meta::constructible_from<T, Args...> &&
            detail::nothrow_constructible_from<T, Args...>;

        template <typename T, typename... Args>
        concept implicitly_constructible_from =
            meta::implicitly_constructible_from<T, Args...> &&
            detail::nothrow_constructible_from<T, Args...>;

        template <typename T, typename... Args>
        concept trivially_constructible =
            meta::trivially_constructible<T, Args...> &&
            detail::nothrow_constructible_from<T, Args...>;

        template <typename T>
        concept default_constructible =
            meta::default_constructible<T> &&
            detail::nothrow_default_constructible<T>;

    }

    namespace detail {

        template <typename T>
        constexpr bool prefer_constructor = false;

        template <typename L, typename R>
        constexpr bool convertible_to = ::std::convertible_to<L, R>;

        template <typename L, typename R>
        constexpr bool nothrow_convertible_to =
            ::std::is_nothrow_convertible_v<L, R>;

    }

    template <typename T>
    concept prefer_constructor = detail::prefer_constructor<T>;

    template <typename L, typename R>
    concept convertible_to = detail::convertible_to<L, R>;

    template <typename L, typename R>
    concept explicitly_convertible_to = requires(L from) {
        { static_cast<R>(from) } -> ::std::same_as<R>;
    };

    template <typename L, typename R>
    concept bit_castable_to = requires(L from) {
        { ::std::bit_cast<R>(from) } -> ::std::same_as<R>;
    };

    namespace nothrow {

        template <typename L, typename R>
        concept convertible_to =
            meta::convertible_to<L, R> &&
            detail::nothrow_convertible_to<L, R>;

        template <typename L, typename R>
        concept explicitly_convertible_to =
            meta::explicitly_convertible_to<L, R> &&
            noexcept(static_cast<R>(::std::declval<L>()));

        template <typename L, typename R>
        concept bit_castable_to =
            meta::bit_castable_to<L, R> &&
            noexcept(::std::bit_cast<R>(::std::declval<L>()));

    }

    /* Trigger implicit conversion operators and/or implicit constructors, but not
    explicit ones.  In contrast, static_cast<>() will trigger explicit constructors on
    the target type, which can give unexpected results and violate type safety. */
    template <typename To, typename From>
    constexpr To implicit_cast(From&& value)
        noexcept(nothrow::convertible_to<From, To>)
        requires(convertible_to<From, To>)
    {
        return ::std::forward<From>(value);
    }

    namespace detail {

        template <typename L, typename R>
        constexpr bool assignable = ::std::assignable_from<L, R>;

        template <typename L, typename R>
        constexpr bool nothrow_assignable = ::std::is_nothrow_assignable_v<L, R>;

        template <typename L, typename R>
        constexpr bool trivially_assignable = ::std::is_trivially_assignable_v<L, R>;

    }

    template <typename L, typename R>
    concept assignable = detail::assignable<L, R>;

    template <typename L, typename R>
    concept trivially_assignable = detail::trivially_assignable<L, R>;

    template <typename L, typename R> requires (assignable<L, R>)
    using assign_type = decltype(::std::declval<L>() = ::std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept assign_returns =
        assignable<L, R> && convertible_to<assign_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept assignable =
            meta::assignable<L, R> && detail::nothrow_assignable<L, R>;

        template <typename L, typename R>
        concept trivially_assignable =
            meta::trivially_assignable<L, R> && detail::nothrow_assignable<L, R>;

        template <typename L, typename R> requires (nothrow::assignable<L, R>)
        using assign_type = meta::assign_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept assign_returns =
            meta::assign_returns<L, R, Ret> &&
            nothrow::assignable<L, R> &&
            nothrow::convertible_to<nothrow::assign_type<L, R>, Ret>;

    }

    namespace detail {

        template <typename T>
        constexpr bool copyable = ::std::copy_constructible<T>;

        template <typename T>
        constexpr bool nothrow_copyable = ::std::is_nothrow_copy_constructible_v<T>;

        template <typename T>
        constexpr bool trivially_copyable = ::std::is_trivially_copyable_v<T>;

        template <typename T>
        constexpr bool copy_assignable = ::std::is_copy_assignable_v<T>;

        template <typename T>
        constexpr bool nothrow_copy_assignable = ::std::is_nothrow_copy_assignable_v<T>;

        template <typename T>
        constexpr bool trivially_copy_assignable = ::std::is_trivially_copy_assignable_v<T>;

    }

    template <typename T>
    concept copyable = detail::copyable<T>;

    template <typename T>
    concept trivially_copyable = detail::trivially_copyable<T>;

    template <typename T>
    concept copy_assignable = detail::copy_assignable<T>;

    template <typename T>
    concept trivially_copy_assignable = detail::trivially_copy_assignable<T>;

    template <copy_assignable T>
    using copy_assign_type = decltype(::std::declval<T>() = ::std::declval<T>());

    template <typename Ret, typename T>
    concept copy_assign_returns =
        copy_assignable<T> && convertible_to<copy_assign_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept copyable =
            meta::copyable<T> && detail::nothrow_copyable<T>;

        template <typename T>
        concept trivially_copyable =
            meta::trivially_copyable<T> && detail::nothrow_copyable<T>;

        template <typename T>
        concept copy_assignable =
            meta::copy_assignable<T> && detail::nothrow_copy_assignable<T>;

        template <typename T>
        concept trivially_copy_assignable =
            meta::trivially_copy_assignable<T> && detail::nothrow_copy_assignable<T>;

        template <nothrow::copy_assignable T>
        using copy_assign_type = meta::copy_assign_type<T>;

        template <typename Ret, typename T>
        concept copy_assign_returns =
            meta::copy_assign_returns<T, Ret> &&
            nothrow::copy_assignable<T> &&
            nothrow::convertible_to<nothrow::copy_assign_type<T>, Ret>;

    }

    namespace detail {

        template <typename T>
        constexpr bool movable = ::std::move_constructible<T>;

        template <typename T>
        constexpr bool nothrow_movable = ::std::is_nothrow_move_constructible_v<T>;

        template <typename T>
        constexpr bool trivially_movable = ::std::is_trivially_move_constructible_v<T>;

        template <typename T>
        constexpr bool move_assignable = ::std::is_move_assignable_v<T>;

        template <typename T>
        constexpr bool nothrow_move_assignable = ::std::is_nothrow_move_assignable_v<T>;

        template <typename T>
        constexpr bool trivially_move_assignable = ::std::is_trivially_move_assignable_v<T>;

    }

    template <typename T>
    concept movable = detail::movable<T>;

    template <typename T>
    concept trivially_movable = detail::trivially_movable<T>;

    template <typename T>
    concept move_assignable = detail::move_assignable<T>;

    template <typename T>
    concept trivially_move_assignable = detail::trivially_move_assignable<T>;

    template <move_assignable T>
    using move_assign_type = decltype(::std::declval<T>() = ::std::declval<T>());

    template <typename Ret, typename T>
    concept move_assign_returns =
        move_assignable<T> && convertible_to<move_assign_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept movable =
            meta::movable<T> && detail::nothrow_movable<T>;

        template <typename T>
        concept trivially_movable =
            meta::trivially_movable<T> && detail::nothrow_movable<T>;

        template <typename T>
        concept move_assignable =
            meta::move_assignable<T> &&
            detail::nothrow_move_assignable<T>;

        template <typename T>
        concept trivially_move_assignable =
            meta::trivially_move_assignable<T> &&
            detail::nothrow_move_assignable<T>;

        template <nothrow::move_assignable T>
        using move_assign_type = meta::move_assign_type<T>;

        template <typename Ret, typename T>
        concept move_assign_returns =
            meta::move_assign_returns<T, Ret> &&
            nothrow::move_assignable<T> &&
            nothrow::convertible_to<nothrow::move_assign_type<T>, Ret>;

    }

    template <typename T>
    concept swappable = ::std::swappable<T>;

    template <typename T, typename U>
    concept swappable_with = ::std::swappable_with<T, U>;

    namespace nothrow {

        template <typename T>
        concept swappable = meta::swappable<T> &&
            noexcept(::std::ranges::swap(
                ::std::declval<meta::as_lvalue<T>>(),
                ::std::declval<meta::as_lvalue<T>>()
            ));

        template <typename T, typename U>
        concept swappable_with =
            meta::swappable_with<T, U> &&
            noexcept(::std::ranges::swap(
                ::std::declval<meta::as_lvalue<T>>(),
                ::std::declval<meta::as_lvalue<T>>()
            )) &&
            noexcept(::std::ranges::swap(
                ::std::declval<meta::as_lvalue<T>>(),
                ::std::declval<meta::as_lvalue<U>>()
            )) &&
            noexcept(::std::ranges::swap(
                ::std::declval<meta::as_lvalue<U>>(),
                ::std::declval<meta::as_lvalue<T>>()
            )) &&
            noexcept(::std::ranges::swap(
                ::std::declval<meta::as_lvalue<U>>(),
                ::std::declval<meta::as_lvalue<U>>()
            ));

    }

    namespace detail {

        template <typename T>
        constexpr bool destructible = ::std::destructible<T>;

        template <typename T>
        constexpr bool nothrow_destructible = ::std::is_nothrow_destructible_v<T>;

        template <typename T>
        constexpr bool trivially_destructible = ::std::is_trivially_destructible_v<T>;

        template <typename T>
        constexpr bool virtually_destructible = ::std::has_virtual_destructor_v<T>;

    }

    template <typename T>
    concept destructible = detail::destructible<T>;

    template <typename T>
    concept trivially_destructible =
        destructible<T> && detail::trivially_destructible<T>;

    template <typename T>
    concept virtually_destructible =
        destructible<T> && detail::virtually_destructible<T>;

    namespace nothrow {

        template <typename T>
        concept destructible =
            meta::destructible<T> && detail::nothrow_destructible<T>;

        template <typename T>
        concept trivially_destructible =
            meta::trivially_destructible<T> && nothrow::destructible<T>;

        template <typename T>
        concept virtually_destructible =
            meta::virtually_destructible<T> && nothrow::destructible<T>;

    }

    template <typename... Ts>
    concept has_common_type = (sizeof...(Ts) > 0) && requires {
        typename ::std::common_reference<Ts...>::type;
    };

    template <typename... Ts> requires (has_common_type<Ts...>)
    using common_type = ::std::common_reference<Ts...>::type;

    namespace nothrow {

        template <typename... Ts>
        concept has_common_type = (
            meta::has_common_type<Ts...> &&
            ... &&
            nothrow::convertible_to<Ts, meta::common_type<Ts...>>
        );

        template <typename... Ts> requires (nothrow::has_common_type<Ts...>)
        using common_type = meta::common_type<Ts...>;

    }

    //////////////////////////
    ////    INVOCATION    ////
    //////////////////////////

    namespace detail {

        template <typename T>
        constexpr bool function_signature =
            ::std::is_function_v<remove_pointer<unqualify<T>>>;

        template <typename F, typename... A>
        constexpr bool callable = ::std::invocable<F, A...>;

        template <typename F, typename... A>
        constexpr bool nothrow_callable = ::std::is_nothrow_invocable_v<F, A...>;

        template <typename S>
        struct call_operator { static constexpr bool enable = false; };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...)> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) volatile> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) volatile noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const volatile> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const volatile noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) &> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = true;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) & noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = true;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const &> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = true;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const & noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = true;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) volatile &> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = true;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) volatile & noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = true;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const volatile &> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = true;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const volatile & noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = true;
            static constexpr bool is_rvalue = false;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) &&> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = true;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) && noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = true;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const &&> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = true;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const && noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = false;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = true;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) volatile &&> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = true;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) volatile && noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = false;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = true;
            static constexpr bool is_noexcept = true;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const volatile &&> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = true;
            static constexpr bool is_noexcept = false;
        };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...) const volatile && noexcept> {
            static constexpr bool enable = true;
            using return_type = R;
            using args = meta::pack<A...>;
            using class_type = C;
            static constexpr bool is_const = true;
            static constexpr bool is_volatile = true;
            static constexpr bool is_lvalue = false;
            static constexpr bool is_rvalue = true;
            static constexpr bool is_noexcept = true;
        };

    }

    template <typename T>
    concept function_signature = detail::function_signature<T>;

    template <typename T>
    concept function_pointer = pointer<T> && function_signature<remove_pointer<T>>;

    template <typename T>
    concept has_call_operator =
        requires() { &remove_reference<T>::operator(); } &&
        detail::call_operator<decltype(&remove_reference<T>::operator())>::enable;

    template <has_call_operator T>
    using call_operator = detail::call_operator<decltype(&remove_reference<T>::operator())>;

    template <typename F, typename... A>
    concept callable = detail::callable<F, A...>;

    template <typename F, typename... A> requires (callable<F, A...>)
    using call_type = ::std::invoke_result_t<F, A...>;

    template <typename R, typename F, typename... A>
    concept call_returns = callable<F, A...> && convertible_to<call_type<F, A...>, R>;

    namespace nothrow {

        template <typename F, typename... A>
        concept callable =
            meta::callable<F, A...> && detail::nothrow_callable<F, A...>;

        template <typename F, typename... A> requires (nothrow::callable<F, A...>)
        using call_type = meta::call_type<F, A...>;

        template <typename R, typename F, typename... A>
        concept call_returns =
            nothrow::callable<F, A...> && nothrow::convertible_to<nothrow::call_type<F, A...>, R>;

    }

    ///////////////////////
    ////    MEMBERS    ////
    ///////////////////////

    namespace detail {

        template <typename T>
        constexpr bool member_object =
            ::std::is_member_object_pointer_v<remove_reference<T>>;

        template <typename T, typename C>
        constexpr bool member_object_of = false;
        template <typename T, typename C2, typename C>
        constexpr bool member_object_of<T(C2::*), C> = inherits<C, C2>;
        template <typename T, typename C2, typename C>
        constexpr bool member_object_of<const T(C2::*), C> = inherits<C, C2>;
        template <typename T, typename C2, typename C>
        constexpr bool member_object_of<volatile T(C2::*), C> = inherits<C, C2>;
        template <typename T, typename C2, typename C>
        constexpr bool member_object_of<const volatile T(C2::*), C> = inherits<C, C2>;

    }

    template <typename T>
    concept member_object = detail::member_object<T>;

    template <typename T, typename C>
    concept member_object_of =
        member_object<T> && detail::member_object_of<unqualify<T>, C>;

    namespace detail {

        template <typename T>
        constexpr bool member_function =
            ::std::is_member_function_pointer_v<remove_reference<T>>;

        template <typename T, typename C>
        constexpr bool member_function_of = false;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...), C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) &, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const &, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile &, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile &, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) &&, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const &&, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile &&, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile &&, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) & noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const & noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile & noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile & noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) && noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const && noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile && noexcept, C> = inherits<C, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile && noexcept, C> = inherits<C, C2>;

    }

    template <typename T>
    concept member_function = detail::member_function<T>;

    template <typename T, typename C>
    concept member_function_of =
        member_function<T> && detail::member_function_of<unqualify<T>, C>;

    template <typename T>
    concept member = member_object<T> || member_function<T>;

    template <typename T, typename C>
    concept member_of = member<T> && (member_object_of<T, C> || member_function_of<T, C>);

    namespace detail {

        template <typename T, typename C>
        struct as_member { using type = T(unqualify<C>::*); };
        template <typename T, typename C>
        struct as_member<const T, C> { using type = const T(unqualify<C>::*); };
        template <typename T, typename C>
        struct as_member<volatile T, C> { using type = volatile T(unqualify<C>::*); };
        template <typename T, typename C>
        struct as_member<const volatile T, C> { using type = const volatile T(unqualify<C>::*); };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), C> { using type = R(C::*)(A...); };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), const C> { using type = R(C::*)(A...) const; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), volatile C> { using type = R(C::*)(A...) volatile; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), const volatile C> { using type = R(C::*)(A...) const volatile; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), C&> { using type = R(C::*)(A...) &; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), const C&> { using type = R(C::*)(A...) const &; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), volatile C&> { using type = R(C::*)(A...) volatile &; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), const volatile C&> { using type = R(C::*)(A...) const volatile &; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), C&&> { using type = R(C::*)(A...) &&; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), const C&&> { using type = R(C::*)(A...) const &&; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), volatile C&&> { using type = R(C::*)(A...) volatile &&; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...), const volatile C&&> { using type = R(C::*)(A...) const volatile &&; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, C> { using type = R(C::*)(A...) noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, const C> { using type = R(C::*)(A...) const noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, volatile C> { using type = R(C::*)(A...) volatile noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, const volatile C> { using type = R(C::*)(A...) const volatile noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, C&> { using type = R(C::*)(A...) & noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, const C&> { using type = R(C::*)(A...) const & noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, volatile C&> { using type = R(C::*)(A...) volatile & noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, const volatile C&> { using type = R(C::*)(A...) const volatile & noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, C&&> { using type = R(C::*)(A...) && noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, const C&&> { using type = R(C::*)(A...) const && noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, volatile C&&> { using type = R(C::*)(A...) volatile && noexcept; };
        template <typename R, typename... A, typename C>
        struct as_member<R(A...) noexcept, const volatile C&&> { using type = R(C::*)(A...) const volatile && noexcept; };
        
    }

    template <typename T, typename C>
    using as_member = detail::as_member<
        ::std::conditional_t<function_pointer<T>, remove_pointer<T>, remove_reference<T>>,
        C
    >::type;

    namespace detail {

        template <typename T>
        struct remove_member { using type = T; };
        template <typename T, typename C>
        struct remove_member<T(C::*)> { using type = T; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...)> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) volatile> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const volatile> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) &> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const &> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) volatile &> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const volatile &> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) &&> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const &&> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) volatile &&> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const volatile &&> { using type = R(A...); };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) volatile noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const volatile noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) & noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const & noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) volatile & noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const volatile & noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) && noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const && noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) volatile && noexcept> { using type = R(A...) noexcept; };
        template <typename R, typename C, typename... A>
        struct remove_member<R(C::*)(A...) const volatile && noexcept> { using type = R(A...) noexcept; };

    }

    template <typename T>
    using remove_member = detail::remove_member<T>::type;

    ///////////////////////////////////
    ////    STRUCTURED BINDINGS    ////
    ///////////////////////////////////

    /// TODO: include typed_get concepts, as well as get_if() support, then generalize
    /// tuple_get() for both cases?  This becomes dramatically simpler if/when
    /// universal template parameters become available.

    namespace detail {

        namespace adl {
            using std::get;
            using std::get_if;

            template <typename T, auto... A>
            concept has_get = requires(T t) { get<A...>(t); };

            template <typename T, auto... A>
            concept nothrow_has_get = requires(T t) { {get<A...>(t)} noexcept; };

            template <typename T, auto... A> requires (has_get<T, A...>)
            using get_type = decltype(get<A...>(::std::declval<T>()));

            template <typename T, auto... A>
            concept has_get_if = requires(T t) { get_if<A...>(t); };

            template <typename T, auto... A>
            concept nothrow_has_get_if = requires(T t) { {get_if<A...>(t)} noexcept; };

            template <typename T, auto... A> requires (has_get_if<T, A...>)
            using get_if_type = decltype(get_if<A...>(::std::declval<T>()));

        }

    }

    template <typename T, auto... A>
    concept has_member_get = requires(T t) {
        t.template get<A...>();
    };

    template <typename T, auto... A> requires (has_member_get<T, A...>)
    using member_get_type = decltype(::std::declval<T>().template get<A...>());

    template <typename Ret, typename T, auto... A>
    concept member_get_returns =
        has_member_get<T, A...> && convertible_to<member_get_type<T, A...>, Ret>;

    template <typename T, auto... A>
    concept has_member_get_if = requires(T t) {
        t.template get_if<A...>();
    };

    template <typename T, auto... A> requires (has_member_get_if<T, A...>)
    using member_get_if_type = decltype(::std::declval<T>().template get_if<A...>());

    template <typename Ret, typename T, auto... A>
    concept member_get_if_returns =
        has_member_get_if<T, A...> && convertible_to<member_get_if_type<T, A...>, Ret>;

    template <typename T, auto... A>
    concept has_adl_get = detail::adl::has_get<T, A...>;

    template <typename T, auto... A> requires (has_adl_get<T, A...>)
    using adl_get_type = detail::adl::get_type<T, A...>;

    template <typename Ret, typename T, auto... A>
    concept adl_get_returns =
        has_adl_get<T, A...> && convertible_to<adl_get_type<T, A...>, Ret>;

    template <typename T, auto... A>
    concept has_adl_get_if = detail::adl::has_get_if<T, A...>;

    template <typename T, auto... A> requires (has_adl_get_if<T, A...>)
    using adl_get_if_type = detail::adl::get_if_type<T, A...>;

    template <typename Ret, typename T, auto... A>
    concept adl_get_if_returns =
        has_adl_get_if<T, A...> && convertible_to<adl_get_if_type<T, A...>, Ret>;

    namespace nothrow {

        template <typename T, auto... A>
        concept has_member_get =
            meta::has_member_get<T, A...> &&
            requires(T t) { {t.template get<A...>()} noexcept; };

        template <typename T, auto... A> requires (nothrow::has_member_get<T, A...>)
        using member_get_type = meta::member_get_type<T, A...>;

        template <typename Ret, typename T, auto... A>
        concept member_get_returns =
            nothrow::has_member_get<T, A...> &&
            nothrow::convertible_to<nothrow::member_get_type<T, A...>, Ret>;

        template <typename T, auto... A>
        concept has_member_get_if =
            meta::has_member_get_if<T, A...> &&
            requires(T t) { {t.template get_if<A...>()} noexcept; };

        template <typename T, auto... A> requires (nothrow::has_member_get_if<T, A...>)
        using member_get_if_type = meta::member_get_if_type<T, A...>;

        template <typename Ret, typename T, auto... A>
        concept member_get_if_returns =
            nothrow::has_member_get_if<T, A...> &&
            nothrow::convertible_to<nothrow::member_get_if_type<T, A...>, Ret>;

        template <typename T, auto... A>
        concept has_adl_get =
            meta::has_adl_get<T, A...> && detail::adl::nothrow_has_get<T, A...>;

        template <typename T, auto... A> requires (nothrow::has_adl_get<T, A...>)
        using adl_get_type = meta::adl_get_type<T, A...>;

        template <typename Ret, typename T, auto... A>
        concept adl_get_returns =
            nothrow::has_adl_get<T, A...> &&
            nothrow::convertible_to<nothrow::adl_get_type<T, A...>, Ret>;

        template <typename T, auto... A>
        concept has_adl_get_if =
            meta::has_adl_get_if<T, A...> && detail::adl::nothrow_has_get_if<T, A...>;

        template <typename T, auto... A> requires (nothrow::has_adl_get_if<T, A...>)
        using adl_get_if_type = meta::adl_get_if_type<T, A...>;

        template <typename Ret, typename T, auto... A>
        concept adl_get_if_returns =
            nothrow::has_adl_get_if<T, A...> &&
            nothrow::convertible_to<nothrow::adl_get_if_type<T, A...>, Ret>;

    }

    namespace detail {

        template <typename T, auto... A>
        struct get_type { using type = member_get_type<T, A...>; };
        template <typename T, auto... A>
            requires (!meta::has_member_get<T, A...> && meta::has_adl_get<T, A...>)
        struct get_type<T, A...> { using type = meta::adl_get_type<T, A...>; };

        template <typename T, auto... A>
        struct get_if_type { using type = member_get_if_type<T, A...>; };
        template <typename T, auto... A>
            requires (!meta::has_member_get_if<T, A...> && meta::has_adl_get_if<T, A...>)
        struct get_if_type<T, A...> { using type = meta::adl_get_if_type<T, A...>; };

    }

    template <typename T, auto... A>
    concept has_get = has_member_get<T, A...> || has_adl_get<T, A...>;

    template <typename T, auto... A> requires (has_get<T, A...>)
    using get_type = detail::get_type<T, A...>::type;

    template <typename Ret, typename T, auto... A>
    concept get_returns = has_get<T, A...> && convertible_to<get_type<T, A...>, Ret>;

    template <typename T, auto... A>
    concept has_get_if = has_member_get_if<T, A...> || has_adl_get_if<T, A...>;

    template <typename T, auto... A> requires (has_get_if<T, A...>)
    using get_if_type = typename detail::get_if_type<T, A...>::type;

    template <typename Ret, typename T, auto... A>
    concept get_if_returns =
        has_get_if<T, A...> && convertible_to<get_if_type<T, A...>, Ret>;

    namespace nothrow {

        template <typename T, auto... A>
        concept has_get = meta::has_get<T, A...> && (
            nothrow::has_member_get<T, A...> ||
            nothrow::has_adl_get<T, A...>
        );

        template <typename T, auto... A> requires (nothrow::has_get<T, A...>)
        using get_type = meta::get_type<T, A...>;

        template <typename Ret, typename T, auto... A>
        concept get_returns =
            nothrow::has_get<T, A...> &&
            nothrow::convertible_to<nothrow::get_type<T, A...>, Ret>;

        template <typename T, auto... A>
        concept has_get_if = meta::has_get_if<T, A...> && (
            nothrow::has_member_get_if<T, A...> ||
            nothrow::has_adl_get_if<T, A...>
        );

        template <typename T, auto... A> requires (nothrow::has_get_if<T, A...>)
        using get_if_type = meta::get_if_type<T, A...>;

        template <typename Ret, typename T, auto... A>
        concept get_if_returns =
            nothrow::has_get_if<T, A...> &&
            nothrow::convertible_to<nothrow::get_if_type<T, A...>, Ret>;

    }

    namespace detail {

        template <typename T, size_t... I>
        constexpr bool structured(std::index_sequence<I...>) noexcept {
            return (meta::has_get<T, I> && ...);
        }

    }

    template <typename T, size_t N>
    concept structured =
        ::std::tuple_size<unqualify<T>>::value == N &&
        detail::structured<T>(std::make_index_sequence<N>{});

    template <typename T>
    concept tuple_like = structured<T, ::std::tuple_size<unqualify<T>>::value>;

    template <tuple_like T>
    constexpr size_t tuple_size = ::std::tuple_size<unqualify<T>>::value;

    namespace detail {

        template <size_t I, meta::tuple_like T, typename... Ts>
        struct tuple_types { using type = pack<Ts...>; };
        template <size_t I, meta::tuple_like T, typename... Ts>
            requires (I < meta::tuple_size<T>)
        struct tuple_types<I, T, Ts...> {
            using type = tuple_types<I + 1, T, Ts..., meta::get_type<T, I>>::type;
        };

        template <typename T, typename... Ts>
        struct structured_with {
            template <size_t I, typename... Us>
            static constexpr bool _value = true;

            template <size_t I, typename U, typename... Us>
            static constexpr bool _value<I, U, Us...> = (
                requires(T t) { { get<I>(t) } -> meta::convertible_to<U>; } ||
                requires(T t) { { ::std::get<I>(t) } -> meta::convertible_to<U>; }
            ) && _value<I + 1, Us...>;

            static constexpr bool value = _value<0, Ts...>;
        };

    }

    template <tuple_like T>
    using tuple_types = detail::tuple_types<0, T>::type;

    template <typename T, typename... Ts>
    concept structured_with =
        structured<T, sizeof...(Ts)> && detail::structured_with<T, Ts...>::value;

    /* Do an integer-based `get<I>()` access on a tuple-like type by first checking for
    a `t.get<I>()` member method. */
    template <size_t I, tuple_like T>
    constexpr decltype(auto) tuple_get(T&& t)
        noexcept(nothrow::has_member_get<T, I>)
        requires(has_member_get<T, I>)
    {
        return (::std::forward<T>(t).template get<I>());
    }

    /* If no `t.get<I>()` member method is found, attempt an ADL-enabled `get<I>(t)`
    instead. */
    template <size_t I, tuple_like T>
    constexpr decltype(auto) tuple_get(T&& t)
        noexcept(nothrow::has_adl_get<T, I>)
        requires(!has_member_get<T, I> && has_adl_get<T, I>)
    {
        using ::std::get;
        return (get<I>(::std::forward<T>(t)));
    }

    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    template <typename T>
    concept iterator = ::std::input_or_output_iterator<T>;

    template <typename T, typename V>
    concept output_iterator = iterator<T> && ::std::output_iterator<T, V>;

    template <typename T>
    concept forward_iterator = iterator<T> && ::std::forward_iterator<T>;

    template <typename T>
    concept bidirectional_iterator =
        forward_iterator<T> && ::std::bidirectional_iterator<T>;

    template <typename T>
    concept random_access_iterator =
        bidirectional_iterator<T> && ::std::random_access_iterator<T>;

    template <typename T>
    concept contiguous_iterator =
        random_access_iterator<T> && ::std::contiguous_iterator<T>;

    template <typename T, typename Iter>
    concept sentinel_for = ::std::sentinel_for<T, Iter>;

    template <typename T, typename Iter>
    concept sized_sentinel_for =
        sentinel_for<T, Iter> && ::std::sized_sentinel_for<T, Iter>;

    template <iterator T>
    using iterator_category = std::conditional_t<
        contiguous_iterator<T>,
        ::std::contiguous_iterator_tag,
        typename ::std::iterator_traits<unqualify<T>>::iterator_category
    >;

    template <iterator T>
    using iterator_difference_type = ::std::iterator_traits<unqualify<T>>::difference_type;

    template <iterator T>
    using iterator_value_type = ::std::iterator_traits<unqualify<T>>::value_type;

    template <iterator T>
    using iterator_reference_type = ::std::iterator_traits<unqualify<T>>::reference;

    template <iterator T>
    using iterator_pointer_type = ::std::iterator_traits<unqualify<T>>::pointer;

    template <typename T>
    concept has_begin = requires(T& t) { ::std::ranges::begin(t); };

    template <has_begin T>
    using begin_type = decltype(::std::ranges::begin(::std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_cbegin = requires(T& t) { ::std::ranges::cbegin(t); };

    template <has_cbegin T>
    using cbegin_type = decltype(::std::ranges::cbegin(::std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_begin =
            meta::has_begin<T> &&
            noexcept(::std::ranges::begin(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_begin T>
        using begin_type = meta::begin_type<T>;

        template <typename T>
        concept has_cbegin =
            meta::has_cbegin<T> &&
            noexcept(::std::ranges::cbegin(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_cbegin T>
        using cbegin_type = meta::cbegin_type<T>;

    }

    template <typename T>
    concept has_end = requires(T& t) { ::std::ranges::end(t); };

    template <has_end T>
    using end_type = decltype(::std::ranges::end(::std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_cend = requires(T& t) { ::std::ranges::cend(t); };

    template <has_cend T>
    using cend_type = decltype(::std::ranges::cend(::std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_end =
            meta::has_end<T> &&
            noexcept(::std::ranges::end(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_end T>
        using end_type = meta::end_type<T>;

        template <typename T>
        concept has_cend =
            meta::has_cend<T> &&
            noexcept(::std::ranges::cend(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_cend T>
        using cend_type = meta::cend_type<T>;

    }

    template <typename T>
    concept has_rbegin = requires(T& t) { ::std::ranges::rbegin(t); };

    template <has_rbegin T>
    using rbegin_type = decltype(::std::ranges::rbegin(::std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_crbegin = requires(T& t) { ::std::ranges::crbegin(t); };

    template <has_crbegin T>
    using crbegin_type = decltype(::std::ranges::crbegin(::std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_rbegin =
            meta::has_rbegin<T> &&
            noexcept(::std::ranges::rbegin(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_rbegin T>
        using rbegin_type = meta::rbegin_type<T>;

        template <typename T>
        concept has_crbegin =
            meta::has_crbegin<T> &&
            noexcept(::std::ranges::crbegin(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_crbegin T>
        using crbegin_type = meta::crbegin_type<T>;

    }

    template <typename T>
    concept has_rend = requires(T& t) { ::std::ranges::rend(t); };

    template <has_rend T>
    using rend_type = decltype(::std::ranges::rend(::std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_crend = requires(T& t) { ::std::ranges::crend(t); };

    template <has_crend T>
    using crend_type = decltype(::std::ranges::crend(::std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_rend =
            meta::has_rend<T> &&
            noexcept(::std::ranges::rend(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_rend T>
        using rend_type = meta::rend_type<T>;

        template <typename T>
        concept has_crend =
            meta::has_crend<T> &&
            noexcept(::std::ranges::crend(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_crend T>
        using crend_type = meta::crend_type<T>;

    }

    template <typename T>
    concept iterable = requires(T& t) {
        { ::std::ranges::begin(t) } -> iterator;
        { ::std::ranges::end(t) } -> sentinel_for<decltype(::std::ranges::begin(t))>;
    };

    template <iterable T>
    using yield_type =
        decltype(*::std::ranges::begin(::std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept yields = iterable<T> && convertible_to<yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept iterable =
            meta::iterable<T> && nothrow::has_begin<T> && nothrow::has_end<T>;

        template <nothrow::iterable T>
        using yield_type = meta::yield_type<T>;

        template <typename T, typename Ret>
        concept yields =
            nothrow::iterable<T> &&
            nothrow::convertible_to<nothrow::yield_type<T>, Ret>;

    }

    template <typename T>
    concept const_iterable = requires(T& t) {
        { ::std::ranges::cbegin(t) } -> iterator;
        { ::std::ranges::cend(t) } -> sentinel_for<decltype(::std::ranges::cbegin(t))>;
    };

    template <const_iterable T>
    using const_yield_type =
        decltype(*::std::ranges::cbegin(::std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept const_yields =
        const_iterable<T> && convertible_to<const_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept const_iterable =
            meta::const_iterable<T> && nothrow::has_cbegin<T> && nothrow::has_cend<T>;

        template <nothrow::const_iterable T>
        using const_yield_type = meta::const_yield_type<T>;

        template <typename T, typename Ret>
        concept const_yields =
            nothrow::const_iterable<T> &&
            nothrow::convertible_to<nothrow::const_yield_type<T>, Ret>;

    }

    template <typename T>
    concept reverse_iterable = requires(T& t) {
        { ::std::ranges::rbegin(t) } -> iterator;
        { ::std::ranges::rend(t) } -> sentinel_for<decltype(::std::ranges::rbegin(t))>;
    };

    template <reverse_iterable T>
    using reverse_yield_type =
        decltype(*::std::ranges::rbegin(::std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept reverse_yields =
        reverse_iterable<T> && convertible_to<reverse_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept reverse_iterable =
            meta::reverse_iterable<T> && nothrow::has_rbegin<T> && nothrow::has_rend<T>;

        template <nothrow::reverse_iterable T>
        using reverse_yield_type = meta::reverse_yield_type<T>;

        template <typename T, typename Ret>
        concept reverse_yields =
            nothrow::reverse_iterable<T> &&
            nothrow::convertible_to<nothrow::reverse_yield_type<T>, Ret>;

    }

    template <typename T>
    concept const_reverse_iterable = requires(T& t) {
        { ::std::ranges::crbegin(t) } -> iterator;
        { ::std::ranges::crend(t) } -> sentinel_for<decltype(::std::ranges::crbegin(t))>;
    };

    template <const_reverse_iterable T>
    using const_reverse_yield_type =
        decltype(*::std::ranges::crbegin(::std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept const_reverse_yields =
        const_reverse_iterable<T> && convertible_to<const_reverse_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept const_reverse_iterable =
            meta::const_reverse_iterable<T> && nothrow::has_crbegin<T> && nothrow::has_crend<T>;

        template <nothrow::const_reverse_iterable T>
        using const_reverse_yield_type = meta::const_reverse_yield_type<T>;

        template <typename T, typename Ret>
        concept const_reverse_yields =
            nothrow::const_reverse_iterable<T> &&
            nothrow::convertible_to<nothrow::const_reverse_yield_type<T>, Ret>;

    }

    template <typename T, typename Idx = ssize_t>
    concept has_at = requires (T t, Idx i) {
        { t.at(i) } -> meta::iterator;
    };

    template <typename T, typename Idx = ssize_t> requires (has_at<T, Idx>)
    using at_type = decltype(::std::declval<T>().at(::std::declval<Idx>()));

    template <typename Ret, typename T, typename Idx = ssize_t>
    concept at_returns = has_at<T, Idx> && convertible_to<at_type<T, Idx>, Ret>;

    namespace nothrow {

        template <typename T, typename Idx = ssize_t>
        concept has_at =
            meta::has_at<T, Idx> &&
            noexcept(::std::declval<T>().at(::std::declval<Idx>()));

        template <typename T, typename Idx = ssize_t> requires (nothrow::has_at<T, Idx>)
        using at_type = meta::at_type<T, Idx>;

        template <typename Ret, typename T, typename Idx = ssize_t>
        concept at_returns =
            nothrow::has_at<T, Idx> &&
            nothrow::convertible_to<nothrow::at_type<T, Idx>, Ret>;

    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    template <typename T, typename... Key>
    concept indexable = !integer<T> && requires(T t, Key... key) {
        ::std::forward<T>(t)[::std::forward<Key>(key)...];
    };

    template <typename T, typename... Key> requires (indexable<T, Key...>)
    using index_type = decltype(::std::declval<T>()[::std::declval<Key>()...]);

    template <typename Ret, typename T, typename... Key>
    concept index_returns =
        indexable<T, Key...> && convertible_to<index_type<T, Key...>, Ret>;

    namespace nothrow {

        template <typename T, typename... Key>
        concept indexable =
            meta::indexable<T, Key...> &&
            noexcept(::std::declval<T>()[::std::declval<Key>()...]);

        template <typename T, typename... Key> requires (nothrow::indexable<T, Key...>)
        using index_type = meta::index_type<T, Key...>;

        template <typename Ret, typename T, typename... Key>
        concept index_returns =
            nothrow::indexable<T, Key...> &&
            nothrow::convertible_to<nothrow::index_type<T, Key...>, Ret>;

    }

    template <typename T, typename Value, typename... Key>
    concept index_assignable =
        !integer<T> && requires(T t, Key... key, Value value) {
            { ::std::forward<T>(t)[::std::forward<Key>(key)...] = ::std::forward<Value>(value) };
        };

    template <typename T, typename Value, typename... Key>
        requires (index_assignable<T, Value, Key...>)
    using index_assign_type = decltype(
        ::std::declval<T>()[::std::declval<Key>()...] = ::std::declval<Value>()
    );

    template <typename Ret, typename T, typename Value, typename... Key>
    concept index_assign_returns =
        index_assignable<T, Value, Key...> &&
        convertible_to<index_assign_type<T, Value, Key...>, Ret>;

    namespace nothrow {

        template <typename T, typename Value, typename... Key>
        concept index_assignable =
            meta::index_assignable<T, Value, Key...> &&
            noexcept(::std::declval<T>()[::std::declval<Key>()...] = ::std::declval<Value>());

        template <typename T, typename Value, typename... Key>
            requires (nothrow::index_assignable<T, Value, Key...>)
        using index_assign_type = meta::index_assign_type<T, Value, Key...>;

        template <typename Ret, typename T, typename Value, typename... Key>
        concept index_assign_returns =
            nothrow::index_assignable<T, Value, Key...> &&
            nothrow::convertible_to<nothrow::index_assign_type<T, Value, Key...>, Ret>;

    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    template <typename T>
    concept has_operator_bool = requires(T t) {
        { static_cast<bool>(t) } -> convertible_to<bool>;
    };

    namespace nothrow {

        template <typename T>
        concept has_operator_bool =
            meta::has_operator_bool<T> &&
            noexcept((static_cast<bool>(::std::declval<T>())));

    }

    template <typename T>
    concept has_address = requires(as_lvalue<T> t) {
        { std::addressof(t) } -> pointer;
    };

    template <has_address T>
    using address_type = decltype(::std::addressof(::std::declval<as_lvalue<T>>()));

    template <typename Ret, typename T>
    concept address_returns =
        has_address<T> && convertible_to<address_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_address =
            meta::has_address<T> &&
            noexcept(::std::addressof(::std::declval<as_lvalue<T>>()));

        template <nothrow::has_address T>
        using address_type = meta::address_type<T>;

        template <typename Ret, typename T>
        concept address_returns =
            nothrow::has_address<T> &&
            nothrow::convertible_to<nothrow::address_type<T>, Ret>;

    }

    template <typename T>
    concept has_dereference = requires(T t) { *t; };

    template <has_dereference T>
    using dereference_type = decltype((*::std::declval<T>()));

    template <typename Ret, typename T>
    concept dereference_returns =
        has_dereference<T> && convertible_to<dereference_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_dereference =
            meta::has_dereference<T> &&
            noexcept((*::std::declval<T>()));

        template <nothrow::has_dereference T>
        using dereference_type = meta::dereference_type<T>;

        template <typename Ret, typename T>
        concept dereference_returns =
            nothrow::has_dereference<T> &&
            nothrow::convertible_to<nothrow::dereference_type<T>, Ret>;

    }

    template <typename T>
    concept has_arrow = requires(T t) {
        { ::std::to_address(t) } -> meta::pointer;
    };

    template <has_arrow T>
    using arrow_type = decltype((::std::to_address(::std::declval<T>())));

    template <typename Ret, typename T>
    concept arrow_returns = has_arrow<T> && convertible_to<arrow_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_arrow =
            meta::has_arrow<T> && noexcept(::std::to_address(::std::declval<T>()));

        template <nothrow::has_arrow T>
        using arrow_type = meta::arrow_type<T>;

        template <typename Ret, typename T>
        concept arrow_returns =
            nothrow::has_arrow<T> &&
            nothrow::convertible_to<nothrow::arrow_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_arrow_dereference = requires(L l, R r) { l->*r; };

    template <typename L, typename R> requires (has_arrow_dereference<L, R>)
    using arrow_dereference_type = decltype((::std::declval<L>()->*::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept arrow_dereference_returns =
        has_arrow_dereference<L, R> && convertible_to<arrow_dereference_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_arrow_dereference =
            meta::has_arrow_dereference<L, R> &&
            noexcept(::std::declval<L>()->*::std::declval<R>());

        template <typename L, typename R> requires (nothrow::has_arrow_dereference<L, R>)
        using arrow_dereference_type = meta::arrow_dereference_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept arrow_dereference_returns =
            nothrow::has_arrow_dereference<L, R> &&
            nothrow::convertible_to<nothrow::arrow_dereference_type<L, R>, Ret>;

    }

    template <typename T>
    concept has_bitwise_not = requires(T t) { ~t; };

    template <has_bitwise_not T>
    using bitwise_not_type = decltype((~::std::declval<T>()));

    template <typename Ret, typename T>
    concept bitwise_not_returns =
        has_bitwise_not<T> && convertible_to<bitwise_not_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_bitwise_not =
            meta::has_bitwise_not<T> && noexcept((~::std::declval<T>()));

        template <nothrow::has_bitwise_not T>
        using bitwise_not_type = meta::bitwise_not_type<T>;

        template <typename Ret, typename T>
        concept bitwise_not_returns =
            nothrow::has_bitwise_not<T> &&
            nothrow::convertible_to<nothrow::bitwise_not_type<T>, Ret>;

    }

    template <typename T>
    concept has_pos = requires(T t) { +t; };

    template <has_pos T>
    using pos_type = decltype((+::std::declval<T>()));

    template <typename Ret, typename T>
    concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_pos =
            meta::has_pos<T> && noexcept((+::std::declval<T>()));

        template <nothrow::has_pos T>
        using pos_type = meta::pos_type<T>;

        template <typename Ret, typename T>
        concept pos_returns =
            nothrow::has_pos<T> &&
            nothrow::convertible_to<nothrow::pos_type<T>, Ret>;

    }

    template <typename T>
    concept has_neg = requires(T t) { -t; };

    template <has_neg T>
    using neg_type = decltype((-::std::declval<T>()));

    template <typename Ret, typename T>
    concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_neg =
            meta::has_neg<T> && noexcept((-::std::declval<T>()));

        template <nothrow::has_neg T>
        using neg_type = meta::neg_type<T>;

        template <typename Ret, typename T>
        concept neg_returns =
            nothrow::has_neg<T> && nothrow::convertible_to<nothrow::neg_type<T>, Ret>;

    }

    template <typename T>
    concept has_preincrement = requires(T t) { ++t; };

    template <has_preincrement T>
    using preincrement_type = decltype((++::std::declval<T>()));

    template <typename Ret, typename T>
    concept preincrement_returns =
        has_preincrement<T> && convertible_to<preincrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_preincrement =
            meta::has_preincrement<T> && noexcept((++::std::declval<T>()));

        template <nothrow::has_preincrement T>
        using preincrement_type = meta::preincrement_type<T>;

        template <typename Ret, typename T>
        concept preincrement_returns =
            nothrow::has_preincrement<T> &&
            nothrow::convertible_to<nothrow::preincrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_postincrement = requires(T t) { t++; };

    template <has_postincrement T>
    using postincrement_type = decltype((::std::declval<T>()++));

    template <typename Ret, typename T>
    concept postincrement_returns =
        has_postincrement<T> && convertible_to<postincrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_postincrement =
            meta::has_postincrement<T> && noexcept((::std::declval<T>()++));

        template <nothrow::has_postincrement T>
        using postincrement_type = meta::postincrement_type<T>;

        template <typename Ret, typename T>
        concept postincrement_returns =
            nothrow::has_postincrement<T> &&
            nothrow::convertible_to<nothrow::postincrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_predecrement = requires(T t) { --t; };

    template <has_predecrement T>
    using predecrement_type = decltype((--::std::declval<T>()));

    template <typename Ret, typename T>
    concept predecrement_returns =
        has_predecrement<T> && convertible_to<predecrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_predecrement =
            meta::has_predecrement<T> && noexcept((--::std::declval<T>()));

        template <nothrow::has_predecrement T>
        using predecrement_type = meta::predecrement_type<T>;

        template <typename Ret, typename T>
        concept predecrement_returns =
            nothrow::has_predecrement<T> &&
            nothrow::convertible_to<nothrow::predecrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_postdecrement = requires(T t) { t--; };

    template <has_postdecrement T>
    using postdecrement_type = decltype((::std::declval<T>()--));

    template <typename Ret, typename T>
    concept postdecrement_returns =
        has_postdecrement<T> && convertible_to<postdecrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_postdecrement =
            meta::has_postdecrement<T> && noexcept((::std::declval<T>()--));

        template <nothrow::has_postdecrement T>
        using postdecrement_type = meta::postdecrement_type<T>;

        template <typename Ret, typename T>
        concept postdecrement_returns =
            nothrow::has_postdecrement<T> &&
            nothrow::convertible_to<nothrow::postdecrement_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_lt = requires(L l, R r) { l < r; };

    template <typename L, typename R> requires (has_lt<L, R>)
    using lt_type = decltype((::std::declval<L>() < ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_lt =
            meta::has_lt<L, R> && noexcept((::std::declval<L>() < ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_lt<L, R>)
        using lt_type = meta::lt_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept lt_returns =
            nothrow::has_lt<L, R> &&
            nothrow::convertible_to<nothrow::lt_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_le = requires(L l, R r) { l <= r; };

    template <typename L, typename R> requires (has_le<L, R>)
    using le_type = decltype((::std::declval<L>() <= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_le =
            meta::has_le<L, R> && noexcept((::std::declval<L>() <= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_le<L, R>)
        using le_type = meta::le_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept le_returns =
            nothrow::has_le<L, R> &&
            nothrow::convertible_to<nothrow::le_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_eq = requires(L l, R r) { l == r; };

    template <typename L, typename R> requires (has_eq<L, R>)
    using eq_type = decltype((::std::declval<L>() == ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_eq =
            meta::has_eq<L, R> && noexcept((::std::declval<L>() == ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_eq<L, R>)
        using eq_type = meta::eq_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept eq_returns =
            nothrow::has_eq<L, R> &&
            nothrow::convertible_to<nothrow::eq_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ne = requires(L l, R r) { l != r; };

    template <typename L, typename R> requires (has_ne<L, R>)
    using ne_type = decltype((::std::declval<L>() != ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ne =
            meta::has_ne<L, R> && noexcept((::std::declval<L>() != ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_ne<L, R>)
        using ne_type = meta::ne_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ne_returns =
            nothrow::has_ne<L, R> &&
            nothrow::convertible_to<nothrow::ne_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ge = requires(L l, R r) { l >= r; };

    template <typename L, typename R> requires (has_ge<L, R>)
    using ge_type = decltype((::std::declval<L>() >= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ge =
            meta::has_ge<L, R> && noexcept((::std::declval<L>() >= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_ge<L, R>)
        using ge_type = meta::ge_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ge_returns =
            nothrow::has_ge<L, R> &&
            nothrow::convertible_to<nothrow::ge_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_gt = requires(L l, R r) { l > r; };

    template <typename L, typename R> requires (has_gt<L, R>)
    using gt_type = decltype((::std::declval<L>() > ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_gt =
            meta::has_gt<L, R> && noexcept((::std::declval<L>() > ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_gt<L, R>)
        using gt_type = meta::gt_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept gt_returns =
            nothrow::has_gt<L, R> &&
            nothrow::convertible_to<nothrow::gt_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_spaceship = requires(L l, R r) { l <=> r; };

    template <typename L, typename R> requires (has_spaceship<L, R>)
    using spaceship_type =
        decltype((::std::declval<L>() <=> ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept spaceship_returns =
        has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_spaceship =
            meta::has_spaceship<L, R> &&
            noexcept((::std::declval<L>() <=> ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_spaceship<L, R>)
        using spaceship_type = meta::spaceship_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept spaceship_returns =
            nothrow::has_spaceship<L, R> &&
            nothrow::convertible_to<nothrow::spaceship_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_add = requires(L l, R r) { l + r; };

    template <typename L, typename R> requires (has_add<L, R>)
    using add_type = decltype((::std::declval<L>() + ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_add =
            meta::has_add<L, R> && noexcept((::std::declval<L>() + ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_add<L, R>)
        using add_type = meta::add_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept add_returns =
            nothrow::has_add<L, R> &&
            nothrow::convertible_to<nothrow::add_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_iadd = requires(L& l, R r) { l += r; };

    template <typename L, typename R> requires (has_iadd<L, R>)
    using iadd_type = decltype((::std::declval<L&>() += ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept iadd_returns = has_iadd<L, R> && convertible_to<iadd_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_iadd =
            meta::has_iadd<L, R> && noexcept((::std::declval<L&>() += ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_iadd<L, R>)
        using iadd_type = meta::iadd_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept iadd_returns =
            nothrow::has_iadd<L, R> &&
            nothrow::convertible_to<nothrow::iadd_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_sub = requires(L l, R r) {{ l - r };};

    template <typename L, typename R> requires (has_sub<L, R>)
    using sub_type = decltype((::std::declval<L>() - ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_sub =
            meta::has_sub<L, R> && noexcept((::std::declval<L>() - ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_sub<L, R>)
        using sub_type = meta::sub_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept sub_returns =
            nothrow::has_sub<L, R> &&
            nothrow::convertible_to<nothrow::sub_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_isub = requires(L& l, R r) { l -= r; };

    template <typename L, typename R> requires (has_isub<L, R>)
    using isub_type = decltype((::std::declval<L&>() -= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept isub_returns = has_isub<L, R> && convertible_to<isub_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_isub =
            meta::has_isub<L, R> && noexcept((::std::declval<L&>() -= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_isub<L, R>)
        using isub_type = meta::isub_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept isub_returns =
            nothrow::has_isub<L, R> &&
            nothrow::convertible_to<nothrow::isub_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_mul = requires(L l, R r) { l * r; };

    template <typename L, typename R> requires (has_mul<L, R>)
    using mul_type = decltype((::std::declval<L>() * ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_mul =
            meta::has_mul<L, R> && noexcept((::std::declval<L>() * ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_mul<L, R>)
        using mul_type = meta::mul_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept mul_returns =
            nothrow::has_mul<L, R> &&
            nothrow::convertible_to<nothrow::mul_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_imul = requires(L& l, R r) { l *= r; };

    template <typename L, typename R> requires (has_imul<L, R>)
    using imul_type = decltype((::std::declval<L&>() *= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept imul_returns = has_imul<L, R> && convertible_to<imul_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_imul =
            meta::has_imul<L, R> && noexcept((::std::declval<L&>() *= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_imul<L, R>)
        using imul_type = meta::imul_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept imul_returns =
            nothrow::has_imul<L, R> &&
            nothrow::convertible_to<nothrow::imul_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_div = requires(L l, R r) { l / r; };

    template <typename L, typename R> requires (has_div<L, R>)
    using div_type = decltype((::std::declval<L>() / ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept div_returns =
        has_div<L, R> && convertible_to<div_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_div =
            meta::has_div<L, R> && noexcept((::std::declval<L>() / ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_div<L, R>)
        using div_type = meta::div_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept div_returns =
            nothrow::has_div<L, R> &&
            nothrow::convertible_to<nothrow::div_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_idiv = requires(L& l, R r) { l /= r; };

    template <typename L, typename R> requires (has_idiv<L, R>)
    using idiv_type = decltype((::std::declval<L&>() /= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept idiv_returns =
        has_idiv<L, R> && convertible_to<idiv_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_idiv =
            meta::has_idiv<L, R> && noexcept((::std::declval<L&>() /= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_idiv<L, R>)
        using idiv_type = meta::idiv_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept idiv_returns =
            nothrow::has_idiv<L, R> &&
            nothrow::convertible_to<nothrow::idiv_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_mod = requires(L l, R r) { l % r; };

    template <typename L, typename R> requires (has_mod<L, R>)
    using mod_type = decltype((::std::declval<L>() % ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_mod =
            meta::has_mod<L, R> && noexcept((::std::declval<L>() % ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_mod<L, R>)
        using mod_type = meta::mod_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept mod_returns =
            nothrow::has_mod<L, R> &&
            nothrow::convertible_to<nothrow::mod_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_imod = requires(L& l, R r) { l %= r; };

    template <typename L, typename R> requires (has_imod<L, R>)
    using imod_type = decltype((::std::declval<L&>() %= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept imod_returns =
        has_imod<L, R> && convertible_to<imod_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_imod =
            meta::has_imod<L, R> && noexcept((::std::declval<L&>() %= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_imod<L, R>)
        using imod_type = meta::imod_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept imod_returns =
            nothrow::has_imod<L, R> &&
            nothrow::convertible_to<nothrow::imod_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_lshift = requires(L l, R r) { l << r; };

    template <typename L, typename R> requires (has_lshift<L, R>)
    using lshift_type = decltype((::std::declval<L>() << ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept lshift_returns =
        has_lshift<L, R> && convertible_to<lshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_lshift =
            meta::has_lshift<L, R> && noexcept((::std::declval<L>() << ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_lshift<L, R>)
        using lshift_type = meta::lshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept lshift_returns =
            nothrow::has_lshift<L, R> &&
            nothrow::convertible_to<nothrow::lshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ilshift = requires(L& l, R r) { l <<= r; };

    template <typename L, typename R> requires (has_ilshift<L, R>)
    using ilshift_type = decltype((::std::declval<L&>() <<= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ilshift_returns =
        has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ilshift =
            meta::has_ilshift<L, R> &&
            noexcept((::std::declval<L&>() <<= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_ilshift<L, R>)
        using ilshift_type = meta::ilshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ilshift_returns =
            nothrow::has_ilshift<L, R> &&
            nothrow::convertible_to<nothrow::ilshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_rshift = requires(L l, R r) { l >> r; };

    template <typename L, typename R> requires (has_rshift<L, R>)
    using rshift_type = decltype((::std::declval<L>() >> ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept rshift_returns =
        has_rshift<L, R> && convertible_to<rshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_rshift =
            meta::has_rshift<L, R> && noexcept((::std::declval<L>() >> ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_rshift<L, R>)
        using rshift_type = meta::rshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept rshift_returns =
            nothrow::has_rshift<L, R> &&
            nothrow::convertible_to<nothrow::rshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_irshift = requires(L& l, R r) { l >>= r; };

    template <typename L, typename R> requires (has_irshift<L, R>)
    using irshift_type = decltype((::std::declval<L&>() >>= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept irshift_returns =
        has_irshift<L, R> && convertible_to<irshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_irshift =
            meta::has_irshift<L, R> &&
            noexcept((::std::declval<L&>() >>= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_irshift<L, R>)
        using irshift_type = meta::irshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept irshift_returns =
            nothrow::has_irshift<L, R> &&
            nothrow::convertible_to<nothrow::irshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_and = requires(L l, R r) { l & r; };

    template <typename L, typename R> requires (has_and<L, R>)
    using and_type = decltype((::std::declval<L>() & ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept and_returns = has_and<L, R> && convertible_to<and_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_and =
            meta::has_and<L, R> && noexcept((::std::declval<L>() & ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_and<L, R>)
        using and_type = meta::and_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept and_returns =
            nothrow::has_and<L, R> &&
            nothrow::convertible_to<nothrow::and_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_iand = requires(L& l, R r) { l &= r; };

    template <typename L, typename R> requires (has_iand<L, R>)
    using iand_type = decltype((::std::declval<L&>() &= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept iand_returns = has_iand<L, R> && convertible_to<iand_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_iand =
            meta::has_iand<L, R> && noexcept((::std::declval<L&>() &= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_iand<L, R>)
        using iand_type = meta::iand_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept iand_returns =
            nothrow::has_iand<L, R> &&
            nothrow::convertible_to<nothrow::iand_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_or = requires(L l, R r) { l | r; };

    template <typename L, typename R> requires (has_or<L, R>)
    using or_type = decltype((::std::declval<L>() | ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_or =
            meta::has_or<L, R> && noexcept((::std::declval<L>() | ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_or<L, R>)
        using or_type = meta::or_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept or_returns =
            nothrow::has_or<L, R> &&
            nothrow::convertible_to<nothrow::or_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ior = requires(L& l, R r) { l |= r; };

    template <typename L, typename R> requires (has_ior<L, R>)
    using ior_type = decltype((::std::declval<L&>() |= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ior_returns = has_ior<L, R> && convertible_to<ior_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ior =
            meta::has_ior<L, R> && noexcept((::std::declval<L&>() |= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_ior<L, R>)
        using ior_type = meta::ior_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ior_returns =
            nothrow::has_ior<L, R> &&
            nothrow::convertible_to<nothrow::ior_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_xor = requires(L l, R r) { l ^ r; };

    template <typename L, typename R> requires (has_xor<L, R>)
    using xor_type = decltype((::std::declval<L>() ^ ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept xor_returns = has_xor<L, R> && convertible_to<xor_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_xor =
            meta::has_xor<L, R> && noexcept((::std::declval<L>() ^ ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_xor<L, R>)
        using xor_type = meta::xor_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept xor_returns =
            nothrow::has_xor<L, R> &&
            nothrow::convertible_to<nothrow::xor_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ixor = requires(L& l, R r) { l ^= r; };

    template <typename L, typename R> requires (has_ixor<L, R>)
    using ixor_type = decltype((::std::declval<L&>() ^= ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ixor_returns = has_ixor<L, R> && convertible_to<ixor_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ixor =
            meta::has_ixor<L, R> && noexcept((::std::declval<L&>() ^= ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_ixor<L, R>)
        using ixor_type = meta::ixor_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ixor_returns =
            nothrow::has_ixor<L, R> &&
            nothrow::convertible_to<nothrow::ixor_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_logical_and = requires(L l, R r) { l && r; };

    template <typename L, typename R> requires (has_logical_and<L, R>)
    using logical_and_type = decltype((::std::declval<L>() && ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept logical_and_returns =
        has_logical_and<L, R> && convertible_to<logical_and_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_logical_and =
            meta::has_logical_and<L, R> &&
            noexcept((::std::declval<L&>() && ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_logical_and<L, R>)
        using logical_and_type = meta::logical_and_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept logical_and_returns =
            nothrow::has_logical_and<L, R> &&
            nothrow::convertible_to<nothrow::logical_and_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_logical_or = requires(L l, R r) { l || r; };

    template <typename L, typename R> requires (has_logical_or<L, R>)
    using logical_or_type = decltype((::std::declval<L>() && ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept logical_or_returns =
        has_logical_or<L, R> && convertible_to<logical_or_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_logical_or =
            meta::has_logical_or<L, R> &&
            noexcept((::std::declval<L&>() || ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_logical_or<L, R>)
        using logical_or_type = meta::logical_or_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept logical_or_returns =
            nothrow::has_logical_or<L, R> &&
            nothrow::convertible_to<nothrow::logical_or_type<L, R>, Ret>;

    }

    template <typename T>
    concept has_logical_not = requires(T t) { !t; };

    template <typename T> requires (has_logical_not<T>)
    using logical_not_type = decltype((!::std::declval<T>()));

    template <typename Ret, typename T>
    concept logical_not_returns =
        has_logical_not<T> && convertible_to<logical_not_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_logical_not =
            meta::has_logical_not<T> && noexcept((!::std::declval<T>()));

        template <typename T> requires (nothrow::has_logical_not<T>)
        using logical_not_type = meta::logical_not_type<T>;

        template <typename Ret, typename T>
        concept logical_not_returns =
            nothrow::has_logical_not<T> &&
            nothrow::convertible_to<nothrow::logical_not_type<T>, Ret>;

    }

    ///////////////////////////////////
    ////    STRUCTURAL CONCEPTS    ////
    ///////////////////////////////////

    template <typename T>
    concept has_value = requires(T t) { t.value(); };

    template <has_value T>
    using value_type = decltype(::std::declval<T>().value());

    template <typename Ret, typename T>
    concept value_returns = has_value<T> && convertible_to<value_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_value =
            meta::has_value<T> && noexcept(::std::declval<T>().value());

        template <nothrow::has_value T>
        using value_type = meta::value_type<T>;

        template <typename Ret, typename T>
        concept value_returns =
            nothrow::has_value<T> && nothrow::convertible_to<nothrow::value_type<T>, Ret>;

    }

    template <typename T>
    concept has_data = requires(T t) { ::std::ranges::data(t); };

    template <has_data T>
    using data_type = decltype(::std::ranges::data(::std::declval<T>()));

    template <typename Ret, typename T>
    concept data_returns = has_data<T> && convertible_to<data_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_data =
            meta::has_data<T> &&
            noexcept(::std::ranges::data(::std::declval<T>()));

        template <nothrow::has_data T>
        using data_type = meta::data_type<T>;

        template <typename Ret, typename T>
        concept data_returns =
            nothrow::has_data<T> &&
            nothrow::convertible_to<nothrow::data_type<T>, Ret>;

    }

    namespace detail {

        template <typename T>
        constexpr bool exact_size = true;

    }

    template <typename T>
    concept has_size = requires(T t) {
        { ::std::ranges::size(t) } -> unsigned_integer;
    };

    template <typename T>
    concept exact_size = has_size<T> && detail::exact_size<unqualify<T>>;

    template <has_size T>
    using size_type = decltype(::std::ranges::size(::std::declval<T>()));

    template <typename Ret, typename T>
    concept size_returns = has_size<T> && convertible_to<size_type<T>, Ret>;

    template <typename T>
    concept has_ssize = requires(T t) {
        { ::std::ranges::ssize(t) } -> signed_integer;
    };

    template <has_ssize T>
    using ssize_type = decltype(::std::ranges::ssize(::std::declval<T>()));

    template <typename Ret, typename T>
    concept ssize_returns = has_ssize<T> && convertible_to<ssize_type<T>, Ret>;

    template <typename T>
    concept has_empty = requires(T t) {
        { ::std::ranges::empty(t) } -> convertible_to<bool>;
    };

    namespace nothrow {

        template <typename T>
        concept has_size =
            meta::has_size<T> &&
            noexcept(::std::ranges::size(::std::declval<T>()));

        template <nothrow::has_size T>
        using size_type = meta::size_type<T>;

        template <typename Ret, typename T>
        concept size_returns =
            nothrow::has_size<T> &&
            nothrow::convertible_to<nothrow::size_type<T>, Ret>;

        template <typename T>
        concept has_ssize =
            meta::has_ssize<T> &&
            noexcept(::std::ranges::ssize(::std::declval<T>()));

        template <nothrow::has_ssize T>
        using ssize_type = meta::ssize_type<T>;

        template <typename Ret, typename T>
        concept ssize_returns =
            nothrow::has_ssize<T> &&
            nothrow::convertible_to<nothrow::ssize_type<T>, Ret>;

        template <typename T>
        concept has_empty =
            meta::has_empty<T> &&
            noexcept(::std::ranges::empty(::std::declval<T>()));

    }

    template <typename T>
    concept has_capacity = requires(T t) { t.capacity(); };

    template <has_capacity T>
    using capacity_type = decltype(::std::declval<T>().capacity());

    template <typename Ret, typename T>
    concept capacity_returns = has_capacity<T> && convertible_to<capacity_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_capacity =
            meta::has_capacity<T> &&
            noexcept(::std::declval<T>().capacity());

        template <nothrow::has_capacity T>
        using capacity_type = meta::capacity_type<T>;

        template <typename Ret, typename T>
        concept capacity_returns =
            nothrow::has_capacity<T> &&
            nothrow::convertible_to<nothrow::capacity_type<T>, Ret>;

    }

    template <typename T>
    concept has_reserve = requires(T t, size_t n) { t.reserve(n); };

    template <has_reserve T>
    using reserve_type = decltype(::std::declval<T>().reserve(::std::declval<size_t>()));

    template <typename Ret, typename T>
    concept reserve_returns = has_reserve<T> && convertible_to<reserve_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_reserve =
            meta::has_reserve<T> &&
            noexcept(::std::declval<T>().reserve(::std::declval<size_t>()));

        template <nothrow::has_reserve T>
        using reserve_type = meta::reserve_type<T>;

        template <typename Ret, typename T>
        concept reserve_returns =
            nothrow::has_reserve<T> &&
            nothrow::convertible_to<nothrow::reserve_type<T>, Ret>;

    }

    template <typename T, typename Key>
    concept has_contains = requires(T t, Key key) { t.contains(key); };

    template <typename T, typename Key> requires (has_contains<T, Key>)
    using contains_type = decltype(::std::declval<T>().contains(::std::declval<Key>()));

    template <typename Ret, typename T, typename Key>
    concept contains_returns =
        has_contains<T, Key> && convertible_to<contains_type<T, Key>, Ret>;

    namespace nothrow {

        template <typename T, typename Key>
        concept has_contains =
            meta::has_contains<T, Key> &&
            noexcept(::std::declval<T>().contains(::std::declval<Key>()));

        template <typename T, typename Key> requires (nothrow::has_contains<T, Key>)
        using contains_type = meta::contains_type<T, Key>;

        template <typename Ret, typename T, typename Key>
        concept contains_returns =
            nothrow::has_contains<T, Key> &&
            nothrow::convertible_to<nothrow::contains_type<T, Key>, Ret>;

    }

    template <typename T>
    concept sequence_like = iterable<T> && has_size<T> && requires(T t) {
        { t[0] } -> convertible_to<yield_type<T>>;
    };

    template <typename T>
    concept mapping_like = requires(T t) {
        typename unqualify<T>::key_type;
        typename unqualify<T>::mapped_type;
        { t[::std::declval<typename unqualify<T>::key_type>()] } ->
            convertible_to<typename unqualify<T>::mapped_type>;
    };

    template <typename T>
    concept has_keys = requires(T t) {
        { t.keys() } -> yields<typename unqualify<T>::key_type>;
    };

    template <has_keys T>
    using keys_type = decltype(::std::declval<T>().keys());

    namespace nothrow {

        template <typename T>
        concept has_keys =
            meta::has_keys<T> &&
            noexcept(::std::declval<T>().keys());

        template <nothrow::has_keys T>
        using keys_type = meta::keys_type<T>;

    }

    template <typename T>
    concept has_values = requires(T t) {
        { t.values() } -> yields<typename unqualify<T>::mapped_type>;
    };

    template <has_values T>
    using values_type = decltype(::std::declval<T>().values());

    namespace nothrow {

        template <typename T>
        concept has_values =
            meta::has_values<T> &&
            noexcept(::std::declval<T>().values());

        template <nothrow::has_values T>
        using values_type = meta::values_type<T>;

    }

    template <typename T>
    concept has_items =
        requires(T t) { { t.items() } -> iterable; } &&
        structured_with<
            yield_type<decltype(::std::declval<T>().items())>,
            typename unqualify<T>::key_type,
            typename unqualify<T>::mapped_type
        >;

    template <has_items T>
    using items_type = decltype(::std::declval<T>().items());

    namespace nothrow {

        template <typename T>
        concept has_items =
            meta::has_items<T> &&
            noexcept(::std::declval<T>().items());

        template <nothrow::has_items T>
        using items_type = meta::items_type<T>;

    }

    template <typename T>
    concept hashable = requires(T t) { ::std::hash<::std::decay_t<T>>{}(t); };

    template <hashable T>
    using hash_type = decltype(::std::hash<::std::decay_t<T>>{}(::std::declval<T>()));

    template <typename Ret, typename T>
    concept hash_returns = hashable<T> && convertible_to<hash_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept hashable =
            meta::hashable<T> &&
            noexcept(::std::hash<::std::decay_t<T>>{}(::std::declval<T>()));

        template <nothrow::hashable T>
        using hash_type = meta::hash_type<T>;

        template <typename Ret, typename T>
        concept hash_returns =
            nothrow::hashable<T> &&
            nothrow::convertible_to<nothrow::hash_type<T>, Ret>;

    }

    template <typename T, typename Char = char>
    concept has_stream_insertion = requires(::std::basic_ostream<Char>& os, T t) {
        { os << t } -> convertible_to<::std::basic_ostream<Char>&>;
    };

    namespace nothrow {

        template <typename T, typename Char = char>
        concept has_stream_insertion =
            meta::has_stream_insertion<T, Char> &&
            noexcept(::std::declval<::std::basic_ostream<Char>&>() << ::std::declval<T>());

    }

    template <typename T>
    concept has_real = requires(T t) { t.real(); };

    template <has_real T>
    using real_type = decltype(::std::declval<T>().real());

    template <typename Ret, typename T>
    concept real_returns = has_real<T> && convertible_to<real_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_real =
            meta::has_real<T> &&
            noexcept(::std::declval<T>().real());

        template <nothrow::has_real T>
        using real_type = meta::real_type<T>;

        template <typename Ret, typename T>
        concept real_returns =
            nothrow::has_real<T> &&
            nothrow::convertible_to<nothrow::real_type<T>, Ret>;

    }

    template <typename T>
    concept has_imag = requires(T t) { t.imag(); };

    template <has_imag T>
    using imag_type = decltype(::std::declval<T>().imag());

    template <typename Ret, typename T>
    concept imag_returns = has_imag<T> && convertible_to<imag_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_imag =
            meta::has_imag<T> &&
            noexcept(::std::declval<T>().imag());

        template <nothrow::has_imag T>
        using imag_type = meta::imag_type<T>;

        template <typename Ret, typename T>
        concept imag_returns =
            nothrow::has_imag<T> &&
            nothrow::convertible_to<nothrow::imag_type<T>, Ret>;

    }

    template <typename T>
    concept complex = real_returns<double, T> && imag_returns<double, T>;



    /// TODO: has_abs and has_pow should be redirected to the generic math operators
    /// I am implementing in math.h

    template <typename T>
    concept has_abs = requires(T t) { ::std::abs(t); };

    template <has_abs T>
    using abs_type = decltype(::std::abs(::std::declval<T>()));

    template <typename Ret, typename T>
    concept abs_returns = has_abs<T> && convertible_to<abs_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_abs =
            meta::has_abs<T> &&
            noexcept(::std::abs(::std::declval<T>()));

        template <nothrow::has_abs T>
        using abs_type = meta::abs_type<T>;

        template <typename Ret, typename T>
        concept abs_returns =
            nothrow::has_abs<T> &&
            nothrow::convertible_to<nothrow::abs_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_pow = requires(L l, R r) { ::std::pow(l, r); };

    template <typename L, typename R> requires (has_pow<L, R>)
    using pow_type = decltype(::std::pow(::std::declval<L>(), ::std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept pow_returns = has_pow<L, R> && convertible_to<pow_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_pow =
            meta::has_pow<L, R> &&
            noexcept(::std::pow(::std::declval<L>(), ::std::declval<R>()));

        template <typename L, typename R> requires (nothrow::has_pow<L, R>)
        using pow_type = meta::pow_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept pow_returns =
            nothrow::has_pow<L, R> &&
            nothrow::convertible_to<nothrow::pow_type<L, R>, Ret>;

    }

    /////////////////////////
    ////    STL TYPES    ////
    /////////////////////////

    namespace std {

        namespace detail {

            template <typename T>
            struct format_string { static constexpr bool enable = false; };
            template <typename Char, typename... Args>
            struct format_string<::std::basic_format_string<Char, Args...>> {
                static constexpr bool enable = true;
                using char_type = Char;
                using args_type = pack<Args...>;
            };

        }

        template <typename T>
        concept format_string = detail::format_string<unqualify<T>>::enable;

        template <format_string T>
        using format_string_char = detail::format_string<unqualify<T>>::char_type;

        template <format_string T>
        using format_string_args = detail::format_string<unqualify<T>>::args_type;

        namespace detail {

            template <typename T>
            struct optional { static constexpr bool value = false; };
            template <typename T>
            struct optional<::std::optional<T>> {
                static constexpr bool value = true;
                using type = T;
            };
    
        }
    
        template <typename T>
        concept optional = detail::optional<unqualify<T>>::value;

        template <optional T>
        using optional_type = detail::optional<unqualify<T>>::type;

        namespace detail {

            template <typename T>
            struct expected { static constexpr bool value = false; };
            template <typename T, typename E>
            struct expected<::std::expected<T, E>> {
                static constexpr bool value = true;
                using type = T;
                using error = E;
            };

        }

        template <typename T>
        concept expected = detail::expected<unqualify<T>>::value;

        template <expected T>
        using expected_type = detail::expected<unqualify<T>>::type;

        template <expected T>
        using expected_error = typename detail::expected<unqualify<T>>::error;

        namespace detail {

            template <typename T>
            struct variant { static constexpr bool value = false; };
            template <typename... Ts>
            struct variant<::std::variant<Ts...>> {
                static constexpr bool value = true;
                using types = pack<Ts...>;
            };
    
        }
    
        template <typename T>
        concept variant = detail::variant<unqualify<T>>::value;
    
        template <variant T>
        using variant_types = detail::variant<unqualify<T>>::types;
    
        namespace detail {
    
            template <typename T>
            struct shared_ptr { static constexpr bool enable = false; };
            template <typename T>
            struct shared_ptr<::std::shared_ptr<T>> {
                static constexpr bool enable = true;
                using type = T;
            };
    
        }
    
        template <typename T>
        concept shared_ptr = detail::shared_ptr<unqualify<T>>::enable;
    
        template <shared_ptr T>
        using shared_ptr_type = detail::shared_ptr<unqualify<T>>::type;
    
        namespace detail {
    
            template <typename T>
            struct unique_ptr { static constexpr bool enable = false; };
            template <typename T>
            struct unique_ptr<::std::unique_ptr<T>> {
                static constexpr bool enable = true;
                using type = T;
            };
    
        }
    
        template <typename T>
        concept unique_ptr = detail::unique_ptr<unqualify<T>>::enable;
    
        template <unique_ptr T>
        using unique_ptr_type = detail::unique_ptr<unqualify<T>>::type;

        template <typename A, typename T>
        concept allocator_for =
            // 1) A must have member alias value_type which equals T
            requires { typename unqualify<A>::value_type; } &&
            ::std::same_as<typename unqualify<A>::value_type, T> &&

            // 2) A must be copy and move constructible/assignable
            meta::copyable<unqualify<A>> &&
            meta::copy_assignable<unqualify<A>> &&
            meta::movable<unqualify<A>> &&
            meta::move_assignable<unqualify<A>> &&

            // 3) A must be equality comparable
            requires(A a, A b) {
                { a == b } -> meta::convertible_to<bool>;
                { a != b } -> meta::convertible_to<bool>;
            } &&

            // 4) A must be able to allocate and deallocate
            requires(A a, T* ptr, size_t n) {
                { a.allocate(n) } -> meta::convertible_to<T*>;
                { a.deallocate(ptr, n) };
            };

    }

    ////////////////////////////////////
    ////    CUSTOMIZATION POINTS    ////
    ////////////////////////////////////

    template <typename T>
    concept wrapper = inherits<T, impl::wrapper_tag>;

    namespace detail {

        /// NOTE: in all cases, cvref qualifiers will be stripped from the input
        /// types before checking against these customization points.

        /* Enables the `sorted()` helper function for sortable container types.  The
        `::type` alias should map the templated `Less` function into the template
        definition for `T`.  */
        template <meta::unqualified Less, meta::unqualified T>
            requires (
                meta::iterable<T> &&
                meta::default_constructible<Less> &&
                meta::call_returns<
                    bool,
                    meta::as_lvalue<Less>,
                    meta::as_const<meta::yield_type<T>>,
                    meta::as_const<meta::yield_type<T>>
                >
            )
        struct sorted { using type = void; };

        /* Enables the `*` unpacking operator for iterable container types. */
        template <meta::iterable T>
        constexpr bool enable_unpack_operator = false;

        /* Enables the `->*` comprehension operator for iterable container types. */
        template <meta::iterable T>
        constexpr bool enable_comprehension_operator = false;

    }

}


/* An empty type that can be used as a drop-in replacement for `std::nullopt` and
`std::nullptr_t`, with the only difference being that conversions to pointer types must
be explicit, in order to avoid ambiguities in overload resolution.  Comparisons with
pointer types are unrestricted, in which `NoneType` acts just like `nullptr`.

This type can also be used as a trivial sentinel in custom iterator implementations,
where all relevant information is isolated to the begin iterator.  Special significance
is also given to this type within the monadic union interface, where it represents
either the empty state of an `Optional` or the result state of an `Expected<void>`,
depending on context. */
struct NoneType {
    [[nodiscard]] constexpr NoneType() = default;
    [[nodiscard]] constexpr NoneType(std::nullopt_t) noexcept {}
    [[nodiscard]] constexpr NoneType(std::nullptr_t) noexcept {}

    [[nodiscard]] friend constexpr bool operator==(NoneType, NoneType) noexcept { return true; }

    template <typename T> requires (!meta::convertible_to<T, NoneType>)
    [[nodiscard]] friend constexpr bool operator==(NoneType, T&& other)
        noexcept (requires{
            {std::nullopt == std::forward<T>(other)} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (requires{
            {std::nullopt == std::forward<T>(other)} -> meta::convertible_to<bool>;
        })
    {
        return std::nullopt == std::forward<T>(other);
    }

    template <typename T> requires (!meta::convertible_to<T, NoneType>)
    [[nodiscard]] friend constexpr bool operator==(NoneType, T&& other)
        noexcept (requires{
            {nullptr == std::forward<T>(other)} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (requires{
            {nullptr == std::forward<T>(other)} -> meta::convertible_to<bool>;
        })
    {
        return nullptr == std::forward<T>(other);
    }

    template <typename T> requires (!meta::convertible_to<T, NoneType>)
    [[nodiscard]] friend constexpr bool operator==(T&& other, NoneType)
        noexcept (requires{
            {std::forward<T>(other) == std::nullopt} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (requires{
            {std::forward<T>(other) == std::nullopt} -> meta::convertible_to<bool>;
        })
    {
        return std::forward<T>(other) == std::nullopt;
    }

    template <typename T> requires (!meta::convertible_to<T, NoneType>)
    [[nodiscard]] friend constexpr bool operator==(T&& other, NoneType)
        noexcept (requires{
            {std::forward<T>(other) == nullptr} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (
            !requires{{std::forward<T>(other) == std::nullopt} -> meta::convertible_to<bool>;} &&
            requires{{std::forward<T>(other) == nullptr} -> meta::convertible_to<bool>;}
        )
    {
        return std::forward<T>(other) == nullptr;
    }
    [[nodiscard]] friend constexpr bool operator!=(NoneType, NoneType) noexcept { return false; }

    template <typename T> requires (!meta::convertible_to<T, NoneType>)
    [[nodiscard]] friend constexpr bool operator!=(NoneType, T&& other)
        noexcept (requires{
            {std::nullopt != std::forward<T>(other)} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (requires{
            {std::nullopt != std::forward<T>(other)} -> meta::convertible_to<bool>;
        })
    {
        return std::nullopt != std::forward<T>(other);
    }

    template <typename T> requires (!meta::convertible_to<T, NoneType>)
    [[nodiscard]] friend constexpr bool operator!=(NoneType, T&& other)
        noexcept (requires{
            {nullptr != std::forward<T>(other)} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (requires{
            {nullptr != std::forward<T>(other)} -> meta::convertible_to<bool>;
        })
    {
        return nullptr != std::forward<T>(other);
    }

    template <typename T> requires (!meta::convertible_to<T, NoneType>)
    [[nodiscard]] friend constexpr bool operator!=(T&& other, NoneType)
        noexcept (requires{
            {std::forward<T>(other) != std::nullopt} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (requires{
            {std::forward<T>(other) != std::nullopt} -> meta::convertible_to<bool>;
        })
    {
        return std::forward<T>(other) != std::nullopt;
    }

    template <typename T> requires (!meta::convertible_to<T, NoneType>)
    [[nodiscard]] friend constexpr bool operator!=(T&& other, NoneType)
        noexcept (requires{
            {std::forward<T>(other) != nullptr} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (
            !requires{{std::forward<T>(other) != std::nullopt} -> meta::convertible_to<bool>;} &&
            requires{{std::forward<T>(other) != nullptr} -> meta::convertible_to<bool>;}
        )
    {
        return std::forward<T>(other) != nullptr;
    }

    template <typename T>
    [[nodiscard]] constexpr operator T() const
        noexcept (meta::nothrow::convertible_to<const std::nullopt_t&, T>)
        requires (meta::convertible_to<const std::nullopt_t&, T>)
    {
        return std::nullopt;
    }

    template <typename T>
    [[nodiscard]] explicit constexpr operator T() const
        noexcept (meta::nothrow::explicitly_convertible_to<const std::nullopt_t&, T>)
        requires (
            !meta::convertible_to<const std::nullopt_t&, T> &&
            meta::explicitly_convertible_to<const std::nullopt_t&, T>
        )
    {
        return static_cast<T>(std::nullopt);
    }

    template <typename T>
    [[nodiscard]] explicit constexpr operator T() const
        noexcept (meta::nothrow::explicitly_convertible_to<const std::nullopt_t&, T>)
        requires (
            !meta::explicitly_convertible_to<const std::nullopt_t&, T> &&
            meta::explicitly_convertible_to<std::nullptr_t, T>
        )
    {
        return static_cast<T>(nullptr);
    }
};


/* The global `None` singleton, representing the absence of a value. */
inline constexpr NoneType None;


/* Hash an arbitrary value.  Equivalent to calling `std::hash<T>{}(...)`, but without
needing to explicitly specialize `std::hash`. */
template <meta::hashable T>
[[nodiscard]] constexpr auto hash(T&& obj) noexcept(meta::nothrow::hashable<T>) {
    return std::hash<std::decay_t<T>>{}(std::forward<T>(obj));
}


namespace impl {

    /* A simple overload set for a set of function objects Fs.... */
    template <typename... Fs>
    struct overloads : public Fs... { using Fs::operator()...; };

    template <typename... Fs>
    overloads(Fs&&...) -> overloads<meta::remove_rvalue<Fs>...>;

    /* A functor that implements a universal, non-cryptographic FNV-1a string hashing
    algorithm, which is stable at both compile time and runtime. */
    struct fnv1a {
        static constexpr size_t seed =
            sizeof(size_t) > 4 ? size_t(14695981039346656037ULL) : size_t(2166136261U);

        static constexpr size_t prime =
            sizeof(size_t) > 4 ? size_t(1099511628211ULL) : size_t(16777619U);

        [[nodiscard]] static constexpr size_t operator()(
            const char* str,
            size_t seed = fnv1a::seed,
            size_t prime = fnv1a::prime
        ) noexcept {
            while (*str) {
                seed ^= static_cast<size_t>(*str);
                seed *= prime;
                ++str;
            }
            return seed;
        }
    };

    /* Merge several hashes into a single value.  Based on `boost::hash_combine()`:
    https://www.boost.org/doc/libs/1_86_0/libs/container_hash/doc/html/hash.html#notes_hash_combine */
    template <meta::convertible_to<size_t>... Hashes>
    size_t hash_combine(size_t first, Hashes... rest) noexcept {
        if constexpr (sizeof(size_t) == 4) {
            constexpr auto mix = [](size_t& seed, size_t value) {
                seed += 0x9e3779b9 + value;
                seed ^= seed >> 16;
                seed *= 0x21f0aaad;
                seed ^= seed >> 15;
                seed *= 0x735a2d97;
                seed ^= seed >> 15;
            };
            (mix(first, rest), ...);
        } else {
            constexpr auto mix = [](size_t& seed, size_t value) {
                seed += 0x9e3779b9 + value;
                seed ^= seed >> 32;
                seed *= 0xe9846af9b1a615d;
                seed ^= seed >> 32;
                seed *= 0xe9846af9b1a615d;
                seed ^= seed >> 28;
            };
            (mix(first, rest), ...);
        }
        return first;
    }

    template <typename T>
    struct Construct {
        template <typename... Args>
        static constexpr T operator()(Args&&... args)
            noexcept (meta::nothrow::constructible_from<T, Args...>)
            requires (meta::constructible_from<T, Args...>)
        {
            return T(std::forward<Args>(args)...);
        }
    };

    struct Assign {
        template <typename T, typename U>
        static constexpr decltype(auto) operator()(T&& curr, U&& other)
            noexcept (meta::nothrow::assignable<T, U>)
            requires (meta::assignable<T, U>)
        {
            return (std::forward<T>(curr) = std::forward<U>(other));
        }
    };

    template <meta::not_void T>
    struct ConvertTo {
        template <meta::convertible_to<T> U>
        static constexpr T operator()(U&& value)
            noexcept (meta::nothrow::convertible_to<U, T>)
        {
            return std::forward<U>(value);
        }
    };

    template <meta::not_void T>
    struct ExplicitConvertTo {
        template <meta::explicitly_convertible_to<T> U>
        static constexpr T operator()(U&& value)
            noexcept (meta::nothrow::explicitly_convertible_to<U, T>)
        {
            return static_cast<T>(std::forward<U>(value));
        }
    };

    struct Hash {
        template <meta::hashable T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept (meta::nothrow::hashable<T>)
        {
            return (bertrand::hash(std::forward<T>(value)));
        }
    };

    struct AddressOf {
        template <meta::has_address T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_address<T>)
        {
            return (std::addressof(std::forward<T>(value)));
        }
    };

    struct Dereference {
        template <meta::has_dereference T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_dereference<T>)
        {
            return (*std::forward<T>(value));
        }
    };

    struct Arrow {
        template <meta::has_arrow T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_arrow<T>)
        {
            return (std::to_address(std::forward<T>(value)));
        }
    };

    struct ArrowDereference {
        template <typename L, typename R> requires (meta::has_arrow_dereference<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_arrow_dereference<L, R>)
        {
            return (std::forward<L>(lhs)->*std::forward<R>(rhs));
        }
    };

    struct Call {
        template <typename F, typename... A> requires (meta::callable<F, A...>)
        static constexpr decltype(auto) operator()(F&& f, A&&... args)
            noexcept(meta::nothrow::callable<F, A...>)
        {
            return (std::forward<F>(f)(std::forward<A>(args)...));
        }
    };

    struct Subscript {
        template <typename T, typename... K> requires (meta::indexable<T, K...>)
        static constexpr decltype(auto) operator()(T&& value, K&&... keys)
            noexcept(meta::nothrow::indexable<T, K...>)
        {
            return (std::forward<T>(value)[std::forward<K>(keys)...]);
        }
    };

    struct Pos {
        template <meta::has_pos T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_pos<T>)
        {
            return (+std::forward<T>(value));
        }
    };

    struct Neg {
        template <meta::has_neg T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_neg<T>)
        {
            return (-std::forward<T>(value));
        }
    };

    struct PreIncrement {
        template <meta::has_preincrement T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_preincrement<T>)
        {
            return (++std::forward<T>(value));
        }
    };

    struct PostIncrement {
        template <meta::has_postincrement T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_postincrement<T>)
        {
            return (std::forward<T>(value)++);
        }
    };

    struct PreDecrement {
        template <meta::has_predecrement T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_predecrement<T>)
        {
            return (--std::forward<T>(value));
        }
    };

    struct PostDecrement {
        template <meta::has_postdecrement T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_postdecrement<T>)
        {
            return (std::forward<T>(value)--);
        }
    };

    struct LogicalNot {
        template <meta::has_logical_not T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_logical_not<T>)
        {
            return (!std::forward<T>(value));
        }
    };

    struct LogicalAnd {
        template <typename L, typename R> requires (meta::has_logical_and<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_logical_and<L, R>)
        {
            return (std::forward<L>(lhs) && std::forward<R>(rhs));
        }
    };

    struct LogicalOr {
        template <typename L, typename R> requires (meta::has_logical_or<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_logical_or<L, R>)
        {
            return (std::forward<L>(lhs) || std::forward<R>(rhs));
        }
    };

    struct Less {
        template <typename L, typename R> requires (meta::has_lt<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_lt<L, R>)
        {
            return (std::forward<L>(lhs) < std::forward<R>(rhs));
        }
    };

    struct LessEqual {
        template <typename L, typename R> requires (meta::has_le<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_le<L, R>)
        {
            return (std::forward<L>(lhs) <= std::forward<R>(rhs));
        }
    };

    struct Equal {
        template <typename L, typename R> requires (meta::has_eq<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_eq<L, R>)
        {
            return (std::forward<L>(lhs) == std::forward<R>(rhs));
        }
    };

    struct NotEqual {
        template <typename L, typename R> requires (meta::has_ne<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ne<L, R>)
        {
            return (std::forward<L>(lhs) != std::forward<R>(rhs));
        }
    };

    struct GreaterEqual {
        template <typename L, typename R> requires (meta::has_ge<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ge<L, R>)
        {
            return (std::forward<L>(lhs) >= std::forward<R>(rhs));
        }
    };

    struct Greater {
        template <typename L, typename R> requires (meta::has_gt<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_gt<L, R>)
        {
            return (std::forward<L>(lhs) > std::forward<R>(rhs));
        }
    };

    struct Spaceship {
        template <typename L, typename R> requires (meta::has_spaceship<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_spaceship<L, R>)
        {
            return (std::forward<L>(lhs) <=> std::forward<R>(rhs));
        }
    };

    struct Add {
        template <typename L, typename R> requires (meta::has_add<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_add<L, R>)
        {
            return (std::forward<L>(lhs) + std::forward<R>(rhs));
        }
    };

    struct InplaceAdd {
        template <typename L, typename R> requires (meta::has_iadd<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_iadd<L, R>)
        {
            return (std::forward<L>(lhs) += std::forward<R>(rhs));
        }
    };

    struct Subtract {
        template <typename L, typename R> requires (meta::has_sub<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_sub<L, R>)
        {
            return (std::forward<L>(lhs) - std::forward<R>(rhs));
        }
    };

    struct InplaceSubtract {
        template <typename L, typename R> requires (meta::has_isub<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_isub<L, R>)
        {
            return (std::forward<L>(lhs) -= std::forward<R>(rhs));
        }
    };

    struct Multiply {
        template <typename L, typename R> requires (meta::has_mul<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_mul<L, R>)
        {
            return (std::forward<L>(lhs) * std::forward<R>(rhs));
        }
    };

    struct InplaceMultiply {
        template <typename L, typename R> requires (meta::has_imul<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_imul<L, R>)
        {
            return (std::forward<L>(lhs) *= std::forward<R>(rhs));
        }
    };

    struct Divide {
        template <typename L, typename R> requires (meta::has_div<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_div<L, R>)
        {
            return (std::forward<L>(lhs) / std::forward<R>(rhs));
        }
    };

    struct InplaceDivide {
        template <typename L, typename R> requires (meta::has_idiv<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_idiv<L, R>)
        {
            return (std::forward<L>(lhs) /= std::forward<R>(rhs));
        }
    };

    struct Modulus {
        template <typename L, typename R> requires (meta::has_mod<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_mod<L, R>)
        {
            return (std::forward<L>(lhs) % std::forward<R>(rhs));
        }
    };

    struct InplaceModulus {
        template <typename L, typename R> requires (meta::has_imod<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_imod<L, R>)
        {
            return (std::forward<L>(lhs) %= std::forward<R>(rhs));
        }
    };

    struct LeftShift {
        template <typename L, typename R> requires (meta::has_lshift<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_lshift<L, R>)
        {
            return (std::forward<L>(lhs) << std::forward<R>(rhs));
        }
    };

    struct InplaceLeftShift {
        template <typename L, typename R> requires (meta::has_ilshift<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ilshift<L, R>)
        {
            return (std::forward<L>(lhs) <<= std::forward<R>(rhs));
        }
    };

    struct RightShift {
        template <typename L, typename R> requires (meta::has_rshift<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_rshift<L, R>)
        {
            return (std::forward<L>(lhs) >> std::forward<R>(rhs));
        }
    };

    struct InplaceRightShift {
        template <typename L, typename R> requires (meta::has_irshift<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_irshift<L, R>)
        {
            return (std::forward<L>(lhs) >>= std::forward<R>(rhs));
        }
    };

    struct BitwiseNot {
        template <meta::has_bitwise_not T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_bitwise_not<T>)
        {
            return (~std::forward<T>(value));
        }
    };

    struct BitwiseAnd {
        template <typename L, typename R> requires (meta::has_and<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_and<L, R>)
        {
            return (std::forward<L>(lhs) & std::forward<R>(rhs));
        }
    };

    struct InplaceBitwiseAnd {
        template <typename L, typename R> requires (meta::has_iand<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_iand<L, R>)
        {
            return (std::forward<L>(lhs) &= std::forward<R>(rhs));
        }
    };

    struct BitwiseOr {
        template <typename L, typename R> requires (meta::has_or<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_or<L, R>)
        {
            return (std::forward<L>(lhs) | std::forward<R>(rhs));
        }
    };

    struct InplaceBitwiseOr {
        template <typename L, typename R> requires (meta::has_ior<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ior<L, R>)
        {
            return (std::forward<L>(lhs) |= std::forward<R>(rhs));
        }
    };

    struct BitwiseXor {
        template <typename L, typename R> requires (meta::has_xor<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_xor<L, R>)
        {
            return (std::forward<L>(lhs) ^ std::forward<R>(rhs));
        }
    };

    struct InplaceBitwiseXor {
        template <typename L, typename R> requires (meta::has_ixor<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ixor<L, R>)
        {
            return (std::forward<L>(lhs) ^= std::forward<R>(rhs));
        }
    };

}


/// TODO: static_str -> StaticStr?


/* A compile-time string literal type that can be used as a non-type template
parameter.

This class allows string literals to be encoded directly into C++'s type system,
consistent with other consteval language constructs.  That means they can be used to
specialize templates and trigger arbitrarily complex metafunctions at compile time,
without any impact on the final binary.  Such metafunctions are used internally to
implement zero-cost keyword arguments for C++ functions, full static type safety for
Python attributes, and minimal perfect hash tables for arbitrary data.

Users can leverage these strings for their own metaprogramming needs as well; the class
implements the full Python string interface as consteval member methods, and even
supports regular expressions through the CTRE library.  This can serve as a powerful
base for user-defined metaprogramming facilities, up to and including full
Domain-Specific Languages (DSLs) that can be parsed at compile time and subjected to
exhaustive static analysis. */
template <size_t N = 0>
struct static_str;


namespace impl {

    /// TODO: float_to_string and int_to_string may need to account for the new,
    /// arbitrary-precision numeric types in bits.h

    template <typename T>
    struct bit_view;
    template <typename T> requires (sizeof(T) == 1)
    struct bit_view<T> { using type = uint8_t; };
    template <typename T> requires (sizeof(T) == 2)
    struct bit_view<T> { using type = uint16_t; };
    template <typename T> requires (sizeof(T) == 4)
    struct bit_view<T> { using type = uint32_t; };
    template <typename T> requires (sizeof(T) == 8)
    struct bit_view<T> { using type = uint64_t; };

    /* Get the state of the sign bit (0 or 1) for a floating point number.  1 indicates
    a negative number. */
    inline constexpr bool sign_bit(double num) noexcept {
        using Int = bit_view<double>::type;
        return std::bit_cast<Int>(num) >> (8 * sizeof(double) - 1);
    };

    template <long long num, size_t base>
    static constexpr size_t _int_string_length = [] {
        if constexpr (num == 0) {
            return 0;
        } else {
            return _int_string_length<num / base, base> + 1;
        }
    }();

    template <long long num, size_t base>
    static constexpr size_t int_string_length = [] {
        // length is always at least 1 to correct for num == 0
        if constexpr (num < 0) {
            return std::max(_int_string_length<-num, base>, 1UL) + 1;  // include negative sign
        } else {
            return std::max(_int_string_length<num, base>, 1UL);
        }
    }();

    template <double num, size_t precision>
    static constexpr size_t float_string_length = [] {
        if constexpr (std::isnan(num)) {
            return 3;  // "nan"
        } else if constexpr (std::isinf(num)) {
            return 3 + impl::sign_bit(num);  // "inf" or "-inf"
        } else {
            // negative zero integral part needs a leading minus sign
            return
            int_string_length<static_cast<long long>(num), 10> +
                (static_cast<long long>(num) == 0 && impl::sign_bit(num)) +
                (precision > 0) +
                precision;
        }
    }();

    /* Convert an integer into a string at compile time using the specified base. */
    template <long long num, size_t base = 10> requires (base >= 2 && base <= 36)
    static constexpr auto int_to_static_string = [] {
        constexpr const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        constexpr size_t len = int_string_length<num, base>;
        static_str<len> result;

        long long temp = num;
        size_t idx = len - 1;
        if constexpr (num < 0) {
            result.buffer[0] = '-';
            temp = -temp;
        } else if constexpr (num == 0) {
            result.buffer[idx--] = '0';
        }

        while (temp > 0) {
            result.buffer[idx--] = chars[temp % base];
            temp /= base;
        }

        result.buffer[len] = '\0';
        return result;
    }();

    /* Convert a floating point number into a string at compile time with the
    specified precision. */
    template <double num, size_t precision = 6>
    static constexpr auto float_to_static_string = [] {
        constexpr size_t len = float_string_length<num, precision>;
        static_str<len> result;

        if constexpr (std::isnan(num)) {
            result.buffer[0] = 'n';
            result.buffer[1] = 'a';
            result.buffer[2] = 'n';

        } else if constexpr (std::isinf(num)) {
            if constexpr (num < 0) {
                result.buffer[0] = '-';
                result.buffer[1] = 'i';
                result.buffer[2] = 'n';
                result.buffer[3] = 'f';
            } else {
                result.buffer[0] = 'i';
                result.buffer[1] = 'n';
                result.buffer[2] = 'f';
            }

        } else {
            // decompose into integral and (rounded) fractional parts
            constexpr long long integral = static_cast<long long>(num);
            constexpr long long fractional = [] {
                double exp = 1;
                for (size_t i = 0; i < precision; ++i) {
                    exp *= 10;
                }
                if constexpr (num > integral) {
                    return static_cast<long long>((num - integral) * exp + 0.5);
                } else {
                    return static_cast<long long>((integral - num) * exp + 0.5);
                }
            }();

            // convert to string (base 10)
            constexpr auto integral_str = int_to_static_string<integral, 10>;
            constexpr auto fractional_str = int_to_static_string<fractional, 10>;

            char* pos = result.buffer;
            if constexpr (integral == 0 && impl::sign_bit(num)) {
                result.buffer[0] = '-';
                ++pos;
            }

            // concatenate integral and fractional parts (zero padded to precision)
            std::copy_n(
                integral_str.buffer,
                integral_str.size(),
                pos
            );
            if constexpr (precision > 0) {
                std::copy_n(".", 1, pos + integral_str.size());
                pos += integral_str.size() + 1;
                std::copy_n(
                    fractional_str.buffer,
                    fractional_str.size(),
                    pos
                );
                std::fill_n(
                    pos + fractional_str.size(),
                    precision - fractional_str.size(),
                    '0'
                );
            }
        }

        result.buffer[len] = '\0';
        return result;
    }();

    template <typename T>
    constexpr auto type_name_impl() {
        #if defined(__clang__)
            constexpr std::string_view prefix {"[T = "};
            constexpr std::string_view suffix {"]"};
            constexpr std::string_view function {__PRETTY_FUNCTION__};
        #elif defined(__GNUC__)
            constexpr std::string_view prefix {"with T = "};
            constexpr std::string_view suffix {"]"};
            constexpr std::string_view function {__PRETTY_FUNCTION__};
        #elif defined(_MSC_VER)
            constexpr std::string_view prefix {"type_name_impl<"};
            constexpr std::string_view suffix {">(void)"};
            constexpr std::string_view function {__FUNCSIG__};
        #else
            #error Unsupported compiler
        #endif

        constexpr size_t start = function.find(prefix) + prefix.size();
        constexpr size_t end = function.rfind(suffix);
        static_assert(start < end);

        constexpr std::string_view name = function.substr(start, (end - start));
        constexpr size_t N = name.size();
        return static_str<N>{name.data()};
    }

}


namespace meta {

    namespace detail {

        template <typename T>
        constexpr bool static_str = false;
        template <size_t N>
        constexpr bool static_str<bertrand::static_str<N>> = true;


    }

    template <typename T>
    concept static_str = detail::static_str<unqualify<T>>;

}


/* Mangle an object according to the compiler's implementation-defined behavior. */
template <typename T>
constexpr std::string_view mangle(T&& value) noexcept {
    return typeid(T).name();
}


/* Mangle a type according to the compiler's implementation-defined behavior. */
template <typename T>
constexpr std::string_view mangle() noexcept {
    return typeid(T).name();
}


/* Demangle a runtime string using the compiler's intrinsics. */
constexpr std::string demangle(const char* name) {
    #if defined(__GNUC__) || defined(__clang__)
        int status = 0;
        std::unique_ptr<char, void(*)(void*)> res {
            abi::__cxa_demangle(
                name,
                nullptr,
                nullptr,
                &status
            ),
            std::free
        };
        return (status == 0) ? res.get() : name;
    #elif defined(_MSC_VER)
        char undecorated_name[1024];
        if (UnDecorateSymbolName(
            name,
            undecorated_name,
            sizeof(undecorated_name),
            UNDNAME_COMPLETE
        )) {
            return std::string(undecorated_name);
        } else {
            return name;
        }
    #else
        return name; // fallback: no demangling
    #endif
}


/* Gets a C++ type name as a fully-qualified, demangled string computed entirely
at compile time.  The underlying buffer is baked directly into the final binary. */
template <typename T>
constexpr auto demangle() noexcept(noexcept(impl::type_name_impl<T>())) {
    return impl::type_name_impl<T>();
}


}  // namespace bertrand


namespace std {

    template <bertrand::meta::is<bertrand::NoneType> T>
    struct hash<T> {
        [[nodiscard]] static constexpr size_t operator()(const T& value) noexcept {
            // any arbitrary constant will do.  Large random numbers are preferred to
            // avoid collisions clustered around zero.
            return 4238894112;
        }
    };

}


#endif  // BERTRAND_COMMON_H
