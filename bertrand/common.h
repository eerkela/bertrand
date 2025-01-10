#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>


namespace bertrand {


template <size_t N>
struct bitset;
template <typename... Ts>
struct args;
template <typename F, typename... Fs>
struct chain;


namespace impl {

    template <typename Search, size_t I, typename... Ts>
    static constexpr size_t _index_of = 0;
    template <typename Search, size_t I, typename T, typename... Ts>
    static constexpr size_t _index_of<Search, I, T, Ts...> =
        std::same_as<Search, T> ? 0 : _index_of<Search, I + 1, Ts...> + 1;

    template <size_t I, typename... Ts>
    struct _unpack_type;
    template <typename T, typename... Ts>
    struct _unpack_type<0, T, Ts...> { using type = T; };
    template <size_t I, typename T, typename... Ts>
    struct _unpack_type<I, T, Ts...> { using type = _unpack_type<I - 1, Ts...>::type; };

    template <typename T, typename Self>
    struct _qualify { using type = T; };
    template <typename T, typename Self>
    struct _qualify<T, const Self> { using type = const T; };
    template <typename T, typename Self>
    struct _qualify<T, volatile Self> { using type = volatile T; };
    template <typename T, typename Self>
    struct _qualify<T, const volatile Self> { using type = const volatile T; };
    template <typename T, typename Self>
    struct _qualify<T, Self&> { using type = T&; };
    template <typename T, typename Self>
    struct _qualify<T, const Self&> { using type = const T&; };
    template <typename T, typename Self>
    struct _qualify<T, volatile Self&> { using type = volatile T&; };
    template <typename T, typename Self>
    struct _qualify<T, const volatile Self&> { using type = const volatile T&; };
    template <typename T, typename Self>
    struct _qualify<T, Self&&> { using type = T&&; };
    template <typename T, typename Self>
    struct _qualify<T, const Self&&> { using type = const T&&; };
    template <typename T, typename Self>
    struct _qualify<T, volatile Self&&> { using type = volatile T&&; };
    template <typename T, typename Self>
    struct _qualify<T, const volatile Self&&> { using type = const volatile T&&; };
    template <typename Self>
    struct _qualify<void, Self> { using type = void; };
    template <typename Self>
    struct _qualify<void, const Self> { using type = void; };
    template <typename Self>
    struct _qualify<void, volatile Self> { using type = void; };
    template <typename Self>
    struct _qualify<void, const volatile Self> { using type = void; };
    template <typename Self>
    struct _qualify<void, Self&> { using type = void; };
    template <typename Self>
    struct _qualify<void, const Self&> { using type = void; };
    template <typename Self>
    struct _qualify<void, volatile Self&> { using type = void; };
    template <typename Self>
    struct _qualify<void, const volatile Self&> { using type = void; };
    template <typename Self>
    struct _qualify<void, Self&&> { using type = void; };
    template <typename Self>
    struct _qualify<void, const Self&&> { using type = void; };
    template <typename Self>
    struct _qualify<void, volatile Self&&> { using type = void; };
    template <typename Self>
    struct _qualify<void, const volatile Self&&> { using type = void; };

    template <typename T>
    struct _remove_lvalue { using type = T; };
    template <typename T>
    struct _remove_lvalue<T&> { using type = std::remove_reference_t<T>; };

    template <typename T>
    struct _remove_rvalue { using type = T; };
    template <typename T>
    struct _remove_rvalue<T&&> { using type = std::remove_reference_t<T>; };

    template <typename... Ts>
    constexpr bool _types_are_unique = true;
    template <typename T, typename... Ts>
    constexpr bool _types_are_unique<T, Ts...> =
        !(std::same_as<T, Ts> || ...) && _types_are_unique<Ts...>;

    template <typename T>
    struct _optional { static constexpr bool value = false; };
    template <typename T>
    struct _optional<std::optional<T>> {
        static constexpr bool value = true;
        using type = T;
    };

    template <typename T>
    struct _variant { static constexpr bool value = false; };
    template <typename... Ts>
    struct _variant<std::variant<Ts...>> {
        static constexpr bool value = true;
        using types = std::tuple<Ts...>;
    };

    template <typename T>
    struct _shared_ptr { static constexpr bool enable = false; };
    template <typename T>
    struct _shared_ptr<std::shared_ptr<T>> {
        static constexpr bool enable = true;
        using type = T;
    };

    template <typename T>
    struct _unique_ptr { static constexpr bool enable = false; };
    template <typename T>
    struct _unique_ptr<std::unique_ptr<T>> {
        static constexpr bool enable = true;
        using type = T;
    };

    struct bitset_tag {};
    struct args_tag {};
    struct chain_tag {};

    template <typename T>
    constexpr bool _is_bitset = false;
    template <size_t N>
    constexpr bool _is_bitset<bitset<N>> = true;

    template <typename T>
    constexpr bool _is_args = false;
    template <typename... Ts>
    constexpr bool _is_args<args<Ts...>> = true;

    template <typename T>
    constexpr bool _is_chain = false;
    template <typename F, typename... Fs>
    constexpr bool _is_chain<chain<F, Fs...>> = true;

