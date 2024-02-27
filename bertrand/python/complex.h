#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMPLEX_H
#define BERTRAND_PYTHON_COMPLEX_H

#include "common.h"
#include <complex>


namespace bertrand {
namespace py {


/* New subclass of pybind11::object that represents a complex number at the Python
level. */
class Complex : public Object, public impl::Ops<Complex> {
    using Ops = impl::Ops<Complex>;

    static PyObject* convert_to_complex(PyObject* obj) {
        Py_complex complex_struct = PyComplex_AsCComplex(obj);
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return PyComplex_FromDoubles(complex_struct.real, complex_struct.imag);
    }

    template <typename T>
    static constexpr bool constructor1 = impl::is_complex_like<T> && impl::is_object<T>;
    template <typename T>
    static constexpr bool constructor2 = (
        (impl::is_bool_like<T> || impl::is_int_like<T> || impl::is_float_like<T>) &&
        !impl::is_object<T>
    );

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_complex_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Complex, PyComplex_Check, convert_to_complex)

    /* Default constructor.  Initializes to 0+0j. */
    Complex() : Object(PyComplex_FromDoubles(0.0, 0.0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert a C++ std::complex<> to py::Complex. */
    template <typename T>
    Complex(const std::complex<T>& value) {
        m_ptr = PyComplex_FromDoubles(value.real(), value.imag());
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert Python complex numbers to py::Complex.  Borrows a
    reference. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    Complex(const T& value) : Object(value.ptr(), borrowed_t{}) {}

    /* Implicitly convert Python complex number to py::Complex.  Steals a reference. */
    template <typename T, std::enable_if_t<constructor1<std::decay_t<T>>, int> = 0>
    Complex(T&& value) : Object(value.release(), stolen_t{}) {}

    /* Explicitly convert a C++ boolean, integer, or float into a py::Complex, using
    the number as a real component. */
    template <typename Real, std::enable_if_t<constructor2<Real>, int> = 0>
    explicit Complex(const Real& real) {
        m_ptr = PyComplex_FromDoubles(real, 0.0);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a pair of C++ booleans, integers, or floats into a
    py::Complex as separate real and imaginary components. */
    template <
        typename Real,
        typename Imag,
        std::enable_if_t<constructor2<Real> && constructor2<Imag>, int> = 0
    >
    explicit Complex(const Real& real, const Imag& imag) {
        m_ptr = PyComplex_FromDoubles(real, imag);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a string into a complex number. */
    template <typename T, std::enable_if_t<impl::is_str_like<T>, int> = 0>
    explicit Complex(const T& value);

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a Complex number into a C++ std::complex. */
    template <typename T>
    operator std::complex<T>() const {
        Py_complex complex_struct = PyComplex_AsCComplex(this->ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return std::complex<T>(complex_struct.real, complex_struct.imag);
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
        Py_complex complex_struct = PyComplex_AsCComplex(this->ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return Complex(complex_struct.real, -complex_struct.imag);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;
    using Ops::operator~;
    using Ops::operator+;
    using Ops::operator-;
    using Ops::operator*;
    using Ops::operator/;
    using Ops::operator%;
    using Ops::operator<<;
    using Ops::operator>>;
    using Ops::operator&;
    using Ops::operator|;
    using Ops::operator^;

private:

    template <typename T>
    static constexpr bool inplace_op = (
        impl::is_bool_like<T> || impl::is_int_like<T> || impl::is_float_like<T> ||
        impl::is_complex_like<T>
    );

public:

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator+=(const T& other) {
        return Ops::operator+=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator-=(const T& other) {
        return Ops::operator-=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator*=(const T& other) {
        return Ops::operator*=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator/=(const T& other) {
        return Ops::operator/=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator%=(const T& other) {
        return Ops::operator%=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator<<=(const T& other) {
        return Ops::operator<<=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator>>=(const T& other) {
        return Ops::operator>>=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator&=(const T& other) {
        return Ops::operator&=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator|=(const T& other) {
        return Ops::operator|=(other);
    }

    template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>
    inline Int& operator^=(const T& other) {
        return Ops::operator^=(other);
    }

};


}  // namespace bertrand
}  // namespace python


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

        Py_complex complex_struct = PyComplex_AsCComplex(src.ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }

        value = bertrand::py::Complex(complex_struct.real, complex_struct.imag);
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
            Py_complex complex_struct = PyComplex_AsCComplex(src.ptr());                \
            if (complex_struct.real == -1.0 && PyErr_Occurred()) {                      \
                PyErr_Clear();                                                          \
                return false;                                                           \
            }                                                                           \
            value = std::complex<T>(                                                    \
                static_cast<T>(complex_struct.real),                                    \
                static_cast<T>(complex_struct.imag)                                     \
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


BERTRAND_STD_HASH(bertrand::py::Complex);


#endif  // BERTRAND_PYTHON_COMPLEX_H
