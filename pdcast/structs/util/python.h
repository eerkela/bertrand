// include guard: BERTRAND_STRUCTS_UTIL_PYTHON_H
#ifndef BERTRAND_STRUCTS_UTIL_PYTHON_H
#define BERTRAND_STRUCTS_UTIL_PYTHON_H

#include <cstddef>  // size_t
#include <functional>  // std::hash, std::less, std::plus, etc.
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


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


/* Apply a `<` comparison between two C++ or Python objects. */
template <typename LHS, typename RHS>
bool lt(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        int val = PyObject_RichCompareBool(lhs, rhs, Py_LT);
        if (val == -1 && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        return static_cast<bool>(val);
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot compare PyObject* pointer (LHS) to C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot compare C++ object (LHS) to PyObject* pointer (RHS)"
        );
    } else {
        return lhs < rhs;
    }
}


/* Apply a `<=` comparison between two C++ or Python objects. */
template <typename LHS, typename RHS>
bool le(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        int val = PyObject_RichCompareBool(lhs, rhs, Py_LE);
        if (val == -1 && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        return static_cast<bool>(val);
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot compare PyObject* pointer (LHS) to C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot compare C++ object (LHS) to PyObject* pointer (RHS)"
        );
    } else {
        return lhs <= rhs;
    }
}


/* Apply an `==` comparison between two C++ or Python objects. */
template <typename LHS, typename RHS>
bool eq(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        int val = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
        if (val == -1 && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        return static_cast<bool>(val);
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot compare PyObject* pointer (LHS) to C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot compare C++ object (LHS) to PyObject* pointer (RHS)"
        );
    } else {
        return lhs == rhs;
    }
}


/* Apply a `!=` comparison between two C++ or Python objects. */
template <typename LHS, typename RHS>
bool ne(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        int val = PyObject_RichCompareBool(lhs, rhs, Py_NE);
        if (val == -1 && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        return static_cast<bool>(val);
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot compare PyObject* pointer (LHS) to C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot compare C++ object (LHS) to PyObject* pointer (RHS)"
        );
    } else {
        return lhs != rhs;
    }
}


/* Apply a `>=` comparison between two C++ or Python objects. */
template <typename LHS, typename RHS>
bool ge(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        int val = PyObject_RichCompareBool(lhs, rhs, Py_GE);
        if (val == -1 && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        return static_cast<bool>(val);
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot compare PyObject* pointer (LHS) to C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot compare C++ object (LHS) to PyObject* pointer (RHS)"
        );
    } else {
        return lhs >= rhs;
    }
}


/* Apply a `>` comparison between two C++ or Python objects. */
template <typename LHS, typename RHS>
bool gt(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        int val = PyObject_RichCompareBool(lhs, rhs, Py_GT);
        if (val == -1 && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        return static_cast<bool>(val);
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot compare PyObject* pointer (LHS) to C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot compare C++ object (LHS) to PyObject* pointer (RHS)"
        );
    } else {
        return lhs > rhs;
    }
}


////////////////////
////    MATH    ////
////////////////////


/* Apply a `+` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto plus(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_Add(lhs, rhs);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot add PyObject* pointer (LHS) to C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot add C++ object (LHS) to PyObject* pointer (RHS)"
        );
    } else {
        return lhs + rhs;
    }
}


/* Apply a `-` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto minus(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_Subtract(lhs, rhs);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot subtract PyObject* pointer (LHS) from C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot subtract C++ object (LHS) from PyObject* pointer (RHS)"
        );
    } else {
        return lhs - rhs;
    }
}


/* Apply a `*` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto multiply(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_Multiply(lhs, rhs);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot multiply PyObject* pointer (LHS) by C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot multiply C++ object (LHS) by PyObject* pointer (RHS)"
        );
    } else {
        return lhs * rhs;
    }
}


/* Apply a `**` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto power(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_Power(lhs, rhs, Py_None);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot raise PyObject* pointer (LHS) to C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot raise C++ object (LHS) to PyObject* pointer (RHS)"
        );
    } else {
        return lhs ** rhs;
    }
}


/* Apply a `/` operation between two C++ or Python objects, with C++ semantics. */
template <typename LHS, typename RHS>
auto divide(const LHS& lhs, const RHS& rhs) {
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

            // convenience func for raising errors during PyObject* initialization
            auto raise = [&result]() {
                Py_DECREF(result);
                throw catch_python<type_error>();  // raises most recent Python error
            };

            // check result < 0
            PyObject* zero = PyLong_FromLong(0);
            if (zero == nullptr) raise();
            int comp = PyObject_RichCompareBool(result, zero, Py_LT);
            Py_DECREF(zero);
            if (comp == -1 && PyErr_Occurred()) raise();
            if (comp == 1) {
                // if result < 0, check remainder != 0
                PyObject* remainder = PyNumber_Remainder(lhs, rhs);
                if (remainder == nullptr) raise();
                comp = PyObject_IsTrue(remainder);
                Py_DECREF(remainder);
                if (comp == -1 && PyErr_Occurred()) raise();
                if (comp == 1) {
                    PyObject* one = PyLong_FromLong(1);
                    if (one == nullptr) raise();
                    PyObject* corrected = PyNumber_Add(result, one);
                    Py_DECREF(result);
                    Py_DECREF(one);
                    if (corrected == nullptr) {
                        throw catch_python<type_error>();
                    }
                    return corrected;  // new reference
                }
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

    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot divide PyObject* pointer (LHS) by C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot divide C++ object (LHS) by PyObject* pointer (RHS)"
        );
    } else {
        return lhs / rhs;
    }
}