    template <typename... Ts>
    struct ArgsBase : args_tag {};
    template <typename T, typename... Ts>
    struct ArgsBase<T, Ts...> : ArgsBase<Ts...> {
        std::conditional_t<
            std::is_lvalue_reference_v<T>,
            T,
            std::remove_reference_t<T>
        > value;
        constexpr ArgsBase(T value, Ts... ts) :
            ArgsBase<Ts...>(std::forward<Ts>(ts)...),
            value(std::forward<T>(value))
        {}
        constexpr ArgsBase(ArgsBase&& other) :
            ArgsBase<Ts...>(std::move(other)),
            value([](ArgsBase&& other) {
                if constexpr (std::is_lvalue_reference_v<T>) {
                    return other.value;
                } else {
                    return std::move(other.value);
                }
            }())
        {}
    };
    template <typename T, typename... Ts>
        requires (std::is_void_v<T> || (std::is_void_v<Ts> || ...))
    struct ArgsBase<T, Ts...> {};
}


/* A generic sentinel type to simplify iterator implementations. */
struct sentinel {};


/* Get the index of a particular type within a parameter pack.  Returns the pack's size
if the type is not present. */
template <typename Search, typename... Ts>
static constexpr size_t index_of = impl::_index_of<Search, 0, Ts...>;


/* Get the type at a particular index of a parameter pack. */
template <size_t I, typename... Ts> requires (I < sizeof...(Ts))
using unpack_type = impl::_unpack_type<I, Ts...>::type;


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


/* Transfer cvref qualifications from the right operand to the left. */
template <typename T, typename Self>
using qualify = impl::_qualify<T, Self>::type;
template <typename T, typename Self>
using qualify_lvalue = std::add_lvalue_reference_t<qualify<T, Self>>;
template <typename T, typename Self>
using qualify_rvalue = std::add_rvalue_reference_t<qualify<T, Self>>;
template <typename T, typename Self>
using qualify_pointer = std::add_pointer_t<std::remove_reference_t<qualify<T, Self>>>;


/* Strip lvalue references from the templated type while preserving rvalue and
non-reference types. */
template <typename T>
using remove_lvalue = impl::_remove_lvalue<T>::type;


/* Strip rvalue references from the templated type while preserving lvalue and
non-reference types. */
template <typename T>
using remove_rvalue = impl::_remove_rvalue<T>::type;


/* Trigger implicit conversion operators and/or implicit constructors, but not
explicit ones.  In contrast, static_cast<>() will trigger explicit constructors on
the target type, which can give unexpected results and violate type safety. */
template <typename U>
decltype(auto) implicit_cast(U&& value) {
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
concept is_rvalue = std::is_rvalue_reference_v<T>;


template <typename T>
concept is_ptr = std::is_pointer_v<std::remove_reference_t<T>>;


template <typename... Ts>
concept types_are_unique = impl::_types_are_unique<Ts...>;


template <typename From, typename To>
concept explicitly_convertible_to = requires(From from) {
    { static_cast<To>(from) } -> std::same_as<To>;
};


template <typename T>
concept string_literal = requires(T t) {
    { []<size_t N>(const char(&)[N]){}(t) };
};


template <typename T>
concept is_optional = impl::_optional<std::remove_cvref_t<T>>::value;
template <is_optional T>
using optional_type = impl::_optional<std::remove_cvref_t<T>>::type;


template <typename T>
concept is_variant = impl::_variant<std::remove_cvref_t<T>>::value;
template <is_variant T>
using variant_types = impl::_variant<std::remove_cvref_t<T>>::types;


template <typename T>
concept is_shared_ptr = impl::_shared_ptr<std::remove_cvref_t<T>>::enable;
template <is_shared_ptr T>
using shared_ptr_type = impl::_shared_ptr<std::remove_cvref_t<T>>::type;


template <typename T>
concept is_unique_ptr = impl::_unique_ptr<std::remove_cvref_t<T>>::enable;
template <is_unique_ptr T>
using unique_ptr_type = impl::_unique_ptr<std::remove_cvref_t<T>>::type;


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


template <typename T>
concept is_bitset = impl::_is_bitset<std::remove_cvref_t<T>>;


/* A simple bitset type that stores flags in a fixed-size array of machine words.
Allows a wider range of operations than `std::bitset<N>`, including full, bigint-style
arithmetic, lexicographic comparisons, one-hot decomposition, and more, which allow
bitsets to pull double duty as portable, unsigned integers of arbitrary width. */
template <size_t N>
struct bitset : impl::bitset_tag {
    using Word = size_t;
    struct Ref;

private:
    static constexpr size_t word_size = sizeof(Word) * 8;
    static constexpr size_t array_size = (N + word_size - 1) / word_size;

    std::array<Word, array_size> m_data;

    struct BigWord {
        Word hi;
        Word lo;
    };

    static constexpr bool wide_gt(BigWord a, BigWord b) noexcept {
        return a.hi > b.hi || a.lo > b.lo;
    }

    static constexpr BigWord wide_sub(BigWord a, BigWord b) noexcept {
        Word temp = a.lo - b.lo;
        return {a.hi - b.hi - (temp > a.lo), temp};
    }

    static constexpr BigWord wide_mul(Word a, Word b) noexcept {
        constexpr size_t chunk = word_size / 2;
        constexpr Word mask = (Word(1) << chunk) - 1;

        // 1. split a, b into low and high halves
        BigWord x = {a >> chunk, a & mask};
        BigWord y = {b >> chunk, b & mask};

        // 2. compute partial products
        Word lo_lo = x.lo * y.lo;
        Word lo_hi = x.lo * y.hi;
        Word hi_lo = x.hi * y.lo;
        Word hi_hi = x.hi * y.hi;

        // 3. combine cross terms
        Word cross = (lo_lo >> chunk) + (lo_hi & mask) + (hi_lo & mask);
        Word carry = cross >> chunk;

        // 4. compute result
        return {
            hi_hi + (lo_hi >> chunk) + (hi_lo >> chunk) + carry,
            (lo_lo & mask) | (cross << chunk)
        };
    }

    static constexpr BigWord wide_div(BigWord u, Word v) noexcept {
        /// NOTE: this implementation is taken from the libdivide reference:
        /// https://github.com/ridiculousfish/libdivide/blob/master/doc/divlu.c
        /// It should be much faster than the naive approach found in Hacker's Delight.
        using Signed = std::make_signed_t<Word>;
        constexpr size_t chunk = word_size / 2;
        constexpr Word b = Word(1) << chunk;
        constexpr Word mask = b - 1;

        // 1. If the high bits are empty, then we devolve to a single word divide.
        if (!u.hi) {
            return {u.lo / v, u.lo % v};
        }

        // 2. Check for overflow and divide by zero.
        if (u.hi >= v) {
            return {~Word(0), ~Word(0)};
        }

        // 3. Left shift divisor until the most significant bit is set.  This cannot
        // overflow the numerator because u.hi < v.  The strange bitwise AND is meant
        // to avoid undefined behavior when shifting by a full word size.  It is taken
        // from https://ridiculousfish.com/blog/posts/labor-of-division-episode-v.html
        size_t shift = std::countl_zero(v);
        v <<= shift;
        u.hi <<= shift;
        u.hi |= ((u.lo >> (-shift & (word_size - 1))) & (-Signed(shift) >> (word_size - 1)));
        u.lo <<= shift;

        // 4. Split divisor and low bits of numerator into partial words.
        BigWord n = {u.lo >> chunk, u.lo & mask};
        BigWord d = {v >> chunk, v & mask};

        // 5. Estimate q1 = [n3 n2 n1] / [d1 d0].  Note that while qhat may be 2
        // half-words, q1 is always just the lower half, which translates to the upper
        // half of the final quotient.
        Word qhat = n.hi / d.hi;
        Word rhat = n.hi % d.hi;
        Word c1 = qhat * d.lo;
        Word c2 = rhat * b + n.lo;
        if (c1 > c2) {
            qhat -= 1 + ((c1 - c2) > v);
        }
        Word q1 = qhat & mask;

        // 6. Compute the true (normalized) partial remainder.
        Word r = n.hi * b + n.lo - q1 * v;

        // 7. Estimate q0 = [r1 r0 n0] / [d1 d0].  These are the bottom bits of the
        // final quotient.
        qhat = r / d.hi;
        rhat = r % d.hi;
        c1 = qhat * d.lo;
        c2 = rhat * b + n.lo;
        if (c1 > c2) {
            qhat -= 1 + ((c1 - c2) > v);
        }
        Word q0 = qhat & mask;

        // 8. Return the quotient and unnormalized remainder
        return {(q1 << chunk) | q0, ((r * b) + n.lo - (q0 * v)) >> shift};
    }

    static constexpr void _divmod(
        const bitset& lhs,
        const bitset& rhs,
        bitset& quotient,
        bitset& remainder
    ) {
        using Signed = std::make_signed_t<Word>;
        constexpr size_t chunk = word_size / 2;
        constexpr Word b = Word(1) << chunk;

        if (!rhs) {
            throw std::domain_error("division by zero");
        }
        if (lhs < rhs || !lhs) {
            remainder = lhs;
            quotient.fill(0);
            return;
        }

        // 1. Compute effective lengths.
        size_t lhs_last = lhs.last_one();
        size_t rhs_last = rhs.last_one();
        size_t n = (rhs_last + (word_size - 1)) / word_size;
        size_t m = ((lhs_last + (word_size - 1)) / word_size) - n;

        // 2. If the divisor is a single word, then we can avoid multi-word division.
        if (n == 1) {
            Word v = rhs.m_data[0];
            Word rem = 0;
            for (size_t i = m + n; i-- > 0;) {
                auto [q, r] = wide_div({rem, lhs.m_data[i]}, v);
                quotient.m_data[i] = q;
                rem = r;
            }
            for (size_t i = m + n; i < array_size; ++i) {
                quotient.m_data[i] = 0;
            }
            remainder.m_data[0] = rem;
            for (size_t i = 1; i < n; ++i) {
                remainder.m_data[i] = 0;
            }
            return;
        }

        /// NOTE: this is based on Knuth's Algorithm D, which is among the simplest for
        /// bigint division.  Much of the implementation was taken from:
        /// https://skanthak.hier-im-netz.de/division.html
        /// Which references the Hacker's Delight reference, with a helpful explanation
        /// of the algorithm design.  See that or the Knuth reference for more details.
        std::array<Word, array_size> v = rhs.m_data;
        std::array<Word, array_size + 1> u;
        for (size_t i = 0; i < array_size; ++i) {
            u[i] = lhs.m_data[i];
        }
        u[array_size] = 0;

        // 3. Left shift until the highest set bit in the divisor is at the top of its
        // respective word.  The strange bitwise AND is meant to avoid undefined
        // behavior when shifting by a full word size (i.e. shift == 0).  It is taken
        // from https://ridiculousfish.com/blog/posts/labor-of-division-episode-v.html
        size_t shift = word_size - 1 - (rhs_last % word_size);
        size_t shift_carry = -shift & (word_size - 1);
        size_t shift_correct = -Signed(shift) >> (word_size - 1);
        for (size_t i = array_size + 1; i-- > 1;) {
            u[i] = (u[i] << shift) | ((u[i - 1] >> shift_carry) & shift_correct);
        }
        u[0] <<= shift;
        for (size_t i = array_size; i-- > 1;) {
            v[i] = (v[i] << shift) | ((v[i - 1] >> shift_carry) & shift_correct);
        }
        v[0] <<= shift;

        // 4. Trial division
        quotient.fill(0);
        for (size_t j = m + 1; j-- > 0;) {
            // take the top two words of the numerator for wide division
            auto [qhat, rhat] = wide_div({u[j + n] * b, u[j + n - 1]}, v[n - 1]);

            // refine quotient if guess is too large
            while (qhat >= b || wide_gt(
                wide_mul(qhat, v[n - 2]),
                {rhat * b, u[j + n - 2]}
            )) {
                --qhat;
                rhat += v[n - 1];
                if (rhat >= b) {
                    break;
                }
            }

            // 5. Multiply and subtract
            Word borrow = 0;
            for (size_t i = 0; i < n; ++i) {
                BigWord prod = wide_mul(qhat, v[i]);
                Word temp = u[i + j] - borrow;
                borrow = temp > u[i + j];
                temp -= prod.lo;
                borrow += temp > prod.lo;
                u[i + j] = temp;
                borrow += prod.hi;
            }

            // 6. Correct for negative remainder
            if (u[j + n] < borrow) {
                --qhat;
                borrow = 0;
                for (size_t i = 0; i < n; ++i) {
                    Word temp = u[i + j] + borrow;
                    borrow = temp < u[i + j];
                    temp += v[i];
                    borrow += temp < v[i];
                    u[i + j] = temp;
                }
                u[j + n] += borrow;
            }
            quotient.m_data[j] = qhat;
        }

        // 7. Unshift the remainder and quotient to get the final result
        for (size_t i = 0; i < n - 1; ++i) {
            remainder.m_data[i] = (u[i] >> shift) | (u[i + 1] << (word_size - shift));
        }
        remainder.m_data[n - 1] = u[n - 1] >> shift;
        for (size_t i = n; i < array_size; ++i) {
            remainder.m_data[i] = 0;
        }
    }

public:
    /* Construct an empty bitset initialized to zero. */
    constexpr bitset() noexcept : m_data{} {}

    /* Construct a bitset from an integer value. */
    constexpr bitset(Word value) noexcept : m_data{} {
        if constexpr (array_size == 1 && N < word_size) {
            constexpr Word mask = (Word(1) << N) - 1;
            m_data[0] = value & mask;
        } else if constexpr (array_size >= 1) {
            m_data[0] = value;
        }
    }

    /* Construct a bitset from a string of 1s and 0s. */
    constexpr bitset(std::string_view str, char zero = '0', char one = '1') : m_data{} {
        for (size_t i = 0; i < N && i < str.size(); ++i) {
            char c = str[i];
            if (c == one) {
                m_data[i / word_size] |= Word(1) << (i % word_size);
            } else if (c != zero) {
                throw std::invalid_argument(
                    "bitset string must contain only 1s and 0s, not: '" +
                    std::string(1, c) + "'"
                );
            }
        }
    }

    /* Construct a bitset from a string literal of 1s and 0s, allowing for CTAD. */
    constexpr bitset(const char(&str)[N + 1], char zero = '0', char one = '1') : m_data{} {
        for (size_t i = 0; i < N; ++i) {
            char c = str[i];
            if (c == one) {
                m_data[i / word_size] |= Word(1) << (i % word_size);
            } else if (c != zero) {
                throw std::invalid_argument(
                    "bitset string must contain only 1s and 0s, not: '" +
                    std::string(1, c) + "'"
                );
            }
        }
    }

    /* Bitsets evalute true if any of their bits are set. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return any();
    }

    /* Convert the bitset to an integer representation if it fits within the platform's
    word size. */
    template <typename T>
        requires (N <= sizeof(Word) * 8 && explicitly_convertible_to<Word, T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept {
        if constexpr (array_size == 0) {
            return static_cast<T>(Word(0));
        } else {
            return static_cast<T>(m_data[0]);
        }
    }

    /* Convert the bitset to a string representation. */
    [[nodiscard]] explicit constexpr operator std::string() const noexcept {
        constexpr int diff = '1' - '0';
        std::string result;
        result.reserve(N);
        for (size_t i = N; i-- > 0;) {
            result.push_back('0' + diff * (*this)[i]);
        }
        return result;
    }

    /* Convert the bitset into a string representation.  Defaults to base 2 with the
    given zero and one characters, but also allows bases up to 36, in which case the
    zero and one characters will be ignored.  The result is always padded to the exact
    width needed to represent the bitset in the chosen base, including leading zeroes
    if needed. */
    [[nodiscard]] constexpr std::string to_string(
        size_t base = 2,
        char zero = '0',
        char one = '1'
    ) const {
        if (base < 2) {
            throw std::invalid_argument("bitset base must be at least 2");
        } else if (base > 36) {
            throw std::invalid_argument("bitset base must be at most 36");
        }
        if (base == 2) {
            int diff = one - zero;
            std::string result;
            result.reserve(N);
            for (size_t i = N; i-- > 0;) {
                result.push_back(zero + (diff * (*this)[i]));
            }
            return result;
        }
        constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        constexpr double log2[] = {
            0.0, 0.0, 1.0, 1.5849625007211563, 2.0,
            2.321928094887362, 2.584962500721156, 2.807354922057604,
            3.0, 3.169925001442312, 3.321928094887362,
            3.4594316186372973, 3.584962500721156, 3.700439718141092,
            3.807354922057604, 3.9068905956085187, 4.0,
            4.087462841250339, 4.169925001442312, 4.247927513443585,
            4.321928094887363, 4.392317422778761, 4.459431618637297,
            4.523561956057013, 4.584962500721156, 4.643856189774724,
            4.700439718141092, 4.754887502163469, 4.807354922057604,
            4.857980995127572, 4.906890595608518, 4.954196310386875,
            5.0, 5.044394119358453, 5.087462841250339,
            5.129283016944966, 5.169925001442312
        };
        double len = N / log2[base];
        size_t ceil = len;
        ceil += ceil < len;
        std::string result(ceil, '0');
        size_t i = 0;
        bitset quotient = *this;
        bitset divisor = base;
        bitset remainder;
        for (size_t i = 0; quotient; ++i) {
            _divmod(quotient, divisor, quotient, remainder);
            result[ceil - i - 1] = digits[remainder.m_data[0]];
        }
        return result;
    }

    /* The number of bits that are held in the set. */
    [[nodiscard]] static constexpr size_t size() noexcept {
        return N;
    }

    /* The total number of bits that were allocated.  This will always be a multiple of
    the machine's word size. */
    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return array_size * word_size;
    }

    /* Get the underlying array that backs the bitset. */
    [[nodiscard]] constexpr auto& data() noexcept { return m_data; }
    [[nodiscard]] constexpr const auto& data() const noexcept { return m_data; }

    /* Check if any of the bits are set. */
    [[nodiscard]] constexpr bool any() const noexcept {
        for (size_t i = 0; i < array_size; ++i) {
            if (m_data[i]) {
                return true;
            }
        }
        return false;
    }

    /* Check if any of the bits are set within a particular range. */
    [[nodiscard]] constexpr bool any(size_t first, size_t last = N) const noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return false;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        return *this & temp;
    }

    /* Check if all of the bits are set. */
    [[nodiscard]] constexpr bool all() const noexcept {
        constexpr bool odd = N % word_size;
        for (size_t i = 0; i < array_size - odd; ++i) {
            if (m_data[i] != std::numeric_limits<Word>::max()) {
                return false;
            }
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            return m_data[array_size - 1] == mask;
        } else {
            return true;
        }
    }

    /* Check if all of the bits are set within a particular range. */
    [[nodiscard]] constexpr bool all(size_t first, size_t last = N) const noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return true;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        return (*this & temp) == temp;
    }

