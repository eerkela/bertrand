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


// TODO: enable __pow__ and overload operator_pow() to account for negative integer
// exponents.  This just delegates to parent implementation, but sets a different
// return type before doing so.
// -> Can't change the return type based on a runtime condition, so this always needs
// to return a float.


/* Represents a statically-typed Python integer in C++. */
class Int : public Object {
    using Base = Object;

    /* Helper function allows explicit conversion from any C++ type that implements an
    implicit or explicit conversion to an integer. */
    template <typename T>
    static auto trigger_explicit_conversions(const T& value) {
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
    static const Type type;

    template <typename T>
    static consteval bool typecheck() {
        return impl::int_like<T>;
    }

    template <typename T>
    static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();
        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyLong_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Int(Handle h, const borrowed_t& t) : Base(h, t) {}
    Int(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (typecheck<T>())
    Int(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Int(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Int>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to 0. */
    Int() : Base(PyLong_FromLong(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly promote Python booleans to py::Int. */
    template <impl::python_like T> requires (impl::bool_like<T>)
    Int(const T& value) : Base(PyNumber_Long(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly convert C++ booleans and integers to py::Int. */
    template <typename T> requires (std::integral<T>)
    Int(const T& value) : Base(nullptr, stolen_t{}) {
        if constexpr (sizeof(T) <= sizeof(long)) {
            if constexpr (std::signed_integral<T>) {
                m_ptr = PyLong_FromLong(value);
            } else {
                m_ptr = PyLong_FromUnsignedLong(value);
            }
        } else {
            if constexpr (std::signed_integral<T>) {
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
    template <impl::python_like T> requires (impl::float_like<T>)
    explicit Int(const T& value) : Base(PyNumber_Long(value.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert a C++ float into a py::Int. */
    template <impl::cpp_like T> requires (impl::float_like<T>)
    explicit Int(const T& value) : Base(PyLong_FromDouble(value), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert an arbitrary Python object into an integer. */
    template <impl::python_like T>
        requires (
            !impl::bool_like<T> &&
            !impl::int_like<T> &&
            !impl::float_like<T> &&
            !impl::str_like<T>
        )
    explicit Int(const T& obj) : Base(PyNumber_Long(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Trigger explicit conversion operators to C++ integer types. */
    template <impl::cpp_like T>
        requires (
            !impl::bool_like<T> &&
            !impl::int_like<T> &&
            !impl::float_like<T> &&
            !impl::str_like<T>
        )
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
    template <impl::python_like T> requires (impl::str_like<T>)
    explicit Int(const T& str, int base = 0);

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Get a static reference to the zero singleton. */
    static const Int& zero() {
        static const Int zero = 0;
        return zero;
    }

    /* Get a static reference to the one singleton. */
    static const Int& one() {
        static const Int one = 1;
        return one;
    }

    /* Get a static reference to the two singleton. */
    static const Int& two() {
        static const Int two = 2;
        return two;
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_INT_H
