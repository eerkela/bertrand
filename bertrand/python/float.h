#ifndef BERTRAND_PYTHON_INCLUDED
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
class Float : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool constructor1 = (
        !impl::is_python<T> && (
            impl::is_bool_like<T> || impl::is_int_like<T> || impl::is_float_like<T>
        )
    );
    template <typename T>
    static constexpr bool constructor2 =
        !impl::is_python<T> && !constructor1<T> && std::is_convertible_v<T, double>;
    template <typename T>
    static constexpr bool constructor3 = impl::is_python<T> && !impl::is_float_like<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_float_like<T>; }

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
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    Float(const T& value) : Base(PyFloat_FromDouble(value), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Trigger explicit C++ conversions to double. */
    template <typename T, std::enable_if_t<constructor2<T>, int> = 0>
    explicit Float(const T& value) : Float(static_cast<double>(value)) {}

    /* Implicitly convert Python booleans and integers to py::Float. */
    template <typename T, std::enable_if_t<constructor3<T>, int> = 0>
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
    template <
        typename T,
        std::enable_if_t<impl::is_python<T> && impl::is_str_like<T>, int> = 0
    >
    explicit Float(const T& str);

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a Python float into a C++ float. */
    inline operator double() const {
        return PyFloat_AS_DOUBLE(this->ptr());
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    template <typename... Args>
    auto operator()(Args&&... args) const = delete;

    template <typename T>
    auto operator[](T&& index) const = delete;

    auto begin() const = delete;
    auto end() const = delete;

    template <typename T>
    auto contains(const T& value) const = delete;

};


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


// TODO: include __abs__

template <>
struct Float::__pos__<> : impl::Returns<Float> {};
template <>
struct Float::__neg__<> : impl::Returns<Float> {};
template <>
struct Float::__invert__<> : impl::Returns<Float> {};


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


template <>
struct Float::__lt__<Object> : impl::Returns<bool> {};
template <typename T>
struct Float::__lt__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Float::__lt__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Float::__lt__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};

template <>
struct Float::__le__<Object> : impl::Returns<bool> {};
template <typename T>
struct Float::__le__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Float::__le__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Float::__le__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};

template <>
struct Float::__ge__<Object> : impl::Returns<bool> {};
template <typename T>
struct Float::__ge__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Float::__ge__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Float::__ge__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};

template <>
struct Float::__gt__<Object> : impl::Returns<bool> {};
template <typename T>
struct Float::__gt__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Float::__gt__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Float::__gt__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


template <>
struct Float::__add__<Object> : impl::Returns<Object> {};
template <typename T>
struct Float::__add__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__add__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__add__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__add__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Float::__sub__<Object> : impl::Returns<Object> {};
template <typename T>
struct Float::__sub__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__sub__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__sub__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__sub__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Float::__mul__<Object> : impl::Returns<Object> {};
template <typename T>
struct Float::__mul__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__mul__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__mul__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__mul__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Float::__truediv__<Object> : impl::Returns<Object> {};
template <typename T>
struct Float::__truediv__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__truediv__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__truediv__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__truediv__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Float::__mod__<Object> : impl::Returns<Object> {};
template <typename T>
struct Float::__mod__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__mod__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__mod__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
// template <typename T>    <-- Disabled in Python
// struct Float::__mod__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};


/////////////////////////////////
////    INPLACE OPERATORS    ////
/////////////////////////////////


template <typename T>
struct Float::__iadd__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__iadd__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__iadd__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};

template <typename T>
struct Float::__isub__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__isub__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__isub__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};

template <typename T>
struct Float::__imul__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__imul__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__imul__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};

template <typename T>
struct Float::__itruediv__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__itruediv__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__itruediv__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};

template <typename T>
struct Float::__imod__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__imod__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Float::__imod__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Float)


#endif  // BERTRAND_PYTHON_FLOAT_H
