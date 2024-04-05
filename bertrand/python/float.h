#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_FLOAT_H
#define BERTRAND_PYTHON_FLOAT_H

#include "common.h"


namespace bertrand {
namespace py {


namespace impl {

template <>
struct __pos__<Float>                                           : Returns<Float> {};
template <>
struct __neg__<Float>                                           : Returns<Float> {};
template <>
struct __abs__<Float>                                           : Returns<Float> {};
template <>
struct __invert__<Float>                                        : Returns<Float> {};
template <>
struct __increment__<Float>                                     : Returns<Float> {};
template <>
struct __decrement__<Float>                                     : Returns<Float> {};
template <>
struct __hash__<Float>                                          : Returns<size_t> {};
template <>
struct __lt__<Float, Object>                                    : Returns<bool> {};
template <bool_like T>
struct __lt__<Float, T>                                         : Returns<bool> {};
template <int_like T>
struct __lt__<Float, T>                                         : Returns<bool> {};
template <float_like T>
struct __lt__<Float, T>                                         : Returns<bool> {};
template <>
struct __le__<Float, Object>                                    : Returns<bool> {};
template <bool_like T>
struct __le__<Float, T>                                         : Returns<bool> {};
template <int_like T>
struct __le__<Float, T>                                         : Returns<bool> {};
template <float_like T>
struct __le__<Float, T>                                         : Returns<bool> {};
template <>
struct __ge__<Float, Object>                                    : Returns<bool> {};
template <bool_like T>
struct __ge__<Float, T>                                         : Returns<bool> {};
template <int_like T>
struct __ge__<Float, T>                                         : Returns<bool> {};
template <float_like T>
struct __ge__<Float, T>                                         : Returns<bool> {};
template <>
struct __gt__<Float, Object>                                    : Returns<bool> {};
template <bool_like T>
struct __gt__<Float, T>                                         : Returns<bool> {};
template <int_like T>
struct __gt__<Float, T>                                         : Returns<bool> {};
template <float_like T>
struct __gt__<Float, T>                                         : Returns<bool> {};
template <>
struct __add__<Float, Object>                                   : Returns<Object> {};
template <bool_like T>
struct __add__<Float, T>                                        : Returns<Float> {};
template <int_like T>
struct __add__<Float, T>                                        : Returns<Float> {};
template <float_like T>
struct __add__<Float, T>                                        : Returns<Float> {};
template <complex_like T>
struct __add__<Float, T>                                        : Returns<Complex> {};
template <>
struct __sub__<Float, Object>                                   : Returns<Object> {};
template <bool_like T>
struct __sub__<Float, T>                                        : Returns<Float> {};
template <int_like T>
struct __sub__<Float, T>                                        : Returns<Float> {};
template <float_like T>
struct __sub__<Float, T>                                        : Returns<Float> {};
template <complex_like T>
struct __sub__<Float, T>                                        : Returns<Complex> {};
template <>
struct __mul__<Float, Object>                                   : Returns<Object> {};
template <bool_like T>
struct __mul__<Float, T>                                        : Returns<Float> {};
template <int_like T>
struct __mul__<Float, T>                                        : Returns<Float> {};
template <float_like T>
struct __mul__<Float, T>                                        : Returns<Float> {};
template <complex_like T>
struct __mul__<Float, T>                                        : Returns<Complex> {};
template <>
struct __truediv__<Float, Object>                               : Returns<Object> {};
template <bool_like T>
struct __truediv__<Float, T>                                    : Returns<Float> {};
template <int_like T>
struct __truediv__<Float, T>                                    : Returns<Float> {};
template <float_like T>
struct __truediv__<Float, T>                                    : Returns<Float> {};
template <complex_like T>
struct __truediv__<Float, T>                                    : Returns<Complex> {};
template <>
struct __mod__<Float, Object>                                   : Returns<Object> {};
template <bool_like T>
struct __mod__<Float, T>                                        : Returns<Float> {};
template <int_like T>
struct __mod__<Float, T>                                        : Returns<Float> {};
template <float_like T>
struct __mod__<Float, T>                                        : Returns<Float> {};
// template <complex_like T>    <-- Disabled in Python
// struct __mod__<Float, T>                                     : Returns<Complex> {};
template <bool_like T>
struct __iadd__<Float, T>                                       : Returns<Float> {};
template <int_like T>
struct __iadd__<Float, T>                                       : Returns<Float> {};
template <float_like T>
struct __iadd__<Float, T>                                       : Returns<Float> {};
template <bool_like T>
struct __isub__<Float, T>                                       : Returns<Float> {};
template <int_like T>
struct __isub__<Float, T>                                       : Returns<Float> {};
template <float_like T>
struct __isub__<Float, T>                                       : Returns<Float> {};
template <bool_like T>
struct __imul__<Float, T>                                       : Returns<Float> {};
template <int_like T>
struct __imul__<Float, T>                                       : Returns<Float> {};
template <float_like T>
struct __imul__<Float, T>                                       : Returns<Float> {};
template <bool_like T>
struct __itruediv__<Float, T>                                   : Returns<Float> {};
template <int_like T>
struct __itruediv__<Float, T>                                   : Returns<Float> {};
template <float_like T>
struct __itruediv__<Float, T>                                   : Returns<Float> {};
template <bool_like T>
struct __imod__<Float, T>                                       : Returns<Float> {};
template <int_like T>
struct __imod__<Float, T>                                       : Returns<Float> {};
template <float_like T>
struct __imod__<Float, T>                                       : Returns<Float> {};

}


/* Wrapper around pybind11::float_ that enables conversions from strings, similar to
Python's `float()` constructor, as well as converting math operators that account for
C++ inputs. */
class Float : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool py_constructor = impl::float_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool cpp_constructor =
        impl::float_like<T> && !impl::python_like<T> && std::is_convertible_v<T, double>;
    template <typename T>
    static constexpr bool py_converting_constructor =
        !impl::float_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool cpp_converting_constructor =
        !impl::float_like<T> && !impl::python_like<T> &&
        impl::explicitly_convertible_to<T, double>;

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Float, impl::float_like, PyFloat_Check)
    BERTRAND_OBJECT_OPERATORS(Float)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to 0.0. */
    Float() : Base(PyFloat_FromDouble(0.0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (py_constructor<T>)
    Float(T&& other) : Base(std::forward<T>(other)) {}

    /* Trigger implicit conversions to double. */
    template <typename T> requires (cpp_constructor<T>)
    Float(const T& value) : Base(PyFloat_FromDouble(value), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    // TODO: implicit conversion from Bool, Int

    /* Explicitly convert an arbitrary Python object to py::Float. */
    template <typename T> requires (py_converting_constructor<T>)
    explicit Float(const T& value) : Base(PyNumber_Float(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Trigger explicit conversions to double. */
    template <typename T> requires (cpp_converting_constructor<T>)
    explicit Float(const T& value) :
        Base(PyFloat_FromDouble(static_cast<double>(value)), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a string into a py::Float. */
    explicit Float(const Str& str);

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert to pybind11::float_. */
    inline operator pybind11::float_() const {
        return reinterpret_borrow<pybind11::float_>(m_ptr);
    }

    /* Implicitly convert a Python float into a C++ float. */
    inline operator double() const {
        return PyFloat_AS_DOUBLE(this->ptr());
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_FLOAT_H
