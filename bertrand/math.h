#ifndef BERTRAND_MATH_H
#define BERTRAND_MATH_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


/// TODO: eventually, delete the `wrapper` type in common.h, which removes all
/// the unwrap() calls here.  Then, I should move the Bits<N>, UInt<N>, Int<N>, and
/// Float<E, M> types into common.h, along with their respective concepts, and
/// specialize the math operators on that basis, delegating to equivalent methods.
/// on the underlying types.



/// TODO: maybe all of this should be in a math:: namespace, and impl::math:: holds
/// the customization points.  So:

/// math::abs(x)
/// math::pow(x, y)
/// math::pow(x, y, mod)
/// math::mul(x, y)
/// math::mul(x, y, mod)
/// math::log<b>(x)
/// math::root<b>(x)
/// math::prime::check()
/// math::prime::next()
/// math::prime::prev()
/// math::div(x, y, math::real)
/// math::div(x, y, math::floor)
/// math::div(x, y, math::ceil)
/// math::div(x, y, math::down)
/// math::div(x, y, math::up)
/// math::div(x, y, math::half_floor)
/// math::div(x, y, math::half_ceil)
/// math::div(x, y, math::half_down)
/// math::div(x, y, math::half_up)
/// math::div(x, y, math::half_even)
/// math::div(x, y, math::half_odd)
/// etc. for math::mod(), math::divmod(), math::round()


/// impl::math::abs<T>{}
/// impl::math::pow<L, R>{}
/// impl::math::pow<L, R, M>{}
/// impl::math::div<L, R, math::real>{}
/// etc.



namespace impl {

    template <typename T>
    concept _check_for_zero_division = DEBUG && requires(const T& rhs) {
        { rhs == 0 } -> meta::explicitly_convertible_to<bool>;
    };

    template <_check_for_zero_division T>
    constexpr void check_for_zero_division(const T& rhs) {
        if (rhs == 0) {
            throw ZeroDivisionError();
        }
    }

    template <typename T> requires (!_check_for_zero_division<T>)
    constexpr void check_for_zero_division(const T& rhs) noexcept {}

    template <typename T>
    concept _check_for_negative_exponent = DEBUG && requires(const T& rhs) {
        { rhs < 0 } -> meta::explicitly_convertible_to<bool>;
    };

    template <_check_for_negative_exponent T>
    constexpr void check_for_negative_exponent(const T& rhs) {
        if (rhs < 0) {
            throw ArithmeticError(
                "Negative exponent not supported for modular exponentiation"
            );
        }
    }

    template <typename T> requires (!_check_for_negative_exponent<T>)
    constexpr void check_for_negative_exponent(const T& rhs) noexcept {}

    /// TODO: to fit in with the rest of the operator function objects:
    /// TODO: abs<T> -> Abs<T>
    /// TODO: pow<T> -> Pow<T>
    /// TODO: divide::true_<T> -> Divide::True<T>
    /// TODO: divide::cpp<T> -> Divide::Cpp<T>
    /// TODO: divide::floor<T> -> Divide::Floor<T>
    /// etc.
    /// TODO: Log<L, R>
    /// TODO: Root<L, R>

    /// TODO: note that the other function objects do not have template arguments, and
    /// so these should not either.  That means the customization points either need to
    /// be separate structs, or I have to allow overloading of the operators themselves,
    /// which can become a real problem.  Using separate structs is probably preferable,
    /// but that means I need to think about how best to separate them such that
    /// specializations are easily registered.

    /// TODO: maybe the solution here is to put the customization points in the
    /// namespaces alongside the function objects?  That would make sense for
    /// division, but likely not for the others.



    template <typename T>
    struct abs {
        static constexpr decltype(auto) operator()(const T& obj)
            noexcept(noexcept(std::abs(obj)))
            requires(requires{std::abs(obj);})
        {
            return std::abs(obj);
        }
    };

    template <typename Base, typename Exp, typename Mod = void>
    struct pow {
        static constexpr Base operator()(const Base& base, const Exp& exp)
            noexcept(noexcept(std::pow(base, exp)))
            requires(requires{
                { std::pow(Base(base), Base(exp)) } -> meta::convertible_to<Base>;
            })
        {
            return std::pow(Base(base), Base(exp));
        }

        static constexpr Base operator()(const Base& base, const Exp& exp, const Mod& mod)
            noexcept(noexcept(
                [](const Base& base, const Exp& exp, const Mod& mod) {
                    if (exp < 0) {
                        return Base(1) / operator()(base, -exp, mod);
                    }
                    Base result = 1;
                    Base y = std::fmod(base, mod);
                    Exp whole;
                    Exp fract = std::modf(exp, &whole);
                    while (whole >= 1) {
                        if (std::fmod(whole, 2.0) >= 1) {
                            result = std::fmod(result * y, mod);
                        }
                        y = std::fmod(y * y, mod);
                        whole = std::floor(whole / 2);
                    }
                    return fract == 0 ? result : std::fmod(result * std::pow(y, fract), mod);
                }(base, exp, mod)
            ))
            requires(requires{
                [](const Base& base, const Exp& exp, const Mod& mod) {
                    if (exp < 0) {
                        return Base(1) / operator()(base, -exp, mod);
                    }
                    Base result = 1;
                    Base y = std::fmod(base, mod);
                    Exp whole;
                    Exp fract = std::modf(exp, &whole);
                    while (whole >= 1) {
                        if (std::fmod(whole, 2.0) >= 1) {
                            result = std::fmod(result * y, mod);
                        }
                        y = std::fmod(y * y, mod);
                        whole = std::floor(whole / 2);
                    }
                    return fract == 0 ? result : std::fmod(result * std::pow(y, fract), mod);
                }(base, exp, mod);
            })
        {
            Base result = 1;
            Base y = std::fmod(base, mod);
            Exp whole;
            Exp fract = std::modf(exp, &whole);
            while (whole >= 1) {
                if (std::fmod(whole, 2.0) >= 1) {
                    result = std::fmod(result * y, mod);
                }
                y = std::fmod(y * y, mod);
                whole = std::floor(whole / 2);
            }
            return fract == 0 ? result : std::fmod(result * std::pow(y, fract), mod);
        }
    };

