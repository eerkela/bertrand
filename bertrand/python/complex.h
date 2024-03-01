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
class Complex : public impl::Ops {
    using Base = impl::Ops;

    template <typename T>
    static constexpr bool constructor1 = (
        !impl::is_python<T> && (
            impl::is_bool_like<T> || impl::is_int_like<T> || impl::is_float_like<T>
        )
    );
    template <typename T>
    static constexpr bool constructor2 =
        !impl::is_python<T> && !constructor1<T> &&
        std::is_convertible_v<T, std::complex<double>>;
    template <typename T>
    static constexpr bool constructor3 =
        impl::is_python<T> && !impl::is_complex_like<T>;

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool check() { return impl::is_complex_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_CONSTRUCTORS(Base, Complex, PyComplex_Check)

    /* Default constructor.  Initializes to 0+0j. */
    Complex() : Base(PyComplex_FromDoubles(0.0, 0.0), stolen_t{}) {
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

    /* Explicitly convert a C++ boolean, integer, or float into a py::Complex, using
    the number as a real component. */
    template <typename Real, std::enable_if_t<constructor1<Real>, int> = 0>
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
        std::enable_if_t<constructor1<Real> && constructor1<Imag>, int> = 0
    >
    explicit Complex(const Real& real, const Imag& imag) {
        m_ptr = PyComplex_FromDoubles(real, imag);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Trigger explicit conversions to std::complex<double>. */
    template <typename T, std::enable_if_t<constructor2<T>, int> = 0>
    explicit Complex(const T& value) :
        Complex(static_cast<std::complex<double>>(value)) {}


    /* Explicitly convert an arbitrary Python object into a complex number. */
    template <typename T, std::enable_if_t<constructor3<T>, int> = 0>
    explicit Complex(const T& obj) {
        Py_complex complex_struct = PyComplex_AsCComplex(obj.ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        m_ptr = PyComplex_FromDoubles(complex_struct.real, complex_struct.imag);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

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
    static constexpr bool inplace_op = (
        impl::is_bool_like<T> || impl::is_int_like<T> || impl::is_float_like<T> ||
        impl::is_complex_like<T>
    );

public:

    #define INPLACE_OP(op)                                                              \
        template <typename T, std::enable_if_t<inplace_op<T>, int> = 0>                 \
        inline Complex& op(const T& other) {                                            \
            Base::op(other);                                                            \
            return *this;                                                               \
        }                                                                               \

    INPLACE_OP(operator+=)
    INPLACE_OP(operator-=)
    INPLACE_OP(operator*=)
    INPLACE_OP(operator/=)
    INPLACE_OP(operator%=)
    INPLACE_OP(operator<<=)
    INPLACE_OP(operator>>=)
    INPLACE_OP(operator&=)
    INPLACE_OP(operator|=)
    INPLACE_OP(operator^=)

    #undef INPLACE_OP

};


}  // namespace bertrand
}  // namespace python


BERTRAND_STD_HASH(bertrand::py::Complex)


#endif  // BERTRAND_PYTHON_COMPLEX_H
