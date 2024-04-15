#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_INT_H
#define BERTRAND_PYTHON_INT_H

#include "common.h"


namespace bertrand {
namespace py {


// TODO: Python ints have extra operations like as_integer_ratio, from_bytes, to_bytes,
// etc.


template <>
struct __pos__<Int>                                             : Returns<Int> {};
template <>
struct __neg__<Int>                                             : Returns<Int> {};
template <>
struct __abs__<Int>                                             : Returns<Int> {};
template <>
struct __invert__<Int>                                          : Returns<Int> {};
template <>
struct __increment__<Int>                                       : Returns<Int> {};
template <>
struct __decrement__<Int>                                       : Returns<Int> {};
template <>
struct __hash__<Int>                                            : Returns<size_t> {};
template <>
struct __lt__<Int, Object>                                      : Returns<bool> {};
template <>
struct __lt__<Object, Int>                                      : Returns<bool> {};
template <impl::bool_like T>
struct __lt__<Int, T>                                           : Returns<bool> {};
template <impl::bool_like T>
struct __lt__<T, Int>                                           : Returns<bool> {};
template <impl::int_like T>
struct __lt__<Int, T>                                           : Returns<bool> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __lt__<T, Int>                                           : Returns<bool> {};
template <impl::float_like T>
struct __lt__<Int, T>                                           : Returns<bool> {};
template <impl::float_like T>
struct __lt__<T, Int>                                           : Returns<bool> {};
template <>
struct __le__<Int, Object>                                      : Returns<bool> {};
template <>
struct __le__<Object, Int>                                      : Returns<bool> {};
template <impl::bool_like T>
struct __le__<Int, T>                                           : Returns<bool> {};
template <impl::bool_like T>
struct __le__<T, Int>                                           : Returns<bool> {};
template <impl::int_like T>
struct __le__<Int, T>                                           : Returns<bool> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __le__<T, Int>                                           : Returns<bool> {};
template <impl::float_like T>
struct __le__<Int, T>                                           : Returns<bool> {};
template <impl::float_like T>
struct __le__<T, Int>                                           : Returns<bool> {};
template <>
struct __ge__<Int, Object>                                      : Returns<bool> {};
template <>
struct __ge__<Object, Int>                                      : Returns<bool> {};
template <impl::bool_like T>
struct __ge__<Int, T>                                           : Returns<bool> {};
template <impl::bool_like T>
struct __ge__<T, Int>                                           : Returns<bool> {};
template <impl::int_like T>
struct __ge__<Int, T>                                           : Returns<bool> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __ge__<T, Int>                                           : Returns<bool> {};
template <impl::float_like T>
struct __ge__<Int, T>                                           : Returns<bool> {};
template <impl::float_like T>
struct __ge__<T, Int>                                           : Returns<bool> {};
template <>
struct __gt__<Int, Object>                                      : Returns<bool> {};
template <>
struct __gt__<Object, Int>                                      : Returns<bool> {};
template <impl::bool_like T>
struct __gt__<Int, T>                                           : Returns<bool> {};
template <impl::bool_like T>
struct __gt__<T, Int>                                           : Returns<bool> {};
template <impl::int_like T>
struct __gt__<Int, T>                                           : Returns<bool> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __gt__<T, Int>                                           : Returns<bool> {};
template <impl::float_like T>
struct __gt__<Int, T>                                           : Returns<bool> {};
template <impl::float_like T>
struct __gt__<T, Int>                                           : Returns<bool> {};
template <>
struct __add__<Int, Object>                                     : Returns<Object> {};
template <>
struct __add__<Object, Int>                                     : Returns<Object> {};
template <impl::bool_like T>
struct __add__<Int, T>                                          : Returns<Int> {};
template <impl::bool_like T>
struct __add__<T, Int>                                          : Returns<Int> {};
template <impl::int_like T>
struct __add__<Int, T>                                          : Returns<Int> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __add__<T, Int>                                          : Returns<Int> {};
template <impl::float_like T>
struct __add__<Int, T>                                          : Returns<Float> {};
template <impl::float_like T>
struct __add__<T, Int>                                          : Returns<Float> {};
template <impl::complex_like T>
struct __add__<Int, T>                                          : Returns<Complex> {};
template <impl::complex_like T>
struct __add__<T, Int>                                          : Returns<Complex> {};
template <impl::bool_like T>
struct __iadd__<Int, T>                                         : Returns<Int&> {};
template <impl::int_like T>
struct __iadd__<Int, T>                                         : Returns<Int&> {};
template <>
struct __sub__<Int, Object>                                     : Returns<Object> {};
template <>
struct __sub__<Object, Int>                                     : Returns<Object> {};
template <impl::bool_like T>
struct __sub__<Int, T>                                          : Returns<Int> {};
template <impl::bool_like T>
struct __sub__<T, Int>                                          : Returns<Int> {};
template <impl::int_like T>
struct __sub__<Int, T>                                          : Returns<Int> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __sub__<T, Int>                                          : Returns<Int> {};
template <impl::float_like T>
struct __sub__<Int, T>                                          : Returns<Float> {};
template <impl::float_like T>
struct __sub__<T, Int>                                          : Returns<Float> {};
template <impl::complex_like T>
struct __sub__<Int, T>                                          : Returns<Complex> {};
template <impl::complex_like T>
struct __sub__<T, Int>                                          : Returns<Complex> {};
template <impl::bool_like T>
struct __isub__<Int, T>                                         : Returns<Int&> {};
template <impl::int_like T>
struct __isub__<Int, T>                                         : Returns<Int&> {};
template <>
struct __mul__<Int, Object>                                     : Returns<Object> {};
template <>
struct __mul__<Object, Int>                                     : Returns<Object> {};
template <impl::bool_like T>
struct __mul__<Int, T>                                          : Returns<Int> {};
template <impl::bool_like T>
struct __mul__<T, Int>                                          : Returns<Int> {};
template <impl::int_like T>
struct __mul__<Int, T>                                          : Returns<Int> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __mul__<T, Int>                                          : Returns<Int> {};
template <impl::float_like T>
struct __mul__<Int, T>                                          : Returns<Float> {};
template <impl::float_like T>
struct __mul__<T, Int>                                          : Returns<Float> {};
template <impl::complex_like T>
struct __mul__<Int, T>                                          : Returns<Complex> {};
template <impl::complex_like T>
struct __mul__<T, Int>                                          : Returns<Complex> {};
template <impl::bool_like T>
struct __imul__<Int, T>                                         : Returns<Int&> {};
template <impl::int_like T>
struct __imul__<Int, T>                                         : Returns<Int&> {};
// operator/= is not type-safe in C++ because it converts the result to a float.  Use
// py::Float a = b / c; or py::Int a = py::div(b, c); instead.
template <>
struct __truediv__<Int, Object>                                 : Returns<Object> {};
template <>
struct __truediv__<Object, Int>                                 : Returns<Object> {};
template <impl::bool_like T>
struct __truediv__<Int, T>                                      : Returns<Float> {};
template <impl::bool_like T>
struct __truediv__<T, Int>                                      : Returns<Float> {};
template <impl::int_like T>
struct __truediv__<Int, T>                                      : Returns<Float> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __truediv__<T, Int>                                      : Returns<Float> {};
template <impl::float_like T>
struct __truediv__<Int, T>                                      : Returns<Float> {};
template <impl::float_like T>
struct __truediv__<T, Int>                                      : Returns<Float> {};
template <impl::complex_like T>
struct __truediv__<Int, T>                                      : Returns<Complex> {};
template <impl::complex_like T>
struct __truediv__<T, Int>                                      : Returns<Complex> {};
template <>
struct __mod__<Int, Object>                                     : Returns<Object> {};
template <>
struct __mod__<Object, Int>                                     : Returns<Object> {};
template <impl::bool_like T>
struct __mod__<Int, T>                                          : Returns<Int> {};
template <impl::bool_like T>
struct __mod__<T, Int>                                          : Returns<Int> {};
template <impl::int_like T>
struct __mod__<Int, T>                                          : Returns<Int> {};
template <impl::int_like T> requires (!std::is_same_v<T, Int>)
struct __mod__<T, Int>                                          : Returns<Int> {};
template <impl::float_like T>
struct __mod__<Int, T>                                          : Returns<Float> {};
template <impl::float_like T>
struct __mod__<T, Int>                                          : Returns<Float> {};
// template <impl::complex_like T>    <-- Disabled in Python
// struct __mod__<Int, T>                                       : Returns<Complex> {};
template <impl::bool_like T>
struct __imod__<Int, T>                                         : Returns<Int&> {};
template <impl::int_like T>
struct __imod__<Int, T>                                         : Returns<Int&> {};
template <>
struct __lshift__<Int, Object>                                  : Returns<Object> {};
template <>
struct __lshift__<Object, Int>                                  : Returns<Object> {};
template <impl::bool_like T>
struct __lshift__<Int, T>                                       : Returns<Int> {};
template <impl::bool_like T>
struct __lshift__<T, Int>                                       : Returns<Int> {};
template <impl::int_like T>
struct __lshift__<Int, T>                                       : Returns<Int> {};
template <impl::int_like T>
struct __lshift__<T, Int>                                       : Returns<Int> {};
template <impl::bool_like T>
struct __ilshift__<Int, T>                                      : Returns<Int&> {};
template <impl::int_like T>
struct __ilshift__<Int, T>                                      : Returns<Int&> {};
template <>
struct __rshift__<Int, Object>                                  : Returns<Object> {};
template <>
struct __rshift__<Object, Int>                                  : Returns<Object> {};
template <impl::bool_like T>
struct __rshift__<Int, T>                                       : Returns<Int> {};
template <impl::bool_like T>
struct __rshift__<T, Int>                                       : Returns<Int> {};
template <impl::int_like T>
struct __rshift__<Int, T>                                       : Returns<Int> {};
template <impl::int_like T>
struct __rshift__<T, Int>                                       : Returns<Int> {};
template <impl::bool_like T>
struct __irshift__<Int, T>                                      : Returns<Int&> {};
template <impl::int_like T>
struct __irshift__<Int, T>                                      : Returns<Int&> {};
template <>
struct __and__<Int, Object>                                     : Returns<Object> {};
template <>
struct __and__<Object, Int>                                     : Returns<Object> {};
template <impl::bool_like T>
struct __and__<Int, T>                                          : Returns<Int> {};
template <impl::bool_like T>
struct __and__<T, Int>                                          : Returns<Int> {};
template <impl::int_like T>
struct __and__<Int, T>                                          : Returns<Int> {};
template <impl::int_like T>
struct __and__<T, Int>                                          : Returns<Int> {};
template <impl::bool_like T>
struct __iand__<Int, T>                                         : Returns<Int&> {};
template <impl::int_like T>
struct __iand__<Int, T>                                         : Returns<Int&> {};
template <>
struct __or__<Int, Object>                                      : Returns<Object> {};
template <>
struct __or__<Object, Int>                                      : Returns<Object> {};
template <impl::bool_like T>
struct __or__<Int, T>                                           : Returns<Int> {};
template <impl::bool_like T>
struct __or__<T, Int>                                           : Returns<Int> {};
template <impl::int_like T>
struct __or__<Int, T>                                           : Returns<Int> {};
template <impl::int_like T>
struct __or__<T, Int>                                           : Returns<Int> {};
template <impl::bool_like T>
struct __ior__<Int, T>                                          : Returns<Int&> {};
template <impl::int_like T>
struct __ior__<Int, T>                                          : Returns<Int&> {};
template <>
struct __xor__<Int, Object>                                     : Returns<Object> {};
template <>
struct __xor__<Object, Int>                                     : Returns<Object> {};
template <impl::bool_like T>
struct __xor__<Int, T>                                          : Returns<Int> {};
template <impl::bool_like T>
struct __xor__<T, Int>                                          : Returns<Int> {};
template <impl::int_like T>
struct __xor__<Int, T>                                          : Returns<Int> {};
template <impl::int_like T>
struct __xor__<T, Int>                                          : Returns<Int> {};
template <impl::bool_like T>
struct __ixor__<Int, T>                                         : Returns<Int&> {};
template <impl::int_like T>
struct __ixor__<Int, T>                                         : Returns<Int&> {};


/* Wrapper around pybind11::int_ that enables conversions from strings with different
bases, similar to Python's `int()` constructor, as well as converting math operators
that account for C++ inputs. */
class Int : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool py_constructor = impl::int_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool cpp_constructor = std::is_integral_v<T>;
    template <typename T>
    static constexpr bool py_bool_constructor =
        impl::bool_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool py_double_constructor =
        impl::float_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool cpp_double_constructor =
        impl::float_like<T> && !impl::python_like<T>;
    template <typename T>
    static constexpr bool py_converting_constructor = (
        !impl::bool_like<T> &&
        !impl::int_like<T> &&
        !impl::float_like<T> &&
        !impl::str_like<T> &&
        impl::python_like<T>
    );
    template <typename T>
    static constexpr bool cpp_converting_constructor = (
        !impl::bool_like<T> &&
        !impl::int_like<T> &&
        !impl::float_like<T> &&
        !impl::str_like<T> &&
        !impl::python_like<T>
    );

