#ifndef BERTRAND_MATH_H
#define BERTRAND_MATH_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


namespace impl {

    template <typename T>
    constexpr void check_for_zero_division(const T& rhs)
        noexcept (!DEBUG || !requires{{rhs == 0} -> meta::explicitly_convertible_to<bool>;})
    {
        if constexpr (DEBUG && requires{{rhs == 0} -> meta::explicitly_convertible_to<bool>;}) {
            if (rhs == 0) {
                throw ZeroDivisionError();
            }
        }
    }

    constexpr ArithmeticError negative_exponent_error() noexcept {
        return ArithmeticError(
            "Negative exponent not supported for modular exponentiation"
        );
    }

    template <typename T>
    constexpr void check_for_negative_exponent(const T& rhs)
        noexcept (!DEBUG || !requires{{rhs < 0} -> meta::explicitly_convertible_to<bool>;})
    {
        if constexpr (DEBUG && requires{{rhs < 0} -> meta::explicitly_convertible_to<bool>;}) {
            if (rhs < 0) {
                throw negative_exponent_error();
            }
        }
    }

    namespace math {
        struct empty {};

        /* Default `abs()` implementation is identical to `std::abs()`. */
        template <typename T>
        struct abs {
            static constexpr decltype(auto) operator()(const T& obj)
                noexcept(noexcept(std::abs(obj)))
                requires(requires{std::abs(obj);})
            {
                return std::abs(obj);
            }
        };

        /* Default `pow()` implementation falls back to STL floating point methods. */
        template <typename Base, typename Exp, typename Mod = empty>
        struct pow {
            static constexpr Base operator()(const Base& base, const Exp& exp)
                noexcept (requires{{
                    std::pow(Base(base), Base(exp))
                } noexcept -> meta::nothrow::convertible_to<Base>;})
                requires (requires{{
                    std::pow(Base(base), Base(exp))
                } -> meta::convertible_to<Base>;})
            {
                return std::pow(Base(base), Base(exp));
            }

            static constexpr Base operator()(const Base& base, const Exp& exp, const Mod& mod)
                noexcept (requires(Base result, Base y, Exp whole, Exp frac) {
                    {Base{1}} noexcept;
                    {Base{std::fmod(base, mod)}} noexcept;
                    {Exp{std::modf(exp, &whole)}} noexcept;
                    {whole >= 1} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                    {
                        std::fmod(whole, 2.0) >= 1
                    } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                    {result = std::fmod(result * y, mod)} noexcept;
                    {y = std::fmod(y * y, mod)} noexcept;
                    {whole = std::floor(whole / 2)} noexcept;
                    {frac == 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                    {
                        std::fmod(result * std::pow(y, frac), mod)
                    } noexcept -> meta::nothrow::convertible_to<Base>;
                })
                requires (requires(Base result, Base y, Exp whole, Exp frac) {
                    {Base{1}};
                    {Base{std::fmod(base, mod)}};
                    {Exp{std::modf(exp, &whole)}};
                    {whole >= 1} -> meta::explicitly_convertible_to<bool>;
                    {std::fmod(whole, 2.0) >= 1} -> meta::explicitly_convertible_to<bool>;
                    {result = std::fmod(result * y, mod)};
                    {y = std::fmod(y * y, mod)};
                    {whole = std::floor(whole / 2)};
                    {frac == 0} -> meta::explicitly_convertible_to<bool>;
                    {std::fmod(result * std::pow(y, frac), mod)} -> meta::convertible_to<Base>;
                })
            {
                Base result {1};
                Base y {std::fmod(base, mod)};
                Exp whole;
                Exp frac {std::modf(exp, &whole)};
                while (whole >= 1) {
                    if (std::fmod(whole, 2.0) >= 1) {
                        result = std::fmod(result * y, mod);
                    }
                    y = std::fmod(y * y, mod);
                    whole = std::floor(whole / 2);
                }
                if (frac == 0) {
                    return result;
                }
                return std::fmod(result * std::pow(y, frac), mod);
            }
        };

        template <typename Base, typename Exp>
        constexpr Base exponentiate_by_squaring(const Base& base, const Exp& exp)
            noexcept (requires(Base result, Base y, Exp e) {
                {Base{1}} noexcept;
                {Base{base}} noexcept;
                {Exp{exp}} noexcept;
                {e >= 1} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {e % 2} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {result *= y} noexcept;
                {y *= y} noexcept;
                {e /= 2} noexcept;
            })
            requires (requires(Base result, Base y, Exp e) {
                {Base{1}};
                {Base{base}};
                {Exp{exp}};
                {e >= 1} -> meta::explicitly_convertible_to<bool>;
                {e % 2} -> meta::explicitly_convertible_to<bool>;
                {result *= y};
                {y *= y};
                {e /= 2};
            })
        {
            Base result {1};
            Base y {base};
            Exp e {exp};
            while (e >= 1) {
                if (e % 2) {
                    result *= y;
                }
                y *= y;
                e /= 2;
            }
            return result;
        }

