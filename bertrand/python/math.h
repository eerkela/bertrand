#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_MATH_H
#define BERTRAND_PYTHON_MATH_H

#include <cmath>
#include <type_traits>

#include "common.h"
#include "int.h"
#include "float.h"
#include "func.h"


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
class Round {

    template <typename Return>
    static consteval void check_truediv_type() {
        static_assert(
            std::derived_from<Return, Object>,
            "True division operator must return a py::Object subclass.  Check your "
            "specialization of __truediv__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
    }

    template <typename Return>
    static consteval void check_floordiv_type() {
        static_assert(
            std::derived_from<Return, Object>,
            "Floor division operator must return a py::Object subclass.  Check your "
            "specialization of __floordiv__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
    }

    template <typename Return>
    static consteval void check_mod_type() {
        static_assert(
            std::derived_from<Return, Object>,
            "Modulus operator must return a py::Object subclass.  Check your "
            "specialization of __mod__ for this type and ensure the Return type is "
            "derived from py::Object."
        );        
    }

    template <typename Derived>
    struct Base {

        template <typename L, typename R> requires (__floordiv__<L, R>::enable)
        static auto div(const L& lhs, const R& rhs) {
            using Quotient = typename __floordiv__<L, R>::Return;
            check_floordiv_type<Quotient>();
            return Derived::template py_div<Quotient, L, R>(lhs, rhs);
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto div(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            return Derived::cpp_div(lhs, rhs);
        }

        template <typename L, typename R> requires (__mod__<L, R>::enable)
        static auto mod(const L& lhs, const R& rhs) {
            using Remainder = typename __mod__<L, R>::Return;
            check_mod_type<Remainder>();
            return Derived::template py_mod<Remainder, L, R>(lhs, rhs);
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto mod(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            return Derived::cpp_mod(lhs, rhs);
        }

        template <typename L, typename R>
            requires (__floordiv__<L, R>::enable && __mod__<L, R>::enable)
        static auto divmod(const L& lhs, const R& rhs) {
            using Quotient = typename __floordiv__<L, R>::Return;
            using Remainder = typename __mod__<L, R>::Return;
            check_floordiv_type<Quotient>();
            check_mod_type<Remainder>();
            return Derived::template py_divmod<Quotient, Remainder, L, R>(lhs, rhs);
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto divmod(const L& lhs, const R& rhs) {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            return Derived::cpp_divmod(lhs, rhs);
        }

        // TODO: pipe the original operand type into py_round?  Does it matter?

        template <typename O>
        static auto round(const O& obj, int digits) {
            // negative digits uniformly divide by a power of 10 with rounding
            if (digits < 0) {
                size_t scale = impl::pow_int(10, -digits);
                if constexpr (std::derived_from<O, Object>) {
                    Int py_scale = scale;
                    return Derived::div(obj, py_scale) * py_scale;
                } else {
                    return Derived::div(obj, scale) * scale;
                }
            }

            // integers and bools are unaffected
            if constexpr (impl::bool_like<O> || impl::int_like<O>) {
                return obj;

            // generic objects are tested for integer-ness
            } else if constexpr (std::same_as<O, Object>) {
                if (PyLong_Check(obj.ptr())) {
                    return obj;
                }
                Int scale = impl::pow_int(10, digits);
                Object whole = Round::Floor::div(obj, Int::one());
                return whole + (Derived::py_round((obj - whole) * scale) / scale);

            // Other objects are split using floor division, minimizing translation
            } else if constexpr (std::derived_from<O, Object>) {
                Int scale = impl::pow_int(10, digits);
                auto whole = Round::Floor::div(obj, Int::one());
                return whole + (Derived::py_round((obj - whole) * scale) / scale);

            // C++ floats can use an STL method to avoid precision loss
            } else if constexpr (std::floating_point<O>) {
                size_t scale = impl::pow_int(10, digits);
                O whole, fract;
                fract = std::modf(obj, &whole);
                return whole + (Derived::cpp_round(fract * scale) / scale);

            // all other C++ objects are handled just like Python objects
            } else {
                size_t scale = impl::pow_int(10, digits);
                auto whole = Round::Floor::div(obj, 1);
                return whole + (Derived::cpp_round((obj - whole) * scale) / scale);
            }
        }

        /////////////////////////
        ////    INHERITED    ////
        /////////////////////////

        template <typename Remainder, typename L, typename R>
        static auto py_mod(const Object& lhs, const Object& rhs) {
            return reinterpret_steal<Remainder>((
                lhs - (Derived::template py_div<Object, L, R>(lhs, rhs) * rhs)
            ).release());
        }

        template <typename Quotient, typename Remainder, typename L, typename R>
        static auto py_divmod(const Object& lhs, const Object& rhs) {
            Object quotient = Derived::template py_div<Quotient, L, R>(lhs, rhs);
            Object remainder = lhs - (quotient * rhs);
            return std::make_pair(
                reinterpret_steal<Quotient>(quotient.release()),
                reinterpret_steal<Remainder>(remainder.release())
            );
        }

        template <typename L, typename R>
        static auto cpp_mod(const L& lhs, const R& rhs) {
            return lhs - Derived::cpp_div(lhs, rhs) * rhs;
        }

        template <typename L, typename R>
        static auto cpp_divmod(const L& lhs, const R& rhs) {
            auto quotient = Derived::cpp_div(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

    };

public:

    struct True {

        /* True division uses / operator in all cases, but casts C++ integers to
         * double to avoid truncation.  Modulus returns the amount of precision loss
         * incurred by the division.
         */

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        static auto div(const L& lhs, const R& rhs) {
            return lhs / rhs;
        }

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        static auto mod(const L& lhs, const R& rhs) {
            using Remainder = decltype(lhs - ((lhs / rhs) * rhs));
            Object a = lhs, b = rhs;
            return reinterpret_steal<Remainder>((a - ((a / b) * b)).release());
        }

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        static auto divmod(const L& lhs, const R& rhs) {
            using Quotient = decltype(lhs / rhs);
            using Remainder = decltype(lhs - ((lhs / rhs) * rhs));
            Object a = lhs, b = rhs;
            Object quotient = a / b;
            Object remainder = a - (quotient * b);
            return std::make_pair(
                reinterpret_steal<Quotient>(quotient.release()),
                reinterpret_steal<Remainder>(remainder.release())
            );
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (std::integral<L> && std::integral<R>) {
                return static_cast<double>(lhs) / rhs;
            } else {
                return lhs / rhs;
            }
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto mod(const L& lhs, const R& rhs) {
            return lhs - (div(lhs, rhs) * rhs);
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto divmod(const L& lhs, const R& rhs) {
            auto quotient = div(lhs, rhs);
            return std::make_pair(quotient, lhs - (quotient * rhs));
        }

        template <typename O>
        static auto round(const O& obj) {
            return obj;
        }

    };

    struct CDiv {

        /* C division uses / operator in most cases, except for Python integers, which
         * use // and add a bias to replicate C's truncation behavior.  Modulus returns
         * the amount of precision loss incurred by the division, or the remainder for
         * integer division.
         */

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        static auto div(const L& lhs, const R& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Int a = lhs, b = rhs;
                Int quotient = impl::ops::operator_floordiv<Int>(a, b);
                if ((quotient < Int::zero()) && ((a % b) != Int::zero())) {
                    ++quotient;
                }
                return quotient;

            } else if constexpr (std::same_as<L, Object> && std::same_as<R, Object>) {
                Object a = lhs, b = rhs;
                if (PyLong_Check(a.ptr()) && PyLong_Check(b.ptr())) {
                    Object quotient = impl::ops::operator_floordiv<Object>(a, b);
                    if ((quotient < Int::zero()) && ((a % b) != Int::zero())) {
                        ++quotient;
                    }
                    return quotient;
                }
                return a / b;

            } else {
                return lhs / rhs;
            }
        }

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        static auto mod(const L& lhs, const R& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Int a = lhs, b = rhs;
                Int remainder = a % b;
                if ((a < Int::zero()) && (remainder != Int::zero())) {
                    return -remainder;
                }
                return remainder;

            } else if constexpr (std::same_as<L, Object> && std::same_as<R, Object>) {
                Object a = lhs, b = rhs;
                if (PyLong_Check(a.ptr()) && PyLong_Check(b.ptr())) {
                    Object remainder = a % b;
                    if ((a < Int::zero()) && (remainder != Int::zero())) {
                        return -remainder;
                    }
                    return remainder;
                }
                return a - ((a / b) * b);

            } else {
                using Remainder = decltype(lhs - ((lhs / rhs) * rhs));
                Object a = lhs, b = rhs;
                return reinterpret_steal<Remainder>((a - ((a / b) * b)).release());
            }
        }

        template <typename L, typename R> requires (impl::object_operand<L, R>)
        static auto divmod(const L& lhs, const R& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Int a = lhs, b = rhs;
                Int quotient = impl::ops::operator_floordiv<Int>(a, b);
                Int remainder = a % b;
                if ((quotient < Int::zero()) && (remainder != Int::zero())) {
                    ++quotient;
                    return std::make_pair(quotient, -remainder);
                }
                return std::make_pair(quotient, remainder);

            } else if constexpr (std::same_as<L, Object> && std::same_as<R, Object>) {
                Object a = lhs, b = rhs;
                if (PyLong_Check(a.ptr()) && PyLong_Check(b.ptr())) {
                    Object quotient = impl::ops::operator_floordiv<Object>(a, b);
                    Object remainder = a % b;
                    if ((quotient < Int::zero()) && (remainder != Int::zero())) {
                        ++quotient;
                        return std::make_pair(quotient, -remainder);
                    }
                    return std::make_pair(quotient, remainder);
                }
                Object quotient = a / b;
                return std::make_pair(quotient, a - (quotient * b));

            } else {
                using Quotient = decltype(lhs / rhs);
                using Remainder = decltype(lhs - ((lhs / rhs) * rhs));
                Object a = lhs, b = rhs;
                Object quotient = a / b;
                Object remainder = a - (quotient * b);
                return std::make_pair(
                    reinterpret_steal<Quotient>(quotient.release()),
                    reinterpret_steal<Remainder>(remainder.release())
                );
            }
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto div(const L& lhs, const R& rhs) {
            return lhs / rhs;
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto mod(const L& lhs, const R& rhs) {
            if constexpr (std::floating_point<L> || std::floating_point<R>) {
                return std::fmod(lhs, rhs);
            } else {
                return lhs % rhs;
            }
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto divmod(const L& lhs, const R& rhs) {
            if constexpr (std::floating_point<L> || std::floating_point<R>) {
                return std::make_pair(lhs / rhs, std::fmod(lhs, rhs));
            } else {
                return std::make_pair(lhs / rhs, lhs % rhs);
            }
        }

        template <typename O>
        static auto round(const O& obj) {
            return obj;
        }

    };

    struct Floor : public Base<Floor> {

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            return impl::ops::operator_floordiv<Quotient>(lhs, rhs);
        }

        template <typename Remainder>
        static auto py_mod(const Object& lhs, const Object& rhs) {
            return impl::ops::operator_mod<Remainder>(lhs, rhs);
        }

        // TODO: maybe use built-in Python divmod()?  It might avoid some extra work,
        // but it also allocates a tuple, which might make it slower overall.  This is
        // something to benchmark.

        template <typename Quotient, typename Remainder, typename L, typename R>
        static auto py_divmod(const Object& lhs, const Object& rhs) {
            return std::make_pair(
                impl::ops::operator_floordiv<Quotient>(lhs, rhs),
                impl::ops::operator_mod<Remainder>(lhs, rhs)
            );
        }

        template <typename O>
        static auto py_round(const O& obj) {
            return div(obj, Int::one());
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            auto quotient = lhs / rhs;
            if constexpr (std::signed_integral<L> && std::signed_integral<R>) {
                return quotient - (((lhs < 0) ^ (rhs < 0)) && ((lhs % rhs) != 0));
            } else if constexpr (std::integral<L> && std::integral<R>) {
                return quotient;
            } else {
                return std::floor(quotient);
            }
        }

        template <typename L, typename R>
        static auto cpp_mod(const L& lhs, const R& rhs) {
            auto correct = rhs * ((lhs < 0) ^ (rhs < 0));
            if constexpr (std::floating_point<L> || std::floating_point<R>) {
                return std::fmod(lhs, rhs) + correct;
            } else {
                return (lhs % rhs) + correct;
            }
        }

        template <typename L, typename R>
        static auto cpp_divmod(const L& lhs, const R& rhs) {
            return std::make_pair(div(lhs, rhs), mod(lhs, rhs));
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            return std::floor(obj);
        }

    };

    struct Ceiling : public Base<Ceiling> {

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            return reinterpret_steal<Quotient>(
                (-impl::ops::operator_floordiv<Object>(-lhs, rhs)).release()
            );
        }

        template <typename Remainder>
        static auto py_mod(const Object& lhs, const Object& rhs) {
            return reinterpret_steal<Remainder>(
                -impl::ops::operator_mod<Object>(-lhs, rhs).release()
            );
        }

        template <typename Quotient, typename Remainder, typename L, typename R>
        static auto py_divmod(const Object& lhs, const Object& rhs) {
            Object c = -lhs;
            return std::make_pair(
                reinterpret_steal<Quotient>(
                    -impl::ops::operator_floordiv<Object>(c, rhs).release()
                ),
                reinterpret_steal<Remainder>(
                    -impl::ops::operator_mod<Object>(c, rhs).release()
                )
            );
        }

        template <typename O>
        static auto py_round(const O& obj) {
            return div(obj, Int::one());
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            auto quotient = lhs / rhs;
            if constexpr (std::integral<L> && std::integral<R>) {
                return quotient + (((lhs < 0) == (rhs < 0)) && ((lhs % rhs) != 0));
            } else {
                return std::ceil(quotient);
            }
        }

        template <typename L, typename R>
        static auto cpp_mod(const L& lhs, const R& rhs) {
            auto correct = rhs * ((lhs < 0) == (rhs < 0));
            if constexpr (std::floating_point<L> || std::floating_point<R>) {
                return std::fmod(lhs, rhs) - correct;
            } else {
                return (lhs % rhs) - correct;
            }
        }

        template <typename L, typename R>
        static auto cpp_divmod(const L& lhs, const R& rhs) {
            return std::make_pair(div(lhs, rhs), mod(lhs, rhs));
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            return std::ceil(obj);
        }

    };

    struct Down : public Base<Down> {

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {
                return reinterpret_steal<Quotient>(
                    (-impl::ops::operator_floordiv<Object>(-lhs, rhs)).release()
                );
            } else {
                return impl::ops::operator_floordiv<Quotient>(lhs, rhs);
            }
        }

        template <typename Remainder>
        static auto py_mod(const Object& lhs, const Object& rhs) {
            if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {
                return reinterpret_steal<Remainder>(
                    -impl::ops::operator_floordiv<Object>(-lhs, rhs).release()
                );
            } else {
                return impl::ops::operator_mod<Remainder>(lhs, rhs);
            }
        }

        template <typename Quotient, typename Remainder, typename L, typename R>
        static auto py_divmod(const Object& lhs, const Object& rhs) {
            if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {
                Object c = -lhs;
                return std::make_pair(
                    reinterpret_steal<Quotient>(
                        -impl::ops::operator_floordiv<Object>(c, rhs).release()
                    ),
                    reinterpret_steal<Remainder>(
                        -impl::ops::operator_mod<Object>(c, rhs).release()
                    )
                );
            } else {
                return std::make_pair(
                    impl::ops::operator_floordiv<Quotient>(lhs, rhs),
                    impl::ops::operator_mod<Remainder>(lhs, rhs)
                );
            }
        }

        template <typename O>
        static auto py_round(const O& obj) {
            return div(obj, Int::one());
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Ceiling::cpp_div(lhs, rhs);
            } else {
                return Floor::cpp_div(lhs, rhs);
            }
        }

        template <typename L, typename R>
        static auto cpp_mod(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Ceiling::cpp_mod(lhs, rhs);
            } else {
                return Floor::cpp_mod(lhs, rhs);
            }
        }

        template <typename L, typename R>
        static auto cpp_divmod(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Ceiling::cpp_divmod(lhs, rhs);
            } else {
                return Floor::cpp_divmod(lhs, rhs);
            }
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            return std::trunc(obj);
        }

    };

    struct Up : public Base<Up> {

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {
                return impl::ops::operator_floordiv<Quotient>(lhs, rhs);
            } else {
                return reinterpret_steal<Quotient>(
                    (-impl::ops::operator_floordiv<Object>(-lhs, rhs)).release()
                );
            }
        }

        template <typename Remainder>
        static auto py_mod(const Object& lhs, const Object& rhs) {
            if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {
                return impl::ops::operator_mod<Remainder>(lhs, rhs);
            } else {
                return reinterpret_steal<Remainder>(
                    -impl::ops::operator_mod<Object>(-lhs, rhs).release()
                );
            }
        }

        template <typename Quotient, typename Remainder, typename L, typename R>
        static auto py_divmod(const Object& lhs, const Object& rhs) {
            if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {
                return std::make_pair(
                    impl::ops::operator_floordiv<Quotient>(lhs, rhs),
                    impl::ops::operator_mod<Remainder>(lhs, rhs)
                );
            } else {
                Object c = -lhs;
                return std::make_pair(
                    reinterpret_steal<Quotient>(
                        -impl::ops::operator_floordiv<Object>(c, rhs).release()
                    ),
                    reinterpret_steal<Remainder>(
                        -impl::ops::operator_mod<Object>(c, rhs).release()
                    )
                );
            }
        }

        template <typename O>
        static auto py_round(const O& obj) {
            return div(obj, Int::one());
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Floor::cpp_div(lhs, rhs);
            } else {
                return Ceiling::cpp_div(lhs, rhs);
            }
        }

        template <typename L, typename R>
        static auto cpp_mod(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Floor::cpp_mod(lhs, rhs);
            } else {
                return Ceiling::cpp_mod(lhs, rhs);
            }
        }

        template <typename L, typename R>
        static auto cpp_divmod(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Floor::cpp_divmod(lhs, rhs);
            } else {
                return Ceiling::cpp_divmod(lhs, rhs);
            }
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            if (obj < 0) {
                return std::floor(obj);
            } else {
                return std::ceil(obj);
            }
        }

    };

    struct HalfFloor : public Base<HalfFloor> {

        /* NOTE: The Python formula for half-floor integer division is:
         *
         *      bias = (d + ~(d < 0)) // 2
         *      (n + bias) // d
         *
         * C++ integer division adds an extra term to the bias:
         *
         *      sign = +1 if operands are the same sign, -1 otherwise
         *      bias = (d + (n < 0) - (d > 0)) / 2
         *      (n + sign * bias) / d
         *
         * For floating point values, it is equivalent to:
         *
         *      ceiling_div(n - d / 2, d))
         *
         * These equations are branchless and preserve as much accuracy as possible in
         * both cases.
         */

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Object a {rhs > Int::zero()};
                Object numerator = lhs + impl::ops::operator_floordiv<Object>(
                    rhs + ~a,
                    Int::two()
                );
                return reinterpret_steal<Quotient>(
                    impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                );
                /*   a ~a    (rhs + ~a) // 2
                 *   0 -1  = (rhs -  1) // 2  =  (rhs - 1) // 2 
                 *   0 -1  = (rhs -  1) // 2  =  (rhs - 1) // 2
                 *   1 -2  = (rhs -  2) // 2  =  (rhs - 2) // 2
                 *   1 -2  = (rhs -  2) // 2  =  (rhs - 2) // 2
                 */
            } else if constexpr (std::same_as<L, Object> || std::same_as<R, Object>) {
                if (PyLong_Check(lhs.ptr()) && PyLong_Check(rhs.ptr())) {
                    Object a {rhs > Int::zero()};
                    Object numerator = lhs + impl::ops::operator_floordiv<Object>(
                        rhs + ~a,
                        Int::two()
                    );
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                    );
                }
                return reinterpret_steal<Quotient>(
                    -impl::ops::operator_floordiv<Object>(
                        -lhs + rhs / Int::two(),
                        rhs
                    ).release()
                );

            } else {
                return reinterpret_steal<Quotient>(
                    -impl::ops::operator_floordiv<Object>(
                        -lhs + rhs / Int::two(),
                        rhs
                    ).release()
                );
            }
        }

        template <typename O>
        static auto py_round(const O& obj) {
            return Ceiling::py_round(obj - Float::half());
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
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
                return Ceiling::cpp_div(lhs - rhs / 2, rhs);
            }
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            return Ceiling::cpp_round(obj - 0.5);
        }

    };

    struct HalfCeiling : public Base<HalfCeiling> {

        /* NOTE: The Python formula for half-floor integer division is:
         *
         *      bias = (d + (d < 0)) // 2
         *      (n + bias) // d
         *
         * C++ integer division adds an extra term to the bias:
         *
         *      sign = +1 if operands are the same sign, -1 otherwise
         *      bias = (d + ~((n < 0) - (d > 0))) / 2
         *      (n + sign * bias) / d
         *
         * For floating point values, it is equivalent to:
         *
         *      floor_div(n + d / 2, d))
         *
         * These equations are branchless and preserve as much accuracy as possible in
         * both cases.
         */

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Object a {rhs > Int::zero()};
                Object numerator = lhs + impl::ops::operator_floordiv<Object>(
                    rhs + a,
                    Int::two()
                );
                return reinterpret_steal<Quotient>(
                    impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                );
                /*   a    (rhs + a) // 2
                 *   0  = (rhs + 0) // 2  =  (rhs    ) // 2 
                 *   0  = (rhs + 0) // 2  =  (rhs    ) // 2
                 *   1  = (rhs + 1) // 2  =  (rhs + 1) // 2
                 *   1  = (rhs + 1) // 2  =  (rhs + 1) // 2
                 */
            } else if constexpr (std::same_as<L, Object> || std::same_as<R, Object>) {
                if (PyLong_Check(lhs.ptr()) && PyLong_Check(rhs.ptr())) {
                    Object a {rhs > Int::zero()};
                    Object numerator = lhs +  impl::ops::operator_floordiv<Object>(
                        rhs + a,
                        Int::two()
                    );
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                    );
                }
                return reinterpret_steal<Quotient>(
                    impl::ops::operator_floordiv<Object>(
                        lhs + rhs / Int::two(),
                        rhs
                    ).release()
                );

            } else {
                return reinterpret_steal<Quotient>(
                    impl::ops::operator_floordiv<Object>(
                        lhs + rhs / Int::two(),
                        rhs
                    ).release()
                );
            }
        }

        template <typename O>
        static auto py_round(const O& obj) {
            return Floor::py_round(obj + Float::half());
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
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
                return Floor::cpp_div(lhs + rhs / 2, rhs);
            }
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            return Floor::cpp_round(obj + 0.5);
        }

    };

    struct HalfDown : public Base<HalfDown> {

        /* NOTE: integer division uses half-floor where operands are the same sign, and
         * half-ceiling where they differ.
         */

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Object a {rhs > Int::zero()};
                Object b {lhs > Int::zero()};
                Object numerator = lhs;
                if (a == b) {  // NOTE: branches are faster than writing more Python
                    numerator += impl::ops::operator_floordiv<Object>(
                        rhs + ~a,
                        Int::two()
                    );
                } else {
                    numerator += impl::ops::operator_floordiv<Object>(
                        rhs + a,
                        Int::two()
                    );
                }
                return reinterpret_steal<Quotient>(
                    impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                );

            } else if (std::same_as<L, Object> || std::same_as<R, Object>) {
                if (PyLong_Check(lhs.ptr()) && PyLong_Check(rhs.ptr())) {
                    Object a {rhs > Int::zero()};
                    Object b { lhs > Int::zero() };
                    Object numerator = lhs;
                    if (a == b) {  // NOTE: branches are faster than writing more Python
                        numerator += impl::ops::operator_floordiv<Object>(
                            rhs + ~a,
                            Int::two()
                        );
                    } else {
                        numerator += impl::ops::operator_floordiv<Object>(
                            rhs + a,
                            Int::two()
                        );
                    }
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                    );
                }

                // half-floor where operands are different signs, half-ceiling otherwise
                Object bias = rhs / Int::two();
                if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {  // half-ceiling
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(
                            lhs + bias,
                            rhs
                        ).release()
                    );
                } else {  // half-floor
                    return reinterpret_steal<Quotient>(
                        -impl::ops::operator_floordiv<Object>(
                            -lhs + bias,
                            rhs
                        ).release()
                    );
                }

            } else {
                // half-floor where operands are different signs, half-ceiling otherwise
                Object bias = rhs / Int::two();
                if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {  // half-ceiling
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(
                            lhs + bias,
                            rhs
                        ).release()
                    );
                } else {  // half-floor
                    return reinterpret_steal<Quotient>(
                        -impl::ops::operator_floordiv<Object>(
                            -lhs + bias,
                            rhs
                        ).release()
                    );
                }
            }
        }

        template <typename O>
        static auto py_round(const O& obj) {
            if (obj > Int::zero()) {
                return HalfFloor::py_round(obj);
            } else {
                return HalfCeiling::py_round(obj);
            }
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            if constexpr (std::integral<L> && std::integral<R>) {
                bool a = lhs < 0;
                bool b = rhs > 0;
                int c = a - b;
                bool d = a != b;
                bool e = a == b;
                return (lhs + (d - e) * ((rhs + c * d + ~c * e) / 2)) / rhs;
            } else {
                if ((lhs < 0) ^ (rhs < 0)) {
                    return Ceiling::cpp_div(lhs - rhs / 2, rhs);
                } else {
                    return Floor::cpp_div(lhs + rhs / 2, rhs);
                }
            }
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            if (obj > 0) {
                return HalfFloor::cpp_round(obj);
            } else {
                return HalfCeiling::cpp_round(obj);
            }
        }

    };

    struct HalfUp : public Base<HalfUp> {

        /* NOTE: integer division uses half-ceiling where operands are the same sign,
         * and half-floor where they differ.
         */

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Object a {rhs > Int::zero()};
                Object b {lhs > Int::zero()};
                Object numerator = lhs;
                if (a == b) {  // NOTE: branches are faster than writing more Python
                    numerator += impl::ops::operator_floordiv<Object>(
                        rhs + a,
                        Int::two()
                    );
                } else {
                    numerator += impl::ops::operator_floordiv<Object>(
                        rhs + ~a,
                        Int::two()
                    );
                }
                return reinterpret_steal<Quotient>(
                    impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                );

            } else if (std::same_as<L, Object> || std::same_as<R, Object>) {
                if (PyLong_Check(lhs.ptr()) && PyLong_Check(rhs.ptr())) {
                    Object a {rhs > Int::zero()};
                    Object b {lhs > Int::zero()};
                    Object numerator = lhs;
                    if (a == b) {  // NOTE: branches are faster than writing more Python
                        numerator += impl::ops::operator_floordiv<Object>(
                            rhs + a,
                            Int::two()
                        );
                    } else {
                        numerator += impl::ops::operator_floordiv<Object>(
                            rhs + ~a,
                            Int::two()
                        );
                    }
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                    );
                }

                // half-floor where operands are different signs, half-ceiling otherwise
                Object bias = rhs / Int::two();
                if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {  // half-floor
                    return reinterpret_steal<Quotient>(
                        -impl::ops::operator_floordiv<Object>(
                            -lhs + bias,
                            rhs
                        ).release()
                    );
                } else {  // half-ceiling
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(
                            lhs + bias,
                            rhs
                        ).release()
                    );
                }

            } else {
                // half-floor where operands are different signs, half-ceiling otherwise
                Object bias = rhs / Int::two();
                if ((lhs < Int::zero()) ^ (rhs < Int::zero())) {  // half-floor
                    return reinterpret_steal<Quotient>(
                        -impl::ops::operator_floordiv<Object>(
                            -lhs + bias,
                            rhs
                        ).release()
                    );
                } else {  // half-ceiling
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(
                            lhs + bias,
                            rhs
                        ).release()
                    );
                }
            }
        }

        template <typename O>
        static auto py_round(const O& obj) {
            if (obj > Int::zero()) {
                return HalfCeiling::py_round(obj);
            } else {
                return HalfFloor::py_round(obj);
            }
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            if constexpr (std::integral<L> && std::integral<R>) {
                bool a = lhs < 0;
                bool b = rhs > 0;
                int c = a - b;
                bool d = a != b;
                bool e = a == b;
                return (lhs + (d - e) * ((rhs + ~c * d + c * e) / 2)) / rhs;
            } else {
                if ((lhs < 0) ^ (rhs < 0)) {
                    return Floor::cpp_div(lhs + rhs / 2, rhs);
                } else {
                    return Ceiling::cpp_div(lhs - rhs / 2, rhs);
                }
            }
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            return std::round(obj);  // always rounds away from zero
        }

    };

    class HalfEven : public Base<HalfEven> {

        static Function get_round() {
            PyObject* builtins = PyEval_GetBuiltins();
            PyObject* round = PyDict_GetItemString(builtins, "round");
            if (round == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Function>(round);
        }

    public:

        /* NOTE: integer division applies half-ceiling where the quotient `n // d`
         * would be odd, and half-floor where it would be even.
         */

        template <typename Quotient, typename L, typename R>
        static auto py_div(const Object& lhs, const Object& rhs) {
            if constexpr (impl::int_like<L> && impl::int_like<R>) {
                Object a {rhs > Int::zero()};
                Object numerator = lhs;
                if (impl::ops::operator_floordiv<Object>(lhs, rhs) % Int::two()) {
                    numerator += impl::ops::operator_floordiv<Object>(
                        rhs + a,
                        Int::two()
                    );
                } else {
                    numerator += impl::ops::operator_floordiv<Object>(
                        rhs + ~a,
                        Int::two()
                    );
                }
                return reinterpret_steal<Quotient>(
                    impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                );

            } else if (std::same_as<L, Object> || std::same_as<R, Object>) {
                if (PyLong_Check(lhs.ptr()) && PyLong_Check(rhs.ptr())) {
                    Object a {rhs > Int::zero()};
                    Object numerator = lhs;
                    if (impl::ops::operator_floordiv<Object>(lhs, rhs) % Int::two()) {
                        numerator += impl::ops::operator_floordiv<Object>(
                            rhs + a,
                            Int::two()
                        );
                    } else {
                        numerator += impl::ops::operator_floordiv<Object>(
                            rhs + ~a,
                            Int::two()
                        );
                    }
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(numerator, rhs).release()
                    );
                }

                // half-ceiling where the quotient would be odd, half-floor otherwise
                Object bias = rhs / Int::two();
                if (impl::ops::operator_floordiv<Object>(lhs, rhs) % Int::two()) {  // half-ceiling
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(
                            lhs + bias,
                            rhs
                        ).release()
                    );
                } else {  // half-floor
                    return reinterpret_steal<Quotient>(
                        -impl::ops::operator_floordiv<Object>(
                            -lhs + bias,
                            rhs
                        ).release()
                    );
                }

            } else {
                // half-ceiling where the quotient would be odd, half-floor otherwise
                Object bias = rhs / Int::two();
                if (impl::ops::operator_floordiv<Object>(lhs, rhs) % Int::two()) {  // half-ceiling
                    return reinterpret_steal<Quotient>(
                        impl::ops::operator_floordiv<Object>(
                            lhs + bias,
                            rhs
                        ).release()
                    );
                } else {  // half-floor
                    return reinterpret_steal<Quotient>(
                        -impl::ops::operator_floordiv<Object>(
                            -lhs + bias,
                            rhs
                        ).release()
                    );
                }
            }
        }

        template <typename O>
        static auto py_round(const O& obj) {
            static const Function func = get_round();
            PyObject* result = PyObject_CallOneArg(func.ptr(), obj.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<O>(result);
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
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
                    return Ceiling::cpp_div(lhs, rhs);
                } else {
                    return Floor::cpp_div(lhs, rhs);
                }
            }
        }

        template <typename O>
        static auto cpp_round(const O& obj) {
            O whole, fract;
            fract = std::modf(obj, &whole);
            if (std::abs(fract) == 0.5) {
                // NOTE: fmod evaluates negative if whole < 0
                return whole + std::fmod(whole, 2);
            } else {
                return std::round(obj);
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
        return div(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return div(lhs, rhs.value());
    } else {
        return mode.div(lhs, rhs);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode = Round::Floor>
    requires (impl::mod_mode<L, R, Mode>)
inline auto mod(const L& lhs, const R& rhs, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return mod(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return mod(lhs, rhs.value());
    } else {
        return mode.mod(lhs, rhs);
    }
}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode = Round::Floor>
    requires (impl::divmod_mode<L, R, Mode>)
inline auto divmod(const L& lhs, const R& rhs, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return divmod(lhs.value(), rhs);
    } else if constexpr (impl::proxy_like<R>) {
        return divmod(lhs, rhs.value());
    } else {
        return mode.divmod(lhs, rhs);
    }
}


/* Round the operand to a given number of digits according to the specified rounding
rule.  Positive digits count to the right of the decimal point, while negative values
count to the left. */
template <typename O, typename Mode = Round::HalfEven>
    requires (impl::round_mode<O, Mode>)
inline auto round(const O& obj, int digits = 0, const Mode& mode = Round::HALF_EVEN) {
    if constexpr (impl::proxy_like<O>) {
        return round(obj.value(), digits, mode);
    } else {
        return mode.round(obj, digits);
    }
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_MATH_H
