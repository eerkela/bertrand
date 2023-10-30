// include guard: BERTRAND_STRUCTS_UTIL_PYTHON_H
#ifndef BERTRAND_STRUCTS_UTIL_PYTHON_H
#define BERTRAND_STRUCTS_UTIL_PYTHON_H

#include <cstddef>  // size_t
#include <functional>  // std::hash, std::less, std::plus, etc.
#include <Python.h>  // CPython API
#include "except.h"  // catch_python
#include "iter.h"  // iter(), PyIterator


/* NOTE: This file contains a collection of helper classes for interacting with the
 * Python C API using C++ RAII principles.  This allows automated handling of reference
 * counts and other memory management concerns, and simplifies overall communication
 * between C++ and Python.
 */


/* Specializations for C++ standard library functors using the Python C API. */
namespace std {

    ////////////////////
    ////    HASH    ////
    ////////////////////

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

    ///////////////////////////
    ////    COMPARISONS    ////
    ///////////////////////////

    /* < comparison for PyObject* pointers. */
    template<>
    struct less<PyObject*> {
        inline bool operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            int val = PyObject_RichCompareBool(lhs, rhs, Py_LT);
            if (val == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();  // propagate error
            }
            return static_cast<bool>(val);
        }
    };

    /* <= comparison for PyObject* pointers. */
    template<>
    struct less_equal<PyObject*> {
        inline bool operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            int val = PyObject_RichCompareBool(lhs, rhs, Py_LE);
            if (val == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();  // propagate error
            }
            return static_cast<bool>(val);
        }
    };

    /* == comparison for PyObject* pointers. */
    template<>
    struct equal_to<PyObject*> {
        inline bool operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            int val = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
            if (val == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();  // propagate error
            }
            return static_cast<bool>(val);
        }
    };

    /* != comparison for PyObject* pointers. */
    template<>
    struct not_equal_to<PyObject*> {
        inline bool operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            int val = PyObject_RichCompareBool(lhs, rhs, Py_NE);
            if (val == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();  // propagate error
            }
            return static_cast<bool>(val);
        }
    };

    /* >= comparison for PyObject* pointers. */
    template<>
    struct greater_equal<PyObject*> {
        inline bool operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            int val = PyObject_RichCompareBool(lhs, rhs, Py_GE);
            if (val == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();  // propagate error
            }
            return static_cast<bool>(val);
        }
    };

    /* > comparison for PyObject* pointers. */
    template<>
    struct greater<PyObject*> {
        inline bool operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            int val = PyObject_RichCompareBool(lhs, rhs, Py_GT);
            if (val == -1 && PyErr_Occurred()) {
                throw catch_python<type_error>();  // propagate error
            }
            return static_cast<bool>(val);
        }
    };

    ////////////////////
    ////    MATH    ////
    ////////////////////

    /* Addition for PyObject* pointers. */
    template<>
    struct plus<PyObject*> {
        inline PyObject* operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_Add(lhs, rhs);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

    /* Subtraction for PyObject* pointers. */
    template<>
    struct minus<PyObject*> {
        inline PyObject* operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_Subtract(lhs, rhs);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

    /* Multiplication for PyObject* pointers. */
    template<>
    struct multiplies<PyObject*> {
        inline PyObject* operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_Multiply(lhs, rhs);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

    /* Division for PyObject* pointers. */
    template<>
    struct divides<PyObject*> {
        inline PyObject* operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_TrueDivide(lhs, rhs);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

    /* Modulus for PyObject* pointers. */
    template<>
    struct modulus<PyObject*> {
        inline PyObject* operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_Remainder(lhs, rhs);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

    /* Negation for PyObject* pointers. */
    template<>
    struct negate<PyObject*> {
        inline PyObject* operator()(PyObject* obj) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_Negative(obj);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

    ///////////////////////
    ////    BITWISE    ////
    ///////////////////////

    /* Bitwise AND for PyObject* pointers. */
    template<>
    struct bit_and<PyObject*> {
        inline PyObject* operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_And(lhs, rhs);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

    /* Bitwise OR for PyObject* pointers. */
    template<>
    struct bit_or<PyObject*> {
        inline PyObject* operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_Or(lhs, rhs);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

    /* Bitwise XOR for PyObject* pointers. */
    template<>
    struct bit_xor<PyObject*> {
        inline PyObject* operator()(PyObject* lhs, PyObject* rhs) const {
            using namespace bertrand::structs::util;
            PyObject* val = PyNumber_Xor(lhs, rhs);
            if (val == nullptr) {
                throw catch_python<type_error>();  // propagate error
            }
            return val;
        }
    };

}  // namespace std


namespace bertrand {
namespace structs {
namespace util {


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

    /* Get the length of the sequence. */
    inline size_t size() const { return length; }

    /* Iterate over the sequence. */
    inline auto begin() const { return iter(this->sequence).begin(); }
    inline auto end() const { return iter(this->sequence).end(); }
    inline auto rbegin() const { return iter(this->sequence).rbegin(); }
    inline auto rend() const { return iter(this->sequence).rend(); }

    /* Get underlying PyObject* array. */
    inline PyObject** array() const { return PySequence_Fast_ITEMS(sequence); }

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