        template <typename Base, typename Exp, typename Mod>
        constexpr Base exponentiate_by_squaring(const Base& base, const Exp& exp, const Mod& mod)
            noexcept (requires(Base result, Base y, Exp e) {
                {Base{1}} noexcept;
                {Base{base % mod}} noexcept;
                {Exp{exp}} noexcept;
                {e >= 1} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {e % 2} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {result = (result * y) % mod} noexcept;
                {y = (y * y) % mod} noexcept;
                {e /= 2} noexcept;
            })
            requires (requires(Base result, Base y, Exp e) {
                {Base{1}};
                {Base{base % mod}};
                {Exp{exp}};
                {e >= 1} -> meta::explicitly_convertible_to<bool>;
                {e % 2} -> meta::explicitly_convertible_to<bool>;
                {result = (result * y) % mod};
                {y = (y * y) % mod};
                {e /= 2};
            })
        {
            Base result {1};
            Base y {base % mod};
            Exp e {exp};
            while (e >= 1) {
                if (e % 2) {
                    result = (result * y) % mod;
                }
                y = (y * y) % mod;
                e /= 2;
            }
            return result;
        }

        /* Any base can be raised to an integer power via exponentiation by squaring. */
        template <typename Base, meta::integer Exp>
            requires (requires(const Base& base, const Exp& exp) {
                {exp < 0} -> meta::explicitly_convertible_to<bool>;
                {Base{1} / exponentiate_by_squaring(base, -exp)} -> meta::convertible_to<Base>;
                {exponentiate_by_squaring(base, exp)};
            })
        struct pow<Base, Exp> {
            static constexpr Base operator()(const Base& base, const Exp& exp)
                noexcept (requires(Base result, Base y, Exp e) {
                    {exp < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                    {
                        Base{1} / exponentiate_by_squaring(base, -exp)
                    } noexcept -> meta::nothrow::convertible_to<Base>;
                    {exponentiate_by_squaring(base, exp)} noexcept;
                })
            {
                if (exp < 0) {
                    return Base{1} / exponentiate_by_squaring(base, -exp);
                }
                return exponentiate_by_squaring(base, exp);
            }
        };

        /* Any base can be raised to a modular integer power via exponentiation by
        squaring with modular reduction. */
        template <typename Base, meta::integer Exp, meta::integer Mod>
            requires (requires(const Base& base, const Exp& exp, const Mod& mod) {
                {exp < 0} -> meta::explicitly_convertible_to<bool>;
                {(Base{1} / exponentiate_by_squaring(base, -exp)) % mod} -> meta::convertible_to<Base>;
                {exponentiate_by_squaring(base, exp, mod)};
            })
        struct pow<Base, Exp, Mod> {
            static constexpr Base operator()(const Base& base, const Exp& exp,const Mod& mod)
                noexcept (requires(Base result, Base y, Exp e) {
                    {exp < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                    {
                        (Base{1} / exponentiate_by_squaring(base, -exp)) % mod
                    } noexcept -> meta::nothrow::convertible_to<Base>;
                    {exponentiate_by_squaring(base, exp, mod)} noexcept;
                })
            {
                if (exp < 0) {
                    return (Base{1} / exponentiate_by_squaring(base, -exp)) % mod;
                }
                return exponentiate_by_squaring(base, exp, mod);
            }
        };

        namespace div {

            /// TODO: the std::ceil/floor/trunc overloads should probably be special
            /// cases for float types, rather than the default behavior.

            template <typename L, typename R>
            struct real {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{lhs / rhs} noexcept;})
                    requires (requires{{lhs / rhs};})
                {
                    return (lhs / rhs);
                }
            };

            template <meta::integer L, meta::integer R>
            struct real<L, R> {
                static constexpr double operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{
                        static_cast<double>(lhs) / rhs
                    } noexcept -> meta::nothrow::convertible_to<double>;})
                    requires (requires{
                        {static_cast<double>(lhs) / rhs} -> meta::convertible_to<double>;
                    })
                {
                    return static_cast<double>(lhs) / rhs;
                }
            };

