#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <exception>
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
}


namespace meta {

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

    }

    template <typename... Ts>
    concept types_are_unique = detail::types_are_unique<Ts...>;

    namespace detail {

        template <typename Search, size_t I, typename... Ts>
        constexpr size_t index_of = 0;
        template <typename Search, size_t I, typename T, typename... Ts>
        constexpr size_t index_of<Search, I, T, Ts...> =
            std::same_as<Search, T> ? 0 : index_of<Search, I + 1, Ts...> + 1;

    }

    /* Get the index of a particular type within a parameter pack.  Returns the pack's
    size if the type is not present. */
    template <typename Search, typename... Ts>
    static constexpr size_t index_of = detail::index_of<Search, 0, Ts...>;

    namespace detail {

        template <size_t I, typename... Ts>
        struct unpack_type;
        template <size_t I, typename T, typename... Ts>
        struct unpack_type<I, T, Ts...> { using type = unpack_type<I - 1, Ts...>::type; };
        template <typename T, typename... Ts>
        struct unpack_type<0, T, Ts...> { using type = T; };

    }

    /* Get the type at a particular index of a parameter pack.  This is superceded by
    the C++26 pack indexing language feature. */
    template <size_t I, typename... Ts> requires (I < sizeof...(Ts))
    using unpack_type = detail::unpack_type<I, Ts...>::type;

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

    }

    template <typename T>
    concept optional = detail::optional<unqualify<T>>::value;

    template <optional T>
    using optional_type = detail::optional<unqualify<T>>::type;

    namespace detail {

        template <typename T>
        struct variant { static constexpr bool value = false; };
        template <typename... Ts>
        struct variant<std::variant<Ts...>> {
            static constexpr bool value = true;
            using types = std::tuple<Ts...>;
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
        struct shared_ptr<std::shared_ptr<T>> {
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
        struct unique_ptr<std::unique_ptr<T>> {
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
        std::same_as<typename unqualify<A>::value_type, T> &&

        // 2) A must be copy and move constructible/assignable
        std::is_copy_constructible_v<unqualify<A>> &&
        std::is_copy_assignable_v<unqualify<A>> &&
        std::is_move_constructible_v<unqualify<A>> &&
        std::is_move_assignable_v<unqualify<A>> &&

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

    ////////////////////////////
    ////    CONSTRUCTION    ////
    ////////////////////////////

    template <typename T, typename... Args>
    concept constructible_from = std::constructible_from<T, Args...>;

    template <typename T>
    concept default_constructible = std::default_initializable<T>;

    template <typename T, typename... Args>
    concept trivially_constructible = std::is_trivially_constructible_v<T, Args...>;

    namespace nothrow {

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
        concept swappable =
            meta::swappable<T> &&
            std::is_nothrow_swappable_v<T>;

        template <typename T, typename U>
        concept swappable_with =
            meta::swappable_with<T, U> &&
            std::is_nothrow_swappable_with_v<T, U>;

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
    concept invoke_returns = invocable<F, A...> && std::is_invocable_r_v<R, F, A...>;

    namespace nothrow {

        template <typename F, typename... A>
        concept invocable =
            meta::invocable<F, A...> &&
            std::is_nothrow_invocable_v<F, A...>;

        template <typename F, typename... A> requires (invocable<F, A...>)
        using invoke_type = meta::invoke_type<F, A...>;

        template <typename R, typename F, typename... A>
        concept invoke_returns =
            meta::invoke_returns<R, F, A...> &&
            std::is_nothrow_invocable_r_v<R, F, A...>;

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
            noexcept(static_cast<bool>(std::declval<T>()));

    }

    template <typename T>
    concept can_dereference = requires(T t) { *t; };

    template <can_dereference T>
    using dereference_type = decltype(*std::declval<T>());

    template <typename T, typename Ret>
    concept dereferences_to =
        can_dereference<T> && convertible_to<dereference_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept can_dereference =
            meta::can_dereference<T> &&
            noexcept(*std::declval<T>());

        template <can_dereference T>
        using dereference_type = meta::dereference_type<T>;

        template <typename T, typename Ret>
        concept dereferences_to =
            can_dereference<T> && convertible_to<dereference_type<T>, Ret>;

    }

    template <typename T>
    concept has_invert = requires(T t) { ~t; };

    template <has_invert T>
    using invert_type = decltype(~std::declval<T>());

    template <typename Ret, typename T>
    concept invert_returns = has_invert<T> && convertible_to<invert_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_invert =
            meta::has_invert<T> && noexcept(~std::declval<T>());

        template <has_invert T>
        using invert_type = meta::invert_type<T>;

        template <typename Ret, typename T>
        concept invert_returns = has_invert<T> && convertible_to<invert_type<T>, Ret>;

    }

    template <typename T>
    concept has_pos = requires(T t) { +t; };

    template <has_pos T>
    using pos_type = decltype(+std::declval<T>());

    template <typename Ret, typename T>
    concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_pos =
            meta::has_pos<T> && noexcept(+std::declval<T>());

        template <has_pos T>
        using pos_type = meta::pos_type<T>;

        template <typename Ret, typename T>
        concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Ret>;

    }

    template <typename T>
    concept has_neg = requires(T t) { -t; };

    template <has_neg T>
    using neg_type = decltype(-std::declval<T>());

    template <typename Ret, typename T>
    concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_neg =
            meta::has_neg<T> && noexcept(-std::declval<T>());

        template <has_neg T>
        using neg_type = meta::neg_type<T>;

        template <typename Ret, typename T>
        concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Ret>;

    }

    template <typename T>
    concept has_preincrement = requires(T t) { ++t; };

    template <has_preincrement T>
    using preincrement_type = decltype(++std::declval<T>());

    template <typename Ret, typename T>
    concept preincrement_returns =
        has_preincrement<T> && convertible_to<preincrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_preincrement =
            meta::has_preincrement<T> && noexcept(++std::declval<T>());

        template <has_preincrement T>
        using preincrement_type = meta::preincrement_type<T>;

        template <typename Ret, typename T>
        concept preincrement_returns =
            has_preincrement<T> && convertible_to<preincrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_postincrement = requires(T t) { t++; };

    template <has_postincrement T>
    using postincrement_type = decltype(std::declval<T>()++);

    template <typename Ret, typename T>
    concept postincrement_returns =
        has_postincrement<T> && convertible_to<postincrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_postincrement =
            meta::has_postincrement<T> && noexcept(std::declval<T>()++);

        template <has_postincrement T>
        using postincrement_type = meta::postincrement_type<T>;

        template <typename Ret, typename T>
        concept postincrement_returns =
            has_postincrement<T> && convertible_to<postincrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_predecrement = requires(T t) { --t; };

    template <has_predecrement T>
    using predecrement_type = decltype(--std::declval<T>());

    template <typename Ret, typename T>
    concept predecrement_returns =
        has_predecrement<T> && convertible_to<predecrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_predecrement =
            meta::has_predecrement<T> && noexcept(--std::declval<T>());

        template <has_predecrement T>
        using predecrement_type = meta::predecrement_type<T>;

        template <typename Ret, typename T>
        concept predecrement_returns =
            has_predecrement<T> && convertible_to<predecrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_postdecrement = requires(T t) { t--; };

    template <has_postdecrement T>
    using postdecrement_type = decltype(std::declval<T>()--);

    template <typename Ret, typename T>
    concept postdecrement_returns =
        has_postdecrement<T> && convertible_to<postdecrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_postdecrement =
            meta::has_postdecrement<T> && noexcept(std::declval<T>()--);

        template <has_postdecrement T>
        using postdecrement_type = meta::postdecrement_type<T>;

        template <typename Ret, typename T>
        concept postdecrement_returns =
            has_postdecrement<T> && convertible_to<postdecrement_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_lt = requires(L l, R r) { l < r; };

    template <typename L, typename R> requires (has_lt<L, R>)
    using lt_type = decltype(std::declval<L>() < std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_lt =
            meta::has_lt<L, R> && noexcept(std::declval<L>() < std::declval<R>());

        template <typename L, typename R> requires (has_lt<L, R>)
        using lt_type = meta::lt_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_le = requires(L l, R r) { l <= r; };

    template <typename L, typename R> requires (has_le<L, R>)
    using le_type = decltype(std::declval<L>() <= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_le =
            meta::has_le<L, R> && noexcept(std::declval<L>() <= std::declval<R>());

        template <typename L, typename R> requires (has_le<L, R>)
        using le_type = meta::le_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_eq = requires(L l, R r) { l == r; };

    template <typename L, typename R> requires (has_eq<L, R>)
    using eq_type = decltype(std::declval<L>() == std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_eq =
            meta::has_eq<L, R> && noexcept(std::declval<L>() == std::declval<R>());

        template <typename L, typename R> requires (has_eq<L, R>)
        using eq_type = meta::eq_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ne = requires(L l, R r) { l != r; };

    template <typename L, typename R> requires (has_ne<L, R>)
    using ne_type = decltype(std::declval<L>() != std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ne =
            meta::has_ne<L, R> && noexcept(std::declval<L>() != std::declval<R>());

        template <typename L, typename R> requires (has_ne<L, R>)
        using ne_type = meta::ne_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ge = requires(L l, R r) { l >= r; };

    template <typename L, typename R> requires (has_ge<L, R>)
    using ge_type = decltype(std::declval<L>() >= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ge =
            meta::has_ge<L, R> && noexcept(std::declval<L>() >= std::declval<R>());

        template <typename L, typename R> requires (has_ge<L, R>)
        using ge_type = meta::ge_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_gt = requires(L l, R r) { l > r; };

    template <typename L, typename R> requires (has_gt<L, R>)
    using gt_type = decltype(std::declval<L>() > std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_gt =
            meta::has_gt<L, R> && noexcept(std::declval<L>() > std::declval<R>());

        template <typename L, typename R> requires (has_gt<L, R>)
        using gt_type = meta::gt_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_spaceship = requires(L l, R r) { l <=> r; };

    template <typename L, typename R> requires (has_spaceship<L, R>)
    using spaceship_type =
        decltype(std::declval<L>() <=> std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept spaceship_returns =
        has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_spaceship =
            meta::has_spaceship<L, R> && noexcept(std::declval<L>() <=> std::declval<R>());

        template <typename L, typename R> requires (has_spaceship<L, R>)
        using spaceship_type = meta::spaceship_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept spaceship_returns =
            has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_add = requires(L l, R r) { l + r; };

    template <typename L, typename R> requires (has_add<L, R>)
    using add_type = decltype(std::declval<L>() + std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_add =
            meta::has_add<L, R> && noexcept(std::declval<L>() + std::declval<R>());

        template <typename L, typename R> requires (has_add<L, R>)
        using add_type = meta::add_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_iadd = requires(L& l, R r) { l += r; };

    template <typename L, typename R> requires (has_iadd<L, R>)
    using iadd_type = decltype(std::declval<L&>() += std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept iadd_returns = has_iadd<L, R> && convertible_to<iadd_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_iadd =
            meta::has_iadd<L, R> && noexcept(std::declval<L&>() += std::declval<R>());

        template <typename L, typename R> requires (has_iadd<L, R>)
        using iadd_type = meta::iadd_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept iadd_returns =
            has_iadd<L, R> && convertible_to<iadd_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_sub = requires(L l, R r) {{ l - r };};

    template <typename L, typename R> requires (has_sub<L, R>)
    using sub_type = decltype(std::declval<L>() - std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_sub =
            meta::has_sub<L, R> && noexcept(std::declval<L>() - std::declval<R>());

        template <typename L, typename R> requires (has_sub<L, R>)
        using sub_type = meta::sub_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_isub = requires(L& l, R r) { l -= r; };

    template <typename L, typename R> requires (has_isub<L, R>)
    using isub_type = decltype(std::declval<L&>() -= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept isub_returns = has_isub<L, R> && convertible_to<isub_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_isub =
            meta::has_isub<L, R> && noexcept(std::declval<L&>() -= std::declval<R>());

        template <typename L, typename R> requires (has_isub<L, R>)
        using isub_type = meta::isub_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept isub_returns =
            has_isub<L, R> && convertible_to<isub_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_mul = requires(L l, R r) { l * r; };

    template <typename L, typename R> requires (has_mul<L, R>)
    using mul_type = decltype(std::declval<L>() * std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_mul =
            meta::has_mul<L, R> && noexcept(std::declval<L>() * std::declval<R>());

        template <typename L, typename R> requires (has_mul<L, R>)
        using mul_type = meta::mul_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_imul = requires(L& l, R r) { l *= r; };

    template <typename L, typename R> requires (has_imul<L, R>)
    using imul_type = decltype(std::declval<L&>() *= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept imul_returns = has_imul<L, R> && convertible_to<imul_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_imul =
            meta::has_imul<L, R> && noexcept(std::declval<L&>() *= std::declval<R>());

        template <typename L, typename R> requires (has_imul<L, R>)
        using imul_type = meta::imul_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept imul_returns =
            has_imul<L, R> && convertible_to<imul_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_div = requires(L l, R r) { l / r; };

    template <typename L, typename R> requires (has_div<L, R>)
    using div_type = decltype(std::declval<L>() / std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept div_returns =
        has_div<L, R> && convertible_to<div_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_div =
            meta::has_div<L, R> && noexcept(std::declval<L>() / std::declval<R>());

        template <typename L, typename R> requires (has_div<L, R>)
        using div_type = meta::div_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept div_returns =
            has_div<L, R> && convertible_to<div_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_idiv = requires(L& l, R r) { l /= r; };

    template <typename L, typename R> requires (has_idiv<L, R>)
    using idiv_type = decltype(std::declval<L&>() /= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept idiv_returns =
        has_idiv<L, R> && convertible_to<idiv_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_idiv =
            meta::has_idiv<L, R> && noexcept(std::declval<L&>() /= std::declval<R>());

        template <typename L, typename R> requires (has_idiv<L, R>)
        using idiv_type = meta::idiv_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept idiv_returns =
            has_idiv<L, R> && convertible_to<idiv_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_mod = requires(L l, R r) { l % r; };

    template <typename L, typename R> requires (has_mod<L, R>)
    using mod_type = decltype(std::declval<L>() % std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_mod =
            meta::has_mod<L, R> && noexcept(std::declval<L>() % std::declval<R>());

        template <typename L, typename R> requires (has_mod<L, R>)
        using mod_type = meta::mod_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_imod = requires(L& l, R r) { l %= r; };

    template <typename L, typename R> requires (has_imod<L, R>)
    using imod_type = decltype(std::declval<L&>() %= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept imod_returns =
        has_imod<L, R> && convertible_to<imod_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_imod =
            meta::has_imod<L, R> && noexcept(std::declval<L&>() %= std::declval<R>());

        template <typename L, typename R> requires (has_imod<L, R>)
        using imod_type = meta::imod_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept imod_returns =
            has_imod<L, R> && convertible_to<imod_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_lshift = requires(L l, R r) { l << r; };

    template <typename L, typename R> requires (has_lshift<L, R>)
    using lshift_type = decltype(std::declval<L>() << std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept lshift_returns =
        has_lshift<L, R> && convertible_to<lshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_lshift =
            meta::has_lshift<L, R> && noexcept(std::declval<L>() << std::declval<R>());

        template <typename L, typename R> requires (has_lshift<L, R>)
        using lshift_type = meta::lshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept lshift_returns =
            has_lshift<L, R> && convertible_to<lshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ilshift = requires(L& l, R r) { l <<= r; };

    template <typename L, typename R> requires (has_ilshift<L, R>)
    using ilshift_type = decltype(std::declval<L&>() <<= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept ilshift_returns =
        has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ilshift =
            meta::has_ilshift<L, R> && noexcept(std::declval<L&>() <<= std::declval<R>());

        template <typename L, typename R> requires (has_ilshift<L, R>)
        using ilshift_type = meta::ilshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ilshift_returns =
            has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_rshift = requires(L l, R r) { l >> r; };

    template <typename L, typename R> requires (has_rshift<L, R>)
    using rshift_type = decltype(std::declval<L>() >> std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept rshift_returns =
        has_rshift<L, R> && convertible_to<rshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_rshift =
            meta::has_rshift<L, R> && noexcept(std::declval<L>() >> std::declval<R>());

        template <typename L, typename R> requires (has_rshift<L, R>)
        using rshift_type = meta::rshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept rshift_returns =
            has_rshift<L, R> && convertible_to<rshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_irshift = requires(L& l, R r) { l >>= r; };

    template <typename L, typename R> requires (has_irshift<L, R>)
    using irshift_type = decltype(std::declval<L&>() >>= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept irshift_returns =
        has_irshift<L, R> && convertible_to<irshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_irshift =
            meta::has_irshift<L, R> && noexcept(std::declval<L&>() >>= std::declval<R>());

        template <typename L, typename R> requires (has_irshift<L, R>)
        using irshift_type = meta::irshift_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept irshift_returns =
            has_irshift<L, R> && convertible_to<irshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_and = requires(L l, R r) { l & r; };

    template <typename L, typename R> requires (has_and<L, R>)
    using and_type = decltype(std::declval<L>() & std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept and_returns = has_and<L, R> && convertible_to<and_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_and =
            meta::has_and<L, R> && noexcept(std::declval<L>() & std::declval<R>());

        template <typename L, typename R> requires (has_and<L, R>)
        using and_type = meta::and_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept and_returns =
            has_and<L, R> && convertible_to<and_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_iand = requires(L& l, R r) { l &= r; };

    template <typename L, typename R> requires (has_iand<L, R>)
    using iand_type = decltype(std::declval<L&>() &= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept iand_returns = has_iand<L, R> && convertible_to<iand_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_iand =
            meta::has_iand<L, R> && noexcept(std::declval<L&>() &= std::declval<R>());

        template <typename L, typename R> requires (has_iand<L, R>)
        using iand_type = meta::iand_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept iand_returns =
            has_iand<L, R> && convertible_to<iand_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_or = requires(L l, R r) { l | r; };

    template <typename L, typename R> requires (has_or<L, R>)
    using or_type = decltype(std::declval<L>() | std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_or =
            meta::has_or<L, R> && noexcept(std::declval<L>() | std::declval<R>());

        template <typename L, typename R> requires (has_or<L, R>)
        using or_type = meta::or_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ior = requires(L& l, R r) { l |= r; };

    template <typename L, typename R> requires (has_ior<L, R>)
    using ior_type = decltype(std::declval<L&>() |= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept ior_returns = has_ior<L, R> && convertible_to<ior_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ior =
            meta::has_ior<L, R> && noexcept(std::declval<L&>() |= std::declval<R>());

        template <typename L, typename R> requires (has_ior<L, R>)
        using ior_type = meta::ior_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ior_returns =
            has_ior<L, R> && convertible_to<ior_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_xor = requires(L l, R r) { l ^ r; };

    template <typename L, typename R> requires (has_xor<L, R>)
    using xor_type = decltype(std::declval<L>() ^ std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept xor_returns = has_xor<L, R> && convertible_to<xor_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_xor =
            meta::has_xor<L, R> && noexcept(std::declval<L>() ^ std::declval<R>());

        template <typename L, typename R> requires (has_xor<L, R>)
        using xor_type = meta::xor_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept xor_returns =
            has_xor<L, R> && convertible_to<xor_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ixor = requires(L& l, R r) { l ^= r; };

    template <typename L, typename R> requires (has_ixor<L, R>)
    using ixor_type = decltype(std::declval<L&>() ^= std::declval<R>());

    template <typename Ret, typename L, typename R>
    concept ixor_returns = has_ixor<L, R> && convertible_to<ixor_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ixor =
            meta::has_ixor<L, R> && noexcept(std::declval<L&>() ^= std::declval<R>());

        template <typename L, typename R> requires (has_ixor<L, R>)
        using ixor_type = meta::ixor_type<L, R>;

        template <typename Ret, typename L, typename R>
        concept ixor_returns =
            has_ixor<L, R> && convertible_to<ixor_type<L, R>, Ret>;

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

    /* Transparently unwrap wrapped types, triggering a getter call if the
    `meta::wrapper` concept is modeled, or returning the result as-is otherwise. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) unwrap(T&& t) noexcept {
        return std::forward<T>(t);
    }

    /* Transparently unwrap wrapped types, triggering a getter call if the
    `meta::wrapper` concept is modeled, or returning the result as-is otherwise. */
    template <wrapper T>
    [[nodiscard]] constexpr decltype(auto) unwrap(T&& t) noexcept(
        noexcept(impl::getter{}(std::forward<T>(t)))
    ) {
        return impl::getter{}(std::forward<T>(t));
    }

    /* Describes the result of the `meta::unwrap()` operator. */
    template <typename T>
    using unwrap_type = decltype(unwrap(std::declval<T>()));

}


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
