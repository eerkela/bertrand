#ifndef BERTRAND_STRUCTS_UTIL_OPS_H
#define BERTRAND_STRUCTS_UTIL_OPS_H

#include <cstddef>  // size_t
#include <cstring>  // std::memcmp
#include <functional>  // std::hash, std::less, std::plus, etc.
#include <optional>  // std::optional<>
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <typeinfo>  // typeid()
#include <type_traits>  // std::is_convertible_v<>, std::remove_cv_t<>, etc.
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python, TypeError
#include "iter.h"  // iter(), PyIterator


/* NOTE: This file contains a collection of helper functions for applying basic
 * operators polymorphically to both C++ and Python objects.  These operators can
 * even do trivial conversions from primitive C++ types to Python objects if necessary.
 */


namespace bertrand {


/* Compare elements of two containers lexicographically, checking whether all
elements of the left operand are less than or equal to their counterparts from the
right operand, with ties broken if the left operand is shorter than the right. */
template <typename LHS, typename RHS>
bool lexical_lt(const LHS& lhs, const RHS& rhs) {
    auto it_lhs = iter(lhs).forward();
    auto it_rhs = iter(rhs).forward();

    while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
        auto x = *it_lhs;
        auto y = *it_rhs;
        if (lt(x, y)) {
            return true;
        }
        if (lt(y, x)) {
            return false;
        }
        ++it_lhs;
        ++it_rhs;
    }

    return (!(it_lhs != it_lhs.end()) && it_rhs != it_rhs.end());
}


/* Compare elements of two containers lexicographically, checking whether all
elements of the left operand are less than or equal to their counterparts from the
right operand, with ties broken if the left operand is the same size or shorter
than the right. */
template <typename LHS, typename RHS>
bool lexical_le(const LHS& lhs, const RHS& rhs) {
    auto it_lhs = iter(lhs).forward();
    auto it_rhs = iter(rhs).forward();

    while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
        auto x = *it_lhs;
        auto y = *it_rhs;
        if (lt(x, y)) {
            return true;
        }
        if (lt(y, x)) {
            return false;
        }
        ++it_lhs;
        ++it_rhs;
    }

    return !(it_lhs != it_lhs.end());
}


/* Compare elements of two containers lexicographically, checking whether all
elements of the left operand are stricly equal to their counterparts from the right
operand, and that the left operand is the same length as the right. */
template <typename LHS, typename RHS>
bool lexical_eq(const LHS& lhs, const RHS& rhs) {
    if constexpr (std::is_same_v<decltype(lhs), decltype(rhs)>) {
        if (&lhs == &rhs) {
            return true;
        }
    }

    auto it_lhs = iter(lhs).forward();
    auto it_rhs = iter(rhs).forward();

    while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
        if (ne(*it_lhs, *it_rhs)) {
            return false;
        }
        ++it_lhs;
        ++it_rhs;
    }

    return (!(it_lhs != it_lhs.end()) && !(it_rhs != it_rhs.end()));
}


/* Compare elements of two containers lexicographically, checking whether all
elements of the left operand are greater than or equal to their counterparts from
the right operand, with ties broken if the left operand is the same size or longer
than the right. */
template <typename LHS, typename RHS>
bool lexical_ge(const LHS& lhs, const RHS& rhs) {
    auto it_lhs = iter(lhs).forward();
    auto it_rhs = iter(rhs).forward();

    while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
        auto x = *it_lhs;
        auto y = *it_rhs;
        if (lt(y, x)) {
            return true;
        }
        if (lt(x, y)) {
            return false;
        }
        ++it_lhs;
        ++it_rhs;
    }

    return !(it_rhs != it_rhs.end());
}


/* Compare elements of two containers lexicographically, checking whether all
elements of the left operand are greater than or equal to their counterparts from
the right operand, with ties broken if the left operand is longer than the right. */
template <typename LHS, typename RHS>
bool lexical_gt(const LHS& lhs, const RHS& rhs) {
    auto it_lhs = iter(lhs).forward();
    auto it_rhs = iter(rhs).forward();

    while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
        auto x = *it_lhs;
        auto y = *it_rhs;
        if (lt(y, x)) {
            return true;
        }
        if (lt(x, y)) {
            return false;
        }
        ++it_lhs;
        ++it_rhs;
    }

    return (!(it_rhs != it_rhs.end()) && it_lhs != it_lhs.end());
}








/* Apply a `%` operation between any combination of C++ or Python objects,
with C++ semantics. */
template <typename LHS, typename RHS>
auto modulo(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            // NOTE: C++ modulus operator always retains the sign of the numerator, whereas
            // Python retains the sign of the denominator.
            if (lt(a, 0)) {
                if (lt(b, 0)) {  // (a < 0, b < 0)  ===  a % b
                    PyObject* result = PyNumber_Remainder(a, b);
                    if (result == nullptr) {
                        throw catch_python();
                    }
                    return result;
                } else {  // (a < 0, b >= 0)  ===  -(-a % b)
                    PyObject* a = negative(a);
                    try {
                        PyObject* c = PyNumber_Remainder(a, b);
                        if (c == nullptr) {
                            throw catch_python();
                        }
                        try {
                            PyObject* result = negative(c);
                            Py_DECREF(c);
                            Py_DECREF(a);
                            return result;

                        } catch (...) {
                            Py_DECREF(c);
                            throw;
                        }
                    } catch (...) {
                        Py_DECREF(a);
                        throw;
                    }
                }
            } else {
                if (lt(b, 0)) {  // (a >= 0, b < 0)  ===  a % -b
                    PyObject* b = negative(b);
                    try {
                        PyObject* result = PyNumber_Remainder(a, b);
                        if (result == nullptr) {
                            throw catch_python();
                        }
                        Py_DECREF(b);
                        return result;
                    } catch (...) {
                        Py_DECREF(b);
                        throw;
                    }
                } else {  // (a >= 0, b >= 0)  ===  a % b
                    PyObject* result = PyNumber_Remainder(a, b);
                    if (result == nullptr) {
                        throw catch_python();
                    }
                    return result;
                }
            }

        } else {
            return a % b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_OPS_H