            template <typename L, typename R>
            struct cpp {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{lhs / rhs} noexcept;})
                    requires (requires{{lhs / rhs};})
                {
                    return (lhs / rhs);
                }
            };

            template <typename L, typename R>
            struct floor {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{std::floor(lhs / rhs)} noexcept;})
                    requires (requires{{std::floor(lhs / rhs)};})
                {
                    return (std::floor(lhs / rhs));
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct floor<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{lhs / rhs} noexcept;})
                    requires (requires{{lhs / rhs};})
                {
                    return (lhs / rhs);
                }
            };

            template <meta::integer L, meta::integer R>
            struct floor<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{
                        {lhs / rhs - ((lhs < 0) ^ (rhs < 0) && (lhs % rhs) != 0)} noexcept;
                    })
                    requires (requires{
                        {lhs / rhs - ((lhs < 0) ^ (rhs < 0) && (lhs % rhs) != 0)};
                    })
                {
                    return (lhs / rhs - ((lhs < 0) ^ (rhs < 0) && (lhs % rhs) != 0));
                }
            };

            template <typename L, typename R>
            struct ceil {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{std::ceil(lhs / rhs)} noexcept;})
                    requires (requires{{std::ceil(lhs / rhs)};})
                {
                    return (std::ceil(lhs / rhs));
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct ceil<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{lhs / rhs + ((lhs % rhs) != 0)} noexcept;})
                    requires (requires{{lhs / rhs + ((lhs % rhs) != 0)};})
                {
                    return (lhs / rhs + ((lhs % rhs) != 0));
                }
            };

            template <meta::integer L, meta::integer R>
            struct ceil<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{
                        {lhs / rhs + ((lhs < 0) == (rhs < 0) && (lhs % rhs) != 0)} noexcept;
                    })
                    requires (requires{
                        {lhs / rhs + ((lhs < 0) == (rhs < 0) && (lhs % rhs) != 0)};
                    })
                {
                    return (lhs / rhs + ((lhs < 0) == (rhs < 0) && (lhs % rhs) != 0));
                }
            };

            template <typename L, typename R>
            struct down {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{std::trunc(lhs / rhs)} noexcept;})
                    requires (requires{{std::trunc(lhs / rhs)};})
                {
                    return (std::trunc(lhs / rhs));
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct down<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{floor<L, R>{}(lhs, rhs)} noexcept;})
                    requires (requires{{floor<L, R>{}(lhs, rhs)};})
                {
                    return (floor<L, R>{}(lhs, rhs));
                }
            };

            template <meta::integer L, meta::integer R>
            struct down<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{(lhs < 0) ^ (rhs < 0) ?
                        ceil<L, R>{}(lhs, rhs) :
                        floor<L, R>{}(lhs, rhs)
                    } noexcept;})
                    requires (requires{{(lhs < 0) ^ (rhs < 0) ?
                        ceil<L, R>{}(lhs, rhs) :
                        floor<L, R>{}(lhs, rhs)
                    };})
                {
                    return (((lhs < 0) ^ (rhs < 0) ?
                        ceil<L, R>{}(lhs, rhs) :
                        floor<L, R>{}(lhs, rhs))
                    );
                }
            };

            template <typename L, typename R>
            struct up {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{(lhs < 0) ^ (rhs < 0) ?
                        floor<L, R>{}(lhs, rhs) :
                        ceil<L, R>{}(lhs, rhs)
                    } noexcept;})
                    requires (requires{{(lhs < 0) ^ (rhs < 0) ?
                        floor<L, R>{}(lhs, rhs) :
                        ceil<L, R>{}(lhs, rhs)
                    };})
                {
                    return ((lhs < 0) ^ (rhs < 0) ?
                        floor<L, R>{}(lhs, rhs) :
                        ceil<L, R>{}(lhs, rhs)
                    );
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct up<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{ceil<L, R>{}(lhs, rhs)} noexcept;})
                    requires (requires{{ceil<L, R>{}(lhs, rhs)};})
                {
                    return (ceil<L, R>{}(lhs, rhs));
                }
            };

            template <typename L, typename R>
            struct half_floor {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{ceil<L, R>{}(lhs - rhs / 2, rhs)} noexcept;})
                    requires (requires{{ceil<L, R>{}(lhs - rhs / 2, rhs)};})
                {
                    return (ceil<L, R>{}(lhs - rhs / 2, rhs));
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct half_floor<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{(lhs + (rhs - 1) / 2) / rhs} noexcept;})
                    requires (requires{{(lhs + (rhs - 1) / 2) / rhs};})
                {
                    return ((lhs + (rhs - 1) / 2) / rhs);
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
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(bool a, bool b, int sign) {
                        {lhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {rhs > 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(a != b) - (a == b)} noexcept -> meta::nothrow::convertible_to<int>;
                        {(lhs + sign * ((rhs + a - b) / 2)) / rhs} noexcept;
                    })
                    requires (requires(bool a, bool b, int sign) {
                        {lhs < 0} -> meta::convertible_to<bool>;
                        {rhs > 0} -> meta::convertible_to<bool>;
                        {(a != b) - (a == b)} -> meta::convertible_to<int>;
                        {(lhs + sign * ((rhs + a - b) / 2)) / rhs};
                    })
                {
                    bool a = lhs < 0;
                    bool b = rhs > 0;
                    int sign = (a != b) - (a == b);
                    return ((lhs + sign * ((rhs + a - b) / 2)) / rhs);
                }
            };

            template <typename L, typename R>
            struct half_ceil {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{floor<L, R>{}(lhs + rhs / 2, rhs)} noexcept;})
                    requires (requires{{floor<L, R>{}(lhs + rhs / 2, rhs)};})
                {
                    return (floor<L, R>{}(lhs + rhs / 2, rhs));
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct half_ceil<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{(lhs + rhs / 2) / rhs} noexcept;})
                    requires (requires{{(lhs + rhs / 2) / rhs};})
                {
                    return ((lhs + rhs / 2) / rhs);
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
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(bool a, bool b, int sign) {
                        {lhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {rhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(a == b) - (a != b)} noexcept -> meta::nothrow::convertible_to<int>;
                        {(lhs + sign * ((rhs + a - b) / 2)) / rhs} noexcept;
                    })
                    requires (requires(bool a, bool b, int sign) {
                        {lhs < 0} -> meta::convertible_to<bool>;
                        {rhs < 0} -> meta::convertible_to<bool>;
                        {(a == b) - (a != b)} -> meta::convertible_to<int>;
                        {(lhs + sign * ((rhs + a - b) / 2)) / rhs};
                    })
                {
                    bool a = rhs < 0;
                    bool b = lhs < 0;
                    int sign = (a == b) - (a != b);
                    return ((lhs + sign * ((rhs + a - b) / 2)) / rhs);
                }
            };

            template <typename L, typename R>
            struct half_down {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{((lhs < 0) ^ (rhs < 0)) ?
                        ceil<L, R>{}(lhs - rhs / 2, rhs) :
                        floor<L, R>{}(lhs + rhs / 2, rhs)
                    } noexcept;})
                    requires (requires{{((lhs < 0) ^ (rhs < 0)) ?
                        ceil<L, R>{}(lhs - rhs / 2, rhs) :
                        floor<L, R>{}(lhs + rhs / 2, rhs)
                    };})
                {
                    return (((lhs < 0) ^ (rhs < 0)) ?
                        ceil<L, R>{}(lhs - rhs / 2, rhs) :
                        floor<L, R>{}(lhs + rhs / 2, rhs)
                    );
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct half_down<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{(lhs + ((rhs - 1) / 2)) / rhs} noexcept;})
                    requires (requires{{(lhs + ((rhs - 1) / 2)) / rhs};})
                {
                    return ((lhs + ((rhs - 1) / 2)) / rhs);
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
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(bool a, bool b, int sign) {
                        {lhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {rhs > 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(a == b) - (a != b)} noexcept -> meta::nothrow::convertible_to<int>;
                        {(lhs + sign * ((rhs + a - b) / 2)) / rhs} noexcept;
                    })
                    requires (requires(bool a, bool b, int sign) {
                        {lhs < 0} -> meta::convertible_to<bool>;
                        {rhs > 0} -> meta::convertible_to<bool>;
                        {(a == b) - (a != b)} -> meta::convertible_to<int>;
                        {(lhs + sign * ((rhs + a - b) / 2)) / rhs};
                    })
                {
                    bool a = rhs < 0;
                    bool b = rhs > 0;
                    bool temp = lhs < 0;
                    int sign = (a == temp) - (a != temp);
                    return ((lhs + sign * ((rhs + a - b) / 2)) / rhs);
                }
            };

            template <typename L, typename R>
            struct half_up {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{std::round(lhs / rhs)} noexcept;})
                    requires (requires{{std::round(lhs / rhs)};})
                {
                    return (std::round(lhs / rhs));
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct half_up<L, R> {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires{{(lhs + (rhs / 2)) / rhs} noexcept;})
                    requires (requires{{(lhs + (rhs / 2)) / rhs};})
                {
                    return ((lhs + (rhs / 2)) / rhs);
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
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(bool a, bool b, int sign) {
                        {lhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {rhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(a == b) - (a != b)} noexcept -> meta::nothrow::convertible_to<int>;
                        {(lhs + sign * (rhs / 2)) / rhs} noexcept;
                    })
                    requires (requires(bool a, bool b, int sign) {
                        {lhs < 0} -> meta::convertible_to<bool>;
                        {rhs < 0} -> meta::convertible_to<bool>;
                        {(a == b) - (a != b)} -> meta::convertible_to<int>;
                        {(lhs + sign * (rhs / 2)) / rhs};
                    })
                {
                    bool a = lhs < 0;
                    bool b = rhs < 0;
                    int sign = (a == b) - (a != b);
                    return (lhs + sign * (rhs / 2)) / rhs;
                }
            };

            template <typename L, typename R>
            struct half_even {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(
                        decltype(lhs / rhs) result,
                        decltype(result) whole,
                        decltype(std::modf(result, &whole)) frac
                    ) {
                        {lhs / rhs} noexcept;
                        {std::modf(result, &whole)} noexcept;
                        {
                            std::abs(frac) == 0.5
                        } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                        {whole + std::fmod(whole, 2.0)} noexcept;
                        {std::round(result)} noexcept;
                    })
                    requires (requires(
                        decltype(lhs / rhs) result,
                        decltype(result) whole,
                        decltype(std::modf(result, &whole)) frac
                    ) {
                        {lhs / rhs};
                        {std::modf(result, &whole)};
                        {std::abs(frac) == 0.5} -> meta::explicitly_convertible_to<bool>;
                        {whole + std::fmod(whole, 2.0)};
                        {std::round(result)};
                    })
                {
                    auto result = lhs / rhs;
                    decltype(result) whole;
                    auto frac = std::modf(result, &whole);
                    return (std::abs(frac) == 0.5 ?
                        whole + std::fmod(whole, 2.0) :
                        std::round(result)
                    );
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct half_even<L, R> {
                /* This is equivalent to half_up when the result of normal division toward
                * zero would be odd, and half_down when it would be even.
                */
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(bool odd) {
                        {(lhs / rhs) % 2} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(lhs + ((rhs - !odd) / 2)) / rhs} noexcept;
                    })
                    requires (requires(bool odd) {
                        {(lhs / rhs) % 2} -> meta::convertible_to<bool>;
                        {(lhs + ((rhs - !odd) / 2)) / rhs};
                    })
                {
                    bool odd = (lhs / rhs) % 2;
                    return ((lhs + ((rhs - !odd) / 2)) / rhs);
                }
            };

            template <meta::integer L, meta::integer R>
            struct half_even<L, R> {
                /* This is equivalent to half_up when the result of normal division toward
                * zero would be odd, and half_down when it would be even.
                */
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(bool a, bool b, bool temp, int sign, bool odd) {
                        {rhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {rhs > 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {lhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(a == temp) - (a != temp)} noexcept -> meta::nothrow::convertible_to<int>;
                        {(lhs / rhs) % 2} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(lhs + sign * ((rhs + !odd * (a - b)) / 2)) / rhs} noexcept;
                    })
                    requires (requires(bool a, bool b, bool temp, int sign, bool odd) {
                        {rhs < 0} -> meta::convertible_to<bool>;
                        {rhs > 0} -> meta::convertible_to<bool>;
                        {lhs < 0} -> meta::convertible_to<bool>;
                        {(a == temp) - (a != temp)} -> meta::convertible_to<int>;
                        {(lhs / rhs) % 2} -> meta::convertible_to<bool>;
                        {(lhs + sign * ((rhs + !odd * (a - b)) / 2)) / rhs};
                    })
                {
                    bool a = rhs < 0;
                    bool b = rhs > 0;
                    bool temp = lhs < 0;
                    int sign = (a == temp) - (a != temp);
                    bool odd = (lhs / rhs) % 2;
                    return ((lhs + sign * ((rhs + !odd * (a - b)) / 2)) / rhs);
                }
            };

            template <typename L, typename R>
            struct half_odd {
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(
                        decltype(lhs / rhs) result,
                        decltype(result) whole,
                        decltype(std::modf(result, &whole)) frac
                    ) {
                        {lhs / rhs} noexcept;
                        {std::modf(result, &whole)} noexcept;
                        {
                            std::abs(frac) == 0.5
                        } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                        {whole + std::fmod(whole + 1.0, 2.0)} noexcept;
                        {std::round(result)} noexcept;
                    })
                    requires (requires(
                        decltype(lhs / rhs) result,
                        decltype(result) whole,
                        decltype(std::modf(result, &whole)) frac
                    ) {
                        {lhs / rhs};
                        {std::modf(result, &whole)};
                        {std::abs(frac) == 0.5} -> meta::explicitly_convertible_to<bool>;
                        {whole + std::fmod(whole + 1.0, 2.0)};
                        {std::round(result)};
                    })
                {
                    auto result = lhs / rhs;
                    decltype(result) whole;
                    auto frac = std::modf(result, &whole);
                    return (std::abs(frac) == 0.5 ?
                        whole + std::fmod(whole + 1.0, 2.0) :
                        std::round(result)
                    );
                }
            };

            template <meta::unsigned_integer L, meta::unsigned_integer R>
            struct half_odd<L, R> {
                /* This is equivalent to half_down when the result of normal division
                * toward zero would be odd, and half_up when it would be even.
                */
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(bool odd) {
                        {(lhs / rhs) % 2} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(lhs + ((rhs - odd) / 2)) / rhs} noexcept;
                    })
                    requires (requires(bool odd) {
                        {(lhs / rhs) % 2} -> meta::convertible_to<bool>;
                        {(lhs + ((rhs - odd) / 2)) / rhs};
                    })
                {
                    bool odd = (lhs / rhs) % 2;
                    return ((lhs + ((rhs - odd) / 2)) / rhs);
                }
            };

            template <meta::integer L, meta::integer R>
            struct half_odd<L, R> {
                /* This is equivalent to half_down when the result of normal division
                * toward zero would be odd, and half_up when it would be even.
                */
                static constexpr decltype(auto) operator()(const L& lhs, const R& rhs)
                    noexcept (requires(bool a, bool b, bool temp, int sign, bool odd) {
                        {rhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {rhs > 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {lhs < 0} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(a == temp) - (a != temp)} noexcept -> meta::nothrow::convertible_to<int>;
                        {(lhs / rhs) % 2} noexcept -> meta::nothrow::convertible_to<bool>;
                        {(lhs + sign * ((rhs + odd * (a - b)) / 2)) / rhs} noexcept;
                    })
                    requires (requires(bool a, bool b, bool temp, int sign, bool odd) {
                        {rhs < 0} -> meta::convertible_to<bool>;
                        {rhs > 0} -> meta::convertible_to<bool>;
                        {lhs < 0} -> meta::convertible_to<bool>;
                        {(a == temp) - (a != temp)} -> meta::convertible_to<int>;
                        {(lhs / rhs) % 2} -> meta::convertible_to<bool>;
                        {(lhs + sign * ((rhs + odd * (a - b)) / 2)) / rhs};
                    })
                {
                    bool a = rhs < 0;
                    bool b = rhs > 0;
                    bool temp = lhs < 0;
                    int sign = (a == temp) - (a != temp);
                    bool odd = (lhs / rhs) % 2;
                    return (lhs + sign * ((rhs + odd * (a - b)) / 2)) / rhs;
                }
            };

        }

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
            math::pow<T, T, T> pow;
            T x = pow(a, d, n);
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

}


