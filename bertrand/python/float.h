#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_FLOAT_H
#define BERTRAND_PYTHON_FLOAT_H

#include "common.h"


namespace bertrand {
namespace py {


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
template <impl::bool_like T>
struct __lt__<Float, T>                                         : Returns<bool> {};
template <impl::int_like T>
struct __lt__<Float, T>                                         : Returns<bool> {};
template <impl::float_like T>
struct __lt__<Float, T>                                         : Returns<bool> {};
template <>
struct __le__<Float, Object>                                    : Returns<bool> {};
template <impl::bool_like T>
struct __le__<Float, T>                                         : Returns<bool> {};
template <impl::int_like T>
struct __le__<Float, T>                                         : Returns<bool> {};
template <impl::float_like T>
struct __le__<Float, T>                                         : Returns<bool> {};
template <>
struct __ge__<Float, Object>                                    : Returns<bool> {};
template <impl::bool_like T>
struct __ge__<Float, T>                                         : Returns<bool> {};
template <impl::int_like T>
struct __ge__<Float, T>                                         : Returns<bool> {};
template <impl::float_like T>
struct __ge__<Float, T>                                         : Returns<bool> {};
template <>
struct __gt__<Float, Object>                                    : Returns<bool> {};
template <impl::bool_like T>
struct __gt__<Float, T>                                         : Returns<bool> {};
template <impl::int_like T>
struct __gt__<Float, T>                                         : Returns<bool> {};
template <impl::float_like T>
struct __gt__<Float, T>                                         : Returns<bool> {};
template <>
struct __add__<Float, Object>                                   : Returns<Object> {};
template <impl::bool_like T>
struct __add__<Float, T>                                        : Returns<Float> {};
template <impl::int_like T>
struct __add__<Float, T>                                        : Returns<Float> {};
template <impl::float_like T>
struct __add__<Float, T>                                        : Returns<Float> {};
template <impl::complex_like T>
struct __add__<Float, T>                                        : Returns<Complex> {};
template <>
struct __sub__<Float, Object>                                   : Returns<Object> {};
template <impl::bool_like T>
struct __sub__<Float, T>                                        : Returns<Float> {};
template <impl::int_like T>
struct __sub__<Float, T>                                        : Returns<Float> {};
template <impl::float_like T>
struct __sub__<Float, T>                                        : Returns<Float> {};
template <impl::complex_like T>
struct __sub__<Float, T>                                        : Returns<Complex> {};
template <>
struct __mul__<Float, Object>                                   : Returns<Object> {};
template <impl::bool_like T>
struct __mul__<Float, T>                                        : Returns<Float> {};
template <impl::int_like T>
struct __mul__<Float, T>                                        : Returns<Float> {};
template <impl::float_like T>
struct __mul__<Float, T>                                        : Returns<Float> {};
template <impl::complex_like T>
struct __mul__<Float, T>                                        : Returns<Complex> {};
template <>
struct __truediv__<Float, Object>                               : Returns<Object> {};
template <impl::bool_like T>
struct __truediv__<Float, T>                                    : Returns<Float> {};
template <impl::int_like T>
struct __truediv__<Float, T>                                    : Returns<Float> {};
template <impl::float_like T>
struct __truediv__<Float, T>                                    : Returns<Float> {};
template <impl::complex_like T>
struct __truediv__<Float, T>                                    : Returns<Complex> {};
template <>
struct __mod__<Float, Object>                                   : Returns<Object> {};
template <impl::bool_like T>
struct __mod__<Float, T>                                        : Returns<Float> {};
template <impl::int_like T>
struct __mod__<Float, T>                                        : Returns<Float> {};
template <impl::float_like T>
struct __mod__<Float, T>                                        : Returns<Float> {};
// template <impl::complex_like T>    <-- Disabled in Python
// struct __mod__<Float, T>                                     : Returns<Complex> {};
template <impl::bool_like T>
struct __iadd__<Float, T>                                       : Returns<Float&> {};
template <impl::int_like T>
struct __iadd__<Float, T>                                       : Returns<Float&> {};
template <impl::float_like T>
struct __iadd__<Float, T>                                       : Returns<Float&> {};
template <impl::bool_like T>
struct __isub__<Float, T>                                       : Returns<Float&> {};
template <impl::int_like T>
struct __isub__<Float, T>                                       : Returns<Float&> {};
template <impl::float_like T>
struct __isub__<Float, T>                                       : Returns<Float&> {};
template <impl::bool_like T>
struct __imul__<Float, T>                                       : Returns<Float&> {};
template <impl::int_like T>
struct __imul__<Float, T>                                       : Returns<Float&> {};
template <impl::float_like T>
struct __imul__<Float, T>                                       : Returns<Float&> {};
template <impl::bool_like T>
struct __itruediv__<Float, T>                                   : Returns<Float&> {};
template <impl::int_like T>
struct __itruediv__<Float, T>                                   : Returns<Float&> {};
template <impl::float_like T>
struct __itruediv__<Float, T>                                   : Returns<Float&> {};
template <impl::bool_like T>
struct __imod__<Float, T>                                       : Returns<Float&> {};
template <impl::int_like T>
struct __imod__<Float, T>                                       : Returns<Float&> {};
template <impl::float_like T>
struct __imod__<Float, T>                                       : Returns<Float&> {};


/* Represents a statically-typed Python float in C++. */
class Float : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return impl::float_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::python_like<T>) {
            return obj.ptr() != nullptr && PyFloat_Check(obj.ptr());
        } else {
            return check<T>();
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Float(Handle h, const borrowed_t& t) : Base(h, t) {}
    Float(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Float(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Float(const detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Float>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to 0.0. */
    Float() : Base(PyFloat_FromDouble(0.0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Trigger implicit conversions to double. */
    template <impl::cpp_like T> requires (impl::float_like<T>)
    Float(const T& value) : Base(PyFloat_FromDouble(value), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Construct from C++ integers/booleans. */
    template <std::integral T>
    Float(T value) : Base(PyFloat_FromDouble(value), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    // TODO: implicit conversion from Bool, Int

    /* Explicitly convert an arbitrary Python object to py::Float. */
    template <impl::python_like T> requires (!impl::float_like<T>)
    explicit Float(const T& value) : Base(PyNumber_Float(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Trigger explicit conversions to double. */
    template <impl::cpp_like T>
        requires (
            !impl::float_like<T> &&
            !std::integral<T> &&
            impl::explicitly_convertible_to<T, double>
        )
    explicit Float(const T& value) :
        Base(PyFloat_FromDouble(static_cast<double>(value)), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert a string into a py::Float. */
    explicit Float(const Str& str);

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert a Python float into a C++ float. */
    inline operator double() const {
        return PyFloat_AS_DOUBLE(this->ptr());
    }

    /* Get the zero singleton. */
    inline static const Float& zero() {
        static const Float val = 0.0;
        return val;
    }

    /* Get the half singleton. */
    inline static const Float& half() {
        static const Float val = 0.5;
        return val;
    }

    /* Get the one singleton. */
    inline static const Float& one() {
        static const Float val = 1.0;
        return val;
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_FLOAT_H
