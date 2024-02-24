#ifndef BERTRAND_PYTHON_MATH_H
#define BERTRAND_PYTHON_MATH_H

#include <cfenv>  // round modes
#include <cmath>
#include <type_traits>

#include "common.h"
#include "tuple.h"


namespace bertrand {
namespace py {


// TODO: round
// TODO: round_div
// TODO: round_mod
// TODO: round_divmod

// py::Round::NONE



/* Collection of tag structs describing rounding strategies for round(). */
class Round {
    struct RoundTag {};

    template <typename T>
    static constexpr bool PYTHONLIKE = detail::is_pyobject<T>::value;

    template <typename T>
    static constexpr bool INTLIKE = std::is_integral_v<T>;

    template <typename T>
    static constexpr bool FLOATLIKE = std::is_floating_point_v<T>;

    /* Do not round.  Used to implement Python-style "true" division. */
    struct True : RoundTag {

        template <typename L, typename R>
        inline auto div(const L& l, const R& r) const {
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
        inline auto mod(const L& l, const R& r) const {
            return l - (divide(l, r) * r);  // equal to rounding error
        }

        template <typename L, typename R>
        inline auto divmod(const L& l, const R& r) const {
            auto quotient = divide(l, r);
            return std::make_pair(quotient, l - (quotient * r));
        }

        template <typename O>
        inline auto round(const O& o) const {
            return o;
        }

    };

    /* Apply C-style division.  Rounds integers toward zero and apply true division
    everywhere else. */
    struct CDiv : RoundTag {

