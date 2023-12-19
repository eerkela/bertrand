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
#include "container.h"  // Object<>
#include "except.h"  // catch_python, TypeError
#include "iter.h"  // iter(), PyIterator


/* NOTE: This file contains a collection of helper functions for applying basic
 * operators to both C++ and Python objects.  This is useful for writing generic
 * algorithms that can accept either C++ or Python objects as arguments.
 */


namespace bertrand {


namespace util {


    // TODO: add string -> PyUnicode?

    /* Attempt to convert a C++ argument into an equivalent Python object. */
    template <typename T>
    PyObject* as_pyobject(T&& obj) {
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
                    throw catch_python();
                }
                return result;
            } else {
                PyObject* result = PyLong_FromLongLong(obj);
                if (result == nullptr) {
                    throw catch_python();
                }
                return result;
            }
        }

        // float -> Python float
        else {
            PyObject* result = PyFloat_FromDouble(obj);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;
        }
    }

    /* Wrap a pure C++/Python operation to allow mixed C++/Python arguments. */
    template <typename LHS, typename RHS, typename F>
    auto allow_mixed_args(F func, LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            return func(lhs, rhs);
        }

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

        else {
            return func(lhs, rhs);
        }
    }

    /* A trait that determines which specialization of repr() is appropriate for a
    given type. */
    template <typename T>
    class ReprTraits {
        using True = std::true_type;
        using False = std::false_type;
        using Stream = std::ostringstream;

        enum class Use {
            python,
            to_string,
            stream,
            iterable,
            type_id
        };

        /* Check if the templated type is a Python object. */
        template<typename U>
        static auto _python(U u) -> decltype(
            PyObject_Repr(std::forward<U>(u)), True{}
        );
        static auto _python(...) -> False;

        /* Check if the templated type is a valid input to std::to_string. */
        template<typename U>
        static auto _to_string(U u) -> decltype(
            std::to_string(std::forward<U>(u)), True{}
        );
        static auto _to_string(...) -> False;

        /* Check if the templated type supports std::ostringstream insertion. */
        template<typename U>
        static auto _streamable(U u) -> decltype(
            std::declval<Stream&>() << std::forward<U>(u), True{}
        );
        static auto _streamable(...) -> False;

        /* Check if the templated type is iterable. */
        template<typename U>
        static auto _iterable(U u) -> decltype(
            std::begin(std::forward<U>(u)), std::end(std::forward<U>(u)), True{}
        );
        static auto _iterable(...) -> False;

        /* Determine the Repr() overload to use for objects of the templated type. */
        static constexpr Use category = [] {
            if constexpr (decltype(_python(std::declval<T>()))::value) {
                return Use::python;
            } else if constexpr (decltype(_to_string(std::declval<T>()))::value) {
                return Use::to_string;
            } else if constexpr (decltype(_streamable(std::declval<T>()))::value) {
                return Use::stream;
            } else if constexpr (decltype(_iterable(std::declval<T>()))::value) {
                return Use::iterable;
            } else {
                return Use::type_id;
            }
        }();

    public:
        static constexpr bool python = (category == Use::python);
        static constexpr bool streamable = (category == Use::stream);
        static constexpr bool to_string = (category == Use::to_string);
        static constexpr bool iterable = (category == Use::iterable);
        static constexpr bool type_id = (category == Use::type_id);
    };

}


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


/* Get the absolute value of a C++ or Python object. */
template <typename T>
inline auto abs(const T& x) {
    if constexpr (is_pyobject<T>) {
        PyObject* val = PyNumber_Absolute(x);
        if (val == nullptr) {
            throw catch_python();
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
            throw catch_python();
        }
        return val;  // new reference
    } else {
        return ~x;
    }
}


/* Get the (-)negation of a C++ or Python object. */
template <typename T>
inline auto negative(const T& x) {
    if constexpr (is_pyobject<T>) {
        PyObject* val = PyNumber_Negative(x);
        if (val == nullptr) {
            throw catch_python();
        }
        return val;  // new reference
    } else {
        return -x;
    }
}


