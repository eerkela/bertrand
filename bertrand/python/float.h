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
class Float : public Object, public impl::Ops<Float> {
    using Ops = impl::Ops<Float>;

    template <typename T>
    static constexpr bool constructor1 = (
        (impl::is_bool_like<T> || impl::is_int_like<T> || impl::is_float_like<T>) &&
        !impl::is_object<T>
    );
    template <typename T>
    static constexpr bool constructor2 = (
        (impl::is_bool_like<T> || impl::is_int_like<T>) &&
        impl::is_object<T>
    );
    template <typename T>
    static constexpr bool constructor3 = impl::is_float_like<T> && impl::is_object<T>;
    template <typename T>
    static constexpr bool constructor4 = (
        !impl::is_bool_like<T> &&
        !impl::is_int_like<T> &&
        !impl::is_float_like<T> &&
        !impl::is_str_like<T> &&
        std::is_convertible_v<T, double>
    );

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_float_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Float, PyFloat_Check, PyNumber_Float)

    /* Default constructor.  Initializes to 0.0. */
    Float() : Object(PyFloat_FromDouble(0.0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert C++ booleans, integers, and floats to py::Float. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    Float(const T& value) : Object(PyFloat_FromDouble(value), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert Python booleans and integers to py::Float. */
    template <typename T, std::enable_if_t<constructor2<T>, int> = 0>
    Float(const T& value) : Object(PyNumber_Float(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert Python floats to py::Float.  Borrows a reference. */
    template <typename T, std::enable_if_t<constructor3<T>, int> = 0>
    Float(const T& value) : Object(value.ptr(), borrowed_t{}) {}

    /* Implicitly convert Python floats to py::Float.  Steals a reference. */
    template <typename T, std::enable_if_t<constructor3<std::decay_t<T>>, int> = 0>
    Float(T&& value) : Object(value.release(), stolen_t{}) {}

    /* Trigger explicit conversions to double. */
    template <typename T, std::enable_if_t<constructor4<T>, int> = 0>
    explicit Float(const T& value) : Float(static_cast<double>(value)) {}

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
        std::enable_if_t<impl::is_str_like<T> && impl::is_object<T>, int> = 0
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

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;
    using Ops::operator~;
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

private:

    template <typename T>
    static constexpr bool inplace_op = (
        impl::is_bool_like<T> || impl::is_int_like<T> || impl::is_float_like<T>
    );

public:

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator+=(const T& other) {
        return Ops::operator+=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator-=(const T& other) {
        return Ops::operator-=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator*=(const T& other) {
        return Ops::operator*=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator/=(const T& other) {
        return Ops::operator/=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator%=(const T& other) {
        return Ops::operator%=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator<<=(const T& other) {
        return Ops::operator<<=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator>>=(const T& other) {
        return Ops::operator>>=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator&=(const T& other) {
        return Ops::operator&=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator|=(const T& other) {
        return Ops::operator|=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Float& operator^=(const T& other) {
        return Ops::operator^=(other);
    }

};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Float)


#endif  // BERTRAND_PYTHON_FLOAT_H
