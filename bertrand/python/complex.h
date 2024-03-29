#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMPLEX_H
#define BERTRAND_PYTHON_COMPLEX_H

#include "common.h"
#include <complex>


namespace bertrand {
namespace py {


namespace impl {

template <>
struct __pos__<Complex>                                         : Returns<Complex> {};
template <>
struct __neg__<Complex>                                         : Returns<Complex> {};
template <>
struct __abs__<Complex>                                         : Returns<Complex> {};
template <>
struct __invert__<Complex>                                      : Returns<Complex> {};
template <>
struct __increment__<Complex>                                   : Returns<Complex> {};
template <>
struct __decrement__<Complex>                                   : Returns<Complex> {};
template <>
struct __hash__<Complex>                                        : Returns<size_t> {};
template <>
struct __add__<Complex, Object>                                 : Returns<Object> {};
template <bool_like T>
struct __add__<Complex, T>                                      : Returns<Complex> {};
template <int_like T>
struct __add__<Complex, T>                                      : Returns<Complex> {};
template <float_like T>
struct __add__<Complex, T>                                      : Returns<Complex> {};
template <complex_like T>
struct __add__<Complex, T>                                      : Returns<Complex> {};
template <>
struct __sub__<Complex, Object>                                 : Returns<Object> {};
template <bool_like T>
struct __sub__<Complex, T>                                      : Returns<Complex> {};
template <int_like T>
struct __sub__<Complex, T>                                      : Returns<Complex> {};
template <float_like T>
struct __sub__<Complex, T>                                      : Returns<Complex> {};
template <complex_like T>
struct __sub__<Complex, T>                                      : Returns<Complex> {};
template <>
struct __mul__<Complex, Object>                                 : Returns<Object> {};
template <bool_like T>
struct __mul__<Complex, T>                                      : Returns<Complex> {};
template <int_like T>
struct __mul__<Complex, T>                                      : Returns<Complex> {};
template <float_like T>
struct __mul__<Complex, T>                                      : Returns<Complex> {};
template <complex_like T>
struct __mul__<Complex, T>                                      : Returns<Complex> {};
template <>
struct __truediv__<Complex, Object>                             : Returns<Object> {};
template <bool_like T>
struct __truediv__<Complex, T>                                  : Returns<Complex> {};
template <int_like T>
struct __truediv__<Complex, T>                                  : Returns<Complex> {};
template <float_like T>
struct __truediv__<Complex, T>                                  : Returns<Complex> {};
template <complex_like T>
struct __truediv__<Complex, T>                                  : Returns<Complex> {};
template <bool_like T>
struct __iadd__<Complex, T>                                     : Returns<Complex> {};
template <int_like T>
struct __iadd__<Complex, T>                                     : Returns<Complex> {};
template <float_like T>
struct __iadd__<Complex, T>                                     : Returns<Complex> {};
template <complex_like T>
struct __iadd__<Complex, T>                                     : Returns<Complex> {};
template <bool_like T>
struct __isub__<Complex, T>                                     : Returns<Complex> {};
template <int_like T>
struct __isub__<Complex, T>                                     : Returns<Complex> {};
template <float_like T>
struct __isub__<Complex, T>                                     : Returns<Complex> {};
template <complex_like T>
struct __isub__<Complex, T>                                     : Returns<Complex> {};
template <bool_like T>
struct __imul__<Complex, T>                                     : Returns<Complex> {};
template <int_like T>
struct __imul__<Complex, T>                                     : Returns<Complex> {};
template <float_like T>
struct __imul__<Complex, T>                                     : Returns<Complex> {};
template <complex_like T>
struct __imul__<Complex, T>                                     : Returns<Complex> {};
template <bool_like T>
struct __itruediv__<Complex, T>                                 : Returns<Complex> {};
template <int_like T>
struct __itruediv__<Complex, T>                                 : Returns<Complex> {};
template <float_like T>
struct __itruediv__<Complex, T>                                 : Returns<Complex> {};
template <complex_like T>
struct __itruediv__<Complex, T>                                 : Returns<Complex> {};

}


/* New subclass of pybind11::object that represents a complex number at the Python
level. */
class Complex : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool py_constructor =
        impl::complex_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool cpp_constructor =
        impl::complex_like<T> && !impl::python_like<T>;
    template <typename T>
    static constexpr bool py_converting_constructor =
        !impl::complex_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool cpp_converting_constructor =
        !impl::complex_like<T> && !impl::python_like<T> &&
        impl::explicitly_convertible_to<T, std::complex<double>>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::complex_like<T>; }

