#ifndef BERTRAND_PYTHON_COMPLEX_H
#define BERTRAND_PYTHON_COMPLEX_H

#include <complex>
#include "common.h"


namespace bertrand {
namespace py {


/* New subclass of pybind11::object that represents a complex number at the Python
level. */
class Complex :
    public pybind11::object,
    public impl::NumericOps<Complex>,
    public impl::FullCompare<Complex>
{
    using Base = pybind11::object;
    using Ops = impl::NumericOps<Complex>;
    using Compare = impl::FullCompare<Complex>;

    static PyObject* convert_to_complex(PyObject* obj) {
        Py_complex complex_struct = PyComplex_AsCComplex(obj);
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        PyObject* result = PyComplex_FromDoubles(
            complex_struct.real, complex_struct.imag
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    static py::Type Type;

    CONSTRUCTORS(Complex, PyComplex_Check, convert_to_complex);

    /* Default constructor.  Initializes to 0+0j. */
    inline Complex() : Base([] {
        PyObject* result = PyComplex_FromDoubles(0.0, 0.0);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Complex number from a C++ std::complex. */
    template <typename T>
    inline Complex(const std::complex<T>& value) : Base([&value] {
        PyObject* result = PyComplex_FromDoubles(
            static_cast<double>(value.real()),
            static_cast<double>(value.imag())
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Complex number from a real component as a C++ integer or float. */
    template <typename Real, std::enable_if_t<std::is_arithmetic_v<Real>, int> = 0>
    inline Complex(Real real) : Base([&real] {
        PyObject* result = PyComplex_FromDoubles(
            static_cast<double>(real),
            0.0
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Complex number from separate real and imaginary components as C++
    integers or floats. */
    template <
        typename Real,
        typename Imag,
        std::enable_if_t<std::is_arithmetic_v<Real> && std::is_arithmetic_v<Imag>, int> = 0
    >
    inline Complex(Real real, Imag imag) : Base([&real, &imag] {
        PyObject* result = PyComplex_FromDoubles(
            static_cast<double>(real),
            static_cast<double>(imag)
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

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
        return {complex_struct.real, -complex_struct.imag};
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

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
    using Ops::operator+=;
    using Ops::operator-=;
    using Ops::operator*=;
    using Ops::operator/=;
    using Ops::operator%=;
    using Ops::operator<<=;
    using Ops::operator>>=;
    using Ops::operator&=;
    using Ops::operator|=;
    using Ops::operator^=;
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
