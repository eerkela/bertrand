#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_MATH_H
#define BERTRAND_PYTHON_MATH_H

#include <cmath>
#include <type_traits>

#include "common.h"
#include "int.h"
#include "tuple.h"


// TODO: simplify this as much as possible.  Accounting for Decimal objects is going to
// be a nightmare, so we need to polish as much as possible first.


namespace bertrand {
namespace py {


/* Collection of tag structs describing language-agnostic rounding strategies for
div(), mod(), divmod(), and round(). */
class Round {
    struct RoundTag {};

    template <typename T>
    static constexpr bool PYTHONLIKE = impl::python_like<T>;

    template <typename T>
    static constexpr bool INTLIKE = std::is_integral_v<T>;

    template <typename T>
    static constexpr bool FLOATLIKE = std::is_floating_point_v<T>;

    /* Round toward negative infinity. */
    struct Floor : RoundTag {

        /* Python: use // operator.
         *
         * C++: apply C-style / operator and then floor the result.  If both operands
         * are integers, subtract 1 if the result is negative and the remainder is
         * nonzero.
         */

        template <typename L, typename R>
        inline static auto divmod(const L& l, const R& r) {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                PyObject* result = PyNumber_Divmod(
                    detail::object_or_cast(l).ptr(),
                    detail::object_or_cast(r).ptr()
                );
                if (result == nullptr) {
                    Exception::from_python();
                }
                return std::make_pair(
                    reinterpret_borrow<Object>(PyTuple_GET_ITEM(result, 0)),
                    reinterpret_borrow<Object>(PyTuple_GET_ITEM(result, 1))
                );

            } else {

            }
        }

    };

    /* Round toward zero. */
    struct Down : RoundTag {

        /* Apply floor when l and r have the same sign and ceiling when they differ. */

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

    };

    /* Round away from zero. */
    struct Up : RoundTag {

        /* Apply ceiling when l and r have the same sign and floor when they differ. */

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

    };

};





/* A collection of tag structs used to implement cross-language rounding strategies for
basic `div()`, `mod()`, `divmod()`, and `round()` operators. */
struct Round {

    struct True {

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        static auto div(const L& lhs, const R& rhs) {
            return lhs / rhs;
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto div(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            if constexpr (std::integral<L> && std::integral<R>) {
                return static_cast<double>(lhs) / rhs;
            } else {
                return lhs / rhs;
            }
        }

        template <typename L, typename R>
        static auto mod(const L& lhs, const R& rhs) {
            return lhs - (div(lhs, rhs) * rhs);
        }

        template <typename L, typename R>
        static auto divmod(const L& lhs, const R& rhs) {
            auto result = div(lhs, rhs);
            return std::make_pair(result, lhs - (result * rhs));
        }

        template <typename O>
        static auto round(const O& obj) {
            return obj;
        }

    };

