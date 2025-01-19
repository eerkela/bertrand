#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <cmath>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>


namespace bertrand {


#ifdef BERTRAND_DEBUG
    constexpr bool DEBUG = true;
#else
    constexpr bool DEBUG = false;
#endif


#ifdef BERTRAND_MAX_ARGS
    static_assert(
        BERTRAND_MAX_ARGS > 0,
        "Maximum number of arguments must be positive."
    );
    constexpr size_t MAX_ARGS = BERTRAND_MAX_ARGS;
#else
    constexpr size_t MAX_ARGS = sizeof(size_t) * 8;
#endif


#ifdef BERTRAND_MAX_OVERLOADS
    static_assert(
        BERTRAND_MAX_OVERLOADS > 0,
        "Maximum number of overloads must be positive."
    );
    constexpr size_t MAX_OVERLOADS = BERTRAND_MAX_OVERLOADS;
#else
    constexpr size_t MAX_OVERLOADS = 256;
#endif


#ifdef BERTRAND_OVERLOAD_CACHE_SIZE
    static_assert(
        BERTRAND_OVERLOAD_CACHE_SIZE > 0,
        "Overload cache size must be positive."
    );
    constexpr size_t OVERLOAD_CACHE_SIZE = BERTRAND_OVERLOAD_CACHE_SIZE;
#else
    constexpr size_t OVERLOAD_CACHE_SIZE = 128;
#endif


#ifdef BERTRAND_TEMPLATE_RECURSION_LIMIT
    static_assert(
        BERTRAND_TEMPLATE_RECURSION_LIMIT > 0,
        "Template recursion limit must be positive."
    );
    constexpr size_t TEMPLATE_RECURSION_LIMIT = BERTRAND_TEMPLATE_RECURSION_LIMIT;
#else
    constexpr size_t TEMPLATE_RECURSION_LIMIT = 1024;
#endif


/* A generic sentinel type to simplify iterator implementations. */
struct sentinel {};


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

}


/* A simple struct holding paths to the bertrand environment's directories, if such an
environment is currently active. */
inline const impl::virtualenv VIRTUAL_ENV = impl::get_virtual_environment();


namespace meta {

    namespace detail {

        template <typename Search, size_t I, typename... Ts>
        static constexpr size_t _index_of = 0;
        template <typename Search, size_t I, typename T, typename... Ts>
        static constexpr size_t _index_of<Search, I, T, Ts...> =
            std::same_as<Search, T> ? 0 : _index_of<Search, I + 1, Ts...> + 1;

        template <size_t I, typename... Ts>
        struct unpack_type;
        template <size_t I, typename T, typename... Ts>
        struct unpack_type<I, T, Ts...> { using type = unpack_type<I - 1, Ts...>::type; };
        template <typename T, typename... Ts>
        struct unpack_type<0, T, Ts...> { using type = T; };

        template <typename T, typename Self>
        struct qualify { using type = T; };
        template <typename T, typename Self>
        struct qualify<T, const Self> { using type = const T; };
        template <typename T, typename Self>
        struct qualify<T, volatile Self> { using type = volatile T; };
        template <typename T, typename Self>
        struct qualify<T, const volatile Self> { using type = const volatile T; };
        template <typename T, typename Self>
        struct qualify<T, Self&> { using type = T&; };
        template <typename T, typename Self>
        struct qualify<T, const Self&> { using type = const T&; };
        template <typename T, typename Self>
        struct qualify<T, volatile Self&> { using type = volatile T&; };
        template <typename T, typename Self>
        struct qualify<T, const volatile Self&> { using type = const volatile T&; };
        template <typename T, typename Self>
        struct qualify<T, Self&&> { using type = T&&; };
        template <typename T, typename Self>
        struct qualify<T, const Self&&> { using type = const T&&; };
        template <typename T, typename Self>
        struct qualify<T, volatile Self&&> { using type = volatile T&&; };
        template <typename T, typename Self>
        struct qualify<T, const volatile Self&&> { using type = const volatile T&&; };
        template <typename Self>
        struct qualify<void, Self> { using type = void; };
        template <typename Self>
        struct qualify<void, const Self> { using type = void; };
        template <typename Self>
        struct qualify<void, volatile Self> { using type = void; };
        template <typename Self>
        struct qualify<void, const volatile Self> { using type = void; };
        template <typename Self>
        struct qualify<void, Self&> { using type = void; };
        template <typename Self>
        struct qualify<void, const Self&> { using type = void; };
        template <typename Self>
        struct qualify<void, volatile Self&> { using type = void; };
        template <typename Self>
        struct qualify<void, const volatile Self&> { using type = void; };
        template <typename Self>
        struct qualify<void, Self&&> { using type = void; };
        template <typename Self>
        struct qualify<void, const Self&&> { using type = void; };
        template <typename Self>
        struct qualify<void, volatile Self&&> { using type = void; };
        template <typename Self>
        struct qualify<void, const volatile Self&&> { using type = void; };