namespace math {

    /// TODO: these public operators should not be defined until op.h, so that they
    /// can be exposed as uniform `def` functions.


    /* A generalized absolute value operator.  The behavior of this operator is controlled
    by the `impl::abs<T>` control struct, which is always specialized for integer and
    floating point types at a minimum. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) abs(const T& obj)
        noexcept (requires{{impl::math::abs<meta::unqualify<T>>{}(obj)} noexcept;})
        requires (requires{{impl::math::abs<meta::unqualify<T>>{}(obj)};})
    {
        return (impl::math::abs<meta::unqualify<T>>{}(obj));
    }

    /* A generalized exponentiation operator.  The behavior of this operator is controlled
    by the `impl::pow<Base, Exp, Mod = void>` control struct, which is always specialized
    for integer and floating point types at a minimum.  Note that integer exponentiation
    with a negative exponent always returns zero, without erroring. */
    template <typename Base, typename Exp>
    [[nodiscard]] constexpr decltype(auto) pow(const Base& base, const Exp& exp)
        noexcept (requires{
            {impl::math::pow<meta::unqualify<Base>, meta::unqualify<Exp>>{}(base, exp)} noexcept;
        })
        requires (requires{
            {impl::math::pow<meta::unqualify<Base>, meta::unqualify<Exp>>{}(base, exp)};
        })
    {
        return (impl::math::pow<meta::unqualify<Base>, meta::unqualify<Exp>>{}(base, exp));
    }

