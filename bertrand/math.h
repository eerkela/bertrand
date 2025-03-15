#ifndef BERTRAND_MATH_H
#define BERTRAND_MATH_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


namespace impl {

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

    namespace divide {

        template <typename L, typename R>
        struct true_ {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(!DEBUG && noexcept(lhs / rhs))
                requires(requires{ lhs / rhs;})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return lhs / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct true_<L, R> {
            static constexpr decltype(auto) operator()(L lhs, R rhs)
                noexcept(!DEBUG && noexcept(static_cast<double>(lhs) / rhs))
                requires(requires{static_cast<double>(lhs) / rhs;})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return static_cast<double>(lhs) / rhs;
            }
        };

        template <typename L, typename R>
        struct floor {
            template <typename L2, typename R2>
            static constexpr decltype(auto) operator()(const L2& lhs, const R2& rhs)
                noexcept(!DEBUG && noexcept(std::floor(lhs / rhs)))
                requires(requires{std::floor(lhs / rhs);})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return std::floor(lhs / rhs);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct floor<L, R> {
            static constexpr decltype(auto) operator()(L lhs, R rhs)
                noexcept(noexcept(lhs / rhs))
                requires(requires{lhs / rhs;})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return lhs / rhs;
            }
        };

        template <meta::signed_integer L, meta::signed_integer R>
        struct floor<L, R> {
            static constexpr decltype(auto) operator()(L lhs, R rhs)
                noexcept(
                    !DEBUG &&
                    noexcept(lhs / rhs - ((lhs < 0) ^ (rhs < 0) && (lhs % rhs)))
                )
                requires(requires{lhs / rhs - ((lhs < 0) ^ (rhs < 0) && (lhs % rhs));})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return lhs / rhs - ((lhs < 0) ^ (rhs < 0) && (lhs % rhs));
            }
        };