        template <typename T>
        struct remove_lvalue { using type = T; };
        template <typename T>
        struct remove_lvalue<T&> { using type = std::remove_reference_t<T>; };

        template <typename T>
        struct remove_rvalue { using type = T; };
        template <typename T>
        struct remove_rvalue<T&&> { using type = std::remove_reference_t<T>; };

        template <typename... Ts>
        constexpr bool types_are_unique = true;
        template <typename T, typename... Ts>
        constexpr bool types_are_unique<T, Ts...> =
            !(std::same_as<T, Ts> || ...) && types_are_unique<Ts...>;

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

    }

    /* Get the index of a particular type within a parameter pack.  Returns the pack's size
    if the type is not present. */
    template <typename Search, typename... Ts>
    static constexpr size_t index_of = detail::_index_of<Search, 0, Ts...>;

    /* Get the type at a particular index of a parameter pack. */
    template <size_t I, typename... Ts> requires (I < sizeof...(Ts))
    using unpack_type = detail::unpack_type<I, Ts...>::type;

    /* Index into a parameter pack and perfectly forward a single item.  This is superceded
    by the C++26 pack indexing language feature. */
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

    /* Transfer cvref qualifications from the right operand to the left. */
    template <typename T, typename Self>
    using qualify = detail::qualify<T, Self>::type;
    template <typename T, typename Self>
    using qualify_lvalue = std::add_lvalue_reference_t<qualify<T, Self>>;
    template <typename T, typename Self>
    using qualify_rvalue = std::add_rvalue_reference_t<qualify<T, Self>>;
    template <typename T, typename Self>
    using qualify_pointer = std::add_pointer_t<std::remove_reference_t<qualify<T, Self>>>;

    /* Strip lvalue references from the templated type while preserving rvalue and
    non-reference types. */
    template <typename T>
    using remove_lvalue = detail::remove_lvalue<T>::type;

    /* Strip rvalue references from the templated type while preserving lvalue and
    non-reference types. */
    template <typename T>
    using remove_rvalue = detail::remove_rvalue<T>::type;

    /* Trigger implicit conversion operators and/or implicit constructors, but not
    explicit ones.  In contrast, static_cast<>() will trigger explicit constructors on
    the target type, which can give unexpected results and violate type safety. */
    template <typename U>
    constexpr decltype(auto) implicit_cast(U&& value) {
        return std::forward<U>(value);
    }

    template <typename L, typename R>
    concept is = std::same_as<std::remove_cvref_t<L>, std::remove_cvref_t<R>>;

    template <typename L, typename R>
    concept inherits = std::derived_from<std::remove_cvref_t<L>, std::remove_cvref_t<R>>;

    template <typename T>
    concept is_const = std::is_const_v<std::remove_reference_t<T>>;

    template <typename T>
    concept is_volatile = std::is_volatile_v<std::remove_reference_t<T>>;

    template <typename T>
    concept is_lvalue = std::is_lvalue_reference_v<T>;

    template <typename T>
    concept is_rvalue = !is_lvalue<T>;

    template <typename T>
    concept is_ptr = std::is_pointer_v<std::remove_reference_t<T>>;

    template <typename T>
    concept is_qualified = std::is_reference_v<T> || is_const<T> || is_volatile<T>;

    template <typename... Ts>
    concept types_are_unique = detail::types_are_unique<Ts...>;

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(From from) {
        { static_cast<To>(from) } -> std::same_as<To>;
    };

    template <typename... Ts>
    concept has_common_type = requires {
        typename std::common_type<Ts...>::type;
    };

    template <typename... Ts> requires (has_common_type<Ts...>)
    using common_type = std::common_type_t<Ts...>;

    template <typename T>
    concept string_literal = requires(T t) {
        { []<size_t N>(const char(&)[N]){}(t) };
    };

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
    concept iterable = requires(T& t) {
        { std::ranges::begin(t) } -> std::input_or_output_iterator;
        { std::ranges::end(t) } -> std::sentinel_for<decltype(std::ranges::begin(t))>;
    };

    template <iterable T>
    using iter_type = decltype(*std::ranges::begin(
        std::declval<std::add_lvalue_reference_t<T>>()
    ));

    template <typename T, typename Value>
    concept yields = iterable<T> && std::convertible_to<iter_type<T>, Value>;