    /* Helper function allows explicit conversion from any C++ type that implements an
    implicit or explicit conversion to an integer. */
    template <typename T>
    inline static auto trigger_explicit_conversions(const T& value) {
        if constexpr (impl::explicitly_convertible_to<T, uint64_t>) {
            return static_cast<uint64_t>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned long long>) {
            return static_cast<unsigned long long>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, int64_t>) {
            return static_cast<int64_t>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, long long>) {
            return static_cast<long long>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, uint32_t>) {
            return static_cast<uint32_t>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned long>) {
            return static_cast<unsigned long>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned int>) {
            return static_cast<unsigned int>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, int32_t>) {
            return static_cast<int32_t>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, long>) {
            return static_cast<int>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, int>) {
            return static_cast<int>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, uint16_t>) {
            return static_cast<uint16_t>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned short>) {
            return static_cast<unsigned short>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, int16_t>) {
            return static_cast<int16_t>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, short>) {
            return static_cast<short>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, uint8_t>) {
            return static_cast<uint8_t>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned char>) {
            return static_cast<unsigned char>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, int8_t>) {
            return static_cast<int8_t>(value);
        } else if constexpr (impl::explicitly_convertible_to<T, char>) {
            return static_cast<char>(value);
        } else {
            static_assert(impl::explicitly_convertible_to<T, bool>);
            return static_cast<bool>(value);
        }
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Int, impl::int_like, PyLong_Check)
    BERTRAND_OBJECT_OPERATORS(Int)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to 0. */
    Int() : Base(PyLong_FromLong(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (py_constructor<T>)
    Int(T&& other) : Base(std::forward<T>(other)) {}

    /* Implicitly promote Python booleans to py::Int. */
    template <typename T> requires (py_bool_constructor<T>)
    Int(const T& value) : Base(PyNumber_Long(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly convert C++ booleans and integers to py::Int. */
    template <typename T> requires (cpp_constructor<T>)
    Int(const T& value) {
        if constexpr (sizeof(T) <= sizeof(long)) {
            if constexpr (std::is_signed_v<T>) {
                m_ptr = PyLong_FromLong(value);
            } else {
                m_ptr = PyLong_FromUnsignedLong(value);
            }
        } else {
            if constexpr (std::is_signed_v<T>) {
                m_ptr = PyLong_FromLongLong(value);
            } else {
                m_ptr = PyLong_FromUnsignedLongLong(value);
            }
        }
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert a Python float into a py::Int. */
    template <typename T> requires (py_double_constructor<T>)
    explicit Int(const T& value) : Base(PyNumber_Long(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert a C++ float into a py::Int. */
    template <typename T> requires (cpp_double_constructor<T>)
    explicit Int(const T& value) : Base(PyLong_FromDouble(value), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert an arbitrary Python object into an integer. */
    template <typename T> requires (py_converting_constructor<T>)
    explicit Int(const T& obj) : Base(PyNumber_Long(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Trigger explicit conversion operators to C++ integer types. */
    template <typename T> requires (cpp_converting_constructor<T>)
    explicit Int(const T& value) : Int(trigger_explicit_conversions(value)) {}

    /* Explicitly convert a string literal with an optional base into a py::Int. */
    explicit Int(const char* str, int base = 0) :
        Base(PyLong_FromString(str, nullptr, base), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert a std::string with an optional base into a py::Int. */
    explicit Int(const std::string& str, int base = 0) : Int(str.c_str(), base) {}

    /* Explicitly convert a std::string_view with an optional base into a py::Int. */
    explicit Int(const std::string_view& str, int base = 0) : Int(str.data(), base) {}

    /* Explicitly convert a Python string with an optional base into a py::Int. */
    template <typename T> requires (impl::python_like<T> && impl::str_like<T>)
    explicit Int(const T& str, int base = 0);

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert to pybind11::int_. */
    inline operator pybind11::int_() const {
        return reinterpret_borrow<pybind11::int_>(m_ptr);
    }

    /* Implicitly convert a Python int into a C++ integer. */
    template <typename T> requires (!impl::python_like<T> && impl::int_like<T>)
    inline operator T() const {
        if constexpr (sizeof(T) <= sizeof(long)) {
            if constexpr (std::is_signed_v<T>) {
                return PyLong_AsLong(m_ptr);
            } else {
                return PyLong_AsUnsignedLong(m_ptr);
            }
        } else {
            if constexpr (std::is_signed_v<T>) {
                return PyLong_AsLongLong(m_ptr);
            } else {
                return PyLong_AsUnsignedLongLong(m_ptr);
            }
        }
    }

    /* Implicitly convert a Python int into a C++ float. */
    template <typename T> requires (std::is_floating_point_v<T>)
    inline operator T() const {
        return PyLong_AsDouble(m_ptr);
    }

    /* Get a static reference to the zero singleton. */
    static const Int& zero() {
        static const Int zero(0);
        return zero;
    }

    /* Get a static reference to the one singleton. */
    static const Int& one() {
        static const Int one(1);
        return one;
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_INT_H