        template <typename L, typename R>
        struct ceil {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(!DEBUG && noexcept(std::ceil(lhs / rhs)))
                requires(requires{std::ceil(lhs / rhs);})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return std::ceil(lhs / rhs);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct ceil<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(!DEBUG && noexcept(lhs / rhs + ((lhs % rhs) != 0)))
                requires(requires{lhs / rhs + ((lhs % rhs) != 0);})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return lhs / rhs + ((lhs % rhs) != 0);
            }
        };

        template <meta::signed_integer L, meta::signed_integer R>
        struct ceil<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(
                    !DEBUG &&
                    noexcept(lhs / rhs + (((lhs < 0) == (rhs < 0)) && ((lhs % rhs) != 0)))
                )
                requires(requires {
                    lhs / rhs + (((lhs < 0) == (rhs < 0)) && ((lhs % rhs) != 0));
                })
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return lhs / rhs + (((lhs < 0) == (rhs < 0)) && ((lhs % rhs) != 0));
            }
        };

        template <typename L, typename R>
        struct down {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept((lhs < 0) ^ (rhs < 0) ?
                    divide::ceil<L, R>{}(lhs, rhs) :
                    divide::floor<L, R>{}(lhs, rhs))
                )
                requires(requires{(lhs < 0) ^ (rhs < 0) ?
                    divide::ceil<L, R>{}(lhs, rhs) :
                    divide::floor<L, R>{}(lhs, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    divide::ceil<L, R>{}(lhs, rhs) :
                    divide::floor<L, R>{}(lhs, rhs);
            }
        };

        template <typename L, typename R>
        struct up {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept((lhs < 0) ^ (rhs < 0) ?
                    divide::floor<L, R>{}(lhs, rhs) :
                    divide::ceil<L, R>{}(lhs, rhs))
                )
                requires(requires{(lhs < 0) ^ (rhs < 0) ?
                    divide::floor<L, R>{}(lhs, rhs) :
                    divide::ceil<L, R>{}(lhs, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    divide::floor<L, R>{}(lhs, rhs) :
                    divide::ceil<L, R>{}(lhs, rhs);
            }
        };

        template <typename L, typename R>
        struct half_floor {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(ceil<L, R>{}(lhs - rhs / 2, rhs)))
                requires(requires{ceil<L, R>{}(lhs - rhs / 2, rhs);})
            {
                return ceil<L, R>{}(lhs - rhs / 2, rhs);
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_floor<L, R> {
            static constexpr decltype(auto) operator()(L lhs, R rhs)
                noexcept(noexcept(
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int sign = (a != b) - (a == b);
                        return (lhs + sign * ((rhs + a - b) / 2)) / rhs;
                    }(lhs, rhs)
                ))
                requires(requires{
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int sign = (a != b) - (a == b);
                        return (lhs + sign * ((rhs + a - b) / 2)) / rhs;
                    }(lhs, rhs);
                })
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                /*   sign  a  b    sign * ((rhs + a - b) / 2)
                 *    1    1  0  =   1  * ((rhs + 1 - 0) / 2)  =   ((rhs + 1) / 2)
                 *   -1    1  1  =  -1  * ((rhs + 1 - 1) / 2)  =  -((rhs    ) / 2)
                 *    1    0  1  =   1  * ((rhs + 0 - 1) / 2)  =   ((rhs - 1) / 2)
                 *   -1    0  0  =  -1  * ((rhs + 0 - 0) / 2)  =  -((rhs    ) / 2)
                 */
                bool a = lhs < 0;
                bool b = rhs > 0;
                int sign = (a != b) - (a == b);
                return (lhs + sign * ((rhs + a - b) / 2)) / rhs;
            }
        };

        template <typename L, typename R>
        struct half_ceil {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(floor<L, R>{}(lhs + rhs / 2, rhs)))
                requires(requires{floor<L, R>{}(lhs + rhs / 2, rhs);})
            {
                return floor<L, R>{}(lhs + rhs / 2, rhs);
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_ceil<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int sign = (a != b) - (a == b);
                        return (lhs + sign * ((rhs + ~(a - b)) / 2)) / rhs;
                    }(lhs, rhs)
                ))
                requires(requires{
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int sign = (a != b) - (a == b);
                        return (lhs + sign * ((rhs + ~(a - b)) / 2)) / rhs;
                    }(lhs, rhs);
                })
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                /*   sign  a  b    sign * ((rhs + ~(a - b)) / 2)
                 *    1    1  0  =   1  * ((rhs + ~(1 - 0)) / 2)  =   ((rhs - 2) / 2)
                 *   -1    1  1  =  -1  * ((rhs + ~(1 - 1)) / 2)  =  -((rhs - 1) / 2)
                 *    1    0  1  =   1  * ((rhs + ~(0 - 1)) / 2)  =   ((rhs    ) / 2)
                 *   -1    0  0  =  -1  * ((rhs + ~(0 - 0)) / 2)  =  -((rhs - 1) / 2)
                 */
                bool a = lhs < 0;
                bool b = rhs > 0;
                int sign = (a != b) - (a == b);
                return (lhs + sign * ((rhs + ~(a - b)) / 2)) / rhs;
            }
        };

        template <typename L, typename R>
        struct half_down {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    ((lhs < 0) ^ (rhs < 0)) ?
                        divide::ceil<L, R>{}(lhs - rhs / 2, rhs) :
                        divide::floor<L, R>{}(lhs + rhs / 2, rhs)
                ))
                requires(requires{
                    ((lhs < 0) ^ (rhs < 0)) ?
                        divide::ceil<L, R>{}(lhs - rhs / 2, rhs) :
                        divide::floor<L, R>{}(lhs + rhs / 2, rhs);
                })
            {
                return ((lhs < 0) ^ (rhs < 0)) ?
                    divide::ceil<L, R>{}(lhs - rhs / 2, rhs) :
                    divide::floor<L, R>{}(lhs + rhs / 2, rhs);
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_down<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int c = a - b;
                        bool d = a != b;
                        bool e = a == b;
                        return (lhs + (d - e) * ((rhs + c * d + ~c * e) / 2)) / rhs;
                    }(lhs, rhs)
                ))
                requires(requires{
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int c = a - b;
                        bool d = a != b;
                        bool e = a == b;
                        return (lhs + (d - e) * ((rhs + c * d + ~c * e) / 2)) / rhs;
                    }(lhs, rhs);
                })
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                bool a = lhs < 0;
                bool b = rhs > 0;
                int c = a - b;
                bool d = a != b;
                bool e = a == b;
                return (lhs + (d - e) * ((rhs + c * d + ~c * e) / 2)) / rhs;
            }
        };

        template <typename L, typename R>
        struct half_up {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    ((lhs < 0) ^ (rhs < 0)) ?
                        divide::floor<L, R>{}(lhs + rhs / 2, rhs) :
                        divide::ceil<L, R>{}(lhs - rhs / 2, rhs)
                ))
                requires(requires{
                    ((lhs < 0) ^ (rhs < 0)) ?
                        divide::floor<L, R>{}(lhs + rhs / 2, rhs) :
                        divide::ceil<L, R>{}(lhs - rhs / 2, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    divide::floor<L, R>{}(lhs + rhs / 2, rhs) :
                    divide::ceil<L, R>{}(lhs - rhs / 2, rhs);
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_up<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int c = a - b;
                        bool d = a != b;
                        bool e = a == b;
                        return (lhs + (d - e) * ((rhs + ~c * d + c * e) / 2)) / rhs;
                    }(lhs, rhs)
                ))
                requires(requires{
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int c = a - b;
                        bool d = a != b;
                        bool e = a == b;
                        return (lhs + (d - e) * ((rhs + ~c * d + c * e) / 2)) / rhs;
                    }(lhs, rhs);
                })
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                bool a = lhs < 0;
                bool b = rhs > 0;
                int c = a - b;
                bool d = a != b;
                bool e = a == b;
                return (lhs + (d - e) * ((rhs + ~c * d + c * e) / 2)) / rhs;
            }
        };

        template <typename L, typename R>
        struct half_even {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    (lhs - rhs / 2) / rhs % 2 ?
                        ceil<L, R>{}(lhs, rhs) :
                        floor<L, R>{}(lhs, rhs)
                ))
                requires(requires{
                    (lhs - rhs / 2) / rhs % 2 ?
                        ceil<L, R>{}(lhs, rhs) :
                        floor<L, R>{}(lhs, rhs);
                })
            {
                return (lhs - rhs / 2) / rhs % 2 ?
                    ceil<L, R>{}(lhs, rhs) :
                    floor<L, R>{}(lhs, rhs);
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_even<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int c = a - b;
                        int sign = (a != b) - (a == b);
                        bool odd = ((lhs - rhs / 2) / rhs) % 2;
                        return (lhs + sign * ((rhs + c * (!odd) + ~c * odd) / 2)) / rhs;
                    }(lhs, rhs)
                ))
                requires(requires{
                    [](const L& lhs, const R& rhs) {
                        if constexpr (DEBUG) {
                            if (rhs == 0) {
                                throw ZeroDivisionError();
                            }
                        }
                        bool a = lhs < 0;
                        bool b = rhs > 0;
                        int c = a - b;
                        int sign = (a != b) - (a == b);
                        bool odd = ((lhs - rhs / 2) / rhs) % 2;
                        return (lhs + sign * ((rhs + c * (!odd) + ~c * odd) / 2)) / rhs;
                    }(lhs, rhs);
                })
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                bool a = lhs < 0;
                bool b = rhs > 0;
                int c = a - b;
                int sign = (a != b) - (a == b);
                bool odd = ((lhs - rhs / 2) / rhs) % 2;
                return (lhs + sign * ((rhs + c * (!odd) + ~c * odd) / 2)) / rhs;
            }
        };

    }  // namespace divide

    namespace modulo {

        template <typename L, typename R>
        struct true_ {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(lhs - (divide::true_<L, R>{}(lhs, rhs) * rhs)))
                requires (requires{lhs - (divide::true_<L, R>{}(lhs, rhs) * rhs);})
            {
                return lhs - (divide::true_<L, R>{}(lhs, rhs) * rhs);
            }
        };

        template <typename L, typename R>
        struct floor {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(!DEBUG && noexcept((lhs % rhs) + rhs * ((lhs < 0) ^ (rhs < 0))))
                requires(requires{(lhs % rhs) + rhs * ((lhs < 0) ^ (rhs < 0));})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return lhs % rhs + rhs * ((lhs < 0) ^ (rhs < 0));
            }
        };

        template <meta::floating L, meta::floating R>
        struct floor<L, R> {
            static constexpr decltype(auto) operator()(L lhs, R rhs)
                noexcept(!DEBUG && noexcept(std::fmod(lhs, rhs) + rhs * ((lhs < 0) ^ (rhs < 0))))
                requires(requires{std::fmod(lhs, rhs) + rhs * ((lhs < 0) ^ (rhs < 0));})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return std::fmod(lhs, rhs) + rhs * ((lhs < 0) ^ (rhs < 0));
            }
        };

        template <typename L, typename R>
        struct ceil {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(!DEBUG && noexcept((lhs % rhs) + rhs * ((lhs < 0) == (rhs < 0))))
                requires(requires{lhs % rhs + rhs * ((lhs < 0) == (rhs < 0));})
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return lhs % rhs - rhs * ((lhs < 0) == (rhs < 0));
            }
        };

        template <meta::floating L, meta::floating R>
        struct ceil<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(
                    !DEBUG &&
                    noexcept(std::fmod(lhs, rhs) - rhs * ((lhs < 0) == (rhs < 0)))
                )
                requires(requires{
                    std::fmod(lhs, rhs) - rhs * ((lhs < 0) == (rhs < 0));
                })
            {
                if constexpr (DEBUG) {
                    if (rhs == 0) {
                        throw ZeroDivisionError();
                    }
                }
                return std::fmod(lhs, rhs) - rhs * ((lhs < 0) == (rhs < 0));
            }
        };

        template <typename L, typename R>
        struct down {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept((lhs < 0) ^ (rhs < 0) ?
                    modulo::ceil<L, R>{}(lhs, rhs) :
                    modulo::floor<L, R>{}(lhs, rhs))
                )
                requires(requires{(lhs < 0) ^ (rhs < 0) ?
                    modulo::ceil<L, R>{}(lhs, rhs) :
                    modulo::floor<L, R>{}(lhs, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    modulo::ceil<L, R>{}(lhs, rhs) :
                    modulo::floor<L, R>{}(lhs, rhs);
            }
        };

        template <typename L, typename R>
        struct up {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept((lhs < 0) ^ (rhs < 0) ?
                    modulo::floor<L, R>{}(lhs, rhs) :
                    modulo::ceil<L, R>{}(lhs, rhs))
                )
                requires(requires{(lhs < 0) ^ (rhs < 0) ?
                    modulo::floor<L, R>{}(lhs, rhs) :
                    modulo::ceil<L, R>{}(lhs, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    modulo::floor<L, R>{}(lhs, rhs) :
                    modulo::ceil<L, R>{}(lhs, rhs);
            }
        };

        template <typename L, typename R>
        struct half_floor {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(lhs - divide::half_floor<L, R>{}(lhs, rhs) * rhs))
                requires(requires{lhs - divide::half_floor<L, R>{}(lhs, rhs) * rhs;})
            {
                return lhs - divide::half_floor<L, R>{}(lhs, rhs) * rhs;
            }
        };

        template <typename L, typename R>
        struct half_ceil {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(lhs - divide::half_ceil<L, R>{}(lhs, rhs) * rhs))
                requires(requires{lhs - divide::half_ceil<L, R>{}(lhs, rhs) * rhs;})
            {
                return lhs - divide::half_ceil<L, R>{}(lhs, rhs) * rhs;
            }
        };

        template <typename L, typename R>
        struct half_down {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(lhs - divide::half_down<L, R>{}(lhs, rhs) * rhs))
                requires(requires{lhs - divide::half_down<L, R>{}(lhs, rhs) * rhs;})
            {
                return lhs - divide::half_down<L, R>{}(lhs, rhs) * rhs;
            }
        };

        template <typename L, typename R>
        struct half_up {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(lhs - divide::half_up<L, R>{}(lhs, rhs) * rhs))
                requires(requires{lhs - divide::half_up<L, R>{}(lhs, rhs) * rhs;})
            {
                return lhs - divide::half_up<L, R>{}(lhs, rhs) * rhs;
            }
        };

        template <typename L, typename R>
        struct half_even {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(lhs - divide::half_even<L, R>{}(lhs, rhs) * rhs))
                requires(requires{lhs - divide::half_even<L, R>{}(lhs, rhs) * rhs;})
            {
                return lhs - divide::half_even<L, R>{}(lhs, rhs) * rhs;
            }
        };

    }  // namespace modulo

    namespace divmod {

        template <typename L, typename R>
        struct true_ {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(
                    noexcept(std::make_pair(
                        divide::true_<L, R>{}(lhs, rhs),
                        lhs - (divide::true_<L, R>{}(lhs, rhs) * rhs)
                    ))
                )
                requires(requires {
                    std::make_pair(
                        divide::true_<L, R>{}(lhs, rhs),
                        lhs - (divide::true_<L, R>{}(lhs, rhs) * rhs)
                    );
                })
            {
                auto quotient = divide::true_<L, R>{}(lhs, rhs);
                return std::make_pair(quotient, lhs - (quotient * rhs));
            }
        };

        template <typename L, typename R>
        struct floor {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(
                    noexcept(std::make_pair(
                        divide::floor<L, R>{}(lhs, rhs),
                        modulo::floor<L, R>{}(lhs, rhs)
                    ))
                )
                requires(requires {
                    std::make_pair(
                        divide::floor<L, R>{}(lhs, rhs),
                        modulo::floor<L, R>{}(lhs, rhs)
                    );
                })
            {
                return std::make_pair(
                    divide::floor<L, R>{}(lhs, rhs),
                    modulo::floor<L, R>{}(lhs, rhs)
                );
            }
        };

        template <typename L, typename R>
        struct ceil {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(
                    noexcept(std::make_pair(
                        divide::ceil<L, R>{}(lhs, rhs),
                        modulo::ceil<L, R>{}(lhs, rhs)
                    ))
                )
                requires(requires {
                    std::make_pair(
                        divide::ceil<L, R>{}(lhs, rhs),
                        modulo::ceil<L, R>{}(lhs, rhs)
                    );
                })
            {
                return std::make_pair(
                    divide::ceil<L, R>{}(lhs, rhs),
                    modulo::ceil<L, R>{}(lhs, rhs)
                );
            }
        };

        template <typename L, typename R>
        struct down {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept((lhs < 0) ^ (rhs < 0) ?
                    divmod::ceil<L, R>{}(lhs, rhs) :
                    divmod::floor<L, R>{}(lhs, rhs))
                )
                requires(requires{(lhs < 0) ^ (rhs < 0) ?
                    divmod::ceil<L, R>{}(lhs, rhs) :
                    divmod::floor<L, R>{}(lhs, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    divmod::ceil<L, R>{}(lhs, rhs) :
                    divmod::floor<L, R>{}(lhs, rhs);
            }
        };

        template <typename L, typename R>
        struct up {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept((lhs < 0) ^ (rhs < 0) ?
                    divmod::floor<L, R>{}(lhs, rhs) :
                    divmod::ceil<L, R>{}(lhs, rhs))
                )
                requires(requires{(lhs < 0) ^ (rhs < 0) ?
                    divmod::floor<L, R>{}(lhs, rhs) :
                    divmod::ceil<L, R>{}(lhs, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    divmod::floor<L, R>{}(lhs, rhs) :
                    divmod::ceil<L, R>{}(lhs, rhs);
            }
        };

        template <typename L, typename R>
        struct half_floor {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    std::make_pair(
                        divide::half_floor<L, R>{}(lhs, rhs),
                        lhs - (divide::half_floor<L, R>{}(lhs, rhs) * rhs)
                    )
                ))
                requires(requires{
                    std::make_pair(
                        divide::half_floor<L, R>{}(lhs, rhs),
                        lhs - (divide::half_floor<L, R>{}(lhs, rhs) * rhs)
                    );
                })
            {
                auto quotient = divide::half_floor<L, R>{}(lhs, rhs);
                return std::make_pair(quotient, lhs - (quotient * rhs));
            }
        };

        template <typename L, typename R>
        struct half_ceil {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    std::make_pair(
                        divide::half_ceil<L, R>{}(lhs, rhs),
                        lhs - (divide::half_ceil<L, R>{}(lhs, rhs) * rhs)
                    )
                ))
                requires(requires{
                    std::make_pair(
                        divide::half_ceil<L, R>{}(lhs, rhs),
                        lhs - (divide::half_ceil<L, R>{}(lhs, rhs) * rhs)
                    );
                })
            {
                auto quotient = divide::half_ceil<L, R>{}(lhs, rhs);
                return std::make_pair(quotient, lhs - (quotient * rhs));
            }
        };

        template <typename L, typename R>
        struct half_down {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    std::make_pair(
                        divide::half_down<L, R>{}(lhs, rhs),
                        lhs - (divide::half_down<L, R>{}(lhs, rhs) * rhs)
                    )
                ))
                requires(requires{
                    std::make_pair(
                        divide::half_down<L, R>{}(lhs, rhs),
                        lhs - (divide::half_down<L, R>{}(lhs, rhs) * rhs)
                    );
                })
            {
                auto quotient = divide::half_down<L, R>{}(lhs, rhs);
                return std::make_pair(quotient, lhs - (quotient * rhs));
            }
        };

        template <typename L, typename R>
        struct half_up {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    std::make_pair(
                        divide::half_up<L, R>{}(lhs, rhs),
                        lhs - (divide::half_up<L, R>{}(lhs, rhs) * rhs)
                    )
                ))
                requires(requires{
                    std::make_pair(
                        divide::half_up<L, R>{}(lhs, rhs),
                        lhs - (divide::half_up<L, R>{}(lhs, rhs) * rhs)
                    );
                })
            {
                auto quotient = divide::half_up<L, R>{}(lhs, rhs);
                return std::make_pair(quotient, lhs - (quotient * rhs));
            }
        };

        template <typename L, typename R>
        struct half_even {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(
                    std::make_pair(
                        divide::half_even<L, R>{}(lhs, rhs),
                        lhs - (divide::half_even<L, R>{}(lhs, rhs) * rhs)
                    )
                ))
                requires(requires{
                    std::make_pair(
                        divide::half_even<L, R>{}(lhs, rhs),
                        lhs - (divide::half_even<L, R>{}(lhs, rhs) * rhs)
                    );
                })
            {
                auto quotient = divide::half_even<L, R>{}(lhs, rhs);
                return std::make_pair(quotient, lhs - (quotient * rhs));
            }
        };

    }  // namespace divmod

    namespace round {

        template <typename T>
        struct true_ {
            static constexpr const T& operator()(const T& obj) noexcept {
                return obj;
            }
        };

        template <typename T>
        struct floor {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(std::floor(obj)))
                requires(requires{std::floor(obj);})
            {
                return std::floor(obj);
            }
        };

        template <typename T>
        struct ceil {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(std::ceil(obj))
                requires(requires{std::ceil(obj);})
            {
                return std::ceil(obj);
            }
        };

        template <typename T>
        struct down {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(std::trunc(obj)))
                requires(requires{std::trunc(obj);})
            {
                return std::trunc(obj);
            }
        };

        template <typename T>
        struct up {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(obj > 0 ? std::ceil(obj) : std::floor(obj)))
                requires(requires{obj > 0 ? std::ceil(obj) : std::floor(obj);})
            {
                return obj > 0 ? std::ceil(obj) : std::floor(obj);
            }
        };

        template <typename T>
        struct half_floor {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(ceil<T>{}(obj - 0.5)))
                requires(requires{ceil<T>{}(obj - 0.5);})
            {
                return ceil<T>{}(obj - 0.5);
            }
        };

        template <typename T>
        struct half_ceil {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(floor<T>{}(obj + 0.5)))
                requires(requires{floor<T>{}(obj + 0.5);})
            {
                return floor<T>{}(obj + 0.5);
            }
        };

        template <typename T>
        struct half_down {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(obj > 0 ? half_floor<T>{}(obj) : half_ceil<T>{}(obj)))
                requires(requires{obj > 0 ? half_floor<T>{}(obj) : half_ceil<T>{}(obj);})
            {
                return obj > 0 ? half_floor<T>{}(obj) : half_ceil<T>{}(obj);
            }
        };

        template <typename T>
        struct half_up {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(std::round(obj)))
                requires(requires{std::round(obj);})
            {
                return std::round(obj);
            }
        };

        template <typename T>
        struct half_even {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(
                    [](const T& obj) {
                        T whole, fract;
                        fract = std::modf(obj, &whole);
                        return std::abs(fract) == 0.5 ?
                            whole + std::fmod(whole, 2) :
                            std::floor(obj);
                    }(obj)
                ))
                requires(requires{
                    [](const T& obj) {
                        T whole, fract;
                        fract = std::modf(obj, &whole);
                        return std::abs(fract) == 0.5 ?
                            whole + std::fmod(whole, 2) :
                            std::floor(obj);
                    }(obj);
                })
            {
                T whole, fract;
                fract = std::modf(obj, &whole);
                return std::abs(fract) == 0.5 ?
                    whole + std::fmod(whole, 2) :  // fmod evaluates negative if whole < 0
                    std::floor(obj);
            }
        };

    }  // namespace round

}  // namespace impl


