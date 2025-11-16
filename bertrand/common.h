#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <forward_list>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <mdspan>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#if __has_include(<stdfloat>)
    #include <stdfloat>
#endif


// required for virtual memory management
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


/* Bertrand exposes as much information about the target platform and compiler as
possible so that downstream code can access it in a standardized, constexpr-friendly
manner. */
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


/* The version specifier for the C++ standard library. */
#ifdef _MSVC_LANG
    #define CXXSTD _MSVC_LANG
#else
    #define CXXSTD __cplusplus
#endif


/* A flag indicating whether the project is being built in debug mode (true) or
release mode (false).  If true, then extra assertions will be inserted around common
failure points, replacing undefined behavior with traced exceptions that are thrown
using standard try/catch semantics.  If false, then these assertions will be optimized
out, increasing performance at the cost of possible undefined behavior unless the
conditions that trigger them have been explicitly handled.

The state of this flag is determined by the presence of a `BERTRAND_RELEASE` macro,
which will be injected by the build system when `bertrand build` is invoked with the
`--release` flag.  If such a macro is present, then this flag will be false, otherwise
it will be true (the default).

Generally speaking, debug builds affect the following operations:

    -   `operator*` and `operator->` dereferencing for `Optional`, which will throw a
        `TypeError` if accessed in the empty state.
    -   `operator[]` container indexing, which will throw an `IndexError` if the index
        is out of bounds after applying Python-style wraparound.

    /// TODO: list some more operations that are affected by this flag.

Note that this is not an exhaustive list, and downstream code may branch on this flag
to enable further assertions as needed.

Bertrand makes a concerted effort to make these assertions as cheap as possible, so
that debug builds can maintain reasonable performance relative to their release
counterparts.  Development should therefore always be done in a debug build first, in
order to catch potential issues as early as possible, in conjunction with automated
testing.  Once all possible sources of undefined behavior have been eliminated, then
this flag can be set false as a final optimization before shipping the code to
production, or simply left as true if the real-world performance impact is negligible.

Users should take care not to assume the state of this flag in their own code, and not
attempt to catch any of the exceptions that it produces, to avoid obfuscating the
source of the error.  Bertrand's error handling philosophy dictates that exceptions are
reserved for truly exceptional circumstances, which should bubble up and crash the
program after closing any open resources.  For errors that may occur as a part of
normal operation, and can therefore be handled gracefully, the `Expected` monad and
errors-as-values paradigm should always be preferred, which allows them to be
exhaustively enforced by the type system. */
#ifdef BERTRAND_RELEASE
    constexpr bool DEBUG = false;
#else
    /// TODO: switch this back to true
    constexpr bool DEBUG = false;
#endif


/* Bertrand uses a large amount of template metaprogramming for its basic features,
which can easily overflow the compiler's normal template recursion limits.  This should
be rare, but can happen in some cases, forcing the user to manually increase the limit.
This can be done by providing the `--template-recursion-limit=...` flag when invoking
`bertrand build`, which sets the value of the following constant for use in C++. */
#ifdef BERTRAND_TEMPLATE_RECURSION_LIMIT
    constexpr size_t TEMPLATE_RECURSION_LIMIT = BERTRAND_TEMPLATE_RECURSION_LIMIT;
#else
    constexpr size_t TEMPLATE_RECURSION_LIMIT = 8192;
#endif
static_assert(
    TEMPLATE_RECURSION_LIMIT > 0,
    "Template recursion limit must be positive."
);

/* Bertrand emits internal vtables to handle algebraic types, such as unions and
tuples.  As an optimization, these vtables will be omitted in favor of simple `if/else`
chains as long as the total number of alternatives is less than this threshold, which
is set by the `--min-vtable-size=...` flag when invoking `bertrand build`.

Profile Guided Optimization (PGO) can be used to optimally select this threshold for a
given architecture, as there is a hardware-dependent sweet spot based on the
characteristics of the branch predictor and relative cost of vtable indirection. */
#ifdef BERTRAND_MIN_VTABLE_SIZE
    inline constexpr size_t MIN_VTABLE_SIZE = BERTRAND_MIN_VTABLE_SIZE;
#else
    inline constexpr size_t MIN_VTABLE_SIZE = 5;
#endif
static_assert(
    MIN_VTABLE_SIZE >= 2,
    "vtable sizes with fewer than 2 elements are not meaningful"
);


