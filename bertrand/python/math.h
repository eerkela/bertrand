#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_MATH_H
#define BERTRAND_PYTHON_MATH_H

#include <cmath>
#include <type_traits>

#include "common.h"
#include "tuple.h"


namespace bertrand {
namespace py {


/* Collection of tag structs describing rounding strategies for div(), mod(), divmod(),
and round(). */
class Round {
    struct RoundTag {};

    template <typename T>
    static constexpr bool PYTHONLIKE = impl::is_python<T>;

    template <typename T>
    static constexpr bool INTLIKE = std::is_integral_v<T>;

    template <typename T>
    static constexpr bool FLOATLIKE = std::is_floating_point_v<T>;

    /* Do not round.  Used to implement Python-style "true" division. */
    struct True : RoundTag {

        /* Python: use normal / operator.
         *
         * C++: if both operands are integers, cast numerator to double and then use
         * normal / operator.
         */

        template <typename L, typename R>
        inline static auto div(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                return l / r;
            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                }
                if constexpr (INTLIKE<L> && INTLIKE<R>) {
                    return static_cast<double>(l) / r;
                } else {
                    return l / r;
                }
            }
        }

        template <typename L, typename R>
        inline static auto mod(const L& l, const R& r) {
            return l - (divide(l, r) * r);  // equal to rounding error
        }

        template <typename L, typename R>
        inline static auto divmod(const L& l, const R& r) {
            auto quotient = divide(l, r);
            return std::make_pair(quotient, l - (quotient * r));
        }

        /* Rounding does nothing for true division. */

        template <typename O>
        inline static auto round(const O& o) {
            return o;
        }

    };

    /* Apply C-style division.  Round integers toward zero and apply true division
    everywhere else. */
    struct CDiv : RoundTag {

        /* Python: if both operands are integers, compute // and add 1 if the remainder
         * is nonzero.
         *
         * C++: use normal / operator.
         */

