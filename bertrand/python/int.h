#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_INT_H
#define BERTRAND_PYTHON_INT_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::int_ that enables conversions from strings with different
bases, similar to Python's `int()` constructor, as well as converting math operators
that account for C++ inputs. */
class Int : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool constructor1 = impl::is_bool_like<T> && !impl::is_python<T>;
    template <typename T>
    static constexpr bool constructor2 = impl::is_bool_like<T> && impl::is_python<T>;
    template <typename T>
    static constexpr bool constructor3 = impl::is_int_like<T> && !impl::is_python<T>;
    template <typename T>
    static constexpr bool constructor4 = impl::is_float_like<T> && !impl::is_python<T>;
    template <typename T>
    static constexpr bool constructor5 = impl::is_float_like<T> && impl::is_python<T>;
    template <typename T>
    static constexpr bool constructor6 = (
        !impl::is_bool_like<T> &&
        !impl::is_int_like<T> &&
        !impl::is_float_like<T> &&
        !impl::is_str_like<T> &&
        !impl::is_python<T>
    );
    template <typename T>
    static constexpr bool constructor7 = (
        !impl::is_bool_like<T> &&
        !impl::is_int_like<T> &&
        !impl::is_float_like<T> &&
        !impl::is_str_like<T> &&
        impl::is_python<T>
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

    template <typename T>
    static constexpr bool check() { return impl::is_int_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Copy/move constructors from equivalent pybind11 type. */
    Int(const pybind11::int_& other) : Base(other.ptr(), borrowed_t{}) {}
    Int(pybind11::int_&& other) : Base(other.release(), stolen_t{}) {}

    BERTRAND_OBJECT_COMMON(Base, Int, PyLong_Check)

    /* Default constructor.  Initializes to 0. */
    Int() : Base(PyLong_FromLong(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert C++ booleans and integers to py::Int. */
    template <typename T, std::enable_if_t<constructor1<T> || constructor3<T>, int> = 0>
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
            throw error_already_set();
        }
    }

    /* Implicitly promote Python booleans to py::Int. */
    template <typename T, std::enable_if_t<constructor2<T>, int> = 0>
    Int(const T& value) : Base(PyNumber_Long(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a C++ float into a py::Int. */
    template <typename T, std::enable_if_t<constructor4<T>, int> = 0>
    explicit Int(const T& value) {
        m_ptr = PyLong_FromDouble(value);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a Python float into a py::Int. */
    template <typename T, std::enable_if_t<constructor5<T>, int> = 0>
    explicit Int(const T& value) : Base(PyNumber_Long(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Trigger explicit conversion operators to C++ integer types. */
    template <typename T, std::enable_if_t<constructor6<T>, int> = 0>
    explicit Int(const T& value) : Int(trigger_explicit_conversions(value)) {}

    /* Explicitly convert an arbitrary Python object into an integer. */
    template <typename T, std::enable_if_t<constructor7<T>, int> = 0>
    explicit Int(const T& obj) : Base(PyNumber_Long(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a string literal with an optional base into a py::Int. */
    explicit Int(const char* str, int base = 0) :
        Base(PyLong_FromString(str, nullptr, base), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a std::string with an optional base into a py::Int. */
    explicit Int(const std::string& str, int base = 0) : Int(str.c_str(), base) {}

    /* Explicitly convert a std::string_view with an optional base into a py::Int. */
    explicit Int(const std::string_view& str, int base = 0) : Int(str.data(), base) {}

    /* Explicitly convert a Python string with an optional base into a py::Int. */
    template <
        typename T,
        std::enable_if_t<impl::is_python<T> && impl::is_str_like<T>, int> = 0
    >
    explicit Int(const T& str, int base = 0);

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a Python int into a C++ integer. */
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
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
    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    inline operator T() const {
        return PyLong_AsDouble(m_ptr);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get a static reference to the zero singleton. */
    static const Int& zero() {
        static const Int zero(0);
        return zero;
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    auto operator*() const = delete;

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


// TODO: include __abs__()?

template <>
struct Int::__pos__<> : impl::Returns<Int> {};
template <>
struct Int::__neg__<> : impl::Returns<Int> {};
template <>
struct Int::__invert__<> : impl::Returns<Int> {};


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


template <>
struct Int::__lt__<Object> : impl::Returns<bool> {};
template <typename T>
struct Int::__lt__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Int::__lt__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Int::__lt__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};

template <>
struct Int::__le__<Object> : impl::Returns<bool> {};
template <typename T>
struct Int::__le__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Int::__le__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Int::__le__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};

template <>
struct Int::__ge__<Object> : impl::Returns<bool> {};
template <typename T>
struct Int::__ge__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Int::__ge__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Int::__ge__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};

template <>
struct Int::__gt__<Object> : impl::Returns<bool> {};
template <typename T>
struct Int::__gt__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Int::__gt__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Int::__gt__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


template <>
struct Int::__add__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__add__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__add__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__add__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Int::__add__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Int::__sub__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__sub__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__sub__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__sub__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Int::__sub__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Int::__mul__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__mul__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__mul__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__mul__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Int::__mul__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Int::__truediv__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__truediv__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Int::__truediv__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Int::__truediv__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Int::__truediv__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Int::__mod__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__mod__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__mod__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__mod__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Float> {};
// template <typename T>    <-- Disabled in Python
// struct Int::__mod__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Int::__lshift__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__lshift__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__lshift__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <>
struct Int::__rshift__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__rshift__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__rshift__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <>
struct Int::__and__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__and__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__and__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <>
struct Int::__or__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__or__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__or__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <>
struct Int::__xor__<Object> : impl::Returns<Object> {};
template <typename T>
struct Int::__xor__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__xor__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};


/////////////////////////////////
////    INPLACE OPERATORS    ////
/////////////////////////////////


template <typename T>
struct Int::__iadd__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__iadd__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <typename T>
struct Int::__isub__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__isub__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <typename T>
struct Int::__imul__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__imul__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

// operator/= is not type-safe in C++ because it converts the result to a float.  Use
// py::Float a = b / c; or py::Int a = py::div(b, c); instead.

template <typename T>
struct Int::__imod__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__imod__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <typename T>
struct Int::__ilshift__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__ilshift__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <typename T>
struct Int::__irshift__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__irshift__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <typename T>
struct Int::__iand__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__iand__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <typename T>
struct Int::__ior__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__ior__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};

template <typename T>
struct Int::__ixor__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Int::__ixor__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Int> {};


}  // namespace python
}  // namespace bertrand

BERTRAND_STD_HASH(bertrand::py::Int)


#endif  // BERTRAND_PYTHON_INT_H
