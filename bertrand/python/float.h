#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_FLOAT_H
#define BERTRAND_PYTHON_FLOAT_H

#include "common.h"
#include "str.h"


namespace bertrand {
namespace py {


template <typename T>
struct __issubclass__<T, Float>                             : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return impl::float_like<T>; }
};


template <typename T>
struct __isinstance__<T, Float>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Float>();
        } else if constexpr (issubclass<T, Float>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyFloat_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python float in C++. */
class Float : public Object {
    using Base = Object;
    using Self = Float;

public:
    static const Type type;

    Float(Handle h, borrowed_t t) : Base(h, t) {}
    Float(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Float, __init__<Float, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Float, std::remove_cvref_t<Args>...>::enable
        )
    Float(Args&&... args) : Base(
        __init__<Float, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    template <typename... Args>
        requires (
            !__init__<Float, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Float, __explicit_init__<Float, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Float, std::remove_cvref_t<Args>...>::enable
        )
    explicit Float(Args&&... args) : Base(
        __explicit_init__<Float, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    BERTRAND_METHOD([[nodiscard]], as_integer_ratio, const)
    BERTRAND_METHOD([[nodiscard]], is_integer, const)
    BERTRAND_METHOD([[nodiscard]], hex, const)
    BERTRAND_STATIC_METHOD([[nodiscard]], fromhex)

    static const Float neg_one;
    static const Float neg_half;
    static const Float zero;
    static const Float half;
    static const Float one;

};


template <>
struct __init__<Float>                                      : Returns<Float> {
    static auto operator()() {
        PyObject* result = PyFloat_FromDouble(0.0);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Float>(result);
    }
};


template <impl::cpp_like T> requires (std::is_arithmetic_v<T>)
struct __init__<Float, T>                                   : Returns<Float> {
    static auto operator()(T value) {
        PyObject* result = PyFloat_FromDouble(value);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Float>(result);
    }
};


template <impl::python_like T> requires (impl::bool_like<T> || impl::int_like<T>)
struct __init__<Float, T>                                   : Returns<Float> {
    static auto operator()(const T& obj) {
        PyObject* result = PyNumber_Float(obj.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Float>(result);
    }
};


template <impl::cpp_like T>
    requires (
        !std::is_arithmetic_v<T> &&
        impl::explicitly_convertible_to<T, double>
    )
struct __explicit_init__<Float, T>                          : Returns<Float> {
    static auto operator()(const T& value) {
        PyObject* result = PyFloat_FromDouble(static_cast<double>(value));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Float>(result);
    }
};


template <std::convertible_to<Str> T>
struct __explicit_init__<Float, T>                          : Returns<Float> {
    static auto operator()(const Str& str) {
        PyObject* result = PyFloat_FromString(str.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Float>(result);
    }
};


template <impl::python_like T>
    requires (
        !impl::bool_like<T> &&
        !impl::int_like<T> &&
        !impl::float_like<T> &&
        !std::convertible_to<T, Str>
    )
struct __explicit_init__<Float, T>                          : Returns<Float> {
    static auto operator()(const T& obj) {
        PyObject* result = PyNumber_Float(obj.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Float>(result);
    }
};


template <std::derived_from<Float> From, std::floating_point To>
struct __cast__<From, To> : Returns<To> {
    static To operator()(const From& from) {
        return PyFloat_AS_DOUBLE(from.ptr());
    }
};


inline const Float Float::neg_one = -1.0;
inline const Float Float::neg_half = -0.5;
inline const Float Float::zero = 0.0;
inline const Float Float::half = 0.5;
inline const Float Float::one = 1.0;


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_FLOAT_H