    /* A generalized modular exponentiation operator.  The behavior of this operator is
    controlled by the `impl::pow<Base, Exp, Mod = void>` control struct, which is always
    specialized for integer and floating point types at a minimum.  Note that modular
    exponentiation with a negative exponent always fails with an `ArithmeticError`, as
    there is no general solution for the modular case.  Also, providing a modulus equal to
    zero will cause a `ZeroDivisionError`. */
    template <typename Base, typename Exp, typename Mod>
    [[nodiscard]] constexpr decltype(auto) pow(const Base& base, const Exp& exp, const Mod& mod)
        noexcept (requires{
            {impl::check_for_negative_exponent(exp)} noexcept;
            {impl::check_for_zero_division(mod)} noexcept;
            {impl::math::pow<
                meta::unqualify<Base>,
                meta::unqualify<Exp>,
                meta::unqualify<Mod>
            >{}(base, exp, mod)} noexcept;
        })
        requires (requires{
            {impl::check_for_negative_exponent(exp)};
            {impl::check_for_zero_division(mod)};
            {impl::math::pow<
                meta::unqualify<Base>,
                meta::unqualify<Exp>,
                meta::unqualify<Mod>
            >{}(base, exp, mod)};
        })
    {
        impl::check_for_negative_exponent(exp);
        impl::check_for_zero_division(mod);
        return (impl::math::pow<
            meta::unqualify<Base>,
            meta::unqualify<Exp>,
            meta::unqualify<Mod>
        >{}(base, exp, mod));
    }

