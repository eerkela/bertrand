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


// /* Collection of tag structs describing language-agnostic rounding strategies for
// div(), mod(), divmod(), and round(). */
// class Round {
//     struct RoundTag {};

//     template <typename T>
//     static constexpr bool PYTHONLIKE = impl::python_like<T>;

//     template <typename T>
//     static constexpr bool INTLIKE = std::is_integral_v<T>;

//     template <typename T>
//     static constexpr bool FLOATLIKE = std::is_floating_point_v<T>;

//     // basically, add/subtract 0.5 and then floor/ceil as needed.

//     /* Round to nearest, with ties toward negative infinity. */
//     struct HalfFloor : RoundTag {

//         /*
//          */

//         template <typename L, typename R>
//         inline static auto div(const L& l, const R& r) {
//             if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
//                 Object numerator = detail::object_or_cast(l);
//                 Object denominator = detail::object_or_cast(r);
//                 if (PyLong_Check(numerator.ptr()) && PyLong_Check(denominator.ptr())) {
//                     // TODO
//                 } else {
//                     return Ceiling::div(l - 0.5, r);
//                 }
//             } else {
//                 if (r == 0) {
//                     throw ZeroDivisionError("division by zero");
//                 }
//                 if constexpr (INTLIKE<L> && INTLIKE<R>) {
//                     // TODO
//                 } else {
//                     return Ceiling::div(l - 0.5, r);
//                 }
//             }
//         }

//     };

// };



/* A collection of tag structs used to implement cross-language rounding strategies for
basic `div()`, `mod()`, `divmod()`, and `round()` operators. */
class Round {

