#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <exception>
#include <filesystem>
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

    template <typename L, typename R, typename Ret>
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

        template <typename L, typename R, typename Ret>
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

    template <typename T, typename Ret>
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

        template <typename T, typename Ret>
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

    template <typename T, typename Ret>
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

        template <typename T, typename Ret>
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

    }

    template <typename T, size_t N>
    concept structured =
        std::tuple_size<T>::value == N && detail::structured<T, N>::value;

    template <typename T>
    concept pair = structured<T, 2>;

    namespace detail {

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

    template <typename T, typename... Key>
    concept indexable = !integer<T> && requires(T t, Key... key) {
        std::forward<T>(t)[std::forward<Key>(key)...];
    };

    template <typename T, typename... Key> requires (indexable<T, Key...>)
    using index_type = decltype(std::declval<T>()[std::declval<Key>()...]);

    template <typename T, typename Ret, typename... Key>
    concept index_returns =
        indexable<T, Key...> && convertible_to<index_type<T, Key...>, Ret>;

    namespace nothrow {

        template <typename T, typename... Key>
        concept indexable =
            meta::indexable<T, Key...> &&
            noexcept(std::declval<T>()[std::declval<Key>()...]);

        template <typename T, typename... Key> requires (indexable<T, Key...>)
        using index_type = meta::index_type<T, Key...>;

        template <typename T, typename Ret, typename... Key>
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

    template <typename T, typename Value, typename Ret, typename... Key>
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

        template <typename T, typename Value, typename Ret, typename... Key>
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

    template <typename T, typename Ret>
    concept invert_returns = has_invert<T> && convertible_to<invert_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_invert =
            meta::has_invert<T> && noexcept(~std::declval<T>());

        template <has_invert T>
        using invert_type = meta::invert_type<T>;

        template <typename T, typename Ret>
        concept invert_returns = has_invert<T> && convertible_to<invert_type<T>, Ret>;

    }

    template <typename T>
    concept has_pos = requires(T t) { +t; };

    template <has_pos T>
    using pos_type = decltype(+std::declval<T>());

    template <typename T, typename Ret>
    concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_pos =
            meta::has_pos<T> && noexcept(+std::declval<T>());

        template <has_pos T>
        using pos_type = meta::pos_type<T>;

        template <typename T, typename Ret>
        concept pos_returns = has_pos<T> && convertible_to<pos_type<T>, Ret>;

    }

    template <typename T>
    concept has_neg = requires(T t) { -t; };

    template <has_neg T>
    using neg_type = decltype(-std::declval<T>());

    template <typename T, typename Ret>
    concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_neg =
            meta::has_neg<T> && noexcept(-std::declval<T>());

        template <has_neg T>
        using neg_type = meta::neg_type<T>;

        template <typename T, typename Ret>
        concept neg_returns = has_neg<T> && convertible_to<neg_type<T>, Ret>;

    }

    template <typename T>
    concept has_preincrement = requires(T t) { ++t; };

    template <has_preincrement T>
    using preincrement_type = decltype(++std::declval<T>());

    template <typename T, typename Ret>
    concept preincrement_returns =
        has_preincrement<T> && convertible_to<preincrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_preincrement =
            meta::has_preincrement<T> && noexcept(++std::declval<T>());

        template <has_preincrement T>
        using preincrement_type = meta::preincrement_type<T>;

        template <typename T, typename Ret>
        concept preincrement_returns =
            has_preincrement<T> && convertible_to<preincrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_postincrement = requires(T t) { t++; };

    template <has_postincrement T>
    using postincrement_type = decltype(std::declval<T>()++);

    template <typename T, typename Ret>
    concept postincrement_returns =
        has_postincrement<T> && convertible_to<postincrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_postincrement =
            meta::has_postincrement<T> && noexcept(std::declval<T>()++);

        template <has_postincrement T>
        using postincrement_type = meta::postincrement_type<T>;

        template <typename T, typename Ret>
        concept postincrement_returns =
            has_postincrement<T> && convertible_to<postincrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_predecrement = requires(T t) { --t; };

    template <has_predecrement T>
    using predecrement_type = decltype(--std::declval<T>());

    template <typename T, typename Ret>
    concept predecrement_returns =
        has_predecrement<T> && convertible_to<predecrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_predecrement =
            meta::has_predecrement<T> && noexcept(--std::declval<T>());

        template <has_predecrement T>
        using predecrement_type = meta::predecrement_type<T>;

        template <typename T, typename Ret>
        concept predecrement_returns =
            has_predecrement<T> && convertible_to<predecrement_type<T>, Ret>;

    }

    template <typename T>
    concept has_postdecrement = requires(T t) { t--; };

    template <has_postdecrement T>
    using postdecrement_type = decltype(std::declval<T>()--);

    template <typename T, typename Ret>
    concept postdecrement_returns =
        has_postdecrement<T> && convertible_to<postdecrement_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_postdecrement =
            meta::has_postdecrement<T> && noexcept(std::declval<T>()--);

        template <has_postdecrement T>
        using postdecrement_type = meta::postdecrement_type<T>;

        template <typename T, typename Ret>
        concept postdecrement_returns =
            has_postdecrement<T> && convertible_to<postdecrement_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_lt = requires(L l, R r) { l < r; };

    template <typename L, typename R> requires (has_lt<L, R>)
    using lt_type = decltype(std::declval<L>() < std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_lt =
            meta::has_lt<L, R> && noexcept(std::declval<L>() < std::declval<R>());

        template <typename L, typename R> requires (has_lt<L, R>)
        using lt_type = meta::lt_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept lt_returns = has_lt<L, R> && convertible_to<lt_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_le = requires(L l, R r) { l <= r; };

    template <typename L, typename R> requires (has_le<L, R>)
    using le_type = decltype(std::declval<L>() <= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_le =
            meta::has_le<L, R> && noexcept(std::declval<L>() <= std::declval<R>());

        template <typename L, typename R> requires (has_le<L, R>)
        using le_type = meta::le_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept le_returns = has_le<L, R> && convertible_to<le_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_eq = requires(L l, R r) { l == r; };

    template <typename L, typename R> requires (has_eq<L, R>)
    using eq_type = decltype(std::declval<L>() == std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_eq =
            meta::has_eq<L, R> && noexcept(std::declval<L>() == std::declval<R>());

        template <typename L, typename R> requires (has_eq<L, R>)
        using eq_type = meta::eq_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept eq_returns = has_eq<L, R> && convertible_to<eq_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ne = requires(L l, R r) { l != r; };

    template <typename L, typename R> requires (has_ne<L, R>)
    using ne_type = decltype(std::declval<L>() != std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ne =
            meta::has_ne<L, R> && noexcept(std::declval<L>() != std::declval<R>());

        template <typename L, typename R> requires (has_ne<L, R>)
        using ne_type = meta::ne_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept ne_returns = has_ne<L, R> && convertible_to<ne_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ge = requires(L l, R r) { l >= r; };

    template <typename L, typename R> requires (has_ge<L, R>)
    using ge_type = decltype(std::declval<L>() >= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ge =
            meta::has_ge<L, R> && noexcept(std::declval<L>() >= std::declval<R>());

        template <typename L, typename R> requires (has_ge<L, R>)
        using ge_type = meta::ge_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept ge_returns = has_ge<L, R> && convertible_to<ge_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_gt = requires(L l, R r) { l > r; };

    template <typename L, typename R> requires (has_gt<L, R>)
    using gt_type = decltype(std::declval<L>() > std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_gt =
            meta::has_gt<L, R> && noexcept(std::declval<L>() > std::declval<R>());

        template <typename L, typename R> requires (has_gt<L, R>)
        using gt_type = meta::gt_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept gt_returns = has_gt<L, R> && convertible_to<gt_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_spaceship = requires(L l, R r) { l <=> r; };

    template <typename L, typename R> requires (has_spaceship<L, R>)
    using spaceship_type =
        decltype(std::declval<L>() <=> std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept spaceship_returns =
        has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_spaceship =
            meta::has_spaceship<L, R> && noexcept(std::declval<L>() <=> std::declval<R>());

        template <typename L, typename R> requires (has_spaceship<L, R>)
        using spaceship_type = meta::spaceship_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept spaceship_returns =
            has_spaceship<L, R> && convertible_to<spaceship_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_add = requires(L l, R r) { l + r; };

    template <typename L, typename R> requires (has_add<L, R>)
    using add_type = decltype(std::declval<L>() + std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_add =
            meta::has_add<L, R> && noexcept(std::declval<L>() + std::declval<R>());

        template <typename L, typename R> requires (has_add<L, R>)
        using add_type = meta::add_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept add_returns = has_add<L, R> && convertible_to<add_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_iadd = requires(L& l, R r) { l += r; };

    template <typename L, typename R> requires (has_iadd<L, R>)
    using iadd_type = decltype(std::declval<L&>() += std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept iadd_returns = has_iadd<L, R> && convertible_to<iadd_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_iadd =
            meta::has_iadd<L, R> && noexcept(std::declval<L&>() += std::declval<R>());

        template <typename L, typename R> requires (has_iadd<L, R>)
        using iadd_type = meta::iadd_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept iadd_returns =
            has_iadd<L, R> && convertible_to<iadd_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_sub = requires(L l, R r) {{ l - r };};

    template <typename L, typename R> requires (has_sub<L, R>)
    using sub_type = decltype(std::declval<L>() - std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_sub =
            meta::has_sub<L, R> && noexcept(std::declval<L>() - std::declval<R>());

        template <typename L, typename R> requires (has_sub<L, R>)
        using sub_type = meta::sub_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept sub_returns = has_sub<L, R> && convertible_to<sub_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_isub = requires(L& l, R r) { l -= r; };

    template <typename L, typename R> requires (has_isub<L, R>)
    using isub_type = decltype(std::declval<L&>() -= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept isub_returns = has_isub<L, R> && convertible_to<isub_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_isub =
            meta::has_isub<L, R> && noexcept(std::declval<L&>() -= std::declval<R>());

        template <typename L, typename R> requires (has_isub<L, R>)
        using isub_type = meta::isub_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept isub_returns =
            has_isub<L, R> && convertible_to<isub_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_mul = requires(L l, R r) { l * r; };

    template <typename L, typename R> requires (has_mul<L, R>)
    using mul_type = decltype(std::declval<L>() * std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_mul =
            meta::has_mul<L, R> && noexcept(std::declval<L>() * std::declval<R>());

        template <typename L, typename R> requires (has_mul<L, R>)
        using mul_type = meta::mul_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept mul_returns = has_mul<L, R> && convertible_to<mul_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_imul = requires(L& l, R r) { l *= r; };

    template <typename L, typename R> requires (has_imul<L, R>)
    using imul_type = decltype(std::declval<L&>() *= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept imul_returns = has_imul<L, R> && convertible_to<imul_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_imul =
            meta::has_imul<L, R> && noexcept(std::declval<L&>() *= std::declval<R>());

        template <typename L, typename R> requires (has_imul<L, R>)
        using imul_type = meta::imul_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept imul_returns =
            has_imul<L, R> && convertible_to<imul_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_truediv = requires(L l, R r) { l / r; };

    template <typename L, typename R> requires (has_truediv<L, R>)
    using truediv_type = decltype(std::declval<L>() / std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept truediv_returns =
        has_truediv<L, R> && convertible_to<truediv_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_truediv =
            meta::has_truediv<L, R> && noexcept(std::declval<L>() / std::declval<R>());

        template <typename L, typename R> requires (has_truediv<L, R>)
        using truediv_type = meta::truediv_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept truediv_returns =
            has_truediv<L, R> && convertible_to<truediv_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_itruediv = requires(L& l, R r) { l /= r; };

    template <typename L, typename R> requires (has_itruediv<L, R>)
    using itruediv_type = decltype(std::declval<L&>() /= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept itruediv_returns =
        has_itruediv<L, R> && convertible_to<itruediv_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_itruediv =
            meta::has_itruediv<L, R> && noexcept(std::declval<L&>() /= std::declval<R>());

        template <typename L, typename R> requires (has_itruediv<L, R>)
        using itruediv_type = meta::itruediv_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept itruediv_returns =
            has_itruediv<L, R> && convertible_to<itruediv_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_mod = requires(L l, R r) { l % r; };

    template <typename L, typename R> requires (has_mod<L, R>)
    using mod_type = decltype(std::declval<L>() % std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_mod =
            meta::has_mod<L, R> && noexcept(std::declval<L>() % std::declval<R>());

        template <typename L, typename R> requires (has_mod<L, R>)
        using mod_type = meta::mod_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept mod_returns = has_mod<L, R> && convertible_to<mod_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_imod = requires(L& l, R r) { l %= r; };

    template <typename L, typename R> requires (has_imod<L, R>)
    using imod_type = decltype(std::declval<L&>() %= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept imod_returns =
        has_imod<L, R> && convertible_to<imod_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_imod =
            meta::has_imod<L, R> && noexcept(std::declval<L&>() %= std::declval<R>());

        template <typename L, typename R> requires (has_imod<L, R>)
        using imod_type = meta::imod_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept imod_returns =
            has_imod<L, R> && convertible_to<imod_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_lshift = requires(L l, R r) { l << r; };

    template <typename L, typename R> requires (has_lshift<L, R>)
    using lshift_type = decltype(std::declval<L>() << std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept lshift_returns =
        has_lshift<L, R> && convertible_to<lshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_lshift =
            meta::has_lshift<L, R> && noexcept(std::declval<L>() << std::declval<R>());

        template <typename L, typename R> requires (has_lshift<L, R>)
        using lshift_type = meta::lshift_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept lshift_returns =
            has_lshift<L, R> && convertible_to<lshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ilshift = requires(L& l, R r) { l <<= r; };

    template <typename L, typename R> requires (has_ilshift<L, R>)
    using ilshift_type = decltype(std::declval<L&>() <<= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept ilshift_returns =
        has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ilshift =
            meta::has_ilshift<L, R> && noexcept(std::declval<L&>() <<= std::declval<R>());

        template <typename L, typename R> requires (has_ilshift<L, R>)
        using ilshift_type = meta::ilshift_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept ilshift_returns =
            has_ilshift<L, R> && convertible_to<ilshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_rshift = requires(L l, R r) { l >> r; };

    template <typename L, typename R> requires (has_rshift<L, R>)
    using rshift_type = decltype(std::declval<L>() >> std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept rshift_returns =
        has_rshift<L, R> && convertible_to<rshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_rshift =
            meta::has_rshift<L, R> && noexcept(std::declval<L>() >> std::declval<R>());

        template <typename L, typename R> requires (has_rshift<L, R>)
        using rshift_type = meta::rshift_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept rshift_returns =
            has_rshift<L, R> && convertible_to<rshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_irshift = requires(L& l, R r) { l >>= r; };

    template <typename L, typename R> requires (has_irshift<L, R>)
    using irshift_type = decltype(std::declval<L&>() >>= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept irshift_returns =
        has_irshift<L, R> && convertible_to<irshift_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_irshift =
            meta::has_irshift<L, R> && noexcept(std::declval<L&>() >>= std::declval<R>());

        template <typename L, typename R> requires (has_irshift<L, R>)
        using irshift_type = meta::irshift_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept irshift_returns =
            has_irshift<L, R> && convertible_to<irshift_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_and = requires(L l, R r) { l & r; };

    template <typename L, typename R> requires (has_and<L, R>)
    using and_type = decltype(std::declval<L>() & std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept and_returns = has_and<L, R> && convertible_to<and_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_and =
            meta::has_and<L, R> && noexcept(std::declval<L>() & std::declval<R>());

        template <typename L, typename R> requires (has_and<L, R>)
        using and_type = meta::and_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept and_returns =
            has_and<L, R> && convertible_to<and_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_iand = requires(L& l, R r) { l &= r; };

    template <typename L, typename R> requires (has_iand<L, R>)
    using iand_type = decltype(std::declval<L&>() &= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept iand_returns = has_iand<L, R> && convertible_to<iand_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_iand =
            meta::has_iand<L, R> && noexcept(std::declval<L&>() &= std::declval<R>());

        template <typename L, typename R> requires (has_iand<L, R>)
        using iand_type = meta::iand_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept iand_returns =
            has_iand<L, R> && convertible_to<iand_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_or = requires(L l, R r) { l | r; };

    template <typename L, typename R> requires (has_or<L, R>)
    using or_type = decltype(std::declval<L>() | std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_or =
            meta::has_or<L, R> && noexcept(std::declval<L>() | std::declval<R>());

        template <typename L, typename R> requires (has_or<L, R>)
        using or_type = meta::or_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept or_returns = has_or<L, R> && convertible_to<or_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ior = requires(L& l, R r) { l |= r; };

    template <typename L, typename R> requires (has_ior<L, R>)
    using ior_type = decltype(std::declval<L&>() |= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept ior_returns = has_ior<L, R> && convertible_to<ior_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ior =
            meta::has_ior<L, R> && noexcept(std::declval<L&>() |= std::declval<R>());

        template <typename L, typename R> requires (has_ior<L, R>)
        using ior_type = meta::ior_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept ior_returns =
            has_ior<L, R> && convertible_to<ior_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_xor = requires(L l, R r) { l ^ r; };

    template <typename L, typename R> requires (has_xor<L, R>)
    using xor_type = decltype(std::declval<L>() ^ std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept xor_returns = has_xor<L, R> && convertible_to<xor_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_xor =
            meta::has_xor<L, R> && noexcept(std::declval<L>() ^ std::declval<R>());

        template <typename L, typename R> requires (has_xor<L, R>)
        using xor_type = meta::xor_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept xor_returns =
            has_xor<L, R> && convertible_to<xor_type<L, R>, Ret>;

    }

    template <typename L, typename R>
    concept has_ixor = requires(L& l, R r) { l ^= r; };

    template <typename L, typename R> requires (has_ixor<L, R>)
    using ixor_type = decltype(std::declval<L&>() ^= std::declval<R>());

    template <typename L, typename R, typename Ret>
    concept ixor_returns = has_ixor<L, R> && convertible_to<ixor_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_ixor =
            meta::has_ixor<L, R> && noexcept(std::declval<L&>() ^= std::declval<R>());

        template <typename L, typename R> requires (has_ixor<L, R>)
        using ixor_type = meta::ixor_type<L, R>;

        template <typename L, typename R, typename Ret>
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

    template <typename T, typename Ret>
    concept data_returns = has_data<T> && convertible_to<data_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_data =
            meta::has_data<T> &&
            noexcept(std::ranges::data(std::declval<T>()));

        template <has_data T>
        using data_type = meta::data_type<T>;

        template <typename T, typename Ret>
        concept data_returns = has_data<T> && convertible_to<data_type<T>, Ret>;

    }

    template <typename T>
    concept has_size = requires(T t) { std::ranges::size(t); };

    template <has_size T>
    using size_type = decltype(std::ranges::size(std::declval<T>()));

    template <typename T, typename Ret>
    concept size_returns = has_size<T> && convertible_to<size_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_size =
            meta::has_size<T> &&
            noexcept(std::ranges::size(std::declval<T>()));

        template <has_size T>
        using size_type = meta::size_type<T>;

        template <typename T, typename Ret>
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

    template <typename T, typename Ret>
    concept capacity_returns = has_capacity<T> && convertible_to<capacity_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_capacity =
            meta::has_capacity<T> &&
            noexcept(std::declval<T>().capacity());

        template <has_capacity T>
        using capacity_type = meta::capacity_type<T>;

        template <typename T, typename Ret>
        concept capacity_returns = has_capacity<T> && convertible_to<capacity_type<T>, Ret>;

    }

    template <typename T>
    concept has_reserve = requires(T t, size_t n) { t.reserve(n); };

    template <has_reserve T>
    using reserve_type = decltype(std::declval<T>().reserve(std::declval<size_t>()));

    template <typename T, typename Ret>
    concept reserve_returns = has_reserve<T> && convertible_to<reserve_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_reserve =
            meta::has_reserve<T> &&
            noexcept(std::declval<T>().reserve(std::declval<size_t>()));

        template <has_reserve T>
        using reserve_type = meta::reserve_type<T>;

        template <typename T, typename Ret>
        concept reserve_returns = has_reserve<T> && convertible_to<reserve_type<T>, Ret>;

    }

    template <typename T, typename Key>
    concept has_contains = requires(T t, Key key) { t.contains(key); };

    template <typename T, typename Key> requires (has_contains<T, Key>)
    using contains_type = decltype(std::declval<T>().contains(std::declval<Key>()));

    template <typename T, typename Key, typename Ret>
    concept contains_returns =
        has_contains<T, Key> && convertible_to<contains_type<T, Key>, Ret>;

    namespace nothrow {

        template <typename T, typename Key>
        concept has_contains =
            meta::has_contains<T, Key> &&
            noexcept(std::declval<T>().contains(std::declval<Key>()));

        template <typename T, typename Key> requires (has_contains<T, Key>)
        using contains_type = meta::contains_type<T, Key>;

        template <typename T, typename Key, typename Ret>
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

    template <typename T, typename Ret>
    concept hash_returns = hashable<T> && convertible_to<hash_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept hashable =
            meta::hashable<T> &&
            noexcept(std::hash<std::decay_t<T>>{}(std::declval<T>()));

        template <hashable T>
        using hash_type = meta::hash_type<T>;

        template <typename T, typename Ret>
        concept hash_returns = hashable<T> && convertible_to<hash_type<T>, Ret>;

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

    template <typename T, typename Ret>
    concept real_returns = has_real<T> && convertible_to<real_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_real =
            meta::has_real<T> &&
            noexcept(std::declval<T>().real());

        template <has_real T>
        using real_type = meta::real_type<T>;

        template <typename T, typename Ret>
        concept real_returns = has_real<T> && convertible_to<real_type<T>, Ret>;

    }

    template <typename T>
    concept has_imag = requires(T t) { t.imag(); };

    template <has_imag T>
    using imag_type = decltype(std::declval<T>().imag());

    template <typename T, typename Ret>
    concept imag_returns = has_imag<T> && convertible_to<imag_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_imag =
            meta::has_imag<T> &&
            noexcept(std::declval<T>().imag());

        template <has_imag T>
        using imag_type = meta::imag_type<T>;

        template <typename T, typename Ret>
        concept imag_returns = has_imag<T> && convertible_to<imag_type<T>, Ret>;

    }

    template <typename T>
    concept complex = real_returns<T, double> && imag_returns<T, double>;

    template <typename T>
    concept has_abs = requires(T t) { std::abs(t); };

    template <has_abs T>
    using abs_type = decltype(std::abs(std::declval<T>()));

    template <typename T, typename Ret>
    concept abs_returns = has_abs<T> && convertible_to<abs_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_abs =
            meta::has_abs<T> &&
            noexcept(std::abs(std::declval<T>()));

        template <has_abs T>
        using abs_type = meta::abs_type<T>;

        template <typename T, typename Ret>
        concept abs_returns = has_abs<T> && convertible_to<abs_type<T>, Ret>;

    }

    template <typename L, typename R>
    concept has_pow = requires(L l, R r) { std::pow(l, r); };

    template <typename L, typename R> requires (has_pow<L, R>)
    using pow_type = decltype(std::pow(std::declval<L>(), std::declval<R>()));

    template <typename L, typename R, typename Ret>
    concept pow_returns = has_pow<L, R> && convertible_to<pow_type<L, R>, Ret>;

    namespace nothrow {

        template <typename L, typename R>
        concept has_pow =
            meta::has_pow<L, R> &&
            noexcept(std::pow(std::declval<L>(), std::declval<R>()));

        template <typename L, typename R> requires (has_pow<L, R>)
        using pow_type = meta::pow_type<L, R>;

        template <typename L, typename R, typename Ret>
        concept pow_returns = has_pow<L, R> && convertible_to<pow_type<L, R>, Ret>;

    }

    ////////////////////////////////////
    ////    CUSTOMIZATION POINTS    ////
    ////////////////////////////////////

    namespace detail {

        /// NOTE: in all cases, cvref qualifiers will be stripped from the input
        /// types before checking against these customization points.

        /// TODO: make sure that the above is actually the case

        /* Enables the `sorted()` helper function for sortable container types.  The
        `::type` alias should map the templated `Less` function into the template
        definition for `T`.  */
        template <meta::unqualified Less, meta::iterable T>
            requires (
                meta::default_constructible<Less> &&
                meta::invoke_returns<
                    bool,
                    Less,
                    meta::yield_type<T>,
                    meta::yield_type<T>
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

    template <typename Msg> requires (meta::constructible_from<std::string, Msg>)
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
        requires (meta::constructible_from<std::string, Msg>)
    explicit constexpr Exception(Msg&& msg = "") :
        m_message(std::forward<Msg>(msg)),
        m_skip(1)  // skip this constructor
    {
        if !consteval {
            new (&m_raw_trace) cpptrace::raw_trace(cpptrace::generate_raw_trace());
        }
    }

    template <typename Msg = const char*>
        requires (meta::constructible_from<std::string, Msg>)
    explicit Exception(cpptrace::raw_trace&& trace, Msg&& msg = "") :
        m_message(std::forward<Msg>(msg)),
        m_trace_type(RAW_TRACE)
    {
        new (&m_raw_trace) cpptrace::raw_trace(std::move(trace));
    }

    template <typename Msg = const char*>
        requires (meta::constructible_from<std::string, Msg>)
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
        template <typename Msg> requires (meta::constructible_from<std::string, Msg>)   \
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
            requires (meta::constructible_from<std::string, Msg>)                       \
        explicit constexpr CLS(Msg&& msg = "") : BASE(                                  \
            get_trace{},                                                                \
            std::forward<Msg>(msg)                                                      \
        ) {}                                                                            \
                                                                                        \
        template <typename Msg = const char*>                                           \
            requires (meta::constructible_from<std::string, Msg>)                       \
        explicit CLS(cpptrace::raw_trace&& trace, Msg&& msg = "") : BASE(               \
            std::move(trace),                                                           \
            std::forward<Msg>(msg)                                                      \
        ) {}                                                                            \
                                                                                        \
        template <typename Msg = const char*>                                           \
            requires (meta::constructible_from<std::string, Msg>)                       \
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


/* A generic sentinel type to simplify iterator implementations. */
struct sentinel {};


/* A simple convenience struct implementing the overload pattern for `visit()`-style
functions. */
template <typename... Funcs>
struct visitor : Funcs... { using Funcs::operator()...; };


/* A convenience class describing the indices provided to a slice-style subscript
operator.  Generally, an instance of this class will be implicitly constructed using an
initializer list to condense the syntax and provide compile-time guarantees when it
comes to slice shape. */
struct slice {
    std::optional<ssize_t> start;
    std::optional<ssize_t> stop;
    ssize_t step;

    [[nodiscard]] explicit constexpr slice(
        std::optional<ssize_t> start = std::nullopt,
        std::optional<ssize_t> stop = std::nullopt,
        std::optional<ssize_t> step = std::nullopt
    ) :
        start(start),
        stop(stop),
        step(step.value_or(ssize_t(1)))
    {
        if (this->step == 0) {
            throw ValueError("slice step cannot be zero");
        }        
    }

    /* Result of the `normalize` method.  Indices of this form can be passed to
    per-container slice implementations to implement the correct traversal logic. */
    struct normalized {
    private:
        friend slice;

        constexpr normalized(
            ssize_t start,
            ssize_t stop,
            ssize_t step,
            ssize_t length
        ) noexcept :
            start(start),
            stop(stop),
            step(step),
            length(length)
        {}

    public:
        ssize_t start;
        ssize_t stop;
        ssize_t step;
        ssize_t length;
    };

    /* Normalize the provided indices against a container of a given size, returning a
    4-tuple with members `start`, `stop`, `step`, and `length` in that order, and
    supporting structured bindings.  If either of the original `start` or `stop`
    indices were given as negative values or `nullopt`, they will be normalized
    according to the size, and will be truncated to the nearest end if they are out
    of bounds.  `length` stores the total number of elements that will be included in
    the slice */
    [[nodiscard]] constexpr normalized normalize(ssize_t size) const noexcept {
        bool neg = step < 0;
        ssize_t zero = 0;
        normalized result {zero, zero, step, zero};

        // normalize start, correcting for negative indices and truncating to bounds
        if (!start) {
            result.start = neg ? size - ssize_t(1) : zero;  // neg: size - 1 | pos: 0
        } else {
            result.start = *start;
            result.start += size * (result.start < zero);
            if (result.start < zero) {
                result.start = -neg;  // neg: -1 | pos: 0
            } else if (result.start >= size) {
                result.start = size - neg;  // neg: size - 1 | pos: size
            }
        }

        // normalize stop, correcting for negative indices and truncating to bounds
        if (!stop) {
            result.stop = neg ? ssize_t(-1) : size;  // neg: -1 | pos: size
        } else {
            result.stop = *stop;
            result.stop += size * (result.stop < zero);
            if (result.stop < zero) {
                result.stop = -neg;  // neg: -1 | pos: 0
            } else if (result.stop >= size) {
                result.stop = size - neg;  // neg: size - 1 | pos: size
            }
        }

        // compute number of included elements
        ssize_t bias = result.step + (result.step < zero) - (result.step > zero);
        result.length = (result.stop - result.start + bias) / result.step;
        result.length *= (result.length > zero);
        return result;
    }
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
    template <meta::integer T>
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
    template <meta::integer T>
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
    template <meta::integer T>
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
    template <meta::integer T>
    constexpr T next_prime(T n) noexcept {
        for (T i = (n + 1) | 1, end = 2 * n; i < end; i += 2) {
            if (is_prime(i)) {
                return i;
            }
        }
        return 2;  // only returned for n < 2
    }

    /* Compute the next power of two greater than or equal to a given value. */
    template <meta::unsigned_integer T>
    constexpr T next_power_of_two(T n) noexcept {
        --n;
        for (size_t i = 1, bits = sizeof(T) * 8; i < bits; i <<= 1) {
            n |= (n >> i);
        }
        return ++n;
    }

    /* Get the floored log base 2 of an unsigned integer.  Uses an optimized compiler
    intrinsic if available, falling back to a generic implementation otherwise.  Inputs
    equal to zero result in undefined behavior. */
    template <meta::unsigned_integer T>
    constexpr size_t log2(T n) noexcept {
        constexpr size_t max = sizeof(T) * 8 - 1;

        #if defined(__GNUC__) || defined(__clang__)
            if constexpr (sizeof(T) <= sizeof(unsigned int)) {
                return max - __builtin_clz(n);
            } else if constexpr (sizeof(T) <= sizeof(unsigned long)) {
                return max - __builtin_clzl(n);
            } else if constexpr (sizeof(T) <= sizeof(unsigned long long)) {
                return max - __builtin_clzll(n);
            }

        #elif defined(_MSC_VER)
            if constexpr (sizeof(T) <= sizeof(unsigned long)) {
                unsigned long index;
                _BitScanReverse(&index, n);
                return index;
            } else if constexpr (sizeof(T) <= sizeof(uint64_t)) {
                unsigned long index;
                _BitScanReverse64(&index, n);
                return index;
            }
        #endif

        size_t count = 0;
        while (n >>= 1) {
            ++count;
        }
        return count;
    }

    /* A fast modulo operator that works for any b power of two. */
    template <meta::unsigned_integer T>
    constexpr T mod2(T a, T b) noexcept {
        return a & (b - 1);
    }

    /* A Python-style modulo operator (%).

    NOTE: Python's `%` operator is defined such that the result has the same sign as the
    divisor (b).  This differs from C/C++, where the result has the same sign as the
    dividend (a). */
    template <meta::integer T>
    constexpr T pymod(T a, T b) noexcept {
        return (a % b + b) % b;
    }

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

    template <meta::not_void T> requires (!meta::reference<T>)
    struct contiguous_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = ssize_t;
        using value_type = T;
        using reference = meta::as_lvalue<value_type>;
        using const_reference = meta::as_lvalue<meta::as_const<value_type>>;
        using pointer = meta::as_pointer<value_type>;
        using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

        pointer data;
        difference_type index;

        constexpr contiguous_iterator(
            pointer data = nullptr,
            difference_type index = 0
        ) noexcept : data(data), index(index) {};

        constexpr contiguous_iterator(const contiguous_iterator&) noexcept = default;
        constexpr contiguous_iterator(contiguous_iterator&&) noexcept = default;
        constexpr contiguous_iterator& operator=(const contiguous_iterator&) noexcept = default;
        constexpr contiguous_iterator& operator=(contiguous_iterator&&) noexcept = default;

        template <typename V> requires (meta::assignable<reference, V>)
        [[maybe_unused]] constexpr contiguous_iterator& operator=(V&& value) && noexcept(
            noexcept(data[index] = std::forward<V>(value))
        ) {
            data[index] = std::forward<V>(value);
            return *this;
        }

        template <typename V> requires (meta::convertible_to<reference, V>)
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

        [[nodiscard]] constexpr difference_type operator-(
            const contiguous_iterator& rhs
        ) noexcept {
            return index - rhs.index;
        }

        [[nodiscard]] constexpr std::strong_ordering operator<=>(
            const contiguous_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (data != rhs.data) {
                    throw AssertionError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return index <=> rhs.index;
        }
    };

    template <meta::not_void T> requires (!meta::reference<T>)
    struct contiguous_slice {
    private:

        struct initializer {
            std::initializer_list<T>& items;
            [[nodiscard]] constexpr size_t size() const noexcept { return items.size(); }
            [[nodiscard]] constexpr auto begin() const noexcept { return items.begin(); }
            [[nodiscard]] constexpr auto end() const noexcept { return items.end(); }
        };

        template <typename V>
        struct iter {
            using iterator_category = std::input_iterator_tag;
            using difference_type = ssize_t;
            using value_type = V;
            using reference = meta::as_lvalue<value_type>;
            using const_reference = meta::as_lvalue<meta::as_const<value_type>>;
            using pointer = meta::as_pointer<value_type>;
            using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

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
        using reference = meta::as_lvalue<value_type>;
        using const_reference = meta::as_lvalue<meta::as_const<value_type>>;
        using pointer = meta::as_pointer<value_type>;
        using const_pointer = meta::as_pointer<meta::as_const<value_type>>;
        using iterator = iter<value_type>;
        using const_iterator = iter<meta::as_const<value_type>>;

        constexpr contiguous_slice(
            pointer data,
            bertrand::slice::normalized indices
        ) noexcept :
            m_data(data),
            m_indices(indices)
        {}

        constexpr contiguous_slice(const contiguous_slice&) = default;
        constexpr contiguous_slice(contiguous_slice&&) = default;
        constexpr contiguous_slice& operator=(const contiguous_slice&) = default;
        constexpr contiguous_slice& operator=(contiguous_slice&&) = default;

        [[nodiscard]] constexpr pointer data() const noexcept { return m_data; }
        [[nodiscard]] constexpr ssize_t start() const noexcept { return m_indices.start; }
        [[nodiscard]] constexpr ssize_t stop() const noexcept { return m_indices.stop; }
        [[nodiscard]] constexpr ssize_t step() const noexcept { return m_indices.step; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return m_indices.length; }
        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(size()); }
        [[nodiscard]] constexpr bool empty() const noexcept { return !ssize(); }
        [[nodiscard]] explicit operator bool() const noexcept { return ssize(); }

        [[nodiscard]] constexpr iterator begin() noexcept {
            return {m_data, m_indices.start, m_indices.step};
        }

        [[nodiscard]] constexpr const_iterator begin() const noexcept {
            return {m_data, m_indices.start, m_indices.step};
        }

        [[nodiscard]] constexpr const_iterator cbegin() noexcept {
            return {m_data, m_indices.start, m_indices.step};
        }

        [[nodiscard]] constexpr iterator end() noexcept {
            return {m_data, m_indices.stop, m_indices.step};
        }

        [[nodiscard]] constexpr const_iterator end() const noexcept {
            return {m_data, m_indices.stop, m_indices.step};
        }

        [[nodiscard]] constexpr const_iterator cend() const noexcept {
            return {m_data, m_indices.stop, m_indices.step};
        }

        template <typename V>
            requires (meta::constructible_from<V, std::from_range_t, contiguous_slice&>)
        [[nodiscard]] constexpr operator V() && noexcept(noexcept(V(std::from_range, *this))) {
            return V(std::from_range, *this);
        }

        template <typename V>
            requires (
                !meta::constructible_from<V, std::from_range_t, contiguous_slice&> &&
                meta::constructible_from<V, iterator, iterator>
            )
        [[nodiscard]] constexpr operator V() && noexcept(noexcept(V(begin(), end()))) {
            return V(begin(), end());
        }

        template <typename Dummy = value_type>
            requires (
                meta::not_const<Dummy> &&
                meta::destructible<Dummy> &&
                meta::copyable<Dummy>
            )
        [[maybe_unused]] constexpr contiguous_slice& operator=(
            std::initializer_list<value_type> items
        ) && {
            return std::move(*this) = initializer{items};
        }

        template <meta::yields<value_type> Range>
            requires (
                meta::not_const<value_type> &&
                meta::destructible<value_type> &&
                meta::constructible_from<value_type, meta::yield_type<Range>>
            )
        [[maybe_unused]] constexpr contiguous_slice& operator=(Range&& range) && {
            using type = meta::unqualify<value_type>;
            constexpr bool has_size = meta::has_size<meta::as_lvalue<Range>>;
            auto it = std::ranges::begin(range);
            auto end = std::ranges::end(range);

            // if the range has an explicit size, then we can check it ahead of time
            // to ensure that it exactly matches that of the slice
            if constexpr (has_size) {
                if (std::ranges::size(range) != size()) {
                    throw ValueError(
                        "cannot assign a range of size " +
                        std::to_string(std::ranges::size(range)) +
                        " to a slice of size " + std::to_string(size())
                    );
                }
            }

            // If we checked the size above, we can avoid checking it again on each
            // iteration
            if (step() > 0) {
                for (ssize_t i = start(); i < stop(); i += step()) {
                    if constexpr (!has_size) {
                        if (it == end) {
                            throw ValueError(
                                "not enough values to fill slice of size " +
                                std::to_string(size())
                            );
                        }
                    }
                    m_data[i].~type();
                    new (m_data + i) type(*it);
                    ++it;
                }
            } else {
                for (ssize_t i = start(); i > stop(); i += step()) {
                    if constexpr (!has_size) {
                        if (it == end) {
                            throw ValueError(
                                "not enough values to fill slice of size " +
                                std::to_string(size())
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
                        std::to_string(size())
                    );
                }
            }
            return *this;
        }

    private:
        pointer m_data;
        bertrand::slice::normalized m_indices;
    };

    /* A stable, adaptive, 4-way merge sort algorithm for contiguous arrays based on
    work by Gelling, Nebel, Smith, and Wild ("Multiway Powersort", 2023), requiring
    O(n) scratch space for rotations.  A full description of the algorithm and its
    benefits can be found at:

        [1] https://www.wild-inter.net/publications/html/cawley-gelling-nebel-smith-wild-2023.pdf.html

    An earlier, 2-way version of this algorithm is currently in use as the default
    CPython sorting backend for the `sorted()` operator and `list.sort()` method as of
    Python 3.11.  A more complete introduction to that algorithm and how it relates to
    the newer 4-way version can be found in Munro, Wild ("Nearly-Optimal Mergesorts:
    Fast, Practical Sorting Methods That Optimally Adapt to Existing Runs", 2018):

        [2] https://www.wild-inter.net/publications/html/munro-wild-2018.pdf.html

    A full reference implementation for each of these algorithms is available at:

        [3] https://github.com/sebawild/powersort

    The 4-way version presented here is adapted from the above implementations, and is
    generally competitive with or better than `std::sort` and `std::stable_sort`, often
    by a significant margin if the data is already partially sorted, requires expensive
    comparisons, or has a `std::numeric_limits<T>::infinity()` value that can be used
    as a sentinel in comparisons. */
    struct powersort {
    private:

        /// TODO: lift `k` to be a template parameter of `powersort<>`, defaulting to
        /// 4.

        /* The algorithm presented in [1] is generalizable for arbitrary `k >= 2`,
        where `k = 2` represents a typical binary mergesort policy, as implemented in
        [2].  [1] implements 4-way merging with a manually unrolled inner loop and
        tournament tree, which asymptotically halves the total number of comparisons
        needed to sort a given array.  This is especially beneficial if comparisons
        are expensive, as is the case with strings, for example. */
        static constexpr size_t k = 4;

        /* Powersort advises a minimum run length of 24, under which insertion sort
        will be used to grow the run, similar to Timsort. */
        static constexpr size_t min_run_length = 24;

        template <typename Iter, typename Less>
        static constexpr void insertion_sort(
            Iter& begin,
            Iter& unsorted,
            Iter& end,
            Less& less_than
        ) {
            using value_type = std::iterator_traits<Iter>::value_type;
            constexpr auto compare = [](Less& less_than, value_type& temp, Iter& j) {
                try {
                    return less_than(temp, *(j - 1));
                } catch (...) {
                    *j = std::move(temp);
                    throw;
                }
            };

            // rotate elements to the right until we find a proper insertion point
            while (unsorted < end) {
                if (less_than(*unsorted, *(unsorted - 1))) {
                    Iter j = unsorted;
                    value_type temp = std::move(*j);

                    // rotate with error recovery
                    while (compare(less_than, temp, j)) {
                        *j = std::move(*(j - 1));
                        if (--j == begin) {
                            break;
                        }
                    }

                    // replace value at insertion point
                    *j = std::move(temp);
                }
                ++unsorted;
            }
        }

        template <typename Iter, typename Less>
            requires (!meta::reference<Iter> && !meta::reference<Less>)
        struct run {
            Iter begin;
            Iter end;
            size_t power = 0;

            /* Initialize sentinel run. */
            constexpr run(Iter& begin, Iter& end) :
                begin(begin),
                end(end)
            {}

            /* Detect the next run starting at `begin` and not exceeding `end`. */
            constexpr run(Less& less_than, Iter& begin, Iter& end) :
                begin(begin),
                end(begin)
            {
                if (this->end != end && ++this->end != end) {
                    auto next = this->end + 1;
                    if (less_than(*this->end, *this->begin)) {  // strictly decreasing
                        while (next != end && less_than(*next, *this->end)) {
                            ++this->end;
                            ++next;
                        }
                        ++this->end;
                        std::ranges::reverse(this->begin, this->end);
                    } else {  // weakly increasing
                        while (next != end && !less_than(*next, *this->end)) {
                            ++this->end;
                            ++next;
                        }
                        ++this->end;
                    }
                }
            }
        };

        template <typename Iter, typename Less>
            requires (!meta::reference<Iter> && !meta::reference<Less>)
        struct merge_tree {
        private:
            using run = powersort::run<Iter, Less>;
            using value_type = meta::remove_reference<meta::dereference_type<Iter>>;
            using pointer = meta::as_pointer<meta::dereference_type<Iter>>;
            using numeric = std::numeric_limits<meta::unqualify<value_type>>;
            static constexpr bool destroy = !meta::trivially_destructible<value_type>;

            /* Scratch space is allocated as an uninitialized buffer using `malloc()`
            and `free()` so as not to impose default constructibility on `value_type`. */
            struct deleter {
                static constexpr void operator()(pointer p) noexcept {
                    std::free(p);
                }
            };

            /* Nodes in the tournament tree are held in RAII guards to provide
            exception safety.  If an error occurs, the values stored in the node will
            be written back to the input range in a partially-sorted order. */
            template <bool has_sentinel>
            struct tournament_node {
                Iter& output;
                pointer left;
                pointer right;
                pointer left_end = left;
                pointer right_end = right;
                pointer curr = nullptr;

                constexpr ~tournament_node() {
                    if (curr) {
                        *output++ = std::move(*curr);
                        if constexpr (destroy) { curr->~value_type(); }
                    }

                    while (left != left_end) {
                        *output++ = std::move(*left);
                        if constexpr (destroy) { left->~value_type(); }
                        ++left;
                    }
                    if constexpr (has_sentinel && destroy) {
                        left_end->~value_type();
                    }

                    if (right_end) {  // might not have a right subtree
                        while (right != right_end) {
                            *output++ = std::move(*right);
                            if constexpr (destroy) { right->~value_type(); }
                            ++right;
                        }
                        if constexpr (has_sentinel && destroy) {
                            right_end->~value_type();
                        }
                    }
                }
            };

            /* An exception-safe tournament tree generalized to arbitrary `N >= 2`.  If
            an error occurs during a comparison, then all runs will be transferred back
            into the output range in partially-sorted order via RAII. */
            template <size_t N, bool = numeric::has_infinity> requires (N >= 2)
            struct tournament_tree {
                static constexpr size_t R = N + (N % 2);
                Less& less_than;
                pointer scratch;
                Iter output;
                size_t size;
                std::array<pointer, R> begin;  // begin iterators for each run
                std::array<pointer, R> end;  // end iterators for each run
                std::array<size_t, R - 1> internal;  // internal nodes of tree
                size_t winner;  // leaf index of overall winner

                /// NOTE: the tree is represented as a loser tree, meaning the internal
                /// nodes store the leaf index of the losing run for that subtree, and
                /// the winner is bubbled up to the next level of the tree.  The root
                /// of the tree (runner-up) is always the first element of the
                /// `internal` buffer, and each subsequent level is compressed into the
                /// next 2^i elements, from left to right.  The last layer will be
                /// incomplete if `N` is not a power of two.

                template <meta::is<Iter>... Iters> requires (sizeof...(Iters) == N + 1)
                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Iters&... iters
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(meta::unpack_arg<0>(iters...)),
                    size(meta::unpack_arg<N>(iters...) - output),
                    begin([&]<size_t... Is>(std::index_sequence<Is...>) {
                        // begin iterators initialize to offsets within the scratch
                        // space based on the size of each run + an extra sentinel
                        if constexpr (N % 2) {
                            // if `N` is odd, then we have to insert an additional
                            // sentinel at the end of the scratch space to give each
                            // internal node exactly two children
                            return std::array<pointer, R>{
                                (scratch + (meta::unpack_arg<Is>(iters...) - output) + Is)...,
                                scratch + size + N
                            };
                        } else {
                            return std::array<pointer, R>{
                                (scratch + (meta::unpack_arg<Is>(iters...) - output) + Is)...
                            };
                        }
                    }(std::make_index_sequence<N>{})),
                    end(begin)  // end iterators initialize to same as begin
                {
                    // move all runs into scratch space.  Afterwards, the end iterators
                    // will point to the sentinel values for each run, plus a possible
                    // extra sentinel if `N` is odd.
                    [&]<size_t I = 0>(this auto&& self) {
                        if constexpr (I < R - 1) {
                            internal[I] = R;  // nodes are initialized to sentinel
                        }

                        if constexpr (I < N) {
                            Iter& i = meta::unpack_arg<I>(iters...);
                            Iter& j = meta::unpack_arg<I + 1>(iters...);
                            while (i != j) { new (end[I]++) value_type(std::move(*i++)); }
                            new (end[I]) value_type(numeric::infinity());
                            std::forward<decltype(self)>(self).template operator()<I + 1>();

                        } else if constexpr (N % 2) {
                            new (end[I]) value_type(numeric::infinity());
                        }
                    }();
                }

                /* Perform the merge. */
                constexpr void merge() {
                    // Initialize the tournament tree
                    //                internal[0]               internal nodes store
                    //            /                \            losing leaf indices.
                    //      internal[1]          internal[2]    Leaf nodes store
                    //      /       \             /       \     scratch iterators
                    //         ...                   ...
                    // begin[0]   begin[1]   begin[2]   begin[3]   ...
                    for (size_t i = 0; i < R - 1; ++i) {
                        winner = i;
                        size_t node = i + (R - 1);
                        while (node > 0) {
                            size_t parent = (node - 1) / 2;
                            size_t loser = internal[parent];

                            // parent may be uninitialized, in which case the current
                            // node automatically loses
                            if (loser == R) {
                                internal[parent] = node;
                                break;  // parent nodes are guaranteed to be empty
                            }

                            // otherwise, if the current winner loses against the
                            // parent, then we swap it and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }

                            node = parent;
                        }
                    }

                    // merge runs according to tournament tree
                    for (size_t i = 0; i < size; ++i) {
                        // move the overall winner into the output range
                        *output++ = std::move(*begin[winner]);
                        if constexpr (destroy) { begin[winner]->~value_type(); }
                        ++begin[winner];

                        // bubble up next winner
                        size_t node = winner + (R - 1);
                        while (node > 0) {
                            size_t parent = (node - 1) / 2;
                            size_t loser = internal[parent];
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        }
                    }
                }

                /* If an error occurs during comparison, attempt to move the
                unprocessed portions of each run back into the output range and destroy
                sentinels. */
                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            if constexpr (destroy) { begin[i]->~value_type(); }
                            ++begin[i];
                        }
                        if constexpr (destroy) { end[i]->~value_type(); }
                    }
                }
            };

            /* A specialized tournament tree for when the underlying type does not
            have a +inf sentinel value to guard comparisons.  Instead, this uses extra
            boundary checks and merges in stages, where `N` steadily decreases as runs
            are fully consumed. */
            template <size_t N> requires (N >= 2)
            struct tournament_tree<N, false> {
                Less& less_than;
                pointer scratch;
                Iter output;
                size_t size;
                std::array<pointer, N> begin;  // begin iterators for each run
                std::array<pointer, N> end;  // end iterators for each run
                std::array<size_t, N - 1> internal;  // internal nodes of tree
                size_t remaining = N;  // effective size of tree
                size_t smallest = std::numeric_limits<size_t>::max();  // length of smallest non-empty run

                /// TODO: is `smallest` even necessary?

                /// NOTE: this specialization plays tournaments in `N - 1` distinct
                /// stages, where each stage ends when `min_length` reaches zero.  At
                /// At that point, empty runs are removed, and a smaller tournament
                /// tree is constructed with the remaining runs.  This continues until
                /// `N == 2`, in which case we proceed as for a binary merge.

                template <meta::is<Iter>... Iters> requires (sizeof...(Iters) == N + 1)
                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Iters&... iters
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(meta::unpack_arg<0>(iters...)),
                    size(meta::unpack_arg<N>(iters...) - output),
                    begin([&]<size_t... Is>(std::index_sequence<Is...>) {
                        // begin iterators initialize to offsets within the scratch
                        // space based on the size of each run
                        return std::array<pointer, N>{
                            (scratch + (meta::unpack_arg<Is>(iters...) - output) + Is)...
                        };
                    }(std::make_index_sequence<N>{})),
                    end(begin)  // end iterators initialize to same as begin
                {
                    // move all runs into scratch space, without any extra sentinels.
                    // Record the minimum length of each run for the the first stage.
                    [&]<size_t I = 0>(this auto&& self) {
                        if constexpr (I < N) {
                            Iter& i = meta::unpack_arg<I>(iters...);
                            Iter& j = meta::unpack_arg<I + 1>(iters...);
                            while (i != j) { new (end[I]++) value_type(std::move(*i++)); }
                            smallest = std::min(smallest, end[I] - begin[I]);
                            std::forward<decltype(self)>(self).template operator()<I + 1>();
                        }
                    }();
                }

                constexpr void merge() {
                    /// TODO: initialize the tournament tree for this stage

                    /// TODO: proceed until a run is exhausted, then pop from the tree
                    /// and reinitialize the tournament tree for the next stage
                }


                /* End a merge stage by pruning empty runs and computing the minimum
                length for the next stage.  */
                constexpr void advance() {
                    smallest = std::numeric_limits<size_t>::max();
                    size_t i = 0;
                    while (i < remaining) {
                        // compute length of run
                        size_t size = end[i] - begin[i];

                        // if empty, pop from the array and left shift subsequent runs
                        if (!size) {
                            --remaining;
                            for (size_t j = i; j < remaining; ++j) {
                                begin[j] = begin[j + 1];
                                end[j] = end[j + 1];
                            }

                        // otherwise, record the minimum length and advance
                        } else {
                            smallest = std::min(smallest, size);
                            ++i;
                        }
                    }
                }



                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            if constexpr (destroy) { begin[i]->~value_type(); }
                            ++begin[i];
                        }
                    }
                }
            };

            /* An optimized tournament tree for binary merges with sentinel values. */
            template <>
            struct tournament_tree<2, true> {
                Less& less_than;
                pointer scratch;
                Iter output;
                size_t size;
                std::array<pointer, 2> begin;
                std::array<pointer, 2> end;

                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Iter& l,
                    Iter& m,
                    Iter& r
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(l),
                    size(r - l),
                    begin {scratch, scratch + (m - l) + 1},
                    end(begin)
                {
                    while (l != m) { new (end[0]++) value_type(std::move(*l++)); }
                    new (end[0]) value_type(numeric::infinity());
                    while (m != r) { new (end[1]++) value_type(std::move(*m++)); }
                    new (end[1]) value_type(numeric::infinity());
                }

                constexpr void merge() {
                    for (size_t i = 0; i < size; ++i) {
                        bool less = less_than(*begin[1], *begin[0]);
                        *output++ = std::move(*begin[less]);
                        if constexpr (destroy) { begin[less]->~value_type(); }
                        ++begin[less];
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            if constexpr (destroy) { begin[i]->~value_type(); }
                            ++begin[i];
                        }
                        if constexpr (destroy) { end[i]->~value_type(); }
                    }
                }
            };

            /* An optimized tournament tree for binary merges without sentinel values,
            which only copies the smaller run into the scratch space. */
            template <>
            struct tournament_tree<2, false> {
                Less& less_than;
                pointer scratch;
                Iter& l;
                Iter& m;
                Iter& r;

                /* Merge policy used when the left run is smaller than the right. */
                struct left_merge {
                    Less& less_than;
                    Iter output;
                    pointer c1;
                    pointer e1;
                    Iter c2;
                    Iter e2;

                    constexpr left_merge(
                        Less& less_than,
                        pointer scratch,
                        Iter& l,
                        Iter& m,
                        Iter& r,
                        size_t n
                    ) :
                        less_than(less_than),
                        output(l),
                        c1(scratch),
                        e1(scratch + n),
                        c2(m),
                        e2(r)
                    {
                        for (size_t i = 0; i < n; ++i) {
                            new (scratch++) value_type(std::move(*l++));
                        }
                    }

                    constexpr void merge() {
                        while (c1 != e1 && c2 != e2) {
                            if (less_than(*c2, *c1)) {
                                *output++ = std::move(*c2++);  // no need to destroy
                            } else {
                                *output++ = std::move(*c1);
                                if constexpr (destroy) { c1->~value_type(); }
                                ++c1;
                            }
                        }
                        while (c1 != e1) {
                            *output++ = std::move(*c1);
                            if constexpr (destroy) { c1->~value_type(); }
                            ++c1;
                        }
                    }

                    constexpr ~left_merge() {
                        while (c2 != e2) {
                            *output++ = std::move(*c2++);  // no need to destroy
                        }
                        while (c1 != e1) {
                            *output++ = std::move(*c1);
                            if constexpr (destroy) { c1->~value_type(); }
                            ++c1;
                        }
                    }
                };

                /* Merge policy used when the right run is smaller than the left. */
                struct right_merge {
                    Less& less_than;
                    Iter output;
                    Iter c1;
                    Iter s1;
                    pointer c2;
                    pointer s2;

                    constexpr right_merge(
                        Less& less_than,
                        pointer scratch,
                        Iter& l,
                        Iter& m,
                        Iter& r,
                        size_t n
                    ) :
                        less_than(less_than),
                        output(r - 1),
                        c1(m - 1),
                        s1(l - 1),
                        c2(scratch + n - 1),
                        s2(scratch - 1)
                    {
                        for (size_t i = 0; i < n; ++i) {
                            new (scratch++) value_type(std::move(*m++));
                        }
                    }

                    constexpr void merge() {
                        while (c1 != s1 && c2 != s2) {
                            if (less_than(*c2, *c1)) {
                                *output-- = std::move(*c2);
                                if constexpr (destroy) { c2->~value_type(); }
                                --c2;
                            } else {
                                *output-- = std::move(*c1--);  // no need to destroy
                            }
                        }
                        while (c2 != s2) {
                            *output-- = std::move(*c2);
                            if constexpr (destroy) { c2->~value_type(); }
                            --c2;
                        }
                    }

                    // due to postfix decrement, `c1` and `c2` are already at the
                    // correct positions when the call operator exits
                    constexpr ~right_merge() {
                        while (c1 != s1) {
                            *output-- = std::move(*c1--);  // no need to destroy
                        }
                        while (c2 != s2) {
                            *output-- = std::move(*c2);
                            if constexpr (destroy) { c2->~value_type(); }
                            --c2;
                        }
                    }
                };

                constexpr void merge() {
                    auto n1 = m - l;
                    auto n2 = r - m;
                    if (n1 <= n2) {
                        left_merge{less_than, scratch, l, m, r, n1}.merge();
                    } else {
                        right_merge{less_than, scratch, l, m, r, n2}.merge();
                    }
                }
            };

            Less& less_than;
            std::vector<run> stack;
            std::unique_ptr<value_type, deleter> scratch;

            static constexpr size_t ceil_log4(size_t n) noexcept {
                return (impl::log2(n - 1) >> 1) + 1;
            }

            /// NOTE: these implementations are taken straight from the reference, and
            /// have only been lightly edited for readability, and to leverage move
            /// semantics + custom comparisons with exception safety.

            static constexpr size_t get_power(
                size_t n,
                size_t prev_begin,
                size_t next_begin,
                size_t next_end
            ) noexcept {
                // if a built-in compiler intrinsic is available, use it
                #if defined(__GNUC__) || defined(__clang__)
                    size_t l = prev_begin + next_begin;
                    size_t r = next_begin + next_end;
                    size_t a = (l << 30) / n;
                    size_t b = (r << 30) / n;
                    if constexpr (sizeof(size_t) <= sizeof(unsigned int)) {
                        return ((__builtin_clz(a ^ b) - 1) >> 1) + 1;
                    } else if constexpr (sizeof(size_t) <= sizeof(unsigned long)) {
                        return ((__builtin_clzl(a ^ b) - 1) >> 1) + 1;
                    } else if constexpr (sizeof(size_t) <= sizeof(unsigned long long)) {
                        return ((__builtin_clzll(a ^ b) - 1) >> 1) + 1;
                    }

                #elif defined(_MSC_VER)
                    size_t l = prev_begin + next_begin;
                    size_t r = next_begin + next_end;
                    size_t a = (l << 30) / n;
                    size_t b = (r << 30) / n;
                    unsigned long index;
                    if constexpr (sizeof(size_t) <= sizeof(unsigned long)) {
                        _BitScanReverse(&index, a ^ b);
                    } else if constexpr (sizeof(size_t) <= sizeof(uint64_t)) {
                        _BitScanReverse64(&index, a ^ b);
                    }
                    return ((index - 1) >> 1) + 1;

                #else
                    size_t l = prev_begin + next_begin;
                    size_t r = next_begin + next_end;
                    size_t n_common_bits = 0;
                    bool digit_a = l >= n;
                    bool digit_b = r >= n;
                    while (digit_a == digit_b) {
                        ++n_common_bits;
                        if (digit_a) {
                            l -= n;
                            r -= n;
                        }
                        l *= 2;
                        r *= 2;
                        digit_a = l >= n;
                        digit_b = r >= n;
                    }
                    return (n_common_bits >> 1) + 1;
                #endif
            }

            constexpr void merge_4runs(Iter l, Iter m1, Iter m2, Iter m3, Iter r) {
                pointer p = scratch.get();
                auto n = r - l;
                auto o = l;

                // if the iterator's value type has a +inf sentinel, then we can use an
                // optimized merge loop
                if constexpr (numeric::has_infinity) {
                    tournament_node<true> x {o, p, p + (m1 - l) + 1};
                    tournament_node<true> y {o, p + (m2 - l) + 2, p + (m3 - l) + 3};

                    // move runs to scratch space and append sentinels
                    while (l != m1) { new (x.left_end++) value_type(std::move(*l++)); }
                    new (x.left_end) value_type(numeric::infinity());
                    while (m1 != m2) { new (x.right_end++) value_type(std::move(*m1++)); }
                    new (x.right_end) value_type(numeric::infinity());
                    while (m2 != m3) { new (y.left_end++) value_type(std::move(*m2++)); }
                    new (y.left_end) value_type(numeric::infinity());
                    while (m3 != r) { new (y.right_end++) value_type(std::move(*m3++)); }
                    new (y.right_end) value_type(numeric::infinity());

                    // initialize tournament tree
                    //          z               `x`, `y`, `z` store iterators
                    //       /     \            from winning runs.
                    //     x         y          `left` also stores whether min
                    //   /  \      /  \         came from left subtree.
                    // c1    c2  c3    c4
                    bool left;
                    x.curr = less_than(*x.right, *x.left) ? x.right++ : x.left++;
                    y.curr = less_than(*y.right, *y.left) ? y.right++ : y.left++;
                    auto z = less_than(*y.curr, *x.curr) ?
                        (left = false, y.curr) : (left = true, x.curr);  // comma operator

                    *o++ = std::move(*z);  // move root into output
                    if constexpr (destroy) { z->~value_type(); }

                    for (decltype(n) i = 1; i < n; ++i) {
                        if (left) {  // root came from c1 or c2, so recompute x
                            x.curr = less_than(*x.right, *x.left) ? x.right++ : x.left++;
                        } else {  // root came from c3 or c4, so recompute y
                            y.curr = less_than(*y.right, *y.left) ? y.right++ : y.left++;
                        }

                        // recompute root
                        if (less_than(*y.curr, *x.curr)) {
                            z = y.curr;
                            y.curr = nullptr;  // in case of error
                            left = false;
                        } else {
                            z = x.curr;
                            x.curr = nullptr;  // in case of error
                            left = true;
                        }

                        // move root into output
                        *o++ = std::move(*z);
                        if constexpr (destroy) { z->~value_type(); }
                    }

                // otherwise, we have to merge in stages using extra boundary checks
                } else {
                    /// TODO: use a different form of generalized tournament tree, which
                    /// encapsulates all the stages.

                    tournament_node<false> x {o, p, p + (m1 - l)};
                    tournament_node<false> y {o, p + (m2 - l), p + (m3 - l)};

                    // move runs into scratch space
                    while (l != m1) { new (x.left_end++) value_type(std::move(*l++)); }
                    while (m1 != m2) { new (x.right_end++) value_type(std::move(*m1++)); }
                    while (m2 != m3) { new (y.left_end++) value_type(std::move(*m2++)); }
                    while (m3 != r) { new (y.right_end++) value_type(std::move(*m3++)); }

                    // merging is done by stages, where each stage continues until
                    // the minimum run length reaches zero, at which point empty stages
                    // will be discarded and the next stage will rebuild an optimized
                    // tournament tree from the remaining runs, devolving to a 2-way
                    // merge for the last stage
                    stages stage {
                        /// TODO: pass the tournament nodes into the constructor,
                        /// which stores references to the iterators, or maybe the
                        /// stages object should handle error recovery for itself?
                        {x.left, x.right, y.left, y.right},
                        {x.left_end, x.right_end, y.left_end, y.right_end},
                        4
                    };
                    stage.advance();  // purge empty runs and get min length

                    // 
                    while (o != r) {
                        switch (stage.remaining) {
                            case 4: if (stage.template merge<4>(o, r)) break;
                            case 3: if (stage.template merge<3>(o, r)) break;
                            case 2: if (stage.template merge<2>(o, r)) break;
                            default: return;
                        }
                    }
                }
            }

        public:
            constexpr merge_tree(
                size_t length,
                Less& less_than,
                run& prev,
                Iter& begin,
                Iter& end
            ) :
                less_than(less_than),
                scratch(
                    reinterpret_cast<pointer>(
                        std::malloc(sizeof(value_type) * (length + k + 1))
                    ),
                    deleter{}
                )
            {
                if (!scratch) {
                    throw MemoryError("failed to allocate scratch space");
                }
                stack.reserve((k - 1) * (ceil_log4(length) + 1));
                stack.emplace_back(begin, end);  // power 0 as sentinel entry
                while (prev.end < end) {

                    // grow and possibly reverse next run using insertion sort
                    run next {less_than, prev.end, end};
                    if ((next.end - next.begin) < min_run_length) {
                        Iter unsorted = next.end;
                        next.end = std::min(end, next.begin + min_run_length);
                        insertion_sort(
                            next.begin,
                            unsorted,
                            next.end,
                            less_than
                        );
                    }

                    // compute previous run's power with respect to next run
                    prev.power = get_power(
                        length,
                        prev.begin - begin,
                        next.begin - begin,
                        next.end - begin
                    );

                    // invariant: powers on stack weakly increase from bottom to top.
                    // If violated, merge runs with equal power into `prev` until
                    // invariant is restored.  Only at most the top 4 runs will ever
                    // meet this criteria due to the structure of the merge tree.
                    while (stack.back().power > prev.power) {
                        run* top = &stack.back();
                        size_t same_power = 1;
                        while ((top - same_power)->power == top->power) {
                            ++same_power;
                        }
                        /// TODO: this can be generalized to arbitrary `k` by using an
                        /// index sequence.
                        if (same_power == 1) {  // 2way
                            Iter f1 = top->begin;
                            tournament_tree<2>{
                                less_than,
                                scratch.get(),
                                f1,
                                prev.begin,
                                prev.end
                            }.merge();
                            prev.begin = f1;
                        } else if (same_power == 2) {  // 3way
                            Iter f1 = (top - 1)->begin;
                            Iter f2 = top->begin;
                            tournament_tree<3>{
                                less_than,
                                scratch.get(),
                                f1,
                                f2,
                                prev.begin,
                                prev.end
                            }.merge();
                            prev.begin = f1;
                        } else {  // 4way
                            Iter f1 = (top - 2)->begin;
                            Iter f2 = (top - 1)->begin;
                            Iter f3 = top->begin;
                            merge_4runs(f1, f2, f3, prev.begin, prev.end);
                            prev.begin = f1;
                        }
                        stack.resize(stack.size() - same_power);  // pop merged runs
                    }

                    // push next run onto stack
                    stack.emplace_back(std::move(prev));
                    prev = std::move(next);
                }
            }

            /* Successively merge the top 4 elements on the stack.  Because runs
            typically increase in size exponentially as we empty the stack, we can
            manually merge the first 2-3 such that the stack size is reduced to a
            multiple of `3n + 1`.  That means all subsequent merges can be 4-way,
            maximizing the benefits of the tournament tree and minimizing total
            comparisons. */
            constexpr void merge_down(run& prev) {
                run* top = &stack.back();
                run* bottom = &stack.front();

                size_t n_runs = stack.size() + 1;  // stack + prev
                switch (n_runs % (k - 1)) {
                    /// TODO: this switch statement can be generalized to arbitrary
                    /// k by using a vtable with function pointers

                    case 0:  // merge topmost 3 runs
                        tournament_tree<3>{
                            less_than,
                            scratch.get(),
                            (top - 1)->begin,
                            top->begin,
                            prev.begin,
                            prev.end
                        }.merge();
                        prev.begin = (top - 1)->begin;
                        top -= 2;
                        break;
                    case 2: // merge topmost 2 runs
                        tournament_tree<2>{
                            less_than,
                            scratch.get(),
                            top->begin,
                            prev.begin,
                            prev.end
                        }.merge();
                        prev.begin = top->begin;
                        --top;
                        break;
                    default:
                        break;
                }

                while (top > bottom) {  // 4-way merge remaining runs
                    /// TODO: in the generalized case, this will always be a `k`-way
                    /// merge
                    merge_4runs(
                        (top - 2)->begin,
                        (top - 1)->begin,
                        top->begin,
                        prev.begin,
                        prev.end
                    );
                    prev.begin = (top - 2)->begin;
                    top -= 3;
                }
            }
        };

    public:

        /* Execute the sort algorithm using unsorted values in the range [begin, end)
        and placing the result back into the same range.  The `less_than` comparison
        function is used to determine the order of the elements.  If no comparison
        function is given, it will default to a transparent `<` operator for each
        element.  If an exception occurs during a comparison, the input range will be
        left in a valid but unspecified state, and may be partially sorted.  Any other
        exception (e.g. in a move constructor, destructor, or iterator operation) can
        result in undefined behavior. */
        template <meta::contiguous_iterator Iter, typename Less = std::less<>>
            requires (
                meta::copyable<Iter> &&
                meta::movable<meta::remove_reference<meta::dereference_type<Iter>>> &&
                meta::move_assignable<meta::remove_reference<meta::dereference_type<Iter>>> &&
                meta::destructible<meta::remove_reference<meta::dereference_type<Iter>>> &&
                meta::invoke_returns<
                    bool,
                    meta::as_lvalue<Less>,
                    meta::dereference_type<Iter>,
                    meta::dereference_type<Iter>
                >
            )
        static constexpr void operator()(Iter begin, Iter end, Less&& less_than = {}) {
            using value_type = std::iterator_traits<Iter>::value_type;
            using L = meta::remove_reference<Less>;

            size_t length = end - begin;
            if (length < 2) {
                return;  // trivially sorted
            }

            // identify and possibly reverse first weakly increasing or strictly
            // decreasing run starting at `begin`
            run<Iter, L> prev {less_than, begin, end};

            // grow run to minimum length using insertion sort
            if ((prev.end - prev.begin) < min_run_length) {
                Iter unsorted = prev.end;
                prev.end = std::min(end, prev.begin + min_run_length);
                insertion_sort(
                    prev.begin,
                    unsorted,
                    prev.end,
                    less_than
                );
            }

            // continue left-right scan to build consolidated merge tree
            merge_tree<Iter, L> stack {length, less_than, prev, begin, end};

            // flatten tree to complete sort
            stack.merge_down(prev);
        }
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
inline void assert_(bool cnd, const char* msg = "") noexcept(!DEBUG) {
    if constexpr (DEBUG) {
        if (!cnd) {
            throw AssertionError(msg);
        }
    }
}


/* ADL-friendly swap method.  Equivalent to calling `l.swap(r)` as a member method. */
template <typename T> requires (requires(T& l, T& r) { {l.swap(r)} -> meta::is_void; })
constexpr void swap(T& l, T& r) noexcept(noexcept(l.swap(r))) {
    l.swap(r);
}


/* Equivalent to calling `std::hash<T>{}(...)`, but without explicitly specializating
`std::hash`. */
template <meta::hashable T>
[[nodiscard]] constexpr size_t hash(T&& obj) noexcept(meta::nothrow::hashable<T>) {
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
            typename meta::detail::sorted<Less, meta::unqualify<T>>::type,
            T
        > || (
            meta::constructible_from<
                typename meta::detail::sorted<Less, meta::unqualify<T>>::type,
                T
            > &&
            meta::default_constructible<Less> &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<
                    typename meta::detail::sorted<
                        Less,
                        meta::unqualify<T>
                    >::type::value_type
                >>,
                meta::as_lvalue<meta::as_const<
                    typename meta::detail::sorted<
                        Less,
                        meta::unqualify<T>
                    >::type::value_type
                >>
            >
        )
    )
[[nodiscard]] decltype(auto) sorted(T&& container) noexcept(
    meta::is<typename meta::detail::sorted<Less, meta::unqualify<T>>::type, T> ||
    noexcept(typename meta::detail::sorted<Less, meta::unqualify<T>>::type(
        std::forward<T>(container)
    ))
) {
    using type = meta::detail::sorted<Less, meta::unqualify<T>>::type;
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
        meta::constructible_from<
            typename meta::detail::sorted<Less, T>::type,
            Args...
        > &&
        meta::invoke_returns<
            bool,
            meta::as_lvalue<Less>,
            meta::as_lvalue<meta::as_const<
                typename meta::detail::sorted<Less, T>::type::value_type
            >>,
            meta::as_lvalue<meta::as_const<
                typename meta::detail::sorted<Less, T>::type::value_type
            >>
        >
    )
[[nodiscard]] meta::detail::sorted<Less, T>::type sorted(Args&&... args) noexcept(
    noexcept(typename meta::detail::sorted<Less, T>::type(std::forward<Args>(args)...))
) {
    return typename meta::detail::sorted<Less, T>::type(std::forward<Args>(args)...);
}


/// TODO: template template versions of sorted() that allow for CTAD


}  // namespace bertrand


#endif  // BERTRAND_COMMON_H