/* Get the (+)positive value of a C++ or Python object. */
template <typename T>
inline auto positive(const T& x) {
    if constexpr (is_pyobject<T>) {
        PyObject* val = PyNumber_Positive(x);
        if (val == nullptr) {
            throw catch_python();
        }
        return val;  // new reference
    } else {
        return +x;
    }
}


/* Get the hash of a C++ or Python object. */
template <typename T>
inline size_t hash(const T& key) {
    if constexpr (is_pyobject<T>) {
        Py_ssize_t result;

        // ASCII string special case (taken from CPython source)
        // see: cpython/objects/setobject.c; set_contains_key()
        if (!PyUnicode_CheckExact(key) ||
            (result = _PyASCIIObject_CAST(key)->hash) == -1
        ) {
            // fall back to PyObject_Hash()
            result = PyObject_Hash(key);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
        }

        return static_cast<size_t>(result);

    } else {
        return std::hash<T>()(key);
    }
}


/* Get the length of a C++ or Python object. */
template <typename T>
inline std::optional<size_t> len(const T& x) {
    if constexpr (util::ContainerTraits<T>::has_size) {
        return std::make_optional(x.size());
    }

    else if constexpr (is_pyobject<T>) {
        if (PyObject_HasAttrString(x, "__len__")) {
            Py_ssize_t size = PyObject_Length(x);
            if (size == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return std::make_optional(static_cast<size_t>(size));
        } else {
            return std::nullopt;
        }
    }

    return std::nullopt;
}


/* Python's repr() function is extremely useful for debugging, but there is no direct
 * equivalent for C++ objects.  This makes debugging C++ code more difficult,
 * especially when working with objects of unknown type (doubly so when Python objects
 * might be mixed in with static C++ types).
 *
 * The repr() function attempts to solve that problem by providing a single, generic
 * function that uses template specialization and SFINAE to determine the best way to
 * stringify an arbitrary object.  This allows us to use the same interface for all
 * objects (whether Python or C++), and to easily extend the functionality to new types
 * as needed.  At the moment, this can accept any object that is:
 *      - convertible to PyObject*, in which case `PyObject_Repr()` is used.
 *      - convertible to std::string, in which case `std::to_string()` is used.
 *      - streamable into a std::ostringstream, in which case `operator<<` is used.
 *      - iterable, in which case each element is recursively unpacked according to the
 *        same rules as listed here.
 *      - none of the above, in which case the raw type name is returned using
 *        `typeid().name()`.
 */


/* Get a string representation of a Python object using PyObject_Repr(). */
template <typename T, std::enable_if_t<util::ReprTraits<T>::python, int> = 0>
std::string repr(const T& obj) {
    if (obj == nullptr) {
        return std::string("NULL");
    }
    PyObject* py_repr = PyObject_Repr(obj);
    if (py_repr == nullptr) {
        throw catch_python();
    }
    Py_ssize_t size;
    const char* c_repr = PyUnicode_AsUTF8AndSize(py_repr, &size);
    Py_DECREF(py_repr);
    if (c_repr == nullptr) {
        throw catch_python();
    }
    return std::string(c_repr, static_cast<size_t>(size));
}


/* Get a string representation of a C++ object using `std::to_string()`. */
template <typename T, std::enable_if_t<util::ReprTraits<T>::to_string, int> = 0>
std::string repr(const T& obj) {
    return std::to_string(obj);
}


/* Get a string representation of a C++ object by streaming it into a
`std::ostringstream`. */
template <typename T, std::enable_if_t<util::ReprTraits<T>::streamable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << obj;
    return stream.str();
}


/* Get a string representation of an iterable C++ object by recursively unpacking
it. */
template <typename T, std::enable_if_t<util::ReprTraits<T>::iterable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << '[';
    for (auto iter = std::begin(obj); iter != std::end(obj);) {
        stream << repr(*iter);
        if (++iter != std::end(obj)) {
            stream << ", ";
        }
    }
    stream << ']';
    return stream.str();
}