    /* Get the number of bits that are currently set. */
    [[nodiscard]] constexpr size_t count() const noexcept {
        size_t count = 0;
        for (size_t i = 0; i < array_size; ++i) {
            count += std::popcount(m_data[i]);
        }
        return count;
    }

    /* Get the number of bits that are currently set within a particular range. */
    [[nodiscard]] constexpr size_t count(size_t first, size_t last = N) const noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return 0;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        size_t count = 0;
        for (size_t i = 0; i < array_size; ++i) {
            count += std::popcount(m_data[i] & temp.m_data[i]);
        }
        return count;
    }

    /* Return the index of the first bit that is set, or the size of the array if no
    bits are set. */
    [[nodiscard]] constexpr size_t first_one() const noexcept {
        for (size_t i = 0; i < array_size; ++i) {
            Word curr = m_data[i];
            if (curr) {
                return word_size * i  + std::countr_zero(curr);
            }
        }
        return N;
    }

    /* Return the index of the first bit that is set within a given range, or the size
    of the array if no bits are set. */
    [[nodiscard]] constexpr size_t first_one(size_t first, size_t last = N) const noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return N;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        for (size_t i = 0; i < array_size; ++i) {
            Word curr = m_data[i] & temp.m_data[i];
            if (curr) {
                return word_size * i + std::countr_zero(curr);
            }
        }
        return N;
    }

    /* Return the index of the last bit that is set, or the size of the array if no
    bits are set. */
    [[nodiscard]] constexpr size_t last_one() const noexcept {
        for (size_t i = array_size; i-- > 0;) {
            Word curr = m_data[i];
            if (curr) {
                return word_size * i + word_size - 1 - std::countl_zero(curr);
            }
        }
        return N;
    }

    /* Return the index of the last bit that is set within a given range, or the size
    of the array if no bits are set. */
    [[nodiscard]] constexpr size_t last_one(size_t first, size_t last = N) const noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return N;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        for (size_t i = array_size; i-- > 0;) {
            Word curr = m_data[i] & temp.m_data[i];
            if (curr) {
                return word_size * i + word_size - 1 - std::countl_zero(curr);
            }
        }
        return N;
    }

    /* Return the index of the first bit that is not set, or the size of the array if
    all bits are set. */
    [[nodiscard]] constexpr size_t first_zero() const noexcept {
        for (size_t i = 0; i < array_size; ++i) {
            Word curr = m_data[i];
            if (curr != std::numeric_limits<Word>::max()) {
                return word_size * i + std::countr_one(curr);
            }
        }
        return N;
    }

    /* Return the index of the first bit that is not set within a given range, or the
    size of the array if all bits are set. */
    [[nodiscard]] constexpr size_t first_zero(size_t first, size_t last = N) const noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return N;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        for (size_t i = 0; i < array_size; ++i) {
            Word curr = m_data[i] & temp.m_data[i];
            if (curr != temp.m_data[i]) {
                return word_size * i + std::countr_one(curr);
            }
        }
        return N;
    }

    /* Return the index of the last bit that is not set, or the size of the array if
    all bits are set. */
    [[nodiscard]] constexpr size_t last_zero() const noexcept {
        constexpr bool odd = N % word_size;
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word curr = m_data[array_size - 1];
            if (curr != mask) {
                return
                    word_size * (array_size - 1) +
                    word_size - 1 -
                    std::countl_one(curr | ~mask);
            }
        }
        for (size_t i = array_size - odd; i-- > 0;) {
            Word curr = m_data[i];
            if (curr != std::numeric_limits<Word>::max()) {
                return word_size * i + word_size - 1 - std::countl_one(curr);
            }
        }
        return N;
    }

    /* Return the index of the last bit that is not set within a given range, or the
    size of the array if all bits are set. */
    [[nodiscard]] constexpr size_t last_zero(size_t first, size_t last = N) const noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return N;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        for (size_t i = array_size; i-- > 0;) {
            Word curr = m_data[i] & temp.m_data[i];
            if (curr != temp.m_data[i]) {
                return
                    word_size * i +
                    word_size - 1 -
                    std::countl_one(curr | ~temp.m_data[i]);
            }
        }
        return N;
    }

    /* Set all of the bits to the given value. */
    [[maybe_unused]] constexpr bitset& fill(bool value) noexcept {
        constexpr bool odd = N % word_size;
        Word filled = std::numeric_limits<Word>::max() * value;
        for (size_t i = 0; i < array_size - odd; ++i) {
            m_data[i] = filled;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            m_data[array_size - 1] = filled & mask;
        }
        return *this;
    }

    /* Set all of the bits within a certain range to the given value. */
    [[maybe_unused]] constexpr bitset& fill(bool value, size_t first, size_t last = N) noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        if (value) {
            *this |= temp;
        } else {
            *this &= ~temp;
        }
        return *this;
    }

    /* Toggle all of the bits in the set. */
    [[maybe_unused]] constexpr bitset& flip() noexcept {
        constexpr bool odd = N % word_size;
        for (size_t i = 0; i < array_size - odd; ++i) {
            m_data[i] ^= std::numeric_limits<Word>::max();
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            m_data[array_size - 1] ^= mask;
        }
        return *this;
    }

    /* Toggle all of the bits within a certain range. */
    [[maybe_unused]] constexpr bitset& flip(size_t first, size_t last = N) noexcept {
        last = std::min(last, N);
        if (first >= last) {
            return;
        }
        bitset temp;
        temp.fill(1);
        temp <<= N - last;
        temp >>= N - last + first;
        temp <<= first;  // [first, last).
        *this ^= temp;
        return *this;
    }

    /* A mutable reference to a single bit in the set. */
    struct Ref {
    private:
        friend bitset;

        Word& value;
        Word index;

        constexpr Ref(Word& value, Word index) noexcept :
            value(value),
            index(index)
        {}

    public:
        [[nodiscard]] constexpr operator bool() const noexcept {
            return value & (Word(1) << index);
        }

        [[nodiscard]] constexpr bool operator~() const noexcept {
            return !*this;
        }

        [[maybe_unused]] Ref& operator=(bool x) noexcept {
            value = (value & ~(Word(1) << index)) | (x << index);
            return *this;
        }

        [[maybe_unused]] Ref& flip() noexcept {
            value ^= Word(1) << index;
            return *this;
        }
    };

    /* Get the value of a specific bit in the set, without bounds checking. */
    [[nodiscard]] constexpr bool operator[](size_t index) const noexcept {
        Word mask = Word(1) << (index % word_size);
        return m_data[index / word_size] & mask;
    }
    [[nodiscard]] constexpr Ref operator[](size_t index) noexcept {
        return {m_data[index / word_size], index % word_size};
    }

    /* Get the value of a specific bit in the set, performing a bounds check on the
    way.  Also available as `std::get<I>(bitset)`, which allows for structured
    bindings. */
    template <size_t I> requires (I < N)
    [[nodiscard]] constexpr bool get() const noexcept {
        return (*this)[I];
    }
    template <size_t I> requires (I < N)
    [[nodiscard]] constexpr Ref get() noexcept {
        return (*this)[I];
    }
    [[nodiscard]] constexpr bool get(size_t index) const {
        if (index >= N) {
            throw std::out_of_range("bitset index out of range");
        }
        return (*this)[index];
    }
    [[nodiscard]] constexpr Ref get(size_t index) {
        if (index >= N) {
            throw std::out_of_range("bitset index out of range");
        }
        return (*this)[index];
    }

    /* A range that decomposes a bitmask into its one-hot components. */
    struct Components {
    private:
        friend bitset;
        const bitset* self;
        size_t first;
        size_t last;

        Components(const bitset* self, size_t first, size_t last) noexcept :
            self(self), first(first), last(last)
        {}

    public:
        struct Iterator {
        private:
            friend Components;

            const bitset* self;
            size_t index;
            size_t last;
            bitset curr;

            Iterator(const bitset* self, size_t first, size_t last) noexcept :
                self(self),
                index(self->first_one(first, last)),
                last(last),
                curr{1}
            {
                curr <<= index;
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = bitset;
            using pointer = const bitset*;
            using reference = const bitset&;

            constexpr reference operator*() const noexcept {
                return curr;
            }

            constexpr pointer operator->() const noexcept {
                return &curr;
            }

            constexpr Iterator& operator++() noexcept {
                size_t new_index = self->first_one(index + 1);
                curr <<= new_index - index;
                index = new_index;
                return *this;
            }

            constexpr Iterator operator++(int) noexcept {
                Iterator copy = *this;
                ++*this;
                return copy;
            }

            friend constexpr bool operator==(const Iterator& self, sentinel) noexcept {
                return self.index >= self.last;
            }

            friend constexpr bool operator==(sentinel, const Iterator& self) noexcept {
                return self.index >= self.last;
            }

            friend constexpr bool operator!=(const Iterator& self, sentinel) noexcept {
                return self.index < self.last;
            }

            friend constexpr bool operator!=(sentinel, const Iterator& self) noexcept {
                return self.index < self.last;
            }
        };

        Iterator begin() const noexcept {
            return {self, first, last};
        }

        sentinel end() const noexcept {
            return {};
        }
    };

    /* Return a view over the one-hot masks that make up the bitset within a given
    range. */
    [[nodiscard]] constexpr Components components(
        size_t first = 0,
        size_t last = N
    ) const noexcept {
        return {this, first, std::min(last, N)};
    }

    /* An iterator over the individual bits within the set. */
    struct Iterator {
    private:
        friend bitset;

        ssize_t index;
        bitset* self;
        mutable Ref cache;

        Iterator(bitset* self, ssize_t index) noexcept : self(self), index(index) {}

    public:
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = ssize_t;
        using value_type = Ref;
        using pointer = Ref*;
        using reference = Ref;

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return (*self)[static_cast<size_t>(index)];
        }

        [[nodiscard]] constexpr const pointer operator->() const noexcept {
            cache = **this;
            return &cache;
        }

        [[nodiscard]] constexpr pointer operator->() noexcept {
            cache = **this;
            return &cache;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
            return (*self)[static_cast<size_t>(index + n)];
        }

        [[maybe_unused]] constexpr Iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[maybe_unused]] constexpr Iterator operator++(int) noexcept {
            Iterator copy = *this;
            ++index;
            return copy;
        }

        [[maybe_unused]] constexpr Iterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        [[nodiscard]] friend constexpr Iterator operator+(
            const Iterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index + rhs};
        }

        [[nodiscard]] friend constexpr Iterator operator+(
            difference_type lhs,
            const Iterator& rhs
        ) noexcept {
            return {rhs.self, rhs.index + lhs};
        }

        [[maybe_unused]] constexpr Iterator& operator--() noexcept {
            --index;
            return *this;
        }

        [[maybe_unused]] constexpr Iterator operator--(int) noexcept {
            Iterator copy = *this;
            --index;
            return copy;
        }

        [[maybe_unused]] constexpr Iterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] friend constexpr Iterator operator-(
            const Iterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index - rhs};
        }

        [[nodiscard]] friend constexpr Iterator operator-(
            difference_type lhs,
            const Iterator& rhs
        ) noexcept {
            return {rhs.self, lhs - rhs.index};
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index - rhs.index;
        }


        [[nodiscard]] friend constexpr bool operator<(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index < rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator<=(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index <= rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index == rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index != rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator>=(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index >= rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator>(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index > rhs.index;
        }
    };

    /* A read-only iterator over the individual bits within the set. */
    struct ConstIterator {
    private:
        friend bitset;

        ssize_t index;
        const bitset* self;
        mutable bool cache;

        ConstIterator(const bitset* self, ssize_t index) noexcept :
            self(self),
            index(index)
        {}

    public:
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = ssize_t;
        using value_type = bool;
        using pointer = const bool*;
        using reference = bool;

        [[nodiscard]] constexpr bool operator*() const noexcept {
            return (*self)[index];
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept {
            cache = **this;
            return &cache;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
            return (*self)[index + n];
        }

        [[maybe_unused]] constexpr ConstIterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[maybe_unused]] constexpr ConstIterator operator++(int) noexcept {
            ConstIterator copy = *this;
            ++index;
            return copy;
        }

        [[maybe_unused]] constexpr ConstIterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        [[nodiscard]] friend constexpr ConstIterator operator+(
            const ConstIterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index + rhs};
        }

        [[nodiscard]] friend constexpr ConstIterator operator+(
            difference_type lhs,
            const ConstIterator& rhs
        ) noexcept {
            return {rhs.self, rhs.index + lhs};
        }

        [[maybe_unused]] constexpr ConstIterator& operator--() noexcept {
            --index;
            return *this;
        }

        [[maybe_unused]] constexpr ConstIterator operator--(int) noexcept {
            ConstIterator copy = *this;
            --index;
            return copy;
        }

        [[maybe_unused]] constexpr ConstIterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] friend constexpr ConstIterator operator-(
            const ConstIterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index - rhs};
        }

        [[nodiscard]] friend constexpr ConstIterator operator-(
            difference_type lhs,
            const ConstIterator& rhs
        ) noexcept {
            return {rhs.self, lhs - rhs.index};
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index - rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator<(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index < rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator<=(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index <= rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index == rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index != rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator>=(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index >= rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator>(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index > rhs.index;
        }
    };

    /* Return a forward iterator over the individual flags within the set. */
    [[nodiscard]] constexpr Iterator begin() noexcept {
        return {this, 0};
    }
    [[nodiscard]] constexpr ConstIterator begin() const noexcept {
        return {this, 0};
    }
    [[nodiscard]] constexpr ConstIterator cbegin() const noexcept {
        return {this, 0};
    }
    [[nodiscard]] constexpr Iterator end() noexcept {
        return {this, N};
    }
    [[nodiscard]] constexpr ConstIterator end() const noexcept {
        return {this, N};
    }
    [[nodiscard]] constexpr ConstIterator cend() const noexcept {
        return {this, N};
    }

    using ReverseIterator = std::reverse_iterator<Iterator>;
    using ConstReverseIterator = std::reverse_iterator<ConstIterator>;

    /* Return a reverse iterator over the individual flags within the set. */
    [[nodiscard]] constexpr ReverseIterator rbegin() noexcept {
        return {end()};
    }
    [[nodiscard]] constexpr ConstReverseIterator rbegin() const noexcept {
        return {end()};
    }
    [[nodiscard]] constexpr ConstReverseIterator crbegin() const noexcept {
        return {cend()};
    }
    [[nodiscard]] constexpr ReverseIterator rend() noexcept {
        return {begin()};
    }
    [[nodiscard]] constexpr ConstReverseIterator rend() const noexcept {
        return {begin()};
    }
    [[nodiscard]] constexpr ConstReverseIterator crend() const noexcept {
        return {cbegin()};
    }

    /* Check whether one bitset is lexicographically less than another of the same
    size. */
    [[nodiscard]] friend constexpr bool operator<(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        for (size_t i = array_size; i-- > 0;) {
            if (lhs.m_data[i] < rhs.m_data[i]) {
                return true;
            } else if (lhs.m_data[i] > rhs.m_data[i]) {
                return false;
            }
        }
        return false;
    }

    /* Check whether one bitset is lexicographically less than or equal to another of
    the same size. */
    [[nodiscard]] friend constexpr bool operator<=(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        for (size_t i = array_size; i-- > 0;) {
            if (lhs.m_data[i] < rhs.m_data[i]) {
                return true;
            } else if (lhs.m_data[i] > rhs.m_data[i]) {
                return false;
            }
        }
        return true;
    }

    /* Check whether one bitset is lexicographically equal to another of the same
    size. */
    [[nodiscard]] friend constexpr bool operator==(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        for (size_t i = 0; i < array_size; ++i) {
            if (lhs.m_data[i] != rhs.m_data[i]) {
                return false;
            }
        }
        return true;
    }

    /* Check whether one bitset is lexicographically not equal to another of the same
    size. */
    [[nodiscard]] friend constexpr bool operator!=(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        for (size_t i = 0; i < array_size; ++i) {
            if (lhs.m_data[i] != rhs.m_data[i]) {
                return true;
            }
        }
        return false;
    }

    /* Check whether one bitset is lexicographically greater than or equal to another of
    the same size. */
    [[nodiscard]] friend constexpr bool operator>=(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        for (size_t i = array_size; i-- > 0;) {
            if (lhs.m_data[i] > rhs.m_data[i]) {
                return true;
            } else if (lhs.m_data[i] < rhs.m_data[i]) {
                return false;
            }
        }
        return true;
    }

    /* Check whether one bitset is lexicographically greater than another of the same
    size. */
    [[nodiscard]] friend constexpr bool operator>(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        for (size_t i = array_size; i-- > 0;) {
            if (lhs.m_data[i] > rhs.m_data[i]) {
                return true;
            } else if (lhs.m_data[i] < rhs.m_data[i]) {
                return false;
            }
        }
        return false;
    }

    /* Add two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator+(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        constexpr bool odd = N % word_size;
        bitset result;
        Word carry = 0;
        for (size_t i = 0; i < array_size - odd; ++i) {
            Word a = lhs.m_data[i] + carry;
            carry = a < lhs.m_data[i];
            Word b = a + rhs.m_data[i];
            carry |= b < a;
            result.m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word sum = lhs.m_data[array_size - 1] + carry + rhs.m_data[array_size - 1];
            result.m_data[array_size - 1] = sum & mask;
        }
        return result;
    }
    [[maybe_unused]] constexpr bitset& operator+=(const bitset& other) noexcept {
        constexpr bool odd = N % word_size;
        Word carry = 0;
        for (size_t i = 0; i < array_size - odd; ++i) {
            Word a = m_data[i] + carry;
            carry = a < m_data[i];
            Word b = a + other.m_data[i];
            carry |= b < a;
            m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word sum = m_data[array_size - 1] + carry + other.m_data[array_size - 1];
            m_data[array_size - 1] = sum & mask;
        }
        return *this;
    }

    /* Subtract two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator-(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        constexpr bool odd = N % word_size;
        bitset result;
        Word borrow = 0;
        for (size_t i = 0; i < array_size - odd; ++i) {
            Word a = lhs.m_data[i] - borrow;
            borrow = a > lhs.m_data[i];
            Word b = a - rhs.m_data[i];
            borrow |= b > a;
            result.m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word diff = lhs.m_data[array_size - 1] - borrow - rhs.m_data[array_size - 1];
            result.m_data[array_size - 1] = diff & mask;
        }
        return result;
    }
    [[maybe_unused]] constexpr bitset& operator-=(const bitset& other) noexcept {
        constexpr bool odd = N % word_size;
        Word borrow = 0;
        for (size_t i = 0; i < array_size - odd; ++i) {
            Word a = m_data[i] - borrow;
            borrow = a > m_data[i];
            Word b = a - other.m_data[i];
            borrow |= b > a;
            m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word diff = m_data[array_size - 1] - borrow - other.m_data[array_size - 1];
            m_data[array_size - 1] = diff & mask;
        }
        return *this;
    }

    /* Multiply two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator*(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        /// NOTE: this uses schoolbook multiplication, which is generally fastest for
        /// small set sizes, up to a couple thousand bits.
        constexpr size_t chunk = word_size / 2;
        bitset result;
        Word carry = 0;
        for (size_t i = 0; i < array_size; ++i) {
            Word carry = 0;
            for (size_t j = 0; j + i < array_size; ++j) {
                size_t k = j + i;
                BigWord prod = wide_mul(lhs.m_data[i], rhs.m_data[j]);
                prod.lo += result.m_data[k];
                if (prod.lo < result.m_data[k]) {
                    ++prod.hi;
                }
                prod.lo += carry;
                if (prod.lo < carry) {
                    ++prod.hi;
                }
                result.m_data[k] = prod.lo;
                carry = prod.hi;
            }
        }
        if constexpr (N % word_size) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            result.m_data[array_size - 1] &= mask;
        }
        return result;
    }
    [[maybe_unused]] constexpr bitset& operator*=(const bitset& other) noexcept {
        *this = *this * other;
        return *this;
    }

    /* Divide this bitset by another and return both the quotient and remainder.  This
    is slightly more efficient than doing separate `/` and `%` operations if both are
    needed. */
    [[nodiscard]] constexpr std::pair<bitset, bitset> divmod(const bitset& d) const {
        bitset quotient, remainder;
        _divmod(*this, d, quotient, remainder);
        return {quotient, remainder};
    }
    [[nodiscard]] constexpr bitset divmod(const bitset& d, bitset& remainder) const {
        bitset quotient;
        _divmod(*this, d, quotient, remainder);
        return quotient;
    }
    constexpr void divmod(const bitset& d, bitset& quotient, bitset& remainder) const {
        _divmod(*this, d, quotient, remainder);
    }

    /* Divide this bitset by another in-place, returning the remainder.  This is
    slightly more efficient than doing separate `/=` and `%` operations */
    [[nodiscard]] constexpr bitset idivmod(const bitset& d) {
        bitset remainder;
        _divmod(*this, d, *this, remainder);
        return remainder;
    }
    constexpr void idivmod(const bitset& d, bitset& remainder) {
        _divmod(*this, d, *this, remainder);
    }

    /* Divide two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator/(
        const bitset& lhs,
        const bitset& rhs
    ) {
        bitset quotient, remainder;
        _divmod(lhs, rhs, quotient, remainder);
        return quotient;
    }
    [[maybe_unused]] constexpr bitset& operator/=(const bitset& other) {
        bitset remainder;
        _divmod(*this, other, *this, remainder);
        return *this;
    }

    /* Get the remainder after dividing two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator%(
        const bitset& lhs,
        const bitset& rhs
    ) {
        bitset quotient, remainder;
        _divmod(lhs, rhs, quotient, remainder);
        return remainder;
    }
    [[maybe_unused]] constexpr bitset& operator%=(const bitset& other) {
        bitset quotient;
        _divmod(*this, other, quotient, *this);
        return *this;
    }

    /* Increment the bitset by one. */
    [[maybe_unused]] bitset& operator++() noexcept {
        if constexpr (N) {
            constexpr bool odd = N % word_size;
            Word carry = m_data[0] == std::numeric_limits<Word>::max();
            ++m_data[0];
            for (size_t i = 1; carry && i < array_size - odd; ++i) {
                carry = m_data[i] == std::numeric_limits<Word>::max();
                ++m_data[i];
            }
            if constexpr (odd) {
                constexpr Word mask = (Word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] = (m_data[array_size - 1] + carry) & mask;
            }
        }
        return *this;
    }
    [[maybe_unused]] bitset operator++(int) noexcept {
        bitset copy = *this;
        ++*this;
        return copy;
    }

    /* Decrement the bitset by one. */
    [[maybe_unused]] bitset& operator--() noexcept {
        if constexpr (N) {
            constexpr bool odd = N % word_size;
            Word borrow = m_data[0] == 0;
            --m_data[0];
            for (size_t i = 1; borrow && i < array_size - odd; ++i) {
                borrow = m_data[i] == 0;
                --m_data[i];
            }
            if constexpr (odd) {
                constexpr Word mask = (Word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] = (m_data[array_size - 1] - borrow) & mask;
            }
        }
        return *this;
    }
    [[maybe_unused]] bitset operator--(int) noexcept {
        bitset copy = *this;
        --*this;
        return copy;
    }

    /* Apply a binary NOT to the contents of the bitset. */
    [[nodiscard]] friend constexpr bitset operator~(const bitset& set) noexcept {
        constexpr bool odd = N % word_size;
        bitset result;
        for (size_t i = 0; i < array_size - odd; ++i) {
            result.m_data[i] = ~set.m_data[i];
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            result.m_data[array_size - 1] = ~set.m_data[array_size - 1] & mask;
        }
        return result;
    }

    /* Apply a binary AND between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator&(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        bitset result;
        for (size_t i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] & rhs.m_data[i];
        }
        return result;
    }
    [[maybe_unused]] constexpr bitset& operator&=(
        const bitset& other
    ) noexcept {
        for (size_t i = 0; i < array_size; ++i) {
            m_data[i] &= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary OR between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator|(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        bitset result;
        for (size_t i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] | rhs.m_data[i];
        }
        return result;
    }
    [[maybe_unused]] constexpr bitset& operator|=(
        const bitset& other
    ) noexcept {
        for (size_t i = 0; i < array_size; ++i) {
            m_data[i] |= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary XOR between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator^(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        bitset result;
        for (size_t i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] ^ rhs.m_data[i];
        }
        return result;
    }
    [[maybe_unused]] constexpr bitset& operator^=(
        const bitset& other
    ) noexcept {
        for (size_t i = 0; i < array_size; ++i) {
            m_data[i] ^= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary left shift to the contents of the bitset. */
    [[nodiscard]] constexpr bitset operator<<(size_t rhs) const noexcept {
        bitset result;
        size_t shift = rhs / word_size;
        if (shift < array_size) {
            size_t remainder = rhs % word_size;
            for (size_t i = array_size; i-- > shift + 1;) {
                size_t offset = i - shift;
                result.m_data[i] = (m_data[offset] << remainder) |
                    (m_data[offset - 1] >> (word_size - remainder));
            }
            result.m_data[shift] = m_data[0] << remainder;
            if constexpr (N % word_size) {
                constexpr Word mask = (Word(1) << (N % word_size)) - 1;
                result.m_data[array_size - 1] &= mask;
            }
        }
        return result;
    }
    [[maybe_unused]] constexpr bitset& operator<<=(size_t rhs) noexcept {
        size_t shift = rhs / word_size;
        if (shift < array_size) {
            size_t remainder = rhs % word_size;
            for (size_t i = array_size; i-- > shift + 1;) {
                size_t offset = i - shift;
                m_data[i] = (m_data[offset] << remainder) |
                    (m_data[offset - 1] >> (word_size - remainder));
            }
            m_data[shift] = m_data[0] << remainder;
            for (size_t i = shift; i-- > 0;) {
                m_data[i] = 0;
            }
            if constexpr (N % word_size) {
                constexpr Word mask = (Word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] &= mask;
            }
        } else {
            for (size_t i = 0; i < array_size; ++i) {
                m_data[i] = 0;
            }
        }
        return *this;
    }

    /* Apply a binary right shift to the contents of the bitset. */
    [[nodiscard]] constexpr bitset operator>>(size_t rhs) const noexcept {
        bitset result;
        size_t shift = rhs / word_size;
        if (shift < array_size) {
            size_t end = array_size - shift - 1;
            size_t remainder = rhs % word_size;
            for (size_t i = 0; i < end; ++i) {
                size_t offset = i + shift;
                result.m_data[i] = (m_data[offset] >> remainder) |
                    (m_data[offset + 1] << (word_size - remainder));
            }
            result.m_data[end] = m_data[array_size - 1] >> remainder;
        }
        return result;
    }
    [[maybe_unused]] constexpr bitset& operator>>=(size_t rhs) noexcept {
        size_t shift = rhs / word_size;
        if (shift < array_size) {
            size_t end = array_size - shift - 1;
            size_t remainder = rhs % word_size;
            for (size_t i = 0; i < end; ++i) {
                size_t offset = i + shift;
                m_data[i] = (m_data[offset] >> remainder) |
                    (m_data[offset + 1] << (word_size - remainder));
            }
            m_data[end] = m_data[array_size - 1] >> remainder;
            for (size_t i = array_size - shift; i < array_size; ++i) {
                m_data[i] = 0;
            }
        } else {
            for (size_t i = 0; i < array_size; ++i) {
                m_data[i] = 0;
            }
        }
        return *this;
    }

    /* Print the bitset to an output stream. */
    [[maybe_unused]] friend std::ostream& operator<<(
        std::ostream& os,
        const bitset& set
    ) {
        os << std::string(set);
        return os;
    }

    /* Read the bitset from an input stream. */
    [[maybe_unused]] friend std::istream& operator>>(
        std::istream& is,
        bitset& set
    ) {
        char c;
        is.get(c);
        while (std::isspace(c)) {
            is.get(c);
        }
        size_t i = 0;
        if (c == '0') {
            is.get(c);
            if (c == 'b') {
                is.get(c);
            } else {
                ++i;
            }
        }
        bool one;
        while (is.good() && i < N && (c == '0' || (one = (c == '1')))) {
            set[i++] = one;
            is.get(c);
        }
        return is;
    }
};


template <std::integral T>
bitset(T) -> bitset<sizeof(T) * 8>;
template <size_t N>
bitset(const char(&)[N]) -> bitset<N - 1>;


}  // namespace bertrand


namespace std {

    /* Specializing `std::hash` allows bitsets to be stored in hash tables. */
    template <bertrand::is_bitset T>
    struct hash<T> {
        [[nodiscard]] static size_t operator()(const T& bitset) noexcept {
            size_t result = 0;
            for (size_t i = 0; i < std::remove_cvref_t<T>::size(); ++i) {
                result ^= bertrand::hash_combine(
                    result,
                    bitset.data()[i]
                );
            }
            return result;
        }
    };

    /* Specializing `std::tuple_size` allows bitsets to be decomposed using
    structured bindings. */
    template <bertrand::is_bitset T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    /* Specializing `std::tuple_element` allows bitsets to be decomposed using
    structured bindings. */
    template <size_t I, bertrand::is_bitset T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = bool;
    };

    /* `std::get<I>(chain)` extracts the I-th flag from the bitset. */
    template <size_t I, bertrand::is_bitset T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr bool get(T&& bitset) noexcept {
        return std::forward<T>(bitset).template get<I>();
    }

}