/* Hash an arbitrary value.  Equivalent to calling `std::hash<T>{}(...)`, but without
explicitly specializating `std::hash`. */
template <meta::hashable T>
[[nodiscard]] constexpr auto hash(T&& obj) noexcept(meta::nothrow::hashable<T>) {
    return std::hash<std::decay_t<T>>{}(std::forward<T>(obj));
}


/* A family of division operators with different rounding strategies, in order to
facilitate inter-language communication where conventions may differ. */
struct divide {
    constexpr divide() noexcept = delete;

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::true_<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) true_(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::true_<L, R>{}(lhs, rhs))) {
        return impl::divide::true_<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::floor<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) floor(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::floor<L, R>{}(lhs, rhs))) {
        return impl::divide::floor<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::ceil<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::ceil<L, R>{}(lhs, rhs))) {
        return impl::divide::ceil<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::down<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) down(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::down<L, R>{}(lhs, rhs))) {
        return impl::divide::down<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::up<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) up(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::up<L, R>{}(lhs, rhs))) {
        return impl::divide::up<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::half_floor<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_floor(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::half_floor<L, R>{}(lhs, rhs))) {
        return impl::divide::half_floor<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::half_ceil<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::half_ceil<L, R>{}(lhs, rhs))) {
        return impl::divide::half_ceil<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::half_down<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_down(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::half_down<L, R>{}(lhs, rhs))) {
        return impl::divide::half_down<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::half_up<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_up(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::half_up<L, R>{}(lhs, rhs))) {
        return impl::divide::half_up<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divide::half_even<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_even(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divide::half_even<L, R>{}(lhs, rhs))) {
        return impl::divide::half_even<L, R>{}(lhs, rhs);
    }

};