    /// TODO: log<b>()
    /// TODO: root<b>()
    /// TODO: prime()
    /// TODO: prime.next()
    /// TODO: prime.prev()

    /// TODO: sin/cos/tan, gcd, lcm, etc.

    /// TODO: a generalized way to do this would be very helpful for the numeric types in
    /// bits.h, since they require logarithms to calculate required digits, and sqrt for
    /// implementing soft float division.



    /* A family of division operators with different rounding strategies, in order to
    facilitate inter-language communication where conventions may differ.  Numeric
    algorithms that use these operators are guaranteed to behave consistently from one
    language to another. */
    inline constexpr struct {
        /* Divide two numbers, returning a floating point approximation of the true
        quotient.  This is the rounding strategy used by Python's `/` operator. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) real(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::real<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::real<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::real<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers according to C++ semantics.  For integers, this performs
        truncated division toward zero.  Otherwise, it is identical to "true" division. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) cpp(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::cpp<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::cpp<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::cpp<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward negative infinity.  This is
        the rounding strategy used by Python's `//` operator. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) floor(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::floor<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::floor<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::floor<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward positive infinity. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) ceil(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::ceil<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::ceil<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::ceil<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward zero. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) down(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::down<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::down<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::down<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient away from zero. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) up(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::up<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::up<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::up<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward negative infinity. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_floor(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::half_floor<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::half_floor<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::half_floor<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward positive infinity. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_ceil(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::half_ceil<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::half_ceil<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::half_ceil<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward zero. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_down(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::half_down<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::half_down<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::half_down<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties away from zero. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_up(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::half_up<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::half_up<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::half_up<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward the nearest even number. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_even(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::half_even<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::half_even<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::half_even<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward the nearest odd number. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_odd(const L& lhs, const R& rhs)
            noexcept (requires{
                {impl::check_for_zero_division(rhs)} noexcept;
                {impl::math::div::half_odd<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)} noexcept;
            })
            requires (requires{
                {impl::check_for_zero_division(rhs)};
                {impl::math::div::half_odd<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs)};
            })
        {
            impl::check_for_zero_division(rhs);
            return (impl::math::div::half_odd<meta::unqualify<L>, meta::unqualify<R>>{}(lhs, rhs));
        }
    } div;

    /* A family of modulus operators with different rounding strategies, in order to
    facilitate inter-language communication where conventions may differ.  Numeric
    algorithms that use these operators are guaranteed to behave consistently from one
    language to another. */
    inline constexpr struct {
        /* Divide two numbers, returning a floating point approximation of the remainder.
        This will always be equivalent to the floating point error of the division (i.e.
        negligible, but not necessarily zero). */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) real(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.real(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.real(lhs, rhs) * rhs};})
        {
            return (lhs - div.real(lhs, rhs) * rhs);
        }