        template <typename L, typename R>
        static auto div(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if (PyLong_Check(numerator.ptr()) && PyLong_Check(denominator.ptr())) {
                    PyObject* temp = PyNumber_FloorDivide(
                        numerator.ptr(),
                        denominator.ptr()
                    );
                    if (temp == nullptr) {
                        throw error_already_set();
                    }
                    Int result = reinterpret_steal<Int>(temp);
                    if (result < 0) {
                        Int a = reinterpret_borrow<Int>(numerator.ptr());
                        Int b = reinterpret_borrow<Int>(denominator.ptr());
                        result += Int(a - (result * b)) != 0;
                    }
                    return reinterpret_steal<Object>(result.release());
                } else {
                    return l / r;
                }

            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                } else {
                    return l / r;
                }
            }
        }

        template <typename L, typename R>
        static auto mod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                PyObject* result = PyNumber_Remainder(
                    detail::object_or_cast(l).ptr(),
                    detail::object_or_cast(r).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                const Int zero(0);
                return (
                    reinterpret_steal<Object>(result) *
                    Int(1 - 2 * ((l < zero) ^ (r < zero)))
                );

            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                }
                if constexpr (FLOATLIKE<L> || FLOATLIKE<R>) {
                    return std::fmod(l, r);
                } else {
                    return l % r;
                }
            }
        }

        template <typename L, typename R>
        static auto divmod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if (PyLong_Check(numerator.ptr()) && PyLong_Check(denominator.ptr())) {
                    Int a = reinterpret_borrow<Int>(numerator.ptr());
                    Int b = reinterpret_borrow<Int>(denominator.ptr());
                    PyObject* temp = PyNumber_FloorDivide(a.ptr(), b.ptr());
                    if (temp == nullptr) {
                        throw error_already_set();
                    }

                    Object quotient = reinterpret_steal<Object>(temp);
                    Object remainder = a - (quotient * b);
                    if ((quotient < 0) && (remainder != 0)) {
                        quotient += 1;
                        remainder *= -1;
                    }
                    return std::make_pair<quotient, remainder>;

                } else {
                    const Int zero(0);
                    Object quotient = l / r;
                    PyObject* temp = PyNumber_Remainder(
                        numerator.ptr(),
                        denominator.ptr()
                    );
                    if (temp == nullptr) {
                        throw error_already_set();
                    }
                    Object remainder = (
                        reinterpret_steal<Object>(temp) *
                        Int(1 - 2 * ((l < zero) ^ (r < zero)))
                    );
                    return std::make_pair<quotient, remainder>;
                }

            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                }
                if constexpr (FLOATLIKE<L> || FLOATLIKE<R>) {
                    return std::make_pair(l / r, std::fmod(l, r));
                } else {
                    return std::make_pair(l / r, l % r);
                }
            }
        }

        /* C-style division is a form a true division and only differs in its handling
         * of integers, so rounding does nothing.
         */

        template <typename O>
        inline static auto round(const O& o) {
            return o;
        }

    };

    /* Round toward negative infinity. */
    struct Floor : RoundTag {

        /* Python: use // operator.
         *
         * C++: apply C-style / operator and then floor the result.  If both operands
         * are integers, subtract 1 if the result is negative and the remainder is
         * nonzero.
         */

        template <typename L, typename R>
        inline static auto div(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                PyObject* result = PyNumber_FloorDivide(
                    detail::object_or_cast(l).ptr(),
                    detail::object_or_cast(r).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return reinterpret_steal<Object>(result);
            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                }
                auto result = l / r;
                if constexpr (std::is_integral_v<L> && std::is_integral_v<R>) {
                    return result - ((result < 0) && (l % r != 0));
                } else {
                    return std::floor(result);
                }
            }
        }

        template <typename L, typename R>
        inline static auto mod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                PyObject* result = PyNumber_Remainder(
                    detail::object_or_cast(l).ptr(),
                    detail::object_or_cast(r).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return reinterpret_steal<Object>(result);
            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                }
                int correction = (1 - 2 * ((l < 0) ^ (r < 0)));
                if constexpr (FLOATLIKE<L> || FLOATLIKE<R>) {
                    // branchless: mod * -1 if only one of the operands is negative
                    return std::fmod(l, r) * correction;
                } else {
                    return (l % r) * correction;
                }
            }
        }

        template <typename L, typename R>
        inline static auto divmod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                PyObject* result = PyNumber_Divmod(
                    detail::object_or_cast(l).ptr(),
                    detail::object_or_cast(r).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return std::make_pair(
                    reinterpret_borrow<Object>(PyTuple_GET_ITEM(result, 0)),
                    reinterpret_borrow<Object>(PyTuple_GET_ITEM(result, 1))
                );

            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                }

                auto quotient = l / r;
                if constexpr (std::is_integral_v<L> && std::is_integral_v<R>) {
                    quotient -= ((quotient < 0) && (l % r != 0));
                } else {
                    quotient = std::floor(quotient);
                }

                int correction = (1 - 2 * ((l < 0) ^ (r < 0)));
                if constexpr (FLOATLIKE<L> || FLOATLIKE<R>) {
                    return std::make_pair(quotient, std::fmod(l, r) * correction);
                } else {
                    return std::make_pair(quotient, (l % r) * correction);
                }
            }
        }

        /* Python: compute obj // 1.
         *
         * C++: use std::floor() for floating point numbers.  Otherwise do the same as
         * for Python.
         */

        template <typename O>
        inline static auto round(const O& o) {
            if constexpr (FLOATLIKE<O>) {
                return std::floor(o);
            } else {
                return div(o, 1);
            }
        }

    };

    /* Round toward positive infinity. */
    struct Ceiling : RoundTag {

        /* compute -(-obj // 1). */

        template <typename L, typename R>
        inline static auto div(const L& l, const R& r) {
            return -Floor::div(-l, r);
        }

        template <typename L, typename R>
        inline static auto mod(const L& l, const R& r) {
            return -Floor::mod(-l, r);
        }

        template <typename L, typename R>
        inline static auto divmod(const L& l, const R& r) {
            auto [quotient, remainder] = Floor::divmod(-l, r);
            return std::make_pair(-quotient, -remainder);
        }

        /* Python: divide by 1
         *
         * C++: use std::ceil() for floating point numbers.  Otherwise do the same as
         * for Python.
         */

        template <typename O>
        inline static auto round(const O& o) {
            if constexpr (FLOATLIKE<O>) {
                return std::ceil(o);
            } else {
                return div(o, 1);
            }
        }

    };

    /* Round toward zero. */
    struct Down : RoundTag {

        /* Apply floor when l and r have the same sign and ceiling when they differ. */

        template <typename L, typename R>
        inline static auto div(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                const Int zero(0);
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if ((numerator < zero) ^ (denominator < zero)) {
                    return Ceiling::div(numerator, denominator);
                } else {
                    return Floor::div(numerator, denominator);
                }
            } else {
                if ((l < 0) ^ (r < 0)) {
                    return Ceiling::div(l, r);
                } else {
                    return Floor::div(l, r);
                }
            }
        }

        template <typename L, typename R>
        inline static auto mod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                const Int zero(0);
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if ((numerator < zero) ^ (denominator < zero)) {
                    return Ceiling::mod(numerator, denominator);
                } else {
                    return Floor::mod(numerator, denominator);
                }
            } else {
                if ((l < 0) ^ (r < 0)) {
                    return Ceiling::mod(l, r);
                } else {
                    return Floor::mod(l, r);
                }
            }
        }

        template <typename L, typename R>
        inline static auto divmod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                const Int zero(0);
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if ((numerator < zero) ^ (denominator < zero)) {
                    return Ceiling::divmod(numerator, denominator);
                } else {
                    return Floor::divmod(numerator, denominator);
                }
            } else {
                if ((l < 0) ^ (r < 0)) {
                    return Ceiling::divmod(l, r);
                } else {
                    return Floor::divmod(l, r);
                }
            }
        }

        /* Python: divide by 1
         *
         * C++: use std::trunc() for floating point numbers.  Otherwise do the same as
         * for Python.
         */

        template <typename O>
        inline static auto round(const O& o) {
            if constexpr (FLOATLIKE<O>) {
                return std::trunc(o);
            } else {
                return div(o, 1);
            }
        }

    };

    /* Round away from zero. */
    struct Up : RoundTag {

        /* Apply ceiling when l and r have the same sign and floor when they differ. */

        template <typename L, typename R>
        inline static auto div(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                const Int zero(0);
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if ((numerator < zero) ^ (denominator < zero)) {
                    return Floor::div(numerator, denominator);
                } else {
                    return Ceiling::div(numerator, denominator);
                }
            } else {
                if ((l < 0) ^ (r < 0)) {
                    return Floor::div(l, r);
                } else {
                    return Ceiling::div(l, r);
                }
            }
        }

        template <typename L, typename R>
        inline static auto mod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                const Int zero(0);
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if ((numerator < zero) ^ (denominator < zero)) {
                    return Floor::mod(numerator, denominator);
                } else {
                    return Ceiling::mod(numerator, denominator);
                }
            } else {
                if ((l < 0) ^ (r < 0)) {
                    return Floor::mod(l, r);
                } else {
                    return Ceiling::mod(l, r);
                }
            }
        }

        template <typename L, typename R>
        inline static auto divmod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                const Int zero(0);
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if ((numerator < zero) ^ (denominator < zero)) {
                    return Ceiling::divmod(numerator, denominator);
                } else {
                    return Floor::divmod(numerator, denominator);
                }
            } else {
                if ((l < 0) ^ (r < 0)) {
                    return Ceiling::divmod(l, r);
                } else {
                    return Floor::divmod(l, r);
                }
            }
        }

        /* Python: divide by 1.
         *
         * C++: for floating point numbers, use std::ceil if positive and std::floor if
         * negative.  Otherwise, do the same as for Python.
         */

        template <typename O>
        inline static auto round(const O& o) {
            if constexpr (FLOATLIKE<O>) {
                if (o < 0) {
                    return std::floor(o);
                } else {
                    return std::ceil(o);
                }
            } else {
                return div(o, 1);
            }
        }

    };

    // basically, add/subtract 0.5 and then floor/ceil as needed.

    /* Round to nearest, with ties toward negative infinity. */
    struct HalfFloor : RoundTag {

        /*
         */

        template <typename L, typename R>
        inline static auto div(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                Object numerator = detail::object_or_cast(l);
                Object denominator = detail::object_or_cast(r);
                if (PyLong_Check(numerator.ptr()) && PyLong_Check(denominator.ptr())) {
                    // TODO
                } else {
                    return Ceiling::div(l - 0.5, r);
                }
            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                }
                if constexpr (INTLIKE<L> && INTLIKE<R>) {
                    // TODO
                } else {
                    return Ceiling::div(l - 0.5, r);
                }
            }
        }

        /* Subtract 0.5 and then ceiling round. */

        template <typename O>
        inline static auto round(const O& o) {
            return Ceiling::round(o - 0.5);
        }

    };

    /* Round to nearest, with ties toward positive infinity. */
    struct HalfCeiling : RoundTag {

        /* Add 0.5 and then floor round. */

        template <typename O>
        inline static auto round(const O& o) {
            return Floor::round(o + 0.5);
        }

    };

    /* Round to nearest, with ties toward zero. */
    struct HalfDown : RoundTag {

        /* Apply half-floor when obj is positive and half-ceiling otherwise. */

        template <typename O>
        inline static auto round(const O& o) {
            if (o > 0) {
                return HalfFloor::round(o);
            } else {
                return HalfCeiling::round(o);
            }
        }

    };

    /* Round to nearest, with ties away from zero. */
    struct HalfUp : RoundTag {
 
        /* Apply half-ceiling when obj is positive and half-floor otherwise.  If
         * operand is a C++ float, use std::round() directly.
         */

        template <typename O>
        inline static auto round(const O& o) {
            if constexpr (FLOATLIKE<O>) {
                return std::round(o);  // always rounds away from zero
            } else {
                if (o > 0) {
                    return HalfCeiling::round(o);
                } else {
                    return HalfFloor::round(o);
                }
            }
        }

    };

    /* Round to nearest, with ties toward the closest even value. */
    struct HalfEven : RoundTag {

        // TODO: document behavior

        template <typename O>
        inline static auto round(const O& o) {
            if constexpr (PYTHONLIKE<O>) {
                static const Handle round = PyDict_GetItemString(
                    PyEval_GetBuiltins(),
                    "round"
                );
                PyObject* result = PyObject_CallOneArg(round.ptr(), o.ptr());
                if (result == nullptr) {
                    throw error_already_set();
                }
                return reinterpret_steal<Object>(result);
            } else if constexpr (FLOATLIKE<O>) {
                O whole, fract;
                fract = std::modf(o, &whole);
                if (std::abs(fract) == 0.5) {
                    return whole + std::fmod(whole, 2);  // -1 if whole % 2 and < 0
                } else {
                    return std::round(o);
                }
            } else {
                // TODO: this branch is incorrect for o = -3.2

                // floor_is_odd = +1 if floor(o) is odd and -1 if it's even.  This is
                // a branchless way of choosing between half-floor and half-ceiling.
                // half-down: (abs(o) + 0.5) // 1 * sign(o)
                // half-up: -(-abs(o) + 0.5) // 1 * sign(o)
                auto floor_is_odd = FLOOR.mod(FLOOR.round(o), 2) * 2 - 1;
                return FLOOR.round(abs(o) * floor_is_odd + 0.5) * floor_is_odd;
            }
        }

    };

