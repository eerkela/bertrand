#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_FLOAT_H
#define BERTRAND_PYTHON_FLOAT_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::float_ that enables conversions from strings, similar to
Python's `float()` constructor, as well as converting math operators that account for
C++ inputs. */
class Float : public impl::Inherits<Object, Float> {
    using Base = impl::Inherits<Object, Float>;

    template <typename T>
    static constexpr bool constructor1 = (
        !impl::python_like<T> && (
            impl::bool_like<T> || impl::int_like<T> || impl::float_like<T>
        )
    );
    template <typename T>
    static constexpr bool constructor2 =
        !impl::python_like<T> && !constructor1<T> && std::is_convertible_v<T, double>;
    template <typename T>
    static constexpr bool constructor3 = impl::python_like<T> && !impl::float_like<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::float_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Float, PyFloat_Check)

    /* Default constructor.  Initializes to 0.0. */
    Float() : Base(PyFloat_FromDouble(0.0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert C++ booleans, integers, and floats to py::Float. */
    template <typename T> requires (constructor1<T>)
    Float(const T& value) : Base(PyFloat_FromDouble(value), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Trigger explicit C++ conversions to double. */
    template <typename T> requires (constructor2<T>)
    explicit Float(const T& value) : Float(static_cast<double>(value)) {}

    /* Implicitly convert Python booleans and integers to py::Float. */
    template <typename T> requires (constructor3<T>)
    Float(const T& value) : Base(PyNumber_Float(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a string literal into a py::Float. */
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

    /* Explicitly convert a std::string into a py::Float. */
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

    /* Explicitly convert a std::string_view into a py::Float. */
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

    /* Explicitly convert a Python string into a py::Float. */
    template <typename T> requires (impl::python_like<T> && impl::str_like<T>)
    explicit Float(const T& str);

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a Python float into a C++ float. */
    inline operator double() const {
        return PyFloat_AS_DOUBLE(this->ptr());
    }

};


namespace impl {

///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


// TODO: include __abs__

template <>
struct __pos__<Float> : Returns<Float> {};
template <>
struct __neg__<Float> : Returns<Float> {};
template <>
struct __invert__<Float> : Returns<Float> {};


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


template <>
struct __lt__<Float, Object> : Returns<bool> {};
template <bool_like T>
struct __lt__<Float, T> : Returns<bool> {};
template <int_like T>
struct __lt__<Float, T> : Returns<bool> {};
template <float_like T>
struct __lt__<Float, T> : Returns<bool> {};

template <>
struct __le__<Float, Object> : Returns<bool> {};
template <bool_like T>
struct __le__<Float, T> : Returns<bool> {};
template <int_like T>
struct __le__<Float, T> : Returns<bool> {};
template <float_like T>
struct __le__<Float, T> : Returns<bool> {};

template <>
struct __ge__<Float, Object> : Returns<bool> {};
template <bool_like T>
struct __ge__<Float, T> : Returns<bool> {};
template <int_like T>
struct __ge__<Float, T> : Returns<bool> {};
template <float_like T>
struct __ge__<Float, T> : Returns<bool> {};

template <>
struct __gt__<Float, Object> : Returns<bool> {};
template <bool_like T>
struct __gt__<Float, T> : Returns<bool> {};
template <int_like T>
struct __gt__<Float, T> : Returns<bool> {};
template <float_like T>
struct __gt__<Float, T> : Returns<bool> {};


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


template <>
struct __add__<Float, Object> : Returns<Object> {};
template <bool_like T>
struct __add__<Float, T> : Returns<Float> {};
template <int_like T>
struct __add__<Float, T> : Returns<Float> {};
template <float_like T>
struct __add__<Float, T> : Returns<Float> {};
template <complex_like T>
struct __add__<Float, T> : Returns<Complex> {};

template <>
struct __sub__<Float, Object> : Returns<Object> {};
template <bool_like T>
struct __sub__<Float, T> : Returns<Float> {};
template <int_like T>
struct __sub__<Float, T> : Returns<Float> {};
template <float_like T>
struct __sub__<Float, T> : Returns<Float> {};
template <complex_like T>
struct __sub__<Float, T> : Returns<Complex> {};

template <>
struct __mul__<Float, Object> : Returns<Object> {};
template <bool_like T>
struct __mul__<Float, T> : Returns<Float> {};
template <int_like T>
struct __mul__<Float, T> : Returns<Float> {};
template <float_like T>
struct __mul__<Float, T> : Returns<Float> {};
template <complex_like T>
struct __mul__<Float, T> : Returns<Complex> {};

template <>
struct __truediv__<Float, Object> : Returns<Object> {};
template <bool_like T>
struct __truediv__<Float, T> : Returns<Float> {};
template <int_like T>
struct __truediv__<Float, T> : Returns<Float> {};
template <float_like T>
struct __truediv__<Float, T> : Returns<Float> {};
template <complex_like T>
struct __truediv__<Float, T> : Returns<Complex> {};

template <>
struct __mod__<Float, Object> : Returns<Object> {};
template <bool_like T>
struct __mod__<Float, T> : Returns<Float> {};
template <int_like T>
struct __mod__<Float, T> : Returns<Float> {};
template <float_like T>
struct __mod__<Float, T> : Returns<Float> {};
// template <complex_like T>    <-- Disabled in Python
// struct __mod__<Float, T> : Returns<Complex> {};


/////////////////////////////////
////    INPLACE OPERATORS    ////
/////////////////////////////////


template <bool_like T>
struct __iadd__<Float, T> : Returns<Float> {};
template <int_like T>
struct __iadd__<Float, T> : Returns<Float> {};
template <float_like T>
struct __iadd__<Float, T> : Returns<Float> {};

template <bool_like T>
struct __isub__<Float, T> : Returns<Float> {};
template <int_like T>
struct __isub__<Float, T> : Returns<Float> {};
template <float_like T>
struct __isub__<Float, T> : Returns<Float> {};

template <bool_like T>
struct __imul__<Float, T> : Returns<Float> {};
template <int_like T>
struct __imul__<Float, T> : Returns<Float> {};
template <float_like T>
struct __imul__<Float, T> : Returns<Float> {};

template <bool_like T>
struct __itruediv__<Float, T> : Returns<Float> {};
template <int_like T>
struct __itruediv__<Float, T> : Returns<Float> {};
template <float_like T>
struct __itruediv__<Float, T> : Returns<Float> {};

template <bool_like T>
struct __imod__<Float, T> : Returns<Float> {};
template <int_like T>
struct __imod__<Float, T> : Returns<Float> {};
template <float_like T>
struct __imod__<Float, T> : Returns<Float> {};


}  // namespace impl

}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Float)


#endif  // BERTRAND_PYTHON_FLOAT_H
