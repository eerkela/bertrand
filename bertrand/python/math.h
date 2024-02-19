#ifndef BERTRAND_PYTHON_MATH_H
#define BERTRAND_PYTHON_MATH_H

#include "common.h"
#include "tuple.h"


namespace bertrand {
namespace py {


//////////////////////////////
////    EXPONENTIATION    ////
//////////////////////////////


/* C++ has no `**` operator, so `pow()` must be used instead. */


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename T, typename U>
inline Object pow(T&& base, U&& exp) {
    PyObject* result = PyNumber_Power(
        detail::object_or_cast(std::forward<T>(base)).ptr(),
        detail::object_or_cast(std::forward<U>(exp)).ptr(),
        Py_None
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `base **= exp` (in-place exponentiation). */
template <typename T, typename U>
inline Object inplace_pow(T&& base, U&& exp) {
    PyObject* result = PyNumber_InPlacePower(
        detail::object_or_cast(std::forward<T>(base)).ptr(),
        detail::object_or_cast(std::forward<U>(exp)).ptr(),
        Py_None
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <typename T, typename U, typename V>
inline Object pow(T&& base, U&& exp, V&& mod) {
    PyObject* result = PyNumber_Power(
        detail::object_or_cast(std::forward<T>(base)).ptr(),
        detail::object_or_cast(std::forward<U>(exp)).ptr(),
        detail::object_or_cast(std::forward<V>(mod)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* In-place modular exponentation.  Not reachable in Python, but implemented here for
completeness. */
template <typename T, typename U, typename V>
inline Object inplace_pow(T&& base, U&& exp, V&& mod) {
    PyObject* result = PyNumber_InPlacePower(
        detail::object_or_cast(std::forward<T>(base)).ptr(),
        detail::object_or_cast(std::forward<U>(exp)).ptr(),
        detail::object_or_cast(std::forward<V>(mod)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


// TODO: sqrt(), log()


/////////////////////////////////////
////    MATRIX MULTIPLICATION    ////
/////////////////////////////////////


/* Python has recently added the `@` operator for matrix multiplication.  This is not
 * commonly used and has no C++ equivalent, but we expose it here for completeness. */


/* Equivalent to Python `a @ b` (matrix multiplication). */
template <typename T, typename U>
inline Object matmul(T&& a, U&& b) {
    PyObject* result = PyNumber_MatrixMultiply(
        detail::object_or_cast(std::forward<T>(a)).ptr(),
        detail::object_or_cast(std::forward<U>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `a @= b` (in-place matrix multiplication). */
template <typename T, typename U>
inline Object inplace_matmul(T&& a, U&& b) {
    PyObject* result = PyNumber_InPlaceMatrixMultiply(
        detail::object_or_cast(std::forward<T>(a)).ptr(),
        detail::object_or_cast(std::forward<U>(b)).ptr()
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
 *      the standard `/`, except that it coerces C++ integers to doubles before
 *      performing the division, matching Python's behavior.
 *  floordiv(): Equivalent to Python `a // b` (floor division).  C++ does not have an
 *      equivalent operator, and its `/` operator always rounds towards zero rather
 *      than negative infinity like in Python.  This function always rounds towards
 *      negative infinity, and converts doubles into integers before applying the
 *      division.
 *  c_div(): Equivalent to C++ `a / b` (C++ division).  Rather than making C++ act like
 *      Python, this function does the opposite.  Python integers will be truncated
 *      towards zero, without affecting any other types.
 */


/* Equivalent to Python `a // b` (floor division). */
template <typename T, typename U>
inline Object floordiv(T&& obj, U&& divisor) {
    PyObject* result = PyNumber_FloorDivide(
        detail::object_or_cast(std::forward<T>(obj)).ptr(),
        detail::object_or_cast(std::forward<U>(divisor)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `a //= b` (in-place floor division). */
template <typename T, typename U>
inline Object inplace_floordiv(T&& obj, U&& divisor) {
    PyObject* result = PyNumber_InPlaceFloorDivide(
        detail::object_or_cast(std::forward<T>(obj)).ptr(),
        detail::object_or_cast(std::forward<U>(divisor)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


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



/* Equivalent to Python `divmod(a, b)`. */
template <typename T, typename U>
inline Tuple divmod(T&& a, U&& b) {
    PyObject* result = PyNumber_Divmod(
        detail::object_or_cast(std::forward<T>(a)).ptr(),
        detail::object_or_cast(std::forward<U>(b)).ptr()
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