public:
    static constexpr True           TRUE            = True{};
    static constexpr CDiv           C               = CDiv{};
    static constexpr Floor          FLOOR           = Floor{};
    static constexpr Ceiling        CEILING         = Ceiling{};
    static constexpr Down           DOWN            = Down{};
    static constexpr Up             UP              = Up{};
    static constexpr HalfFloor      HALF_FLOOR      = HalfFloor{};
    static constexpr HalfCeiling    HALF_CEILING    = HalfCeiling{};
    static constexpr HalfDown       HALF_DOWN       = HalfDown{};
    static constexpr HalfUp         HALF_UP         = HalfUp{};
    static constexpr HalfEven       HALF_EVEN       = HalfEven{};

    /* Check whether a given rounding strategy is valid. */
    template <typename T>
    static constexpr bool is_valid = std::is_base_of_v<RoundTag, T>;

    /* Check whether a given rounding strategy represents Python or C-style true
    division. */
    template <typename T>
    static constexpr bool is_true_or_c = (
        std::is_same_v<T, True> || std::is_same_v<T, CDiv>
    );

};


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


/* NOTE: C++'s math operators have slightly different semantics to Python in some
 * cases, which can be subtle and surprising for first-time users.  Here's a summary:
 *
 *  1.  C++ has no `**` exponentiation operator, so `std::pow()` is typically used
 *      instead.  Rather than overload a standard method, a separate `py::pow()`
 *      function is provided, which is consistent with both Python and C++ inputs.
 *  2.  Similarly, C++ has no `//` floor division operator, as it has entirely
 *      different division semantics to Python.  Worse still, C++ uses C-style division
 *      for the `/` operator, which differs from Python in its handling of integers.
 *      In Python, `/` represents *true* division, which can return a float when
 *      dividing two integers.  In contrast, C++ truncates the result towards zero,
 *      which can cause compatibility issues when mixing Python and C++ code.  The
 *      py::div() function exists to standardize this by applying a customizable
 *      rounding strategy to both Python and C++ inputs.  This goes beyond just
 *      consolidating the syntax - the rounding strategies are significantly more
 *      comprehensive, and can be used to implement more than just true or floor
 *      division.
 *  3.  Because of the above, the `%` operator also has different semantics in both
 *      languages.  This amounts to a sign difference when one of the operands is
 *      negative, in order to satisfy the invariant `a == (a / b) * b + (a % b)`).  The
 *      `py::mod()` function provides a consistent implementation for both languages,
 *      and also allows for the same rounding strategies as `py::div()`.
 *  4.  The `py::divmod()` function is provided for completeness and slightly increased
 *      performance as long as both results are needed.
 *  5.  The `py::round()` function hooks into the same rounding strategies as
 *      `py::div()`, `py::mod()`, and `py::divmod()`, and applies them in a consistent
 *      manner to both Python and C++ inputs.  It is fully generic, and massively
 *      simplifies the syntax for rounding numbers in both languages.
 */