    struct CDiv {

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        inline static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Int numerator = lhs;
                Int denominator = rhs;
                Int result = Round::Floor::invoke<Int>(numerator, denominator);
                if (
                    (result < Int::zero()) &&
                    ((numerator - (result * denominator)) != Int::zero())
                ) {
                    ++result;
                }
                return result;

            } else if constexpr (std::same_as<L, Object> || std::same_as<R, Object>) {
                Object numerator = lhs;
                Object denominator = rhs;
                if (PyLong_Check(numerator.ptr()) && PyLong_Check(denominator.ptr())) {
                    Object result = Floor::invoke<Object>(numerator, denominator);
                    if (
                        (result < Int::zero()) &&
                        ((numerator - (result * denominator)) != Int::zero())
                    ) {
                        ++result;
                    }
                    return result;
                }
                return numerator / denominator;

            } else {
                return lhs / rhs;
            }
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        inline static auto div(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            return lhs / rhs;
        }

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        inline static auto mod(const L& lhs, const R& rhs) {
            if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {
                return -(lhs % rhs);
            } else {
                return lhs % rhs;
            }
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        inline static auto mod(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            if constexpr (std::floating_point<L> || std::floating_point<R>) {
                return std::fmod(lhs, rhs);
            } else {
                return lhs % rhs;
            }
        }

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        inline static auto divmod(const L& lhs, const R& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Int numerator = lhs;
                Int denominator = rhs;
                Int quotient = Floor::invoke<Int>(numerator, denominator);
                Int remainder = numerator - (quotient * denominator);
                if ((quotient < Int::zero()) && (remainder != Int::zero())) {
                    ++quotient;
                    return std::make_pair(quotient, -remainder);
                }
                return std::make_pair(quotient, remainder);

            } else if constexpr (std::same_as<L, Object> || std::same_as<R, Object>) {
                Object numerator = lhs;
                Object denominator = rhs;
                if (PyLong_Check(numerator.ptr()) && PyLong_Check(denominator.ptr())) {
                    Object quotient = Floor::invoke<Object>(numerator, denominator);
                    Object remainder = numerator - (quotient * denominator);
                    if ((quotient < Int::zero()) && (remainder != Int::zero())) {
                        ++quotient;
                        return std::make_pair(quotient, -remainder);
                    }
                    return std::make_pair(quotient, remainder);
                }

                Object quotient = numerator / denominator;
                if ((numerator < Int::zero()) ^ (denominator < Int::zero())) {
                    return std::make_pair(quotient, -(numerator % denominator));
                }
                return std::make_pair(quotient, numerator % denominator);

            } else if constexpr (impl::object_operand<L, R>) {
                auto quotient = lhs / rhs;
                if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {
                    return std::make_pair(quotient, -(lhs % rhs));
                }
                return std::make_pair(quotient, lhs % rhs);
            
            } else {
                auto quotient = lhs / rhs;
                if ((lhs < 0) ^ (rhs < 0)) {
                    return std::make_pair(quotient, -(lhs % rhs));
                }
                return std::make_pair(quotient, lhs % rhs);
            }
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        inline static auto divmod(const L& lhs, const R& rhs) {
            if (r == 0) {
                throw ZeroDivisionError("division by zero");
            }
            if constexpr (std::floating_point<L> || std::floating_point<R>) {
                return std::make_pair(lhs / rhs, std::fmod(lhs, rhs));
            } else {
                return std::make_pair(lhs / rhs, lhs % rhs);
            }
        }

        template <typename O>
        inline static auto round(const O& obj) {
            return obj;
        }

    };

    struct Floor {