namespace impl {
    struct prefer_constructor_tag {};
    struct basic_tuple_tag : prefer_constructor_tag {};

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


/* A convenience function that produces a `std::type_identity` instance specialized for
type `T`.  Instances of this form can be supplied as `auto` template parameters in
order to mix values and types within the same argument list.  This may be deprecated in
favor of static reflection specifiers or ideally universal template parameters if and
when those features become available. */
template <typename T>
constexpr std::type_identity<T> type;


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
        constexpr bool pointer = ::std::is_pointer_v<meta::unqualify<T>>;

    }

    template <typename T>
    concept lvalue = detail::lvalue<T>;

    template <typename T>
    concept not_lvalue = !lvalue<T>;

    template <typename T>
    using as_lvalue = ::std::add_lvalue_reference_t<T>;

    template <typename T>
    using remove_lvalue = ::std::conditional_t<lvalue<T>, remove_reference<T>, T>;

    template <typename T>
    concept rvalue = detail::rvalue<T>;

    template <typename T>
    concept not_rvalue = !rvalue<T>;

    template <typename T>
    using as_rvalue = ::std::add_rvalue_reference_t<T>;

    template <typename T>
    using remove_rvalue = ::std::conditional_t<rvalue<T>, remove_reference<T>, T>;

    template <typename T>
    concept reference = lvalue<T> || rvalue<T>;

    template <typename T>
    concept not_reference = !reference<T>;

    template <typename T>
    concept pointer = detail::pointer<T>;

    template <typename T>
    concept not_pointer = !pointer<T>;

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

    /* Forward an arbitrary argument as a const-qualified type. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) to_const(T&& t) noexcept {
        return (std::forward<as_const<T>>(t));
    }

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
    concept const_ref = is_const<T> && lvalue<T>;

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

    template <typename T>
    using forward = ::std::conditional_t<
        pointer<T>,
        remove_reference<T>,
        decltype(::std::forward<T>(::std::declval<T>()))
    >;

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
    concept trivially_constructible = detail::trivially_constructible<T, Args...>;

    template <typename T>
    concept default_constructible = detail::default_constructible<T>;

    namespace nothrow {

        template <typename T, typename... Args>
        concept constructible_from =
            meta::constructible_from<T, Args...> &&
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

        template <typename L, typename R>
        constexpr bool convertible_to = ::std::convertible_to<L, R>;

        template <typename L, typename R>
        constexpr bool nothrow_convertible_to =
            ::std::is_nothrow_convertible_v<L, R>;

    }

    template <typename L, typename R>
    concept convertible_to = detail::convertible_to<L, R>;

    template <typename L, typename R>
    concept explicitly_convertible_to = requires(L from) {
        {static_cast<R>(from)} -> ::std::same_as<R>;
    };

    template <typename T>
    concept truthy = explicitly_convertible_to<T, bool>;

    template <typename L, typename R>
    concept bit_castable_to = requires(L from) {
        {::std::bit_cast<R>(from)} -> ::std::same_as<R>;
    };

    namespace nothrow {

        template <typename L, typename R>
        concept convertible_to =
            meta::convertible_to<L, R> &&
            detail::nothrow_convertible_to<L, R>;

        template <typename L, typename R>
        concept explicitly_convertible_to =
            meta::explicitly_convertible_to<L, R> &&
            requires(L from) {
                {static_cast<R>(from)} noexcept -> ::std::same_as<R>;
            };

        template <typename T>
        concept truthy = meta::truthy<T> && nothrow::explicitly_convertible_to<T, bool>;

        template <typename L, typename R>
        concept bit_castable_to =
            meta::bit_castable_to<L, R> &&
            requires(L from) {
                {::std::bit_cast<R>(from)} noexcept -> ::std::same_as<R>;
            };

    }

    /* Trigger implicit conversion operators and/or implicit constructors, but not
    explicit ones.  In contrast, static_cast<>() will trigger explicit constructors on
    the target type, which can give unexpected results and violate type safety. */
    template <not_void To, typename From>
    [[nodiscard]] constexpr To implicit_cast(From&& v)
        noexcept(nothrow::convertible_to<From, To>)
        requires(convertible_to<From, To>)
    {
        return ::std::forward<From>(v);
    }

    /* Extend the lifetime of an rvalue input by converting rvalues into prvalues and
    leaving lvalues unchanged. */
    template <typename T>
    [[nodiscard]] constexpr meta::remove_rvalue<T> decay(T&& v)
        noexcept (nothrow::convertible_to<T, meta::remove_rvalue<T>>)
        requires (convertible_to<T, meta::remove_rvalue<T>>)
    {
        return ::std::forward<T>(v);
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

    /* Explicitly invoke a `.copy()` member method for the given type. */
    template <typename T>
    [[nodiscard]] constexpr T copy(const T& v)
        noexcept (requires{{v.copy()} noexcept -> nothrow::convertible_to<T>;})
        requires (requires{{v.copy()} -> convertible_to<T>;})
    {
        return v.copy();
    }

    /* Explicitly invoke a copy constructor for the given type. */
    template <typename T>
    [[nodiscard]] constexpr T copy(const T& v)
        noexcept (nothrow::copyable<T>)
        requires (!requires{{v.copy()} -> convertible_to<T>;} && copyable<T>)
    {
        return v;
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

    /* Explicitly invoke a copy or move constructor for the given type. */
    template <movable T>
    [[nodiscard]] constexpr unqualify<T> move(T&& v) noexcept (nothrow::movable<T>) {
        return ::std::forward<T>(v);
    }

    namespace detail {

        struct swap_fn {
            template <typename L, typename R>
            constexpr void operator()(L& l, R& r) const
                noexcept (requires{{l.swap(r)} noexcept;})
                requires (requires{{l.swap(r)};})
            {
                l.swap(r);
            };

            template <typename L, typename R>
            constexpr void operator()(L& l, R& r) const
                noexcept (requires{{::std::ranges::swap(l, r)} noexcept;})
                requires (!requires{{l.swap(r)};} && requires{{::std::ranges::swap(l, r)};})
            {
                ::std::ranges::swap(l, r);
            };
        };

    }

    /* A generalized `swap()` operator between two types.  This is
    expression-equivalent to `std::ranges::swap()`, except that it first checs for an
    `lhs.swap(rhs)` member method before proceeding to a `swap(lhs, rhs)` ADL method. */
    inline constexpr detail::swap_fn swap;

    template <typename L, typename R = L>
    concept swappable = requires(as_lvalue<L> l, as_lvalue<R> r) {
        {swap(l, r)};
    };

    namespace nothrow {

        template <typename L, typename R = L>
        concept swappable = meta::swappable<L, R> && requires(as_lvalue<L> l, as_lvalue<R> r) {
            {swap(l, r)} noexcept;
        };

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

        template <typename... Ts>
        struct first_type { using type = void; };
        template <typename T, typename... Ts>
        struct first_type<T, Ts...> { using type = T; };

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

        template <typename T>
        constexpr bool is_template = false;
        template <template <typename...> class C, typename... Ts>
        constexpr bool is_template<C<Ts...>> = true;

        template <typename T>
        struct specialization { using type = meta::pack<>; };
        template <template <typename...> class C, typename... Ts>
        struct specialization<C<Ts...>> { using type = meta::pack<Ts...>; };

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

    /* Extract the first type in a parameter pack, assuming it is not empty.  If it
    is empty, the result will be `void` instead. */
    template <typename... Ts>
    using first_type = detail::first_type<Ts...>::type;

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

    /* `true` if the type is a specialization of some template.  Note that due to
    restrictions in C++23, the template and all its parameters must be types.  This
    restriction may be relaxed in a later standard if/when universal template
    parameters are standardized. */
    template <typename T>
    concept is_template = detail::is_template<unqualify<T>>;

    /* A pack containing the template parameters that were used to specialize some
    template.  Note that due to restrictions in C++23, the template and all its
    parameters must be types.  This restriction may be relaxed in a later standard
    if/when universal template parameters are standardized. */
    template <typename T>
    using specialization = detail::specialization<unqualify<T>>::type;

    namespace detail {

        template <typename out, typename...>
        struct concat { using type = out; };
        template <typename... out, typename... curr, typename... next>
        struct concat<pack<out...>, pack<curr...>, next...> {
            using type = concat<pack<out..., curr...>, next...>::type;
        };

        template <typename out, typename...>
        struct concat_unique { using type = out; };
        template <typename out, typename... next>
        struct concat_unique<out, pack<>, next...> : concat_unique<out, next...> {};
        template <typename... out, typename T, typename... Ts, typename... next>
            requires (!::std::same_as<T, out> && ...)
        struct concat_unique<pack<out...>, pack<T, Ts...>, next...> :
            concat_unique<pack<out..., T>, pack<Ts...>, next...>
        {};
        template <typename... out, typename T, typename... Ts, typename... next>
            requires (::std::same_as<T, out> || ...)
        struct concat_unique<pack<out...>, pack<T, Ts...>, next...> :
            concat_unique<pack<out...>, pack<Ts...>, next...>
        {};

        template <typename out, typename...>
        struct reverse { using type = out; };
        template <typename... out, typename T, typename... Ts>
        struct reverse<pack<out...>, T, Ts...> {
            using type = reverse<pack<T, out...>, Ts...>::type;
        };

        template <typename out, size_t, size_t, typename...>
        struct repeat { using type = out; };
        template <typename... out, size_t N, typename T, typename... Ts>
        struct repeat<pack<out...>, 0, N, T, Ts...> : repeat<pack<out..., T>, N, N, T, Ts...> {};
        template <typename... out, size_t N, size_t I, typename T, typename... Ts>
        struct repeat<pack<out...>, I, N, T, Ts...> : repeat<pack<out..., T>, I - 1, N, T, Ts...> {};

        template <typename, typename...>
        struct product;
        template <size_t... Is, typename... packs>
        struct product<::std::index_sequence<Is...>, packs...> {
            template <typename out, size_t, typename...>
            struct permute { using type = out; };
            template <typename... out, size_t I, typename curr, typename... next>
            struct permute<pack<out...>, I, curr, next...> : permute<
                pack<out..., typename curr::template at<I / (next::size() * ... * 1)>>,
                I % (next::size() * ... * 1),
                next...
            > {};
            using type = pack<typename permute<pack<>, Is, packs...>::type...>;
        };

        template <template <typename> class F, typename...>
        constexpr bool boolean_value = true;
        template <template <typename> class F, typename U, typename... Us>
        constexpr bool boolean_value<F, U, Us...> =
            requires{ static_cast<bool>(F<U>::value); } && boolean_value<F, Us...>;

        template <template <typename> class F, typename out, typename...>
        struct filter { using type = out; };
        template <template <typename> class F, typename... out, typename U, typename... Us>
        struct filter<F, pack<out...>, U, Us...> : filter<F, pack<out...>, Us...> {};
        template <template <typename> class F, typename... out, typename U, typename... Us>
            requires (F<U>::value)
        struct filter<F, pack<out...>, U, Us...> : filter<F, pack<out..., U>, Us...> {};

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
        struct to_unique<pack<out...>, T, Ts...> : to_unique<pack<out..., T>, Ts...> {};
        template <typename... out, typename T, typename... Ts>
            requires (::std::same_as<T, out> || ...)
        struct to_unique<pack<out...>, T, Ts...> : to_unique<pack<out...>, Ts...> {};

        template <typename...>
        constexpr bool consolidated = true;
        template <typename T, typename... Ts>
        constexpr bool consolidated<T, Ts...> =
            (!meta::is<T, Ts> && ...) && consolidated<Ts...>;

        template <typename out, typename...>
        struct consolidate { using type = out; };
        template <typename... out, typename U, typename... Us>
        struct consolidate<pack<out...>, U, Us...> : consolidate<pack<out...>, Us...> {};
        template <typename... out, typename U, typename... Us>
            requires ((!meta::is<U, out> && ...) && (!meta::is<U, Us> && ...))
        struct consolidate<pack<out...>, U, Us...> : consolidate<pack<out..., U>, Us...> {};
        template <typename... out, typename U, typename... Us>
        requires ((!meta::is<U, out> && ...) && (meta::is<U, Us> || ...))
        struct consolidate<pack<out...>, U, Us...> :
            consolidate<pack<out..., unqualify<U>>, Us...>
        {};

    }

    /* Return a pack with the merged contents of all constituent packs. */
    template <is_pack... packs>
    using concat = detail::concat<pack<>, unqualify<packs>...>::type;

    template <is_pack... packs>
    using concat_unique = detail::concat_unique<pack<>, unqualify<packs>...>::type;

    /* Append types to a pack. */
    template <is_pack P, typename... Ts>
    using append = concat<P, pack<Ts...>>;

    /* Append types to a pack if they are not already contained within it. */
    template <is_pack P, typename... Ts>
    using append_unique = concat_unique<P, pack<Ts...>>;

    /* Return a pack containing the types in `Ts...`, but in the opposite order. */
    template <typename... Ts>
    using reverse = detail::reverse<pack<>, Ts...>::type;

    /* Return a pack containing `N` consecutive repetitions for each type in
    `Ts...`. */
    template <size_t N, typename... Ts>
    using repeat = detail::repeat<pack<>, N, N, Ts...>::type;

    /* Get a pack of packs containing all unique permutations of the component packs,
    returning their Cartesian product.  */
    template <is_pack... packs>
    using product = detail::product<
        ::std::make_index_sequence<(unqualify<packs>::size() * ... * 1)>,
        unqualify<packs>...
    >::type;

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

        /* True if all of the pack's types are implicitly convertible to the given
        type. */
        template <typename T>
        static constexpr bool convertible_to = (meta::convertible_to<Ts, T> && ...);

        /* True if all of the pack's types are implicitly convertible to the given
        type without throwing exceptions. */
        template <typename T>
        static constexpr bool nothrow_convertible_to = (nothrow::convertible_to<Ts, T> && ...);

        /* True if the pack's types share a common type to which they can all be
        converted. */
        static constexpr bool has_common_type = meta::has_common_type<Ts...>;

        /* Member equivalent for `meta::unpack_type<I, Ts...>` (pack indexing). */
        template <index_type I> requires (impl::valid_index<ssize(), I>)
        using at = meta::unpack_type<I, Ts...>;

        /* Expand the pack, instantiating a template template parameter with its
        contents. */
        template <template <typename...> class F> requires (requires{typename F<Ts...>;})
        using eval = F<Ts...>;

        /* Map a template template parameter over the given arguments, returning a new
        pack containing the transformed result */
        template <template <typename> class F> requires (requires{typename pack<F<Ts>...>;})
        using map = pack<F<Ts>...>;

        /* Get a new pack with one or more types appended after the current contents. */
        template <typename... Us>
        using append = meta::append<pack, Us...>;

        /* Get a new pack with the given types appended after the current contents, but
        only if they are not already contained in the pack. */
        template <typename... Us>
        using append_unique = meta::append_unique<pack, Us...>;

        /* Member equivalent for `meta::concat<pack, packs...>`. */
        template <typename... packs> requires (requires{typename meta::concat<pack, packs...>;})
        using concat = meta::concat<pack, packs...>;

        /* Member equivalent for `meta::concat_unique<pack, packs...>`. */
        template <typename... packs>
            requires (requires{typename meta::concat_unique<pack, packs...>;})
        using concat_unique = meta::concat_unique<pack, packs...>;

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

    template <typename T>
    struct Foo {};

    static_assert(std::same_as<specialization<Foo<double&&>>, meta::pack<double&&>>);

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

        template <typename T, auto min, auto max>
        concept in_range =
            min >= ::std::numeric_limits<T>::min() &&
            max <= ::std::numeric_limits<T>::max();

        template <auto min, auto max>
        struct smallest_signed_int { using type = int64_t; };
        template <auto min, auto max> requires (in_range<int8_t, min, max>)
        struct smallest_signed_int<min, max> { using type = int8_t; };
        template <auto min, auto max>
            requires (!in_range<int8_t, min, max> && in_range<int16_t, min, max>)
        struct smallest_signed_int<min, max> { using type = int16_t; };
        template <auto min, auto max>
            requires (!in_range<int16_t, min, max> && in_range<int32_t, min, max>)
        struct smallest_signed_int<min, max> { using type = int32_t; };
        template <auto min, auto max>
            requires (!in_range<int32_t, min, max> && in_range<int64_t, min, max>)
        struct smallest_signed_int<min, max> { using type = int64_t; };

        template <auto min, auto max>
        struct smallest_unsigned_int;
        template <auto min, auto max>
            requires (in_range<uint8_t, 0, max>)
        struct smallest_unsigned_int<min, max> { using type = uint8_t; };
        template <auto min, auto max>
            requires (!in_range<uint8_t, 0, max> && in_range<uint16_t, 0, max>)
        struct smallest_unsigned_int<min, max> { using type = uint16_t; };
        template <auto min, auto max>
            requires (!in_range<uint16_t, 0, max> && in_range<uint32_t, 0, max>)
        struct smallest_unsigned_int<min, max> { using type = uint32_t; };
        template <auto min, auto max>
            requires (!in_range<uint32_t, 0, max> && in_range<uint64_t, 0, max>)
        struct smallest_unsigned_int<min, max> { using type = uint64_t; };

        template <auto min, auto max>
        struct smallest_int;
        template <auto min, auto max> requires (in_range<int8_t, min, max>)
        struct smallest_int<min, max> { using type = int8_t; };
        template <auto min, auto max>
            requires (!in_range<int8_t, min, max> && in_range<uint8_t, min, max>)
        struct smallest_int<min, max> { using type = uint8_t; };
        template <auto min, auto max>
            requires (
                !in_range<int8_t, min, max> &&
                !in_range<uint8_t, min, max> &&
                in_range<int16_t, min, max>
            )
        struct smallest_int<min, max> { using type = int16_t; };
        template <auto min, auto max>
            requires (
                !in_range<int16_t, min, max> &&
                !in_range<uint8_t, min, max> &&
                in_range<uint16_t, min, max>
            )
        struct smallest_int<min, max> { using type = uint16_t; };
        template <auto min, auto max>
            requires (
                !in_range<int16_t, min, max> &&
                !in_range<uint16_t, min, max> &&
                in_range<int32_t, min, max>
            )
        struct smallest_int<min, max> { using type = int32_t; };
        template <auto min, auto max>
            requires (
                !in_range<int32_t, min, max> &&
                !in_range<uint16_t, min, max> &&
                in_range<uint32_t, min, max>
            )
        struct smallest_int<min, max> { using type = uint32_t; };
        template <auto min, auto max>
            requires (
                !in_range<int32_t, min, max> &&
                !in_range<uint32_t, min, max> &&
                in_range<int64_t, min, max>
            )
        struct smallest_int<min, max> { using type = int64_t; };
        template <auto min, auto max>
            requires (
                !in_range<int64_t, min, max> &&
                !in_range<uint32_t, min, max> &&
                in_range<uint64_t, min, max>
            )
        struct smallest_int<min, max> { using type = uint64_t; };

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

    template <typename T>
    concept has_signed = detail::as_signed<unqualify<T>>::enable;

    template <has_signed T>
    using as_signed = qualify<typename detail::as_signed<unqualify<T>>::type, T>;

    template <has_signed T>
    [[nodiscard]] constexpr decltype(auto) to_signed(T&& value)
        noexcept (signed_integer<T> || requires{
            {as_signed<unqualify<T>>(::std::forward<T>(value))} noexcept;
        })
        requires (signed_integer<T> || requires{
            {as_signed<unqualify<T>>(::std::forward<T>(value))};
        })
    {
        if constexpr (signed_integer<T>) {
            return (::std::forward<T>(value));
        } else {
            return (as_signed<unqualify<T>>(::std::forward<T>(value)));
        }
    }

    template <auto min, auto max> requires (detail::in_range<int64_t, min, max>)
    using smallest_signed_int = detail::smallest_signed_int<min, max>::type;

    template <typename T>
    concept has_unsigned = detail::as_unsigned<unqualify<T>>::enable;

    template <has_unsigned T>
    using as_unsigned = qualify<typename detail::as_unsigned<unqualify<T>>::type, T>;

    template <has_unsigned T>
    [[nodiscard]] constexpr decltype(auto) to_unsigned(T&& value)
        noexcept (unsigned_integer<T> || requires{
            {as_unsigned<unqualify<T>>(::std::forward<T>(value))} noexcept;
        })
        requires (unsigned_integer<T> || requires{
            {as_unsigned<unqualify<T>>(::std::forward<T>(value))};
        })
    {
        if constexpr (unsigned_integer<T>) {
            return (::std::forward<T>(value));
        } else {
            return (as_unsigned<unqualify<T>>(::std::forward<T>(value)));
        }
    }

    template <auto max> requires (detail::in_range<uint64_t, 0, max>)
    using smallest_unsigned_int = detail::smallest_unsigned_int<0, max>::type;

    template <auto min, auto max>
        requires (detail::in_range<int64_t, 0, max> || detail::in_range<uint64_t, 0, max>)
    using smallest_int = detail::smallest_int<min, max>::type;

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
    concept character = 
        is<T, char> ||
        is<T, signed char> ||
        is<T, unsigned char> ||
        is<T, wchar_t> ||
        is<T, char8_t> ||
        is<T, char16_t> ||
        is<T, char32_t>;

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
        constexpr bool is_class = ::std::is_class_v<unqualify<T>>;

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
    concept is_class = detail::is_class<T>;

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

    //////////////////////////
    ////    INVOCATION    ////
    //////////////////////////

    namespace detail {

        template <typename T>
        constexpr bool function = ::std::is_function_v<unqualify<remove_pointer<unqualify<T>>>>;

        template <typename F, typename... A>
        constexpr bool callable = ::std::invocable<F, A...>;

        template <typename F, typename... A>
        constexpr bool nothrow_callable = ::std::is_nothrow_invocable_v<F, A...>;

        template <typename>
        struct call_operator { static constexpr bool exists = false; };
        template <typename R, typename C, typename... A>
        struct call_operator<R(C::*)(A...)> {
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
            static constexpr bool exists = true;
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
    concept function = detail::function<T>;

    template <typename T>
    struct call_operator { static constexpr bool exists = false; };
    template <typename T> requires (requires{{&remove_reference<T>::operator()};})
    struct call_operator<T> : detail::call_operator<decltype(&remove_reference<T>::operator())> {};

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
    concept member_object = detail::member_object<T>;

    template <typename T, typename C>
    concept member_object_of =
        member_object<T> &&
        is_class<C> &&
        detail::member_object_of<unqualify<T>, C>;

    template <typename T>
    concept member_function = detail::member_function<T>;

    template <typename T, typename C>
    concept member_function_of =
        member_function<T> &&
        is_class<C> &&
        detail::member_function_of<unqualify<T>, C>;

    template <typename T>
    concept member = member_object<T> || member_function<T>;

    template <typename T, typename C>
    concept member_of =
        member<T> &&
        is_class<C> &&
        (member_object_of<T, C> || member_function_of<T, C>);

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

    template <typename T, is_class C>
    using as_member = detail::as_member<remove_pointer<remove_reference<T>>, C>::type;

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

        namespace adl {
            using std::get;
            using std::get_if;

            template <typename T, auto... A>
            concept has_get = requires(T t) {{get<A...>(t)};};

            template <typename T, auto... A>
            concept nothrow_has_get =
                has_get<T, A...> && requires(T t) {{get<A...>(t)} noexcept;};

            template <typename T, auto... A>
            concept has_get_if = requires(T t) {{get_if<A...>(t)};};

            template <typename T, auto... A>
            concept nothrow_has_get_if =
                has_get_if<T, A...> && requires(T t) {{get_if<A...>(t)} noexcept;};

        }

        namespace member {

            template <typename T, auto... A>
            concept has_get = requires(T t) {{t.template get<A...>()};};

            template <typename T, auto... A>
            concept nothrow_has_get =
                has_get<T, A...> &&
                requires(T t) {{t.template get<A...>()} noexcept;};

            template <typename T, auto... A>
            concept has_get_if = requires(T t) {{t.template get_if<A...>()};};

            template <typename T, auto... A>
            concept nothrow_has_get_if =
                has_get_if<T, A...> &&
                requires(T t) {{t.template get_if<A...>()} noexcept;};

        }

        template <auto... K>
        struct get_fn {
            template <typename T>
            static constexpr decltype(auto) operator()(T&& t)
                noexcept (member::nothrow_has_get<T, K...>)
                requires (member::has_get<T, K...>)
            {
                return (::std::forward<T>(t).template get<K...>());
            }
            template <typename T>
            static constexpr decltype(auto) operator()(T&& t)
                noexcept (adl::nothrow_has_get<T, K...>)
                requires (!member::has_get<T, K...> && adl::has_get<T, K...>)
            {
                using ::std::get;
                return (get<K...>(::std::forward<T>(t)));
            }
        };

        template <auto... K>
        struct get_if_fn {
            template <typename T>
            static constexpr decltype(auto) operator()(T&& t)
                noexcept (member::nothrow_has_get_if<T, K...>)
                requires (member::has_get_if<T, K...>)
            {
                return (::std::forward<T>(t).template get_if<K...>());
            }
            template <typename T>
            static constexpr decltype(auto) operator()(T&& t)
                noexcept (adl::nothrow_has_get_if<T, K...>)
                requires (!member::has_get_if<T, K...> && adl::has_get_if<T, K...>)
            {
                using ::std::get_if;
                return (get_if<K...>(::std::forward<T>(t)));
            }
        };
    }

    /* Do a generalized `get<K...>(t)` access by first checking for a `t.get<K...>()`
    member method and then falling back to an ADL-enabled `get<K...>(t)` method. */
    template <auto... K>
    constexpr detail::get_fn<K...> get;

    template <typename T, auto... A>
    concept has_get = requires(T t) {{get<A...>(::std::forward<T>(t))};};

    template <typename T, auto... A> requires (has_get<T, A...>)
    using get_type = remove_rvalue<decltype((get<A...>(::std::declval<T>())))>;

    template <typename Ret, typename T, auto... A>
    concept get_returns = has_get<T, A...> && convertible_to<get_type<T, A...>, Ret>;

    /* Do a generalized `get_if<K...>(t)` access by first checking for a
    `t.get_if<K...>()` member method and then falling back to an ADL-enabled
    `get_if<K...>(t)` method. */
    template <auto... K>
    constexpr detail::get_if_fn<K...> get_if;

    template <typename T, auto... A>
    concept has_get_if = requires(T t) {{get_if<A...>(::std::forward<T>(t))};};

    template <typename T, auto... A> requires (has_get_if<T, A...>)
    using get_if_type = remove_rvalue<decltype((get_if<A...>(::std::declval<T>())))>;

    template <typename Ret, typename T, auto... A>
    concept get_if_returns =
        has_get_if<T, A...> && convertible_to<get_if_type<T, A...>, Ret>;

    namespace nothrow {

        template <typename T, auto... A>
        concept has_get = meta::has_get<T, A...> && requires(T t) {
            {get<A...>(::std::forward<T>(t))} noexcept;
        };

        template <typename T, auto... A> requires (nothrow::has_get<T, A...>)
        using get_type = meta::get_type<T, A...>;

        template <typename Ret, typename T, auto... A>
        concept get_returns =
            nothrow::has_get<T, A...> &&
            nothrow::convertible_to<nothrow::get_type<T, A...>, Ret>;

        template <typename T, auto... A>
        concept has_get_if = meta::has_get_if<T, A...> && requires(T t) {
            {get_if<A...>(::std::forward<T>(t))} noexcept;
        };

        template <typename T, auto... A> requires (nothrow::has_get_if<T, A...>)
        using get_if_type = meta::get_if_type<T, A...>;

        template <typename Ret, typename T, auto... A>
        concept get_if_returns =
            nothrow::has_get_if<T, A...> &&
            nothrow::convertible_to<nothrow::get_if_type<T, A...>, Ret>;

    }

    namespace detail {

        template <typename, typename>
        constexpr bool structured = false;
        template <typename T, size_t... Is>
        constexpr bool structured<T, ::std::index_sequence<Is...>> = (meta::has_get<T, Is> && ...);

        template <typename, typename>
        constexpr bool nothrow_structured = false;
        template <typename T, size_t... Is>
        constexpr bool nothrow_structured<T, ::std::index_sequence<Is...>> =
            (meta::nothrow::has_get<T, Is> && ...);

    }

    template <typename T, size_t N>
    concept structured = ::std::tuple_size<unqualify<T>>::value == N;

    template <typename T>
    concept tuple_like = requires{::std::tuple_size<unqualify<T>>::value;};

    template <tuple_like T>
    constexpr size_t tuple_size = ::std::tuple_size<unqualify<T>>::value;

    template <typename Ret, typename T>
    concept tuple_size_returns =
        tuple_like<T> && convertible_to<decltype(meta::tuple_size<T>), Ret>;

    namespace nothrow {

        template <typename T, size_t N>
        concept structured =
            meta::structured<T, N> &&
            detail::nothrow_structured<T, std::make_index_sequence<N>>;

        template <typename T>
        concept tuple_like =
            meta::tuple_like<T> &&
            detail::nothrow_structured<T, std::make_index_sequence<
                ::std::tuple_size<unqualify<T>>::value
            >>;

        template <nothrow::tuple_like T>
        constexpr size_t tuple_size = ::std::tuple_size<unqualify<T>>::value;

        template <typename Ret, typename T>
        concept tuple_size_returns =
            nothrow::tuple_like<T> &&
            nothrow::convertible_to<decltype(nothrow::tuple_size<T>), Ret>;

    }

    namespace detail {

        template <size_t I, meta::tuple_like T, typename... Ts>
        struct tuple_types { using type = pack<Ts...>; };
        template <size_t I, meta::tuple_like T, typename... Ts>
            requires (I < meta::tuple_size<T>)
        struct tuple_types<I, T, Ts...> {
            using type = tuple_types<I + 1, T, Ts..., meta::get_type<T, I>>::type;
        };

        template <typename T, typename, typename...>
        constexpr bool structured_with = false;
        template <typename T, size_t... Is, typename... Ts>
            requires (sizeof...(Is) == sizeof...(Ts))
        constexpr bool structured_with<T, ::std::index_sequence<Is...>, Ts...> =
            (meta::get_returns<Ts, T, Is> && ...);

        template <typename T, typename, typename...>
        constexpr bool nothrow_structured_with = false;
        template <typename T, size_t... Is, typename... Ts>
            requires (sizeof...(Is) == sizeof...(Ts))
        constexpr bool nothrow_structured_with<T, ::std::index_sequence<Is...>, Ts...> =
            (meta::nothrow::get_returns<Ts, T, Is> && ...);

    }

    template <tuple_like T>
    using tuple_types = detail::tuple_types<0, T>::type;

    template <typename T, typename... Ts>
    concept structured_with =
        structured<T, sizeof...(Ts)> &&
        detail::structured_with<T, ::std::index_sequence_for<Ts...>, Ts...>;

    namespace nothrow {

        template <nothrow::tuple_like T>
        using tuple_types = meta::tuple_types<T>;

        template <typename T, typename... Ts>
        concept structured_with =
            nothrow::structured<T, sizeof...(Ts)> &&
            detail::nothrow_structured_with<T, ::std::index_sequence_for<Ts...>, Ts...>;

    }

    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    template <typename T>
    concept iterator = ::std::input_or_output_iterator<unqualify<T>>;

    template <typename T, typename V>
    concept output_iterator = iterator<T> && ::std::output_iterator<unqualify<T>, V>;

    template <typename T>
    concept forward_iterator = iterator<T> && ::std::forward_iterator<unqualify<T>>;

    template <typename T>
    concept bidirectional_iterator =
        forward_iterator<T> && ::std::bidirectional_iterator<unqualify<T>>;

    template <typename T>
    concept random_access_iterator =
        bidirectional_iterator<T> && ::std::random_access_iterator<unqualify<T>>;

    template <typename T>
    concept contiguous_iterator =
        random_access_iterator<T> && ::std::contiguous_iterator<unqualify<T>>;

    template <typename T, typename Iter>
    concept sentinel_for = ::std::sentinel_for<unqualify<T>, Iter>;

    template <typename T, typename Iter>
    concept sized_sentinel_for =
        sentinel_for<T, Iter> && ::std::sized_sentinel_for<unqualify<T>, Iter>;

    template <iterator T>
    using iterator_category = std::conditional_t<
        contiguous_iterator<T>,
        ::std::contiguous_iterator_tag,
        typename ::std::iterator_traits<unqualify<T>>::iterator_category
    >;

    template <iterator T>
    using iterator_difference = ::std::iterator_traits<unqualify<T>>::difference_type;

    template <iterator T>
    using iterator_value = ::std::iterator_traits<unqualify<T>>::value_type;

    template <iterator T>
    using iterator_reference = ::std::iterator_traits<unqualify<T>>::reference;

    template <iterator T>
    using iterator_pointer = ::std::iterator_traits<unqualify<T>>::pointer;

    namespace nothrow {

        template <typename T>
        concept iterator =
            meta::iterator<T> &&
            nothrow::movable<unqualify<T>> &&
            nothrow::assignable<unqualify<T>&, T> &&
            nothrow::swappable<unqualify<T>> &&
            requires(unqualify<T> t) {
                {++t} noexcept;
                {t++} noexcept;
                {*t} noexcept;
            };

        template <typename T, typename V>
        concept output_iterator =
            nothrow::iterator<T> &&
            meta::output_iterator<T, V> &&
            requires(unqualify<T> t, V&& v) {
                {*t = ::std::forward<V>(v)} noexcept;
            };

        template <typename T>
        concept forward_iterator =
            nothrow::iterator<T> &&
            meta::forward_iterator<T> &&
            requires(as_const_ref<T> t) {
                {t == t} noexcept -> nothrow::truthy;
                {t != t} noexcept -> nothrow::truthy;
            };

        template <typename T>
        concept bidirectional_iterator =
            nothrow::forward_iterator<T> &&
            meta::bidirectional_iterator<T> &&
            requires(unqualify<T> t) {
                {--t} noexcept;
                {t--} noexcept;
            };

        template <typename T>
        concept random_access_iterator =
            nothrow::bidirectional_iterator<T> &&
            meta::random_access_iterator<T> &&
            requires(unqualify<T> t, iterator_difference<T> n) {
                {t += n} noexcept;
                {t + n} noexcept;
                {n + t} noexcept;
                {t -= n} noexcept;
                {t - n} noexcept;
                {t[n]} noexcept;
            } && requires(as_const_ref<T> t1, as_const_ref<T> t2) {
                {t1 < t2} noexcept -> nothrow::truthy;
                {t1 > t2} noexcept -> nothrow::truthy;
                {t1 <= t2} noexcept -> nothrow::truthy;
                {t1 >= t2} noexcept -> nothrow::truthy;
            };

        template <typename T>
        concept contiguous_iterator =
            nothrow::random_access_iterator<T> &&
            meta::contiguous_iterator<T>;

        template <typename T, typename Iter>
        concept sentinel_for =
            meta::sentinel_for<T, Iter> &&
            requires(as_const_ref<T> t, as_const_ref<Iter> i) {
                {t == i} noexcept -> nothrow::truthy;
                {i == t} noexcept -> nothrow::truthy;
                {t != i} noexcept -> nothrow::truthy;
                {i != t} noexcept -> nothrow::truthy;
            };

        template <typename T, typename Iter>
        concept sized_sentinel_for =
            nothrow::sentinel_for<T, Iter> &&
            meta::sized_sentinel_for<T, Iter> &&
            requires(as_const_ref<T> t, as_const_ref<Iter> i) {
                {i - t} noexcept -> nothrow::convertible_to<iterator_difference<Iter>>;
                {t - i} noexcept -> nothrow::convertible_to<iterator_difference<Iter>>;
            };

    }

    /* Get a `begin()` iterator for a generic type `T`.  This is identical to
    `std::ranges::begin`. */
    inline constexpr auto begin = ::std::ranges::begin;

    template <typename T>
    concept has_begin = requires(T t) {{begin(t)};};

    template <has_begin T>
    using begin_type = decltype(begin(::std::declval<as_lvalue<T>>()));

    /* Get a `cbegin()` iterator for a generic type `T`.  This is identical to
    `std::ranges::cbegin`. */
    inline constexpr auto cbegin = ::std::ranges::cbegin;

    template <typename T>
    concept has_cbegin = requires(as_lvalue<T> t) {{cbegin(t)};};

    template <has_cbegin T>
    using cbegin_type = decltype(cbegin(::std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_begin = meta::has_begin<T> && requires(T t) {{meta::begin(t)} noexcept;};

        template <nothrow::has_begin T>
        using begin_type = meta::begin_type<T>;

        template <typename T>
        concept has_cbegin = meta::has_cbegin<T> && requires(T t) {{meta::cbegin(t)} noexcept;};

        template <nothrow::has_cbegin T>
        using cbegin_type = meta::cbegin_type<T>;

    }

    /* Get an `end()` iterator for a generic type `T`.  This is identical to
    `std::ranges::end`. */
    inline constexpr auto end = ::std::ranges::end;

    template <typename T>
    concept has_end = requires(T t) {{end(t)};};

    template <has_end T>
    using end_type = decltype(end(::std::declval<as_lvalue<T>>()));

    /* Get a `cend()` iterator for a generic type `T`.  This is identical to
    `std::ranges::cend`. */
    inline constexpr auto cend = ::std::ranges::cend;

    template <typename T>
    concept has_cend = requires(T t) {{cend(t)};};

    template <has_cend T>
    using cend_type = decltype(cend(::std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_end = meta::has_end<T> && requires(T t) {{meta::end(t)} noexcept;};

        template <nothrow::has_end T>
        using end_type = meta::end_type<T>;

        template <typename T>
        concept has_cend = meta::has_cend<T> && requires(T t) {{meta::cend(t)} noexcept;};

        template <nothrow::has_cend T>
        using cend_type = meta::cend_type<T>;

    }

    /* Get an `rbegin()` iterator for a generic type `T`.  This is identical to
    `std::ranges::rbegin`. */
    inline constexpr auto rbegin = ::std::ranges::rbegin;

    template <typename T>
    concept has_rbegin = requires(T t) {{rbegin(t)};};

    template <has_rbegin T>
    using rbegin_type = decltype(rbegin(::std::declval<as_lvalue<T>>()));

    /* Get a `crbegin()` iterator for a generic type `T`.  This is identical to
    `std::ranges::crbegin`. */
    inline constexpr auto crbegin = ::std::ranges::crbegin;

    template <typename T>
    concept has_crbegin = requires(T t) {{crbegin(t)};};

    template <has_crbegin T>
    using crbegin_type = decltype(crbegin(::std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_rbegin = meta::has_rbegin<T> && requires(T t) {{meta::rbegin(t)} noexcept;};

        template <nothrow::has_rbegin T>
        using rbegin_type = meta::rbegin_type<T>;

        template <typename T>
        concept has_crbegin = meta::has_crbegin<T> && requires(T t) {{meta::crbegin(t)} noexcept;};

        template <nothrow::has_crbegin T>
        using crbegin_type = meta::crbegin_type<T>;

    }

    /* Get an `rend()` iterator for a generic type `T`.  This is identical to
    `std::ranges::rend`. */
    inline constexpr auto rend = ::std::ranges::rend;

    template <typename T>
    concept has_rend = requires(T t) {{rend(t)};};

    template <has_rend T>
    using rend_type = decltype(rend(::std::declval<as_lvalue<T>>()));

    /* Get a `crend()` iterator for a generic type `T`.  This is identical to
    `std::ranges::crend`. */
    inline constexpr auto crend = ::std::ranges::crend;

    template <typename T>
    concept has_crend = requires(T t) {{crend(t)};};

    template <has_crend T>
    using crend_type = decltype(crend(::std::declval<as_lvalue<T>>()));

    namespace nothrow {

        template <typename T>
        concept has_rend = meta::has_rend<T> && requires(T t) {{meta::rend(t)} noexcept;};

        template <nothrow::has_rend T>
        using rend_type = meta::rend_type<T>;

        template <typename T>
        concept has_crend = meta::has_crend<T> && requires(T t) {{meta::crend(t)} noexcept;};

        template <nothrow::has_crend T>
        using crend_type = meta::crend_type<T>;

    }

    template <typename T>
    concept iterable = requires(as_lvalue<T> t) {
        {begin(t)} -> iterator;
        {end(t)} -> sentinel_for<begin_type<T>>;
    };

    template <iterable T>
    using yield_type = decltype(*begin(::std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept yields = iterable<T> && convertible_to<yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept iterable =
            meta::iterable<T> &&
            nothrow::has_begin<T> &&
            nothrow::has_end<T> &&
            nothrow::iterator<begin_type<T>> &&
            nothrow::sentinel_for<end_type<T>, begin_type<T>>;

        template <nothrow::iterable T>
        using yield_type = meta::yield_type<T>;

        template <typename T, typename Ret>
        concept yields =
            nothrow::iterable<T> &&
            nothrow::convertible_to<nothrow::yield_type<T>, Ret>;

    }

    template <typename T>
    concept const_iterable = requires(as_lvalue<T> t) {
        {cbegin(t)} -> iterator;
        {cend(t)} -> sentinel_for<cbegin_type<T>>;
    };

    template <const_iterable T>
    using const_yield_type = decltype(*cbegin(::std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept const_yields =
        const_iterable<T> && convertible_to<const_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept const_iterable =
            meta::const_iterable<T> &&
            nothrow::has_cbegin<T> &&
            nothrow::has_cend<T> &&
            nothrow::iterator<cbegin_type<T>> &&
            nothrow::sentinel_for<cend_type<T>, cbegin_type<T>>;

        template <nothrow::const_iterable T>
        using const_yield_type = meta::const_yield_type<T>;

        template <typename T, typename Ret>
        concept const_yields =
            nothrow::const_iterable<T> &&
            nothrow::convertible_to<nothrow::const_yield_type<T>, Ret>;

    }

    template <typename T>
    concept reverse_iterable = requires(as_lvalue<T> t) {
        {rbegin(t)} -> iterator;
        {rend(t)} -> sentinel_for<rbegin_type<T>>;
    };

    template <reverse_iterable T>
    using reverse_yield_type = decltype(*rbegin(::std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept reverse_yields =
        reverse_iterable<T> && convertible_to<reverse_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept reverse_iterable =
            meta::reverse_iterable<T> &&
            nothrow::has_rbegin<T> &&
            nothrow::has_rend<T> &&
            nothrow::iterator<rbegin_type<T>> &&
            nothrow::sentinel_for<rend_type<T>, rbegin_type<T>>;

        template <nothrow::reverse_iterable T>
        using reverse_yield_type = meta::reverse_yield_type<T>;

        template <typename T, typename Ret>
        concept reverse_yields =
            nothrow::reverse_iterable<T> &&
            nothrow::convertible_to<nothrow::reverse_yield_type<T>, Ret>;

    }

    template <typename T>
    concept const_reverse_iterable = requires(as_lvalue<T> t) {
        {crbegin(t)} -> iterator;
        {crend(t)} -> sentinel_for<crbegin_type<T>>;
    };

    template <const_reverse_iterable T>
    using const_reverse_yield_type = decltype(*crbegin(::std::declval<meta::as_lvalue<T>>()));

    template <typename T, typename Ret>
    concept const_reverse_yields =
        const_reverse_iterable<T> && convertible_to<const_reverse_yield_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept const_reverse_iterable =
            meta::const_reverse_iterable<T> &&
            nothrow::has_crbegin<T> &&
            nothrow::has_crend<T> &&
            nothrow::iterator<crbegin_type<T>> &&
            nothrow::sentinel_for<crend_type<T>, crbegin_type<T>>;

        template <nothrow::const_reverse_iterable T>
        using const_reverse_yield_type = meta::const_reverse_yield_type<T>;

        template <typename T, typename Ret>
        concept const_reverse_yields =
            nothrow::const_reverse_iterable<T> &&
            nothrow::convertible_to<nothrow::const_reverse_yield_type<T>, Ret>;

    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    namespace detail {

        template <typename T>
        constexpr bool wraparound = false;

    }

    template <typename T>
    concept wraparound = detail::wraparound<T>;

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

    template <typename T, typename Char = char>
    concept formattable = ::std::formattable<T, Char>;

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

    template <typename T>
    constexpr auto to_arrow(T&& value)
        noexcept (requires{{::std::to_address(::std::forward<T>(value))} noexcept;} || (
            !requires{{::std::to_address(::std::forward<T>(value))};} &&
            requires{{::std::addressof(::std::forward<T>(value))} noexcept;}
        ))
        requires (
            requires{{::std::to_address(::std::forward<T>(value))};} ||
            requires{{::std::addressof(::std::forward<T>(value))};}
        )
    {
        if constexpr (requires{{::std::to_address(::std::forward<T>(value))};}) {
            return ::std::to_address(::std::forward<T>(value));
        } else {
            return ::std::addressof(::std::forward<T>(value));
        }
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
    using logical_or_type = decltype((::std::declval<L>() || ::std::declval<R>()));

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

    /* Retrieve the size of a generic type `T` as an unsigned integer.  This is
    expression-equivalent to `std::ranges::size()`. */
    inline constexpr auto size = std::ranges::size;

    template <typename T>
    concept has_size = requires(T t) {{size(t)} -> unsigned_integer;};

    template <has_size T>
    using size_type = remove_rvalue<decltype(size(::std::declval<T>()))>;

    template <typename Ret, typename T>
    concept size_returns = has_size<T> && convertible_to<size_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_size = meta::has_size<T> && requires(T t) {
            {meta::size(t)} noexcept -> meta::unsigned_integer;
        };

        template <nothrow::has_size T>
        using size_type = meta::size_type<T>;

        template <typename Ret, typename T>
        concept size_returns =
            nothrow::has_size<T> &&
            nothrow::convertible_to<nothrow::size_type<T>, Ret>;

    }

    namespace detail {

        namespace adl {
            using ::std::ssize;

            template <typename T>
            concept has_ssize = requires(T t) {
                {ssize(::std::forward<T>(t))} -> meta::signed_integer;
            };

            template <typename T>
            concept nothrow_ssize = requires(T t) {
                {ssize(::std::forward<T>(t))} noexcept -> meta::signed_integer;
            };

        }

        namespace member {

            template <typename T>
            concept has_ssize = requires(T t) {
                {::std::forward<T>(t).ssize()} -> meta::signed_integer;
            };

            template <typename T>
            concept nothrow_ssize = requires(T t) {
                {::std::forward<T>(t).ssize()} noexcept -> meta::signed_integer;
            };

        }

        struct ssize_fn {
            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (member::nothrow_ssize<T>)
                requires (member::has_ssize<T>)
            {
                return (::std::forward<T>(t).ssize());
            }

            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (adl::nothrow_ssize<T>)
                requires (!member::has_ssize<T> && adl::has_ssize<T>)
            {
                using ::std::ssize;
                return (ssize(::std::forward<T>(t)));
            }

            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{
                    {::std::ranges::ssize(::std::forward<T>(t))} noexcept -> meta::signed_integer;
                })
                requires (
                    !member::has_ssize<T> &&
                    !adl::has_ssize<T> &&
                    requires{{::std::ranges::ssize(::std::forward<T>(t))} -> meta::signed_integer;}
                )
            {
                return (::std::ranges::ssize(::std::forward<T>(t)));
            }
        };

    }

    /* Retrieve the size of a generic type `T` as a signed integer.  This is
    expression-equivalent to `std::ranges::ssize()`, except that it first checks
    for an `obj.ssize()` member method or `ssize(obj)` ADL method before invoking
    `std::ranges::ssize()`, which converts the result of `meta::size()` to a signed
    integer. */
    inline constexpr detail::ssize_fn ssize;

    template <typename T>
    concept has_ssize = requires(T t) {{ssize(t)} -> signed_integer;};

    template <has_ssize T>
    using ssize_type = remove_rvalue<decltype((ssize(::std::declval<T>())))>;

    template <typename Ret, typename T>
    concept ssize_returns = has_ssize<T> && convertible_to<ssize_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_ssize = meta::has_ssize<T> && requires(T t) {
            {meta::ssize(t)} noexcept -> meta::signed_integer;
        };

        template <nothrow::has_ssize T>
        using ssize_type = meta::ssize_type<T>;

        template <typename Ret, typename T>
        concept ssize_returns =
            nothrow::has_ssize<T> &&
            nothrow::convertible_to<nothrow::ssize_type<T>, Ret>;

    }

    namespace detail {

        struct distance_fn {
            template <typename... A>
            [[nodiscard]] static constexpr decltype(auto) operator()(A&&... a)
                noexcept (requires{{meta::ssize(::std::forward<A>(a)...)} noexcept;})
                requires (requires{{meta::ssize(::std::forward<A>(a)...)};})
            {
                return (meta::ssize(::std::forward<A>(a)...));
            }
            template <typename... A>
            [[nodiscard]] static constexpr decltype(auto) operator()(A&&... a)
                noexcept (requires{{std::ranges::distance(::std::forward<A>(a)...)} noexcept;})
                requires (
                    !requires{{meta::ssize(::std::forward<A>(a)...)};} &&
                    requires{{std::ranges::distance(::std::forward<A>(a)...)};}
                )
            {
                return (std::ranges::distance(::std::forward<A>(a)...));
            }
        };

    }

    /* Retrieve the distance between two iterators or the overall size of an iterable
    container, possibly in linear time.  This is expression-equivalent to
    `std::ranges::distance()`, except that it first checks for an `obj.ssize()` member
    method or `ssize(obj)` ADL method before invoking `std::ranges::distance()`, which
    may use `std::ranges::size()` if the container supports it.  If not, then the
    distance calculation may require a loop over the container. */
    inline constexpr detail::distance_fn distance;

    template <typename... A>
    concept has_distance = requires(A... a) {{distance(::std::forward<A>(a)...)};};

    template <has_distance... A>
    using distance_type = remove_rvalue<decltype((distance(::std::declval<A>()...)))>;

    template <typename Ret, typename... A>
    concept distance_returns = has_distance<A...> && convertible_to<distance_type<A...>, Ret>;

    namespace nothrow {

        template <typename... A>
        concept has_distance = meta::has_distance<A...> && requires(A... a) {
            {meta::distance(::std::forward<A>(a)...)} noexcept;
        };

        template <nothrow::has_distance... A>
        using distance_type = meta::distance_type<A...>;

        template <typename Ret, typename... A>
        concept distance_returns =
            nothrow::has_distance<A...> &&
            nothrow::convertible_to<nothrow::distance_type<A...>, Ret>;

    }

    /* Check whether a generic type `T` contains zero elements.  This is
    expression-equivalent to `std::ranges::empty()`. */
    inline constexpr auto empty = ::std::ranges::empty;

    template <typename T>
    concept has_empty = requires(T t) {{empty(t)};};

    namespace nothrow {

        template <typename T>
        concept has_empty =
            meta::has_empty<T> && requires(T t) {
                {meta::empty(t)} noexcept -> nothrow::convertible_to<bool>;
            };

    }

    namespace detail {

        namespace adl {

            template <typename T>
            concept has_capacity = requires(T t) {
                {capacity(::std::forward<T>(t))};
            };

        }

        namespace member {

            template <typename T>
            concept has_capacity = requires(T t) {
                {::std::forward<T>(t).capacity()};
            };

        }

        struct capacity_fn {
            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{::std::forward<T>(t).capacity()} noexcept;})
                requires (member::has_capacity<T>)
            {
                return (::std::forward<T>(t).capacity());
            }

            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{capacity(::std::forward<T>(t))} noexcept;})
                requires (!member::has_capacity<T> && adl::has_capacity<T>)
            {
                return (capacity(::std::forward<T>(t)));
            }
        };

    }

    /* Retrieve the capacity of a generic container type `T`, preferring the following:
    
        1.  an `obj.capacity()` member method.
        2.  a `capacity(obj)` ADL function.

    In both cases, the result will be perfectly-forwarded. */
    inline constexpr detail::capacity_fn capacity;

    template <typename T>
    concept has_capacity = requires(T t) {{capacity(t)};};

    template <has_capacity T>
    using capacity_type = remove_rvalue<decltype((capacity(::std::declval<T>())))>;

    template <typename Ret, typename T>
    concept capacity_returns = has_capacity<T> && convertible_to<capacity_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_capacity =
            meta::has_capacity<T> && requires(T t) {{meta::capacity(t)} noexcept;};

        template <nothrow::has_capacity T>
        using capacity_type = meta::capacity_type<T>;

        template <typename Ret, typename T>
        concept capacity_returns =
            nothrow::has_capacity<T> &&
            nothrow::convertible_to<nothrow::capacity_type<T>, Ret>;

    }

    namespace detail {

        namespace adl {

            template <typename T>
            concept has_reserve = requires(T t, size_t n) {
                {reserve(::std::forward<T>(t), n)};
            };

        }

        namespace member {

            template <typename T>
            concept has_reserve = requires(T t, size_t n) {
                {::std::forward<T>(t).reserve(n)};
            };

        }

        struct reserve_fn {
            template <typename T>
            static constexpr void operator()(T&& t, size_t n)
                noexcept (requires{{::std::forward<T>(t).reserve(n)} noexcept;})
                requires (member::has_reserve<T>)
            {
                ::std::forward<T>(t).reserve(n);
            }

            template <typename T>
            static constexpr void operator()(T&& t, size_t n)
                noexcept (requires{{reserve(::std::forward<T>(t), n)} noexcept;})
                requires (!member::has_reserve<T> && adl::has_reserve<T>)
            {
                reserve(::std::forward<T>(t), n);
            }
        };

    }

    /* Reserve space for at least `n` elements in a generic container type `T`, preferring
    the following:

        1.  an `obj.reserve(n)` member method.
        2.  a `reserve(obj, n)` ADL function.
    */
    inline constexpr detail::reserve_fn reserve;

    template <typename T>
    concept has_reserve = requires(T t, size_t n) {{reserve(t, n)};};

    template <has_reserve T>
    using reserve_type =
        remove_rvalue<decltype((reserve(::std::declval<T>(), ::std::declval<size_t>())))>;

    template <typename Ret, typename T>
    concept reserve_returns = has_reserve<T> && convertible_to<reserve_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_reserve =
            meta::has_reserve<T> && requires(T t, size_t n) {{meta::reserve(t, n)} noexcept;};

        template <nothrow::has_reserve T>
        using reserve_type = meta::reserve_type<T>;

        template <typename Ret, typename T>
        concept reserve_returns =
            nothrow::has_reserve<T> &&
            nothrow::convertible_to<nothrow::reserve_type<T>, Ret>;

    }


    namespace detail {

        namespace adl {

            template <typename T>
            concept has_front = requires(T t) {
                {front(::std::forward<T>(t))};
            };

        }

        namespace member {

            template <typename T>
            concept has_front = requires(T t) {
                {::std::forward<T>(t).front()};
            };

        }

        struct front_fn {
            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{::std::forward<T>(t).front()} noexcept;})
                requires (
                    !meta::structured<T, 0> &&
                    member::has_front<T>
                )
            {
                return (::std::forward<T>(t).front());
            }

            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{front(::std::forward<T>(t))} noexcept;})
                requires (
                    !meta::structured<T, 0> &&
                    !member::has_front<T> &&
                    adl::has_front<T>
                )
            {
                return (front(::std::forward<T>(t)));
            }

            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{*meta::begin(t)} noexcept;})
                requires (
                    !meta::structured<T, 0> &&
                    !member::has_front<T> &&
                    !adl::has_front<T> &&
                    meta::iterable<T>
                )
            {
                return (*meta::begin(t));
            }
        };

    }

    /* Retrieve the first element in a generic type `T`, preferring the following:

        1.  an `obj.front()` member method.
        2.  a `front(obj)` ADL function.
        3.  dereferencing the result of `meta::begin(obj)`.

    In all cases, the result will be perfectly-forwarded.  Note that no extra bounds
    checking is performed, meaning that the behavior is undefined if `obj` is empty.
    Cases (1) and (2) are free to implement whatever bounds checking they need on a
    container-specific basis. */
    inline constexpr detail::front_fn front;

    template <typename T>
    concept has_front = requires(T t) {{front(t)};};

    template <has_front T>
    using front_type = remove_rvalue<decltype((front(::std::declval<T>())))>;

    template <typename Ret, typename T>
    concept front_returns = has_front<T> && convertible_to<front_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_front = meta::has_front<T> && requires(T t) {{meta::front(t)} noexcept;};

        template <nothrow::has_front T>
        using front_type = meta::front_type<T>;

        template <typename Ret, typename T>
        concept front_returns =
            nothrow::has_front<T> && nothrow::convertible_to<nothrow::front_type<T>, Ret>;

    }

    namespace detail {

        namespace adl {

            template <typename T>
            concept has_back = requires(T t) {
                {back(::std::forward<T>(t))};
            };

        }

        namespace member {

            template <typename T>
            concept has_back = requires(T t) {
                {::std::forward<T>(t).back()};
            };

        }

        struct back_fn {
            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{::std::forward<T>(t).back()} noexcept;})
                requires (
                    !meta::structured<T, 0> &&
                    member::has_back<T>
                )
            {
                return (::std::forward<T>(t).back());
            }

            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{back(::std::forward<T>(t))} noexcept;})
                requires (
                    !meta::structured<T, 0> &&
                    !member::has_back<T> &&
                    adl::has_back<T>
                )
            {
                return (back(::std::forward<T>(t)));
            }

            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{*meta::rbegin(t)} noexcept;})
                requires (
                    !meta::structured<T, 0> &&
                    !member::has_back<T> &&
                    !adl::has_back<T> &&
                    meta::reverse_iterable<T>
                )
            {
                return (*meta::rbegin(t));
            }

            template <typename T>
            [[nodiscard]] static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires(meta::begin_type<T> it) {
                    {meta::begin(t)} noexcept;
                    {it += meta::ssize(t) - 1} noexcept;
                    {*it} noexcept;
                })
                requires (
                    !meta::structured<T, 0> &&
                    !member::has_back<T> &&
                    !adl::has_back<T> &&
                    !meta::reverse_iterable<T> &&
                    requires(meta::begin_type<T> it) { {it += meta::ssize(t) - 1};}
                )
            {
                auto it = meta::begin(t);
                it += meta::ssize(t) - 1;
                return (*it);
            }
        };

    }

    /* Retrieve the last element in a generic type `T`, preferring the following:
    
        1.  an `obj.back()` member method.
        2.  a `back(obj)` ADL function.
        3.  dereferencing the result of `meta::rbegin(obj)`.
        4.  advancing the `meta::begin(obj)` iterator by `meta::ssize(obj) - 1` using
            `operator+=` (random access).

    In all cases, the result will be perfectly-forwarded.  Note that no extra bounds
    checking is performed, meaning that the behavior is undefined if `obj` is empty.
    Cases (1) and (2) are free to implement whatever bounds checking they need on a
    container-specific basis. */
    inline constexpr detail::back_fn back;

    template <typename T>
    concept has_back = requires(T t) {{back(t)};};

    template <has_back T>
    using back_type = remove_rvalue<decltype((back(::std::declval<T>())))>;

    template <typename Ret, typename T>
    concept back_returns = has_back<T> && convertible_to<back_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_back = meta::has_back<T> && requires(T t) {{meta::back(t)} noexcept;};

        template <nothrow::has_back T>
        using back_type = meta::back_type<T>;

        template <typename Ret, typename T>
        concept back_returns =
            nothrow::has_back<T> && nothrow::convertible_to<nothrow::back_type<T>, Ret>;

    }

    /* Retrieve a pointer to the underlying data buffer for a contiguous range.  This
    is expression-equivalent to `std::ranges::data()`. */
    inline constexpr auto data = ::std::ranges::data;

    /* Retrieve a const pointer to the underlying data buffer for a contiguous range.
    This is expression-equivalent to `std::ranges::cdata()`. */
    inline constexpr auto cdata = ::std::ranges::cdata;

    template <typename T>
    concept has_data = requires(as_lvalue<T> t) {{data(t)} -> pointer;};

    template <has_data T>
    using data_type = decltype(data(::std::declval<as_lvalue<T>>()));

    template <typename Ret, typename T>
    concept data_returns = has_data<T> && convertible_to<data_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_data = meta::has_data<T> && requires(T t) {
            {meta::data(t)} noexcept -> meta::pointer;
        };

        template <nothrow::has_data T>
        using data_type = meta::data_type<T>;

        template <typename Ret, typename T>
        concept data_returns =
            nothrow::has_data<T> &&
            nothrow::convertible_to<nothrow::data_type<T>, Ret>;

    }

    namespace detail {

        struct hash_fn {
            template <typename T>
            [[nodiscard]] static constexpr size_t operator()(const T& t)
                noexcept (requires{{::std::hash<::std::decay_t<T>>{}(t)} noexcept;})
                requires (requires{{::std::hash<::std::decay_t<T>>{}(t)};})
            {
                return (::std::hash<::std::decay_t<T>>{}(t));
            }
        };

    }

    /* Hash a generic object of type `T`.  This is expression-equivalent to a
    `std::hash<std::decay_t<T>>{}()` call, but without requiring the user to manually
    specialize `std::hash`.  The return type is always `size_t`. */
    inline constexpr detail::hash_fn hash;

    template <typename T>
    concept hashable = requires(T t) {{hash(t)} -> unsigned_integer;};

    template <hashable T>
    using hash_type = remove_rvalue<decltype((hash(::std::declval<T>())))>;

    template <typename Ret, typename T>
    concept hash_returns = hashable<T> && convertible_to<hash_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept hashable = meta::hashable<T> && requires(T t) {{meta::hash(t)} noexcept;};

        template <nothrow::hashable T>
        using hash_type = meta::hash_type<T>;

        template <typename Ret, typename T>
        concept hash_returns =
            nothrow::hashable<T> && nothrow::convertible_to<nothrow::hash_type<T>, Ret>;

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


    /// TODO: not really sure if any of these concepts are useful over and above
    /// simple `requires` clauses where applicable.

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
            constexpr bool type_identity = false;
            template <typename T>
            constexpr bool type_identity<::std::type_identity<T>> = true;

        }

        template <typename T>
        concept type_identity = detail::type_identity<unqualify<T>>;

        template <type_identity T>
        using type_identity_type = unqualify<T>::type;

        namespace detail {

            template <typename T>
            constexpr bool in_place_index = false;
            template <size_t I>
            constexpr bool in_place_index<::std::in_place_index_t<I>> = true;

            template <typename T>
            constexpr size_t in_place_index_value = 0;
            template <size_t I>
            constexpr size_t in_place_index_value<::std::in_place_index_t<I>> = I;

        }

        template <typename T>
        concept in_place_index = detail::in_place_index<unqualify<T>>;

        template <in_place_index T>
        constexpr size_t in_place_index_value = detail::in_place_index_value<unqualify<T>>;

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

    namespace detail {

        /// NOTE: in all cases, cvref qualifiers will be stripped from the input
        /// types before checking against these customization points.

        /* Disables certain implicit conversion operators to the indicated type,
        reducing the candidate overloads to just the type's normal constructors.  This
        can prevent ambiguities in overload resolution if the type has multiple
        conversion paths.  The conversion operator must check this concept in its
        constraints. */
        template <typename T>
        constexpr bool prefer_constructor = meta::inherits<T, impl::prefer_constructor_tag>;
        template <typename T, auto Extent>
        constexpr bool prefer_constructor<::std::span<T, Extent>> = true;
        template <typename... Ts>
        constexpr bool prefer_constructor<::std::mdspan<Ts...>> = true;

        /* Enables the `*` unpacking operator for iterable container types. */
        template <meta::iterable T>
        constexpr bool enable_unpack = false;

        /* Enables the `->*` comprehension operator for iterable container types. */
        template <meta::iterable T>
        constexpr bool enable_comprehension = false;

        /// TODO: `meta::detail::enable_wraparound`?

    }

    template <typename T>
    concept prefer_constructor = detail::prefer_constructor<unqualify<T>>;

    template <typename T>
    concept enable_unpack = detail::enable_unpack<unqualify<T>>;

    template <typename T>
    concept enable_comprehension = detail::enable_comprehension<unqualify<T>>;

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
struct NoneType : impl::prefer_constructor_tag {
    [[nodiscard]] constexpr NoneType() = default;
    [[nodiscard]] constexpr NoneType(std::nullopt_t) noexcept {}
    [[nodiscard]] constexpr NoneType(std::nullptr_t) noexcept {}