    template <typename Base, meta::integer Exp>
    struct pow<Base, Exp> {
        static constexpr Base operator()(const Base& base, const Exp& exp) noexcept {
            if (exp < 0) {
                return Base(1) / operator()(base, -exp);
            }
            Base result = 1;
            Base y = base;
            Exp e = exp;
            while (e >= 1) {
                if (e % 2) {
                    result *= y;
                }
                y *= y;
                e /= 2;
            }
            return result;
        }
    };

    template <meta::integer Base, meta::integer Exp>
    struct pow<Base, Exp> {
        static constexpr Base operator()(const Base& base, const Exp& exp) noexcept {
            Base result = exp >= 0;
            Base y = base;
            Exp e = exp;
            while (e >= 1) {
                if (e % 2) {
                    result *= y;
                }
                y *= y;
                e /= 2;
            }
            return result;
        }
    };

    template <meta::integer Base, meta::integer Exp, meta::integer Mod>
    struct pow<Base, Exp, Mod> {
        static constexpr Base operator()(
            const Base& base,
            const Exp& exp,
            const Mod& mod
        ) noexcept {
            Base result = exp >= 0;
            Base y = base % mod;
            Exp e = exp;
            while (e >= 1) {
                if (e % 2) {
                    result = (result * y) % mod;
                }
                y = (y * y) % mod;
                e /= 2;
            }
            return result;
        }
    };

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

    /* Returns true if `n` is a power of two (incl. 1) or zero.  False otherwise. */
    template <meta::unsigned_integer T>
    constexpr bool is_power2(T n) noexcept {
        return (n & (n - 1)) == 0;
    }

    /* Compute the next power of two greater than or equal to a given value. */
    template <meta::unsigned_integer T>
    constexpr T next_power2(T n) noexcept {
        --n;
        for (size_t i = 1, bits = sizeof(T) * 8; i < bits; i <<= 1) {
            n |= (n >> i);
        }
        return ++n;
    }

    /* Get the floored log base 2 of an unsigned integer.  Inputs equal to zero result
    in undefined behavior. */
    template <meta::unsigned_integer T>
    constexpr size_t log2(T n) noexcept {
        return sizeof(T) * 8 - 1 - std::countl_zero(n);
    }

    /* A fast modulo operator that works for any b power of two. */
    template <meta::unsigned_integer T>
    constexpr T mod2(T a, T b) noexcept {
        return a & (b - 1);
    }

    /// TODO: maybe if I have adequate constexpr math functions, I can use a
    /// probabilistic Miller-Rabin test that adapts to arbitrary-precision arithmetic.

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
            T x = pow<T, T, T>{}(a, d, n);
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

    /// TODO: this can be internal to Bits<N>, which is a generalization of helpers
    /// like these.

    /* Reverse the order of bits in a byte. */
    template <meta::unsigned_integer T> requires (sizeof(T) == 1)
    constexpr T bit_reverse(T n) noexcept {
        n = ((n & 0b1111'0000U) >> 4) | ((n & 0b0000'1111U) << 4);
        n = ((n & 0b1100'1100U) >> 2) | ((n & 0b0011'0011U) << 2);
        n = ((n & 0b1010'1010U) >> 1) | ((n & 0b0101'0101U) << 1);
        return n;
    }

    /* Reverse the order of bits in a 16-bit integer. */
    template <meta::unsigned_integer T> requires (sizeof(T) == 2)
    constexpr T bit_reverse(T n) noexcept {
        n = ((n & 0b1111'1111'0000'0000U)) >> 8 | ((n & 0b0000'0000'1111'1111U)) << 8;
        n = ((n & 0b1111'0000'1111'0000U)) >> 4 | ((n & 0b0000'1111'0000'1111U)) << 4;
        n = ((n & 0b1100'1100'1100'1100U)) >> 2 | ((n & 0b0011'0011'0011'0011U)) << 2;
        n = ((n & 0b1010'1010'1010'1010U)) >> 1 | ((n & 0b0101'0101'0101'0101U)) << 1;
        return n;
    }

