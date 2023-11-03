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
    PyObject* as_pyobject(T& obj) {
        static constexpr bool is_bool = std::is_same_v<T, bool>;
        static constexpr bool is_integer = std::is_integral_v<T>;
        static constexpr bool is_float = std::is_floating_point_v<T>;
        static_assert(
            is_bool || is_integer || is_float,
            "cannot convert C++ object to PyObject* pointer"
        );

        // booleans -> Python bool
        if constexpr (is_bool) {
            return Py_NewRef(obj ? Py_True : Py_False);
        }

        // integers -> Python int
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

        // floats -> Python float
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
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            return func(lhs, rhs);
        } else if constexpr (is_pyobject<LHS>) {
            PyObject* b = as_pyobject(rhs);
            try {
                PyObject* result = func(lhs, b);
                Py_DECREF(b);
                return result;
            } catch (...) {
                Py_DECREF(b);
                throw;
            }
        } else if constexpr (is_pyobject<RHS>) {
            PyObject* a = as_pyobject(lhs);
            try {
                PyObject* result = func(a, rhs);
                Py_DECREF(a);
                return result;
            } catch (...) {
                Py_DECREF(a);
                throw;
            }
        } else {
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


/* Pure C++/Python implementations for binary operators. */
namespace detail {


    template <typename LHS, typename RHS>
    auto bit_and(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_And(lhs, rhs);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs & rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto bit_or(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_Or(lhs, rhs);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs | rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto bit_xor(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_Xor(lhs, rhs);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs ^ rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto lshift(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_Lshift(lhs, rhs);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs << rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto rshift(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_Rshift(lhs, rhs);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs >> rhs;
        }
    }


}  // namespace detail


/* Apply a bitwise `&` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_and(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::bit_and<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a bitwise `|` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_or(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::bit_or<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a bitwise `^` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto bit_xor(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::bit_xor<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a bitwise `<<` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto lshift(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::lshift<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a bitwise `>>` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto rshift(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::rshift<const LHS&, const RHS&>, lhs, rhs);
}


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


/* Pure C++/Python implementations for comparison operators. */
namespace detail {


    template <typename LHS, typename RHS>
    inline bool lt(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            int result = PyObject_RichCompareBool(lhs, rhs, Py_LT);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return lhs < rhs;
        }
    }


    template <typename LHS, typename RHS>
    inline bool le(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            int result = PyObject_RichCompareBool(lhs, rhs, Py_LE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return lhs <= rhs;
        }
    }


    template <typename LHS, typename RHS>
    inline bool eq(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            int result = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return lhs == rhs;
        }
    }


    template <typename LHS, typename RHS>
    inline bool ne(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            int result = PyObject_RichCompareBool(lhs, rhs, Py_NE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return lhs != rhs;
        }
    }


    template <typename LHS, typename RHS>
    inline bool ge(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            int result = PyObject_RichCompareBool(lhs, rhs, Py_GE);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return lhs >= rhs;
        }
    }


    template <typename LHS, typename RHS>
    inline bool gt(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            int result = PyObject_RichCompareBool(lhs, rhs, Py_GT);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();
            }
            return static_cast<bool>(result);
        } else {
            return lhs > rhs;
        }
    }

}  // namespace detail


/* Apply a `<` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool lt(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::lt<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `<=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool le(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::le<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply an `==` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool eq(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::eq<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `!=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool ne(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::ne<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `>=` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool ge(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::ge<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `>` comparison between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline bool gt(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::gt<const LHS&, const RHS&>, lhs, rhs);
}


////////////////////
////    MATH    ////
////////////////////


/* Pure C++/Python implementations for math operators. */
namespace detail {