    template <typename Return>
    static consteval void check_truediv_type() {
        static_assert(
            std::is_base_of_v<Object, Return>,
            "True division operator must return a py::Object subclass.  Check your "
            "specialization of __truediv__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
    }

    template <typename Return>
    static consteval void check_floordiv_type() {
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Floor division operator must return a py::Object subclass.  Check your "
            "specialization of __floordiv__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
    }

    template <typename Return>
    static consteval void check_mod_type() {
        static_assert(
            std::is_base_of_v<Object, Return>,
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
            return Derived::template py_div<Quotient>(lhs, rhs);
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto div(const L& lhs, const R& rhs) {
            return Derived::cpp_div(lhs, rhs);
        }

        template <typename L, typename R> requires (__mod__<L, R>::enable)
        static auto mod(const L& lhs, const R& rhs) {
            using Remainder = typename __mod__<L, R>::Return;
            check_mod_type<Remainder>();
            return Derived::template py_mod<Remainder>(lhs, rhs);
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto mod(const L& lhs, const R& rhs) {
            return Derived::cpp_mod(lhs, rhs);
        }

        template <typename L, typename R>
            requires (__floordiv__<L, R>::enable && __mod__<L, R>::enable)
        static auto divmod(const L& lhs, const R& rhs) {
            using Quotient = typename __floordiv__<L, R>::Return;
            using Remainder = typename __mod__<L, R>::Return;
            check_floordiv_type<Quotient>();
            check_mod_type<Remainder>();
            return Derived::template py_divmod<Quotient, Remainder>(lhs, rhs);
        }

        template <typename L, typename R> requires (!impl::object_operand<L, R>)
        static auto divmod(const L& lhs, const R& rhs) {
            return Derived::cpp_divmod(lhs, rhs);
        }

    };

public:

    struct True {

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
                return std::make_pair(
                    reinterpret_steal<Quotient>(quotient.release()),
                    reinterpret_steal<Remainder>((a - (quotient * b)).release())
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

    class Floor : public Base<Floor> {
        friend Base<Floor>;

        template <typename Quotient>
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

        template <typename Quotient, typename Remainder>
        static auto py_divmod(const Object& lhs, const Object& rhs) {
            return std::make_pair(
                impl::ops::operator_floordiv<Quotient>(lhs, rhs),
                impl::ops::operator_mod<Remainder>(lhs, rhs)
            );
        }

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            auto quotient = lhs / rhs;
            if constexpr (std::signed_integral<L> && std::signed_integral<R>) {
                return quotient - ((quotient < 0) && ((lhs % rhs) != 0));
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

    public:

        template <typename O>
        static auto round(const O& obj) {
            if constexpr (std::floating_point<O>) {
                return std::floor(obj);
            } else {
                return div(obj, 1);
            }
        }

    };

    class Ceiling : public Base<Ceiling> {
        friend Base<Ceiling>;

        template <typename Quotient>
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

        template <typename Quotient, typename Remainder>
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

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            auto quotient = lhs / rhs;
            if constexpr (std::integral<L> && std::integral<R>) {
                return quotient + ((quotient > 0) && ((lhs % rhs) != 0));
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

    public:

        template <typename O>
        static auto round(const O& obj) {
            if constexpr (std::floating_point<O>) {
                return std::ceil(obj);
            } else {
                return div(obj, 1);
            }
        }

    };

    class Down : public Base<Down> {
        friend Base<Down>;

        template <typename Quotient>
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

        template <typename Quotient, typename Remainder>
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

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Ceiling::div(lhs, rhs);
            } else {
                return Floor::div(lhs, rhs);
            }
        }

        template <typename L, typename R>
        static auto cpp_mod(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Ceiling::mod(lhs, rhs);
            } else {
                return Floor::mod(lhs, rhs);
            }
        }

        template <typename L, typename R>
        static auto cpp_divmod(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Ceiling::divmod(lhs, rhs);
            } else {
                return Floor::divmod(lhs, rhs);
            }
        }

    public:

        template <typename O>
        static auto round(const O& obj) {
            if constexpr (std::floating_point<O>) {
                return std::trunc(obj);
            } else {
                return div(obj, 1);
            }
        }

    };

    class Up : public Base<Up> {
        friend Base<Up>;

        template <typename Quotient>
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

        template <typename Quotient, typename Remainder>
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

        template <typename L, typename R>
        static auto cpp_div(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Floor::div(lhs, rhs);
            } else {
                return Ceiling::div(lhs, rhs);
            }
        }

        template <typename L, typename R>
        static auto cpp_mod(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Floor::mod(lhs, rhs);
            } else {
                return Ceiling::mod(lhs, rhs);
            }
        }

        template <typename L, typename R>
        static auto cpp_divmod(const L& lhs, const R& rhs) {
            if ((lhs < 0) ^ (rhs < 0)) {
                return Floor::divmod(lhs, rhs);
            } else {
                return Ceiling::divmod(lhs, rhs);
            }
        }

    public:

        template <typename O>
        static auto round(const O& obj) {
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

}


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Mode = Round::Floor>
    requires (impl::div_mode<L, R, Mode>)
inline auto div(const L& lhs, const R& rhs, const Mode& mode = Round::FLOOR) {
    if constexpr (impl::proxy_like<L>) {
        return mode.div(lhs.value(), rhs);

    } else if constexpr (impl::proxy_like<R>) {
        return mode.div(lhs, rhs.value());

    } else {
        if constexpr (impl::object_operand<L, R>) {
            return mode.div(lhs, rhs);
        } else {
            if (rhs == 0) {
                throw ZeroDivisionError("division by zero");
            }
            return mode.div(lhs, rhs);
        }
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


// /* Equivalent to Python `round(obj, ndigits)`, but works on both Python and C++ inputs
// and accepts a third tag parameter that determines the rounding strategy to apply. */
// template <typename T, typename Mode = decltype(Round::HALF_EVEN)>
// auto round(const T& n, int digits = 0, const Mode& mode = Round::HALF_EVEN) {
//     static_assert(
//         Round::is_valid<Mode> && !Round::is_true_or_c<Mode>,
//         "rounding mode must be one of Round::FLOOR, ::CEILING, ::DOWN, ::UP, "
//         "::HALF_FLOOR, ::HALF_CEILING, ::HALF_DOWN, ::HALF_UP, or ::HALF_EVEN"
//     );

//     // TODO: mitigate precision loss when scaling
//     // -> if digits < 0, fall back to div?

//     // integer rounding only has an effect if digits are negative
//     if constexpr (std::is_integral_v<T>) {
//         if (digits >= 0) {
//             return n;
//         } else {
//             return div(n, static_cast<T>(std::pow(10, digits)), mode);
//         }

//     // same for Python integers
//     } else if constexpr (impl::python_like<T>) {
//         // // TODO: uncomment this when all tags have a functional div() method
//         // if (PyLong_Check(n.ptr())) {
//         //     if (digits >= 0) {
//         //         return reinterpret_borrow<Object>(n.ptr());
//         //     } else {
//         //         return div(n, Int(static_cast<long long>(pow(10, digits))), mode);
//         //     }
//         // } else {
//             double scale = std::pow(10, digits);
//             return Mode::round(n * scale) / scale;
//         // }

//     } else {
//         double scale = std::pow(10, digits);
//         return Mode::round(n * scale) / scale;
//     }
// }


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_MATH_H