    template <typename T>
    concept reverse_iterable = requires(T& t) {
        { std::ranges::rbegin(t) } -> std::input_or_output_iterator;
        { std::ranges::rend(t) } -> std::sentinel_for<decltype(std::ranges::rbegin(t))>;
    };

    template <reverse_iterable T>
    using reverse_iter_type = decltype(*std::ranges::rbegin(
        std::declval<std::add_lvalue_reference_t<T>>()
    ));

    template <typename T, typename Value>
    concept yields_reverse =
        reverse_iterable<T> && std::convertible_to<reverse_iter_type<T>, Value>;

    template <typename T>
    concept has_size = requires(T t) {
        { std::ranges::size(t) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_empty = requires(T t) {
        { std::ranges::empty(t) } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept sequence_like = iterable<T> && has_size<T> && requires(T t) {
        { t[0] } -> std::convertible_to<iter_type<T>>;
    };

    template <typename T>
    concept mapping_like = requires(T t) {
        typename std::remove_cvref_t<T>::key_type;
        typename std::remove_cvref_t<T>::mapped_type;
        { t[std::declval<typename std::remove_cvref_t<T>::key_type>()] } ->
            std::convertible_to<typename std::remove_cvref_t<T>::mapped_type>;
    };

    template <typename T, typename... Key>
    concept supports_lookup =
        !std::is_pointer_v<T> &&
        !std::integral<std::remove_cvref_t<T>> &&
        requires(T t, Key... key) {
            { t[key...] };
        };

    template <typename T, typename... Key> requires (supports_lookup<T, Key...>)
    using lookup_type = decltype(std::declval<T>()[std::declval<Key>()...]);

    template <typename T, typename Value, typename... Key>
    concept lookup_yields = supports_lookup<T, Key...> && requires(T t, Key... key) {
        { t[key...] } -> std::convertible_to<Value>;
    };

    template <typename T, typename Value, typename... Key>
    concept supports_item_assignment =
        !std::is_pointer_v<T> &&
        !std::integral<std::remove_cvref_t<T>> &&
        requires(T t, Key... key, Value value) {
            { t[key...] = value };
        };

    template <typename T>
    concept pair_like = std::tuple_size<T>::value == 2 && requires(T t) {
        { std::get<0>(t) };
        { std::get<1>(t) };
    };

    template <typename T, typename First, typename Second>
    concept pair_like_with = pair_like<T> && requires(T t) {
        { std::get<0>(t) } -> std::convertible_to<First>;
        { std::get<1>(t) } -> std::convertible_to<Second>;
    };

    template <typename T>
    concept yields_pairs = iterable<T> && pair_like<iter_type<T>>;

    template <typename T, typename First, typename Second>
    concept yields_pairs_with =
        iterable<T> && pair_like_with<iter_type<T>, First, Second>;

    template <typename T>
    concept has_to_string = requires(T t) {
        { std::to_string(t) } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_stream_insertion = requires(std::ostream& os, T t) {
        { os << t } -> std::convertible_to<std::ostream&>;
    };

    template <typename T>
    concept has_call_operator = requires() {
        { &std::remove_cvref_t<T>::operator() };
    };

    template <typename T>
    concept complex_like = requires(T t) {
        { t.real() } -> std::convertible_to<double>;
        { t.imag() } -> std::convertible_to<double>;
    };

    template <typename T>
    concept has_reserve = requires(T t, size_t n) {
        { t.reserve(n) } -> std::same_as<void>;
    };

    template <typename T, typename Key>
    concept has_contains = requires(T t, Key key) {
        { t.contains(key) } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept has_keys = requires(T t) {
        { t.keys() } -> iterable;
        { t.keys() } -> yields<typename std::remove_cvref_t<T>::key_type>;
    };

    template <typename T>
    concept has_values = requires(T t) {
        { t.values() } -> iterable;
        { t.values() } -> yields<typename std::remove_cvref_t<T>::mapped_type>;
    };

    template <typename T>
    concept has_items = requires(T t) {
        { t.items() } -> iterable;
        { t.items() } -> yields_pairs_with<
            typename std::remove_cvref_t<T>::key_type,
            typename std::remove_cvref_t<T>::mapped_type
        >;
    };

    template <typename T>
    concept has_operator_bool = requires(T t) {
        { !t } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept hashable = requires(T t) {
        { std::hash<std::decay_t<T>>{}(t) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_abs = requires(T t) {{ std::abs(t) };};
    template <has_abs T>
    using abs_type = decltype(std::abs(std::declval<T>()));
    template <typename T, typename Return>
    concept abs_returns = requires(T t) {
        { std::abs(t) } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_invert = requires(T t) {{ ~t };};
    template <has_invert T>
    using invert_type = decltype(~std::declval<T>());
    template <typename T, typename Return>
    concept invert_returns = requires(T t) {
        { ~t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_pos = requires(T t) {{ +t };};
    template <has_pos T>
    using pos_type = decltype(+std::declval<T>());
    template <typename T, typename Return>
    concept pos_returns = requires(T t) {
        { +t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_neg = requires(T t) {{ -t };};
    template <has_neg T>
    using neg_type = decltype(-std::declval<T>());
    template <typename T, typename Return>
    concept neg_returns = requires(T t) {
        { -t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_preincrement = requires(T t) {{ ++t };};
    template <has_preincrement T>
    using preincrement_type = decltype(++std::declval<T>());
    template <typename T, typename Return>
    concept preincrement_returns = requires(T t) {
        { ++t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_postincrement = requires(T t) {{ t++ };};
    template <has_postincrement T>
    using postincrement_type = decltype(std::declval<T>()++);
    template <typename T, typename Return>
    concept postincrement_returns = requires(T t) {
        { t++ } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_predecrement = requires(T t) {{ --t };};
    template <has_predecrement T>
    using predecrement_type = decltype(--std::declval<T>());
    template <typename T, typename Return>
    concept predecrement_returns = requires(T t) {
        { --t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_postdecrement = requires(T t) {{ t-- };};
    template <has_postdecrement T>
    using postdecrement_type = decltype(std::declval<T>()--);
    template <typename T, typename Return>
    concept postdecrement_returns = requires(T t) {
        { t-- } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_lt = requires(L l, R r) {{ l < r };};
    template <typename L, typename R> requires (has_lt<L, R>)
    using lt_type = decltype(std::declval<L>() < std::declval<R>());
    template <typename L, typename R, typename Return>
    concept lt_returns = requires(L l, R r) {
        { l < r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_le = requires(L l, R r) {{ l <= r };};
    template <typename L, typename R> requires (has_le<L, R>)
    using le_type = decltype(std::declval<L>() <= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept le_returns = requires(L l, R r) {
        { l <= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_eq = requires(L l, R r) {{ l == r };};
    template <typename L, typename R> requires (has_eq<L, R>)
    using eq_type = decltype(std::declval<L>() == std::declval<R>());
    template <typename L, typename R, typename Return>
    concept eq_returns = requires(L l, R r) {
        { l == r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ne = requires(L l, R r) {{ l != r };};
    template <typename L, typename R> requires (has_ne<L, R>)
    using ne_type = decltype(std::declval<L>() != std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ne_returns = requires(L l, R r) {
        { l != r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ge = requires(L l, R r) {{ l >= r };};
    template <typename L, typename R> requires (has_ge<L, R>)
    using ge_type = decltype(std::declval<L>() >= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ge_returns = requires(L l, R r) {
        { l >= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_gt = requires(L l, R r) {{ l > r };};
    template <typename L, typename R> requires (has_gt<L, R>)
    using gt_type = decltype(std::declval<L>() > std::declval<R>());
    template <typename L, typename R, typename Return>
    concept gt_returns = requires(L l, R r) {
        { l > r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_add = requires(L l, R r) {{ l + r };};
    template <typename L, typename R> requires (has_add<L, R>)
    using add_type = decltype(std::declval<L>() + std::declval<R>());
    template <typename L, typename R, typename Return>
    concept add_returns = requires(L l, R r) {
        { l + r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_iadd = requires(L& l, R r) {{ l += r };};
    template <typename L, typename R> requires (has_iadd<L, R>)
    using iadd_type = decltype(std::declval<L&>() += std::declval<R>());
    template <typename L, typename R, typename Return>
    concept iadd_returns = requires(L& l, R r) {
        { l += r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_sub = requires(L l, R r) {{ l - r };};
    template <typename L, typename R> requires (has_sub<L, R>)
    using sub_type = decltype(std::declval<L>() - std::declval<R>());
    template <typename L, typename R, typename Return>
    concept sub_returns = requires(L l, R r) {
        { l - r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_isub = requires(L& l, R r) {{ l -= r };};
    template <typename L, typename R> requires (has_isub<L, R>)
    using isub_type = decltype(std::declval<L&>() -= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept isub_returns = requires(L& l, R r) {
        { l -= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_mul = requires(L l, R r) {{ l * r };};
    template <typename L, typename R> requires (has_mul<L, R>)
    using mul_type = decltype(std::declval<L>() * std::declval<R>());
    template <typename L, typename R, typename Return>
    concept mul_returns = requires(L l, R r) {
        { l * r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_imul = requires(L& l, R r) {{ l *= r };};
    template <typename L, typename R> requires (has_imul<L, R>)
    using imul_type = decltype(std::declval<L&>() *= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept imul_returns = requires(L& l, R r) {
        { l *= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_truediv = requires(L l, R r) {{ l / r };};
    template <typename L, typename R> requires (has_truediv<L, R>)
    using truediv_type = decltype(std::declval<L>() / std::declval<R>());
    template <typename L, typename R, typename Return>
    concept truediv_returns = requires(L l, R r) {
        { l / r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_itruediv = requires(L& l, R r) {{ l /= r };};
    template <typename L, typename R> requires (has_itruediv<L, R>)
    using itruediv_type = decltype(std::declval<L&>() /= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept itruediv_returns = requires(L& l, R r) {
        { l /= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_mod = requires(L l, R r) {{ l % r };};
    template <typename L, typename R> requires (has_mod<L, R>)
    using mod_type = decltype(std::declval<L>() % std::declval<R>());
    template <typename L, typename R, typename Return>
    concept mod_returns = requires(L l, R r) {
        { l % r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_imod = requires(L& l, R r) {{ l %= r };};
    template <typename L, typename R> requires (has_imod<L, R>)
    using imod_type = decltype(std::declval<L&>() %= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept imod_returns = requires(L& l, R r) {
        { l %= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_pow = requires(L l, R r) {{ std::pow(l, r) };};
    template <typename L, typename R> requires (has_pow<L, R>)
    using pow_type = decltype(std::pow(std::declval<L>(), std::declval<R>()));
    template <typename L, typename R, typename Return>
    concept pow_returns = requires(L l, R r) {
        { std::pow(l, r) } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_lshift = requires(L l, R r) {{ l << r };};
    template <typename L, typename R> requires (has_lshift<L, R>)
    using lshift_type = decltype(std::declval<L>() << std::declval<R>());
    template <typename L, typename R, typename Return>
    concept lshift_returns = requires(L l, R r) {
        { l << r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ilshift = requires(L& l, R r) {{ l <<= r };};
    template <typename L, typename R> requires (has_ilshift<L, R>)
    using ilshift_type = decltype(std::declval<L&>() <<= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ilshift_returns = requires(L& l, R r) {
        { l <<= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_rshift = requires(L l, R r) {{ l >> r };};
    template <typename L, typename R> requires (has_rshift<L, R>)
    using rshift_type = decltype(std::declval<L>() >> std::declval<R>());
    template <typename L, typename R, typename Return>
    concept rshift_returns = requires(L l, R r) {
        { l >> r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_irshift = requires(L& l, R r) {{ l >>= r };};
    template <typename L, typename R> requires (has_irshift<L, R>)
    using irshift_type = decltype(std::declval<L&>() >>= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept irshift_returns = requires(L& l, R r) {
        { l >>= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_and = requires(L l, R r) {{ l & r };};
    template <typename L, typename R> requires (has_and<L, R>)
    using and_type = decltype(std::declval<L>() & std::declval<R>());
    template <typename L, typename R, typename Return>
    concept and_returns = requires(L l, R r) {
        { l & r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_iand = requires(L& l, R r) {{ l &= r };};
    template <typename L, typename R> requires (has_iand<L, R>)
    using iand_type = decltype(std::declval<L&>() &= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept iand_returns = requires(L& l, R r) {
        { l &= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_or = requires(L l, R r) {{ l | r };};
    template <typename L, typename R> requires (has_or<L, R>)
    using or_type = decltype(std::declval<L>() | std::declval<R>());
    template <typename L, typename R, typename Return>
    concept or_returns = requires(L l, R r) {
        { l | r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ior = requires(L& l, R r) {{ l |= r };};
    template <typename L, typename R> requires (has_ior<L, R>)
    using ior_type = decltype(std::declval<L&>() |= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ior_returns = requires(L& l, R r) {
        { l |= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_xor = requires(L l, R r) {{ l ^ r };};
    template <typename L, typename R> requires (has_xor<L, R>)
    using xor_type = decltype(std::declval<L>() ^ std::declval<R>());
    template <typename L, typename R, typename Return>
    concept xor_returns = requires(L l, R r) {
        { l ^ r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ixor = requires(L& l, R r) {{ l ^= r };};
    template <typename L, typename R> requires (has_ixor<L, R>)
    using ixor_type = decltype(std::declval<L&>() ^= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ixor_returns = requires(L& l, R r) {
        { l ^= r } -> std::convertible_to<Return>;
    };

}


}  // namespace bertrand


#endif  // BERTRAND_COMMON_H
