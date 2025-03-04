#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <cmath>
#include <concepts>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <typeindex>
#include <type_traits>
#include <variant>


#include <cpptrace/cpptrace.hpp>


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
    constexpr size_t TEMPLATE_RECURSION_LIMIT = 1024;
#endif


static_assert(
    TEMPLATE_RECURSION_LIMIT > 0,
    "Template recursion limit must be positive."
);


namespace meta {

    /////////////////////////
    ////    UTILITIES    ////
    /////////////////////////

    /* Trigger implicit conversion operators and/or implicit constructors, but not
    explicit ones.  In contrast, static_cast<>() will trigger explicit constructors on
    the target type, which can give unexpected results and violate type safety. */
    template <typename T>
    constexpr T implicit_cast(auto&& value) {
        return std::forward<decltype(value)>(value);
    }

    namespace detail {

        template <typename... Ts>
        constexpr bool types_are_unique = true;
        template <typename T, typename... Ts>
        constexpr bool types_are_unique<T, Ts...> =
            !(std::same_as<T, Ts> || ...) && types_are_unique<Ts...>;

        template <typename Search, size_t I, typename... Ts>
        constexpr size_t index_of = 0;
        template <typename Search, size_t I, typename T, typename... Ts>
        constexpr size_t index_of<Search, I, T, Ts...> =
            std::same_as<Search, T> ? 0 : index_of<Search, I + 1, Ts...> + 1;

        template <size_t I, typename... Ts>
        struct unpack_type;
        template <size_t I, typename T, typename... Ts>
        struct unpack_type<I, T, Ts...> { using type = unpack_type<I - 1, Ts...>::type; };
        template <typename T, typename... Ts>
        struct unpack_type<0, T, Ts...> { using type = T; };

    }

    template <typename... Ts>
    concept types_are_unique = detail::types_are_unique<Ts...>;

    /* Get the index of a particular type within a parameter pack.  Returns the pack's
    size if the type is not present. */
    template <typename Search, typename... Ts>
    static constexpr size_t index_of = detail::index_of<Search, 0, Ts...>;

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

    /* Get the type at a particular index of a parameter pack.  This is superceded by
    the C++26 pack indexing language feature. */
    template <size_t I, typename... Ts> requires (I < sizeof...(Ts))
    using unpack_type = detail::unpack_type<I, Ts...>::type;

    template <typename... Ts>
    concept has_common_type = requires { typename std::common_type<Ts...>::type; };

    template <typename... Ts> requires (has_common_type<Ts...>)
    using common_type = std::common_type_t<Ts...>;

    /////////////////////////////
    ////    QUALIFICATION    ////
    /////////////////////////////

    template <typename L, typename R>
    concept is = std::same_as<std::remove_cvref_t<L>, std::remove_cvref_t<R>>;

    template <typename L, typename R>
    concept inherits = std::derived_from<std::remove_cvref_t<L>, std::remove_cvref_t<R>>;

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

    ////////////////////////////
    ////    CONSTRUCTION    ////
    ////////////////////////////

    template <typename T, typename... Args>
    concept constructible_from = std::constructible_from<T, Args...>;

    template <typename T, typename... Args>
    concept trivially_constructible = std::is_trivially_constructible_v<T, Args...>;

    template <typename L, typename R>
    concept convertible_to = std::convertible_to<L, R>;

    template <typename L, typename R>
    concept explicitly_convertible_to = requires(L from) {
        { static_cast<R>(from) } -> std::same_as<R>;
    };

    template <typename L, typename R>
    concept assignable = std::assignable_from<L, R>;

    template <typename L, typename R>
    concept trivially_assignable = std::is_trivially_assignable_v<L, R>;

