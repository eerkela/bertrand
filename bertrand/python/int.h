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
class Int : public impl::Ops {
    using Base = impl::Ops;

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
        std::is_convertible_v<T, long long>
    );
    template <typename T>
    static constexpr bool constructor7 = (
        !impl::is_bool_like<T> &&
        !impl::is_int_like<T> &&
        !impl::is_float_like<T> &&
        !impl::is_str_like<T> &&
        impl::is_python<T>
    );

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_int_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_CONSTRUCTORS(Base, Int, PyLong_Check)

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

    /* Trigger implicit C++ conversions to long long. */
    template <typename T, std::enable_if_t<constructor6<T>, int> = 0>
    explicit Int(const T& value) : Int(static_cast<long long>(value)) {}

    /* Explicitly convert an arbitrary Python object into an integer. */
    template <typename T, std::enable_if_t<constructor7<T>, int> = 0>
    explicit Int(const T& obj) : Base(PyNumber_Long(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a string literal with an optional base into a py::Int. */
    explicit Int(const char* str, int base = 0) {
        m_ptr = PyLong_FromString(str, nullptr, base);
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

    using Base::operator<;
    using Base::operator<=;
    using Base::operator==;
    using Base::operator!=;
    using Base::operator>=;
    using Base::operator>;
    using Base::operator~;
    using Base::operator+;
    using Base::operator-;
    using Base::operator*;
    using Base::operator/;
    using Base::operator%;
    using Base::operator<<;
    using Base::operator>>;
    using Base::operator&;
    using Base::operator|;
    using Base::operator^;

private:

    template <typename T>
    static constexpr bool inplace_op = impl::is_bool_like<T> || impl::is_int_like<T>;

public:

    #define INPLACE_OP(op)                                                              \
        template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>                 \
        inline Int& op(const T& other) {                                                \
            Base::op(other);                                                            \
            return *this;                                                               \
        }                                                                               \

    INPLACE_OP(operator+=)
    INPLACE_OP(operator-=)
    INPLACE_OP(operator*=)
    // NOTE: /= is not type-safe in C++ because it converts the result to a float.  Use
    // py::Float a = b / c; or py::Int a = py::div(b, c); instead.
    INPLACE_OP(operator%=)
    INPLACE_OP(operator<<=)
    INPLACE_OP(operator>>=)
    INPLACE_OP(operator&=)
    INPLACE_OP(operator|=)
    INPLACE_OP(operator^=)

    #undef INPLACE_OP
};


}  // namespace python
}  // namespace bertrand

BERTRAND_STD_HASH(bertrand::py::Int)


#endif  // BERTRAND_PYTHON_INT_H
