#ifndef BERTRAND_PYTHON_COMPLEX_H
#define BERTRAND_PYTHON_COMPLEX_H

#include "common.h"
#include "float.h"
#include "str.h"


namespace py {


template <typename T>
struct __issubclass__<T, Complex>                           : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return impl::complex_like<T>; }
};


template <typename T>
struct __isinstance__<T, Complex>                           : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Complex>();
        } else if constexpr (issubclass<T, Complex>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyComplex_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python complex number in C++. */
class Complex : public Object {
    using Base = Object;
    using Self = Complex;

public:
    static const Type type;

    Complex(Handle h, borrowed_t t) : Base(h, t) {}
    Complex(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Complex, __init__<Complex, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Complex, std::remove_cvref_t<Args>...>::enable
        )
    Complex(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Complex, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Complex, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Complex, __explicit_init__<Complex, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Complex, std::remove_cvref_t<Args>...>::enable
        )
    explicit Complex(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Complex, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Get the real part of the Complex number. */
    [[nodiscard]] double real() const noexcept {
        return PyComplex_RealAsDouble(this->ptr());
    }

    /* Get the imaginary part of the Complex number. */
    [[nodiscard]] double imag() const noexcept {
        return PyComplex_ImagAsDouble(this->ptr());
    }

    /* Get the magnitude of the Complex number. */
    [[nodiscard]] auto conjugate() const;

};


template <typename Real, typename Imag>
    requires (
        (impl::bool_like<Real> || impl::int_like<Real> || impl::float_like<Real>) &&
        (impl::bool_like<Imag> || impl::int_like<Imag> || impl::float_like<Imag>)
    )
struct __init__<Complex, Real, Imag>                        : Returns<Complex> {
    static auto operator()(const Real& real, const Imag& imag) {
        PyObject* result = PyComplex_FromDoubles(
            static_cast<double>(real),
            static_cast<double>(imag)
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Complex>(result);
    }
};
template <typename Real>
    requires (impl::bool_like<Real> || impl::int_like<Real> || impl::float_like<Real>)
struct __init__<Complex, Real>                              : Returns<Complex> {
    static auto operator()(const Real& real) { return Complex(real, 0.0); }
};
template <>
struct __init__<Complex>                                    : Returns<Complex> {
    static auto operator()() { return Complex(0.0, 0.0); }
};


template <impl::cpp_like T> requires (impl::complex_like<T>)
struct __init__<Complex, T>                                 : Returns<Complex> {
    static auto operator()(const T& value) {
        return Complex(value.real(), value.imag());
    }
};


template <impl::cpp_like T>
    requires (
        !impl::bool_like<T> &&
        !impl::int_like<T> &&
        !impl::float_like<T> &&
        !impl::complex_like<T> &&
        !impl::str_like<T> &&
        impl::explicitly_convertible_to<T, double>
    )
struct __explicit_init__<Complex, T>                        : Returns<Complex> {
    static auto operator()(const T& value) {
        return Complex(static_cast<double>(value));
    }
};


template <impl::cpp_like T>
    requires (
        !impl::bool_like<T> &&
        !impl::int_like<T> &&
        !impl::float_like<T> &&
        !impl::complex_like<T> &&
        !impl::str_like<T> &&
        impl::explicitly_convertible_to<T, std::complex<double>>
    )
struct __explicit_init__<Complex, T>                        : Returns<Complex> {
    static auto operator()(const T& value) {
        std::complex<double> complex = static_cast<std::complex<double>>(value);
        return Complex(complex.real(), complex.imag());
    }
};


template <impl::str_like T>
struct __explicit_init__<Complex, T>                        : Returns<Complex> {
    static auto operator()(const Str& value) {
        PyObject* result = PyObject_CallOneArg(
            (PyObject*)&PyComplex_Type,
            value.ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Complex>(result);
    }
};


template <impl::python_like T>
    requires (
        !impl::bool_like<T> &&
        !impl::int_like<T> &&
        !impl::float_like<T> &&
        !impl::complex_like<T> &&
        !impl::str_like<T>
    )
struct __explicit_init__<Complex, T>                         : Returns<Complex> {
    static auto operator()(const T& obj) {
        Py_complex complex = PyComplex_AsCComplex(obj.ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return Complex(complex.real, complex.imag);
    }
};


template <std::derived_from<Complex> From, typename To>
struct __cast__<From, std::complex<To>>                     : Returns<std::complex<To>> {
    static auto operator()(const From& from) {
        Py_complex complex = PyComplex_AsCComplex(from.ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return std::complex<To>(complex.real, complex.imag);
    }
};


[[nodiscard]] inline auto Complex::conjugate() const {
    Py_complex complex = PyComplex_AsCComplex(this->ptr());
    if (complex.real == -1.0 && PyErr_Occurred()) {
        Exception::from_python();
    }
    return Complex(complex.real, -complex.imag);
}


}  // namespace py


#endif