/* Apply a `%` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto modulo(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        // NOTE: C++ modulus operator always retains the sign of the numerator, whereas
        // Python retains the sign of the denominator.
        PyObject* zero = PyLong_FromLong(0);
        if (zero == nullptr) throw catch_python<type_error>();
        int neg_lhs = PyObject_RichCompareBool(lhs, PyLong_FromLong(0), Py_LT);
        if (neg_lhs == -1 && PyErr_Occurred()) {
            Py_DECREF(zero);
            throw catch_python<type_error>();
        }
        int neg_rhs = PyObject_RichCompareBool(rhs, PyLong_FromLong(0), Py_LT);
        Py_DECREF(zero);
        if (neg_rhs == -1 && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }

        // conditional tree based on signs of numerator/denominator
        PyObject* result;
        if (neg_lhs) {
            if (neg_rhs) {  // a % b
                result = PyNumber_Remainder(lhs, rhs);
                if (result == nullptr) throw catch_python<type_error>();
            } else {  // -(-a % b)
                PyObject* a = PyNumber_Negative(lhs);
                if (a == nullptr) throw catch_python<type_error>();
                PyObject* c = PyNumber_Remainder(a, rhs);
                Py_DECREF(a);
                if (c == nullptr) throw catch_python<type_error>();
                result = PyNumber_Negative(c);
                Py_DECREF(c);
                if (result == nullptr) throw catch_python<type_error>();
            }
        } else {
            if (neg_rhs) {  // a % -b
                PyObject* b = PyNumber_Negative(rhs);
                if (b == nullptr) throw catch_python<type_error>();
                result = PyNumber_Remainder(lhs, b);
                Py_DECREF(b);
                if (result == nullptr) throw catch_python<type_error>();
            } else {  // a % b
                result = PyNumber_Remainder(lhs, rhs);
                if (result == nullptr) throw catch_python<type_error>();
            }
        }
        return result;  // new reference

    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot take modulus of PyObject* pointer (LHS) by C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot take modulus of C++ object (LHS) by PyObject* pointer (RHS)"
        );
    } else {
        return lhs % rhs;
    }
}


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


/* Get the absolute value of a C++ or Python object. */
template <typename T>
auto abs(const T& x) {
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
auto invert(const T& x) {
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
auto neg(const T& x) {
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
auto pos(const T& x) {
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
auto len(const T& x) {
    if constexpr (is_pyobject<T>) {
        PyObject* val = PyObject_Length(x);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else {
        return x.size();
    }
}


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


/* Apply a bitwise `&` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto bit_and(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_And(lhs, rhs);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot bitwise-and PyObject* pointer (LHS) with C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot bitwise-and C++ object (LHS) with PyObject* pointer (RHS)"
        );
    } else {
        return lhs & rhs;
    }
}


/* Apply a bitwise `|` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto bit_or(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_Or(lhs, rhs);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot bitwise-or PyObject* pointer (LHS) with C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot bitwise-or C++ object (LHS) with PyObject* pointer (RHS)"
        );
    } else {
        return lhs | rhs;
    }
}


/* Apply a bitwise `^` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto bit_xor(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_Xor(lhs, rhs);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot bitwise-xor PyObject* pointer (LHS) with C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot bitwise-xor C++ object (LHS) with PyObject* pointer (RHS)"
        );
    } else {
        return lhs ^ rhs;
    }
}


/* Apply a bitwise `<<` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto lshift(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_Lshift(lhs, rhs);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot left-shift PyObject* pointer (LHS) by C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot left-shift C++ object (LHS) by PyObject* pointer (RHS)"
        );
    } else {
        return lhs << rhs;
    }
}


/* Apply a bitwise `>>` operation between two C++ or Python objects. */
template <typename LHS, typename RHS>
auto rshift(const LHS& lhs, const RHS& rhs) {
    if constexpr (is_pyobject<LHS> && is_pyobject<RHS>) {
        PyObject* val = PyNumber_Rshift(lhs, rhs);
        if (val == nullptr) {
            throw catch_python<type_error>();
        }
        return val;  // new reference
    } else if constexpr (is_pyobject<LHS>) {
        throw std::logic_error(
            "cannot right-shift PyObject* pointer (LHS) by C++ object (RHS)"
        );
    } else if constexpr (is_pyobject<RHS>) {
        throw std::logic_error(
            "cannot right-shift C++ object (LHS) by PyObject* pointer (RHS)"
        );
    } else {
        return lhs >> rhs;
    }
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