    [[nodiscard]] constexpr bool operator==(NoneType) const noexcept { return true; }
    [[nodiscard]] constexpr bool operator!=(NoneType) const noexcept { return false; }

    template <typename T>
    [[nodiscard]] constexpr operator T() const
        noexcept (meta::nothrow::convertible_to<const std::nullopt_t&, T>)
        requires (!meta::prefer_constructor<T> && meta::convertible_to<const std::nullopt_t&, T>)
    {
        return std::nullopt;
    }

    template <typename T>
    [[nodiscard]] explicit constexpr operator T() const
        noexcept (meta::nothrow::explicitly_convertible_to<const std::nullopt_t&, T>)
        requires (
            !meta::prefer_constructor<T> &&
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
            !meta::prefer_constructor<T> &&
            !meta::explicitly_convertible_to<const std::nullopt_t&, T> &&
            meta::explicitly_convertible_to<std::nullptr_t, T>
        )
    {
        return static_cast<T>(nullptr);
    }
};


/* The global `None` singleton, representing the absence of a value. */
inline constexpr NoneType None;


/* A generalized `swap()` operator that allows any type in the `bertrand::` namespace
that exposes a `.swap()` member method to be used in conjunction with
`std::ranges::swap()`. */
template <typename T>
constexpr void swap(T& lhs, T& rhs)
    noexcept (requires{{lhs.swap(rhs)} noexcept;})
    requires (requires{{lhs.swap(rhs)};})
{
    lhs.swap(rhs);
}


