#ifndef BERTRAND_PYTHON_MATH_H
#define BERTRAND_PYTHON_MATH_H

#include <cmath>
#include <type_traits>

#include "common.h"
#include "tuple.h"


namespace bertrand {
namespace py {


//////////////////////////////
////    EXPONENTIATION    ////
//////////////////////////////


/* C++ has no `**` operator, so `pow()` must be used instead.
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
    // if constexpr (
    //     detail::is_pyobject<std::decay_t<L>>::value ||
    //     detail::is_pyobject<std::decay_t<R>>::value ||
    //     detail::is_pyobject<std::decay_t<E>>::value
    // ) {
    //     PyObject* result = PyNumber_Power(
    //         detail::object_or_cast(std::forward<L>(base)).ptr(),
    //         detail::object_or_cast(std::forward<R>(exp)).ptr(),
    //         detail::object_or_cast(std::forward<E>(mod)).ptr()
    //     );
    //     if (result == nullptr) {
    //         throw error_already_set();
    //     }
    //     return reinterpret_steal<Object>(result);
    // } else {
    //     return std::pow(base, exp);
    // }

    PyObject* result = PyNumber_Power(
        detail::object_or_cast(std::forward<L>(base)).ptr(),
        detail::object_or_cast(std::forward<R>(exp)).ptr(),
        detail::object_or_cast(std::forward<E>(mod)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


////////////////////////
////    DIVISION    ////
////////////////////////


/* NOTE: C++'s division operators have slightly different semantics to Python, which
 * can be subtle and surprising for first-time users.  By default, all wrapper classes
 * defer to Python's ordinary `/` operator, but this causes an asymmetry that can cause
 * compatibility issues when mixed with C++.  For example, `5 / 2 == 2`, while
 * `py::Int(5) / 2 == py::Float(2.5)`.  The following functions allow users to rectify
 * this by explicitly specifying the behavior of these operators.  Here's a summary:
 *
 *  truediv(): Equivalent to Python `a / b` (true division).  This is identical to
 *      the standard `/`, except that it coerces C++ integers into doubles before
 *      performing the division, matching Python's behavior.
 *  floordiv(): Equivalent to Python `a // b` (floor division).  C++ does not have an
 *      equivalent operator, and its `/` operator always rounds towards zero rather
 *      than negative infinity like in Python.
 *  cdiv(): Equivalent to C++ `a / b` (C++ division).  Rather than making C++ act like
 *      Python, this function does the opposite.  Python integers will be truncated
 *      towards zero, without affecting any other types.
 */


/* Equivalent to Python `a / b` (true division) for both Python and C++ types. */
template <typename L, typename R>
auto truediv(L&& numerator, R&& denominator) {
    if constexpr (std::is_integral_v<L> && std::is_integral_v<R>) {
        return static_cast<double>(numerator) / denominator;
    } else {
        return numerator / denominator;
    }
}


/* Equivalent to Python `a // b` (floor division) for both Python and C++ types. */
template <typename L, typename R>
auto floordiv(L&& numerator, R&& denominator) {
    if constexpr (
        detail::is_pyobject<std::decay_t<L>>::value ||
        detail::is_pyobject<std::decay_t<R>>::value
    ) {
        PyObject* result = PyNumber_FloorDivide(
            detail::object_or_cast(std::forward<L>(numerator)).ptr(),
            detail::object_or_cast(std::forward<R>(denominator)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    } else {
        if (denominator == 0) {
            throw ZeroDivisionError("division by zero");
        }
        auto result = numerator / denominator;
        if constexpr (
            std::is_integral_v<std::decay_t<L>> &&
            std::is_integral_v<std::decay_t<R>>
        ) {
            return result - (result < 0 && numerator % denominator != 0);
        } else {
            return std::floor(result);
        }
    }
}


/* Equivalent to C `a / b` (truncated division) for both Python and C++ types. */
template <typename L, typename R>
auto cdiv(L&& numerator, R&& denominator) {
    if constexpr (detail::is_pyobject<std::decay_t<L>>::value) {
        if (PyLong_Check(numerator.ptr())) {
            Object other = detail::object_or_cast(denominator);
            if (PyLong_Check(other.ptr())) {
                PyObject* temp = PyNumber_FloorDivide(numerator.ptr(), other.ptr());
                if (temp == nullptr) {
                    throw error_already_set();
                }
                Int result = reinterpret_steal<Int>(temp);
                if (result < 0) {
                    Int a = reinterpret_borrow<Int>(numerator.ptr());
                    Int b = reinterpret_borrow<Int>(other.ptr());
                    result += Int(a - (result * b)) != 0;
                }
                return reinterpret_steal<Object>(result.release());
            }
        }
    } else if constexpr (detail::is_pyobject<std::decay_t<R>>::value) {
        if (PyLong_Check(denominator.ptr())) {
            Object other = detail::object_or_cast(numerator);
            if (PyLong_Check(other.ptr())) {
                PyObject* temp = PyNumber_FloorDivide(other.ptr(), denominator.ptr());
                if (temp == nullptr) {
                    throw error_already_set();
                }
                Int result = reinterpret_steal<Int>(temp);
                if (result < 0) {
                    Int a = reinterpret_borrow<Int>(other.ptr());
                    Int b = reinterpret_borrow<Int>(denominator.ptr());
                    result += Int(a - (result * b)) != 0;
                }
                return reinterpret_steal<Object>(result.release());
            }
        }
    }

    return numerator / denominator;
}


// TODO: mod(), cmod(), divmod(), cdivmod()


//////////////////////
////    MODULO    ////
//////////////////////


/* NOTE: Similar to the division operators, C++'s `%` operator has different syntax to
 * Python.  By default, all wrapper classes defer to Python's ordinary `%` operator,
 * but the following functions can be used to explicitly alter this behavior.
 *
 *  mod(): Equivalent to Python `a % b` (modulo).  This is mostly identical to C++'s
 *      `%` except for how it handles negative numbers.  In Python, the sign of the
 *      remainder is always the same as the divisor, while in C++, it's the other way
 *      around.  This always uses the Python behavior.
 *  c_mod(): Equivalent to C++ `a % b` (C++ modulo).  This is the opposite of `mod()`,
 *      and always uses C++ behavior.
 *  divmod(): combines the results of `floordiv()` and `mod()`
 *  c_divmod(): combines the results of `c_div()` and `c_mod()`
 */


// TODO: divmod should return a std::pair rather than python tuple.


/* Equivalent to Python `divmod(a, b)`. */
template <typename L, typename R>
inline Tuple divmod(L&& a, R&& b) {
    PyObject* result = PyNumber_Divmod(
        detail::object_or_cast(std::forward<L>(a)).ptr(),
        detail::object_or_cast(std::forward<R>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Tuple>(result);
}


////////////////////////
////    ROUNDING    ////
////////////////////////


/* Equivalent to Python `round(obj)`. */
inline Object round(const pybind11::handle& obj) {
    PyObject* result = PyObject_CallOneArg(
        PyDict_GetItemString(PyEval_GetBuiltins(), "round"),
        obj.ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
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


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* Equivalent to Python `abs(obj)`. */
inline Object abs(const pybind11::handle& obj) {
    PyObject* result = PyNumber_Absolute(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}





}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_MATH_H