        /* Divide two numbers, returning the remainder according to C++ semantics.  For
        integers, this returns the remainder of truncated division toward zero.  Otherwise,
        it is identical to a "true" modulus (i.e. floating point error) of a division. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) cpp(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.cpp(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.cpp(lhs, rhs) * rhs};})
        {
            return (lhs - div.cpp(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward negative infinity and returning
        the remainder.  This is the rounding strategy used by Python's `%` operator. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) floor(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.floor(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.floor(lhs, rhs) * rhs};})
        {
            return (lhs - div.floor(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward positive infinity and returning
        the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) ceil(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.ceil(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.ceil(lhs, rhs) * rhs};})
        {
            return (lhs - div.ceil(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward zero and returning the
        remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) down(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.down(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.down(lhs, rhs) * rhs};})
        {
            return (lhs - div.down(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient away from zero and returning the
        remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) up(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.up(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.up(lhs, rhs) * rhs};})
        {
            return (lhs - div.up(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward negative infinity, and returning the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_floor(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.half_floor(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.half_floor(lhs, rhs) * rhs};})
        {
            return (lhs - div.half_floor(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward positive infinity, and returning the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_ceil(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.half_ceil(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.half_ceil(lhs, rhs) * rhs};})
        {
            return (lhs - div.half_ceil(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward zero, and returning the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_down(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.half_down(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.half_down(lhs, rhs) * rhs};})
        {
            return (lhs - div.half_down(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties away from zero, and returning the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_up(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.half_up(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.half_up(lhs, rhs) * rhs};})
        {
            return (lhs - div.half_up(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward the nearest even number. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_even(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.half_even(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.half_even(lhs, rhs) * rhs};})
        {
            return (lhs - div.half_even(lhs, rhs) * rhs);
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward the nearest odd number. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr decltype(auto) half_odd(const L& lhs, const R& rhs)
            noexcept (requires{{lhs - div.half_odd(lhs, rhs) * rhs} noexcept;})
            requires (requires{{lhs - div.half_odd(lhs, rhs) * rhs};})
        {
            return (lhs - div.half_odd(lhs, rhs) * rhs);
        }
    } mod;

    /* A family of combined division and modulus operators with different rounding
    strategies, in order to facilitate inter-language communication where conventions may
    differ.  Numeric algorithms that use these operators are guaranteed to behave
    consistently from one language to another. */
    inline constexpr struct {
        /* Divide two numbers, returning both the quotient and remainder as floating point
        approximations.  The remainder will always be equivalent to the floating point
        error of the division (i.e. negligible, but not necessarily zero). */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto real(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.real(lhs, rhs),
                lhs - (div.real(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.real(lhs, rhs),
                lhs - (div.real(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.real(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, returning both the quotient and remainder according to C++
        semantics.  For integers, this returns the result of truncated division toward
        zero.  Otherwise, it is identical to a "true" division and modulus (floating point
        error). */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto cpp(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.cpp(lhs, rhs),
                lhs - (div.cpp(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.cpp(lhs, rhs),
                lhs - (div.cpp(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.cpp(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward negative infinity and returning
        it along with the remainder.  This is the rounding strategy used by Python's
        `divmod` operator. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto floor(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.floor(lhs, rhs),
                lhs - (div.floor(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.floor(lhs, rhs),
                lhs - (div.floor(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.floor(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward positive infinity and returning
        it along with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto ceil(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.ceil(lhs, rhs),
                lhs - (div.ceil(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.ceil(lhs, rhs),
                lhs - (div.ceil(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.ceil(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward zero and returning it along
        with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto down(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.down(lhs, rhs),
                lhs - (div.down(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.down(lhs, rhs),
                lhs - (div.down(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.down(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient away from zero and returning it along
        with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto up(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.up(lhs, rhs),
                lhs - (div.up(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.up(lhs, rhs),
                lhs - (div.up(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.up(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward negative infinity, and returning it along with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto half_floor(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.half_floor(lhs, rhs),
                lhs - (div.half_floor(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.half_floor(lhs, rhs),
                lhs - (div.half_floor(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.half_floor(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward positive infinity, and returning it along with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto half_ceil(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.half_ceil(lhs, rhs),
                lhs - (div.half_ceil(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.half_ceil(lhs, rhs),
                lhs - (div.half_ceil(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.half_ceil(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward zero, and returning it along with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto half_down(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.half_down(lhs, rhs),
                lhs - (div.half_down(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.half_down(lhs, rhs),
                lhs - (div.half_down(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.half_down(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties away from zero, and returning it along with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto half_up(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.half_up(lhs, rhs),
                lhs - (div.half_up(lhs, rhs) * rhs)
            )} noexcept;})
        {
            auto quotient = div.half_up(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward the nearest even number, and returning it along with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto half_even(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.half_even(lhs, rhs),
                lhs - (div.half_even(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.half_even(lhs, rhs),
                lhs - (div.half_even(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.half_even(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        /* Divide two numbers, rounding the quotient toward the nearest whole number, with
        ties toward the nearest odd number, and returning it along with the remainder. */
        template <typename L, typename R>
        [[nodiscard]] static constexpr auto half_odd(const L& lhs, const R& rhs)
            noexcept (requires{{std::make_pair(
                div.half_odd(lhs, rhs),
                lhs - (div.half_odd(lhs, rhs) * rhs)
            )} noexcept;})
            requires (requires{{std::make_pair(
                div.half_odd(lhs, rhs),
                lhs - (div.half_odd(lhs, rhs) * rhs)
            )};})
        {
            auto quotient = div.half_odd(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }
    } divmod;

    /* A family of rounding operators with different strategies, in order to facilitate
    inter-language communication where conventions may differ.  Numeric algorithms that
    use these operators are guaranteed to behave consistently from one language to
    another. */
    inline constexpr struct {
        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  For "true" rounding, this effectively does
        nothing, and simply corrects for possible floating point error with respect to
        the digit count. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) real(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::real<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::real<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::real<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::real<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::real<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::real<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  For "cpp" rounding, this only appreciably
        impacts integer values, in which case it is identical to rounding down (toward
        zero) by the digit count.  Otherwise, it is a simple floating point error
        correction, similar to "true" rounding. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) cpp(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::cpp<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::cpp<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::cpp<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::cpp<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::cpp<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::cpp<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward negative infinity,
        as if floor dividing by a power of 10, and then rescaling by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) floor(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::floor<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::floor<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::floor<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::floor<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::floor<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::floor<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward positive infinity,
        as if ceil dividing by a power of 10, and then rescaling by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) ceil(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::ceil<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::ceil<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::ceil<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::ceil<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::ceil<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::ceil<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward zero, as if
        dividing down by a power of 10, and then rescaling by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) down(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::down<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::down<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::down<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::down<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::down<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::down<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded away from zero, as if
        dividing up by a power of 10, and then rescaling by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) up(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::up<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::up<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::up<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::up<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::up<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::up<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward the nearest whole
        number, as if performing half-floor division by a power of 10, and then rescaling
        by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) half_floor(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::half_floor<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::half_floor<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::half_floor<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::half_floor<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::half_floor<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::half_floor<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward the nearest whole
        number, as if performing half-ceiling division by a power of 10, and then rescaling
        by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) half_ceil(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::half_ceil<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::half_ceil<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::half_ceil<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::half_ceil<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::half_ceil<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::half_ceil<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward the nearest whole
        number, as if performing half-down division by a power of 10, and then rescaling
        by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) half_down(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::half_down<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::half_down<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::half_down<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::half_down<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::half_down<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::half_down<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward the nearest whole
        number, as if performing half-up division by a power of 10, and then rescaling
        by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) half_up(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::half_up<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::half_up<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::half_up<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::half_up<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::half_up<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::half_up<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward the nearest whole
        number, as if performing half-even division by a power of 10, and then rescaling
        by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) half_even(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::half_even<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::half_even<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::half_even<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::half_even<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::half_even<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::half_even<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }

        /* Round a value to the specified number of digits as a power of 10, defaulting
        to the ones place.  Positive digits count to the right of the decimal place, and
        negative digits count to the left.  Results are rounded toward the nearest whole
        number, as if performing half-odd division by a power of 10, and then rescaling
        by the same factor. */
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) half_odd(const T& obj, int digits = 0)
            noexcept (requires(long long scale) {
                {impl::math::div::half_odd<meta::unqualify<T>, long long>{}(obj * scale, scale)} noexcept;
                {impl::math::div::half_odd<meta::unqualify<T>, long long>{}(obj, scale) * scale} noexcept;
            })
            requires (requires(long long scale) {
                {impl::math::div::half_odd<meta::unqualify<T>, long long>{}(obj * scale, scale)};
                {impl::math::div::half_odd<meta::unqualify<T>, long long>{}(obj, scale) * scale};
            })
        {
            if (digits > 0) {
                long long scale = pow(10LL, digits);
                return (impl::math::div::half_odd<meta::unqualify<T>, long long>{}(obj * scale, scale));
            }
            long long scale = pow(10LL, -digits);
            return (impl::math::div::half_odd<meta::unqualify<T>, long long>{}(obj, scale) * scale);
        }
    } round;

}


}


#endif