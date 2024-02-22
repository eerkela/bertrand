#ifndef BERTRAND_PYTHON_FLOAT_H
#define BERTRAND_PYTHON_FLOAT_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::float_ that enables conversions from strings, similar to
Python's `float()` constructor, as well as converting math operators that account for
C++ inputs. */
struct Float : public Object, public impl::Ops<Float> {
    static py::Type Type;
    BERTRAND_PYTHON_OPERATORS(impl::Ops<Float>)
    BERTRAND_PYTHON_CONSTRUCTORS(Object, Float, PyFloat_Check, PyNumber_Float)

    /* Default constructor.  Initializes to 0.0. */
    Float() : Object(PyFloat_FromDouble(0.0), stolen_t{}) {}

    /* Implicitly convert a C++ float into a Python float. */
    Float(double value) : Object(PyFloat_FromDouble(value), stolen_t{}) {}

    /* Construct a Float from a string. */
    explicit Float(const pybind11::str& str) {
        m_ptr = PyFloat_FromString(str.ptr());
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a Float from a string. */
    explicit Float(const char* str) {
        PyObject* string = PyUnicode_FromString(str);
        if (string == nullptr) {
            throw error_already_set();
        }
        m_ptr = PyFloat_FromString(string);
        Py_DECREF(string);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a Float from a string. */
    explicit Float(const std::string& str) {
        PyObject* string = PyUnicode_FromStringAndSize(str.c_str(), str.size());
        if (string == nullptr) {
            throw error_already_set();
        }
        m_ptr = PyFloat_FromString(string);
        Py_DECREF(string);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a Float from a string. */
    explicit Float(const std::string_view& str) {
        PyObject* string = PyUnicode_FromStringAndSize(str.data(), str.size());
        if (string == nullptr) {
            throw error_already_set();
        }
        m_ptr = PyFloat_FromString(string);
        Py_DECREF(string);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert a Python float into a C++ float. */
    inline operator double() const {
        return PyFloat_AsDouble(this->ptr());
    }

};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Float)


#endif  // BERTRAND_PYTHON_FLOAT_H