namespace bertrand {


template <typename T>
concept is_args = impl::_is_args<std::remove_cvref_t<T>>;


/* Save a set of input arguments for later use.  Returns an args<> container, which
stores the arguments similar to a `std::tuple`, except that it is capable of storing
references and cannot be copied or moved.  Calling the args pack as an rvalue will
perfectly forward its values to an input function, without any extra copies, and at
most 2 moves per element (one when the pack is created and another when it is consumed).

Also provides utilities for compile-time argument manipulation wherever arbitrary lists
of types may be necessary. 

WARNING: Undefined behavior can occur if an lvalue is bound that falls out of scope
before the pack is consumed.  Such values will not have their lifetimes extended in any
way, and it is the user's responsibility to ensure that this is observed at all times.
Generally speaking, ensuring that no packs are returned out of a local context is
enough to satisfy this guarantee.  Typically, this class will be consumed within the
same context in which it was created, or in a downstream one where all of the objects
are still in scope, as a way of enforcing a certain order of operations.  Note that
this guidance does not apply to rvalues and temporaries, which are stored directly
within the pack for its natural lifetime. */
template <typename... Ts>
struct args : impl::ArgsBase<Ts...> {
private:

    template <typename>
    struct _concat;
    template <typename... Us>
    struct _concat<args<Us...>> { using type = args<Ts..., Us...>; };

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

