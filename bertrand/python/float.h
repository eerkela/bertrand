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
    static consteval bool typecheck() {
        return impl::float_like<T>;
    }

    template <typename T>
    static constexpr bool typecheck(const T& obj) {
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

    Float(Handle h, const borrowed_t& t) : Base(h, t) {}
    Float(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (typecheck<T>())
    Float(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Float(const pybind11::detail::accessor<Policy>& accessor) :
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

    // TODO: these can be ordinary static members rather than functions, which would
    // yield higher performance.

    /* Get the zero singleton. */
    static const Float& zero() {
        static const Float val = 0.0;
        return val;
    }

    /* Get the half singleton. */
    static const Float& half() {
        static const Float val = 0.5;
        return val;
    }

    /* Get the one singleton. */
    static const Float& one() {
        static const Float val = 1.0;
        return val;
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_FLOAT_H
