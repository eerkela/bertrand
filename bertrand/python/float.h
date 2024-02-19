#ifndef BERTRAND_PYTHON_FLOAT_H
#define BERTRAND_PYTHON_FLOAT_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::float_ that enables conversions from strings, similar to
Python's `float()` constructor, as well as converting math operators that account for
C++ inputs. */
class Float :
    public pybind11::float_,
    public impl::NumericOps<Float>,
    public impl::FullCompare<Float>
{
    using Base = pybind11::float_;
    using Ops = impl::NumericOps<Float>;
    using Compare = impl::FullCompare<Float>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Float from a string. */
    explicit Float(const pybind11::str& str) : Base([&str] {
        PyObject* result = PyFloat_FromString(str.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Float from a string. */
    explicit Float(const char* str) : Base([&str] {
        PyObject* string = PyUnicode_FromString(str);
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Float from a string. */
    explicit Float(const std::string& str) : Base([&str] {
        PyObject* string = PyUnicode_FromStringAndSize(str.c_str(), str.size());
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Float from a string. */
    explicit Float(const std::string_view& str) : Base([&str] {
        PyObject* string = PyUnicode_FromStringAndSize(str.data(), str.size());
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

    using Ops::operator+;
    using Ops::operator-;
    using Ops::operator*;
    using Ops::operator/;
    using Ops::operator%;
    using Ops::operator<<;
    using Ops::operator>>;
    using Ops::operator&;
    using Ops::operator|;
    using Ops::operator^;
    using Ops::operator+=;
    using Ops::operator-=;
    using Ops::operator*=;
    using Ops::operator/=;
    using Ops::operator%=;
    using Ops::operator<<=;
    using Ops::operator>>=;
    using Ops::operator&=;
    using Ops::operator|=;
    using Ops::operator^=;
};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Float)


#endif  // BERTRAND_PYTHON_FLOAT_H