    /* Reverse the order of bits in a 32-bit integer. */
    template <meta::unsigned_integer T> requires (sizeof(T) == 4)
    constexpr T bit_reverse(T n) noexcept {
        n = ((n & 0b1111'1111'1111'1111'0000'0000'0000'0000UL)) >> 16 |
            ((n & 0b0000'0000'0000'0000'1111'1111'1111'1111UL)) << 16;
        n = ((n & 0b1111'1111'0000'0000'1111'1111'0000'0000UL)) >> 8 |
            ((n & 0b0000'0000'1111'1111'0000'0000'1111'1111UL)) << 8;
        n = ((n & 0b1111'0000'1111'0000'1111'0000'1111'0000UL)) >> 4 |
            ((n & 0b0000'1111'0000'1111'0000'1111'0000'1111UL)) << 4;
        n = ((n & 0b1100'1100'1100'1100'1100'1100'1100'1100UL)) >> 2 |
            ((n & 0b0011'0011'0011'0011'0011'0011'0011'0011UL)) << 2;
        n = ((n & 0b1010'1010'1010'1010'1010'1010'1010'1010UL)) >> 1 |
            ((n & 0b0101'0101'0101'0101'0101'0101'0101'0101UL)) << 1;
        return n;
    }

    /* Reverse the order of bits in a 64-bit integer. */
    template <meta::unsigned_integer T> requires (sizeof(T) == 8)
    constexpr T bit_reverse(T n) noexcept {
        n = ((n & 0b1111'1111'1111'1111'1111'1111'1111'1111'0000'0000'0000'0000'0000'0000'0000'0000ULL)) >> 32 |
            ((n & 0b0000'0000'0000'0000'0000'0000'0000'0000'1111'1111'1111'1111'1111'1111'1111'1111ULL)) << 32;
        n = ((n & 0b1111'1111'1111'1111'0000'0000'0000'0000'1111'1111'1111'1111'0000'0000'0000'0000ULL)) >> 16 |
            ((n & 0b0000'0000'0000'0000'1111'1111'1111'1111'0000'0000'0000'0000'1111'1111'1111'1111ULL)) << 16;
        n = ((n & 0b1111'1111'0000'0000'1111'1111'0000'0000'1111'1111'0000'0000'1111'1111'0000'0000ULL)) >> 8 |
            ((n & 0b0000'0000'1111'1111'0000'0000'1111'1111'0000'0000'1111'1111'0000'0000'1111'1111ULL)) << 8;
        n = ((n & 0b1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000ULL)) >> 4 |
            ((n & 0b0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111'0000'1111ULL)) << 4;
        n = ((n & 0b1100'1100'1100'1100'1100'1100'1100'1100'1100'1100'1100'1100'1100'1100'1100'1100ULL)) >> 2 |
            ((n & 0b0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011'0011ULL)) << 2;
        n = ((n & 0b1010'1010'1010'1010'1010'1010'1010'1010'1010'1010'1010'1010'1010'1010'1010'1010ULL)) >> 1 |
            ((n & 0b0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101'0101ULL)) << 1;
        return n;
    }

    namespace divide {

        template <typename L, typename R>
        struct true_ {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(lhs / rhs))
                requires(requires{lhs / rhs;})
            {
                return lhs / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct true_<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return static_cast<double>(lhs) / rhs;
            }
        };

        template <typename L, typename R>
        struct cpp {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(lhs / rhs))
                requires(requires{lhs / rhs;})
            {
                return lhs / rhs;
            }
        };

        template <typename L, typename R>
        struct floor {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(std::floor(lhs / rhs)))
                requires(requires{std::floor(lhs / rhs);})
            {
                return std::floor(lhs / rhs);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct floor<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return lhs / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct floor<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return lhs / rhs - ((lhs < 0) ^ (rhs < 0) && (lhs % rhs) != 0);
            }
        };

        template <typename L, typename R>
        struct ceil {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(std::ceil(lhs / rhs)))
                requires(requires{std::ceil(lhs / rhs);})
            {
                return std::ceil(lhs / rhs);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct ceil<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return lhs / rhs + ((lhs % rhs) != 0);
            }
        };

        template <meta::integer L, meta::integer R>
        struct ceil<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return lhs / rhs + ((lhs < 0) == (rhs < 0) && (lhs % rhs) != 0);
            }
        };

        template <typename L, typename R>
        struct down {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(std::trunc(lhs / rhs)))
                requires(requires{std::trunc(lhs / rhs);})
            {
                return std::trunc(lhs / rhs);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct down<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(floor<L, R>{}(lhs, rhs)))
                requires(requires{floor<L, R>{}(lhs, rhs);})
            {
                return floor<L, R>{}(lhs, rhs);
            }
        };

        template <meta::integer L, meta::integer R>
        struct down<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept((lhs < 0) ^ (rhs < 0) ?
                    ceil<L, R>{}(lhs, rhs) :
                    floor<L, R>{}(lhs, rhs)
                ))
                requires(requires{(lhs < 0) ^ (rhs < 0) ?
                    ceil<L, R>{}(lhs, rhs) :
                    floor<L, R>{}(lhs, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    ceil<L, R>{}(lhs, rhs) :
                    floor<L, R>{}(lhs, rhs);
            }
        };

        template <typename L, typename R>
        struct up {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept((lhs < 0) ^ (rhs < 0) ?
                    floor<L, R>{}(lhs, rhs) :
                    ceil<L, R>{}(lhs, rhs))
                )
                requires(requires{(lhs < 0) ^ (rhs < 0) ?
                    floor<L, R>{}(lhs, rhs) :
                    ceil<L, R>{}(lhs, rhs);
                })
            {
                return (lhs < 0) ^ (rhs < 0) ?
                    floor<L, R>{}(lhs, rhs) :
                    ceil<L, R>{}(lhs, rhs);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct up<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(ceil<L, R>{}(lhs, rhs)))
                requires(requires{ceil<L, R>{}(lhs, rhs);})
            {
                return ceil<L, R>{}(lhs, rhs);
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

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct half_floor<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return (lhs + (rhs - 1) / 2) / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_floor<L, R> {
            /*   lhs rhs     a  b     sign *  ((rhs + a - b) / 2)
             *    +   +  =>  0  1  =>   1  *  ((rhs + 0 - 1) / 2)  =   ((rhs - 1) / 2)
             *    +   -  =>  0  0  =>  -1  *  ((rhs + 0 - 0) / 2)  =  -((rhs    ) / 2)
             *    -   +  =>  1  1  =>  -1  *  ((rhs + 1 - 1) / 2)  =  -((rhs    ) / 2)
             *    -   -  =>  1  0  =>   1  *  ((rhs + 1 - 0) / 2)  =   ((rhs + 1) / 2)
             */
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
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

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct half_ceil<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return (lhs + rhs / 2) / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_ceil<L, R> {
            /*   lhs rhs     a  b     sign *  ((rhs + a - b) / 2)
             *    +   +  =>  0  0  =>   1  *  ((rhs + 0 - 0) / 2)  =   ((rhs    ) / 2)
             *    +   -  =>  1  0  =>  -1  *  ((rhs + 1 - 0) / 2)  =  -((rhs + 1) / 2)
             *    -   +  =>  0  1  =>  -1  *  ((rhs + 0 - 1) / 2)  =  -((rhs - 1) / 2)
             *    -   -  =>  1  1  =>   1  *  ((rhs + 1 - 1) / 2)  =   ((rhs    ) / 2)
             */
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                bool a = rhs < 0;
                bool b = lhs < 0;
                int sign = (a == b) - (a != b);
                return (lhs + sign * ((rhs + a - b) / 2)) / rhs;
            }
        };

        template <typename L, typename R>
        struct half_down {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(((lhs < 0) ^ (rhs < 0)) ?
                    ceil<L, R>{}(lhs - rhs / 2, rhs) :
                    floor<L, R>{}(lhs + rhs / 2, rhs)
                ))
                requires(requires{((lhs < 0) ^ (rhs < 0)) ?
                    ceil<L, R>{}(lhs - rhs / 2, rhs) :
                    floor<L, R>{}(lhs + rhs / 2, rhs);
                })
            {
                return ((lhs < 0) ^ (rhs < 0)) ?
                    ceil<L, R>{}(lhs - rhs / 2, rhs) :
                    floor<L, R>{}(lhs + rhs / 2, rhs);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct half_down<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return (lhs + ((rhs - 1) / 2)) / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_down<L, R> {
            /*   lhs rhs     a  b     sign *  ((rhs + a - b) / 2)
             *    +   +  =>  0  1  =>   1  *  ((rhs + 0 - 1) / 2)  =   ((rhs - 1) / 2)
             *    +   -  =>  1  0  =>  -1  *  ((rhs + 1 - 0) / 2)  =  -((rhs + 1) / 2)
             *    -   +  =>  0  1  =>  -1  *  ((rhs + 0 - 1) / 2)  =  -((rhs - 1) / 2)
             *    -   -  =>  1  0  =>   1  *  ((rhs + 1 - 0) / 2)  =   ((rhs + 1) / 2)
             */
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                bool a = rhs < 0;
                bool b = rhs > 0;
                bool temp = lhs < 0;
                int sign = (a == temp) - (a != temp);
                return (lhs + sign * ((rhs + a - b) / 2)) / rhs;
            }
        };

        template <typename L, typename R>
        struct half_up {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept(noexcept(std::round(lhs / rhs)))
                requires(requires{std::round(lhs / rhs);})
            {
                return std::round(lhs / rhs);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct half_up<L, R> {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                return (lhs + (rhs / 2)) / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_up<L, R> {
            /*   lhs rhs     a  b     sign *  ((rhs + a - b) / 2)
             *    +   +  =>  0  0  =>   1  *  ((rhs + 0 - 1) / 2)  =   (rhs / 2)
             *    +   -  =>  0  0  =>  -1  *  ((rhs + 1 - 0) / 2)  =  -(rhs / 2)
             *    -   +  =>  0  0  =>  -1  *  ((rhs + 0 - 1) / 2)  =  -(rhs / 2)
             *    -   -  =>  0  0  =>   1  *  ((rhs + 1 - 0) / 2)  =   (rhs / 2)
             */
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                bool a = lhs < 0;
                bool b = rhs < 0;
                int sign = (a == b) - (a != b);
                return (lhs + sign * (rhs / 2)) / rhs;
            }
        };

        template <typename L, typename R>
        struct half_even {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept
                requires(requires{
                    [](const L& lhs, const R& rhs) {
                        auto result = lhs / rhs;
                        decltype(result) whole;
                        auto fract = std::modf(result, &whole);
                        if (std::abs(fract) == 0.5) {
                            return whole + std::fmod(whole, 2.0);
                        }
                        return std::round(result);
                    }(lhs, rhs);
                })
            {
                auto result = lhs / rhs;
                decltype(result) whole;
                auto fract = std::modf(result, &whole);
                return std::abs(fract) == 0.5 ?
                    whole + std::fmod(whole, 2.0) :
                    std::round(result);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct half_even<L, R> {
            /* This is equivalent to half_up when the result of normal division toward
             * zero would be odd, and half_down when it would be even.
             */
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                bool odd = (lhs / rhs) % 2;
                return (lhs + ((rhs - !odd) / 2)) / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_even<L, R> {
            /* This is equivalent to half_up when the result of normal division toward
             * zero would be odd, and half_down when it would be even.
             */
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                bool a = rhs < 0;
                bool b = rhs > 0;
                bool temp = lhs < 0;
                int sign = (a == temp) - (a != temp);
                bool odd = (lhs / rhs) % 2;
                return (lhs + sign * ((rhs + !odd * (a - b)) / 2)) / rhs;
            }
        };

        template <typename L, typename R>
        struct half_odd {
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                noexcept
                requires(requires{
                    [](const L& lhs, const R& rhs) {
                        auto result = lhs / rhs;
                        decltype(result) whole;
                        auto fract = std::modf(result, &whole);
                        if (std::abs(fract) == 0.5) {
                            return whole + std::fmod(whole + 1.0, 2.0);
                        }
                        return std::round(result);
                    }(lhs, rhs);
                })
            {
                auto result = lhs / rhs;
                decltype(result) whole;
                auto fract = std::modf(result, &whole);
                return std::abs(fract) == 0.5 ?
                    whole + std::fmod(whole + 1.0, 2.0) :
                    std::round(result);
            }
        };

        template <meta::unsigned_integer L, meta::unsigned_integer R>
        struct half_odd<L, R> {
            /* This is equivalent to half_down when the result of normal division
             * toward zero would be odd, and half_up when it would be even.
             */
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                bool odd = (lhs / rhs) % 2;
                return (lhs + ((rhs - odd) / 2)) / rhs;
            }
        };

        template <meta::integer L, meta::integer R>
        struct half_odd<L, R> {
            /* This is equivalent to half_down when the result of normal division
             * toward zero would be odd, and half_up when it would be even.
             */
            static constexpr decltype(auto) operator()(const L& lhs, const R& rhs) noexcept {
                bool a = rhs < 0;
                bool b = rhs > 0;
                bool temp = lhs < 0;
                int sign = (a == temp) - (a != temp);
                bool odd = (lhs / rhs) % 2;
                return (lhs + sign * ((rhs + odd * (a - b)) / 2)) / rhs;
            }
        };

    }

}  // namespace impl


namespace math {


    template <meta::integer T>
    [[nodiscard]] constexpr decltype(auto) abs(T&& i) noexcept {
        if constexpr (meta::signed_integer<T>) {
            /// TODO: specialization for Int<N>, which can simply check msb_is_set(), and
            /// possibly negate the value in-place if the argument is an rvalue.
            return (i < 0 ? -i : i);
        } else {
            return (std::forward<T>(i));
        }
    }

    template <meta::floating T>
    [[nodiscard]] constexpr decltype(auto) abs(T&& f)
        noexcept(noexcept(std::abs(std::forward<T>(f))))
        requires(requires{std::abs(std::forward<T>(f));})
    {
        /// TODO: a separate specialization for Float<E, M>, which calls .abs() 
        return (std::abs(std::forward<T>(f)));
    }


    /// TODO: not sure the best way to handle binary operators in order to prevent
    /// combinatoric template bloat.  One way to keep everything sane is to force the
    /// use of a helper struct, which mandates shared template constraints, like
    /// impl::math::pow<meta::numeric, meta::numeric, meta::numeric || void>.  Then,
    /// when you specialize it, it would fail to compile for any type that does not
    /// satisfy meta::numeric, and you would have to specify that explicitly when you
    /// specialize it.



    template <meta::numeric L, meta::numeric R>
    [[nodiscard]] constexpr decltype(auto) mul(L&& lhs, R&& rhs)
        noexcept(noexcept(std::forward<L>(lhs) * std::forward<R>(rhs)))
        requires(requires{std::forward<L>(lhs) * std::forward<R>(rhs);})
    {
        return (std::forward<L>(lhs) * std::forward<R>(rhs));
    }


    template <meta::numeric L, meta::numeric R, meta::integer M>
    [[nodiscard]] constexpr decltype(auto) mul(
        L&& lhs,
        R&& rhs,
        M&& mod
    ) noexcept(noexcept(std::forward<L>(lhs) * std::forward<R>(rhs) % std::forward<M>(mod)))
        requires(requires{std::forward<L>(lhs) * std::forward<R>(rhs) % std::forward<M>(mod);})
    {
        return (std::forward<L>(lhs) * std::forward<R>(rhs) % std::forward<M>(mod));
    }



}







/* A generalized absolute value operator.  The behavior of this operator is controlled
by the `impl::abs<T>` control struct, which is always specialized for integer and
floating point types at a minimum. */
template <typename T>
    requires (requires(const T& obj) {
        impl::abs<meta::unqualify<meta::unwrap_type<T>>>{}(obj);
    })
[[nodiscard]] constexpr decltype(auto) abs(const T& obj) noexcept(
    noexcept(impl::abs<meta::unqualify<meta::unwrap_type<T>>>{}(obj))
) {
    return impl::abs<meta::unqualify<meta::unwrap_type<T>>>{}(obj);
}


/* A generalized exponentiation operator.  The behavior of this operator is controlled
by the `impl::pow<Base, Exp, Mod = void>` control struct, which is always specialized
for integer and floating point types at a minimum.  Note that integer exponentiation
with a negative exponent always returns zero, without erroring. */
template <typename Base, typename Exp>
    requires (requires(const Base& base, const Exp& exp) {
        impl::pow<
            meta::unqualify<meta::unwrap_type<Base>>,
            meta::unqualify<meta::unwrap_type<Exp>>
        >{}(base, exp);
    })
[[nodiscard]] constexpr decltype(auto) pow(
    const Base& base,
    const Exp& exp
) noexcept(
    noexcept(impl::pow<
        meta::unqualify<meta::unwrap_type<Base>>,
        meta::unqualify<meta::unwrap_type<Exp>>
    >{}(base, exp))
) {
    return impl::pow<
        meta::unqualify<meta::unwrap_type<Base>>,
        meta::unqualify<meta::unwrap_type<Exp>>
    >{}(base, exp);
}


/* A generalized modular exponentiation operator.  The behavior of this operator is
controlled by the `impl::pow<Base, Exp, Mod = void>` control struct, which is always
specialized for integer and floating point types at a minimum.  Note that modular
exponentiation with a negative exponent always fails with an `ArithmeticError`, as
there is no general solution for the modular case.  Also, providing a modulus equal to
zero will cause a `ZeroDivisionError`. */
template <typename Base, typename Exp, typename Mod>
    requires (requires(const Base& base, const Exp& exp, const Mod& mod) {
        impl::pow<
            meta::unqualify<meta::unwrap_type<Base>>,
            meta::unqualify<meta::unwrap_type<Exp>>,
            meta::unqualify<meta::unwrap_type<Mod>>
        >{}(base, exp, mod);
    })
[[nodiscard]] constexpr decltype(auto) pow(
    const Base& base,
    const Exp& exp,
    const Mod& mod
) noexcept(
    noexcept(impl::check_for_negative_exponent(exp)) &&
    noexcept(impl::check_for_zero_division(mod)) &&
    noexcept(impl::pow<
        meta::unqualify<meta::unwrap_type<Base>>,
        meta::unqualify<meta::unwrap_type<Exp>>,
        meta::unqualify<meta::unwrap_type<Mod>>
    >{}(base, exp, mod))
) {
    impl::check_for_negative_exponent(exp);
    impl::check_for_zero_division(mod);
    return impl::pow<
        meta::unqualify<meta::unwrap_type<Base>>,
        meta::unqualify<meta::unwrap_type<Exp>>,
        meta::unqualify<meta::unwrap_type<Mod>>
    >{}(base, exp, mod);
}


/// TODO: log<b>()
/// TODO: root<b>()
/// TODO: prime::check()
/// TODO: prime::next()
/// TODO: prime::prev()

/// TODO: factors, gcd, lcm, etc.


/// TODO: a generalized way to do this would be very helpful for the numeric types in
/// bits.h, since they require logarithms to calculate required digits, and sqrt for
/// implementing soft float division.



/* A family of division operators with different rounding strategies, in order to
facilitate inter-language communication where conventions may differ.  Numeric
algorithms that use these operators are guaranteed to behave consistently from one
language to another. */
struct divide {
private:

    template <typename T>
    using unwrap = meta::unqualify<meta::unwrap_type<T>>;

public:
    constexpr divide() noexcept = delete;

    /* Divide two numbers, returning a floating point approximation of the true
    quotient.  This is the rounding strategy used by Python's `/` operator. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::true_<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) true_(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::true_<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::true_<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers according to C++ semantics.  For integers, this performs
    truncated division toward zero.  Otherwise, it is identical to "true" division. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::cpp<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) cpp(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::cpp<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::cpp<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward negative infinity.  This is
    the rounding strategy used by Python's `//` operator. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::floor<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) floor(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::floor<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::floor<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward positive infinity. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::ceil<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::ceil<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::ceil<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward zero. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::down<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) down(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::down<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::down<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient away from zero. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::up<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) up(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::up<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::up<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward negative infinity. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::half_floor<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) half_floor(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::half_floor<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::half_floor<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward positive infinity. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::half_ceil<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) half_ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::half_ceil<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::half_ceil<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward zero. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::half_down<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) half_down(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::half_down<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::half_down<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties away from zero. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::half_up<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) half_up(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::half_up<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::half_up<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward the nearest even number. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::half_even<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) half_even(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::half_even<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::half_even<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward the nearest odd number. */
    template <typename L, typename R>
        requires (meta::invocable<
            impl::divide::half_odd<unwrap<L>, unwrap<R>>,
            const L&,
            const R&
        >)
    [[nodiscard]] static constexpr decltype(auto) half_odd(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(impl::check_for_zero_division(rhs)) &&
        noexcept(impl::divide::half_odd<unwrap<L>, unwrap<R>>{}(lhs, rhs))
    ) {
        impl::check_for_zero_division(rhs);
        return impl::divide::half_odd<unwrap<L>, unwrap<R>>{}(lhs, rhs);
    }
};


/* A family of modulus operators with different rounding strategies, in order to
facilitate inter-language communication where conventions may differ.  Numeric
algorithms that use these operators are guaranteed to behave consistently from one
language to another. */
struct modulo {
    constexpr modulo() noexcept = delete;

    /* Divide two numbers, returning a floating point approximation of the remainder.
    This will always be equivalent to the floating point error of the division (i.e.
    negligible, but not necessarily zero). */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::true_(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) true_(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::true_(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::true_(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, returning the remainder according to C++ semantics.  For
    integers, this returns the remainder of truncated division toward zero.  Otherwise,
    it is identical to a "true" modulus (i.e. floating point error) of a division. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::cpp(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) cpp(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::cpp(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::cpp(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward negative infinity and returning
    the remainder.  This is the rounding strategy used by Python's `%` operator. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::floor(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) floor(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::floor(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::floor(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward positive infinity and returning
    the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::ceil(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::ceil(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::ceil(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward zero and returning the
    remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::down(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) down(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::down(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::down(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient away from zero and returning the
    remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::up(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) up(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::up(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::up(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward negative infinity, and returning the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::half_floor(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) half_floor(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::half_floor(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::half_floor(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward positive infinity, and returning the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::half_ceil(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) half_ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::half_ceil(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::half_ceil(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward zero, and returning the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::half_down(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) half_down(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::half_down(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::half_down(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties away from zero, and returning the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::half_up(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) half_up(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::half_up(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::half_up(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward the nearest even number. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::half_even(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) half_even(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::half_even(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::half_even(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward the nearest odd number. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            meta::unwrap(lhs) - divide::half_odd(lhs, rhs) * meta::unwrap(rhs);
        })
    [[nodiscard]] static constexpr decltype(auto) half_odd(
        const L& lhs,
        const R& rhs
    ) noexcept(
        noexcept(meta::unwrap(lhs) - divide::half_odd(lhs, rhs) * meta::unwrap(rhs))
    ) {
        return [](const auto& lhs, const auto& rhs) {
            return lhs - divide::half_odd(lhs, rhs) * rhs;
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }
};


/* A family of combined division and modulus operators with different rounding
strategies, in order to facilitate inter-language communication where conventions may
differ.  Numeric algorithms that use these operators are guaranteed to behave
consistently from one language to another. */
struct divmod {
    constexpr divmod() noexcept = delete;

    /* Divide two numbers, returning both the quotient and remainder as floating point
    approximations.  The remainder will always be equivalent to the floating point
    error of the division (i.e. negligible, but not necessarily zero). */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(divide::true_(lhs, rhs), modulo::true_(lhs, rhs));
        })
    [[nodiscard]] static constexpr decltype(auto) true_(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(divide::true_(lhs, rhs), modulo::true_(lhs, rhs))
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::true_(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, returning both the quotient and remainder according to C++
    semantics.  For integers, this returns the result of truncated division toward
    zero.  Otherwise, it is identical to a "true" division and modulus (floating point
    error). */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(divide::cpp(lhs, rhs), modulo::cpp(lhs, rhs));
        })
    [[nodiscard]] static constexpr decltype(auto) cpp(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(divide::cpp(lhs, rhs), modulo::cpp(lhs, rhs))
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::cpp(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward negative infinity and returning
    it along with the remainder.  This is the rounding strategy used by Python's
    `divmod` operator. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(divide::floor(lhs, rhs), modulo::floor(lhs, rhs));
        })
    [[nodiscard]] static constexpr decltype(auto) floor(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(divide::floor(lhs, rhs), modulo::floor(lhs, rhs))
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::floor(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward positive infinity and returning
    it along with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(divide::ceil(lhs, rhs), modulo::ceil(lhs, rhs));
        })
    [[nodiscard]] static constexpr decltype(auto) ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(divide::ceil(lhs, rhs), modulo::ceil(lhs, rhs))
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::ceil(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward zero and returning it along
    with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(divide::down(lhs, rhs), modulo::down(lhs, rhs));
        })
    [[nodiscard]] static constexpr decltype(auto) down(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(divide::down(lhs, rhs), modulo::down(lhs, rhs))
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::down(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient away from zero and returning it along
    with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(divide::up(lhs, rhs), modulo::up(lhs, rhs));
        })
    [[nodiscard]] static constexpr decltype(auto) up(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(divide::up(lhs, rhs), modulo::up(lhs, rhs))
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::up(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward negative infinity, and returning it along with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(
                divide::half_floor(lhs, rhs),
                modulo::half_floor(lhs, rhs)
            );
        })
    [[nodiscard]] static constexpr decltype(auto) half_floor(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(
            divide::half_floor(lhs, rhs),
            modulo::half_floor(lhs, rhs)
        )
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::half_floor(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward positive infinity, and returning it along with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(
                divide::half_ceil(lhs, rhs),
                modulo::half_ceil(lhs, rhs)
            );
        })
    [[nodiscard]] static constexpr decltype(auto) half_ceil(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(
            divide::half_ceil(lhs, rhs),
            modulo::half_ceil(lhs, rhs)
        )
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::half_ceil(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward zero, and returning it along with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(
                divide::half_down(lhs, rhs),
                modulo::half_down(lhs, rhs)
            );
        })
    [[nodiscard]] static constexpr decltype(auto) half_down(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(
            divide::half_down(lhs, rhs),
            modulo::half_down(lhs, rhs)
        )
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::half_down(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties away from zero, and returning it along with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(
                divide::half_up(lhs, rhs),
                modulo::half_up(lhs, rhs)
            );
        })
    [[nodiscard]] static constexpr decltype(auto) half_up(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(
            divide::half_up(lhs, rhs),
            modulo::half_up(lhs, rhs)
        )
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::half_up(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward the nearest even number, and returning it along with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(
                divide::half_even(lhs, rhs),
                modulo::half_even(lhs, rhs)
            );
        })
    [[nodiscard]] static constexpr decltype(auto) half_even(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(
            divide::half_even(lhs, rhs),
            modulo::half_even(lhs, rhs)
        )
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::half_even(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }

    /* Divide two numbers, rounding the quotient toward the nearest whole number, with
    ties toward the nearest odd number, and returning it along with the remainder. */
    template <typename L, typename R>
        requires (requires(const L& lhs, const R& rhs) {
            std::make_pair(
                divide::half_odd(lhs, rhs),
                modulo::half_odd(lhs, rhs)
            );
        })
    [[nodiscard]] static constexpr decltype(auto) half_odd(
        const L& lhs,
        const R& rhs
    ) noexcept(noexcept(
        std::make_pair(
            divide::half_odd(lhs, rhs),
            modulo::half_odd(lhs, rhs)
        )
    )) {
        return [](const auto& lhs, const auto& rhs) {
            auto quotient = divide::half_odd(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }(meta::unwrap(lhs), meta::unwrap(rhs));
    }
};


/* A family of rounding operators with different strategies, in order to facilitate
inter-language communication where conventions may differ.  Numeric algorithms that
use these operators are guaranteed to behave consistently from one language to
another. */
struct round {
private:
    template <typename T>
    using unwrap = meta::unqualify<meta::unwrap_type<T>>;

public:
    constexpr round() noexcept = delete;

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  For "true" rounding, this effectively does
    nothing, and simply corrects for possible floating point error with respect to
    the digit count. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::true_<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::true_<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) true_(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::true_<unwrap<T>, long long>{}(obj * 1LL, 1LL)) &&
        noexcept(impl::divide::true_<unwrap<T>, long long>{}(obj, 1LL) * 1LL)
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::true_<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::true_<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  For "cpp" rounding, this only appreciably
    impacts integer values, in which case it is identical to rounding down (toward
    zero) by the digit count.  Otherwise, it is a simple floating point error
    correction, similar to "true" rounding. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::cpp<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::cpp<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) cpp(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::cpp<unwrap<T>, long long>{}(obj * 1LL, 1LL)) &&
        noexcept(impl::divide::cpp<unwrap<T>, long long>{}(obj, 1LL) * 1LL)
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::cpp<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::cpp<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward negative infinity,
    as if floor dividing by a power of 10, and then rescaling by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::floor<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::floor<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) floor(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::floor<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::floor<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::floor<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward positive infinity,
    as if ceil dividing by a power of 10, and then rescaling by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::ceil<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::ceil<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) ceil(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::ceil<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::ceil<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::ceil<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward zero, as if
    dividing down by a power of 10, and then rescaling by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::down<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::down<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) down(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::down<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::down<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::down<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded away from zero, as if
    dividing up by a power of 10, and then rescaling by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::up<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::up<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) up(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::up<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::up<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::up<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward the nearest whole
    number, as if performing half-floor division by a power of 10, and then rescaling
    by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::half_floor<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::half_floor<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) half_floor(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::half_floor<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::half_floor<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::half_floor<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward the nearest whole
    number, as if performing half-ceiling division by a power of 10, and then rescaling
    by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::half_ceil<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::half_ceil<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) half_ceil(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::half_ceil<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::half_ceil<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::half_ceil<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward the nearest whole
    number, as if performing half-down division by a power of 10, and then rescaling
    by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::half_down<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::half_down<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) half_down(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::half_down<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::half_down<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::half_down<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward the nearest whole
    number, as if performing half-up division by a power of 10, and then rescaling
    by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::half_up<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::half_up<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) half_up(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::half_up<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::half_up<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::half_up<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward the nearest whole
    number, as if performing half-even division by a power of 10, and then rescaling
    by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::half_even<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::half_even<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) half_even(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::half_even<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::half_even<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::half_even<unwrap<T>, long long>{}(obj, factor) * factor;
    }

    /* Round a value to the specified number of digits as a power of 10, defaulting
    to the ones place.  Positive digits count to the right of the decimal place, and
    negative digits count to the left.  Results are rounded toward the nearest whole
    number, as if performing half-odd division by a power of 10, and then rescaling
    by the same factor. */
    template <typename T>
        requires (requires(const T& obj) {
            impl::divide::half_odd<unwrap<T>, long long>{}(obj * 1LL, 1LL);
            impl::divide::half_odd<unwrap<T>, long long>{}(obj, 1LL) * 1LL;
        })
    [[nodiscard]] static constexpr decltype(auto) half_odd(
        const T& obj,
        int digits = 0
    ) noexcept(
        noexcept(impl::divide::half_odd<unwrap<T>, long long>{}(obj * 1LL, 1LL))
    ) {
        if (digits > 0) {
            long long factor = pow(10LL, digits);
            return impl::divide::half_odd<unwrap<T>, long long>{}(obj * factor, factor);
        }
        long long factor = pow(10LL, -digits);
        return impl::divide::half_odd<unwrap<T>, long long>{}(obj, factor) * factor;
    }
};


}


namespace std {

    template <bertrand::meta::wrapper T>
    struct hash<T> {
        static constexpr decltype(auto) operator()(const T& t)
            noexcept(noexcept(bertrand::hash(bertrand::meta::unwrap(t))))
            requires(requires{bertrand::hash(bertrand::meta::unwrap(t));})
        {
            return bertrand::hash(bertrand::meta::unwrap(t));
        }
    };

}


#endif