    template <typename out, typename...>
    struct _unique { using type = out; };
    template <typename... Vs, typename U, typename... Us>
    struct _unique<args<Vs...>, U, Us...> {
        template <typename>
        struct helper { using type = args<Vs...>; };
        template <typename U2> requires (!(std::same_as<U2, Us> || ...))
        struct helper<U2> { using type = args<Vs..., U>; };
        using type = _unique<typename helper<U>::type, Us...>::type;
    };

    template <typename>
    struct _to_value;
    template <typename... Us>
    struct _to_value<args<Us...>> {
        template <typename out, typename...>
        struct filter { using type = out; };
        template <typename... Ws, typename V, typename... Vs>
        struct filter<args<Ws...>, V, Vs...> {
            template <typename>
            struct helper { using type = args<Ws...>; };
            template <typename V2>
                requires (!(std::same_as<std::remove_cvref_t<V2>, Ws> || ...))
            struct helper<V2> {
                using type = args<Ws..., std::conditional_t<
                    (std::same_as<
                        std::remove_cvref_t<V2>,
                        std::remove_cvref_t<Vs>
                    > || ...),
                    std::remove_cvref_t<V2>,
                    V2
                >>;
            };
            using type = filter<typename helper<V>::type, Vs...>::type;
        };
        using type = filter<args<>, Us...>::type;
    };

