#ifndef BERTRAND_PYTHON_MATH_H
#define BERTRAND_PYTHON_MATH_H

#include <cmath>

#include "common.h"
#include "int.h"
#include "float.h"


// TODO: allow for complex/Decimal operands?


namespace bertrand {
namespace py {


namespace impl {

    template <typename L, typename R, typename Mode>
    concept div_mode = requires(const L& lhs, const R& rhs, const Mode& mode) {
        mode.div(lhs, rhs);
    };

    template <typename L, typename R, typename Mode>
    concept mod_mode = requires(const L& lhs, const R& rhs, const Mode& mode) {
        mode.mod(lhs, rhs);
    };

    template <typename L, typename R, typename Mode>
    concept divmod_mode = requires(const L& lhs, const R& rhs, const Mode& mode) {
        mode.divmod(lhs, rhs);
    };

    template <typename O, typename Mode>
    concept round_mode = requires(const O& obj, const Mode& mode) {
        mode.round(obj);
    };

    inline size_t pow_int(size_t base, size_t exp) {
        size_t result = 1;
        while (exp > 0) {
            if (exp % 2 == 1) {
                result *= base;
            }
            base *= base;
            exp /= 2;
        }
        return result;
    }

}


/* A collection of tag structs used to implement cross-language rounding strategies for
basic `div()`, `mod()`, `divmod()`, and `round()` operators. */
struct Round : public impl::BertrandTag {

    struct True {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                return lhs / rhs;
            } else {
                if constexpr (std::integral<L> && std::integral<R>) {
                    return static_cast<double>(lhs) / rhs;
                } else {
                    return lhs / rhs;
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            return lhs - (div(lhs, rhs) * rhs);
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            auto quotient = div(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        template <typename T>
        static auto round(const T& obj) {
            return obj;
        }

    };

    struct CDiv {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if (isinstance<Int>(lhs) && isinstance<Int>(rhs)) {
                    Int a = lhs, b = rhs;
                    Int quotient = impl::floordiv(a, b);
                    if ((quotient < Int::zero) && ((a % b) != Int::zero)) {
                        ++quotient;
                    }
                    return quotient;
                } else {
                    return lhs / rhs;
                }

            } else {
                return lhs / rhs;
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if (isinstance<Int>(lhs) && isinstance<Int>(rhs)) {
                    Int a = lhs, b = rhs;
                    Int remainder = a % b;
                    if ((a < Int::zero) && (remainder != Int::zero)) {
                        return -remainder;
                    }
                    return remainder;
                } else {
                    return lhs - ((lhs / rhs) * rhs);
                }

            } else {
                if constexpr (std::floating_point<L> || std::floating_point<R>) {
                    return std::fmod(lhs, rhs);
                } else {
                    return lhs % rhs;
                }
            }
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if (isinstance<Int>(lhs) && isinstance<Int>(rhs)) {
                    Int a = lhs, b = rhs;
                    Int quotient = impl::floordiv(a, b);
                    Int remainder = a % b;
                    if ((quotient < Int::zero) && (remainder != Int::zero)) {
                        ++quotient;
                        return std::make_pair(quotient, -remainder);
                    }
                    return std::make_pair(quotient, remainder);

                } else {
                    auto quotient = lhs / rhs;
                    return std::make_pair(quotient, lhs - (quotient * rhs));
                }

            } else {
                if constexpr (std::floating_point<L> || std::floating_point<R>) {
                    return std::make_pair(lhs / rhs, std::fmod(lhs, rhs));
                } else {
                    return std::make_pair(lhs / rhs, lhs % rhs);
                }
            }
        }

        template <typename T>
        static auto round(const T& obj) {
            return obj;
        }

    };

