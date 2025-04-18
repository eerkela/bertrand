#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <algorithm>
#include <array>
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
    struct args_tag {};
}


template <typename... Ts>
struct args;


namespace meta {

    /* Trigger implicit conversion operators and/or implicit constructors, but not
    explicit ones.  In contrast, static_cast<>() will trigger explicit constructors on
    the target type, which can give unexpected results and violate type safety. */
    template <typename T>
    constexpr T implicit_cast(auto&& value) {
        return std::forward<decltype(value)>(value);
    }

    namespace detail {

        template <typename Search, size_t I, typename... Ts>
        constexpr size_t index_of = 0;
        template <typename Search, size_t I, typename T, typename... Ts>
        constexpr size_t index_of<Search, I, T, Ts...> =
            std::same_as<Search, T> ? 0 : index_of<Search, I + 1, Ts...> + 1;

        template <typename...>
        constexpr bool types_are_unique = true;
        template <typename T, typename... Ts>
        constexpr bool types_are_unique<T, Ts...> =
            !(std::same_as<T, Ts> || ...) && types_are_unique<Ts...>;

        template <typename out, typename...>
        struct unique_types { using type = out; };
        template <typename... out, typename T, typename... Ts>
        struct unique_types<bertrand::args<out...>, T, Ts...> {
            using type = unique_types<bertrand::args<out..., T>, Ts...>::type;
        };
        template <typename... out, typename T, typename... Ts>
            requires (std::same_as<T, out> || ...)
        struct unique_types<bertrand::args<out...>, T, Ts...> {
            using type = unique_types<bertrand::args<out...>, Ts...>::type;
        };

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

    }

    /* Get the count of a particular type within a parameter pack.  Returns zero if
    the type is not present. */
    template <typename Search, typename... Ts>
    static constexpr size_t count_of = (std::same_as<Search, Ts> + ...);

    /* Get the index of a particular type within a parameter pack.  Returns the pack's
    size if the type is not present. */
    template <typename Search, typename... Ts>
    static constexpr size_t index_of = detail::index_of<Search, 0, Ts...>;

    /* Get the type at a particular index of a parameter pack.  This is superceded by
    the C++26 pack indexing language feature. */
    template <size_t I, typename... Ts> requires (I < sizeof...(Ts))
    using unpack_type = detail::unpack_type<I, Ts...>::type;

    /* Unpack the non-type template parameter at a particular index of a parameter
    pack.  This is superceded by the C++26 pack indexing language feature. */
    template <size_t I, auto... Vs> requires (I < sizeof...(Vs))
    constexpr auto unpack_value = detail::unpack_value<I, Vs...>;

    /* Index into a parameter pack and perfectly forward a single item.  This is
    superceded by the C++26 pack indexing language feature. */
    template <size_t I, typename... Ts> requires (I < sizeof...(Ts))
    constexpr void unpack_arg(Ts&&...) noexcept {}
    template <size_t I, typename T, typename... Ts> requires (I < (sizeof...(Ts) + 1))
    constexpr decltype(auto) unpack_arg(T&& curr, Ts&&... next) noexcept {
        if constexpr (I == 0) {
            return std::forward<T>(curr);
        } else {
            return unpack_arg<I - 1>(std::forward<Ts>(next)...);
        }
    }

    /* Concept that is satisfied only if every `T` occurs exactly once in `Ts...`. */
    template <typename... Ts>
    concept types_are_unique = detail::types_are_unique<Ts...>;

    /* Filter out any duplicate types from `Ts...`, returning a `bertrand::args<Us...>`
    placeholder where `Us...` contains only the unique types. */
    template <typename... Ts>
    using unique_types = detail::unique_types<bertrand::args<>, Ts...>::type;

    /////////////////////////////
    ////    QUALIFICATION    ////
    /////////////////////////////

    template <typename L, typename R>
    concept is = std::same_as<std::remove_cvref_t<L>, std::remove_cvref_t<R>>;

    template <typename L, typename R>
    concept inherits =
        is<L, R> || std::derived_from<std::remove_cvref_t<L>, std::remove_cvref_t<R>>;

    template <typename T>
    concept lvalue = std::is_lvalue_reference_v<T>;

    template <typename T>
    using as_lvalue = std::add_lvalue_reference_t<T>;

    template <typename T>
    using remove_lvalue = std::conditional_t<
        lvalue<T>,
        std::remove_reference_t<T>,
        T
    >;

    template <typename T>
    concept rvalue = std::is_rvalue_reference_v<T>;

    template <typename T>
    using as_rvalue = std::add_rvalue_reference_t<T>;

    template <typename T>
    using remove_rvalue = std::conditional_t<
        rvalue<T>,
        std::remove_reference_t<T>,
        T
    >;

    template <typename T>
    concept reference = lvalue<T> || rvalue<T>;

    template <typename T>
    using remove_reference = std::remove_reference_t<T>;

    template <typename T>
    concept pointer = std::is_pointer_v<std::remove_reference_t<T>>;

    template <typename T>
    using as_pointer = std::add_pointer_t<T>;

    template <typename T>
    using remove_pointer = std::remove_pointer_t<T>;

    template <typename T>
    concept is_const = std::is_const_v<std::remove_reference_t<T>>;

    template <typename T>
    concept not_const = !is_const<T>;

    template <typename T>
    using as_const = std::conditional_t<
        meta::lvalue<T>,
        meta::as_lvalue<std::add_const_t<meta::remove_reference<T>>>,
        std::conditional_t<
            meta::rvalue<T>,
            meta::as_rvalue<std::add_const_t<meta::remove_reference<T>>>,
            std::add_const_t<meta::remove_reference<T>>
        >
    >;

    template <typename T>
    using remove_const = std::conditional_t<
        is_const<T>,
        std::conditional_t<
            meta::lvalue<T>,
            meta::as_lvalue<std::remove_const_t<meta::remove_reference<T>>>,
            std::conditional_t<
                meta::rvalue<T>,
                meta::as_rvalue<std::remove_const_t<meta::remove_reference<T>>>,
                std::remove_const_t<meta::remove_reference<T>>
            >
        >,
        T
    >;

    template <typename T>
    concept is_volatile = std::is_volatile_v<std::remove_reference_t<T>>;

    template <typename T>
    using as_volatile = std::conditional_t<
        meta::lvalue<T>,
        meta::as_lvalue<std::add_volatile_t<meta::remove_reference<T>>>,
        std::conditional_t<
            meta::rvalue<T>,
            meta::as_rvalue<std::add_volatile_t<meta::remove_reference<T>>>,
            std::add_volatile_t<meta::remove_reference<T>>
        >
    >;

    template <typename T>
    using remove_volatile = std::conditional_t<
        is_volatile<T>,
        std::conditional_t<
            meta::lvalue<T>,
            meta::as_lvalue<std::remove_volatile_t<meta::remove_reference<T>>>,
            std::conditional_t<
                meta::rvalue<T>,
                meta::as_rvalue<std::remove_volatile_t<meta::remove_reference<T>>>,
                std::remove_volatile_t<meta::remove_reference<T>>
            >
        >,
        T
    >;

    template <typename T>
    concept is_cv = is_const<T> || is_volatile<T>;

    template <typename T>
    using remove_cv = remove_volatile<remove_const<T>>;

    template <typename T>
    concept is_void = std::is_void_v<std::remove_cvref_t<T>>;

    template <typename T>
    concept not_void = !is_void<T>;

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

    template <typename T>
    concept qualified = is_const<T> || is_volatile<T> || reference<T>;

    template <typename T>
    concept unqualified = !qualified<T>;

    template <typename L, typename R>
    using qualify = detail::qualify<L, R>::type;

    template <typename T>
    using unqualify = std::remove_cvref_t<T>;

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
        !std::same_as<remove_reference<T>, remove_reference<U>> &&
        detail::more_qualified_than<remove_reference<T>, remove_reference<U>>;

    template <typename... Ts>
    concept has_common_type =
        (sizeof...(Ts) > 0) && requires { typename std::common_reference<Ts...>::type; };

    template <typename... Ts> requires (has_common_type<Ts...>)
    using common_type = std::common_reference<Ts...>::type;

    //////////////////////////
    ////    PRIMITIVES    ////
    //////////////////////////

    template <typename T>
    concept boolean = std::same_as<unqualify<T>, bool>;

    template <typename T>
    concept integer = std::integral<unqualify<T>>;

    template <typename T>
    concept signed_integer = integer<T> && std::signed_integral<unqualify<T>>;

    template <integer T>
    using as_signed = qualify<std::make_signed_t<unqualify<T>>, T>;

    template <typename T>
    concept int8 = signed_integer<T> && sizeof(unqualify<T>) == 1;

    template <typename T>
    concept int16 = signed_integer<T> && sizeof(unqualify<T>) == 2;

    template <typename T>
    concept int32 = signed_integer<T> && sizeof(unqualify<T>) == 4;

    template <typename T>
    concept int64 = signed_integer<T> && sizeof(unqualify<T>) == 8;

    template <typename T>
    concept int128 = signed_integer<T> && sizeof(unqualify<T>) == 16;

    template <typename T>
    concept unsigned_integer = integer<T> && std::unsigned_integral<unqualify<T>>;

    template <integer T>
    using as_unsigned = qualify<std::make_unsigned_t<unqualify<T>>, T>;

    template <typename T>
    concept uint8 = unsigned_integer<T> && sizeof(unqualify<T>) == 1;

    template <typename T>
    concept uint16 = unsigned_integer<T> && sizeof(unqualify<T>) == 2;

    template <typename T>
    concept uint32 = unsigned_integer<T> && sizeof(unqualify<T>) == 4;

    template <typename T>
    concept uint64 = unsigned_integer<T> && sizeof(unqualify<T>) == 8;

    template <typename T>
    concept uint128 = unsigned_integer<T> && sizeof(unqualify<T>) == 16;

    template <typename T>
    concept floating = std::floating_point<unqualify<T>>;

    template <typename T>
    concept float8 = floating<T> && sizeof(unqualify<T>) == 1;

    template <typename T>
    concept float16 = floating<T> && sizeof(unqualify<T>) == 2;

    template <typename T>
    concept float32 = floating<T> && sizeof(unqualify<T>) == 4;

    template <typename T>
    concept float64 = floating<T> && sizeof(unqualify<T>) == 8;

    template <typename T>
    concept float128 = floating<T> && sizeof(unqualify<T>) == 16;

    template <typename T>
    concept string_literal = requires(T t) { []<size_t N>(const char(&)[N]){}(t); };

    template <string_literal T>
    constexpr size_t string_literal_size = sizeof(T) - 1;

    template <typename T>
    concept raw_array = std::is_array_v<remove_reference<T>>;

    template <typename T>
    concept raw_bounded_array = std::is_bounded_array_v<remove_reference<T>>;

    template <typename T>
    concept raw_unbounded_array = std::is_unbounded_array_v<remove_reference<T>>;

    template <typename T>
    concept raw_enum = std::is_enum_v<unqualify<T>>;

    template <typename T>
    concept scoped_enum = std::is_scoped_enum_v<unqualify<T>>;

    template <typename T> requires (raw_enum<T> || scoped_enum<T>)
    using enum_type = std::underlying_type_t<unqualify<T>>;

    template <typename T>
    concept raw_union = std::is_union_v<unqualify<T>>;

    template <typename T>
    concept has_members = !std::is_empty_v<unqualify<T>>;

    template <typename T>
    concept no_members = std::is_empty_v<unqualify<T>>;

    template <typename T>
    concept is_virtual = std::is_polymorphic_v<unqualify<T>>;

    template <typename T>
    concept is_abstract = std::is_abstract_v<unqualify<T>>;

    template <typename T>
    concept is_final = std::is_final_v<unqualify<T>>;

    template <typename T>
    concept is_aggregate = std::is_aggregate_v<unqualify<T>>;

    ////////////////////////////
    ////    CONSTRUCTION    ////
    ////////////////////////////

    template <typename T, typename... Args>
    concept constructible_from = std::constructible_from<T, Args...>;

    template <typename T, typename... Args>
    concept implicitly_constructible_from =
        constructible_from<T, Args...> && requires(Args... args) {
            [](Args... args) -> T { return {std::forward<Args>(args)...}; };
        };

    template <typename T>
    concept default_constructible = std::default_initializable<T>;

    template <typename T, typename... Args>
    concept trivially_constructible = std::is_trivially_constructible_v<T, Args...>;

    namespace nothrow {

        template <typename T, typename... Args>
        concept implicitly_constructible_from =
            meta::implicitly_constructible_from<T, Args...> &&
            std::is_nothrow_constructible_v<T, Args...>;

