// include guard: BERTRAND_STRUCTS_UTIL_PYTHON_H
#ifndef BERTRAND_STRUCTS_UTIL_PYTHON_H
#define BERTRAND_STRUCTS_UTIL_PYTHON_H

#include <cstddef>  // size_t
#include <functional>  // std::hash, std::less, std::plus, etc.
#include <optional>  // std::optional<>
#include <stdexcept>  // std::out_of_range, std::logic_error
#include <type_traits>  // std::is_convertible_v<>, std::remove_cv_t<>, etc.
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python
#include "iter.h"  // iter(), PyIterator


#include <limits>  // std::numeric_limits<>


/* NOTE: This file contains a collection of helper classes for interacting with the
 * Python C API using C++ RAII principles.  This allows automated handling of reference
 * counts and other memory management concerns, and simplifies overall communication
 * between C++ and Python.
 */


/* Specializations for C++ standard library functors using the Python C API. */
namespace std {


    /* Hash function for PyObject* pointers. */
    template<>
    struct hash<PyObject*> {
        inline size_t operator()(PyObject* obj) const {
            using namespace bertrand::structs::util;
            Py_hash_t val = PyObject_Hash(obj);
            if (val == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();  // propagate error
            }
            return static_cast<size_t>(val);
        }
    };
    


}  // namespace std


namespace bertrand {
namespace structs {
namespace util {


/* Utilities for applying C++/Python operators agnostically across types. */
namespace detail {


    /* Try to convert a C++ argument into an equivalent Python object. */
    template <typename T>
    PyObject* as_pyobject(T&& obj) {
        // object must be a basic type
        static constexpr bool is_bool = std::is_same_v<std::decay_t<T>, bool>;
        static constexpr bool is_integer = std::is_integral_v<std::decay_t<T>>;
        static constexpr bool is_float = std::is_floating_point_v<std::decay_t<T>>;
        static_assert(
            is_bool || is_integer || is_float,
            "cannot convert C++ object to PyObject* pointer"
        );

        // boolean -> Python bool
        if constexpr (is_bool) {
            return Py_NewRef(obj ? Py_True : Py_False);
        }

        // integer -> Python int
        else if constexpr (is_integer) {
            if constexpr (std::is_unsigned_v<T>) {
                PyObject* result = PyLong_FromUnsignedLongLong(obj);
                if (result == nullptr) {
                    throw catch_python<type_error>();
                }
                return result;
            } else {
                PyObject* result = PyLong_FromLongLong(obj);
                if (result == nullptr) {
                    throw catch_python<type_error>();
                }
                return result;
            }
        }

        // float -> Python float
        else {
            PyObject* result = PyFloat_FromDouble(obj);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;
        }
    }


    /* Wrap a pure C++/Python operation to allow mixed input. */
    template <typename LHS, typename RHS, typename F>
    auto wrap(F func, LHS lhs, RHS rhs) {
        // case 1: both arguments are Python objects
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            return func(lhs, rhs);
        }

        // case 2: left argument is Python object
        else if constexpr (is_pyobject<LHS>) {
            PyObject* b = as_pyobject(rhs);
            try {
                auto result = func(lhs, b);
                Py_DECREF(b);
                return result;
            } catch (...) {
                Py_DECREF(b);
                throw;
            }
        }

        // case 3: right argument is Python object
        else if constexpr (is_pyobject<RHS>) {
            PyObject* a = as_pyobject(lhs);
            try {
                auto result = func(a, rhs);
                Py_DECREF(a);
                return result;
            } catch (...) {
                Py_DECREF(a);
                throw;
            }
        }