        template <typename L, typename R> requires (__floordiv__<L, R>::enable)
        inline static auto div(const L& lhs, const R& rhs) {
            using Return = typename __truediv__<L, R>::Return;
            static_assert(
                std::is_base_of_v<Object, Return>,
                "Floor division operator must return a py::Object subclass.  Check "
                "your specialization of __floordiv__ for this type and ensure the "
                "Return type is derived from py::Object."
            );
            return invoke<Return>(lhs, rhs);
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        inline static auto div(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            auto result = lhs / rhs;
            if constexpr (std::integral<L> && std::integral<R>) {
                return result - ((result < 0) && (lhs % rhs != 0));
            } else {
                return std::floor(result);
            }
        }

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        inline static auto mod(const L& lhs, const R& rhs) {
            return lhs % rhs;
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        inline static auto mod(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            int correction = 1 - 2 * ((lhs < 0) ^ (rhs < 0));
            if constexpr (std::floating_point<L> || std::floating_point<R>) {
                return std::fmod(lhs, rhs) * correction;
            } else {
                return (lhs % rhs) * correction;
            }
        }

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        inline static auto divmod(const L& lhs, const R& rhs) {
            // TODO: call impl::unsafe::operator_divmod() here, and construct the
            // return type from __floordiv__ and __mod__.
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        inline static auto divmod(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }

            auto quotient = lhs / rhs;
            if constexpr (std::integral<L> && std::integral<R>) {
                quotient -= ((quotient < 0) && (lhs % rhs != 0));
            } else {
                quotient = std::floor(quotient);
            }

            int correction = (1 - 2 * ((lhs < 0) ^ (rhs < 0)));
            if constexpr (std::floating_point<L> || std::floating_point<R>) {
                return std::make_pair(quotient, std::fmod(lhs, rhs) * correction);
            } else {
                return std::make_pair(quotient, (lhs % rhs) * correction);
            }
        }

        template <typename O>
        inline static auto round(const O& obj) {
            if constexpr (std::floating_point<O>) {
                return std::floor(obj);
            } else {
                // TODO: use math.floor()?
                return div(obj, 1);
            }
        }

    };

    struct Ceiling {

        template <typename L, typename R>
        inline static auto div(const L& lhs, const R& rhs) {
            return -Floor::div(-lhs, rhs);
        }

        template <typename L, typename R>
        inline static auto mod(const L& lhs, const R& rhs) {
            return -Round::Floor::mod(-lhs, rhs);
        }

        template <typename L, typename R>
        inline static auto divmod(const L& lhs, const R& rhs) {
            auto [result, remainder] = Round::Floor::divmod(-lhs, rhs);
            return std::make_pair(-result, -remainder);
        }

        template <typename O>
        inline static auto round(const O& obj) {
            if constexpr (std::floating_point<O>) {
                return std::ceil(obj);
            } else {
                // TODO: use math.ceil()?
                return div(obj, 1);
            }
        }

    };

    struct Down {

        template <typename L, typename R> requires (__floordiv__<L, R>::enable)
        inline static auto div(const L& lhs, const R& rhs) {
            using Return = typename __floordiv__<L, R>::Return;
            static_assert(
                std::is_base_of_v<Object, Return>,
                "Floor division operator must return a py::Object subclass.  Check "
                "your specialization of __floordiv__ for this type and ensure the "
                "Return type is derived from py::Object."
            );
            Object numerator = lhs;
            Object denominator = rhs;
            if ((numerator < Int::zero()) ^ (denominator < Int::zero())) {
                return -Floor::invoke<Return>(-numerator, denominator);
            } else {
                return Floor::invoke<Return>(numerator, denominator);
            }
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        inline static auto div(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Ceiling::div(lhs, rhs);
            } else {
                return Floor::div(lhs, rhs);
            }
        }

        template <typename L, typename R>
        inline static auto mod(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto divmod(const L& lhs, const R& rhs);

        template <typename O>
        inline static auto round(const O& obj) {
            if constexpr (std::floating_point<O>) {
                return std::trunc(obj);
            } else {
                // TODO: use math.trunc()?
                return div(obj, 1);
            }
        }

    };

    struct Up {

        template <typename L, typename R> requires (__floordiv__<L, R>::enable)
        inline static auto div(const L& lhs, const R& rhs) {
            using Return = typename __floordiv__<L, R>::Return;
            static_assert(
                std::is_base_of_v<Object, Return>,
                "Floor division operator must return a py::Object subclass.  Check your "
                "specialization of __floordiv__ for this type and ensure the Return type is "
                "derived from py::Object."
            );
            Object numerator = lhs;
            Object denominator = rhs;
            if ((numerator < Int::zero()) ^ (denominator < Int::zero())) {
                return Floor::invoke<Return>(numerator, denominator);
            } else {
                return -Floor::invoke<Return>(-numerator, denominator);
            }
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        inline static auto div(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Floor::div(lhs, rhs);
            } else {
                return Ceiling::div(lhs, rhs);
            }
        }

        template <typename L, typename R>
        inline static auto mod(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto divmod(const L& lhs, const R& rhs);

        template <typename O>
        inline static auto round(const O& obj) {
            if constexpr (std::floating_point<O>) {
                if (obj < 0) {
                    return std::floor(obj);
                } else {
                    return std::ceil(obj);
                }
            } else {
                return div(obj, 1);
            }
        }

    };

    struct HalfFloor {
        template <typename L, typename R>
        inline static auto div(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto mod(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto divmod(const L& lhs, const R& rhs);

        template <typename O>
        inline static auto round(const O& obj) {
            return Round::Ceiling::round(obj - 0.5);
        }

    };

    struct HalfCeiling {
        template <typename L, typename R>
        inline static auto div(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto mod(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto divmod(const L& lhs, const R& rhs);

        template <typename O>
        inline static auto round(const O& obj) {
            return Floor::round(obj + 0.5);
        }

    };

    struct HalfDown {
        template <typename L, typename R>
        inline static auto div(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto mod(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto divmod(const L& lhs, const R& rhs);

        template <typename O>
        inline static auto round(const O& obj) {
            if (obj > 0) {
                return Round::HalfFloor::round(obj);
            } else {
                return Round::HalfCeiling::round(obj);
            }
        }

    };

    struct HalfUp {
        template <typename L, typename R>
        inline static auto div(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto mod(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto divmod(const L& lhs, const R& rhs);

        template <typename O>
        inline static auto round(const O& obj) {
            if constexpr (std::floating_point<O>) {
                return std::round(obj);  // always rounds away from zero
            } else {
                if (obj > 0) {
                    return Round::HalfCeiling::round(obj);
                } else {
                    return Round::HalfFloor::round(obj);
                }
            }
        }

    };

    struct HalfEven {
        template <typename L, typename R>
        inline static auto div(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto mod(const L& lhs, const R& rhs);
        template <typename L, typename R>
        inline static auto divmod(const L& lhs, const R& rhs);

        template <typename O>
        inline static auto round(const O& obj) {
            if constexpr (impl::python_like<O>) {
                static const Function py_round = builtins()["round"];
                PyObject* result = PyObject_CallOneArg(py_round.ptr(), obj.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<O>(result);

            } else if constexpr (std::floating_point<O>) {
                O whole, fract;
                fract = std::modf(obj, &whole);
                if (std::abs(fract) == 0.5) {
                    return whole + std::fmod(whole, 2);  // -1 if whole % 2 and < 0
                } else {
                    return std::round(obj);
                }

            } else {
                // TODO: this branch is incorrect for o = -3.2

                // floor_is_odd = +1 if floor(o) is odd and -1 if it's even.  This is
                // a branchless way of choosing between half-floor and half-ceiling.
                // half-down: (abs(o) + 0.5) // 1 * sign(o)
                // half-up: -(-abs(o) + 0.5) // 1 * sign(o)
                auto floor_is_odd =
                    Round::FLOOR.mod(Round::FLOOR.round(obj), 2) * 2 - 1;
                return FLOOR.round(abs(obj) * floor_is_odd + 0.5) * floor_is_odd;
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
template <typename L, typename R, typename Mode = Round::Floor>
    requires (impl::div_mode<L, R, Mode>)
inline auto div(const L& lhs, const R& rhs, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return mode.div(lhs.value(), rhs);

    } else if constexpr (impl::proxy_like<R>) {
        return mode.div(lhs, rhs.value());

    } else {
        return mode.div(lhs, rhs);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode = Round::Floor>
    requires (impl::mod_mode<L, R, Mode>)
inline auto mod(const L& l, const R& r, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return mode.mod(l.value(), r);
    } else if constexpr (impl::proxy_like<R>) {
        return mode.mod(l, r.value());
    } else {
        return mode.mod(l, r);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode = Round::Floor>
    requires (impl::divmod_mode<L, R, Mode>)
inline auto divmod(const L& l, const R& r, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return mode.divmod(l.value(), r);
    } else if constexpr (impl::proxy_like<R>) {
        return mode.divmod(l, r.value());
    } else {
        return mode.divmod(l, r);
    }
}


template <typename O, typename Mode = Round::HalfEven>
    requires (impl::round_mode<O, Mode>)
inline auto round(const O& obj, const Mode& mode = Round::HALF_EVEN) {
    if constexpr (impl::proxy_like<O>) {
        return mode.round(obj.value());
    } else {
        return mode.round(obj);
    }
}


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
            return div(n, static_cast<T>(std::pow(10, digits)), mode);
        }

    // same for Python integers
    } else if constexpr (impl::python_like<T>) {
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