    template <typename LHS, typename RHS>
    auto plus(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_Add(lhs, rhs);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs + rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto minus(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_Subtract(lhs, rhs);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs - rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto multiply(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_Multiply(lhs, rhs);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs * rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto power(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            PyObject* result = PyNumber_Power(lhs, rhs, Py_None);
            if (result == nullptr) {
                throw catch_python<type_error>();
            }
            return result;  // new reference
        } else {
            return lhs ** rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto divide(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            // NOTE: C++ division operator truncates integers toward zero, whereas Python
            // returns a float.
            if (PyLong_Check(lhs) && PyLong_Check(rhs)) {

                // try converting to long long and dividing directly
                auto happy_path = [&lhs, &rhs]() {
                    long long x = PyLong_AsLongLong(lhs);
                    if (x == -1 && PyErr_Occurred()) {
                        return nullptr;  // fall back
                    }
                    long long y = PyLong_AsLongLong(rhs);
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
                result = PyNumber_FloorDivide(lhs, rhs);
                if (result == nullptr) {
                    throw catch_python<type_error>();
                }

                // if result < 0, check remainder != 0 and correct
                try {
                    if (util::lt(result, 0)) {
                        PyObject* remainder = PyNumber_Remainder(lhs, rhs);
                        if (remainder == nullptr) throw catch_python<type_error>();
                        try {
                            bool nonzero = util::ne(remainder, 0);
                            if (nonzero) {
                                PyObject* corrected = detail::wrap(
                                    detail::plus<LHS, RHS>,
                                    result,
                                    1
                                );
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
                PyObject* result = PyNumber_TrueDivide(lhs, rhs);
                if (result == nullptr) {
                    throw catch_python<type_error>();
                }
                return result;  // new reference
            }

        } else {
            return lhs / rhs;
        }
    }


    template <typename LHS, typename RHS>
    auto modulo(LHS lhs, RHS rhs) {
        if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
            // NOTE: C++ modulus operator always retains the sign of the numerator, whereas
            // Python retains the sign of the denominator.
            if (util::lt(lhs, 0)) {
                if (util::lt(rhs, 0)) {  // (a < 0, b < 0)  ===  a % b
                    PyObject* result = PyNumber_Remainder(lhs, rhs);
                    if (result == nullptr) throw catch_python<type_error>();
                    return result;
                } else {  // (a < 0, b >= 0)  ===  -(-a % b)
                    PyObject* a = util::neg(lhs);
                    try {
                        PyObject* c = PyNumber_Remainder(a, rhs);
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
                if (util::lt(rhs, 0)) {  // (a >= 0, b < 0)  ===  a % -b
                    PyObject* b = util::neg(rhs);
                    try {
                        PyObject* result = PyNumber_Remainder(lhs, b);
                        if (result == nullptr) throw catch_python<type_error>();
                        Py_DECREF(b);
                        return result;
                    } catch (...) {
                        Py_DECREF(b);
                        throw;
                    }
                } else {  // (a >= 0, b >= 0)  ===  a % b
                    PyObject* result = PyNumber_Remainder(lhs, rhs);
                    if (result == nullptr) throw catch_python<type_error>();
                    return result;
                }
            }

        } else {
            return lhs % rhs;
        }
    }


}  // namespace detail



/* Apply a `+` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto plus(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::plus<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `-` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto minus(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::minus<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `*` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto multiply(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::multiply<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `**` operation between any combination of C++ or Python objects. */
template <typename LHS, typename RHS>
inline auto power(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::power<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `/` operation between any combination of C++ or Python objects,
with C++ semantics. */
template <typename LHS, typename RHS>
inline auto divide(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::divide<const LHS&, const RHS&>, lhs, rhs);
}


/* Apply a `%` operation between any combination of C++ or Python objects,
with C++ semantics. */
template <typename LHS, typename RHS>
inline auto modulo(const LHS& lhs, const RHS& rhs) {
    return detail::wrap(detail::modulo<const LHS&, const RHS&>, lhs, rhs);
}


////////////////////////////////
////    PYTHON ITERABLES    ////
////////////////////////////////


/* NOTE: Python iterables are somewhat tricky to interact with from C++.  Reference
 * counts have to be carefully managed to avoid memory leaks, and the C API is not
 * particularly intuitive.  These helper classes simplify the interface and allow us to
 * use RAII to automatically manage reference counts.
 * 
 * PyIterator is a wrapper around a C++ iterator that allows it to be used from Python.
 * It can only be used for iterators that dereference to PyObject*, and it uses a
 * manually-defined PyTypeObject (whose name must be given as a template argument) to
 * expose the iterator to Python.  This type defines the __iter__() and __next__()
 * magic methods, which are used to implement the iterator protocol in Python.
 * 
 * PyIterable is essentially the inverse.  It represents a C++ wrapper around a Python
 * iterator that defines the __iter__() and __next__() magic methods.  The wrapper can
 * be iterated over using normal C++ syntax, and it automatically manages reference
 * counts for both the iterator itself and each element as we access them.
 * 
 * PySequence is a C++ wrapper around a Python sequence (list or tuple) that allows
 * elements to be accessed by index.  It corresponds to the PySequence_FAST() family of
 * C API functions.  Just like PyIterable, the wrapper automatically manages reference
 * counts for the sequence and its contents as they are accessed.
 */


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
class PySequence {
public:

    /* Construct a PySequence from an iterable or other sequence. */
    PySequence(PyObject* items, const char* err_msg = "could not get sequence") :
        sequence(PySequence_Fast(items, err_msg)),
        length(static_cast<size_t>(PySequence_Fast_GET_SIZE(sequence)))
    {
        if (sequence == nullptr) {
            throw catch_python<type_error>();  // propagate error
        }
    }

    /* Release the Python sequence on destruction. */
    ~PySequence() { Py_DECREF(sequence); }

    /* Iterate over the sequence. */
    inline auto begin() const { return iter(this->sequence).begin(); }
    inline auto cbegin() const { return iter(this->sequence).cbegin(); }
    inline auto end() const { return iter(this->sequence).end(); }
    inline auto cend() const { return iter(this->sequence).cend(); }
    inline auto rbegin() const { return iter(this->sequence).rbegin(); }
    inline auto crbegin() const { return iter(this->sequence).crbegin(); }
    inline auto rend() const { return iter(this->sequence).rend(); }
    inline auto crend() const { return iter(this->sequence).crend(); }

    /* Get underlying PyObject* array. */
    inline PyObject** data() const { return PySequence_Fast_ITEMS(sequence); }
    inline size_t size() const { return length; }

    /* Get the value at a particular index of the sequence. */
    inline PyObject* operator[](size_t index) const {
        if (index >= length) {
            throw std::out_of_range("index out of range");
        }
        return PySequence_Fast_GET_ITEM(sequence, index);  // borrowed reference
    }

protected:
    PyObject* sequence;
    size_t length;
};


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_PYTHON_H