        template <typename L, typename R>
        auto div(const L& l, const R& r) const {
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
        inline auto mod(const L& l, const R& r) const {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                PyObject* result = PyNumber_Remainder(
                    detail::object_or_cast(l).ptr(),
                    detail::object_or_cast(r).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                static const Int zero(0);
                return (
                    reinterpret_steal<Object>(result) *
                    Int(1 - 2 * (l < zero ^ r < zero))
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
        auto divmod(const L& l, const R& r) const {
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
                    if (quotient < 0 && remainder != 0) {
                        quotient += 1;
                        remainder *= -1;
                    }
                    return std::make_pair<quotient, remainder>;

                } else {
                    static const Int zero(0);
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
                        Int(1 - 2 * (l < zero ^ r < zero))
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

        template <typename O>
        inline auto round(const O& o) const {
            return o;
        }

    };

    /* Round toward negative infinity. */
    struct Floor : RoundTag {

        template <typename L, typename R>
        inline auto div(const L& l, const R& r) const {
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
                    return result - (result < 0 && (l % r != 0));
                } else {
                    return std::floor(result);
                }
            }
        }

        template <typename L, typename R>
        inline auto mod(const L& l, const R& r) const {
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
                int correction = (1 - 2 * (l < 0 ^ r < 0));
                if constexpr (FLOATLIKE<L> || FLOATLIKE<R>) {
                    // branchless: mod * -1 if only one of the operands is negative
                    return std::fmod(l, r) * correction;
                } else {
                    return (l % r) * correction;
                }
            }
        }


        template <typename L, typename R>
        inline auto divmod(const L& l, const R& r) const {
            if constexpr (PYTHONLIKE<L> || PYTHONLIKE<R>) {
                PyObject* result = PyNumber_Divmod(
                    detail::object_or_cast(l).ptr(),
                    detail::object_or_cast(r).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return std::make_pair(
                    PyTuple_GET_ITEM(result, 0),
                    PyTuple_GET_ITEM(result, 1)
                );

            } else {
                if (r == 0) {
                    throw ZeroDivisionError("division by zero");
                }

                auto quotient = l / r;
                if constexpr (std::is_integral_v<L> && std::is_integral_v<R>) {
                    quotient -= (quotient < 0 && l % r != 0);
                } else {
                    quotient = std::floor(quotient);
                }

                int correction = (1 - 2 * (l < 0 ^ r < 0));
                if constexpr (FLOATLIKE<L> || FLOATLIKE<R>) {
                    return std::make_pair(quotient, std::fmod(l, r) * correction);
                } else {
                    return std::make_pair(quotient, (l % r) * correction);
                }
            }
        }

        template <typename O>
        inline auto round(const O& o) const {
            if constexpr (PYTHONLIKE<O>) {
                if (PyLong_Check(o.ptr())) {
                    return reinterpret_borrow<Object>(o.ptr());
                } else {
                    return Object(o.attr("__floor__")());
                }
            } else if constexpr (INTLIKE<O>) {
                return o;
            } else {
                return std::floor(o);
            }
        }

    };

    /* Round toward positive infinity. */
    struct Ceiling : RoundTag {

        template <typename L, typename R>
        inline auto div(const L& l, const R& r) const {
            // TODO
        }

        template <typename O>
        inline auto round(const O& o) const {
            if constexpr (PYTHONLIKE<O>) {
                if (PyLong_Check(o.ptr())) {
                    return reinterpret_borrow<Object>(o.ptr());
                } else {
                    return Object(o.attr("__ceil__")());
                }
            } else if constexpr (INTLIKE<O>) {
                return o;
            } else {
                return std::ceil(o);
            }
        }

    };

    /* Round toward zero. */
    struct Down : RoundTag {


        template <typename O>
        inline auto round(const O& o) const {
            if constexpr (PYTHONLIKE<O>) {
                if (PyLong_Check(o.ptr())) {
                    return reinterpret_borrow<Object>(o.ptr());
                } else {
                    return Object(o.attr("__trunc__")());
                }
            } else if constexpr (INTLIKE<O>) {
                return o;
            } else {
                return std::trunc(o);
            }
        }

    };

    /* Round away from zero. */
    struct Up : RoundTag {

        template <typename O>
        inline auto round(const O& o) const {
            if constexpr (PYTHONLIKE<O>) {
                static const Int zero(0);
                if (PyLong_Check(o.ptr())) {
                    return reinterpret_borrow<Object>(o.ptr());
                } else if (o < zero) {
                    return Object(o.attr("__floor__")());
                } else {
                    return Object(o.attr("__ceil__")());
                }
            } else if constexpr (INTLIKE<O>) {
                return o;
            } else {
                if (o < 0) {
                    return std::floor(o);
                } else {
                    return std::ceil(o);
                }
            }
        }

    };

    /* Round to nearest, with ties toward negative infinity. */
    struct HalfFloor : RoundTag {
        // TODO

    };

    /* Round to nearest, with ties toward positive infinity. */
    struct HalfCeiling : RoundTag {
        // TODO
    };

    /* Round to nearest, with ties toward zero. */
    struct HalfDown : RoundTag {

        template <typename O>
        inline auto round(const O& o) const {
            if constexpr (PYTHONLIKE<O>) {
                // TODO
            } else if constexpr (INTLIKE<O>) {
                return o;
            } else {
                int saved = std::fegetround();
                std::fesetround(FE_TOWARDZERO);
                auto result = std::nearbyint(o);
                std::fesetround(saved);
                return result;
            }
        }

    };

    /* Round to nearest, with ties away from zero. */
    struct HalfUp : RoundTag {
 
        template <typename O>
        inline auto round(const O& o) const {
            if constexpr (PYTHONLIKE<O>) {
                // TODO
            } else if constexpr (INTLIKE<O>) {
                return o;
            } else {
                return std::round(o);  // always rounds away from zero
            }
        }

    };

    /* Round to nearest, with ties toward the closest even value. */
    struct HalfEven : RoundTag {

        template <typename O>
        inline auto round(const O& o) const {
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
            } else if constexpr (INTLIKE<O>) {
                return o;
            } else {
                return std::nearbyint(o);
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
};


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


/* NOTE: C++'s math operators have slightly different semantics to Python in some
 * cases, which can be subtle and surprising for first-time users.  Here's a summary:
 *
 *  1.  C++ has no `**` exponentiation operator, so `std::pow()` is typically used
 *      instead.  Rather than overload a standard method, a separate `py::pow()`
 *      function is provided, which is consistent with both Python and C++.
 *  2.  Similarly, C++ has no `//` floor division operator, as it has entirely
 *      different division semantics to Python.  The `py::floordiv()` function is
 *      provided to account for this, and consistently applies Python semantics to both
 *      Python and C++ types.
 *  3.  C++ uses C-style division for the `/` operator, which differs from Python in
 *      its handling of integers.  In Python, `/` represents *true* division, which can
 *      return a float when dividing two integers.  In contrast, C++ truncates the
 *      result towards zero, which can cause compatibility issues when mixing Python
 *      and C++ code.  The `py::truediv()` and `py::cdiv()` functions are designed to
 *      account for this, and always apply the respective language's semantics to both
 *      Python and C++ inputs.
 *  4.  Because of the above, the `%` operator also has different semantics in both
 *      languages.  This amounts to a sign difference when one of the operands is
 *      negative, in order to satisfy the invariant `a == (a / b) * b + (a % b)`).  The
 *      `py::mod()` and `py::cmod()` functions force the respective language's semantics
 *      to be used for both Python and C++ inputs.
 *  5.  The `py::divmod()` and `py::cdivmod()` functions are provided for completeness,
 *      and combine the results of `py::floordiv()` and `py::mod()` or `py::cdiv()` and
 *      `py::cmod()`, respectively.
 */


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename L, typename R>
auto pow(L&& base, R&& exp) {
    if constexpr (
        detail::is_pyobject<std::decay_t<L>>::value ||
        detail::is_pyobject<std::decay_t<R>>::value
    ) {
        PyObject* result = PyNumber_Power(
            detail::object_or_cast(std::forward<L>(base)).ptr(),
            detail::object_or_cast(std::forward<R>(exp)).ptr(),
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
auto pow(L&& base, R&& exp, E&& mod) {
    using A = std::decay_t<L>;
    using B = std::decay_t<R>;
    using C = std::decay_t<E>;
    static_assert(
        (std::is_integral_v<A> || detail::is_pyobject<A>::value) &&
        (std::is_integral_v<B> || detail::is_pyobject<B>::value) &&
        (std::is_integral_v<C> || detail::is_pyobject<C>::value),
        "pow() 3rd argument not allowed unless all arguments are integers"
    );

    if constexpr (
        detail::is_pyobject<A>::value ||
        detail::is_pyobject<B>::value ||
        detail::is_pyobject<C>::value
    ) {
        PyObject* result = PyNumber_Power(
            detail::object_or_cast(std::forward<L>(base)).ptr(),
            detail::object_or_cast(std::forward<R>(exp)).ptr(),
            detail::object_or_cast(std::forward<E>(mod)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    } else {
        std::common_type_t<A, B, C> result = 1;
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


/* Divide the left and right operands according to the specified rounding rule. */
template <typename L, typename R, typename Strategy>
auto div(L&& n, R&& d, Strategy mode = Round::TRUE) {
    static_assert(
        Round::is_valid<Strategy>,
        "rounding strategy must be one of Round::TRUE, ::C, ::FLOOR, ::CEILING, "
        "::DOWN, ::UP, ::HALF_FLOOR, ::HALF_CEILING, ::HALF_DOWN, ::HALF_UP, or "
        "::HALF_EVEN"
    );
    return mode.div(n, d);
}


/* Compute the remainder of a division according to the specified rounding rule. */
template <typename L, typename R, typename Strategy>
auto mod(L&& n, R&& d, Strategy mode = Round::TRUE) {
    static_assert(
        Round::is_valid<Strategy>,
        "rounding strategy must be one of Round::TRUE, ::C, ::FLOOR, ::CEILING, "
        "::DOWN, ::UP, ::HALF_FLOOR, ::HALF_CEILING, ::HALF_DOWN, ::HALF_UP, or "
        "::HALF_EVEN"
    );
    return mode.mod(n, d);
}











///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


/* Below is a selection of unary operators that don't fit in the standard Python/C++
 * operator set.  The implementations that are provided here work equally well with
 * inputs from both languages, and consistently apply the same behavior in both cases.
 *
 * NOTE: Unlike Python, the round() function accepts an optional third parameter, which
 * specifies the rounding strategy to use.  This allows it to be used in place of the
 * floor()/ceil()/trunc() functions, which consolidates some of the syntax.
 */


/* Equivalent to Python `abs(obj)`. */
template <typename T>
inline auto abs(T&& obj) {
    if constexpr (detail::is_pyobject<std::decay_t<T>>::value) {
        PyObject* result = PyNumber_Absolute(obj.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    } else {
        return std::abs(obj);
    }
}




/* Equivalent to Python `round(obj, ndigits)`, but works on both Python and C++ inputs
and accepts a third tag parameter that determines the rounding strategy to apply. */
template <typename T, typename Strategy>
inline auto round(T&& obj, int ndigits = 0, Strategy strategy = Round::FLOOR) {
    static_assert(
        Round::is_valid<Strategy>,
        "rounding strategy must be one of Round::FLOOR, ::CEILING, ::DOWN, ::UP, "
        "::HALF_FLOOR, ::HALF_CEILING, ::HALF_DOWN, ::HALF_UP, or ::HALF_EVEN"
    );

    double scale = std::pow(10, ndigits);
    return strategy.execute(obj * scale) / scale;
}


/* Equivalent to Python `math.floor(number)`.  Used to implement the built-in `round()`
function. */
inline Object floor(const pybind11::handle& number) {
    return number.attr("__floor__")();
}


/* Equivalent to Python `math.ceil(number)`.  Used to implement the built-in `round()`
function. */
inline Object ceil(const pybind11::handle& number) {
    return number.attr("__ceil__")();
}


/* Equivalent to Python `math.trunc(number)`.  Used to implement the built-in `round()`
function. */
inline Object trunc(const pybind11::handle& number) {
    return number.attr("__trunc__")();
}



}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_MATH_H