    template <typename L, typename R> requires (assignable<L, R>)
    using assign_type = decltype(std::declval<L>() = std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept assign_returns = assignable<L, R> &&
        convertible_to<assign_type<L, R>, Ret>;

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

    template <typename T, typename Ret>
    concept copy_assign_returns = copy_assignable<T> &&
        convertible_to<copy_assign_type<T>, Ret>;

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

    template <typename T, typename Ret>
    concept move_assign_returns = move_assignable<T> &&
        convertible_to<move_assign_type<T>, Ret>;

    template <typename T>
    concept swappable = std::swappable<T>;

    template <typename T>
    concept destructible = std::destructible<T>;

    template <typename T>
    concept trivially_destructible = std::is_trivially_destructible_v<T>;

    namespace nothrow {

        template <typename T, typename... Args>
        concept constructible_from =
            meta::constructible_from<T, Args...> &&
            std::is_nothrow_constructible_v<T, Args...>;

        template <typename L, typename R>
        concept convertible =
            meta::convertible_to<L, R> &&
            std::is_nothrow_convertible_v<L, R>;

        template <typename L, typename R>
        concept explicitly_convertible =
            meta::explicitly_convertible_to<L, R> &&
            noexcept(static_cast<R>(std::declval<L>()));

        template <typename L, typename R>
        concept assignable =
            meta::assignable<L, R> &&
            std::is_nothrow_assignable_v<L, R>;

        template <typename T>
        concept copyable =
            meta::copyable<T> &&
            std::is_nothrow_copy_constructible_v<T>;

        template <typename T>
        concept copy_assignable =
            meta::copy_assignable<T> &&
            std::is_nothrow_copy_assignable_v<T>;

        template <typename T>
        concept movable =
            meta::movable<T> &&
            std::is_nothrow_move_constructible_v<T>;

        template <typename T>
        concept move_assignable =
            meta::move_assignable<T> &&
            std::is_nothrow_move_assignable_v<T>;

        template <typename T>
        concept swappable =
            meta::swappable<T> &&
            std::is_nothrow_swappable_v<T>;

        template <typename T>
        concept destructible =
            meta::destructible<T> &&
            std::is_nothrow_destructible_v<T>;

    }

    //////////////////////////
    ////    PRIMITIVES    ////
    //////////////////////////

    template <typename T>
    concept boolean = std::same_as<unqualify<T>, bool>;

    template <typename T>
    concept integer = std::integral<unqualify<T>>;

    template <typename T>
    concept signed_integer = integer<T> && std::signed_integral<unqualify<T>>;

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
    concept string_literal = requires(T t) {
        { []<size_t N>(const char(&)[N]){}(t) };
    };

    //////////////////////////
    ////    INVOCATION    ////
    //////////////////////////



    template <typename T>
    concept function_signature =
        std::is_function_v<meta::remove_pointer<meta::unqualify<T>>>;

    /// TODO: other function signature concepts


    /// TODO: invocable




    ///////////////////////
    ////    MEMBERS    ////
    ///////////////////////

    namespace detail {

        template <typename T, typename C>
        constexpr bool member_object_of = false;
        template <typename T, typename C2, typename C>
        constexpr bool member_object_of<T(C2::*), C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename T, typename C2, typename C>
        constexpr bool member_object_of<const T(C2::*), C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename T, typename C2, typename C>
        constexpr bool member_object_of<volatile T(C2::*), C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename T, typename C2, typename C>
        constexpr bool member_object_of<const volatile T(C2::*), C> = std::derived_from<std::remove_cvref_t<C>, C2>;

        template <typename T, typename C>
        constexpr bool member_function_of = false;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...), C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) &, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const &, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile &, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile &, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) &&, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const &&, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile &&, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile &&, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) & noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const & noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile & noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile & noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) && noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const && noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) volatile && noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;
        template <typename R, typename C2, typename... A, typename C>
        constexpr bool member_function_of<R(C2::*)(A...) const volatile && noexcept, C> = std::derived_from<std::remove_cvref_t<C>, C2>;

    }

    template <typename T>
    concept member_object = std::is_member_object_pointer_v<std::remove_reference_t<T>>;

    template <typename T, typename C>
    concept member_object_of =
        member_object<T> && detail::member_object_of<std::remove_cvref_t<T>, C>;

    template <typename T>
    concept member_function = std::is_member_function_pointer_v<std::remove_reference_t<T>>;

    template <typename T, typename C>
    concept member_function_of =
        member_function<T> && detail::member_function_of<std::remove_cvref_t<T>, C>;

    template <typename T>
    concept member = member_object<T> || member_function<T>;

    template <typename T, typename C>
    concept member_of = member<T> && (member_object_of<T, C> || member_function_of<T, C>);

    /// TODO: as_member_of

    /// TODO: remove_member



    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    template <typename T>
    concept iterator = std::input_or_output_iterator<T>;

    template <typename T, typename Iter>
    concept sentinel_for = std::sentinel_for<T, Iter>;

    template <typename T>
    concept has_begin = requires(T& t) { std::ranges::begin(t); };

    template <has_begin T>
    using begin_type = decltype(std::ranges::begin(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_cbegin = requires(T& t) { std::ranges::cbegin(t); };

    template <has_cbegin T>
    using cbegin_type = decltype(std::ranges::cbegin(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_end = requires(T& t) { std::ranges::end(t); };

    template <has_end T>
    using end_type = decltype(std::ranges::end(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_cend = requires(T& t) { std::ranges::cend(t); };

    template <has_cend T>
    using cend_type = decltype(std::ranges::cend(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_rbegin = requires(T& t) { std::ranges::rbegin(t); };

    template <has_rbegin T>
    using rbegin_type = decltype(std::ranges::rbegin(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_crbegin = requires(T& t) { std::ranges::crbegin(t); };

    template <has_crbegin T>
    using crbegin_type = decltype(std::ranges::crbegin(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_rend = requires(T& t) { std::ranges::rend(t); };

    template <has_rend T>
    using rend_type = decltype(std::ranges::rend(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept has_crend = requires(T& t) { std::ranges::crend(t); };

    template <has_crend T>
    using crend_type = decltype(std::ranges::crend(std::declval<as_lvalue<T>>()));

    template <typename T>
    concept iterable = requires(T& t) {
        { std::ranges::begin(t) } -> iterator;
        { std::ranges::end(t) } -> sentinel_for<decltype(std::ranges::begin(t))>;
    };

    template <typename T>
    concept const_iterable = requires(T& t) {
        { std::ranges::cbegin(t) } -> iterator;
        { std::ranges::cend(t) } -> sentinel_for<decltype(std::ranges::cbegin(t))>;
    };

    template <typename T>
    concept reverse_iterable = requires(T& t) {
        { std::ranges::rbegin(t) } -> iterator;
        { std::ranges::rend(t) } -> sentinel_for<decltype(std::ranges::rbegin(t))>;
    };

    template <typename T>
    concept const_reverse_iterable = requires(T& t) {
        { std::ranges::crbegin(t) } -> iterator;
        { std::ranges::crend(t) } -> sentinel_for<decltype(std::ranges::crbegin(t))>;
    };

    template <iterable T>
    using yield_type =
        decltype(*std::ranges::begin(std::declval<meta::as_lvalue<T>>()));

    template <reverse_iterable T>
    using reverse_yield_type =
        decltype(*std::ranges::rbegin(std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Value>
    concept yields = iterable<T> && convertible_to<yield_type<T>, Value>;

    template <typename T, typename Value>
    concept reverse_yields =
        reverse_iterable<T> && convertible_to<reverse_yield_type<T>, Value>;

    namespace nothrow {

        template <typename T>
        concept has_begin =
            meta::has_begin<T> &&
            noexcept(std::ranges::begin(std::declval<as_lvalue<T>>()));

        template <typename T>
        concept has_cbegin =
            meta::has_cbegin<T> &&
            noexcept(std::ranges::cbegin(std::declval<as_lvalue<T>>()));

        template <typename T>
        concept has_end =
            meta::has_end<T> &&
            noexcept(std::ranges::end(std::declval<as_lvalue<T>>()));

        template <typename T>
        concept has_cend =
            meta::has_cend<T> &&
            noexcept(std::ranges::cend(std::declval<as_lvalue<T>>()));

        template <typename T>
        concept has_rbegin =
            meta::has_rbegin<T> &&
            noexcept(std::ranges::rbegin(std::declval<as_lvalue<T>>()));

        template <typename T>
        concept has_crbegin =
            meta::has_crbegin<T> &&
            noexcept(std::ranges::crbegin(std::declval<as_lvalue<T>>()));

        template <typename T>
        concept has_rend =
            meta::has_rend<T> &&
            noexcept(std::ranges::rend(std::declval<as_lvalue<T>>()));

        template <typename T>
        concept has_crend =
            meta::has_crend<T> &&
            noexcept(std::ranges::crend(std::declval<as_lvalue<T>>()));

        template <typename T>
        concept iterable =
            meta::iterable<T> && nothrow::has_begin<T> && nothrow::has_end<T>;

        template <typename T>
        concept const_iterable =
            meta::const_iterable<T> && nothrow::has_cbegin<T> && nothrow::has_cend<T>;

        template <typename T>
        concept reverse_iterable =
            meta::reverse_iterable<T> && nothrow::has_rbegin<T> && nothrow::has_rend<T>;

        template <typename T>
        concept const_reverse_iterable =
            meta::const_reverse_iterable<T> && nothrow::has_crbegin<T> && nothrow::has_crend<T>;

    }

    /// TODO: this can probably be eliminated with a refactor of the Python iterator
    /// interface
    template <iterable Container>
    struct iter_traits {
        using begin = begin_type<Container>;
        using end = end_type<Container>;
        using container = std::conditional_t<
            lvalue<Container>,
            void,
            remove_reference<Container>
        >;
    };

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    namespace detail {

        template <typename T, size_t N>
        struct structured {
            template <size_t I>
            static constexpr bool _value = (
                requires(T t) { get<I - 1>(t); } ||
                requires(T t) { std::get<I - 1>(t); }
            ) && _value<I - 1>;

            template <>
            static constexpr bool _value<0> = true;

            static constexpr bool value = _value<N>;
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

    template <typename T, size_t N>
    concept structured =
        std::tuple_size<T>::value == N && detail::structured<T, N>::value;

    template <typename T>
    concept pair = structured<T, 2>;

    template <typename T, typename... Ts>
    concept structured_with =
        structured<T, sizeof...(Ts)> && detail::structured_with<T, Ts...>::value;

    template <typename T, typename First, typename Second>
    concept pair_with = structured_with<T, First, Second>;

    template <typename T, typename... Key>
    concept indexable = !integer<T> && requires(T t, Key... key) {
        std::forward<T>(t)[std::forward<Key>(key)...];
    };

    template <typename T, typename... Key> requires (indexable<T, Key...>)
    using index_type = decltype(std::declval<T>()[std::declval<Key>()...]);

    template <typename T, typename Value, typename... Key>
    concept index_returns =
        indexable<T, Key...> && convertible_to<index_type<T, Key...>, Value>;

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

    template <typename T, typename Value, typename... Key>
    concept index_assign_returns =
        index_assignable<T, Value, Key...> &&
        convertible_to<index_assign_type<T, Value, Key...>, Value>;

    namespace nothrow {

        /// TODO: noexcept versions of the above

    }

    ///////////////////////////////////
    ////    STRUCTURAL CONCEPTS    ////
    ///////////////////////////////////

    template <typename T>
    concept has_call_operator = requires() { &unqualify<T>::operator(); };

    template <typename T>
    concept has_size = requires(T t) { std::ranges::size(t); };

    template <has_size T>
    using size_type = decltype(std::ranges::size(std::declval<T>()));

    template <typename T, typename Value>
    concept size_returns = has_size<T> && convertible_to<size_type<T>, Value>;

    template <typename T>
    concept has_empty = requires(T t) {
        { std::ranges::empty(t) } -> convertible_to<bool>;
    };

    template <typename T>
    concept has_operator_bool = requires(T t) {
        { static_cast<bool>(t) } -> convertible_to<bool>;
    };

    template <typename T>
    concept has_capacity = requires(T t) { t.capacity(); };

    template <has_capacity T>
    using capacity_type = decltype(std::declval<T>().capacity());

    template <typename T, typename Value>
    concept capacity_returns = has_capacity<T> && convertible_to<capacity_type<T>, Value>;

    template <typename T>
    concept has_reserve = requires(T t, size_t n) { t.reserve(n); };

    template <has_reserve T>
    using reserve_type = decltype(std::declval<T>().reserve(std::declval<size_t>()));

    template <typename T, typename Value>
    concept reserve_returns = has_reserve<T> && convertible_to<reserve_type<T>, Value>;

    template <typename T, typename Key>
    concept has_contains = requires(T t, Key key) { t.contains(key); };

    template <typename T, typename Key> requires (has_contains<T, Key>)
    using contains_type = decltype(std::declval<T>().contains(std::declval<Key>()));

    template <typename T, typename Key, typename Value>
    concept contains_returns =
        has_contains<T, Key> && convertible_to<contains_type<T, Key>, Value>;

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
    concept yields_pairs = iterable<T> && pair<yield_type<T>>;

    template <typename T, typename First, typename Second>
    concept yields_pairs_with = yields_pairs<T> && pair_with<yield_type<T>, First, Second>;

    template <typename T>
    concept has_keys = requires(T t) {
        { t.keys() } -> yields<typename unqualify<T>::key_type>;
    };

    template <has_keys T>
    using keys_type = decltype(std::declval<T>().keys());

    template <typename T>
    concept has_values = requires(T t) {
        { t.values() } -> yields<typename std::remove_cvref_t<T>::mapped_type>;
    };

    template <has_values T>
    using values_type = decltype(std::declval<T>().values());

    template <typename T>
    concept has_items = requires(T t) {
        { t.items() } -> yields_pairs_with<
            typename unqualify<T>::key_type,
            typename unqualify<T>::mapped_type
        >;
    };

    template <has_items T>
    using items_type = decltype(std::declval<T>().items());

    template <typename T>
    concept hashable = requires(T t) { std::hash<std::decay_t<T>>{}(t); };

    template <hashable T>
    using hash_type = decltype(std::hash<std::decay_t<T>>{}(std::declval<T>()));

    template <typename T, typename Value>
    concept hash_returns = hashable<T> && convertible_to<hash_type<T>, Value>;

    template <typename T>
    concept has_member_to_string = requires(T t) {
        { std::forward<T>(t).to_string() } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_adl_to_string = requires(T t) {
        { to_string(std::forward<T>(t)) } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_std_to_string = requires(T t) {
        { std::to_string(std::forward<T>(t)) } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_to_string =
        has_member_to_string<T> || has_adl_to_string<T> || has_std_to_string<T>;

    template <typename T>
    concept has_stream_insertion = requires(std::ostream& os, T t) {
        { os << t } -> std::convertible_to<std::ostream&>;
    };

    template <typename T>
    concept has_real = requires(T t) { t.real(); };

    template <has_real T>
    using real_type = decltype(std::declval<T>().real());

    template <typename T, typename Value>
    concept real_returns = has_real<T> && convertible_to<real_type<T>, Value>;

    template <typename T>
    concept has_imag = requires(T t) { t.imag(); };

    template <has_imag T>
    using imag_type = decltype(std::declval<T>().imag());

    template <typename T, typename Value>
    concept imag_returns = has_imag<T> && convertible_to<imag_type<T>, Value>;

    template <typename T>
    concept complex = real_returns<T, double> && imag_returns<T, double>;

    template <typename T>
    concept can_dereference = requires(T t) { *t; };

    template <can_dereference T>
    using dereference_type = decltype(*std::declval<T>());

    template <typename T, typename Value>
    concept dereferences_to =
        can_dereference<T> && convertible_to<dereference_type<T>, Value>;

    template <typename T>
    concept has_abs = requires(T t) { std::abs(t); };

    template <has_abs T>
    using abs_type = decltype(std::abs(std::declval<T>()));

    template <typename T, typename Value>
    concept abs_returns = has_abs<T> && convertible_to<abs_type<T>, Value>;

    template <typename L, typename R>
    concept has_pow = requires(L l, R r) { std::pow(l, r); };

    template <typename L, typename R> requires (has_pow<L, R>)
    using pow_type = decltype(std::pow(std::declval<L>(), std::declval<R>()));

    template <typename L, typename R, typename Value>
    concept pow_returns = has_pow<L, R> && convertible_to<pow_type<L, R>, Value>;

    namespace nothrow {

        template <typename T>
        concept has_size =
            meta::has_size<T> && noexcept(std::ranges::size(std::declval<T>()));

        template <typename T>
        concept has_empty =
            meta::has_empty<T> && noexcept(std::ranges::empty(std::declval<T>()));

        template <typename T>
        concept has_operator_bool =
            meta::has_operator_bool<T> && noexcept(static_cast<bool>(std::declval<T>()));

        template <typename T>
        concept has_capacity =
            meta::has_capacity<T> && noexcept(std::declval<T>().capacity());

        /// TODO: nothrow versions of the above

    }

    //////////////////////////
    ////    ARITHMETIC    ////
    //////////////////////////

    template <typename T>
    concept has_invert = requires(T t) { ~t; };

    template <has_invert T>
    using invert_type = decltype(~std::declval<T>());

    template <typename T, typename Value>
    concept invert_returns = has_invert<T> && convertible_to<invert_type<T>, Value>;

    template <typename T>
    concept has_pos = requires(T t) { +t; };

    template <has_pos T>
    using pos_type = decltype(+std::declval<T>());

    template <typename T, typename Value>
    concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Value>;

    template <typename T>
    concept has_neg = requires(T t) { -t; };

    template <has_neg T>
    using neg_type = decltype(-std::declval<T>());

    template <typename T, typename Value>
    concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Value>;

    template <typename T>
    concept has_preincrement = requires(T t) { ++t; };

    template <has_preincrement T>
    using preincrement_type = decltype(++std::declval<T>());

    template <typename T, typename Value>
    concept preincrement_returns =
        has_preincrement<T> && convertible_to<preincrement_type<T>, Value>;

    template <typename T>
    concept has_postincrement = requires(T t) { t++; };

    template <has_postincrement T>
    using postincrement_type = decltype(std::declval<T>()++);

    template <typename T, typename Value>
    concept postincrement_returns =
        has_postincrement<T> && convertible_to<postincrement_type<T>, Value>;

    template <typename T>
    concept has_predecrement = requires(T t) { --t; };

    template <has_predecrement T>
    using predecrement_type = decltype(--std::declval<T>());

    template <typename T, typename Value>
    concept predecrement_returns =
        has_predecrement<T> && convertible_to<predecrement_type<T>, Value>;

    template <typename T>
    concept has_postdecrement = requires(T t) { t--; };

    template <has_postdecrement T>
    using postdecrement_type = decltype(std::declval<T>()--);

    template <typename T, typename Return>
    concept postdecrement_returns =
        has_postdecrement<T> && convertible_to<postdecrement_type<T>, Return>;

    template <typename L, typename R>
    concept has_lt = requires(L l, R r) { l < r; };

    template <typename L, typename R> requires (has_lt<L, R>)
    using lt_type = decltype(std::declval<L>() < std::declval<R>());

    template <typename L, typename R, typename Value>
    concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_le = requires(L l, R r) { l <= r; };

    template <typename L, typename R> requires (has_le<L, R>)
    using le_type = decltype(std::declval<L>() <= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_eq = requires(L l, R r) { l == r; };

    template <typename L, typename R> requires (has_eq<L, R>)
    using eq_type = decltype(std::declval<L>() == std::declval<R>());

    template <typename L, typename R, typename Value>
    concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_ne = requires(L l, R r) { l != r; };

    template <typename L, typename R> requires (has_ne<L, R>)
    using ne_type = decltype(std::declval<L>() != std::declval<R>());

    template <typename L, typename R, typename Value>
    concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_ge = requires(L l, R r) { l >= r; };

    template <typename L, typename R> requires (has_ge<L, R>)
    using ge_type = decltype(std::declval<L>() >= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_gt = requires(L l, R r) { l > r; };

    template <typename L, typename R> requires (has_gt<L, R>)
    using gt_type = decltype(std::declval<L>() > std::declval<R>());

    template <typename L, typename R, typename Value>
    concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_spaceship = requires(L l, R r) { l <=> r; };

    template <typename L, typename R> requires (has_spaceship<L, R>)
    using spaceship_type =
        decltype(std::declval<L>() <=> std::declval<R>());

    template <typename L, typename R, typename Value>
    concept spaceship_returns =
        has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Value>;

    /// TODO: partially_ordered, totally_ordered, etc.

    template <typename L, typename R>
    concept has_add = requires(L l, R r) { l + r; };

    template <typename L, typename R> requires (has_add<L, R>)
    using add_type = decltype(std::declval<L>() + std::declval<R>());

    template <typename L, typename R, typename Value>
    concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_iadd = requires(L& l, R r) { l += r; };

    template <typename L, typename R> requires (has_iadd<L, R>)
    using iadd_type = decltype(std::declval<L&>() += std::declval<R>());

    template <typename L, typename R, typename Value>
    concept iadd_returns = has_iadd<L, R> && convertible_to<iadd_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_sub = requires(L l, R r) {{ l - r };};

    template <typename L, typename R> requires (has_sub<L, R>)
    using sub_type = decltype(std::declval<L>() - std::declval<R>());

    template <typename L, typename R, typename Value>
    concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_isub = requires(L& l, R r) { l -= r; };

    template <typename L, typename R> requires (has_isub<L, R>)
    using isub_type = decltype(std::declval<L&>() -= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept isub_returns = has_isub<L, R> && convertible_to<isub_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_mul = requires(L l, R r) { l * r; };

    template <typename L, typename R> requires (has_mul<L, R>)
    using mul_type = decltype(std::declval<L>() * std::declval<R>());

    template <typename L, typename R, typename Value>
    concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_imul = requires(L& l, R r) { l *= r; };

    template <typename L, typename R> requires (has_imul<L, R>)
    using imul_type = decltype(std::declval<L&>() *= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept imul_returns = has_imul<L, R> && convertible_to<imul_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_truediv = requires(L l, R r) { l / r; };

    template <typename L, typename R> requires (has_truediv<L, R>)
    using truediv_type = decltype(std::declval<L>() / std::declval<R>());

    template <typename L, typename R, typename Value>
    concept truediv_returns =
        has_truediv<L, R> && convertible_to<truediv_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_itruediv = requires(L& l, R r) { l /= r; };

    template <typename L, typename R> requires (has_itruediv<L, R>)
    using itruediv_type = decltype(std::declval<L&>() /= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept itruediv_returns =
        has_itruediv<L, R> && convertible_to<itruediv_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_mod = requires(L l, R r) { l % r; };

    template <typename L, typename R> requires (has_mod<L, R>)
    using mod_type = decltype(std::declval<L>() % std::declval<R>());

    template <typename L, typename R, typename Value>
    concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_imod = requires(L& l, R r) { l %= r; };

    template <typename L, typename R> requires (has_imod<L, R>)
    using imod_type = decltype(std::declval<L&>() %= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept imod_returns =
        has_imod<L, R> && convertible_to<imod_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_lshift = requires(L l, R r) { l << r; };

    template <typename L, typename R> requires (has_lshift<L, R>)
    using lshift_type = decltype(std::declval<L>() << std::declval<R>());

    template <typename L, typename R, typename Value>
    concept lshift_returns =
        has_lshift<L, R> && convertible_to<lshift_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_ilshift = requires(L& l, R r) { l <<= r; };

    template <typename L, typename R> requires (has_ilshift<L, R>)
    using ilshift_type = decltype(std::declval<L&>() <<= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept ilshift_returns =
        has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_rshift = requires(L l, R r) { l >> r; };

    template <typename L, typename R> requires (has_rshift<L, R>)
    using rshift_type = decltype(std::declval<L>() >> std::declval<R>());

    template <typename L, typename R, typename Value>
    concept rshift_returns =
        has_rshift<L, R> && convertible_to<rshift_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_irshift = requires(L& l, R r) { l >>= r; };

    template <typename L, typename R> requires (has_irshift<L, R>)
    using irshift_type = decltype(std::declval<L&>() >>= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept irshift_returns =
        has_irshift<L, R> && convertible_to<irshift_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_and = requires(L l, R r) { l & r; };

    template <typename L, typename R> requires (has_and<L, R>)
    using and_type = decltype(std::declval<L>() & std::declval<R>());

    template <typename L, typename R, typename Value>
    concept and_returns = has_and<L, R> && convertible_to<and_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_iand = requires(L& l, R r) { l &= r; };

    template <typename L, typename R> requires (has_iand<L, R>)
    using iand_type = decltype(std::declval<L&>() &= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept iand_returns = has_iand<L, R> && convertible_to<iand_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_or = requires(L l, R r) { l | r; };

    template <typename L, typename R> requires (has_or<L, R>)
    using or_type = decltype(std::declval<L>() | std::declval<R>());

    template <typename L, typename R, typename Value>
    concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_ior = requires(L& l, R r) { l |= r; };

    template <typename L, typename R> requires (has_ior<L, R>)
    using ior_type = decltype(std::declval<L&>() |= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept ior_returns = has_ior<L, R> && convertible_to<ior_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_xor = requires(L l, R r) { l ^ r; };

    template <typename L, typename R> requires (has_xor<L, R>)
    using xor_type = decltype(std::declval<L>() ^ std::declval<R>());

    template <typename L, typename R, typename Value>
    concept xor_returns = has_xor<L, R> && convertible_to<xor_type<L, R>, Value>;

    template <typename L, typename R>
    concept has_ixor = requires(L& l, R r) { l ^= r; };

    template <typename L, typename R> requires (has_ixor<L, R>)
    using ixor_type = decltype(std::declval<L&>() ^= std::declval<R>());

    template <typename L, typename R, typename Value>
    concept ixor_returns = has_ixor<L, R> && convertible_to<ixor_type<L, R>, Value>;

    namespace nothrow {

        template <typename T>
        concept has_invert =
            meta::has_invert<T> && noexcept(~std::declval<T>());

        template <has_invert T>
        using invert_type = decltype(~std::declval<T>());

        template <typename T, typename Value>
        concept invert_returns = has_invert<T> && convertible_to<invert_type<T>, Value>;

        template <typename T>
        concept has_pos =
            meta::has_pos<T> && noexcept(+std::declval<T>());

        template <has_pos T>
        using pos_type = decltype(+std::declval<T>());

        template <typename T, typename Value>
        concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Value>;

        template <typename T>
        concept has_neg =
            meta::has_neg<T> && noexcept(-std::declval<T>());

        template <has_neg T>
        using neg_type = decltype(-std::declval<T>());

        template <typename T, typename Value>
        concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Value>;

        template <typename T>
        concept has_preincrement =
            meta::has_preincrement<T> && noexcept(++std::declval<T>());

        template <has_preincrement T>
        using preincrement_type = decltype(++std::declval<T>());

        template <typename T, typename Value>
        concept preincrement_returns =
            has_preincrement<T> && convertible_to<preincrement_type<T>, Value>;

        template <typename T>
        concept has_postincrement =
            meta::has_postincrement<T> && noexcept(std::declval<T>()++);

        template <has_postincrement T>
        using postincrement_type = decltype(std::declval<T>()++);

        template <typename T, typename Value>
        concept postincrement_returns =
            has_postincrement<T> && convertible_to<postincrement_type<T>, Value>;

        template <typename T>
        concept has_predecrement =
            meta::has_predecrement<T> && noexcept(--std::declval<T>());

        template <has_predecrement T>
        using predecrement_type = decltype(--std::declval<T>());

        template <typename T, typename Value>
        concept predecrement_returns =
            has_predecrement<T> && convertible_to<predecrement_type<T>, Value>;

        template <typename T>
        concept has_postdecrement =
            meta::has_postdecrement<T> && noexcept(std::declval<T>()--);

        template <has_postdecrement T>
        using postdecrement_type = decltype(std::declval<T>()--);

        template <typename T, typename Return>
        concept postdecrement_returns =
            has_postdecrement<T> && convertible_to<postdecrement_type<T>, Return>;

        template <typename L, typename R>
        concept has_lt =
            meta::has_lt<L, R> && noexcept(std::declval<L>() < std::declval<R>());

        template <typename L, typename R> requires (has_lt<L, R>)
        using lt_type = decltype(std::declval<L>() < std::declval<R>());

        template <typename L, typename R, typename Value>
        concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_le =
            meta::has_le<L, R> && noexcept(std::declval<L>() <= std::declval<R>());

        template <typename L, typename R> requires (has_le<L, R>)
        using le_type = decltype(std::declval<L>() <= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_eq =
            meta::has_eq<L, R> && noexcept(std::declval<L>() == std::declval<R>());

        template <typename L, typename R> requires (has_eq<L, R>)
        using eq_type = decltype(std::declval<L>() == std::declval<R>());

        template <typename L, typename R, typename Value>
        concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_ne =
            meta::has_ne<L, R> && noexcept(std::declval<L>() != std::declval<R>());

        template <typename L, typename R> requires (has_ne<L, R>)
        using ne_type = decltype(std::declval<L>() != std::declval<R>());

        template <typename L, typename R, typename Value>
        concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_ge =
            meta::has_ge<L, R> && noexcept(std::declval<L>() >= std::declval<R>());

        template <typename L, typename R> requires (has_ge<L, R>)
        using ge_type = decltype(std::declval<L>() >= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_gt =
            meta::has_gt<L, R> && noexcept(std::declval<L>() > std::declval<R>());

        template <typename L, typename R> requires (has_gt<L, R>)
        using gt_type = decltype(std::declval<L>() > std::declval<R>());

        template <typename L, typename R, typename Value>
        concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_spaceship =
            meta::has_spaceship<L, R> && noexcept(std::declval<L>() <=> std::declval<R>());

        template <typename L, typename R> requires (has_spaceship<L, R>)
        using spaceship_type =
            decltype(std::declval<L>() <=> std::declval<R>());

        template <typename L, typename R, typename Value>
        concept spaceship_returns =
            has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_add =
            meta::has_add<L, R> && noexcept(std::declval<L>() + std::declval<R>());

        template <typename L, typename R> requires (has_add<L, R>)
        using add_type = decltype(std::declval<L>() + std::declval<R>());

        template <typename L, typename R, typename Value>
        concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_iadd =
            meta::has_iadd<L, R> && noexcept(std::declval<L&>() += std::declval<R>());

        template <typename L, typename R> requires (has_iadd<L, R>)
        using iadd_type = decltype(std::declval<L&>() += std::declval<R>());

        template <typename L, typename R, typename Value>
        concept iadd_returns =
            has_iadd<L, R> && convertible_to<iadd_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_sub =
            meta::has_sub<L, R> && noexcept(std::declval<L>() - std::declval<R>());

        template <typename L, typename R> requires (has_sub<L, R>)
        using sub_type = decltype(std::declval<L>() - std::declval<R>());

        template <typename L, typename R, typename Value>
        concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_isub =
            meta::has_isub<L, R> && noexcept(std::declval<L&>() -= std::declval<R>());

        template <typename L, typename R> requires (has_isub<L, R>)
        using isub_type = decltype(std::declval<L&>() -= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept isub_returns =
            has_isub<L, R> && convertible_to<isub_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_mul =
            meta::has_mul<L, R> && noexcept(std::declval<L>() * std::declval<R>());

        template <typename L, typename R> requires (has_mul<L, R>)
        using mul_type = decltype(std::declval<L>() * std::declval<R>());

        template <typename L, typename R, typename Value>
        concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_imul =
            meta::has_imul<L, R> && noexcept(std::declval<L&>() *= std::declval<R>());

        template <typename L, typename R> requires (has_imul<L, R>)
        using imul_type = decltype(std::declval<L&>() *= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept imul_returns =
            has_imul<L, R> && convertible_to<imul_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_truediv =
            meta::has_truediv<L, R> && noexcept(std::declval<L>() / std::declval<R>());

        template <typename L, typename R> requires (has_truediv<L, R>)
        using truediv_type = decltype(std::declval<L>() / std::declval<R>());

        template <typename L, typename R, typename Value>
        concept truediv_returns =
            has_truediv<L, R> && convertible_to<truediv_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_itruediv =
            meta::has_itruediv<L, R> && noexcept(std::declval<L&>() /= std::declval<R>());

        template <typename L, typename R> requires (has_itruediv<L, R>)
        using itruediv_type = decltype(std::declval<L&>() /= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept itruediv_returns =
            has_itruediv<L, R> && convertible_to<itruediv_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_mod =
            meta::has_mod<L, R> && noexcept(std::declval<L>() % std::declval<R>());

        template <typename L, typename R> requires (has_mod<L, R>)
        using mod_type = decltype(std::declval<L>() % std::declval<R>());

        template <typename L, typename R, typename Value>
        concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_imod =
            meta::has_imod<L, R> && noexcept(std::declval<L&>() %= std::declval<R>());

        template <typename L, typename R> requires (has_imod<L, R>)
        using imod_type = decltype(std::declval<L&>() %= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept imod_returns =
            has_imod<L, R> && convertible_to<imod_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_lshift =
            meta::has_lshift<L, R> && noexcept(std::declval<L>() << std::declval<R>());

        template <typename L, typename R> requires (has_lshift<L, R>)
        using lshift_type = decltype(std::declval<L>() << std::declval<R>());

        template <typename L, typename R, typename Value>
        concept lshift_returns =
            has_lshift<L, R> && convertible_to<lshift_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_ilshift =
            meta::has_ilshift<L, R> && noexcept(std::declval<L&>() <<= std::declval<R>());

        template <typename L, typename R> requires (has_ilshift<L, R>)
        using ilshift_type = decltype(std::declval<L&>() <<= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept ilshift_returns =
            has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_rshift =
            meta::has_rshift<L, R> && noexcept(std::declval<L>() >> std::declval<R>());

        template <typename L, typename R> requires (has_rshift<L, R>)
        using rshift_type = decltype(std::declval<L>() >> std::declval<R>());

        template <typename L, typename R, typename Value>
        concept rshift_returns =
            has_rshift<L, R> && convertible_to<rshift_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_irshift =
            meta::has_irshift<L, R> && noexcept(std::declval<L&>() >>= std::declval<R>());

        template <typename L, typename R> requires (has_irshift<L, R>)
        using irshift_type = decltype(std::declval<L&>() >>= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept irshift_returns =
            has_irshift<L, R> && convertible_to<irshift_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_and =
            meta::has_and<L, R> && noexcept(std::declval<L>() & std::declval<R>());

        template <typename L, typename R> requires (has_and<L, R>)
        using and_type = decltype(std::declval<L>() & std::declval<R>());

        template <typename L, typename R, typename Value>
        concept and_returns =
            has_and<L, R> && convertible_to<and_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_iand =
            meta::has_iand<L, R> && noexcept(std::declval<L&>() &= std::declval<R>());

        template <typename L, typename R> requires (has_iand<L, R>)
        using iand_type = decltype(std::declval<L&>() &= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept iand_returns =
            has_iand<L, R> && convertible_to<iand_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_or =
            meta::has_or<L, R> && noexcept(std::declval<L>() | std::declval<R>());

        template <typename L, typename R> requires (has_or<L, R>)
        using or_type = decltype(std::declval<L>() | std::declval<R>());

        template <typename L, typename R, typename Value>
        concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_ior =
            meta::has_ior<L, R> && noexcept(std::declval<L&>() |= std::declval<R>());

        template <typename L, typename R> requires (has_ior<L, R>)
        using ior_type = decltype(std::declval<L&>() |= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept ior_returns =
            has_ior<L, R> && convertible_to<ior_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_xor =
            meta::has_xor<L, R> && noexcept(std::declval<L>() ^ std::declval<R>());

        template <typename L, typename R> requires (has_xor<L, R>)
        using xor_type = decltype(std::declval<L>() ^ std::declval<R>());

        template <typename L, typename R, typename Value>
        concept xor_returns =
            has_xor<L, R> && convertible_to<xor_type<L, R>, Value>;

        template <typename L, typename R>
        concept has_ixor =
            meta::has_ixor<L, R> && noexcept(std::declval<L&>() ^= std::declval<R>());

        template <typename L, typename R> requires (has_ixor<L, R>)
        using ixor_type = decltype(std::declval<L&>() ^= std::declval<R>());

        template <typename L, typename R, typename Value>
        concept ixor_returns =
            has_ixor<L, R> && convertible_to<ixor_type<L, R>, Value>;

    }





    /// TODO: all other individual operators

    namespace nothrow {

        template <typename T>
        concept can_dereference =
            meta::can_dereference<T> && noexcept(*std::declval<T>());

        template <typename T, typename Value>
        concept dereferences_to =
            can_dereference<T> && meta::dereferences_to<T, Value>;

    }



    /////////////////////////
    ////    STL TYPES    ////
    /////////////////////////
    
    namespace detail {

        template <typename T>
        struct optional { static constexpr bool value = false; };
        template <typename T>
        struct optional<std::optional<T>> {
            static constexpr bool value = true;
            using type = T;
        };

        template <typename T>
        struct variant { static constexpr bool value = false; };
        template <typename... Ts>
        struct variant<std::variant<Ts...>> {
            static constexpr bool value = true;
            using types = std::tuple<Ts...>;
        };

        template <typename T>
        struct shared_ptr { static constexpr bool enable = false; };
        template <typename T>
        struct shared_ptr<std::shared_ptr<T>> {
            static constexpr bool enable = true;
            using type = T;
        };

        template <typename T>
        struct unique_ptr { static constexpr bool enable = false; };
        template <typename T>
        struct unique_ptr<std::unique_ptr<T>> {
            static constexpr bool enable = true;
            using type = T;
        };

        template <typename T>
        struct promise { static constexpr bool enable = false; };
        template <typename T>
        struct promise<std::promise<T>> {
            static constexpr bool enable = true;
            using type = T;
        };

        template <typename Less, typename T>
        struct sorted { using type = void; };

    }



    template <typename T>
    concept is_optional = detail::optional<std::remove_cvref_t<T>>::value;
    template <is_optional T>
    using optional_type = detail::optional<std::remove_cvref_t<T>>::type;

    template <typename T>
    concept is_variant = detail::variant<std::remove_cvref_t<T>>::value;
    template <is_variant T>
    using variant_types = detail::variant<std::remove_cvref_t<T>>::types;

    template <typename T>
    concept is_shared_ptr = detail::shared_ptr<std::remove_cvref_t<T>>::enable;
    template <is_shared_ptr T>
    using shared_ptr_type = detail::shared_ptr<std::remove_cvref_t<T>>::type;

    template <typename T>
    concept is_unique_ptr = detail::unique_ptr<std::remove_cvref_t<T>>::enable;
    template <is_unique_ptr T>
    using unique_ptr_type = detail::unique_ptr<std::remove_cvref_t<T>>::type;

    template <typename T>
    concept is_promise = detail::promise<std::remove_cvref_t<T>>::enable;
    template <is_promise T>
    using promise_type = detail::promise<std::remove_cvref_t<T>>::type;






    

    template <class A, class T>
    concept allocator_for =
        // 1) A must have member alias value_type which equals T
        requires { typename A::value_type; } &&
        std::same_as<typename A::value_type, T> &&

        // 2) A must be copy and move constructible/assignable
        std::is_copy_constructible_v<A> &&
        std::is_copy_assignable_v<A> &&
        std::is_move_constructible_v<A> &&
        std::is_move_assignable_v<A> &&

        // 3) A must be equality comparable
        requires(A a, A b) {
            { a == b } -> std::convertible_to<bool>;
            { a != b } -> std::convertible_to<bool>;
        } &&

        // 4) A must be able to allocate and deallocate
        requires(A a, T* ptr, size_t n) {
            { a.allocate(n) } -> std::convertible_to<T*>;
            { a.deallocate(ptr, n) };
        };

}


/* A generic sentinel type to simplify iterator implementations. */
struct sentinel {};


/* A simple convenience struct implementing the overload pattern for `visit()`-style
functions. */
template <typename... Funcs>
struct visitor : Funcs... { using Funcs::operator()...; };


/* CPython exception types:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * Inheritance hierarchy:
 *      https://docs.python.org/3/library/exceptions.html#exception-hierarchy
 */


/* The root of the bertrand exception hierarchy.  This and all its subclasses are
usable just like their built-in Python equivalents, and maintain coherent stack traces
across both languages.  If Python is not loaded, then the same exception types can
still be used in a pure C++ context, but the `from_python()` and `to_python()` helpers
will be disabled. */
struct Exception : public std::exception {
protected:
    /// NOTE: cpptrace always stores the most recent frame first.

    std::string m_message;
    mutable size_t m_skip = 0;
    mutable std::string m_what;
    union {
        mutable cpptrace::raw_trace m_raw_trace;
        mutable cpptrace::stacktrace m_stacktrace;
    };
    mutable enum : uint8_t {
        NO_TRACE,
        RAW_TRACE,
        STACK_TRACE
    } m_trace_type = NO_TRACE;

    struct get_trace {
        size_t skip = 1;
        constexpr get_trace operator++(int) const noexcept { return {skip + 1}; }
    };

    static std::string format_frame(const cpptrace::stacktrace_frame& frame) {
        std::string result = "    File \"" + frame.filename + "\", line ";
        if (frame.line.has_value()) {
            result += std::to_string(frame.line.value()) + ", in ";
        } else {
            result += "<unknown>, in ";
        }
        if (frame.is_inline) {
            result += "[inline] ";
        }
        result += frame.symbol + "\n";
        return result;
    }

    template <typename Msg> requires (std::constructible_from<std::string, Msg>)
    explicit constexpr Exception(get_trace trace, Msg&& msg) :
        m_message(std::forward<Msg>(msg)),
        m_skip(trace.skip + 1)  // skip this constructor
    {
        if !consteval {
            new (&m_raw_trace) cpptrace::raw_trace(cpptrace::generate_raw_trace());
        }
    }

public:

    template <typename Msg = const char*>
        requires (std::constructible_from<std::string, Msg>)
    explicit constexpr Exception(Msg&& msg = "") :
        m_message(std::forward<Msg>(msg)),
        m_skip(1)  // skip this constructor
    {
        if !consteval {
            new (&m_raw_trace) cpptrace::raw_trace(cpptrace::generate_raw_trace());
        }
    }

    template <typename Msg = const char*>
        requires (std::constructible_from<std::string, Msg>)
    explicit Exception(cpptrace::raw_trace&& trace, Msg&& msg = "") :
        m_message(std::forward<Msg>(msg)),
        m_trace_type(RAW_TRACE)
    {
        new (&m_raw_trace) cpptrace::raw_trace(std::move(trace));
    }

    template <typename Msg = const char*>
        requires (std::constructible_from<std::string, Msg>)
    explicit Exception(cpptrace::stacktrace&& trace, Msg&& msg) :
        m_message(std::forward<Msg>(msg)),
        m_trace_type(STACK_TRACE)
    {
        new (&m_stacktrace) cpptrace::stacktrace(std::move(trace));
    }

    Exception(const Exception& other) :
        m_message(other.m_message),
        m_skip(other.m_skip),
        m_what(other.m_what),
        m_trace_type(other.m_trace_type)
    {
        if !consteval {
            switch (m_trace_type) {
                case RAW_TRACE:
                    new (&m_raw_trace) cpptrace::raw_trace(other.m_raw_trace);
                    break;
                case STACK_TRACE:
                    new (&m_stacktrace) cpptrace::stacktrace(other.m_stacktrace);
                    break;
                default:
                    break;
            }
        }
    }

    Exception(Exception&& other) :
        m_message(std::move(other.m_message)),
        m_skip(other.m_skip),
        m_what(std::move(other.m_what)),
        m_trace_type(other.m_trace_type)
    {
        if !consteval {
            switch (m_trace_type) {
                case RAW_TRACE:
                    new (&m_raw_trace) cpptrace::raw_trace(std::move(other.m_raw_trace));
                    break;
                case STACK_TRACE:
                    new (&m_stacktrace) cpptrace::stacktrace(std::move(other.m_stacktrace));
                    break;
                default:
                    break;
            }
        }
    }

    Exception& operator=(const Exception& other) {
        if (&other != this) {
            m_message = other.m_message;
            m_skip = other.m_skip;
            m_what = other.m_what;
            m_trace_type = other.m_trace_type;
            if !consteval {
                switch (m_trace_type) {
                    case RAW_TRACE:
                        new (&m_raw_trace) cpptrace::raw_trace(other.m_raw_trace);
                        break;
                    case STACK_TRACE:
                        new (&m_stacktrace) cpptrace::stacktrace(other.m_stacktrace);
                        break;
                    default:
                        break;
                }
            }
        }
        return *this;
    }

    Exception& operator=(Exception&& other) {
        if (&other != this) {
            m_message = std::move(other.m_message);
            m_skip = other.m_skip;
            m_what = std::move(other.m_what);
            m_trace_type = other.m_trace_type;
            if !consteval {
                switch (m_trace_type) {
                    case RAW_TRACE:
                        new (&m_raw_trace) cpptrace::raw_trace(std::move(other.m_raw_trace));
                        break;
                    case STACK_TRACE:
                        new (&m_stacktrace) cpptrace::stacktrace(std::move(other.m_stacktrace));
                        break;
                    default:
                        break;
                }
            }
        }
        return *this;
    }

    ~Exception() noexcept {
        if !consteval {
            switch (m_trace_type) {
                case RAW_TRACE:
                    m_raw_trace.~raw_trace();
                    break;
                case STACK_TRACE:
                    m_stacktrace.~stacktrace();
                    break;
                default:
                    break;
            }
        }
    }

    /* Skip the `n` most recent frames in the stack trace.  Note that this works by
    incrementing an internal counter, so no extra traces are resolved at runtime, and
    it is not guaranteed that the first skipped frame is the current one, unless all
    earlier frames have been already been skipped in a similar fashion.  Forwards the
    exception itself for simplified chaining (e.g. `throw exc.skip(2)`). */
    template <typename Self>
    constexpr decltype(auto) skip(this Self&& self, size_t n = 0) noexcept {
        if !consteval {
            ++n;  // always skip this method
            if (self.m_trace_type == STACK_TRACE) {
                if (n >= self.m_stacktrace.frames.size()) {
                    self.m_stacktrace.frames.clear();
                } else {
                    self.m_stacktrace.frames.erase(
                        self.m_stacktrace.frames.begin(),
                        self.m_stacktrace.frames.begin() + n
                    );
                }
            }
            self.m_skip += n;
        }
        return std::forward<Self>(self);
    }

    /* Discard any frames that are more recent than the frame in which this method was
    invoked, or an earlier frame if an offset is supplied.  Forwards the exception
    itself for simplified chaining (e.g. `throw exc.trim_before()`), and also resets
    the `skip()` counter to start counting from the current frame. */
    template <typename Self>
    constexpr decltype(auto) trim_before(this Self&& self, size_t offset = 0) noexcept {
        if !consteval {
            ++offset;  // always skip this method
            cpptrace::raw_trace curr = cpptrace::generate_raw_trace();
            if (offset > curr.frames.size()) {
                return std::forward<Self>(self);  // no frames to cut
            }
            cpptrace::frame_ptr pivot = curr.frames[curr.frames.size() - offset];
            switch (self.m_trace_type) {
                case RAW_TRACE:
                    for (size_t i = self.m_raw_trace.frames.size(); i-- > self.m_skip;) {
                        if (self.m_raw_trace.frames[i] == pivot) {
                            self.m_raw_trace.frames.erase(
                                self.m_raw_trace.frames.begin(),
                                self.m_raw_trace.frames.begin() + i
                            );
                            self.m_skip = 0;
                            return std::forward<Self>(self);
                        }
                    }
                    break;
                case STACK_TRACE:
                    for (size_t i = self.m_stacktrace.frames.size(); i-- > self.m_skip;) {
                        if (self.m_stacktrace.frames[i].raw_address == pivot) {
                            self.m_stacktrace.frames.erase(
                                self.m_stacktrace.frames.begin(),
                                self.m_stacktrace.frames.begin() + i
                            );
                            self.m_skip = 0;
                            return std::forward<Self>(self);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        return std::forward<Self>(self);
    }

    /* Discard any frames that are less recent than the frame in which this method was
    invoked, or a later frame if an offset is supplied.  Forwards the exception
    itself for simplified chaining (e.g. `throw exc.trim_after()`) */
    template <typename Self>
    constexpr decltype(auto) trim_after(this Self&& self, size_t offset = 0) noexcept {
        if !consteval {
            ++offset;  // always skip this method
            cpptrace::raw_trace curr = cpptrace::generate_raw_trace();
            if (offset > curr.frames.size()) {
                return std::forward<Self>(self);  // no frames to cut
            }
            cpptrace::frame_ptr pivot = curr.frames[offset];
            switch (self.m_trace_type) {
                case RAW_TRACE:
                    for (size_t i = self.m_skip; i < self.m_raw_trace.frames.size(); ++i) {
                        if (self.m_raw_trace.frames[i] == pivot) {
                            self.m_raw_trace.frames.resize(i + 1);
                            return std::forward<Self>(self);
                        }
                    }
                    break;
                case STACK_TRACE:
                    for (size_t i = self.m_skip; i < self.m_stacktrace.frames.size(); ++i) {
                        if (self.m_stacktrace.frames[i].raw_address == pivot) {
                            self.m_stacktrace.frames.resize(i + 1);
                            return std::forward<Self>(self);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        return std::forward<Self>(self);
    }

    /* The raw text of the exception message, sans exception type and traceback. */
    constexpr std::string_view message() const noexcept {
        return m_message;
    }

    /* A resolved trace to the source location where the error occurred, with internal
    C++/Python frames removed.  The trace is lazily loaded directly from the program
    counter when first accessed (typically only when an unhandled exception is
    displayed via the `what()` method).  This may return a null pointer if the
    exception has no traceback to report, which only occurs when an exception is thrown
    in a constexpr context (C++26 and later). */
    constexpr const cpptrace::stacktrace* trace() const noexcept {
        if !consteval {
            if (m_trace_type == STACK_TRACE) {
                return &m_stacktrace;
            } else if (m_trace_type == RAW_TRACE) {
                cpptrace::stacktrace trace = m_raw_trace.resolve();
                cpptrace::stacktrace filtered;
                if (m_skip < trace.frames.size()) {
                    filtered.frames.reserve(trace.frames.size() - m_skip);
                }
                for (size_t i = m_skip; i < trace.frames.size(); ++i) {
                    cpptrace::stacktrace_frame& frame = trace.frames[i];
                    if constexpr (!DEBUG) {
                        if (frame.symbol.starts_with("__")) {
                            continue;  // filter out C++ internals in release mode
                        }
                    }
                    filtered.frames.emplace_back(std::move(frame));
                }
                m_raw_trace.~raw_trace();
                new (&m_stacktrace) cpptrace::stacktrace(std::move(filtered));
                m_trace_type = STACK_TRACE;
                return &m_stacktrace;
            }
        }
        return nullptr;
    }

    /* A type index for this exception, which can be searched in the global
    `to_python()` map to find a corresponding callback. */
    virtual std::type_index type() const noexcept {
        return typeid(Exception);
    }

    /* The plaintext name of the exception type, displayed immediately before the
    error message. */
    constexpr virtual std::string_view name() const noexcept {
        return "Exception";
    }

    /* The full exception diagnostic, including a coherent, Python-style traceback and
    error text. */
    constexpr virtual const char* what() const noexcept override {
        if (m_what.empty()) {
            m_what = "Traceback (most recent call last):\n";
            if (const cpptrace::stacktrace* trace = this->trace()) {
                for (size_t i = trace->frames.size(); i-- > 0;) {
                    m_what += format_frame(trace->frames[i]);
                }
            }
            m_what += name();
            m_what += ": ";
            m_what += message();
        }
        return m_what.data();
    }

    /* Clear the exception's what() cache, forcing it to be recomputed the next time
    it is requested. */
    void flush() noexcept {
        m_what.clear();
    }

    /* Throw the most recent C++ exception as a corresponding Python error, pushing it
    onto the active interpreter.  If there is no unhandled exception for this thread or
    no callback could be found (for instance if Python isn't loaded), then this will
    terminate the program instead. */
    static void to_python() noexcept;

    /* Catch an exception from Python, re-throwing it as an equivalent C++ error. */
    [[noreturn]] static void from_python();

};
    

#define BERTRAND_EXCEPTION(CLS, BASE)                                                   \
    struct CLS : BASE {                                                                 \
    protected:                                                                          \
                                                                                        \
        template <typename Msg> requires (std::constructible_from<std::string, Msg>)    \
        explicit constexpr CLS(get_trace trace, Msg&& msg) : BASE(                      \
            trace++,                                                                    \
            std::forward<Msg>(msg)                                                      \
        ) {}                                                                            \
                                                                                        \
    public:                                                                             \
        virtual std::type_index type() const noexcept override { return typeid(CLS); }  \
        constexpr virtual std::string_view name() const noexcept override { return #CLS; } \
                                                                                        \
        template <typename Msg = const char*>                                           \
            requires (std::constructible_from<std::string, Msg>)                        \
        explicit constexpr CLS(Msg&& msg = "") : BASE(                                  \
            get_trace{},                                                                \
            std::forward<Msg>(msg)                                                      \
        ) {}                                                                            \
                                                                                        \
        template <typename Msg = const char*>                                           \
            requires (std::constructible_from<std::string, Msg>)                        \
        explicit CLS(cpptrace::raw_trace&& trace, Msg&& msg = "") : BASE(               \
            std::move(trace),                                                           \
            std::forward<Msg>(msg)                                                      \
        ) {}                                                                            \
                                                                                        \
        template <typename Msg = const char*>                                           \
            requires (std::constructible_from<std::string, Msg>)                        \
        explicit CLS(cpptrace::stacktrace&& trace, Msg&& msg = "") : BASE(              \
            std::move(trace),                                                           \
            std::forward<Msg>(msg)                                                      \
        ) {}                                                                            \
    };


BERTRAND_EXCEPTION(ArithmeticError, Exception)
    BERTRAND_EXCEPTION(FloatingPointError, ArithmeticError)
    BERTRAND_EXCEPTION(OverflowError, ArithmeticError)
    BERTRAND_EXCEPTION(ZeroDivisionError, ArithmeticError)
BERTRAND_EXCEPTION(AssertionError, Exception)
BERTRAND_EXCEPTION(AttributeError, Exception)
BERTRAND_EXCEPTION(BufferError, Exception)
BERTRAND_EXCEPTION(EOFError, Exception)
BERTRAND_EXCEPTION(ImportError, Exception)
    BERTRAND_EXCEPTION(ModuleNotFoundError, ImportError)
BERTRAND_EXCEPTION(LookupError, Exception)
    BERTRAND_EXCEPTION(IndexError, LookupError)
    BERTRAND_EXCEPTION(KeyError, LookupError)
BERTRAND_EXCEPTION(MemoryError, Exception)
BERTRAND_EXCEPTION(NameError, Exception)
    BERTRAND_EXCEPTION(UnboundLocalError, NameError)
BERTRAND_EXCEPTION(OSError, Exception)
    BERTRAND_EXCEPTION(BlockingIOError, OSError)
    BERTRAND_EXCEPTION(ChildProcessError, OSError)
    BERTRAND_EXCEPTION(ConnectionError, OSError)
        BERTRAND_EXCEPTION(BrokenPipeError, ConnectionError)
        BERTRAND_EXCEPTION(ConnectionAbortedError, ConnectionError)
        BERTRAND_EXCEPTION(ConnectionRefusedError, ConnectionError)
        BERTRAND_EXCEPTION(ConnectionResetError, ConnectionError)
    BERTRAND_EXCEPTION(FileExistsError, OSError)
    BERTRAND_EXCEPTION(FileNotFoundError, OSError)
    BERTRAND_EXCEPTION(InterruptedError, OSError)
    BERTRAND_EXCEPTION(IsADirectoryError, OSError)
    BERTRAND_EXCEPTION(NotADirectoryError, OSError)
    BERTRAND_EXCEPTION(PermissionError, OSError)
    BERTRAND_EXCEPTION(ProcessLookupError, OSError)
    BERTRAND_EXCEPTION(TimeoutError, OSError)
BERTRAND_EXCEPTION(ReferenceError, Exception)
BERTRAND_EXCEPTION(RuntimeError, Exception)
    BERTRAND_EXCEPTION(NotImplementedError, RuntimeError)
    BERTRAND_EXCEPTION(RecursionError, RuntimeError)
BERTRAND_EXCEPTION(StopAsyncIteration, Exception)
BERTRAND_EXCEPTION(StopIteration, Exception)
BERTRAND_EXCEPTION(SyntaxError, Exception)
    BERTRAND_EXCEPTION(IndentationError, SyntaxError)
        BERTRAND_EXCEPTION(TabError, IndentationError)
BERTRAND_EXCEPTION(SystemError, Exception)
BERTRAND_EXCEPTION(TypeError, Exception)
BERTRAND_EXCEPTION(ValueError, Exception)
    BERTRAND_EXCEPTION(UnicodeError, ValueError)
        // BERTRAND_EXCEPTION(UnicodeDecodeError, UnicodeError)
        // BERTRAND_EXCEPTION(UnicodeEncodeError, UnicodeError)
        // BERTRAND_EXCEPTION(UnicodeTranslateError, UnicodeError)


#undef BERTRAND_EXCEPTION


struct UnicodeDecodeError : UnicodeError {
protected:
    explicit constexpr UnicodeDecodeError(
        get_trace trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            trace++,
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

public:
    virtual std::type_index type() const noexcept override {
        return typeid(UnicodeDecodeError);
    }

    constexpr virtual std::string_view name() const noexcept override {
        return "UnicodeDecodeError";
    }

    std::string encoding;
    std::string object;
    ssize_t start;
    ssize_t end;
    std::string reason;

    explicit constexpr UnicodeDecodeError(
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            get_trace{},
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    explicit UnicodeDecodeError(
        cpptrace::raw_trace&& trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            std::move(trace),
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    explicit UnicodeDecodeError(
        cpptrace::stacktrace&& trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            std::move(trace),
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}
};


struct UnicodeEncodeError : UnicodeError {
protected:
    explicit constexpr UnicodeEncodeError(
        get_trace trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            trace++,
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

public:
    virtual std::type_index type() const noexcept override {
        return typeid(UnicodeEncodeError);
    }

    constexpr virtual std::string_view name() const noexcept override {
        return "UnicodeEncodeError";
    }

    std::string encoding;
    std::string object;
    ssize_t start;
    ssize_t end;
    std::string reason;

    explicit constexpr UnicodeEncodeError(
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            get_trace{},
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    explicit UnicodeEncodeError(
        cpptrace::raw_trace&& trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            std::move(trace),
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    explicit UnicodeEncodeError(
        cpptrace::stacktrace&& trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            std::move(trace),
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}
};


struct UnicodeTranslateError : UnicodeError {
protected:
    explicit constexpr UnicodeTranslateError(
        get_trace trace,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            trace++,
            "can't translate characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

public:
    virtual std::type_index type() const noexcept override {
        return typeid(UnicodeTranslateError);
    }

    constexpr virtual std::string_view name() const noexcept override {
        return "UnicodeTranslateError";
    }

    std::string object;
    ssize_t start;
    ssize_t end;
    std::string reason;

    explicit constexpr UnicodeTranslateError(
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            get_trace{},
            "can't translate characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    explicit UnicodeTranslateError(
        cpptrace::raw_trace&& trace,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            std::move(trace),
            "can't translate characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    explicit UnicodeTranslateError(
        cpptrace::stacktrace&& trace,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) :
        UnicodeError(
            std::move(trace),
            "can't translate characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}
};


namespace impl {

    struct virtualenv;
    static virtualenv get_virtual_environment() noexcept;

    struct virtualenv {
    private:
        friend virtualenv get_virtual_environment() noexcept;

        virtualenv() = default;

    public:
        std::filesystem::path path = [] {
            if (const char* path = std::getenv("BERTRAND_HOME")) {
                return std::filesystem::path(path);
            }
            return std::filesystem::path();
        }();
        std::filesystem::path bin = *this ? path / "bin" : std::filesystem::path();
        std::filesystem::path lib = *this ? path / "lib" : std::filesystem::path();
        std::filesystem::path include = *this ? path / "include" : std::filesystem::path(); 
        std::filesystem::path modules = *this ? path / "modules" : std::filesystem::path();

        virtualenv(const virtualenv&) = delete;
        virtualenv(virtualenv&&) = delete;
        virtualenv& operator=(const virtualenv&) = delete;
        virtualenv& operator=(virtualenv&&) = delete;

        explicit operator bool() const noexcept {
            return !path.empty();
        }
    };

    static virtualenv get_virtual_environment() noexcept {
        return virtualenv();
    }

    /* Modular integer multiplication. */
    template <std::integral T>
    constexpr T mul_mod(T a, T b, T mod) noexcept {
        T result = 0, y = a % mod;
        while (b > 0) {
            if (b & 1) {
                result = (result + y) % mod;
            }
            y = (y << 1) % mod;
            b >>= 1;
        }
        return result % mod;
    }

    /* Modular integer exponentiation. */
    template <std::integral T>
    constexpr T exp_mod(T base, T exp, T mod) noexcept {
        T result = 1;
        T y = base;
        while (exp > 0) {
            if (exp & 1) {
                result = (result * y) % mod;
            }
            y = (y * y) % mod;
            exp >>= 1;
        }
        return result % mod;
    }

    /* Deterministic Miller-Rabin primality test with a fixed set of bases valid for
    n < 2^64.  Can be computed at compile time, and guaranteed not to produce false
    positives. */
    template <std::integral T>
    constexpr bool is_prime(T n) noexcept {
        if ((n & 1) == 0) {
            return n == 2;
        } else if (n < 2) {
            return false;
        }

        T d = n - 1;
        int r = 0;
        while ((d & 1) == 0) {
            d >>= 1;
            ++r;
        }

        constexpr auto test = [](T n, T d, int r, T a) noexcept {
            T x = exp_mod(a, d, n);
            if (x == 1 || x == n - 1) {
                return true;  // probably prime
            }
            for (int i = 0; i < r - 1; ++i) {
                x = mul_mod(x, x, n);
                if (x == n - 1) {
                    return true;  // probably prime
                }
            }
            return false;  // composite
        };

        constexpr T bases[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
        for (T a : bases) {
            if (a >= n) {
                break;  // only test bases < n
            }
            if (!test(n, d, r, a)) {
                return false;
            }
        }

        return true;
    }

    /* Computes the next prime after a given value by applying a deterministic Miller-Rabin
    primality test, which can be computed at compile time. */
    template <std::integral T>
    constexpr T next_prime(T n) noexcept {
        for (T i = (n + 1) | 1, end = 2 * n; i < end; i += 2) {
            if (is_prime(i)) {
                return i;
            }
        }
        return 2;  // only returned for n < 2
    }

    /* Compute the next power of two greater than or equal to a given value. */
    template <std::unsigned_integral T>
    constexpr T next_power_of_two(T n) noexcept {
        --n;
        for (size_t i = 1, bits = sizeof(T) * 8; i < bits; i <<= 1) {
            n |= (n >> i);
        }
        return ++n;
    }

    /* Get the log base 2 of a number. */
    template <std::unsigned_integral T>
    constexpr size_t log2(T n) noexcept {
        size_t count = 0;
        while (n >>= 1) {
            ++count;
        }
        return count;
    }

    /* A fast modulo operator that works for any b power of two. */
    template <std::unsigned_integral T>
    constexpr T mod2(T a, T b) noexcept {
        return a & (b - 1);
    }

    /* A Python-style modulo operator (%).

    NOTE: Python's `%` operator is defined such that the result has the same sign as the
    divisor (b).  This differs from C/C++, where the result has the same sign as the
    dividend (a). */
    template <std::integral T>
    constexpr T pymod(T a, T b) noexcept {
        return (a % b + b) % b;
    }

    /* Default seed for FNV-1a hash function. */
    constexpr size_t fnv1a_seed = [] {
        if constexpr (sizeof(size_t) > 4) {
            return 14695981039346656037ULL;
        } else {
            return 2166136261u;
        }
    }();

    /* Default prime for FNV-1a hash function. */
    constexpr size_t fnv1a_prime = [] {
        if constexpr (sizeof(size_t) > 4) {
            return 1099511628211ULL;
        } else {
            return 16777619u;
        }
    }();

    /* A deterministic FNV-1a string hashing function that gives the same results at both
    compile time and run time. */
    constexpr size_t fnv1a(
        const char* str,
        size_t seed = fnv1a_seed,
        size_t prime = fnv1a_prime
    ) noexcept {
        while (*str) {
            seed ^= static_cast<size_t>(*str);
            seed *= prime;
            ++str;
        }
        return seed;
    }

    /* A wrapper around the `bertrand::fnv1a()` function that allows it to be used in
    template expressions. */
    struct FNV1a {
        static constexpr size_t operator()(
            const char* str,
            size_t seed = fnv1a_seed,
            size_t prime = fnv1a_prime
        ) noexcept {
            return fnv1a(str, seed, prime);
        }
    };

    /* Merge several hashes into a single value.  Based on `boost::hash_combine()`:
    https://www.boost.org/doc/libs/1_86_0/libs/container_hash/doc/html/hash.html#notes_hash_combine */
    template <std::convertible_to<size_t>... Hashes>
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

    /* Apply python-style wraparound to a given index, throwing an `IndexError` if the
    index is out of bounds after normalizing. */
    inline constexpr ssize_t normalize_index(size_t size, ssize_t i) {
        ssize_t n = static_cast<ssize_t>(size);
        ssize_t j = i + n * (i < 0);
        if (j < 0 || j >= n) {
            throw IndexError(std::to_string(i));
        }
        return j;
    }

    /* Apply python-style wraparound to a given index, truncating to the nearest edge
    if the index is out of bounds after normalizing. */
    inline constexpr ssize_t truncate_index(size_t size, ssize_t i) noexcept {
        ssize_t n = static_cast<ssize_t>(size);
        i += n * (i < 0);
        if (i < 0) {
            return 0;
        } else if (i >= n) {
            return n;
        }
        return i;
    }

    template <typename T>
    struct contiguous_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = ssize_t;
        using value_type = T;
        using reference = std::add_lvalue_reference_t<value_type>;
        using const_reference = std::add_lvalue_reference_t<std::add_const_t<value_type>>;
        using pointer = std::add_pointer_t<value_type>;
        using const_pointer = std::add_pointer_t<std::add_const_t<value_type>>;

        pointer data = nullptr;
        difference_type index = 0;

        constexpr contiguous_iterator& operator=(const contiguous_iterator&) noexcept = default;
        constexpr contiguous_iterator& operator=(contiguous_iterator&&) noexcept = default;

        template <typename V> requires (std::assignable_from<reference, V>)
        [[maybe_unused]] constexpr contiguous_iterator& operator=(V&& value) && noexcept(
            noexcept(data[index] = std::forward<V>(value))
        ) {
            data[index] = std::forward<V>(value);
            return *this;
        }

        template <typename V> requires (std::convertible_to<reference, V>)
        [[nodiscard]] constexpr operator V() && noexcept(noexcept(V(**this))) {
            return **this;
        }

        [[nodiscard]] constexpr reference operator*() noexcept {
            return data[index];
        }

        [[nodiscard]] constexpr const_reference operator*() const noexcept {
            return data[index];
        }

        [[nodiscard]] constexpr pointer operator->() noexcept {
            return data + index;
        }

        [[nodiscard]] constexpr const_pointer operator->() const noexcept {
            return data + index;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) noexcept {
            return data[index + n];
        }

        [[nodiscard]] constexpr const_reference operator[](difference_type n) const noexcept {
            return data[index + n];
        }

        [[maybe_unused]] constexpr contiguous_iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator++(int) noexcept {
            contiguous_iterator copy = *this;
            ++(*this);
            return copy;
        }

        [[maybe_unused]] constexpr contiguous_iterator& operator+=(
            difference_type n
        ) noexcept {
            index += n;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator+(
            difference_type n
        ) const noexcept {
            return {data, index + n};
        }

        [[maybe_unused]] constexpr contiguous_iterator& operator--() noexcept {
            --index;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator--(int) noexcept {
            contiguous_iterator copy = *this;
            --(*this);
            return copy;
        }

        [[maybe_unused]] constexpr contiguous_iterator& operator-=(
            difference_type n
        ) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator-(
            difference_type n
        ) const noexcept {
            return {data, index - n};
        }

        [[nodiscard]] friend constexpr bool operator<(
            const contiguous_iterator& lhs,
            const contiguous_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.data != rhs.data) {
                    throw AssertionError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.index < rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator<=(
            const contiguous_iterator& lhs,
            const contiguous_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.data != rhs.data) {
                    throw AssertionError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.index <= rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const contiguous_iterator& lhs,
            const contiguous_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.data != rhs.data) {
                    throw AssertionError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.index == rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const contiguous_iterator& lhs,
            const contiguous_iterator& rhs
        ) noexcept(!DEBUG) {
            return !(lhs == rhs);
        }

        [[nodiscard]] friend constexpr bool operator>=(
            const contiguous_iterator& lhs,
            const contiguous_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.data != rhs.data) {
                    throw AssertionError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.index >= rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator>(
            const contiguous_iterator& lhs,
            const contiguous_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.data != rhs.data) {
                    throw AssertionError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.index > rhs.index;
        }
    };

    template <typename T>
    struct contiguous_slice {
    private:

        struct initializer {
            std::initializer_list<T>& items;
            [[nodiscard]] constexpr size_t size() const noexcept { return items.size(); }
            [[nodiscard]] constexpr auto begin() const noexcept { return items.begin(); }
            [[nodiscard]] constexpr auto end() const noexcept { return items.end(); }
        };

        void normalize(
            ssize_t size,
            std::optional<ssize_t> start,
            std::optional<ssize_t> stop
        ) noexcept {
            // normalize start, correcting for negative indices and truncating to bounds
            if (!start) {
                m_start = 0;
            } else {
                m_start = *start + size * (*start < 0);
                if (m_start < 0) {
                    m_start = 0;
                } else if (m_start > size) {
                    m_start = size;
                }
            }

            // normalize stop, correcting for negative indices and truncating to bounds
            if (!stop) {
                m_stop = size;
            } else {
                m_stop = *stop + size * (*stop < 0);
                if (m_stop < 0) {
                    m_stop = 0;
                } else if (m_stop > size) {
                    m_stop = size;
                }
            }

            // compute number of included elements
            ssize_t bias = m_step + (m_step < ssize_t(0)) - (m_step > ssize_t(0));
            m_size = (m_stop - m_start + bias) / m_step;
            m_size *= (m_size > ssize_t(0));
        }

        void normalize(
            ssize_t size,
            std::optional<ssize_t> start,
            std::optional<ssize_t> stop,
            std::optional<ssize_t> step
        ) {
            // normalize step, defaulting to 1
            m_step = step.value_or(1);
            if (m_step == 0) {
                throw ValueError("slice step cannot be zero");
            };
            bool neg = m_step < 0;

            // normalize start, correcting for negative indices and truncating to bounds
            if (!start) {
                m_start = neg ? size - 1 : 0;  // neg: size - 1 | pos: 0
            } else {
                m_start = *start + size * (*start < 0);
                if (m_start < 0) {
                    m_start = -neg;  // neg: -1 | pos: 0
                } else if (m_start >= size) {
                    m_start = size - neg;  // neg: size - 1 | pos: size
                }
            }

            // normalize stop, correcting for negative indices and truncating to bounds
            if (!stop) {
                m_stop = neg ? -1 : size;  // neg: -1 | pos: size
            } else {
                m_stop = *stop + size * (*stop < 0);
                if (m_stop < 0) {
                    m_stop = -neg;  // neg: -1 | pos: 0
                } else if (m_stop >= size) {
                    m_stop = size - neg;  // neg: size - 1 | pos: size
                }
            }

            // compute number of included elements
            ssize_t bias = m_step + (m_step < ssize_t(0)) - (m_step > ssize_t(0));
            m_size = (m_stop - m_start + bias) / m_step;
            m_size *= (m_size > ssize_t(0));
        }

        template <typename V>
        struct iter {
            using iterator_category = std::input_iterator_tag;
            using difference_type = ssize_t;
            using value_type = V;
            using reference = std::add_lvalue_reference_t<value_type>;
            using const_reference = std::add_lvalue_reference_t<std::add_const_t<value_type>>;
            using pointer = std::add_pointer_t<value_type>;
            using const_pointer = std::add_pointer_t<std::add_const_t<value_type>>;

            pointer data = nullptr;
            ssize_t index = 0;
            ssize_t step = 1;

            [[nodiscard]] constexpr reference operator*() noexcept {
                return data[index];
            }

            [[nodiscard]] constexpr const_reference operator*() const noexcept {
                return data[index];
            }

            [[nodiscard]] constexpr pointer operator->() noexcept {
                return data + index;
            }

            [[nodiscard]] constexpr const_pointer operator->() const noexcept {
                return data + index;
            }

            [[maybe_unused]] iter& operator++() noexcept {
                index += step;
                return *this;
            }

            [[nodiscard]] iter operator++(int) noexcept {
                iterator copy = *this;
                ++(*this);
                return copy;
            }

            [[nodiscard]] constexpr bool operator==(const iter& other) noexcept {
                return step > 0 ? index >= other.index : index <= other.index;
            }

            [[nodiscard]] constexpr bool operator!=(const iter& other) noexcept {
                return !(*this == other);
            }
        };

    public:
        using value_type = T;
        using reference = std::add_lvalue_reference_t<value_type>;
        using const_reference = std::add_lvalue_reference_t<std::add_const_t<value_type>>;
        using pointer = std::add_pointer_t<value_type>;
        using const_pointer = std::add_pointer_t<std::add_const_t<value_type>>;
        using iterator = iter<value_type>;
        using const_iterator = iter<std::add_const_t<value_type>>;

        constexpr contiguous_slice(
            pointer data,
            ssize_t size,
            const std::initializer_list<std::optional<ssize_t>>& slice
        ) {
            auto it = slice.begin();
            auto end = slice.end();
            if (it == end) {
                normalize(size, std::nullopt, std::nullopt);
                return;
            }
            std::optional<ssize_t> start = *it++;
            if (it == end) {
                normalize(size, start, std::nullopt);
                return;
            }
            std::optional<ssize_t> stop = *it++;
            if (it == end) {
                normalize(size, start, stop);
                return;
            }
            std::optional<ssize_t> step = *it++;
            if (it == end) {
                normalize(size, start, stop, step);
            }
            throw IndexError(
                "Slices must be of the form {[start[, stop[, step]]]} "
                "(received " + std::to_string(slice.size()) + " indices)"
            );
        }

        [[nodiscard]] constexpr pointer data() const noexcept { return m_data; }
        [[nodiscard]] constexpr ssize_t start() const noexcept { return m_start; }
        [[nodiscard]] constexpr ssize_t stop() const noexcept { return m_stop; }
        [[nodiscard]] constexpr ssize_t step() const noexcept { return m_step; }
        [[nodiscard]] constexpr ssize_t size() const noexcept { return m_size; }
        [[nodiscard]] constexpr bool empty() const noexcept { return !size(); }
        [[nodiscard]] explicit operator bool() const noexcept { return size(); }

        [[nodiscard]] constexpr iterator begin() noexcept {
            return {m_data, m_start, m_step};
        }

        [[nodiscard]] constexpr const_iterator begin() const noexcept {
            return {m_data, m_start, m_step};
        }

        [[nodiscard]] constexpr const_iterator cbegin() noexcept {
            return {m_data, m_start, m_step};
        }

        [[nodiscard]] constexpr iterator end() noexcept {
            return {m_data, m_stop, m_step};
        }

        [[nodiscard]] constexpr const_iterator end() const noexcept {
            return {m_data, m_stop, m_step};
        }

        [[nodiscard]] constexpr const_iterator cend() const noexcept {
            return {m_data, m_stop, m_step};
        }

        template <typename V>
            requires (std::constructible_from<V, std::from_range_t, contiguous_slice&>)
        [[nodiscard]] constexpr operator V() && noexcept(noexcept(V(std::from_range, *this))) {
            return V(std::from_range, *this);
        }

        template <typename V>
            requires (
                !std::constructible_from<V, std::from_range_t, contiguous_slice&> &&
                std::constructible_from<V, iterator, iterator>
            )
        [[nodiscard]] constexpr operator V() && noexcept(noexcept(V(begin(), end()))) {
            return V(begin(), end());
        }

        constexpr contiguous_slice& operator=(const contiguous_slice&) = default;
        constexpr contiguous_slice& operator=(contiguous_slice&&) = default;

        template <typename Dummy = value_type>
            requires (
                !meta::is_const<Dummy> &&
                std::destructible<Dummy> &&
                std::copy_constructible<Dummy>
            )
        [[maybe_unused]] constexpr contiguous_slice& operator=(
            std::initializer_list<value_type> items
        ) && {
            return std::move(*this) = initializer{items};
        }

        template <meta::yields<value_type> Range>
            requires (
                !meta::is_const<value_type> &&
                std::destructible<value_type> &&
                std::constructible_from<value_type, meta::yield_type<Range>>
            )
        [[maybe_unused]] constexpr contiguous_slice& operator=(Range&& range) && {
            using type = std::remove_cvref_t<value_type>;
            constexpr bool has_size = meta::has_size<std::add_lvalue_reference_t<Range>>;
            auto it = std::ranges::begin(range);
            auto end = std::ranges::end(range);

            // if the range has an explicit size, then we can check it ahead of time
            // to ensure that it exactly matches that of the slice
            if constexpr (has_size) {
                if (std::ranges::size(range) != m_size) {
                    throw ValueError(
                        "cannot assign a range of size " +
                        std::to_string(std::ranges::size(range)) +
                        " to a slice of size " + std::to_string(m_size)
                    );
                }
            }

            // If we checked the size above, we can avoid checking it again on each
            // iteration
            if (m_step > 0) {
                for (ssize_t i = m_start; i < m_stop; i += m_step) {
                    if constexpr (!has_size) {
                        if (it == end) {
                            throw ValueError(
                                "not enough values to fill slice of size " +
                                std::to_string(m_size)
                            );
                        }
                    }
                    m_data[i].~type();
                    new (m_data + i) type(*it);
                    ++it;
                }
            } else {
                for (ssize_t i = m_start; i > m_stop; i += m_step) {
                    if constexpr (!has_size) {
                        if (it == end) {
                            throw ValueError(
                                "not enough values to fill slice of size " +
                                std::to_string(m_size)
                            );
                        }
                    }
                    m_data[i].~type();
                    new (m_data + i) type(*it);
                    ++it;
                }
            }

            if constexpr (!has_size) {
                if (it != end) {
                    throw ValueError(
                        "range length exceeds slice of size " +
                        std::to_string(m_size)
                    );
                }
            }
            return *this;
        }

    private:
        pointer m_data = nullptr;
        ssize_t m_start = 0;
        ssize_t m_stop = 0;
        ssize_t m_step = 1;
        ssize_t m_size = 0;
    };

}


/* A simple struct holding paths to the bertrand environment's directories, if such an
environment is currently active. */
inline const impl::virtualenv VIRTUAL_ENV = impl::get_virtual_environment();


/* A python-style `assert` statement in C++, which is optimized away if built without
`-DBERTRAND_DEBUG` (release mode).  The only difference between this and the built-in
C++ `assert()` macro is that this is implemented as a normal function and throws a
`bertrand::AssertionError` which can be passed up to Python with a coherent traceback.
It is thus possible to implement pytest-style unit tests using this function just as
you would in Python. */
inline void assert_(bool cnd, const char* msg = "") {
    if constexpr (DEBUG) {
        if (!cnd) {
            throw AssertionError(msg);
        }
    }
}


/* ADL-friendly swap method.  Equivalent to calling `l.swap(r)` as a member method. */
template <typename T> requires (requires(T& l, T& r) { {l.swap(r)} -> std::same_as<void>; })
constexpr void swap(T& l, T& r) noexcept(noexcept(l.swap(r))) {
    l.swap(r);
}


/// TODO: del()


/* Equivalent to calling `std::hash<T>{}(...)`, but without explicitly specializating
`std::hash`. */
template <meta::hashable T>
[[nodiscard]] constexpr size_t hash(T&& obj) {
    return std::hash<std::decay_t<T>>{}(std::forward<T>(obj));
}


/* Produce a sorted version of a container with the specified less-than comparison
function.  If no explicit comparison function is given, it will default to a
transparent `<` operator for each element.  This operator must be explicitly enabled
for a given container type by specializing the `meta::detail::sorted<Less, T>` struct
and providing an appropriate `::type` alias that injects the comparison function into
the container logic. */
template <meta::unqualified Less = std::less<>, typename T>
    requires (
        meta::is<
            typename meta::detail::sorted<Less, std::remove_cvref_t<T>>::type,
            T
        > || (
            std::constructible_from<
                typename meta::detail::sorted<Less, std::remove_cvref_t<T>>::type,
                T
            > &&
            std::is_default_constructible_v<Less> &&
            std::is_invocable_r_v<
                bool,
                std::add_lvalue_reference_t<Less>,
                std::add_lvalue_reference_t<std::add_const_t<
                    typename meta::detail::sorted<
                        Less,
                        std::remove_cvref_t<T>
                    >::type::value_type
                >>,
                std::add_lvalue_reference_t<std::add_const_t<
                    typename meta::detail::sorted<
                        Less,
                        std::remove_cvref_t<T>
                    >::type::value_type
                >>
            >
        )
    )
decltype(auto) sorted(T&& container) noexcept(
    meta::is<
        typename meta::detail::sorted<Less, std::remove_cvref_t<T>>::type,
        T
    > || noexcept(
        typename meta::detail::sorted<Less, std::remove_cvref_t<T>>::type(
            std::forward<T>(container)
        )
    )
) {
    using type = meta::detail::sorted<Less, std::remove_cvref_t<T>>::type;
    if constexpr (meta::is<type, T>) {
        return std::forward<T>(container);
    } else {
        return type(std::forward<T>(container));
    }
}


/* Specialization of `sorted()` that accepts the container type as an explicit template
parameter, and constructs it using the supplied arguments, rather than requiring a
copy or move. */
template <meta::unqualified T, meta::unqualified Less = std::less<>, typename... Args>
    requires (
        std::constructible_from<
            typename meta::detail::sorted<Less, T>::type,
            Args...
        > &&
        std::is_invocable_r_v<
            bool,
            std::add_lvalue_reference_t<Less>,
            std::add_lvalue_reference_t<std::add_const_t<
                typename meta::detail::sorted<Less, T>::type::value_type
            >>,
            std::add_lvalue_reference_t<std::add_const_t<
                typename meta::detail::sorted<Less, T>::type::value_type
            >>
        >
    )
meta::detail::sorted<Less, T>::type sorted(Args&&... args) noexcept(
    noexcept(typename meta::detail::sorted<Less, T>::type(std::forward<Args>(args)...))
) {
    return typename meta::detail::sorted<Less, T>::type(std::forward<Args>(args)...);
}


}  // namespace bertrand


#endif  // BERTRAND_COMMON_H