/* Equivalent to Python `abs(obj)`. */
template <typename T>
inline auto abs(const T& obj) {
    if constexpr (impl::is_python<T>) {
        PyObject* result = PyNumber_Absolute(obj.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    } else {
        return std::abs(obj);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode>
inline auto div(const L& l, const R& r, const Mode& mode = Round::TRUE) {
    static_assert(
        Round::is_valid<Mode>,
        "rounding mode must be one of Round::TRUE, ::C, ::FLOOR, ::CEILING, ::DOWN, "
        "::UP, ::HALF_FLOOR, ::HALF_CEILING, ::HALF_DOWN, ::HALF_UP, or ::HALF_EVEN"
    );
    return Mode::div(l, r);
}


/* Compute the remainder of a division according to the specified rounding rule. */
template <typename L, typename R, typename Mode>
auto mod(const L& l, const R& r, const Mode& mode = Round::FLOOR) {
    static_assert(
        Round::is_valid<Mode>,
        "rounding mode must be one of Round::TRUE, ::C, ::FLOOR, ::CEILING, ::DOWN, "
        "::UP, ::HALF_FLOOR, ::HALF_CEILING, ::HALF_DOWN, ::HALF_UP, or ::HALF_EVEN"
    );
    return Mode::mod(l, r);
}


/* Get the quotient and remainder of a division at the same time, using the specified
rounding rule. */
template <typename L, typename R, typename Mode>
auto divmod(const L& l, const R& r, const Mode& mode = Round::FLOOR) {
    static_assert(
        Round::is_valid<Mode>,
        "rounding mode must be one of Round::TRUE, ::C, ::FLOOR, ::CEILING, ::DOWN, "
        "::UP, ::HALF_FLOOR, ::HALF_CEILING, ::HALF_DOWN, ::HALF_UP, or ::HALF_EVEN"
    );
    return Mode::divmod(l, r);
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename L, typename R>
auto pow(const L& base, const R& exp) {
    if constexpr (impl::is_python<L> || impl::is_python<R>) {
        PyObject* result = PyNumber_Power(
            detail::object_or_cast(base).ptr(),
            detail::object_or_cast(exp).ptr(),
            Py_None
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    } else {
        return std::pow(base, exp);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <typename L, typename R, typename E>
auto pow(const L& base, const R& exp, const E& mod) {
    static_assert(
        (std::is_integral_v<L> || impl::is_python<L>) &&
        (std::is_integral_v<R> || impl::is_python<R>) &&
        (std::is_integral_v<E> || impl::is_python<E>),
        "pow() 3rd argument not allowed unless all arguments are integers"
    );

    if constexpr (impl::is_python<L> || impl::is_python<R> || impl::is_python<E>) {
        PyObject* result = PyNumber_Power(
            detail::object_or_cast(base).ptr(),
            detail::object_or_cast(exp).ptr(),
            detail::object_or_cast(mod).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    } else {
        std::common_type_t<L, R, E> result = 1;
        base = py::mod(base, mod);
        while (exp > 0) {
            if (exp % 2) {
                result = py::mod(result * base, mod);
            }
            exp >>= 1;
            base = py::mod(base * base, mod);
        }
        return result;
    }
}


/* Equivalent to Python `round(obj, ndigits)`, but works on both Python and C++ inputs
and accepts a third tag parameter that determines the rounding strategy to apply. */
template <typename T, typename Mode = decltype(Round::HALF_EVEN)>
auto round(const T& n, int digits = 0, const Mode& mode = Round::HALF_EVEN) {
    static_assert(
        Round::is_valid<Mode> && !Round::is_true_or_c<Mode>,
        "rounding mode must be one of Round::FLOOR, ::CEILING, ::DOWN, ::UP, "
        "::HALF_FLOOR, ::HALF_CEILING, ::HALF_DOWN, ::HALF_UP, or ::HALF_EVEN"
    );

    // TODO: mitigate precision loss when scaling
    // -> if digits < 0, fall back to div?

    // integer rounding only has an effect if digits are negative
    if constexpr (std::is_integral_v<T>) {
        if (digits >= 0) {
            return n;
        } else {
            return div(n, static_cast<T>(pow(10, digits)), mode);
        }

    // same for Python integers
    } else if constexpr (impl::is_python<T>) {
        // // TODO: uncomment this when all tags have a functional div() method
        // if (PyLong_Check(n.ptr())) {
        //     if (digits >= 0) {
        //         return reinterpret_borrow<Object>(n.ptr());
        //     } else {
        //         return div(n, Int(static_cast<long long>(pow(10, digits))), mode);
        //     }
        // } else {
            double scale = std::pow(10, digits);
            return Mode::round(n * scale) / scale;
        // }

    } else {
        double scale = std::pow(10, digits);
        return Mode::round(n * scale) / scale;
    }
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_MATH_H
