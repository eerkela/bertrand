#ifndef BERTRAND_PYTHON_MATH_H
#define BERTRAND_PYTHON_MATH_H

#include "common.h"
#include "tuple.h"


namespace bertrand {
namespace py {


/////////////////////////
////    OPERATORS    ////
/////////////////////////


/* NOTE: some Python operators are not reachable in C++ simply due to language
 * limitations.  For instance, C++ has no `**`, `//`, or `@` operators, so `pow()`,
 * `floor_div()`, and `matrix_multiply()` must be used instead.  Similarly, the
 * semantics of C++'s `/` and `%` operators are different from Python's, and we defer
 * to Python's semantics in normal operation.  If necessary, the `c_div()`, `c_mod()`,
 * and `c_divmod()` functions can be used to obtain the C++ semantics.
 */


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


/* Equivalent to Python `a // b` (floor division). */
template <typename T, typename U>
inline Object floor_div(T&& obj, U&& divisor) {
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
inline Object inplace_floor_div(T&& obj, U&& divisor) {
    PyObject* result = PyNumber_InPlaceFloorDivide(
        detail::object_or_cast(std::forward<T>(obj)).ptr(),
        detail::object_or_cast(std::forward<U>(divisor)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


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


/* Equivalent to Python `a @ b` (matrix multiplication). */
template <typename T, typename U>
inline Object matrix_multiply(T&& a, U&& b) {
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
inline Object inplace_matrix_multiply(T&& a, U&& b) {
    PyObject* result = PyNumber_InPlaceMatrixMultiply(
        detail::object_or_cast(std::forward<T>(a)).ptr(),
        detail::object_or_cast(std::forward<U>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
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


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_MATH_H