    struct Floor {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                return impl::floordiv(lhs, rhs);

            } else {
                auto quotient = lhs / rhs;
                if constexpr (std::unsigned_integral<L> && std::unsigned_integral<R>) {
                    return quotient;
                } else if constexpr (std::integral<L> && std::integral<R>) {
                    return quotient - (((lhs < 0) ^ (rhs < 0)) && ((lhs % rhs) != 0));
                } else {
                    return std::floor(quotient);
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                return lhs % rhs;

            } else {
                auto correct = rhs * ((lhs < 0) ^ (rhs < 0));
                if constexpr (std::floating_point<L> || std::floating_point<R>) {
                    return std::fmod(lhs, rhs) + correct;
                } else {
                    return (lhs % rhs) + correct;
                }
            }
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            return std::make_pair(div(lhs, rhs), mod(lhs, rhs));
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                return div(obj, Int::one);
            } else {
                return std::floor(obj);
            }
        }

    };

    struct Ceiling {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                return -impl::floordiv(-lhs, rhs);

            } else {
                auto quotient = lhs / rhs;
                if constexpr (std::integral<L> && std::integral<R>) {
                    return quotient + (((lhs < 0) == (rhs < 0)) && ((lhs % rhs) != 0));
                } else {
                    return std::ceil(quotient);
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                return -(-lhs % rhs);

            } else {
                auto correct = rhs * ((lhs < 0) == (rhs < 0));
                if constexpr (std::floating_point<L> || std::floating_point<R>) {
                    return std::fmod(lhs, rhs) - correct;
                } else {
                    return (lhs % rhs) - correct;
                }
            }
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                auto temp = -lhs;
                return std::make_pair(
                    -impl::floordiv(temp, rhs),
                    -(temp % rhs)
                );

            } else {
                return std::make_pair(div(lhs, rhs), mod(lhs, rhs));
            }
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                return div(obj, Int::one);
            } else {
                return std::ceil(obj);
            }
        }

    };

    struct Down {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if ((lhs < Int::zero) ^ (rhs < Int::zero)) {
                    return -impl::floordiv(-lhs, rhs);
                } else {
                    return impl::floordiv(lhs, rhs);
                }

            } else {
                if ((lhs < 0) ^ (rhs < 0)) {
                    return Ceiling::div(lhs, rhs);
                } else {
                    return Floor::div(lhs, rhs);
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if ((lhs < Int::zero) ^ (rhs < Int::zero)) {
                    return -(-lhs % rhs);
                } else {
                    return lhs % rhs;
                }

            } else {
                if ((lhs < 0) ^ (rhs < 0)) {
                    return Ceiling::mod(lhs, rhs);
                } else {
                    return Floor::mod(lhs, rhs);
                }
            }
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if ((lhs < Int::zero) ^ (rhs < Int::zero)) {
                    auto temp = -lhs;
                    return std::make_pair(
                        -impl::floordiv(temp, rhs),
                        -(temp % rhs)
                    );
                } else {
                    return std::make_pair(
                        impl::floordiv(lhs, rhs),
                        lhs % rhs
                    );
                }

            } else {
                if ((lhs < 0) ^ (rhs < 0)) {
                    return Ceiling::divmod(lhs, rhs);
                } else {
                    return Floor::divmod(lhs, rhs);
                }
            }
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                return div(obj, Int::one);
            } else {
                return std::trunc(obj);
            }   
        }

    };

    struct Up {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if ((lhs < Int::zero) ^ (rhs < Int::zero)) {
                    return impl::floordiv(lhs, rhs);
                } else {
                    return -impl::floordiv(-lhs, rhs);
                }

            } else {
                if ((lhs < 0) ^ (rhs < 0)) {
                    return Floor::div(lhs, rhs);
                } else {
                    return Ceiling::div(lhs, rhs);
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if ((lhs < Int::zero) ^ (rhs < Int::zero)) {
                    return lhs % rhs;
                } else {
                    return -(-lhs % rhs);
                }

            } else {
                if ((lhs < 0) ^ (rhs < 0)) {
                    return Floor::mod(lhs, rhs);
                } else {
                    return Ceiling::mod(lhs, rhs);
                }
            }
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if ((lhs < Int::zero) ^ (rhs < Int::zero)) {
                    return std::make_pair(
                        impl::floordiv(lhs, rhs),
                        lhs % rhs
                    );
                } else {
                    auto temp = -lhs;
                    return std::make_pair(
                        -impl::floordiv(temp, rhs),
                        -(temp % rhs)
                    );
                }

            } else {
                if ((lhs < 0) ^ (rhs < 0)) {
                    return Floor::divmod(lhs, rhs);
                } else {
                    return Ceiling::divmod(lhs, rhs);
                }
            }
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                return div(obj, Int::one);
            } else {
                if (obj < 0) {
                    return std::floor(obj);
                } else {
                    return std::ceil(obj);
                }
            }
        }

    };

    struct HalfFloor {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if (isinstance<Int>(lhs) && isinstance<Int>(rhs)) {
                    auto bias = impl::floordiv(rhs + ~(rhs > Int::zero), Int::two);
                    return impl::floordiv(lhs + bias, rhs);
                    /*   a ~a    (rhs + ~a) // 2
                    *   0 -1  = (rhs -  1) // 2  =  (rhs - 1) // 2 
                    *   0 -1  = (rhs -  1) // 2  =  (rhs - 1) // 2
                    *   1 -2  = (rhs -  2) // 2  =  (rhs - 2) // 2
                    *   1 -2  = (rhs -  2) // 2  =  (rhs - 2) // 2
                    */
                } else {
                    return -impl::floordiv(-lhs + rhs / Int::two, rhs);
                }
            } else {
                if constexpr (std::integral<L> && std::integral<R>) {
                    bool a = lhs < 0;
                    bool b = rhs > 0;
                    return (lhs + ((a != b) - (a == b)) * ((rhs + a - b) / 2)) / rhs;
                    /*   sign  a  b    sign * ((rhs + a - b) / 2)
                    *    1    1  0  =   1  * ((rhs + 1 - 0) / 2)  =   ((rhs + 1) / 2)
                    *   -1    1  1  =  -1  * ((rhs + 1 - 1) / 2)  =  -((rhs    ) / 2)
                    *    1    0  1  =   1  * ((rhs + 0 - 1) / 2)  =   ((rhs - 1) / 2)
                    *   -1    0  0  =  -1  * ((rhs + 0 - 0) / 2)  =  -((rhs    ) / 2)
                    */
                } else {
                    return Ceiling::div(lhs - rhs / 2, rhs);
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            return lhs - div(lhs, rhs) * rhs;
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            auto quotient = div(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                return Ceiling::round(obj - Float::half);
            } else {
                return Ceiling::round(obj - 0.5);
            }
        }

    };

    struct HalfCeiling {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if (isinstance<Int>(lhs) && isinstance<Int>(rhs)) {
                    auto bias = impl::floordiv(
                        rhs + (rhs > Int::zero),
                        Int::two
                    );
                    return impl::floordiv(lhs + bias, rhs);
                    /*   a    (rhs + a) // 2
                    *   0  = (rhs + 0) // 2  =  (rhs    ) // 2 
                    *   0  = (rhs + 0) // 2  =  (rhs    ) // 2
                    *   1  = (rhs + 1) // 2  =  (rhs + 1) // 2
                    *   1  = (rhs + 1) // 2  =  (rhs + 1) // 2
                    */
                } else {
                    return impl::floordiv(lhs + rhs / Int::two, rhs);
                }

            } else {
                if constexpr (std::integral<L> && std::integral<R>) {
                    bool a = lhs < 0;
                    bool b = rhs > 0;
                    return (lhs + ((a != b) - (a == b)) * ((rhs + ~(a - b)) / 2)) / rhs;
                    /*   sign  a  b    sign * ((rhs + ~(a - b)) / 2)
                    *    1    1  0  =   1  * ((rhs + ~(1 - 0)) / 2)  =   ((rhs - 2) / 2)
                    *   -1    1  1  =  -1  * ((rhs + ~(1 - 1)) / 2)  =  -((rhs - 1) / 2)
                    *    1    0  1  =   1  * ((rhs + ~(0 - 1)) / 2)  =   ((rhs    ) / 2)
                    *   -1    0  0  =  -1  * ((rhs + ~(0 - 0)) / 2)  =  -((rhs - 1) / 2)
                    */
                } else {
                    return Floor::div(lhs + rhs / 2, rhs);
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            return lhs - div(lhs, rhs) * rhs;
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            auto quotient = div(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                return Floor::round(obj + Float::half);
            } else {
                return Floor::round(obj + 0.5);
            }
        }

    };

    struct HalfDown {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if constexpr (isinstance<Int>(lhs) && isinstance<Int>(rhs)) {
                    auto a = rhs > Int::zero;
                    auto b = lhs > Int::zero;
                    if (a == b) {  // NOTE: branches are faster than writing more Python
                        auto bias = impl::floordiv(rhs + ~a, Int::two);
                        return impl::floordiv(lhs + bias, rhs);
                    } else {
                        auto bias = impl::floordiv(rhs + a, Int::two);
                        return impl::floordiv(lhs + bias, rhs);
                    }
                } else {
                    auto bias = rhs / Int::two;
                    if ((lhs < Int::zero) ^ (rhs < Int::zero)) {
                        return impl::floordiv(lhs + bias, rhs);
                    } else {
                        return -impl::floordiv(-lhs + bias, rhs);
                    }
                }

            } else {
                if constexpr (std::integral<L> && std::integral<R>) {
                    bool a = lhs < 0;
                    bool b = rhs > 0;
                    int c = a - b;
                    bool d = a != b;
                    bool e = a == b;
                    return (lhs + (d - e) * ((rhs + c * d + ~c * e) / 2)) / rhs;
                } else {
                    if ((lhs < 0) ^ (rhs < 0)) {
                        return Ceiling::div(lhs - rhs / 2, rhs);
                    } else {
                        return Floor::div(lhs + rhs / 2, rhs);
                    }
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            return lhs - div(lhs, rhs) * rhs;
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            auto quotient = div(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                if (obj > Int::zero) {
                    return HalfFloor::round(obj);
                } else {
                    return HalfCeiling::round(obj);
                }
            } else {
                if (obj > 0) {
                    return HalfFloor::round(obj);
                } else {
                    return HalfCeiling::round(obj);
                }
            }
        }

    };

    struct HalfUp {

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if constexpr (isinstance<Int>(lhs) && isinstance<Int>(rhs)) {
                    auto a = rhs > Int::zero;
                    auto b = lhs > Int::zero;
                    if (a == b) {  // NOTE: branches are faster than writing more Python
                        auto bias = impl::floordiv(rhs + a, Int::two);
                        return impl::floordiv(lhs + bias, rhs);
                    } else {
                        auto bias = impl::floordiv(rhs + ~a, Int::two);
                        return impl::floordiv(lhs + bias, rhs);
                    }

                } else {
                    auto bias = rhs / Int::two;
                    if ((lhs < Int::zero) ^ (rhs < Int::zero)) {
                        return -impl::floordiv(-lhs + bias, rhs);
                    } else {
                        return impl::floordiv(lhs + bias, rhs);
                    }
                }
            } else {
                if constexpr (std::integral<L> && std::integral<R>) {
                    bool a = lhs < 0;
                    bool b = rhs > 0;
                    int c = a - b;
                    bool d = a != b;
                    bool e = a == b;
                    return (lhs + (d - e) * ((rhs + ~c * d + c * e) / 2)) / rhs;
                } else {
                    if ((lhs < 0) ^ (rhs < 0)) {
                        return Floor::div(lhs + rhs / 2, rhs);
                    } else {
                        return Ceiling::div(lhs - rhs / 2, rhs);
                    }
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            return lhs - div(lhs, rhs) * rhs;
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            auto quotient = div(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                if (obj > Int::zero) {
                    return HalfCeiling::round(obj);
                } else {
                    return HalfFloor::round(obj);
                }
            } else {
                return std::round(obj);  // always rounds away from zero
            }
        }

    };

    class HalfEven {

        inline static auto py_round = [] {
            PyObject* builtins = PyEval_GetBuiltins();
            PyObject* round = PyDict_GetItemString(builtins, "round");
            if (round == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(round);
        }();

    public:

        template <typename L, typename R>
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::any_are_python_like<L, R>) {
                if constexpr (isinstance<Int>(lhs) && isinstance<Int>(rhs)) {
                    auto a = rhs > Int::zero;
                    if (impl::floordiv(lhs, rhs) % Int::two) {
                        auto bias = impl::floordiv(rhs + a, Int::two);
                        return impl::floordiv(lhs + bias, rhs);
                    } else {
                        auto bias = impl::floordiv(rhs + ~a, Int::two);
                        return impl::floordiv(lhs + bias, rhs);
                    }

                } else {
                    auto bias = rhs / Int::two;
                    if (impl::floordiv(lhs, rhs) % Int::two) {
                        return impl::floordiv(lhs + bias, rhs);
                    } else {
                        return -impl::floordiv(-lhs + bias, rhs);
                    }
                }

            } else {
                if constexpr (std::integral<L> && std::integral<R>) {
                    bool a = lhs < 0;
                    bool b = rhs > 0;
                    int c = a - b;
                    int sign = (a != b) - (a == b);
                    bool odd = ((lhs - rhs / 2) / rhs) % 2;
                    return (lhs + sign * ((rhs + c * (!odd) + ~c * odd) / 2)) / rhs;
                } else {
                    bool odd = (lhs - rhs / 2) / rhs % 2;
                    if (odd) {
                        return Ceiling::div(lhs, rhs);
                    } else {
                        return Floor::div(lhs, rhs);
                    }
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            return lhs - div(lhs, rhs) * rhs;
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            auto quotient = div(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        template <typename T>
        static auto round(const T& obj) {
            if constexpr (impl::python_like<T>) {
                PyObject* result = PyObject_CallOneArg(py_round.ptr(), obj.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<T>(result);
            } else {
                T whole, fract;
                fract = std::modf(obj, &whole);
                if (std::abs(fract) == 0.5) {
                    // NOTE: fmod evaluates negative if whole < 0
                    return whole + std::fmod(whole, 2);
                } else {
                    return std::round(obj);
                }
            }
        }

    };

    static constexpr True TRUE {};
    static constexpr CDiv C {};
    static constexpr Floor FLOOR {};
    static constexpr Ceiling CEILING {};
    static constexpr Down DOWN {};
    static constexpr Up UP {};
    static constexpr HalfFloor HALF_FLOOR {};
    static constexpr HalfCeiling HALF_CEILING {};
    static constexpr HalfDown HALF_DOWN {};
    static constexpr HalfUp HALF_UP {};
    static constexpr HalfEven HALF_EVEN {};

};


/* Divide the left and right operands according to the specified rounding rule. */
template <typename Mode, typename L, typename R>
    requires (impl::div_mode<L, R, Mode>)
[[nodiscard]] auto div(const L& lhs, const R& rhs) {
    if constexpr (impl::proxy_like<L>) {
        return div(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return div(lhs, rhs.value());
    } else {
        if constexpr (!impl::any_are_python_like<L, R>) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
        }
        return Mode::div(lhs, rhs);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode>
    requires (impl::div_mode<L, R, Mode>)
[[nodiscard]] auto div(const L& lhs, const R& rhs, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return div(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return div(lhs, rhs.value());
    } else {
        if constexpr (!impl::any_are_python_like<L, R>) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
        }
        return mode.div(lhs, rhs);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename Mode, typename L, typename R>
    requires (impl::mod_mode<L, R, Mode>)
[[nodiscard]] auto mod(const L& lhs, const R& rhs) {
    if constexpr (impl::proxy_like<L>) {
        return mod(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return mod(lhs, rhs.value());
    } else {
        if constexpr (!impl::any_are_python_like<L, R>) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
        }
        return Mode::mod(lhs, rhs);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode>
    requires (impl::mod_mode<L, R, Mode>)
[[nodiscard]] auto mod(const L& lhs, const R& rhs, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return mod(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return mod(lhs, rhs.value());
    } else {
        if constexpr (!impl::any_are_python_like<L, R>) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
        }
        return mode.mod(lhs, rhs);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename Mode, typename L, typename R>
    requires (impl::divmod_mode<L, R, Mode>)
[[nodiscard]] auto divmod(const L& lhs, const R& rhs) {
    if constexpr (impl::proxy_like<L>) {
        return divmod(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return divmod(lhs, rhs.value());
    } else {
        if constexpr (!impl::any_are_python_like<L, R>) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
        }
        return Mode::divmod(lhs, rhs);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode>
    requires (impl::divmod_mode<L, R, Mode>)
[[nodiscard]] auto divmod(const L& lhs, const R& rhs, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return divmod(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return divmod(lhs, rhs.value());
    } else {
        if constexpr (!impl::any_are_python_like<L, R>) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
        }
        return mode.divmod(lhs, rhs);
    }
}


/* Round the operand to a given number of digits according to the specified rounding
rule.  Positive digits count to the right of the decimal point, while negative values
count to the left. */
template <typename Mode, typename T>
    requires (impl::round_mode<T, Mode>)
[[nodiscard]] auto round(const T& obj, int digits = 0) {
    if constexpr (impl::proxy_like<T>) {
        return round<Mode>(obj.value(), digits);
    } else {
        // negative digits uniformly divide by a power of 10 with rounding
        if (digits < 0) {
            if constexpr (std::derived_from<T, Object>) {
                Int scale = impl::pow_int(10, -digits);
                return Mode::div(obj, scale) * scale;
            } else {
                size_t scale = impl::pow_int(10, -digits);
                return Mode::div(obj, scale) * scale;
            }
        }

        // positive digits do not affect discrete values
        if constexpr (impl::bool_like<T> || impl::int_like<T>) {
            return obj;

        // Python objects are split into whole/fractional parts to avoid precision loss
        } else if constexpr (impl::python_like<T>) {
            // dynamic objects are checked for bool/integer-ness
            if constexpr (impl::is_object_exact<T>) {
                if (PyLong_Check(obj.ptr())) {
                    return obj;
                }
            }
            Int scale = impl::pow_int(10, digits);
            Object whole = Round::Floor::div(obj, Int::one);
            return whole + (Mode::round((obj - whole) * scale) / scale);

        // C++ floats can use an STL method to avoid precision loss
        } else if constexpr (std::floating_point<T>) {
            size_t scale = impl::pow_int(10, digits);
            T whole, fract;
            fract = std::modf(obj, &whole);
            return whole + (Mode::round(fract * scale) / scale);

        // all other C++ objects are handled similar to Python objects
        } else {
            size_t scale = impl::pow_int(10, digits);
            auto whole = Round::Floor::div(obj, 1);
            return whole + (Mode::round((obj - whole) * scale) / scale);
        }
    }
}


/* Round the operand to a given number of digits according to the specified rounding
rule.  Positive digits count to the right of the decimal point, while negative values
count to the left. */
template <typename T, typename Mode>
    requires (impl::round_mode<T, Mode>)
[[nodiscard]] auto round(const T& obj, int digits = 0, const Mode& mode = Round::HALF_EVEN) {
    if constexpr (impl::proxy_like<T>) {
        return round(obj.value(), digits, mode);
    } else {
        // negative digits uniformly divide by a power of 10 with rounding
        if (digits < 0) {
            if constexpr (std::derived_from<T, Object>) {
                Int scale = impl::pow_int(10, -digits);
                return mode.div(obj, scale) * scale;
            } else {
                size_t scale = impl::pow_int(10, -digits);
                return mode.div(obj, scale) * scale;
            }
        }

        // positive digits do not affect discrete values
        if constexpr (impl::bool_like<T> || impl::int_like<T>) {
            return obj;

        // Python objects are split into whole/fractional parts to avoid precision loss
        } else if constexpr (impl::python_like<T>) {
            // dynamic objects are checked for bool/integer-ness
            if constexpr (impl::is_object_exact<T>) {
                if (PyLong_Check(obj.ptr())) {
                    return obj;
                }
            }
            Int scale = impl::pow_int(10, digits);
            Object whole = Round::Floor::div(obj, Int::one);
            return whole + (mode.round((obj - whole) * scale) / scale);

        // C++ floats can use an STL method to avoid precision loss
        } else if constexpr (std::floating_point<T>) {
            size_t scale = impl::pow_int(10, digits);
            T whole, fract;
            fract = std::modf(obj, &whole);
            return whole + (mode.round(fract * scale) / scale);

        // all other C++ objects are handled similar to Python objects
        } else {
            size_t scale = impl::pow_int(10, digits);
            auto whole = Round::Floor::div(obj, 1);
            return whole + (mode.round((obj - whole) * scale) / scale);
        }
    }
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_MATH_H