/* Get a string representation of an arbitrary C++ object by getting its mangled type
name.  NOTE: this is the default implementation if no specialization can be found. */
template <typename T, std::enable_if_t<util::ReprTraits<T>::type_id, int> = 0>
std::string repr(const T& obj) {
    return std::string(typeid(obj).name());
}


/* NOTE: A generic iter() function, which will iterate over both Python and C++
 * containers, is provided in the iter.h header alongside this file.  It is
 * omitted here for brevity.
 */


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


/* Apply a `<` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool lt(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_LT);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        } else {
            return a < b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a `<=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool le(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_LE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        } else {
            return a <= b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply an `==` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool eq(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            // fast path: check pointer equality
            if (a == b) {
                return true;
            }

            // fast path: use string comparison if both objects are strings
            if (PyUnicode_CheckExact(a) && PyUnicode_CheckExact(b)) {
                // ASCII string special case (taken from CPython source)
                // see: cpython/Objects/unicodeobject.c; unicode_compare_eq()
                if (PyUnicode_IS_ASCII(a) && PyUnicode_IS_ASCII(b)) {
                    Py_ssize_t length = PyUnicode_GET_LENGTH(a);
                    if (PyUnicode_GET_LENGTH(b) != length) {
                        return false;
                    }
                    if (length == 0) {
                        return true;
                    }
                    const void* data_a = PyUnicode_DATA(a);
                    const void* data_b = PyUnicode_DATA(b);
                    return std::memcmp(data_a, data_b, length) == 0;
                }

                // fall back to normal string comparison
                int result = PyUnicode_Compare(a, b);
                if (result == -1 && PyErr_Occurred()) {
                    throw catch_python();
                }
                return static_cast<bool>(result == 0);
            }

            // fall back to normal == comparison
            int result = PyObject_RichCompareBool(a, b, Py_EQ);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        } else {
            return a == b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a `!=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool ne(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_NE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        } else {
            return a != b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a `>=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool ge(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_GE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        } else {
            return a >= b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a `>` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool gt(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            int result = PyObject_RichCompareBool(a, b, Py_GT);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        } else {
            return a > b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


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


/* Apply a `+` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto plus(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Add(a, b);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a + b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a `-` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto minus(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Subtract(a, b);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a - b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a `*` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto multiply(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Multiply(a, b);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a * b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a `**` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto power(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Power(a, b, Py_None);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a ** b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
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
                    throw catch_python();
                }

                // if result < 0, check remainder != 0 and correct
                try {
                    if (lt(result, 0)) {
                        PyObject* remainder = PyNumber_Remainder(a, b);
                        if (remainder == nullptr) {
                            throw catch_python();
                        }
                        try {
                            bool nonzero = ne(remainder, 0);
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
                    throw catch_python();
                }
                return result;  // new reference
            }

        } else {
            return a / b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a `%` operation between any combination of C++ or Python objects,
with C++ semantics. */
template <typename LHS, typename RHS>
inline auto modulo(const LHS& lhs, const RHS& rhs) {
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


/* Apply a bitwise `&` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_and(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_And(a, b);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a & b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a bitwise `|` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_or(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Or(a, b);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a | b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a bitwise `^` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_xor(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Xor(a, b);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a ^ b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a bitwise `<<` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto lshift(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Lshift(a, b);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a << b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


/* Apply a bitwise `>>` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto rshift(const LHS& lhs, const RHS& rhs) {
    auto execute = [](auto a, auto b) {
        if constexpr (is_pyobject<decltype(a)> && is_pyobject<decltype(b)>) {
            PyObject* result = PyNumber_Rshift(a, b);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;  // new reference
        } else {
            return a >> b;
        }
    };

    return util::allow_mixed_args(execute, lhs, rhs);
}


}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_OPS_H