/* A family of modulus operators with different rounding strategies, in order to
facilitate inter-language communication where conventions may differ. */
struct modulo {
    constexpr modulo() noexcept = delete;

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::true_<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) true_(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::true_<L, R>{}(lhs, rhs))) {
        return impl::modulo::true_<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::floor<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) floor(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::floor<L, R>{}(lhs, rhs))) {
        return impl::modulo::floor<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::ceil<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::ceil<L, R>{}(lhs, rhs))) {
        return impl::modulo::ceil<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::down<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) down(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::down<L, R>{}(lhs, rhs))) {
        return impl::modulo::down<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::up<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) up(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::up<L, R>{}(lhs, rhs))) {
        return impl::modulo::up<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::half_floor<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_floor(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::half_floor<L, R>{}(lhs, rhs))) {
        return impl::modulo::half_floor<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::half_ceil<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::half_ceil<L, R>{}(lhs, rhs))) {
        return impl::modulo::half_ceil<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::half_down<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_down(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::half_down<L, R>{}(lhs, rhs))) {
        return impl::modulo::half_down<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::half_up<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_up(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::half_up<L, R>{}(lhs, rhs))) {
        return impl::modulo::half_up<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::modulo::half_even<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_even(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::modulo::half_even<L, R>{}(lhs, rhs))) {
        return impl::modulo::half_even<L, R>{}(lhs, rhs);
    }

};