    template <typename out, size_t I>
    struct _get_base { using type = out; };
    template <typename... Us, size_t I> requires (I < sizeof...(Ts))
    struct _get_base<impl::ArgsBase<Us...>, I> {
        using type = _get_base<
            impl::ArgsBase<Us..., unpack_type<I, Ts...>>,
            I + 1
        >::type;
    };
    template <size_t I> requires (I < sizeof...(Ts))
    using get_base = _get_base<impl::ArgsBase<>, I>::type;

    template <size_t I> requires (I < sizeof...(Ts))
    decltype(auto) forward() {
        if constexpr (std::is_lvalue_reference_v<unpack_type<I, Ts...>>) {
            return get_base<I>::value;
        } else {
            return std::move(get_base<I>::value);
        }
    }

public:
    /* The total number of arguments being stored. */
    [[nodiscard]] static constexpr size_t size() noexcept {
        return sizeof...(Ts);
    }
    [[nodiscard]] static constexpr bool empty() noexcept {
        return sizeof...(Ts) == 0;
    }

    /// TODO: eventually, delete ::n in favor of ::size().

    static constexpr size_t n = sizeof...(Ts);
    template <typename T>
    static constexpr size_t index_of = bertrand::index_of<T, Ts...>;
    template <typename T>
    static constexpr bool contains = index_of<T> != n;