    BERTRAND_OBJECT_COMMON(Base, Complex, PyComplex_Check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to 0+0j. */
    Complex() : Base(PyComplex_FromDoubles(0.0, 0.0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (py_constructor<T>)
    Complex(T&& value) : Base(std::forward<T>(value)) {}

    /* Explicitly convert a double into a py::Complex object as its real component. */
    explicit Complex(double real) :
        Base(PyComplex_FromDoubles(real, 0.0), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a pair of doubles representing separate real, imaginary
    components into a py::Complex object. */
    explicit Complex(double real, double imag) :
        Base(PyComplex_FromDoubles(real, imag), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert any C++ type that implements `.real()` and `.imag()` into
    a py::Complex object. */
    template <typename T> requires (cpp_constructor<T>)
    Complex(const T& value) : Base(
        PyComplex_FromDoubles(value.real(), value.imag()),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert an arbitrary Python object into a complex number. */
    template <typename T> requires (py_converting_constructor<T>)
    explicit Complex(const T& obj) {
        Py_complex complex = PyComplex_AsCComplex(obj.ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        m_ptr = PyComplex_FromDoubles(complex.real, complex.imag);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Trigger explicit conversions to std::complex<double>. */
    template <typename T> requires (cpp_converting_constructor<T>)
    explicit Complex(const T& value) :
        Complex(static_cast<std::complex<double>>(value))
    {}

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert a Complex number into a C++ std::complex. */
    template <typename T>
    operator std::complex<T>() const {
        Py_complex complex = PyComplex_AsCComplex(this->ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return std::complex<T>(complex.real, complex.imag);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the real part of the Complex number. */
    inline double real() const noexcept {
        return PyComplex_RealAsDouble(this->ptr());
    }

    /* Get the imaginary part of the Complex number. */
    inline double imag() const noexcept {
        return PyComplex_ImagAsDouble(this->ptr());
    }

    /* Get the magnitude of the Complex number. */
    inline Complex conjugate() const {
        Py_complex complex = PyComplex_AsCComplex(this->ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return Complex(complex.real, -complex.imag);
    }

};


}  // namespace py
}  // namespace bertrand


namespace pybind11 {
namespace detail {

template <>
struct type_caster<bertrand::py::Complex> {
    PYBIND11_TYPE_CASTER(bertrand::py::Complex, _("Complex"));

    /* Convert a Python object into a py::Complex value. */
    bool load(handle src, bool convert) {
        if (PyComplex_Check(src.ptr())) {
            value = reinterpret_borrow<bertrand::py::Complex>(src.ptr());
            return true;
        }

        if (!convert) {
            return false;
        }

        Py_complex complex = PyComplex_AsCComplex(src.ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }

        value = bertrand::py::Complex(complex.real, complex.imag);
        return true;
    }

    /* Convert a Complex value into a Python object. */
    inline static handle cast(
        const bertrand::py::Complex& src,
        return_value_policy /* policy */,
        handle /* parent */
    ) {
        return Py_XNewRef(src.ptr());
    }

};

/* NOTE: pybind11 already implements a type caster for the generic std::complex<T>, so
we can't override it directly.  However, we can specialize it at a lower level to
handle the specific std::complex<> types that we want, which effectively bypasses the
pybind11 implementation.  This is a bit of a hack, but it works. */
#define COMPLEX_CASTER(T)                                                               \
    template <>                                                                         \
    struct type_caster<std::complex<T>> {                                               \
        PYBIND11_TYPE_CASTER(std::complex<T>, _("complex"));                            \
                                                                                        \
        /* Convert a Python object into a std::complex<T> value. */                     \
        bool load(handle src, bool convert) {                                           \
            if (src.ptr() == nullptr) {                                                 \
                return false;                                                           \
            }                                                                           \
            if (!convert && !PyComplex_Check(src.ptr())) {                              \
                return false;                                                           \
            }                                                                           \
            Py_complex complex = PyComplex_AsCComplex(src.ptr());                       \
            if (complex.real == -1.0 && PyErr_Occurred()) {                             \
                PyErr_Clear();                                                          \
                return false;                                                           \
            }                                                                           \
            value = std::complex<T>(                                                    \
                static_cast<T>(complex.real),                                           \
                static_cast<T>(complex.imag)                                            \
            );                                                                          \
            return true;                                                                \
        }                                                                               \
                                                                                        \
        /* Convert a std::complex<T> value into a Python object. */                     \
        inline static handle cast(                                                      \
            const std::complex<T>& src,                                                 \
            return_value_policy /* policy */,                                           \
            handle /* parent */                                                         \
        ) {                                                                             \
            return bertrand::py::Complex(src).release();                                \
        }                                                                               \
                                                                                        \
    };                                                                                  \

COMPLEX_CASTER(float);
COMPLEX_CASTER(double);
COMPLEX_CASTER(long double);

#undef COMPLEX_CASTER

} // namespace detail
} // namespace pybind11


#endif  // BERTRAND_PYTHON_COMPLEX_H