/* A family of combined division and modulus operators with different rounding
strategies, in order to facilitate inter-language communication where conventions may
differ. */
struct divmod {
    constexpr divmod() noexcept = delete;

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::true_<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) true_(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::true_<L, R>{}(lhs, rhs))) {
        return impl::divmod::true_<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::floor<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) floor(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::floor<L, R>{}(lhs, rhs))) {
        return impl::divmod::floor<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::ceil<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::ceil<L, R>{}(lhs, rhs))) {
        return impl::divmod::ceil<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::down<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) down(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::down<L, R>{}(lhs, rhs))) {
        return impl::divmod::down<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::up<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) up(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::up<L, R>{}(lhs, rhs))) {
        return impl::divmod::up<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::half_floor<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_floor(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::half_floor<L, R>{}(lhs, rhs))) {
        return impl::divmod::half_floor<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::half_ceil<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::half_ceil<L, R>{}(lhs, rhs))) {
        return impl::divmod::half_ceil<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::half_down<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_down(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::half_down<L, R>{}(lhs, rhs))) {
        return impl::divmod::half_down<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::half_up<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_up(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::half_up<L, R>{}(lhs, rhs))) {
        return impl::divmod::half_up<L, R>{}(lhs, rhs);
    }

    template <typename L, typename R>
        requires (meta::invocable<impl::divmod::half_even<L, R>, const L&, const R&>)
    [[nodiscard]] static constexpr decltype(auto) half_even(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(impl::divmod::half_even<L, R>{}(lhs, rhs))) {
        return impl::divmod::half_even<L, R>{}(lhs, rhs);
    }

};