namespace impl {

    /* A generalized `swap()` operator that allows any type in the `bertrand::impl`
    namespace that exposes a `.swap()` member method to be used in conjunction with
    `std::ranges::swap()`. */
    template <typename T>
    constexpr void swap(T& lhs, T& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }

    /* A trivial wrapper for an arbitrarily-qualified type that strips rvalue
    references (taking ownership over the referenced object) and preserves lvalues
    (converting them into pointers that rebind on assignment, without affecting the
    referenced object).  Dereferencing the `ref` object will perfectly forward the
    value according to both its original cvref qualifications as well as those of the
    `ref` object itself.  Containers can use this as an elementwise storage type to
    allow storage of arbitrarily-qualified types with perfect forwarding. */
    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct ref {
        using type = meta::remove_rvalue<T>;
        [[no_unique_address]] type data;

        constexpr void swap(ref& other)
            noexcept (requires{{std::ranges::swap(data, other.data)} noexcept;})
            requires (requires{{std::ranges::swap(data, other.data)};})
        {
            std::ranges::swap(data, other.data);
        }

        template <typename Self>
        constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (std::forward<Self>(self).data);
        }

        template <typename Self>
        constexpr auto operator->(this Self&& self)
            noexcept (requires{{std::addressof(self.data)} noexcept;})
            requires (requires{{std::addressof(self.data)};})
        {
            return std::addressof(self.data);
        }
    };
    template <meta::not_void T> requires (meta::not_rvalue<T> && meta::lvalue<T>)
    struct ref<T> {
        using type = T;
        meta::as_pointer<T> data = nullptr;

        [[nodiscard]] constexpr ref() noexcept = default;
        [[nodiscard]] constexpr ref(T data) noexcept : data(std::addressof(data)) {}

        constexpr void swap(ref& other) noexcept {
            meta::swap(data, other.data);
        }

        constexpr T operator*() noexcept {
            return *data;
        }

        constexpr T operator*() volatile noexcept {
            return *static_cast<meta::as_pointer<meta::as_volatile<T>>>(data);
        }

        constexpr meta::as_const<T> operator*() const noexcept {
            return *static_cast<meta::as_pointer<meta::as_const<T>>>(data);
        }

        constexpr meta::as_const<T> operator*() const volatile noexcept {
            return *static_cast<meta::as_pointer<meta::as_const<meta::as_volatile<T>>>>(data);
        }

        constexpr auto operator->() noexcept {
            return data;
        }

        constexpr auto operator->() volatile noexcept {
            return static_cast<meta::as_pointer<meta::as_volatile<T>>>(data);
        }

        constexpr auto operator->() const noexcept {
            return static_cast<meta::as_pointer<meta::as_const<T>>>(data);
        }

        constexpr auto operator->() const volatile noexcept {
            return static_cast<meta::as_pointer<meta::as_const<meta::as_volatile<T>>>>(data);
        }
    };

    template <typename T>
    ref(T&&) -> ref<meta::remove_rvalue<T>>;

    /* An extension of `ref<T>` that recursively forwards the `->` operator if
    possible.  This helps when defining iterators and other objects that wish to expose
    the `->` operator, but may dereference to a temporary result, which would otherwise
    encounter lifetime issues.  Because the built-in `operator->` recursively invokes
    itself until a non pointer-like type is encountered, this proxy will effectively
    guarantee that the address of the temporary remains valid for the full duration of
    the `->` expression, and then be released immediately afterwards. */
    template <meta::not_void T>
    struct arrow : ref<T> {
        constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(**this)} noexcept;})
            requires (requires{{meta::to_arrow(**this)};})
        {
            return meta::to_arrow(**this);
        }
        constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(**this)} noexcept;})
            requires (requires{{meta::to_arrow(**this)};})
        {
            return meta::to_arrow(**this);
        }
    };

    template <typename T>
    arrow(T&&) -> arrow<T>;

    /* A generic overload set implemented using recursive inheritance.  This allows the
    overload set to accept functions by reference, as well as index into them as if
    they were a tuple.  Calling the overload set will invoke the appropriate function
    just like any other overloaded function call.

    This class is also a convenient, minimal implementation of a raw tuple, which can
    be used as a building block for more complex data structures that potentially
    need to store multiple values of different types, some of which may be lvalues,
    which should be stored by reference rather than by value. */
    template <meta::not_rvalue...>
    struct basic_tuple : impl::basic_tuple_tag {
        [[nodiscard]] constexpr basic_tuple() noexcept {};
        [[nodiscard]] static constexpr size_t size() noexcept { return 0; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }
        [[nodiscard]] static constexpr bool empty() noexcept { return true; }
        constexpr void swap(basic_tuple&) noexcept {}
        template <size_t I, typename Self> requires (false)  // never actually called
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept;
    };
    template <meta::not_rvalue T, meta::not_rvalue... Ts>
    struct basic_tuple<T, Ts...> : basic_tuple<Ts...> {
        [[no_unique_address]] ref<T> data;

        [[nodiscard]] static constexpr size_t size() noexcept { return sizeof...(Ts) + 1; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return sizeof...(Ts) + 1; }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

        [[nodiscard]] constexpr basic_tuple() = default;
        [[nodiscard]] constexpr basic_tuple(meta::forward<T> first, meta::forward<Ts>... rest)
            noexcept (
                meta::nothrow::convertible_to<meta::forward<T>, T> &&
                meta::nothrow::constructible_from<basic_tuple<Ts...>, Ts...>
            )
            requires (
                meta::convertible_to<meta::forward<T>, T> &&
                meta::constructible_from<basic_tuple<Ts...>, Ts...>
            )
        :
            basic_tuple<Ts...>(std::forward<Ts>(rest)...),
            data(std::forward<T>(first))
        {}

        constexpr void swap(basic_tuple& other)
            noexcept (
                (meta::lvalue<T> || meta::nothrow::swappable<T>) &&
                meta::nothrow::swappable<basic_tuple<Ts...>>
            )
            requires (
                (meta::lvalue<T> || meta::swappable<T>) &&
                meta::swappable<basic_tuple<Ts...>>
            )
        {
            basic_tuple<Ts...>::swap(other);
            data.swap(other.data);
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {(*std::forward<Self>(self).data)(std::forward<A>(args)...)} noexcept;
            })
            requires (requires{{(*std::forward<Self>(self).data)(std::forward<A>(args)...)};})
        {
            return ((*std::forward<Self>(self).data)(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<basic_tuple<Ts...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{(*std::forward<Self>(self).data)(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<basic_tuple<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<basic_tuple<Ts...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }

        template <size_t I, typename Self> requires (I < size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (*std::forward<Self>(self).data);
            } else {
                using base = meta::qualify<basic_tuple<Ts...>, Self>;
                return (std::forward<base>(self).template get<I - 1>());
            }
        }
    };

    template <typename... Fs>
    basic_tuple(Fs&&...) -> basic_tuple<meta::remove_rvalue<Fs>...>;

    /* A helper class that generates a compile-time vtable for a visitor function `F`,
    which must be a template class that accepts a single non-type template parameter
    chosen from the enumerated options via a runtime index.  This effectively promotes
    the index to compile time, and allows the inner function to specialize accordingly,
    which serves as the basis for most forms of type erasure, polymorphism, and runtime
    dispatch.

    Note that the vtable will only be generated when the helper object is invoked, and
    will therefore be unique for each signature.  If the total vtable size (i.e. the
    number of non-type template parameters provided to this class) is less than
    `MIN_VTABLE_SIZE`, then the vtable will be replaced with a recursive `if`/`else`
    chain instead, which promotes inlining optimizations and reduces binary size. */
    template <template <auto> typename F, auto... Ts>
        requires (meta::default_constructible<F<Ts>> && ...)
    struct vtable {
        size_t index;

        static constexpr size_t size() noexcept { return 0; }
        static constexpr ssize_t ssize() noexcept { return 0; }
        static constexpr bool empty() noexcept { return true; }
        constexpr void swap(vtable& other) noexcept { std::ranges::swap(index, other.index); }
    };
    template <template <auto> typename F, auto T, auto... Ts>
        requires (meta::default_constructible<F<T>> && ... && meta::default_constructible<F<Ts>>)
    struct vtable<F, T, Ts...> {
        size_t index;

        static constexpr size_t size() noexcept { return sizeof...(Ts) + 1; }
        static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        static constexpr bool empty() noexcept { return false; }
        constexpr void swap(vtable& other) noexcept { std::ranges::swap(index, other.index); }

    private:
        template <typename... A>
        static constexpr bool nothrow = (
            meta::nothrow::callable<F<T>, A...> &&
            ... &&
            meta::nothrow::callable<F<Ts>, A...>
        );

        template <typename... A>
        using type = meta::call_type<F<T>, A...>;

        template <typename... A>
        using ptr = type<A...>(*)(A...) noexcept (nothrow<A...>);

        template <size_t I, typename... A>
        static constexpr type<A...> fn(A... args) noexcept (nothrow<A...>) {
            return F<meta::unpack_value<I, T, Ts...>>{}(std::forward<A>(args)...);
        }

        template <typename, typename... A>
        static constexpr ptr<A...> table[size()];
        template <size_t... Is, typename... A>
        static constexpr ptr<A...> table<std::index_sequence<Is...>, A...>[size()] {
            &fn<Is, A...>...
        };

    public:
        /* If the vtable size is less than `MIN_VTABLE_SIZE`, then we can optimize
        the dispatch to a recursive `if` chain, which is easier for the compiler to
        optimize. */
        template <size_t I = 0, typename... A>
        [[gnu::always_inline]] constexpr type<A...> operator()(A&&... args) const
            noexcept (nothrow<A...>)
            requires (
                size() < MIN_VTABLE_SIZE &&
                (meta::callable<F<T>, A...> && ... && meta::callable<F<Ts>, A...>) &&
                (std::same_as<meta::call_type<F<T>, A...>, meta::call_type<F<Ts>, A...>> && ...)
            )
        {
            if constexpr (I + 1 == size()) {
                return F<meta::unpack_value<I, T, Ts...>>{}(std::forward<A>(args)...);
            } else {
                if (index == I) {
                    return F<meta::unpack_value<I, T, Ts...>>{}(std::forward<A>(args)...);
                } else {
                    return operator()<I + 1>(std::forward<A>(args)...);
                }
            }
        }

        /* Otherwise, a normal vtable will be emitted and stored in the binary. */
        template <typename... A>
        [[gnu::always_inline]] constexpr type<A...> operator()(A&&... args) const
            noexcept (nothrow<A...>)
            requires (
                size() >= MIN_VTABLE_SIZE &&
                (meta::callable<F<T>, A...> && ... && meta::callable<F<Ts>, A...>) &&
                (std::same_as<meta::call_type<F<T>, A...>, meta::call_type<F<Ts>, A...>> && ...)
            )
        {
            return table<std::make_index_sequence<size()>, meta::forward<A>...>[index](
                std::forward<A>(args)...
            );
        }
    };

    template <template <size_t> typename F, typename>
    struct _basic_vtable;
    template <template <size_t> typename F, size_t... Is>
    struct _basic_vtable<F, std::index_sequence<Is...>> { using type = vtable<F, Is...>; };

    /* Produce a simple vtable with purely integer indices and a specified size. */
    template <template <size_t> typename F, size_t N>
    using basic_vtable = _basic_vtable<F, std::make_index_sequence<N>>::type;

    /* A trivial iterator that applies a transformation function to the elements of
    another iterator.  The increment/decrement and comparison operators are
    unchanged. */
    template <meta::iterator Iter, typename F> requires (requires(Iter it, F func) {{func(*it)};})
    struct transform_iterator {
        using iterator_category = std::conditional_t<
            meta::inherits<meta::iterator_category<Iter>, std::contiguous_iterator_tag>,
            std::random_access_iterator_tag,
            meta::iterator_category<Iter>
        >;
        using difference_type = meta::iterator_difference<Iter>;
        using value_type = meta::remove_reference<meta::call_type<
            meta::as_lvalue<F>,
            meta::dereference_type<Iter>
        >>;
        using reference = value_type;
        using pointer = void;

        [[no_unique_address]] Iter iter;
        [[no_unique_address]] F func;

        [[nodiscard]] constexpr value_type operator*() const
            noexcept (requires{{func(*iter)} noexcept;})
            requires (requires{{func(*iter)};})
        {
            return func(*iter);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{func(*iter)}} noexcept;})
            requires (requires{{impl::arrow{func(*iter)}};})
        {
            return impl::arrow{func(*iter)};
        }

        [[nodiscard]] constexpr value_type operator[](difference_type n) const
            noexcept (requires{{func(iter[n])} noexcept;})
            requires (requires{{func(iter[n])};})
        {
            return func(iter[n]);
        }

        constexpr transform_iterator& operator++()
            noexcept (requires{{++iter} noexcept;})
            requires (requires{{++iter};})
        {
            ++iter;
            return *this;
        }

        [[nodiscard]] constexpr transform_iterator operator++(int)
            noexcept (requires{{transform_iterator{iter++}} noexcept;})
            requires (requires{{transform_iterator{iter++}};})
        {
            return {iter++};
        }

        constexpr transform_iterator& operator--()
            noexcept (requires{{--iter} noexcept;})
            requires (requires{{--iter};})
        {
            --iter;
            return *this;
        }

        [[nodiscard]] constexpr transform_iterator operator--(int)
            noexcept (requires{{transform_iterator{iter--}} noexcept;})
            requires (requires{{transform_iterator{iter--}};})
        {
            return {iter--};
        }

        constexpr transform_iterator& operator+=(difference_type n)
            noexcept (requires{{iter += n} noexcept;})
            requires (requires{{iter += n};})
        {
            iter += n;
            return *this;
        }

        [[nodiscard]] friend constexpr transform_iterator operator+(
            const transform_iterator& self,
            difference_type n
        ) noexcept (requires{{transform_iterator{self.iter + n}} noexcept;})
            requires (requires{{transform_iterator{self.iter + n}};})
        {
            return {self.iter + n};
        }

        [[nodiscard]] friend constexpr transform_iterator operator+(
            difference_type n,
            const transform_iterator& self
        ) noexcept (requires{{transform_iterator{self.iter + n}} noexcept;})
            requires (requires{{transform_iterator{self.iter + n}};})
        {
            return {self.iter + n};
        }

        constexpr transform_iterator& operator-=(difference_type n)
            noexcept (requires{{iter -= n} noexcept;})
            requires (requires{{iter -= n};})
        {
            iter -= n;
            return *this;
        }

        [[nodiscard]] constexpr transform_iterator operator-(difference_type n) const
            noexcept (requires{{transform_iterator{iter - n}} noexcept;})
            requires (requires{{transform_iterator{iter - n}};})
        {
            return {iter - n};
        }

        template <typename T, typename G>
        [[nodiscard]] constexpr difference_type operator-(const transform_iterator<T, G>& other) const
            noexcept (requires{
                {iter - other.iter} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{
                {iter - other.iter} -> meta::convertible_to<difference_type>;
            })
        {
            return iter - other.iter;
        }

        template <typename T, typename G>
        [[nodiscard]] constexpr bool operator<(const transform_iterator<T, G>& other) const
            noexcept (requires{{iter < other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter < other.iter} -> meta::truthy;})
        {
            return bool(iter < other.iter);
        }

        template <typename T, typename G>
        [[nodiscard]] constexpr bool operator<=(const transform_iterator<T, G>& other) const
            noexcept (requires{{iter <= other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter <= other.iter} -> meta::truthy;})
        {
            return bool(iter <= other.iter);
        }

        template <typename T, typename G>
        [[nodiscard]] constexpr bool operator==(const transform_iterator<T, G>& other) const
            noexcept (requires{{iter == other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter == other.iter} -> meta::truthy;})
        {
            return bool(iter == other.iter);
        }

        template <typename T, typename G>
        [[nodiscard]] constexpr bool operator!=(const transform_iterator<T, G>& other) const
            noexcept (requires{{iter != other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter != other.iter} -> meta::truthy;})
        {
            return bool(iter != other.iter);
        }

        template <typename T, typename G>
        [[nodiscard]] constexpr bool operator>=(const transform_iterator<T, G>& other) const
            noexcept (requires{{iter >= other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter >= other.iter} -> meta::truthy;})
        {
            return bool(iter >= other.iter);
        }

        template <typename T, typename G>
        [[nodiscard]] constexpr bool operator>(const transform_iterator<T, G>& other) const
            noexcept (requires{{iter > other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter > other.iter} -> meta::truthy;})
        {
            return bool(iter > other.iter);
        }

        template <typename T, typename G>
        [[nodiscard]] constexpr auto operator<=>(const transform_iterator<T, G>& other) const
            noexcept (requires{{iter <=> other.iter} noexcept;})
            requires (requires{{iter <=> other.iter};})
        {
            return iter <=> other.iter;
        }
    };

    /// TODO: probably just use pointers rather than a separate contiguous iterator
    /// class.

    /* A trivial iterator that acts just like a raw pointer over contiguous storage.
    Using a wrapper rather than the pointer directly comes with some advantages
    regarding type safety, preventing accidental conversions to pointer arguments,
    boolean conversions, placement new, etc. */
    template <meta::lvalue T> requires (meta::has_address<T>)
    struct contiguous_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = meta::remove_reference<T>;
        using reference = T;
        using pointer = meta::address_type<T>;

        pointer ptr = nullptr;

        constexpr void swap(contiguous_iterator& other) noexcept {
            std::ranges::swap(ptr, other.ptr);
        }

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return *ptr;
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept {
            return ptr;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
            return *(ptr + n);
        }

        constexpr contiguous_iterator& operator++() noexcept {
            ++ptr;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator++(int) noexcept {
            auto tmp = *this;
            ++ptr;
            return tmp;
        }

        [[nodiscard]] friend constexpr contiguous_iterator operator+(
            const contiguous_iterator& self,
            difference_type n
        ) noexcept {
            return {self.ptr + n};
        }

        [[nodiscard]] friend constexpr contiguous_iterator operator+(
            difference_type n,
            const contiguous_iterator& self
        ) noexcept {
            return {self.ptr + n};
        }

        constexpr contiguous_iterator& operator+=(difference_type n) noexcept {
            ptr += n;
            return *this;
        }

        constexpr contiguous_iterator& operator--() noexcept {
            --ptr;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator--(int) noexcept {
            auto tmp = *this;
            --ptr;
            return tmp;
        }

        [[nodiscard]] constexpr contiguous_iterator operator-(difference_type n) const noexcept {
            return {ptr - n};
        }

        [[nodiscard]] constexpr difference_type operator-(
            const contiguous_iterator& other
        ) const noexcept {
            return ptr - other.ptr;
        }

        constexpr contiguous_iterator& operator-=(difference_type n) noexcept {
            ptr -= n;
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const contiguous_iterator& other) const noexcept {
            return ptr == other.ptr;
        }

        [[nodiscard]] constexpr auto operator<=>(const contiguous_iterator& other) const noexcept {
            return ptr <=> other.ptr;
        }
    };

    /* A simple functor that implements a universal, non-cryptographic FNV-1a string
    hashing algorithm, which is stable at both compile time and runtime. */
    struct fnv1a : impl::prefer_constructor_tag {
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

    /// TODO: lowercase these?  They may also be merged into op.h when I finally finish
    /// functions.

    template <typename T>
    struct Construct : impl::prefer_constructor_tag {
        template <typename... Args>
        static constexpr T operator()(Args&&... args)
            noexcept (meta::nothrow::constructible_from<T, Args...>)
            requires (meta::constructible_from<T, Args...>)
        {
            return T(std::forward<Args>(args)...);
        }
    };

    struct Assign : impl::prefer_constructor_tag {
        template <typename T, typename U>
        static constexpr decltype(auto) operator()(T&& curr, U&& other)
            noexcept (meta::nothrow::assignable<T, U>)
            requires (meta::assignable<T, U>)
        {
            return (std::forward<T>(curr) = std::forward<U>(other));
        }
    };

    template <meta::not_void T>
    struct ConvertTo : impl::prefer_constructor_tag {
        template <meta::convertible_to<T> U>
        static constexpr T operator()(U&& value)
            noexcept (meta::nothrow::convertible_to<U, T>)
        {
            return std::forward<U>(value);
        }
    };

    template <meta::not_void T>
    struct ExplicitConvertTo : impl::prefer_constructor_tag {
        template <meta::explicitly_convertible_to<T> U>
        static constexpr T operator()(U&& value)
            noexcept (meta::nothrow::explicitly_convertible_to<U, T>)
        {
            return static_cast<T>(std::forward<U>(value));
        }
    };

    struct Hash : impl::prefer_constructor_tag {
        template <meta::hashable T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept (meta::nothrow::hashable<T>)
        {
            return (meta::hash(std::forward<T>(value)));
        }
    };

    struct AddressOf : impl::prefer_constructor_tag {
        template <meta::has_address T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_address<T>)
        {
            return (std::addressof(std::forward<T>(value)));
        }
    };

    struct Dereference : impl::prefer_constructor_tag {
        template <meta::has_dereference T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_dereference<T>)
        {
            return (*std::forward<T>(value));
        }
    };

    struct Arrow : impl::prefer_constructor_tag {
        template <meta::has_arrow T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_arrow<T>)
        {
            return (std::to_address(std::forward<T>(value)));
        }
    };

    struct ArrowDereference : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_arrow_dereference<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_arrow_dereference<L, R>)
        {
            return (std::forward<L>(lhs)->*std::forward<R>(rhs));
        }
    };

    struct Call : impl::prefer_constructor_tag {
        template <typename F, typename... A> requires (meta::callable<F, A...>)
        static constexpr decltype(auto) operator()(F&& f, A&&... args)
            noexcept(meta::nothrow::callable<F, A...>)
        {
            return (std::forward<F>(f)(std::forward<A>(args)...));
        }
    };

    struct Subscript : impl::prefer_constructor_tag {
        template <typename T, typename... K> requires (meta::indexable<T, K...>)
        static constexpr decltype(auto) operator()(T&& value, K&&... keys)
            noexcept(meta::nothrow::indexable<T, K...>)
        {
            return (std::forward<T>(value)[std::forward<K>(keys)...]);
        }
    };

    struct Pos : impl::prefer_constructor_tag {
        template <meta::has_pos T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_pos<T>)
        {
            return (+std::forward<T>(value));
        }
    };

    struct Neg : impl::prefer_constructor_tag {
        template <meta::has_neg T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_neg<T>)
        {
            return (-std::forward<T>(value));
        }
    };

    struct PreIncrement : impl::prefer_constructor_tag {
        template <meta::has_preincrement T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_preincrement<T>)
        {
            return (++std::forward<T>(value));
        }
    };

    struct PostIncrement : impl::prefer_constructor_tag {
        template <meta::has_postincrement T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_postincrement<T>)
        {
            return (std::forward<T>(value)++);
        }
    };

    struct PreDecrement : impl::prefer_constructor_tag {
        template <meta::has_predecrement T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_predecrement<T>)
        {
            return (--std::forward<T>(value));
        }
    };

    struct PostDecrement : impl::prefer_constructor_tag {
        template <meta::has_postdecrement T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_postdecrement<T>)
        {
            return (std::forward<T>(value)--);
        }
    };

    struct LogicalNot : impl::prefer_constructor_tag {
        template <meta::has_logical_not T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_logical_not<T>)
        {
            return (!std::forward<T>(value));
        }
    };

    struct LogicalAnd : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_logical_and<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_logical_and<L, R>)
        {
            return (std::forward<L>(lhs) && std::forward<R>(rhs));
        }
    };

    struct LogicalOr : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_logical_or<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_logical_or<L, R>)
        {
            return (std::forward<L>(lhs) || std::forward<R>(rhs));
        }
    };

    struct Less : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_lt<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_lt<L, R>)
        {
            return (std::forward<L>(lhs) < std::forward<R>(rhs));
        }
    };

    struct LessEqual : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_le<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_le<L, R>)
        {
            return (std::forward<L>(lhs) <= std::forward<R>(rhs));
        }
    };

    struct Equal : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_eq<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_eq<L, R>)
        {
            return (std::forward<L>(lhs) == std::forward<R>(rhs));
        }
    };

    struct NotEqual : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_ne<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ne<L, R>)
        {
            return (std::forward<L>(lhs) != std::forward<R>(rhs));
        }
    };

    struct GreaterEqual : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_ge<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ge<L, R>)
        {
            return (std::forward<L>(lhs) >= std::forward<R>(rhs));
        }
    };

    struct Greater : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_gt<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_gt<L, R>)
        {
            return (std::forward<L>(lhs) > std::forward<R>(rhs));
        }
    };

    struct Spaceship : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_spaceship<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_spaceship<L, R>)
        {
            return (std::forward<L>(lhs) <=> std::forward<R>(rhs));
        }
    };

    struct Add : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_add<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_add<L, R>)
        {
            return (std::forward<L>(lhs) + std::forward<R>(rhs));
        }
    };

    struct InplaceAdd : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_iadd<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_iadd<L, R>)
        {
            return (std::forward<L>(lhs) += std::forward<R>(rhs));
        }
    };

    struct Subtract : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_sub<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_sub<L, R>)
        {
            return (std::forward<L>(lhs) - std::forward<R>(rhs));
        }
    };

    struct InplaceSubtract : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_isub<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_isub<L, R>)
        {
            return (std::forward<L>(lhs) -= std::forward<R>(rhs));
        }
    };

    struct Multiply : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_mul<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_mul<L, R>)
        {
            return (std::forward<L>(lhs) * std::forward<R>(rhs));
        }
    };

    struct InplaceMultiply : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_imul<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_imul<L, R>)
        {
            return (std::forward<L>(lhs) *= std::forward<R>(rhs));
        }
    };

    struct Divide : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_div<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_div<L, R>)
        {
            return (std::forward<L>(lhs) / std::forward<R>(rhs));
        }
    };

    struct InplaceDivide : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_idiv<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_idiv<L, R>)
        {
            return (std::forward<L>(lhs) /= std::forward<R>(rhs));
        }
    };

    struct Modulus : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_mod<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_mod<L, R>)
        {
            return (std::forward<L>(lhs) % std::forward<R>(rhs));
        }
    };

    struct InplaceModulus : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_imod<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_imod<L, R>)
        {
            return (std::forward<L>(lhs) %= std::forward<R>(rhs));
        }
    };

    struct LeftShift : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_lshift<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_lshift<L, R>)
        {
            return (std::forward<L>(lhs) << std::forward<R>(rhs));
        }
    };

    struct InplaceLeftShift : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_ilshift<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ilshift<L, R>)
        {
            return (std::forward<L>(lhs) <<= std::forward<R>(rhs));
        }
    };

    struct RightShift : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_rshift<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_rshift<L, R>)
        {
            return (std::forward<L>(lhs) >> std::forward<R>(rhs));
        }
    };

    struct InplaceRightShift : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_irshift<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_irshift<L, R>)
        {
            return (std::forward<L>(lhs) >>= std::forward<R>(rhs));
        }
    };

    struct BitwiseNot : impl::prefer_constructor_tag {
        template <meta::has_bitwise_not T>
        static constexpr decltype(auto) operator()(T&& value)
            noexcept(meta::nothrow::has_bitwise_not<T>)
        {
            return (~std::forward<T>(value));
        }
    };

    struct BitwiseAnd : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_and<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_and<L, R>)
        {
            return (std::forward<L>(lhs) & std::forward<R>(rhs));
        }
    };

    struct InplaceBitwiseAnd : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_iand<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_iand<L, R>)
        {
            return (std::forward<L>(lhs) &= std::forward<R>(rhs));
        }
    };

    struct BitwiseOr : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_or<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_or<L, R>)
        {
            return (std::forward<L>(lhs) | std::forward<R>(rhs));
        }
    };

    struct InplaceBitwiseOr : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_ior<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ior<L, R>)
        {
            return (std::forward<L>(lhs) |= std::forward<R>(rhs));
        }
    };

    struct BitwiseXor : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_xor<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_xor<L, R>)
        {
            return (std::forward<L>(lhs) ^ std::forward<R>(rhs));
        }
    };

    struct InplaceBitwiseXor : impl::prefer_constructor_tag {
        template <typename L, typename R> requires (meta::has_ixor<L, R>)
        static constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::has_ixor<L, R>)
        {
            return (std::forward<L>(lhs) ^= std::forward<R>(rhs));
        }
    };

}