    /* Evaluate a control structure's `::enable` state by inserting this pack's
    template parameters. */
    template <template <typename...> class Control>
    static constexpr bool enable = Control<Ts...>::enable;

    /* Evaluate a control structure's `::type` state by inserting this pack's
    template parameters, assuming they are valid. */
    template <template <typename...> class Control> requires (enable<Control>)
    using type = Control<Ts...>::type;

    /* Get the type at index I. */
    template <size_t I> requires (I < n)
    using at = unpack_type<I, Ts...>;

    /* Get a new pack with the type appended. */
    template <typename T>
    using append = args<Ts..., T>;

    /* Get a new pack that combines the contents of this pack with another. */
    template <is_args T>
    using concat = _concat<T>::type;

    /* Get a pack of packs containing all unique permutations of the types in this
    parameter pack and all others, returning their Cartesian product.  */
    template <is_args... packs> requires (n > 0 && ((packs::n > 0) && ...))
    using product = _product<packs...>::type;

    /* Get a new pack with exact duplicates filtered out, accounting for cvref
    qualifications. */
    using unique = _unique<args<>, Ts...>::type;

    /* Get a new pack with duplicates filtered out, replacing any types that differ
    only in cvref qualifications with an unqualified equivalent, thereby forcing a
    copy/move. */
    using to_value = _to_value<unique>::type;