        template <typename T, typename... Args>
        concept constructible_from =
            meta::constructible_from<T, Args...> &&
            std::is_nothrow_constructible_v<T, Args...>;

        template <typename T>
        concept default_constructible =
            meta::default_constructible<T> &&
            std::is_nothrow_default_constructible_v<T>;

        template <typename T, typename... Args>
        concept trivially_constructible =
            meta::trivially_constructible<T, Args...> &&
            std::is_nothrow_constructible_v<T, Args...>;

    }

    template <typename L, typename R>
    concept convertible_to = std::convertible_to<L, R>;

    template <typename L, typename R>
    concept explicitly_convertible_to = requires(L from) {
        { static_cast<R>(from) } -> std::same_as<R>;
    };

    namespace nothrow {

        template <typename L, typename R>
        concept convertible_to =
            meta::convertible_to<L, R> &&
            std::is_nothrow_convertible_v<L, R>;

        template <typename L, typename R>
        concept explicitly_convertible_to =
            meta::explicitly_convertible_to<L, R> &&
            noexcept(static_cast<R>(std::declval<L>()));

    }

    template <typename L, typename R>
    concept assignable = std::assignable_from<L, R>;

    template <typename L, typename R>
    concept trivially_assignable = std::is_trivially_assignable_v<L, R>;

    template <typename L, typename R> requires (assignable<L, R>)
    using assign_type = decltype(std::declval<L>() = std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept assign_returns = assignable<L, R> &&
        convertible_to<assign_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept assignable =
            meta::assignable<L, R> &&
            std::is_nothrow_assignable_v<L, R>;

        template <typename L, typename R>
        concept trivially_assignable =
            meta::trivially_assignable<L, R> &&
            std::is_nothrow_assignable_v<L, R>;

        template <typename L, typename R> requires (assignable<L, R>)
        using assign_type = meta::assign_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept assign_returns =
            meta::assign_returns<L, R, Ret> &&
            std::is_nothrow_assignable_v<L, R> &&
            convertible_to<assign_type<L, R>, Ret>;

    }

    template <typename T>
    concept copyable = std::copy_constructible<T>;

    template <typename T>
    concept trivially_copyable = std::is_trivially_copyable_v<T>;

    template <typename T>
    concept copy_assignable = std::is_copy_assignable_v<T>;

    template <typename T>
    concept trivially_copy_assignable = std::is_trivially_copy_assignable_v<T>;

    template <copy_assignable T>
    using copy_assign_type = decltype(std::declval<T>() = std::declval<T>());

    template <typename Ret, typename T>
    concept copy_assign_returns =
        copy_assignable<T> &&
        convertible_to<copy_assign_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept copyable =
            meta::copyable<T> &&
            std::is_nothrow_copy_constructible_v<T>;

        template <typename T>
        concept trivially_copyable =
            meta::trivially_copyable<T> &&
            std::is_nothrow_copy_constructible_v<T>;

        template <typename T>
        concept copy_assignable =
            meta::copy_assignable<T> &&
            std::is_nothrow_copy_assignable_v<T>;

        template <typename T>
        concept trivially_copy_assignable =
            meta::trivially_copy_assignable<T> &&
            std::is_nothrow_copy_assignable_v<T>;

        template <copy_assignable T>
        using copy_assign_type = meta::copy_assign_type<T>;

