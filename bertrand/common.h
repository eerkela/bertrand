#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H

#include <cstddef>
#include <concepts>


namespace bertrand {
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

}


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


/* Compute the next power of two greater than or equal to a given value. */
template <std::unsigned_integral T>
constexpr T next_power_of_two(T n) noexcept {
    --n;
    for (size_t i = 1, bits = sizeof(T) * 8; i < bits; i <<= 1) {
        n |= (n >> i);
    }
    return ++n;
}


/* Deterministic Miller-Rabin primality test.  Avoids random numbers so the test can be
computed at compile time if necessary. */
template <std::integral T>
constexpr bool is_prime(T n) noexcept {
    if (!(n & 1)) {
        return n == 2;
    } else if (n < 2) {
        return false;
    }

    /// TODO: can use if consteval to optimize this for both compile and runtime

    T a = n - 1;
    T b = 0;
    while (!(a & 1)) {
        a >>= 1;
        ++b;
    }

    // deterministic set of bases for n < 2^64
    constexpr T bases[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (int i = 0, end = sizeof(bases) / sizeof(T); i < end; ++i) {
        T base = bases[i];
        if (base >= n) {
            break;  // only test bases smaller than n
        }

        T x = exp_mod(base, a, n);
        if (x == 1 || x == n - 1) {
            continue;
        }

        bool composite = true;
        for (T c = 1; c < b; ++c) {
            x = mul_mod(x, x, n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) {
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


}


#endif  // BERTRAND_COMMON_H