    template <std::convertible_to<Ts>... Us>
    args(Us&&... args) : impl::ArgsBase<Ts...>(
        std::forward<Us>(args)...
    ) {}

    args(args&&) = default;

    args(const args&) = delete;
    args& operator=(const args&) = delete;
    args& operator=(args&&) = delete;

    /* Get the argument at index I. */
    template <size_t I> requires (I < size())
    [[nodiscard]] decltype(auto) get() && {
        if constexpr (std::is_lvalue_reference_v<unpack_type<I, Ts...>>) {
            return get_base<I>::value;
        } else {
            return std::move(get_base<I>::value);
        }
    }

    /* Calling a pack as an rvalue will perfectly forward the input arguments to an
    input function that is templated to accept them. */
    template <typename Func>
        requires (!(std::is_void_v<Ts> || ...) && std::is_invocable_v<Func, Ts...>)
    decltype(auto) operator()(Func&& func) && {
        return [&]<size_t... Is>(std::index_sequence<Is...>) {
            return func(forward<Is>()...);
        }(std::index_sequence_for<Ts...>{});
    }
};


template <typename... Ts>
args(Ts&&...) -> args<Ts...>;


}  // namespace bertrand


namespace std {

    /* Specializing `std::tuple_size` allows args packs to be decomposed using
    structured bindings. */
    template <bertrand::is_args T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    /* Specializing `std::tuple_element` allows args packs to be decomposed using
    structured bindings. */
    template <size_t I, bertrand::is_args T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = std::remove_cvref_t<T>::template at<I>;
    };

    /* `std::get<I>(args)` extracts the I-th argument from the pack. */
    template <size_t I, bertrand::is_args T>
        requires (
            !std::is_lvalue_reference_v<T> &&
            I < std::remove_cvref_t<T>::size()
        )
    [[nodiscard]] constexpr decltype(auto) get(T&& args) noexcept {
        return std::forward<T>(args).template get<I>();
    }

}


namespace bertrand {


template <typename T>
concept is_chain = impl::_is_chain<std::remove_cvref_t<T>>;


/* A higher-order function that merges a sequence of component functions into a single
operation.  When called, the chain will evaluate the first function with the input
arguments, then pass the result to the next function, and so on, until the final result
is returned. */
template <typename F, typename... Fs>
struct chain : impl::chain_tag {
private:
    std::remove_cvref_t<F> func;

public:
    /* The number of component functions in the chain. */
    [[nodiscard]] static constexpr size_t size() noexcept {
        return 1;
    }

    /* Get the unqualified type of the component function at index I. */
    template <size_t I> requires (I < size())
    using at = std::remove_cvref_t<F>;

    constexpr chain(F func) : func(std::forward<F>(func)) {}

    /* Get the component function at index I. */
    template <size_t I> requires (I < size())
    [[nodiscard]] constexpr decltype(auto) get(this auto&& self) {
        return std::forward<decltype(self)>(self).func;
    }

    /* Invoke the function chain. */
    template <typename... A> requires (std::is_invocable_v<F, A...>)
    constexpr decltype(auto) operator()(this auto&& self, A&&... args) {
        return std::forward<decltype(self)>(self).func(std::forward<A>(args)...);
    }
};


template <typename F1, typename F2, typename... Fs>
struct chain<F1, F2, Fs...> : chain<F2, Fs...> {
private:
    std::remove_cvref_t<F1> func;

    template <size_t I>
    struct _at { using type = chain<F2, Fs...>::template at<I - 1>; };
    template <>
    struct _at<0> { using type = std::remove_cvref_t<F1>; };

public:
    /* The number of component functions in the chain. */
    [[nodiscard]] static constexpr size_t size() noexcept {
        return chain<F2, Fs...>::size() + 1;
    }

    /* Get the unqualified type of the component function at index I. */
    template <size_t I> requires (I < size())
    using at = _at<I>::type;

    constexpr chain(F1 first, F2 next, Fs... rest) :
        chain<F2, Fs...>(std::forward<F2>(next), std::forward<Fs>(rest)...),
        func(std::forward<F1>(func))
    {}

    /* Get the component function at index I. */
    template <size_t I> requires (I < size())
    [[nodiscard]] constexpr decltype(auto) get(this auto&& self) {
        if constexpr (I == 0) {
            return std::forward<decltype(self)>(self).func;
        } else {
            using parent = qualify<chain<F2, Fs...>, decltype(self)>;
            return static_cast<parent>(self).template get<I - 1>();
        }
    }

    /* Invoke the function chain. */
    template <typename... A> requires (std::is_invocable_v<F1, A...>)
    constexpr decltype(auto) operator()(this auto&& self, A&&... args) {
        using parent = qualify<chain<F2, Fs...>, decltype(self)>;
        return static_cast<parent>(self)(
            std::forward<decltype(self)>(self).func(std::forward<A>(args)...)
        );
    }
};


template <typename F1, typename... Fs>
chain(F1&&, Fs&&...) -> chain<F1, Fs...>;


template <is_chain Self, is_chain Next>
[[nodiscard]] constexpr decltype(auto) operator>>(Self&& self, Next&& next) {
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


template <is_chain Self, typename Next>
[[nodiscard]] constexpr decltype(auto) operator>>(Self&& self, Next&& next) {
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


template <typename Prev, is_chain Self>
[[nodiscard]] constexpr decltype(auto) operator>>(Prev&& prev, Self&& self) {
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


}


namespace std {


    /* Specializing `std::tuple_size` allows function chains to be decomposed using
    structured bindings. */
    template <bertrand::is_chain T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    /* Specializing `std::tuple_element` allows function chains to be decomposed using
    structured bindings. */
    template <size_t I, bertrand::is_chain T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    /* `std::get<I>(chain)` extracts the I-th function in the chain. */
    template <size_t I, bertrand::is_chain T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr decltype(auto) get(T&& chain) {
        return std::forward<T>(chain).template get<I>();
    }

}


#endif  // BERTRAND_COMMON_H