/* A family of rounding operators with different strategies, in order to facilitate
inter-language communication where conventions may differ. */
struct round {
    constexpr round() noexcept = delete;

    /// TODO: special cases for integer operands



    template <typename T>
        requires (meta::invocable<impl::round::true_<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) true_(const T& obj)
        noexcept(noexcept(impl::round::true_<T>{}(obj)))
    {
        return impl::round::true_<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::floor<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) floor(const T& obj)
        noexcept(noexcept(impl::round::floor<T>{}(obj)))
    {
        return impl::round::floor<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::ceil<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) ceil(const T& obj)
        noexcept(noexcept(impl::round::ceil<T>{}(obj)))
    {
        return impl::round::ceil<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::down<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) down(const T& obj)
        noexcept(noexcept(impl::round::down<T>{}(obj)))
    {
        return impl::round::down<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::up<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) up(const T& obj)
        noexcept(noexcept(impl::round::up<T>{}(obj)))
    {
        return impl::round::up<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::half_floor<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) half_floor(const T& obj)
        noexcept(noexcept(impl::round::half_floor<T>{}(obj)))
    {
        return impl::round::half_floor<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::half_ceil<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) half_ceil(const T& obj)
        noexcept(noexcept(impl::round::half_ceil<T>{}(obj)))
    {
        return impl::round::half_ceil<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::half_down<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) half_down(const T& obj)
        noexcept(noexcept(impl::round::half_down<T>{}(obj)))
    {
        return impl::round::half_down<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::half_up<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) half_up(const T& obj)
        noexcept(noexcept(impl::round::half_up<T>{}(obj)))
    {
        return impl::round::half_up<T>{}(obj);
    }

    template <typename T>
        requires (meta::invocable<impl::round::half_even<T>, const T&>)
    [[nodiscard]] static constexpr decltype(auto) half_even(const T& obj)
        noexcept(noexcept(impl::round::half_even<T>{}(obj)))
    {
        return impl::round::half_even<T>{}(obj);
    }

};


/// TODO: pow(), abs()


}


#endif