        // case 4: neither argument is Python object
        else {
            return func(lhs, rhs);
        }
    }


}  // namespace detail


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


/* Get the absolute value of a C++ or Python object. */
template <typename T>
inline auto abs(const T& x) {
    if constexpr (is_pyobject<T>) {
        PyObject* val = PyNumber_Absolute(x);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else {
        return std::abs(x);
    }
}


/* Get the (~)bitwise inverse of a C++ or Python object. */
template <typename T>
inline auto invert(const T& x) {
    if constexpr (is_pyobject<T>) {
        PyObject* val = PyNumber_Invert(x);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else {
        return ~x;
    }
}


/* Get the (-)negation of a C++ or Python object. */
template <typename T>
inline auto neg(const T& x) {
    if constexpr (is_pyobject<T>) {
        PyObject* val = PyNumber_Negative(x);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else {
        return -x;
    }
}


/* Get the (+)positive value of a C++ or Python object. */
template <typename T>
inline auto pos(const T& x) {
    if constexpr (is_pyobject<T>) {
        PyObject* val = PyNumber_Positive(x);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else {
        return +x;
    }
}


/* Get the length of a C++ or Python object. */
template <typename T>
inline std::optional<size_t> len(const T& x) {
    // check for x.size()
    if constexpr (ContainerTraits<T>::has_size) {
        return std::make_optional(x.size());
    } 

    // check for PyObject_Length()
    else if constexpr (util::is_pyobject<T>) {
        if (PyObject_HasAttrString(x, "__len__")) {
            Py_ssize_t size = PyObject_Length(x);
            if (size == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return std::make_optional(static_cast<size_t>(size));
        } else {
            return std::nullopt;
        }
    }

    return std::nullopt;
}


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


/* Apply a bitwise `&` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_and(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_And(a, b);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a & b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a bitwise `|` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_or(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Or(a, b);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a | b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a bitwise `^` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_xor(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Xor(a, b);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a ^ b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a bitwise `<<` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto lshift(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Lshift(a, b);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a << b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a bitwise `>>` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto rshift(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Rshift(a, b);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a >> b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


/* Apply a `<` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool lt(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_LT);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return a < b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `<=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool le(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_LE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return a <= b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply an `==` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool eq(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_EQ);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return a == b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `!=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool ne(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_NE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return a != b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `>=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool ge(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_GE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return a >= b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `>` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool gt(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_GT);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return a > b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


////////////////////
////    MATH    ////
////////////////////


/* Apply a `+` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto plus(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Add(a, b);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a + b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `-` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto minus(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Subtract(a, b);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a - b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `*` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto multiply(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Multiply(a, b);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a * b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `**` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto power(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Power(a, b, Py_None);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return a ** b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `/` operation between any combination of C++ or Python objects,
with C++ semantics. */
template <typename LHS, typename RHS>
inline auto divide(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            // NOTE: C++ division operator truncates integers toward zero, whereas Python
            // returns a float.
            if (PyLong_Check(a) && PyLong_Check(b)) {

                // try converting to long long and dividing directly
                auto happy_path = [&a, &b]() {
                    long long x = PyLong_AsLongLong(a);
                    if (x == -1 && PyErr_Occurred()) {
                        return nullptr;  // fall back
                    }
                    long long y = PyLong_AsLongLong(b);
                    if (y == 0) {
                        throw std::runtime_error("division by zero");
                    } else if (y == -1 && PyErr_Occurred()) {
                        return nullptr;  // fall back
                    }
                    return PyLong_FromLongLong(x / y);  // new reference
                };

                // attempt happy path
                PyObject* result = happy_path();
                if (result != nullptr) {
                    return result;  // new reference
                }

                // happy path overflows - fall back to Python API
                PyErr_Clear();
                result = PyNumber_FloorDivide(a, b);
                if (result == nullptr) {
                    throw catch_python<type_error>();
                }

                // if result < 0, check remainder != 0 and correct
                try {
                    if (util::lt(result, 0)) {
                        PyObject* remainder = PyNumber_Remainder(a, b);
                        if (remainder == nullptr) throw catch_python<type_error>();
                        try {
                            bool nonzero = util::ne(remainder, 0);
                            if (nonzero) {
                                PyObject* corrected = plus(result, 1);
                                Py_DECREF(remainder);
                                Py_DECREF(result);
                                return corrected;
                            }
                        } catch (...) {
                            Py_DECREF(remainder);
                            throw;
                        }
                    }
                } catch (...) {
                    Py_DECREF(result);
                    throw;
                }

                // no correction needed
                return result;  // new reference

            // Otherwise, both operators have the same semantics
            } else {
                PyObject* result = PyNumber_TrueDivide(a, b);
                if (result == nullptr) {
                    throw catch_python<type_error>();
                }
                return result;  // new reference
            }

        } else {
            return a / b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


/* Apply a `%` operation between any combination of C++ or Python objects,
with C++ semantics. */
template <typename LHS, typename RHS>
inline auto modulo(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            // NOTE: C++ modulus operator always retains the sign of the numerator, whereas
            // Python retains the sign of the denominator.
            if (util::lt(a, 0)) {
                if (util::lt(b, 0)) {  // (a < 0, b < 0)  ===  a % b
                    PyObject* result = PyNumber_Remainder(a, b);
                    if (result == nullptr) throw catch_python<type_error>();
                    return result;
                } else {  // (a < 0, b >= 0)  ===  -(-a % b)
                    PyObject* a = util::neg(a);
                    try {
                        PyObject* c = PyNumber_Remainder(a, b);
                        if (c == nullptr) throw catch_python<type_error>();
                        try {
                            PyObject* result = util::neg(c);
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
                if (util::lt(b, 0)) {  // (a >= 0, b < 0)  ===  a % -b
                    PyObject* b = util::neg(b);
                    try {
                        PyObject* result = PyNumber_Remainder(a, b);
                        if (result == nullptr) throw catch_python<type_error>();
                        Py_DECREF(b);
                        return result;
                    } catch (...) {
                        Py_DECREF(b);
                        throw;
                    }
                } else {  // (a >= 0, b >= 0)  ===  a % b
                    PyObject* result = PyNumber_Remainder(a, b);
                    if (result == nullptr) throw catch_python<type_error>();
                    return result;
                }
            }

        } else {
            return a % b;
        }
    };

    return detail::wrap(execute, lhs, rhs);
}


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_PYTHON_H