/// TODO: static_str<N> -> Str<Optional<N> = None>?


template <size_t N = 0>
struct static_str;


template <size_t N>
static_str(const char(&)[N]) -> static_str<N - 1>;


namespace impl {

    /// TODO: float_to_string and int_to_string may need to account for the new,
    /// arbitrary-precision numeric types in bits.h, which makes the circular
    /// dependencies a bit tricky.  Also, all this stuff should ideally be delayed
    /// as long as possible, but that will really have to wait until I get to strings
    /// and formatting in general.

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
    concept basic_tuple = inherits<T, impl::basic_tuple_tag>;

    namespace detail {

        template <typename T>
        constexpr bool static_str = false;
        template <size_t N>
        constexpr bool static_str<bertrand::static_str<N>> = true;

        template <typename T>
        constexpr bool prefer_constructor<impl::ref<T>> = true;
        template <typename T>
        constexpr bool prefer_constructor<impl::arrow<T>> = true;
        template <template <auto> typename F, auto... Ts>
        constexpr bool prefer_constructor<impl::vtable<F, Ts...>> = true;
        template <typename T>
        constexpr bool prefer_constructor<impl::contiguous_iterator<T>> = true;

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

    template <bertrand::meta::None T>
    struct hash<T> {
        [[nodiscard]] static constexpr size_t operator()(const T& value) noexcept {
            return std::bit_cast<size_t>(&bertrand::None);
        }
    };

    template <bertrand::meta::None T, typename Char>
    struct formatter<T, Char> : public formatter<std::basic_string_view<Char>, Char> {
        constexpr auto format(const T&, auto& ctx) const {
            using str = std::basic_string_view<Char>;
            return formatter<str, Char>::format(str{"None"}, ctx);
        }
    };

    template <bertrand::meta::basic_tuple T>
    struct tuple_size<T> {
        static constexpr size_t value = bertrand::meta::unqualify<T>::size();
    };

    template <size_t I, bertrand::meta::basic_tuple T>
    struct tuple_element<I, T> {
        using type = bertrand::meta::remove_rvalue<
            decltype((std::declval<T>().template get<I>()))
        >;
    };

}


#endif  // BERTRAND_COMMON_H