        template <typename Ret, typename T>
        concept copy_assign_returns =
            meta::copy_assign_returns<T, Ret> &&
            std::is_nothrow_copy_assignable_v<T> &&
            convertible_to<copy_assign_type<T>, Ret>;

    }

    template <typename T>
    concept movable = std::move_constructible<T>;

    template <typename T>
    concept trivially_movable = std::is_trivially_move_constructible_v<T>;

    template <typename T>
    concept move_assignable = std::is_move_assignable_v<T>;

    template <typename T>
    concept trivially_move_assignable = std::is_trivially_move_assignable_v<T>;

    template <move_assignable T>
    using move_assign_type = decltype(std::declval<T>() = std::declval<T>());

    template <typename Ret, typename T>
    concept move_assign_returns = move_assignable<T> &&
        convertible_to<move_assign_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept movable =
            meta::movable<T> &&
            std::is_nothrow_move_constructible_v<T>;

        template <typename T>
        concept trivially_movable =
            meta::trivially_movable<T> &&
            std::is_nothrow_move_constructible_v<T>;

        template <typename T>
        concept move_assignable =
            meta::move_assignable<T> &&
            std::is_nothrow_move_assignable_v<T>;

        template <typename T>
        concept trivially_move_assignable =
            meta::trivially_move_assignable<T> &&
            std::is_nothrow_move_assignable_v<T>;

        template <move_assignable T>
        using move_assign_type = meta::move_assign_type<T>;

        template <typename Ret, typename T>
        concept move_assign_returns =
            meta::move_assign_returns<T, Ret> &&
            std::is_nothrow_move_assignable_v<T> &&
            convertible_to<move_assign_type<T>, Ret>;

    }

    template <typename T>
    concept swappable = std::swappable<T>;

    template <typename T, typename U>
    concept swappable_with = std::swappable_with<T, U>;

    namespace nothrow {

        template <typename T>
        concept swappable = meta::swappable<T> &&
            noexcept(std::ranges::swap(
                std::declval<meta::as_lvalue<T>>(),
                std::declval<meta::as_lvalue<T>>()
            ));

        template <typename T, typename U>
        concept swappable_with =
            meta::swappable_with<T, U> &&
            noexcept(std::ranges::swap(
                std::declval<meta::as_lvalue<T>>(),
                std::declval<meta::as_lvalue<T>>()
            )) &&
            noexcept(std::ranges::swap(
                std::declval<meta::as_lvalue<T>>(),
                std::declval<meta::as_lvalue<U>>()
            )) &&
            noexcept(std::ranges::swap(
                std::declval<meta::as_lvalue<U>>(),
                std::declval<meta::as_lvalue<T>>()
            )) &&
            noexcept(std::ranges::swap(
                std::declval<meta::as_lvalue<U>>(),
                std::declval<meta::as_lvalue<U>>()
            ));

    }

    template <typename T>
    concept destructible = std::destructible<T>;

    template <typename T>
    concept trivially_destructible = std::is_trivially_destructible_v<T>;

    template <typename T>
    concept virtually_destructible = std::has_virtual_destructor_v<T>;

    namespace nothrow {

        template <typename T>
        concept destructible =
            meta::destructible<T> &&
            std::is_nothrow_destructible_v<T>;

        template <typename T>
        concept trivially_destructible =
            meta::trivially_destructible<T> &&
            std::is_nothrow_destructible_v<T>;

        template <typename T>
        concept virtually_destructible =
            meta::virtually_destructible<T> &&
            std::is_nothrow_destructible_v<T>;

    }

    //////////////////////////
    ////    INVOCATION    ////
    //////////////////////////

    template <typename T>
    concept function_signature =
        std::is_function_v<meta::remove_pointer<meta::unqualify<T>>>;

    template <typename T>
    concept function_pointer = pointer<T> && function_signature<remove_pointer<T>>;

    template <typename T>
    concept has_call_operator = requires() { &unqualify<T>::operator(); };

    template <typename F, typename... A>
    concept invocable = std::invocable<F, A...>;

    template <typename F, typename... A> requires (invocable<F, A...>)
    using invoke_type = std::invoke_result_t<F, A...>;

    template <typename R, typename F, typename... A>
    concept invoke_returns = invocable<F, A...> && convertible_to<invoke_type<F, A...>, R>;

    namespace nothrow {

        template <typename F, typename... A>
        concept invocable =
            meta::invocable<F, A...> && std::is_nothrow_invocable_v<F, A...>;

        template <typename F, typename... A> requires (invocable<F, A...>)
        using invoke_type = meta::invoke_type<F, A...>;

        template <typename R, typename F, typename... A>
        concept invoke_returns =
            invocable<F, A...> && convertible_to<invoke_type<F, A...>, R>;

    }

    ///////////////////////
    ////    MEMBERS    ////
    ///////////////////////

    namespace detail {

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
    concept member_object = std::is_member_object_pointer_v<remove_reference<T>>;

    template <typename T, typename C>
    concept member_object_of =
        member_object<T> && detail::member_object_of<unqualify<T>, C>;

    namespace detail {

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
    concept member_function = std::is_member_function_pointer_v<remove_reference<T>>;

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
        std::conditional_t<
            function_pointer<T>,
            remove_pointer<T>,
            remove_reference<T>
        >,
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

    template <typename T, auto... I>
    concept has_member_get = requires(T t) {
        t.template get<I...>();
    };

    template <typename T, auto... I> requires (has_member_get<T, I...>)
    using member_get_type = decltype(std::declval<T>().template get<I...>());

    template <typename Ret, typename T, auto... I>
    concept member_get_returns =
        has_member_get<T, I...> && convertible_to<member_get_type<T, I...>, Ret>;

    template <typename T, auto... I>
    concept has_adl_get = requires(T t) { get<I...>(t); };

    template <typename T, auto... I> requires (has_adl_get<T, I...>)
    using adl_get_type = decltype(get<I...>(std::declval<T>()));

    template <typename Ret, typename T, auto... I>
    concept adl_get_returns =
        has_adl_get<T, I...> && convertible_to<adl_get_type<T, I...>, Ret>;

    template <typename T, auto... I>
    concept has_std_get = requires(T t) {
        std::get<I...>(t);
    };

    template <typename T, auto... I> requires (has_std_get<T, I...>)
    using std_get_type = decltype(std::get<I...>(std::declval<T>()));

    template <typename Ret, typename T, auto... I>
    concept std_get_returns =
        has_std_get<T, I...> && convertible_to<std_get_type<T, I...>, Ret>;

    namespace nothrow {

        template <typename T, auto... I>
        concept has_member_get =
            meta::has_member_get<T, I...> &&
            noexcept(std::declval<T>().template get<I...>());

        template <typename T, auto... I> requires (has_member_get<T, I...>)
        using member_get_type = meta::member_get_type<T, I...>;

        template <typename Ret, typename T, auto... I>
        concept member_get_returns =
            has_member_get<T, I...> && convertible_to<member_get_type<T, I...>, Ret>;

        template <typename T, auto... I>
        concept has_adl_get =
            meta::has_adl_get<T, I...> &&
            noexcept(get<I...>(std::declval<T>()));

        template <typename T, auto... I> requires (has_adl_get<T, I...>)
        using adl_get_type = meta::adl_get_type<T, I...>;

        template <typename Ret, typename T, auto... I>
        concept adl_get_returns =
            has_adl_get<T, I...> && convertible_to<adl_get_type<T, I...>, Ret>;

        template <typename T, auto... I>
        concept has_std_get =
            meta::has_std_get<T, I...> &&
            noexcept(std::get<I...>(std::declval<T>()));

        template <typename T, auto... I> requires (has_std_get<T, I...>)
        using std_get_type = meta::std_get_type<T, I...>;

        template <typename Ret, typename T, auto... I>
        concept std_get_returns =
            has_std_get<T, I...> && convertible_to<std_get_type<T, I...>, Ret>;

    }

    namespace detail {

        template <typename T, auto... I>
        struct get_type { using type = member_get_type<T, I...>; };
        template <typename T, auto... I> requires (
            !meta::has_member_get<T, I...> &&
            meta::has_adl_get<T, I...>
        )
        struct get_type<T, I...> { using type = adl_get_type<T, I...>; };
        template <typename T, auto... I> requires (
            !meta::has_member_get<T, I...> &&
            !meta::has_adl_get<T, I...> &&
            meta::has_std_get<T, I...>
        )
        struct get_type<T, I...> { using type = std_get_type<T, I...>; };

    }

    template <typename T, auto... I>
    concept has_get =
        has_member_get<T, I...> || has_adl_get<T, I...> || has_std_get<T, I...>;

    template <typename T, auto... I> requires (has_get<T, I...>)
    using get_type = typename detail::get_type<T, I...>::type;

    template <typename Ret, typename T, auto... I>
    concept get_returns = has_get<T, I...> && convertible_to<get_type<T, I...>, Ret>;

    namespace nothrow {

        template <typename T, auto... I>
        concept has_get = meta::has_get<T, I...> && (
            meta::has_member_get<T, I...> ||
            meta::has_adl_get<T, I...> ||
            meta::has_std_get<T, I...>
        );

        template <typename T, auto... I> requires (has_get<T, I...>)
        using get_type = meta::get_type<T, I...>;

        template <typename Ret, typename T, auto... I>
        concept get_returns =
            has_get<T, I...> && convertible_to<get_type<T, I...>, Ret>;

    }

    namespace detail {

        template <typename T, size_t N>
        struct structured {
            template <size_t I, typename Dummy = void>
            static constexpr bool _value = meta::has_get<T, I - 1> && _value<I - 1>;
            template <typename Dummy>
            static constexpr bool _value<0, Dummy> = true;

            static constexpr bool value = _value<N>;
        };

    }

    template <typename T, size_t N>
    concept structured =
        std::tuple_size<unqualify<T>>::value == N && detail::structured<T, N>::value;

    template <typename T>
    concept tuple_like = structured<T, std::tuple_size<unqualify<T>>::value>;

    template <tuple_like T>
    constexpr size_t tuple_size = std::tuple_size<unqualify<T>>::value;

    template <typename T>
    concept pair = structured<T, 2>;

    namespace detail {

        template <typename T>
        struct common_tuple_type;
        template <meta::tuple_like T>
        struct common_tuple_type<T> {
            static constexpr size_t size = meta::tuple_size<T>;

            template <size_t I, typename... Ts>
            struct filter {
                static constexpr bool value =
                    filter<I + 1, Ts..., meta::get_type<T, I>>::value;
                using type = filter<I + 1, Ts..., meta::get_type<T, I>>::type;
            };

            template <size_t I, typename... Ts> requires (I >= size)
            struct filter<I, Ts...> {
                template <typename... Us>
                struct do_filter {
                    static constexpr bool value = false;
                    using type = void;
                };
                template <typename... Us> requires (meta::has_common_type<Us...>)
                struct do_filter<Us...> {
                    static constexpr bool value = true;
                    using type = meta::common_type<Us...>;
                };
                static constexpr bool value = do_filter<Ts...>::value;
                using type = do_filter<Ts...>::type;
            };

            static constexpr bool value = filter<0>::value;
            using type = filter<0>::type;
        };

        template <typename T, typename... Ts>
        struct structured_with {
            template <size_t I, typename... Us>
            static constexpr bool _value = true;

            template <size_t I, typename U, typename... Us>
            static constexpr bool _value<I, U, Us...> = (
                requires(T t) { { get<I>(t) } -> meta::convertible_to<U>; } ||
                requires(T t) { { std::get<I>(t) } -> meta::convertible_to<U>; }
            ) && _value<I + 1, Us...>;

            static constexpr bool value = _value<0, Ts...>;
        };

    }

    template <typename T>
    concept has_common_tuple_type =
        tuple_like<T> && detail::common_tuple_type<T>::value;

    template <has_common_tuple_type T>
    using common_tuple_type = detail::common_tuple_type<T>::type;

    template <typename T, typename... Ts>
    concept structured_with =
        structured<T, sizeof...(Ts)> && detail::structured_with<T, Ts...>::value;

    template <typename T, typename First, typename Second>
    concept pair_with = structured_with<T, First, Second>;

    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    template <typename T>
    concept iterator = std::input_or_output_iterator<T>;

    template <typename T, typename V>
    concept output_iterator = iterator<T> && std::output_iterator<T, V>;

    template <typename T>
    concept forward_iterator = std::forward_iterator<T>;

    template <typename T>
    concept bidirectional_iterator = std::bidirectional_iterator<T>;

    template <typename T>
    concept random_access_iterator = std::random_access_iterator<T>;

    template <typename T>
    concept contiguous_iterator = std::contiguous_iterator<T>;

    template <typename T, typename Iter>
    concept sentinel_for = std::sentinel_for<T, Iter>;

    template <typename T, typename Iter>
    concept sized_sentinel_for = sentinel_for<T, Iter> && std::sized_sentinel_for<T, Iter>;

    template <typename T>
    concept has_begin = requires(T& t) { std::ranges::begin(t); };

    template <has_begin T>
    using begin_type = decltype(std::ranges::begin(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_cbegin = requires(T& t) { std::ranges::cbegin(t); };

    template <has_cbegin T>
    using cbegin_type = decltype(std::ranges::cbegin(std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_begin =
            meta::has_begin<T> &&
            noexcept(std::ranges::begin(std::declval<as_lvalue<T>>()));

        template <has_begin T>
        using begin_type = meta::begin_type<T>;

        template <typename T>
        concept has_cbegin =
            meta::has_cbegin<T> &&
            noexcept(std::ranges::cbegin(std::declval<as_lvalue<T>>()));

        template <has_cbegin T>
        using cbegin_type = meta::cbegin_type<T>;

    }

    template <typename T>
    concept has_end = requires(T& t) { std::ranges::end(t); };

    template <has_end T>
    using end_type = decltype(std::ranges::end(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_cend = requires(T& t) { std::ranges::cend(t); };

    template <has_cend T>
    using cend_type = decltype(std::ranges::cend(std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_end =
            meta::has_end<T> &&
            noexcept(std::ranges::end(std::declval<as_lvalue<T>>()));

        template <has_end T>
        using end_type = meta::end_type<T>;

        template <typename T>
        concept has_cend =
            meta::has_cend<T> &&
            noexcept(std::ranges::cend(std::declval<as_lvalue<T>>()));

        template <has_cend T>
        using cend_type = meta::cend_type<T>;

    }

    template <typename T>
    concept has_rbegin = requires(T& t) { std::ranges::rbegin(t); };

    template <has_rbegin T>
    using rbegin_type = decltype(std::ranges::rbegin(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_crbegin = requires(T& t) { std::ranges::crbegin(t); };

    template <has_crbegin T>
    using crbegin_type = decltype(std::ranges::crbegin(std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_rbegin =
            meta::has_rbegin<T> &&
            noexcept(std::ranges::rbegin(std::declval<as_lvalue<T>>()));

        template <has_rbegin T>
        using rbegin_type = meta::rbegin_type<T>;

        template <typename T>
        concept has_crbegin =
            meta::has_crbegin<T> &&
            noexcept(std::ranges::crbegin(std::declval<as_lvalue<T>>()));

        template <has_crbegin T>
        using crbegin_type = meta::crbegin_type<T>;

    }

    template <typename T>
    concept has_rend = requires(T& t) { std::ranges::rend(t); };

    template <has_rend T>
    using rend_type = decltype(std::ranges::rend(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_crend = requires(T& t) { std::ranges::crend(t); };

    template <has_crend T>
    using crend_type = decltype(std::ranges::crend(std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_rend =
            meta::has_rend<T> &&
            noexcept(std::ranges::rend(std::declval<as_lvalue<T>>()));

        template <has_rend T>
        using rend_type = meta::rend_type<T>;

        template <typename T>
        concept has_crend =
            meta::has_crend<T> &&
            noexcept(std::ranges::crend(std::declval<as_lvalue<T>>()));

        template <has_crend T>
        using crend_type = meta::crend_type<T>;

    }

    template <typename T>
    concept iterable = requires(T& t) {
        { std::ranges::begin(t) } -> iterator;
        { std::ranges::end(t) } -> sentinel_for<decltype(std::ranges::begin(t))>;
    };

    template <iterable T>
    using yield_type =
        decltype(*std::ranges::begin(std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept yields = iterable<T> && convertible_to<yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept iterable =
            meta::iterable<T> && has_begin<T> && has_end<T>;

        template <iterable T>
        using yield_type = meta::yield_type<T>;

        template <typename T, typename Ret>
        concept yields = iterable<T> && convertible_to<yield_type<T>, Ret>;

    }

    template <typename T>
    concept const_iterable = requires(T& t) {
        { std::ranges::cbegin(t) } -> iterator;
        { std::ranges::cend(t) } -> sentinel_for<decltype(std::ranges::cbegin(t))>;
    };

    template <const_iterable T>
    using const_yield_type =
        decltype(*std::ranges::cbegin(std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept const_yields =
        const_iterable<T> && convertible_to<const_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept const_iterable =
            meta::const_iterable<T> && has_cbegin<T> && has_cend<T>;

        template <const_iterable T>
        using const_yield_type = meta::const_yield_type<T>;

        template <typename T, typename Ret>
        concept const_yields =
            const_iterable<T> && convertible_to<const_yield_type<T>, Ret>;

    }

    template <typename T>
    concept reverse_iterable = requires(T& t) {
        { std::ranges::rbegin(t) } -> iterator;
        { std::ranges::rend(t) } -> sentinel_for<decltype(std::ranges::rbegin(t))>;
    };

    template <reverse_iterable T>
    using reverse_yield_type =
        decltype(*std::ranges::rbegin(std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept reverse_yields =
        reverse_iterable<T> && convertible_to<reverse_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept reverse_iterable =
            meta::reverse_iterable<T> && has_rbegin<T> && has_rend<T>;

        template <reverse_iterable T>
        using reverse_yield_type = meta::reverse_yield_type<T>;

        template <typename T, typename Ret>
        concept reverse_yields =
            reverse_iterable<T> && convertible_to<reverse_yield_type<T>, Ret>;

    }

    template <typename T>
    concept const_reverse_iterable = requires(T& t) {
        { std::ranges::crbegin(t) } -> iterator;
        { std::ranges::crend(t) } -> sentinel_for<decltype(std::ranges::crbegin(t))>;
    };

    template <const_reverse_iterable T>
    using const_reverse_yield_type =
        decltype(*std::ranges::crbegin(std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept const_reverse_yields =
        const_reverse_iterable<T> && convertible_to<const_reverse_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept const_reverse_iterable =
            meta::const_reverse_iterable<T> && has_crbegin<T> && has_crend<T>;

        template <const_reverse_iterable T>
        using const_reverse_yield_type = meta::const_reverse_yield_type<T>;

        template <typename T, typename Ret>
        concept const_reverse_yields =
            const_reverse_iterable<T> && convertible_to<const_reverse_yield_type<T>, Ret>;

    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    template <typename T, typename... Key>
    concept indexable = !integer<T> && requires(T t, Key... key) {
        std::forward<T>(t)[std::forward<Key>(key)...];
    };

    template <typename T, typename... Key> requires (indexable<T, Key...>)
    using index_type = decltype(std::declval<T>()[std::declval<Key>()...]);

    template <typename Ret, typename T, typename... Key>
    concept index_returns =
        indexable<T, Key...> && convertible_to<index_type<T, Key...>, Ret>;

    namespace nothrow {

        template <typename T, typename... Key>
        concept indexable =
            meta::indexable<T, Key...> &&
            noexcept(std::declval<T>()[std::declval<Key>()...]);

        template <typename T, typename... Key> requires (indexable<T, Key...>)
        using index_type = meta::index_type<T, Key...>;

        template <typename Ret, typename T, typename... Key>
        concept index_returns =
            indexable<T, Key...> && convertible_to<index_type<T, Key...>, Ret>;

    }

    template <typename T, typename Value, typename... Key>
    concept index_assignable =
        !integer<T> && requires(T t, Key... key, Value value) {
            { std::forward<T>(t)[std::forward<Key>(key)...] = std::forward<Value>(value) };
        };

    template <typename T, typename Value, typename... Key>
        requires (index_assignable<T, Value, Key...>)
    using index_assign_type = decltype(
        std::declval<T>()[std::declval<Key>()...] = std::declval<Value>()
    );

    template <typename Ret, typename T, typename Value, typename... Key>
    concept index_assign_returns =
        index_assignable<T, Value, Key...> &&
        convertible_to<index_assign_type<T, Value, Key...>, Ret>;

    namespace nothrow {

        template <typename T, typename Value, typename... Key>
        concept index_assignable =
            meta::index_assignable<T, Value, Key...> &&
            noexcept(std::declval<T>()[std::declval<Key>()...] = std::declval<Value>());

        template <typename T, typename Value, typename... Key>
            requires (index_assignable<T, Value, Key...>)
        using index_assign_type = meta::index_assign_type<T, Value, Key...>;

        template <typename Ret, typename T, typename Value, typename... Key>
        concept index_assign_returns =
            index_assignable<T, Value, Key...> &&
            convertible_to<index_assign_type<T, Value, Key...>, Ret>;

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
            noexcept((static_cast<bool>(std::declval<T>())));

    }

    template <typename T>
    concept addressable = requires(T t) {
        { &t } -> std::convertible_to<as_pointer<T>>;
    };

    template <addressable T>
    using address_type = decltype(&std::declval<T>());

    template <typename T, typename Ret>
    concept address_returns =
        addressable<T> && convertible_to<address_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept addressable = meta::addressable<T> && noexcept(&std::declval<T>());

        template <addressable T>
        using address_type = meta::address_type<T>;

        template <typename T, typename Ret>
        concept address_returns =
            addressable<T> && convertible_to<address_type<T>, Ret>;

    }

    template <typename T>
    concept dereferenceable = requires(T t) { *t; };

    template <dereferenceable T>
    using dereference_type = decltype((*std::declval<T>()));

    template <typename T, typename Ret>
    concept dereferences_to =
        dereferenceable<T> && convertible_to<dereference_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept dereferenceable =
            meta::dereferenceable<T> &&
            noexcept((*std::declval<T>()));

        template <dereferenceable T>
        using dereference_type = meta::dereference_type<T>;

        template <typename T, typename Ret>
        concept dereferences_to =
            dereferenceable<T> && convertible_to<dereference_type<T>, Ret>;

    }

    template <typename T>
    concept has_arrow = requires(T t) {
        { t.operator->() } -> meta::pointer;
    };

    template <has_arrow T>
    using arrow_type = decltype((std::declval<T>().operator->()));

    template <typename T, typename Ret>
    concept arrow_returns = has_arrow<T> && convertible_to<arrow_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_arrow =
            meta::has_arrow<T> && noexcept(std::declval<T>().operator->());

        template <has_arrow T>
        using arrow_type = meta::arrow_type<T>;

        template <typename T, typename Ret>
        concept arrow_returns =
            has_arrow<T> && convertible_to<arrow_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_arrow_dereference = requires(L l, R r) { l->*r; };

    template <typename L, typename R> requires (has_arrow_dereference<L, R>)
    using arrow_dereference_type = decltype((std::declval<L>()->*std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept arrow_dereference_returns =
        has_arrow_dereference<L, R> && convertible_to<arrow_dereference_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_arrow_dereference =
            meta::has_arrow_dereference<L, R> &&
            noexcept(std::declval<L>()->*std::declval<R>());

        template <typename L, typename R> requires (has_arrow_dereference<L, R>)
        using arrow_dereference_type = meta::arrow_dereference_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept arrow_dereference_returns =
            has_arrow_dereference<L, R> &&
            convertible_to<arrow_dereference_type<L, R>, Ret>;

    }

    template <typename T>
    concept has_invert = requires(T t) { ~t; };

    template <has_invert T>
    using invert_type = decltype((~std::declval<T>()));

    template <typename Ret, typename T>
    concept invert_returns = has_invert<T> && convertible_to<invert_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_invert =
            meta::has_invert<T> && noexcept((~std::declval<T>()));

        template <has_invert T>
        using invert_type = meta::invert_type<T>;

        template <typename Ret, typename T>
        concept invert_returns = has_invert<T> && convertible_to<invert_type<T>, Ret>;

    }

    template <typename T>
    concept has_pos = requires(T t) { +t; };

    template <has_pos T>
    using pos_type = decltype((+std::declval<T>()));

    template <typename Ret, typename T>
    concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_pos =
            meta::has_pos<T> && noexcept((+std::declval<T>()));

        template <has_pos T>
        using pos_type = meta::pos_type<T>;

        template <typename Ret, typename T>
        concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Ret>;

    }

    template <typename T>
    concept has_neg = requires(T t) { -t; };

    template <has_neg T>
    using neg_type = decltype((-std::declval<T>()));

    template <typename Ret, typename T>
    concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_neg =
            meta::has_neg<T> && noexcept((-std::declval<T>()));

        template <has_neg T>
        using neg_type = meta::neg_type<T>;

        template <typename Ret, typename T>
        concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Ret>;

    }

    template <typename T>
    concept has_preincrement = requires(T t) { ++t; };

    template <has_preincrement T>
    using preincrement_type = decltype((++std::declval<T>()));

    template <typename Ret, typename T>
    concept preincrement_returns =
        has_preincrement<T> && convertible_to<preincrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_preincrement =
            meta::has_preincrement<T> && noexcept((++std::declval<T>()));

        template <has_preincrement T>
        using preincrement_type = meta::preincrement_type<T>;

        template <typename Ret, typename T>
        concept preincrement_returns =
            has_preincrement<T> && convertible_to<preincrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_postincrement = requires(T t) { t++; };

    template <has_postincrement T>
    using postincrement_type = decltype((std::declval<T>()++));

    template <typename Ret, typename T>
    concept postincrement_returns =
        has_postincrement<T> && convertible_to<postincrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_postincrement =
            meta::has_postincrement<T> && noexcept((std::declval<T>()++));

        template <has_postincrement T>
        using postincrement_type = meta::postincrement_type<T>;

        template <typename Ret, typename T>
        concept postincrement_returns =
            has_postincrement<T> && convertible_to<postincrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_predecrement = requires(T t) { --t; };

    template <has_predecrement T>
    using predecrement_type = decltype((--std::declval<T>()));

    template <typename Ret, typename T>
    concept predecrement_returns =
        has_predecrement<T> && convertible_to<predecrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_predecrement =
            meta::has_predecrement<T> && noexcept((--std::declval<T>()));

        template <has_predecrement T>
        using predecrement_type = meta::predecrement_type<T>;

        template <typename Ret, typename T>
        concept predecrement_returns =
            has_predecrement<T> && convertible_to<predecrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_postdecrement = requires(T t) { t--; };

    template <has_postdecrement T>
    using postdecrement_type = decltype((std::declval<T>()--));

    template <typename Ret, typename T>
    concept postdecrement_returns =
        has_postdecrement<T> && convertible_to<postdecrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_postdecrement =
            meta::has_postdecrement<T> && noexcept((std::declval<T>()--));

        template <has_postdecrement T>
        using postdecrement_type = meta::postdecrement_type<T>;

        template <typename Ret, typename T>
        concept postdecrement_returns =
            has_postdecrement<T> && convertible_to<postdecrement_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_lt = requires(L l, R r) { l < r; };

    template <typename L, typename R> requires (has_lt<L, R>)
    using lt_type = decltype((std::declval<L>() < std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_lt =
            meta::has_lt<L, R> && noexcept((std::declval<L>() < std::declval<R>()));

        template <typename L, typename R> requires (has_lt<L, R>)
        using lt_type = meta::lt_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_le = requires(L l, R r) { l <= r; };

    template <typename L, typename R> requires (has_le<L, R>)
    using le_type = decltype((std::declval<L>() <= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_le =
            meta::has_le<L, R> && noexcept((std::declval<L>() <= std::declval<R>()));

        template <typename L, typename R> requires (has_le<L, R>)
        using le_type = meta::le_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_eq = requires(L l, R r) { l == r; };

    template <typename L, typename R> requires (has_eq<L, R>)
    using eq_type = decltype((std::declval<L>() == std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_eq =
            meta::has_eq<L, R> && noexcept((std::declval<L>() == std::declval<R>()));

        template <typename L, typename R> requires (has_eq<L, R>)
        using eq_type = meta::eq_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ne = requires(L l, R r) { l != r; };

    template <typename L, typename R> requires (has_ne<L, R>)
    using ne_type = decltype((std::declval<L>() != std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ne =
            meta::has_ne<L, R> && noexcept((std::declval<L>() != std::declval<R>()));

        template <typename L, typename R> requires (has_ne<L, R>)
        using ne_type = meta::ne_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ge = requires(L l, R r) { l >= r; };

    template <typename L, typename R> requires (has_ge<L, R>)
    using ge_type = decltype((std::declval<L>() >= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ge =
            meta::has_ge<L, R> && noexcept((std::declval<L>() >= std::declval<R>()));

        template <typename L, typename R> requires (has_ge<L, R>)
        using ge_type = meta::ge_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_gt = requires(L l, R r) { l > r; };

    template <typename L, typename R> requires (has_gt<L, R>)
    using gt_type = decltype((std::declval<L>() > std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_gt =
            meta::has_gt<L, R> && noexcept((std::declval<L>() > std::declval<R>()));

        template <typename L, typename R> requires (has_gt<L, R>)
        using gt_type = meta::gt_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_spaceship = requires(L l, R r) { l <=> r; };

    template <typename L, typename R> requires (has_spaceship<L, R>)
    using spaceship_type =
        decltype((std::declval<L>() <=> std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept spaceship_returns =
        has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_spaceship =
            meta::has_spaceship<L, R> &&
            noexcept((std::declval<L>() <=> std::declval<R>()));

        template <typename L, typename R> requires (has_spaceship<L, R>)
        using spaceship_type = meta::spaceship_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept spaceship_returns =
            has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_add = requires(L l, R r) { l + r; };

    template <typename L, typename R> requires (has_add<L, R>)
    using add_type = decltype((std::declval<L>() + std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_add =
            meta::has_add<L, R> && noexcept((std::declval<L>() + std::declval<R>()));

        template <typename L, typename R> requires (has_add<L, R>)
        using add_type = meta::add_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_iadd = requires(L& l, R r) { l += r; };

    template <typename L, typename R> requires (has_iadd<L, R>)
    using iadd_type = decltype((std::declval<L&>() += std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept iadd_returns = has_iadd<L, R> && convertible_to<iadd_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_iadd =
            meta::has_iadd<L, R> && noexcept((std::declval<L&>() += std::declval<R>()));

        template <typename L, typename R> requires (has_iadd<L, R>)
        using iadd_type = meta::iadd_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept iadd_returns =
            has_iadd<L, R> && convertible_to<iadd_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_sub = requires(L l, R r) {{ l - r };};

    template <typename L, typename R> requires (has_sub<L, R>)
    using sub_type = decltype((std::declval<L>() - std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_sub =
            meta::has_sub<L, R> && noexcept((std::declval<L>() - std::declval<R>()));

        template <typename L, typename R> requires (has_sub<L, R>)
        using sub_type = meta::sub_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_isub = requires(L& l, R r) { l -= r; };

    template <typename L, typename R> requires (has_isub<L, R>)
    using isub_type = decltype((std::declval<L&>() -= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept isub_returns = has_isub<L, R> && convertible_to<isub_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_isub =
            meta::has_isub<L, R> && noexcept((std::declval<L&>() -= std::declval<R>()));

        template <typename L, typename R> requires (has_isub<L, R>)
        using isub_type = meta::isub_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept isub_returns =
            has_isub<L, R> && convertible_to<isub_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_mul = requires(L l, R r) { l * r; };

    template <typename L, typename R> requires (has_mul<L, R>)
    using mul_type = decltype((std::declval<L>() * std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_mul =
            meta::has_mul<L, R> && noexcept((std::declval<L>() * std::declval<R>()));

        template <typename L, typename R> requires (has_mul<L, R>)
        using mul_type = meta::mul_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_imul = requires(L& l, R r) { l *= r; };

    template <typename L, typename R> requires (has_imul<L, R>)
    using imul_type = decltype((std::declval<L&>() *= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept imul_returns = has_imul<L, R> && convertible_to<imul_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_imul =
            meta::has_imul<L, R> && noexcept((std::declval<L&>() *= std::declval<R>()));

        template <typename L, typename R> requires (has_imul<L, R>)
        using imul_type = meta::imul_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept imul_returns =
            has_imul<L, R> && convertible_to<imul_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_div = requires(L l, R r) { l / r; };

    template <typename L, typename R> requires (has_div<L, R>)
    using div_type = decltype((std::declval<L>() / std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept div_returns =
        has_div<L, R> && convertible_to<div_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_div =
            meta::has_div<L, R> && noexcept((std::declval<L>() / std::declval<R>()));

        template <typename L, typename R> requires (has_div<L, R>)
        using div_type = meta::div_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept div_returns =
            has_div<L, R> && convertible_to<div_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_idiv = requires(L& l, R r) { l /= r; };

    template <typename L, typename R> requires (has_idiv<L, R>)
    using idiv_type = decltype((std::declval<L&>() /= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept idiv_returns =
        has_idiv<L, R> && convertible_to<idiv_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_idiv =
            meta::has_idiv<L, R> && noexcept((std::declval<L&>() /= std::declval<R>()));

        template <typename L, typename R> requires (has_idiv<L, R>)
        using idiv_type = meta::idiv_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept idiv_returns =
            has_idiv<L, R> && convertible_to<idiv_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_mod = requires(L l, R r) { l % r; };

    template <typename L, typename R> requires (has_mod<L, R>)
    using mod_type = decltype((std::declval<L>() % std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_mod =
            meta::has_mod<L, R> && noexcept((std::declval<L>() % std::declval<R>()));

        template <typename L, typename R> requires (has_mod<L, R>)
        using mod_type = meta::mod_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_imod = requires(L& l, R r) { l %= r; };

    template <typename L, typename R> requires (has_imod<L, R>)
    using imod_type = decltype((std::declval<L&>() %= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept imod_returns =
        has_imod<L, R> && convertible_to<imod_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_imod =
            meta::has_imod<L, R> && noexcept((std::declval<L&>() %= std::declval<R>()));

        template <typename L, typename R> requires (has_imod<L, R>)
        using imod_type = meta::imod_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept imod_returns =
            has_imod<L, R> && convertible_to<imod_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_lshift = requires(L l, R r) { l << r; };

    template <typename L, typename R> requires (has_lshift<L, R>)
    using lshift_type = decltype((std::declval<L>() << std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept lshift_returns =
        has_lshift<L, R> && convertible_to<lshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_lshift =
            meta::has_lshift<L, R> && noexcept((std::declval<L>() << std::declval<R>()));

        template <typename L, typename R> requires (has_lshift<L, R>)
        using lshift_type = meta::lshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept lshift_returns =
            has_lshift<L, R> && convertible_to<lshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ilshift = requires(L& l, R r) { l <<= r; };

    template <typename L, typename R> requires (has_ilshift<L, R>)
    using ilshift_type = decltype((std::declval<L&>() <<= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ilshift_returns =
        has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ilshift =
            meta::has_ilshift<L, R> &&
            noexcept((std::declval<L&>() <<= std::declval<R>()));

        template <typename L, typename R> requires (has_ilshift<L, R>)
        using ilshift_type = meta::ilshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ilshift_returns =
            has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_rshift = requires(L l, R r) { l >> r; };

    template <typename L, typename R> requires (has_rshift<L, R>)
    using rshift_type = decltype((std::declval<L>() >> std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept rshift_returns =
        has_rshift<L, R> && convertible_to<rshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_rshift =
            meta::has_rshift<L, R> && noexcept((std::declval<L>() >> std::declval<R>()));

        template <typename L, typename R> requires (has_rshift<L, R>)
        using rshift_type = meta::rshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept rshift_returns =
            has_rshift<L, R> && convertible_to<rshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_irshift = requires(L& l, R r) { l >>= r; };

    template <typename L, typename R> requires (has_irshift<L, R>)
    using irshift_type = decltype((std::declval<L&>() >>= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept irshift_returns =
        has_irshift<L, R> && convertible_to<irshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_irshift =
            meta::has_irshift<L, R> &&
            noexcept((std::declval<L&>() >>= std::declval<R>()));

        template <typename L, typename R> requires (has_irshift<L, R>)
        using irshift_type = meta::irshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept irshift_returns =
            has_irshift<L, R> && convertible_to<irshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_and = requires(L l, R r) { l & r; };

    template <typename L, typename R> requires (has_and<L, R>)
    using and_type = decltype((std::declval<L>() & std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept and_returns = has_and<L, R> && convertible_to<and_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_and =
            meta::has_and<L, R> && noexcept((std::declval<L>() & std::declval<R>()));

        template <typename L, typename R> requires (has_and<L, R>)
        using and_type = meta::and_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept and_returns =
            has_and<L, R> && convertible_to<and_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_iand = requires(L& l, R r) { l &= r; };

    template <typename L, typename R> requires (has_iand<L, R>)
    using iand_type = decltype((std::declval<L&>() &= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept iand_returns = has_iand<L, R> && convertible_to<iand_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_iand =
            meta::has_iand<L, R> && noexcept((std::declval<L&>() &= std::declval<R>()));

        template <typename L, typename R> requires (has_iand<L, R>)
        using iand_type = meta::iand_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept iand_returns =
            has_iand<L, R> && convertible_to<iand_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_or = requires(L l, R r) { l | r; };

    template <typename L, typename R> requires (has_or<L, R>)
    using or_type = decltype((std::declval<L>() | std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_or =
            meta::has_or<L, R> && noexcept((std::declval<L>() | std::declval<R>()));

        template <typename L, typename R> requires (has_or<L, R>)
        using or_type = meta::or_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ior = requires(L& l, R r) { l |= r; };

    template <typename L, typename R> requires (has_ior<L, R>)
    using ior_type = decltype((std::declval<L&>() |= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ior_returns = has_ior<L, R> && convertible_to<ior_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ior =
            meta::has_ior<L, R> && noexcept((std::declval<L&>() |= std::declval<R>()));

        template <typename L, typename R> requires (has_ior<L, R>)
        using ior_type = meta::ior_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ior_returns =
            has_ior<L, R> && convertible_to<ior_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_xor = requires(L l, R r) { l ^ r; };

    template <typename L, typename R> requires (has_xor<L, R>)
    using xor_type = decltype((std::declval<L>() ^ std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept xor_returns = has_xor<L, R> && convertible_to<xor_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_xor =
            meta::has_xor<L, R> && noexcept((std::declval<L>() ^ std::declval<R>()));

        template <typename L, typename R> requires (has_xor<L, R>)
        using xor_type = meta::xor_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept xor_returns =
            has_xor<L, R> && convertible_to<xor_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ixor = requires(L& l, R r) { l ^= r; };

    template <typename L, typename R> requires (has_ixor<L, R>)
    using ixor_type = decltype((std::declval<L&>() ^= std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept ixor_returns = has_ixor<L, R> && convertible_to<ixor_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ixor =
            meta::has_ixor<L, R> && noexcept((std::declval<L&>() ^= std::declval<R>()));

        template <typename L, typename R> requires (has_ixor<L, R>)
        using ixor_type = meta::ixor_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ixor_returns =
            has_ixor<L, R> && convertible_to<ixor_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_logical_and = requires(L l, R r) { l && r; };

    template <typename L, typename R> requires (has_logical_and<L, R>)
    using logical_and_type = decltype((std::declval<L>() && std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept logical_and_returns =
        has_logical_and<L, R> && convertible_to<logical_and_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_logical_and =
            meta::has_logical_and<L, R> &&
            noexcept((std::declval<L&>() && std::declval<R>()));

        template <typename L, typename R> requires (has_logical_and<L, R>)
        using logical_and_type = meta::logical_and_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept logical_and_returns =
            has_logical_and<L, R> && convertible_to<logical_and_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_logical_or = requires(L l, R r) { l || r; };

    template <typename L, typename R> requires (has_logical_or<L, R>)
    using logical_or_type = decltype((std::declval<L>() && std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept logical_or_returns =
        has_logical_or<L, R> && convertible_to<logical_or_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_logical_or =
            meta::has_logical_or<L, R> &&
            noexcept((std::declval<L&>() || std::declval<R>()));

        template <typename L, typename R> requires (has_logical_or<L, R>)
        using logical_or_type = meta::logical_or_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept logical_or_returns =
            has_logical_or<L, R> && convertible_to<logical_or_type<L, R>, Ret>;

    }

    template <typename T>
    concept has_logical_not = requires(T t) { !t; };

    template <typename T> requires (has_logical_not<T>)
    using logical_not_type = decltype((!std::declval<T>()));

    template <typename Ret, typename T>
    concept logical_not_returns =
        has_logical_not<T> && convertible_to<logical_not_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_logical_not =
            meta::has_logical_not<T> && noexcept((!std::declval<T>()));

        template <typename T> requires (has_logical_not<T>)
        using logical_not_type = meta::logical_not_type<T>;

        template <typename Ret, typename T>
        concept logical_not_returns =
            has_logical_not<T> && convertible_to<logical_not_type<T>, Ret>;

    }

    ///////////////////////////////////
    ////    STRUCTURAL CONCEPTS    ////
    ///////////////////////////////////

    template <typename T>
    concept has_data = requires(T t) { std::ranges::data(t); };

    template <has_data T>
    using data_type = decltype(std::ranges::data(std::declval<T>()));

    template <typename Ret, typename T>
    concept data_returns = has_data<T> && convertible_to<data_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_data =
            meta::has_data<T> &&
            noexcept(std::ranges::data(std::declval<T>()));

        template <has_data T>
        using data_type = meta::data_type<T>;

        template <typename Ret, typename T>
        concept data_returns = has_data<T> && convertible_to<data_type<T>, Ret>;

    }

    template <typename T>
    concept has_size = requires(T t) { std::ranges::size(t); };

    template <has_size T>
    using size_type = decltype(std::ranges::size(std::declval<T>()));

    template <typename Ret, typename T>
    concept size_returns = has_size<T> && convertible_to<size_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_size =
            meta::has_size<T> &&
            noexcept(std::ranges::size(std::declval<T>()));

        template <has_size T>
        using size_type = meta::size_type<T>;

        template <typename Ret, typename T>
        concept size_returns = has_size<T> && convertible_to<size_type<T>, Ret>;

    }

    template <typename T>
    concept has_empty = requires(T t) {
        { std::ranges::empty(t) } -> convertible_to<bool>;
    };

    namespace nothrow {

        template <typename T>
        concept has_empty =
            meta::has_empty<T> &&
            noexcept(std::ranges::empty(std::declval<T>()));

    }

    template <typename T>
    concept has_capacity = requires(T t) { t.capacity(); };

    template <has_capacity T>
    using capacity_type = decltype(std::declval<T>().capacity());

    template <typename Ret, typename T>
    concept capacity_returns = has_capacity<T> && convertible_to<capacity_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_capacity =
            meta::has_capacity<T> &&
            noexcept(std::declval<T>().capacity());

        template <has_capacity T>
        using capacity_type = meta::capacity_type<T>;

        template <typename Ret, typename T>
        concept capacity_returns = has_capacity<T> && convertible_to<capacity_type<T>, Ret>;

    }

    template <typename T>
    concept has_reserve = requires(T t, size_t n) { t.reserve(n); };

    template <has_reserve T>
    using reserve_type = decltype(std::declval<T>().reserve(std::declval<size_t>()));

    template <typename Ret, typename T>
    concept reserve_returns = has_reserve<T> && convertible_to<reserve_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_reserve =
            meta::has_reserve<T> &&
            noexcept(std::declval<T>().reserve(std::declval<size_t>()));

        template <has_reserve T>
        using reserve_type = meta::reserve_type<T>;

        template <typename Ret, typename T>
        concept reserve_returns = has_reserve<T> && convertible_to<reserve_type<T>, Ret>;

    }

    template <typename T, typename Key>
    concept has_contains = requires(T t, Key key) { t.contains(key); };

    template <typename T, typename Key> requires (has_contains<T, Key>)
    using contains_type = decltype(std::declval<T>().contains(std::declval<Key>()));

    template <typename Ret, typename T, typename Key>
    concept contains_returns =
        has_contains<T, Key> && convertible_to<contains_type<T, Key>, Ret>;

    namespace nothrow {

        template <typename T, typename Key>
        concept has_contains =
            meta::has_contains<T, Key> &&
            noexcept(std::declval<T>().contains(std::declval<Key>()));

        template <typename T, typename Key> requires (has_contains<T, Key>)
        using contains_type = meta::contains_type<T, Key>;

        template <typename Ret, typename T, typename Key>
        concept contains_returns =
            has_contains<T, Key> && convertible_to<contains_type<T, Key>, Ret>;

    }

    template <typename T>
    concept sequence_like = iterable<T> && has_size<T> && requires(T t) {
        { t[0] } -> convertible_to<yield_type<T>>;
    };

    template <typename T>
    concept mapping_like = requires(T t) {
        typename unqualify<T>::key_type;
        typename unqualify<T>::mapped_type;
        { t[std::declval<typename unqualify<T>::key_type>()] } ->
            convertible_to<typename unqualify<T>::mapped_type>;
    };

    template <typename T>
    concept has_keys = requires(T t) {
        { t.keys() } -> yields<typename unqualify<T>::key_type>;
    };

    template <has_keys T>
    using keys_type = decltype(std::declval<T>().keys());

    namespace nothrow {

        template <typename T>
        concept has_keys =
            meta::has_keys<T> &&
            noexcept(std::declval<T>().keys());

        template <has_keys T>
        using keys_type = meta::keys_type<T>;

    }

    template <typename T>
    concept has_values = requires(T t) {
        { t.values() } -> yields<typename unqualify<T>::mapped_type>;
    };

    template <has_values T>
    using values_type = decltype(std::declval<T>().values());

    namespace nothrow {

        template <typename T>
        concept has_values =
            meta::has_values<T> &&
            noexcept(std::declval<T>().values());

        template <has_values T>
        using values_type = meta::values_type<T>;

    }

    template <typename T>
    concept has_items =
        requires(T t) { { t.items() } -> iterable; } &&
        pair_with<
            yield_type<decltype(std::declval<T>().items())>,
            typename unqualify<T>::key_type,
            typename unqualify<T>::mapped_type
        >;

    template <has_items T>
    using items_type = decltype(std::declval<T>().items());

    namespace nothrow {

        template <typename T>
        concept has_items =
            meta::has_items<T> &&
            noexcept(std::declval<T>().items());

        template <has_items T>
        using items_type = meta::items_type<T>;

    }

    template <typename T>
    concept hashable = requires(T t) { std::hash<std::decay_t<T>>{}(t); };

    template <hashable T>
    using hash_type = decltype(std::hash<std::decay_t<T>>{}(std::declval<T>()));

    template <typename Ret, typename T>
    concept hash_returns = hashable<T> && convertible_to<hash_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept hashable =
            meta::hashable<T> &&
            noexcept(std::hash<std::decay_t<T>>{}(std::declval<T>()));

        template <hashable T>
        using hash_type = meta::hash_type<T>;

        template <typename Ret, typename T>
        concept hash_returns = hashable<T> && convertible_to<hash_type<T>, Ret>;

    }

    template <typename T, typename... A>
    concept has_member_sort = requires(T t, A... a) {
        std::forward<T>(t).sort(std::forward<A>(a)...);
    };

    namespace nothrow {

        template <typename T, typename... A>
        concept has_member_sort =
            meta::has_member_sort<T, A...> &&
            noexcept(std::declval<T>().sort(std::declval<A>()...));

    }

    template <typename T>
    concept has_member_to_string = requires(T t) {
        { std::forward<T>(t).to_string() } -> convertible_to<std::string>;
    };

    template <typename T>
    concept has_adl_to_string = requires(T t) {
        { to_string(std::forward<T>(t)) } -> convertible_to<std::string>;
    };

    template <typename T>
    concept has_std_to_string = requires(T t) {
        { std::to_string(std::forward<T>(t)) } -> convertible_to<std::string>;
    };

    template <typename T>
    concept has_to_string =
        has_member_to_string<T> || has_adl_to_string<T> || has_std_to_string<T>;

    template <typename T>
    concept has_stream_insertion = requires(std::ostream& os, T t) {
        { os << t } -> convertible_to<std::ostream&>;
    };

    namespace nothrow {

        template <typename T>
        concept has_member_to_string =
            meta::has_member_to_string<T> &&
            noexcept(std::declval<T>().to_string());

        template <typename T>
        concept has_adl_to_string =
            meta::has_adl_to_string<T> &&
            noexcept(to_string(std::declval<T>()));

        template <typename T>
        concept has_std_to_string =
            meta::has_std_to_string<T> &&
            noexcept(std::to_string(std::declval<T>()));

        template <typename T>
        concept has_to_string =
            has_member_to_string<T> || has_adl_to_string<T> || has_std_to_string<T>;

        template <typename T>
        concept has_stream_insertion =
            meta::has_stream_insertion<T> &&
            noexcept(std::declval<std::ostream&>() << std::declval<T>());

    }

    template <typename T>
    concept has_real = requires(T t) { t.real(); };

    template <has_real T>
    using real_type = decltype(std::declval<T>().real());

    template <typename Ret, typename T>
    concept real_returns = has_real<T> && convertible_to<real_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_real =
            meta::has_real<T> &&
            noexcept(std::declval<T>().real());

        template <has_real T>
        using real_type = meta::real_type<T>;

        template <typename Ret, typename T>
        concept real_returns = has_real<T> && convertible_to<real_type<T>, Ret>;

    }

    template <typename T>
    concept has_imag = requires(T t) { t.imag(); };

    template <has_imag T>
    using imag_type = decltype(std::declval<T>().imag());

    template <typename Ret, typename T>
    concept imag_returns = has_imag<T> && convertible_to<imag_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_imag =
            meta::has_imag<T> &&
            noexcept(std::declval<T>().imag());

        template <has_imag T>
        using imag_type = meta::imag_type<T>;

        template <typename Ret, typename T>
        concept imag_returns = has_imag<T> && convertible_to<imag_type<T>, Ret>;

    }

    template <typename T>
    concept complex = real_returns<double, T> && imag_returns<double, T>;

    template <typename T>
    concept has_abs = requires(T t) { std::abs(t); };

    template <has_abs T>
    using abs_type = decltype(std::abs(std::declval<T>()));

    template <typename Ret, typename T>
    concept abs_returns = has_abs<T> && convertible_to<abs_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_abs =
            meta::has_abs<T> &&
            noexcept(std::abs(std::declval<T>()));

        template <has_abs T>
        using abs_type = meta::abs_type<T>;

        template <typename Ret, typename T>
        concept abs_returns = has_abs<T> && convertible_to<abs_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_pow = requires(L l, R r) { std::pow(l, r); };

    template <typename L, typename R> requires (has_pow<L, R>)
    using pow_type = decltype(std::pow(std::declval<L>(), std::declval<R>()));

    template <typename Ret, typename L, typename R>
    concept pow_returns = has_pow<L, R> && convertible_to<pow_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_pow =
            meta::has_pow<L, R> &&
            noexcept(std::pow(std::declval<L>(), std::declval<R>()));

        template <typename L, typename R> requires (has_pow<L, R>)
        using pow_type = meta::pow_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept pow_returns = has_pow<L, R> && convertible_to<pow_type<L, R>, Ret>;

    }

    /////////////////////////
    ////    STL TYPES    ////
    /////////////////////////

    namespace std {

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
                using types = bertrand::args<Ts...>;
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
            ::std::is_copy_constructible_v<unqualify<A>> &&
            ::std::is_copy_assignable_v<unqualify<A>> &&
            ::std::is_move_constructible_v<unqualify<A>> &&
            ::std::is_move_assignable_v<unqualify<A>> &&

            // 3) A must be equality comparable
            requires(A a, A b) {
                { a == b } -> ::std::convertible_to<bool>;
                { a != b } -> ::std::convertible_to<bool>;
            } &&

            // 4) A must be able to allocate and deallocate
            requires(A a, T* ptr, size_t n) {
                { a.allocate(n) } -> ::std::convertible_to<T*>;
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
                meta::invoke_returns<
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


namespace impl {

    /* Recursive base class backing the `bertrand::args` utility.  This is the base
    case, which does not store a value. */
    template <typename... Ts>
    struct args_base : args_tag {
        template <typename Self, typename F, typename... A>
            requires (meta::invocable<F, A...>)
        static constexpr decltype(auto) operator()(
            Self&& self,
            F&& f,
            A&&... args
        ) noexcept(
            meta::nothrow::invocable<F, A...>
        ) {
            return (std::forward<F>(f)(std::forward<A>(args)...));
        }
    };

    /* If any of the argument types are void, then none of the base classes will store
    values, and the constructor will be disabled. */
    template <typename T, typename... Ts>
        requires (meta::is_void<T> || (meta::is_void<Ts> || ...))
    struct args_base<T, Ts...> : args_tag {};

    /* Otherwise, each base will store one type as a `.value` member, and recursively
    inherit from the remaining bases until all types are exhausted. */
    template <typename T, typename... Ts>
    struct args_base<T, Ts...> : args_base<Ts...> {
        using type = meta::remove_rvalue<T>;
        type value;

        template <meta::convertible_to<type> V, typename... Vs>
        constexpr args_base(V&& curr, Vs&&... rest) noexcept(
            meta::nothrow::convertible_to<V, type> &&
            noexcept(args_base<Ts...>(std::forward<Vs>(rest)...))
        ) :
            args_base<Ts...>(std::forward<Vs>(rest)...),
            value(std::forward<V>(curr))
        {}

        constexpr args_base(args_base&& other)
            noexcept(
                meta::nothrow::convertible_to<T, type> &&
                noexcept(args_base<Ts...>(std::move(other)))
            )
        :
            args_base<Ts...>(std::move(other)),
            value(std::forward<T>(other.value))
        {}

        template <size_t I, typename Self>
        static constexpr decltype(auto) get(Self&& self) noexcept {
            if constexpr (I == 0) {
                return std::forward<meta::qualify<args_base, Self>>(self).value;
            } else {
                return args_base<Ts...>::template get<I - 1>(std::forward<Self>(self));
            }
        }

        template <typename Self, typename F, typename... A>
        static constexpr decltype(auto) operator()(
            Self&& self,
            F&& f,
            A&&... args
        )
            noexcept(noexcept(args_base<Ts...>::operator()(
                std::forward<Self>(self),
                std::forward<F>(f),
                std::forward<A>(args)...,
                std::forward<meta::qualify<args_base, Self>>(self).value
            )))
            requires (requires{args_base<Ts...>::operator()(
                std::forward<Self>(self),
                std::forward<F>(f),
                std::forward<A>(args)...,
                std::forward<meta::qualify<args_base, Self>>(self).value
            ); })
        {
            return (args_base<Ts...>::operator()(
                std::forward<Self>(self),
                std::forward<F>(f),
                std::forward<A>(args)...,
                std::forward<meta::qualify<args_base, Self>>(self).value
            ));
        }
    };

    /* A generic sentinel type to simplify iterator implementations. */
    struct sentinel {
        constexpr bool operator==(sentinel) const noexcept { return true; }
        constexpr auto operator<=>(sentinel) const noexcept {
            return std::strong_ordering::equal;
        }
    };

    /* A helper type that simplifies friend declarations for wrapper types.  Every
    subclass of `wrapper` should declare this as a friend and provide its own
    `getter()` private method to implement its behavior. */
    struct getter {
        template <meta::wrapper T>
            requires (requires(T&& value) { std::forward<T>(value).getter(); })
        static constexpr decltype(auto) operator()(T&& value) noexcept(
            noexcept(std::forward<T>(value).getter())
        ) {
            return std::forward<T>(value).getter();
        }
    };

    /* A helper type that simplifies friend declarations for wrapper types.  Every
    subclass of `wrapper` should declare this as a friend and provide its own
    `setter(T)` private method to implement its behavior, or omit it for read-only
    wrappers. */
    struct setter {
        template <meta::wrapper T, typename U>
            requires (requires(T&& self, U&& value) {
                std::forward<T>(self).setter(std::forward<U>(value));
            })
        static constexpr decltype(auto) operator()(T&& self, U&& value) noexcept(
            noexcept(std::forward<T>(self).setter(std::forward<U>(value)))
        ) {
            return std::forward<T>(self).setter(std::forward<U>(value));
        }
    };

}


namespace meta {

    template <typename T>
    concept args = inherits<T, impl::args_tag>;

    /* Transparently unwrap wrapped types, triggering a getter call if the
    `meta::wrapper` concept is modeled, or returning the result as-is otherwise. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) unwrap(T&& t) noexcept {
        return ::std::forward<T>(t);
    }

    /* Transparently unwrap wrapped types, triggering a getter call if the
    `meta::wrapper` concept is modeled, or returning the result as-is otherwise. */
    template <wrapper T>
    [[nodiscard]] constexpr decltype(auto) unwrap(T&& t) noexcept(
        noexcept(impl::getter{}(::std::forward<T>(t)))
    ) {
        return impl::getter{}(::std::forward<T>(t));
    }

    /* Describes the result of the `meta::unwrap()` operator. */
    template <typename T>
    using unwrap_type = decltype(unwrap(::std::declval<T>()));

}


/* Save a set of input arguments for later use.  Returns an args<> container, which
stores the arguments similar to a `std::tuple`, except that it is move-only and capable
of storing references.  Calling the args pack will perfectly forward its values to an
input function, preserving any value categories from the original objects or the args
pack itself.

Also provides utilities for higher-order template metaprogramming wherever manipulation
of arbitrary type lists may be necessary.

NOTE: in most implementations, the C++ standard does not strictly define the order of
evaluation for function arguments, which can lead to surprising behavior if the
arguments have side effects, or depend on each other.  However, this restriction is
limited in the case of class constructors and initializer lists, which are guaranteed
to evaluate from left to right.  This class can be used to trivially exploit that
loophole by storing the arguments in a pack and immediately consuming them, without
any additional overhead besides a possible move in and out of the argument pack.

WARNING: undefined behavior can occur if an lvalue is bound that falls out of scope
before the pack is consumed.  Such values will not have their lifetimes extended by
this class in any way, and it is the user's responsibility to ensure that proper
reference semantics are observed at all times.  Generally speaking, ensuring that no
packs are returned out of a local context is enough to satisfy this guarantee.
Typically, this class will be consumed within the same context in which it was created,
or in a downstream one where all of the objects are still in scope, as a way of
enforcing a certain order of operations.  Note that this guidance does not apply to
rvalues, which are stored directly within the pack, extending their lifetimes. */
template <typename... Ts>
struct args : impl::args_base<Ts...> {
private:

    template <template <typename> class T, typename...>
    static constexpr bool broadcast_value = true;
    template <template <typename> class T, typename U, typename... Us>
    static constexpr bool broadcast_value<T, U, Us...> =
        requires{ T<U>::value; } && broadcast_value<T, Us...>;

    template <typename...>
    struct get_common_type { using type = void; };
    template <typename... Us> requires (meta::has_common_type<Us...>)
    struct get_common_type<Us...> { using type = meta::common_type<Us...>; };

    template <typename out, typename...>
    struct _reverse { using type = out; };
    template <typename... out, typename U, typename... Us>
    struct _reverse<args<out...>, U, Us...> {
        using type = _reverse<args<U, out...>, Us...>::type;
    };

    template <typename>
    struct _concat;
    template <typename... Us>
    struct _concat<args<Us...>> { using type = args<Ts..., Us...>; };

    template <size_t, typename out, typename...>
    struct _repeat { using type = out; };
    template <size_t N, typename... out, typename U, typename... Us>
    struct _repeat<N, args<out...>, U, Us...> {
        template <size_t I, typename... Vs>
        struct expand { using type = expand<I - 1, Vs..., U>::type; };
        template <typename... Vs>
        struct expand<0, Vs...> {
            using type = _repeat<N, args<out..., Vs...>, Us...>::type;
        };
        using type = expand<N>::type;
    };

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

    template <template <typename> class F, typename out, typename...>
    struct _filter { using type = out; };
    template <template <typename> class F, typename... out, typename U, typename... Us>
    struct _filter<F, args<out...>, U, Us...> {
        using type = _filter<F, args<out...>, Us...>::type;
    };
    template <template <typename> class F, typename... out, typename U, typename... Us>
        requires (F<U>::value)
    struct _filter<F, args<out...>, U, Us...> {
        using type = _filter<F, args<out..., U>, Us...>::type;
    };

    template <typename...>
    struct _fold_left {
        template <template <typename, typename> class F>
        using type = void;
    };
    template <typename out, typename... next>
    struct _fold_left<out, next...> {
        template <template <typename, typename> class F>
        using type = out;
    };
    template <typename out, typename curr, typename... next>
    struct _fold_left<out, curr, next...> {
        template <template <typename, typename> class F>
        using type = _fold_left<F<out, curr>, next...>::template type<F>;
    };

    template <typename...>
    struct _fold_right {
        template <template <typename, typename> class F>
        using type = void;
    };
    template <typename out, typename... next>
    struct _fold_right<out, next...> {
        template <template <typename, typename> class F>
        using type = out;
    };
    template <typename out, typename curr, typename... next>
    struct _fold_right<out, curr, next...> {
        template <template <typename, typename> class F>
        using type = _fold_right<F<out, curr>, next...>::template type<F>;
    };

    template <typename out, typename...>
    struct _consolidate { using type = out; };
    template <typename... out, typename U, typename... Us>
    struct _consolidate<args<out...>, U, Us...> {
        using type = _consolidate<args<out...>, Us...>::type;
    };
    template <typename... out, typename U, typename... Us>
        requires ((!meta::is<U, out> && ...) && (!meta::is<U, Us> && ...))
    struct _consolidate<args<out...>, U, Us...> {
        using type = _consolidate<args<out..., U>, Us...>::type;
    };
    template <typename... out, typename U, typename... Us>
    requires ((!meta::is<U, out> && ...) && (meta::is<U, Us> || ...))
    struct _consolidate<args<out...>, U, Us...> {
        using type = _consolidate<args<out..., meta::unqualify<U>>, Us...>::type;
    };

public:
    /* The total number of arguments being stored. */
    [[nodiscard]] static constexpr size_t size() noexcept { return sizeof...(Ts); }
    [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
    [[nodiscard]] static constexpr bool empty() noexcept { return !sizeof...(Ts); }

    /* Check to see whether a particular type is present within the pack. */
    template <typename T>
    static constexpr bool contains() noexcept { return index<T>() < size(); }

    /* Get the count of a particular type within the pack. */
    template <typename T>
    static constexpr size_t count() noexcept { return meta::count_of<T, Ts...>; }

    /* Get the index of the first occurrence of a particular type within the pack. */
    template <typename T>
    static constexpr size_t index() noexcept { return meta::index_of<T, Ts...>; }

    /* True if the argument types are all unique.  False otherwise. */
    static constexpr bool unique = meta::types_are_unique<Ts...>;

    /* True if the argument types all share a common type to which they can be
    converted. */
    static constexpr bool has_common_type = meta::has_common_type<Ts...>;

    /* The common type to which all arguments can be convertd, or `void` if no such
    type exists. */
    using common_type = get_common_type<Ts...>::type;

    /* Get the type at index I. */
    template <size_t I> requires (I < size())
    using at = meta::unpack_type<I, Ts...>;

    /* Return a new pack with the same contents in reverse order. */
    using reverse = _reverse<args<>, Ts...>::type;

    /* Evaluate a template template parameter over the given arguments. */
    template <template <typename...> class F>
        requires (requires{typename F<Ts...>;})
    using eval = F<Ts...>;

    /* Map a template template parameter over the given arguments, returning a new
    pack containing the transformed result */
    template <template <typename> class F>
        requires (requires{typename args<F<Ts>...>;})
    using map = args<F<Ts>...>;

    /* Get a new pack containing only those types in `Ts...` for which `cnd<T>::value`
    evaluates to true. */
    template <template <typename> class F>
        requires (broadcast_value<F, Ts...>)
    using filter = _filter<F, args<>, Ts...>::type;

    /* Apply a pairwise template template parameter over the contents of a non-empty
    pack, accumulating results from left to right into a single collapsed value. */
    template <template <typename, typename> class F> requires (size() > 0)
    using fold_left = _fold_left<Ts...>::template type<F>;

    /* Apply a pairwise template template parameter over the contents of a non-empty
    pack, accumulating results from right to left into a single collapsed value. */
    template <template <typename, typename> class F> requires (size() > 0)
    using fold_right = reverse::template eval<_fold_right>::template type<F>;

    /* Get a new pack with one or more types appended after the current contents. */
    template <typename... Us>
    using append = args<Ts..., Us...>;

    /* Get a new pack that combines the contents of this pack with another. */
    template <meta::args T>
    using concat = _concat<meta::unqualify<T>>::type;

    /* Get a new pack containing `N` consecutive repetitions for each type in this
    pack. */
    template <size_t N>
    using repeat = _repeat<N, args<>, Ts...>::type;

    /* Get a pack of packs containing all unique permutations of the types in this
    argument pack and all others, returning the Cartesian product.  */
    template <meta::args... packs> requires ((size() > 0) && ... && (packs::size() > 0))
    using product = _product<packs...>::type;

    /* Get a new pack with exact duplicates filtered out, accounting for cvref
    qualifications. */
    using to_unique = meta::unique_types<Ts...>;

    /* Get a new pack with duplicates filtered out and replacing any types that differ
    only in cvref qualifications with an unqualified equivalent, thereby forcing a
    copy/move upon construction. */
    using consolidate = _consolidate<args<>, Ts...>::type;

    /* Constructor.  Saves a pack of arguments for later use, retaining proper
    lvalue/rvalue categories and cv qualifiers. */
    template <std::convertible_to<meta::remove_rvalue<Ts>>... Us>
    constexpr args(Us&&... args)
        noexcept(noexcept(impl::args_base<Ts...>(std::forward<Us>(args)...)))
    :
        impl::args_base<Ts...>(std::forward<Us>(args)...)
    {}

    // args are move constructible, but not copyable or assignable
    constexpr args(args&& other)
        noexcept(noexcept(impl::args_base<Ts...>(std::move(other))))
        requires(meta::convertible_to<Ts, meta::remove_rvalue<Ts>> && ...)
    {}
    constexpr args(const args&) = delete;
    constexpr args& operator=(const args&) = delete;
    constexpr args& operator=(args&&) = delete;

    /* Get the argument at index I, perfectly forwarding it according to the pack's
    current cvref qualifications.  This means that if the pack is supplied as an
    lvalue, then all arguments will be forwarded as lvalues, regardless of their
    status in the template signature.  If the pack is an rvalue, then the arguments
    will be perfectly forwarded according to their original categories.  If the pack
    is cv qualified, then the result will be forwarded with those same qualifiers. */
    template <size_t I, typename Self>
        requires (meta::not_void<Ts> && ... && (I < size()))
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
        return impl::args_base<Ts...>::template get<I>(std::forward<Self>(self));
    }

    /* Calling a pack will perfectly forward the saved arguments according to the
    pack's current cvref qualifications.  This means that if the pack is supplied as an
    lvalue, then all arguments will be forwarded as lvalues, regardless of their status
    in the template signature.  If the pack is an rvalue, then the arguments will be
    perfectly forwarded according to their original categories.  If the pack is cv
    qualified, then the arguments will be forwarded with those same qualifiers. */
    template <typename Self, typename F>
    constexpr decltype(auto) operator()(this Self&& self, F&& f)
        noexcept(meta::nothrow::invocable<F, Ts...>)
        requires(meta::not_void<Ts> && ... && requires{
            impl::args_base<Ts...>::operator()(
                std::forward<Self>(self),
                std::forward<F>(f)
            );
        })
    {
        return (impl::args_base<Ts...>::operator()(
            std::forward<Self>(self),
            std::forward<F>(f)
        ));
    }
};


/* CTAD guide allows argument types to be implicitly captured just from an initializer
list. */
template <typename... Ts>
args(Ts&&...) -> args<Ts...>;


namespace impl {

    /* A smart reference class that perfectly forwards every operator except
    assignment, unary `&`, `*`, `->`, and `->*` to the result of a private `getter()`
    method declared with `bertrand::impl::getter` as a friend.  Assignment is forwarded
    to a separate `setter(T)` method exposed via `bertrand::impl::setter`, while unary
    `&` refers to the address of the wrapper, `*` triggers an explicit getter call, and
    `->`/`->*` allow indirect access to members of the wrapped type.

    Inheriting from this class allows a forwarding wrapper to be declared just by
    implementing its getter/setter logic, without needing to recreate the full operator
    interface.  Note that wrappers of this form cannot be copied or moved by default,
    and should not expose any public interface of their own, including constructors.
    The only logic that should be implemented is the getter (which must return a
    reference), and optionally the setter (whose result will be returned from the
    assignment operator). */
    struct wrapper : wrapper_tag {
    private:
        static constexpr impl::setter setter;

    protected:
        constexpr wrapper() = default;

    public:
        constexpr wrapper(const wrapper&) = delete;
        constexpr wrapper(wrapper&&) = delete;
        constexpr wrapper& operator=(const wrapper&) = delete;
        constexpr wrapper& operator=(wrapper&&) = delete;

        template <typename S, typename T>
            requires (!meta::is<T, wrapper> && requires(S self, T value) {
                setter(std::forward<S>(self), meta::unwrap(std::forward<T>(value)));
            })
        constexpr decltype(auto) operator=(this S&& self, T&& value) noexcept(
            noexcept(setter(std::forward<S>(self), meta::unwrap(std::forward<T>(value))))
        ) {
            return setter(std::forward<S>(self), meta::unwrap(std::forward<T>(value)));
        }

        template <typename S, typename T>
            requires (meta::convertible_to<decltype(meta::unwrap(std::declval<S>())), T>)
        constexpr operator T(this S&& self) noexcept(
            noexcept(meta::implicit_cast<T>(meta::unwrap(std::forward<S>(self))))
        ) {
            return meta::unwrap(std::forward<S>(self));
        }

        template <typename S, typename T>
            requires (
                !meta::convertible_to<decltype(meta::unwrap(std::declval<S>())), T> &&
                meta::explicitly_convertible_to<decltype(meta::unwrap(std::declval<S>())), T>
            )
        explicit constexpr operator T(this S&& self) noexcept(
            noexcept(static_cast<T>(meta::unwrap(std::forward<S>(self))))
        ) {
            return static_cast<T>(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                meta::unwrap(std::forward<S>(self));
            })
        constexpr decltype(auto) operator*(this S&& self) noexcept(
            noexcept(meta::unwrap(std::forward<S>(self)))
        ) {
            return meta::unwrap(std::forward<S>(self));
        }

        template <typename S>
            requires (requires(S self) {
                &meta::unwrap(std::forward<S>(self));
            })
        constexpr decltype(auto) operator->(this S&& self) noexcept(
            noexcept(&meta::unwrap(std::forward<S>(self)))
        ) {
            return &meta::unwrap(std::forward<S>(self));
        }

        template <typename S, typename M>
            requires (requires(S self, M member) {
                meta::unwrap(std::forward<S>(self)).*std::forward<M>(member);
            })
        constexpr decltype(auto) operator->*(this S&& self, M&& member) noexcept(
            noexcept(meta::unwrap(std::forward<S>(self)).*std::forward<M>(member))
        ) {
            return meta::unwrap(std::forward<S>(self)).*std::forward<M>(member);
        }

        template <typename S, typename... K>
            requires (requires(S self, K... keys) {
                meta::unwrap(std::forward<S>(self))[std::forward<K>(keys)...];
            })
        constexpr decltype(auto) operator[](this S&& self, K&&... keys) noexcept(
            noexcept(meta::unwrap(std::forward<S>(self))[std::forward<K>(keys)...])
        ) {
            return meta::unwrap(std::forward<S>(self))[std::forward<K>(keys)...];
        }

        template <typename S, typename... A>
            requires (requires(S self, A... args) {
                meta::unwrap(std::forward<S>(self))(std::forward<A>(args)...);
            })
        constexpr decltype(auto) operator()(this S&& self, A&&... args) noexcept(
            noexcept(meta::unwrap(std::forward<S>(self))(std::forward<A>(args)...))
        ) {
            return meta::unwrap(std::forward<S>(self))(std::forward<A>(args)...);
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)), meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator,(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)), meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)), meta::unwrap(std::forward<R>(rhs));
        }

        template <typename S>
            requires (requires(S self) {
                !meta::unwrap(std::forward<S>(self));
            })
        constexpr decltype(auto) operator!(this S&& self) noexcept(
            noexcept(!meta::unwrap(std::forward<S>(self)))
        ) {
            return !meta::unwrap(std::forward<S>(self));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) && meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator&&(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) && meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) && meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) || meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator||(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) || meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) || meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) < meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator<(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) < meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) < meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) <= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator<=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) <= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) <= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) == meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator==(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) == meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) == meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) != meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator!=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) != meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) != meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) >= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator>=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) >= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) >= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) > meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator>(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) > meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) > meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) <=> meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator<=>(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) <=> meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) <=> meta::unwrap(std::forward<R>(rhs));
        }

        template <typename S>
            requires (requires(S self) {
                ~meta::unwrap(std::forward<S>(self));
            })
        constexpr decltype(auto) operator~(this S&& self) noexcept(
            noexcept(~meta::unwrap(std::forward<S>(self)))
        ) {
            return ~meta::unwrap(std::forward<S>(self));
        }

        template <typename S>
            requires (requires(S self) {
                +meta::unwrap(std::forward<S>(self));
            })
        constexpr decltype(auto) operator+(this S&& self) noexcept(
            noexcept(+meta::unwrap(std::forward<S>(self)))
        ) {
            return +meta::unwrap(std::forward<S>(self));
        }

        template <typename S>
            requires (requires(S self) {
                -meta::unwrap(std::forward<S>(self));
            })
        constexpr decltype(auto) operator-(this S&& self) noexcept(
            noexcept(-meta::unwrap(std::forward<S>(self)))
        ) {
            return -meta::unwrap(std::forward<S>(self));
        }

        template <typename S>
            requires (requires(S self) {
                ++meta::unwrap(std::forward<S>(self));
            })
        constexpr decltype(auto) operator++(this S&& self) noexcept(
            noexcept(++meta::unwrap(std::forward<S>(self)))
        ) {
            return ++meta::unwrap(std::forward<S>(self));
        }

        template <typename S>
            requires (requires(S self) {
                meta::unwrap(std::forward<S>(self))++;
            })
        constexpr decltype(auto) operator++(this S&& self, int) noexcept(
            noexcept(meta::unwrap(std::forward<S>(self))++)
        ) {
            return meta::unwrap(std::forward<S>(self))++;
        }

        template <typename S>
            requires (requires(S self) {
                --meta::unwrap(std::forward<S>(self));
            })
        constexpr decltype(auto) operator--(this S&& self) noexcept(
            noexcept(--meta::unwrap(std::forward<S>(self)))
        ) {
            return --meta::unwrap(std::forward<S>(self));
        }

        template <typename S>
            requires (requires(S self) {
                meta::unwrap(std::forward<S>(self))--;
            })
        constexpr decltype(auto) operator--(this S&& self, int) noexcept(
            noexcept(meta::unwrap(std::forward<S>(self))--)
        ) {
            return meta::unwrap(std::forward<S>(self))--;
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) + meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator+(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) + meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) + meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) += meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator+=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) += meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) += meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) - meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator-(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) - meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) - meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) -= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator-=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) -= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) -= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) * meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator*(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) * meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) * meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) *= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator*=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) *= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) *= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) / meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator/(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) / meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) / meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) /= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator/=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) /= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) /= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) % meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator%(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) % meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) % meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) %= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator%=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) %= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) %= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) << meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator<<(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) << meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) << meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) <<= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator<<=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) <<= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) <<= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) >> meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator>>(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) >> meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) >> meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) >>= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator>>=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) >>= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) >>= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) & meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator&(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) & meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) & meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) &= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator&=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) &= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) &= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) | meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator|(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) | meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) | meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) |= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator|=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) |= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) |= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) ^ meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator^(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) ^ meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) ^ meta::unwrap(std::forward<R>(rhs));
        }

        template <typename L, typename R>
            requires (requires(L lhs, R rhs) {
                meta::unwrap(std::forward<L>(lhs)) ^= meta::unwrap(std::forward<R>(rhs));
            })
        constexpr friend decltype(auto) operator^=(L&& lhs, R&& rhs) noexcept(
            noexcept(meta::unwrap(std::forward<L>(lhs)) ^= meta::unwrap(std::forward<R>(rhs)))
        ) {
            return meta::unwrap(std::forward<L>(lhs)) ^= meta::unwrap(std::forward<R>(rhs));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::begin(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto begin(this S&& self) noexcept(
            noexcept(std::ranges::begin(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::begin(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::cbegin(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto cbegin(this S&& self) noexcept(
            noexcept(std::ranges::cbegin(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::cbegin(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::end(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto end(this S&& self) noexcept(
            noexcept(std::ranges::end(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::end(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::cend(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto cend(this S&& self) noexcept(
            noexcept(std::ranges::cend(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::cend(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::rbegin(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto rbegin(this S&& self) noexcept(
            noexcept(std::ranges::rbegin(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::rbegin(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::crbegin(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto crbegin(this S&& self) noexcept(
            noexcept(std::ranges::crbegin(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::crbegin(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::rend(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto rend(this S&& self) noexcept(
            noexcept(std::ranges::rend(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::rend(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::crend(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto crend(this S&& self) noexcept(
            noexcept(std::ranges::crend(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::crend(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::size(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto size(this S&& self) noexcept(
            noexcept(std::ranges::size(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::size(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::empty(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto empty(this S&& self) noexcept(
            noexcept(std::ranges::empty(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::empty(meta::unwrap(std::forward<S>(self)));
        }

        template <typename S>
            requires (requires(S self) {
                std::ranges::data(meta::unwrap(std::forward<S>(self)));
            })
        constexpr auto data(this S&& self) noexcept(
            noexcept(std::ranges::data(meta::unwrap(std::forward<S>(self))))
        ) {
            return std::ranges::data(meta::unwrap(std::forward<S>(self)));
        }

        template <auto... I, typename S, typename... A>
            requires (requires(S self, A... args) {
                meta::unwrap(std::forward<S>(self)).template get<I...>(
                    std::forward<A>(args)...
                );
            })
        constexpr decltype(auto) get(this S&& self, A&&... args) noexcept(
            noexcept(meta::unwrap(std::forward<S>(self)).template get<I...>(
                std::forward<A>(args)...
            ))
        ) {
            return meta::unwrap(std::forward<S>(self)).template get<I...>(
                std::forward<A>(args)...
            );
        }

        /// TODO: ADL get() version?  This would require some kind of customization
        /// point that always triggers ADL, but the problem with that is that it
        /// prevents me from explicitly providing template arguments, unless you do
        /// something like meta::get<...>{}(self, args...) which is a bit clunky,
        /// especially without universal template parameters.

        template <auto... I, typename S, typename... A>
            requires (!requires(S self, A... args) {
                meta::unwrap(std::forward<S>(self)).template get<I...>(
                    std::forward<A>(args)...
                );
            } && requires(S self, A... args) {
                std::get<I...>(meta::unwrap(
                    std::forward<S>(self),
                    std::forward<A>(args)...
                ));
            })
        constexpr decltype(auto) get(this S&& self, A&&... args) noexcept(
            noexcept(std::get<I...>(meta::unwrap(
                std::forward<S>(self),
                std::forward<A>(args)...
            )))
        ) {
            return std::get<I...>(meta::unwrap(
                std::forward<S>(self),
                std::forward<A>(args)...
            ));
        }
    };

}


/* Hash an arbitrary value.  Equivalent to calling `std::hash<T>{}(...)`, but without
needing to explicitly specialize `std::hash`. */
template <meta::hashable T>
[[nodiscard]] constexpr auto hash(T&& obj) noexcept(meta::nothrow::hashable<T>) {
    return std::hash<std::decay_t<T>>{}(std::forward<T>(obj));
}


namespace impl {

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

    template <meta::not_void T>
    struct ConvertTo {
        template <meta::convertible_to<T> U>
        static constexpr T operator()(U&& value)
            noexcept(meta::nothrow::convertible_to<U, T>)
        {
            return std::forward<U>(value);
        }
    };

    template <meta::not_void T>
    struct ExplicitConvertTo {
        template <meta::explicitly_convertible_to<T> U>
        static constexpr decltype(auto) operator()(U&& value)
            noexcept(meta::nothrow::explicitly_convertible_to<U, T>)
        {
            return (static_cast<T>(std::forward<U>(value)));
        }
    };

    struct Hash {
        template <meta::hashable T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::hashable<T>)
        {
            return (bertrand::hash(std::forward<T>(value)));
        }
    };

    struct AddressOf {
        template <meta::addressable T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::addressable<T>)
        {
            return (&std::forward<T>(value));
        }
    };

    struct Dereference {
        template <meta::dereferenceable T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::dereferenceable<T>)
        {
            return (*std::forward<T>(value));
        }
    };

    struct Arrow {
        template <meta::has_arrow T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_arrow<T>)
        {
            return (std::forward<T>(value).operator->());
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
        template <typename F, typename... A> requires (meta::invocable<F, A...>)
        static constexpr decltype(auto) operator()(F&& f, A&&... args)
            noexcept(meta::nothrow::invocable<F, A...>)
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
        template <meta::has_invert T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_invert<T>)
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
    struct static_str_tag {};

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

    template <typename T>
    concept static_str = inherits<T, impl::static_str_tag>;

}


/* Gets a C++ type name as a fully-qualified, demangled string computed entirely
at compile time.  The underlying buffer is baked directly into the final binary. */
template <typename T>
constexpr auto type_name = impl::type_name_impl<T>();


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


}  // namespace bertrand


namespace std {

    template <bertrand::meta::wrapper T>
        requires (requires {
            std::tuple_size<
                std::remove_cvref_t<bertrand::meta::unwrap_type<T>>
            >::value;
        })
    struct tuple_size<T> : std::integral_constant<
        size_t,
        std::tuple_size<
            std::remove_cvref_t<bertrand::meta::unwrap_type<T>>
        >::value
    > {};

    template <size_t I, bertrand::meta::wrapper T>
        requires (requires {
            typename std::tuple_element<
                I,
                std::remove_cvref_t<bertrand::meta::unwrap_type<T>>
            >;
        })
    struct tuple_element<I, T> : std::tuple_element<
        I,
        std::remove_cvref_t<bertrand::meta::unwrap_type<T>>
    > {};

    template <auto I, bertrand::meta::wrapper T>
        requires (requires(T t) {
            std::get<I>(static_cast<bertrand::meta::unwrap_type<T>>(t));
        })
    constexpr decltype(auto) get(T&& t) noexcept(
        noexcept(std::get<I>(static_cast<bertrand::meta::unwrap_type<T>>(t)))
    ) {
        return std::get<I>(static_cast<bertrand::meta::unwrap_type<T>>(t));
    }

}


#endif  // BERTRAND_COMMON_H
