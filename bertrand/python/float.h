#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_FLOAT_H
#define BERTRAND_PYTHON_FLOAT_H

#include "common.h"


namespace bertrand {
namespace py {


/* Represents a statically-typed Python float in C++. */
class Float : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        return impl::float_like<T>;
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();
        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyFloat_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to 0.0. */
    Float() : Base(PyFloat_FromDouble(0.0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Reinterpret_borrow/reinterpret_steal constructors. */
    Float(Handle h, const borrowed_t& t) : Base(h, t) {}
    Float(Handle h, const stolen_t& t) : Base(h, t) {}

    /* Convert equivalent pybind11 types to a py::Float. */
    template <impl::pybind11_like T> requires (typecheck<T>())
    Float(T&& other) : Base(std::forward<T>(other)) {}

    /* Unwrap a pybind11 accessor into a py::Float. */
    template <typename Policy>
    Float(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Float>(accessor).release(), stolen_t{})
    {}

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

    static const Float neg_one;
    static const Float neg_half;
    static const Float zero;
    static const Float half;
    static const Float one;

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    BERTRAND_METHOD(Float, [[nodiscard]], as_integer_ratio, const)
    BERTRAND_METHOD(Float, [[nodiscard]], is_integer, const)
    BERTRAND_METHOD(Float, [[nodiscard]], hex, const)
    BERTRAND_STATIC_METHOD(Float, [[nodiscard]], fromhex)

};


inline const Float Float::neg_one = -1.0;
inline const Float Float::neg_half = -0.5;
inline const Float Float::zero = 0.0;
inline const Float Float::half = 0.5;
inline const Float Float::one = 1.0;


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_FLOAT_H
