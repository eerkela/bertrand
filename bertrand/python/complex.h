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
class Complex : public Object {
    using Base = Object;

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
    static Type type;

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
    Complex(const std::complex<T>& value) : Base(
        PyComplex_FromDoubles(value.real(), value.imag()), stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert a C++ boolean, integer, or float into a py::Complex, using
    the number as a real component. */
    template <typename Real, std::enable_if_t<constructor1<Real>, int> = 0>
    explicit Complex(const Real& real) :
        Base(PyComplex_FromDoubles(real, 0.0), stolen_t{})
    {
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
    explicit Complex(const Real& real, const Imag& imag) :
        Base(PyComplex_FromDoubles(real, imag), stolen_t{})
    {
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

    auto begin() const = delete;
    auto end() const = delete;

    DELETE_OPERATOR(operator[])
    DELETE_OPERATOR(operator())

};


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


// TODO: include __abs__

template <>
struct Complex::__pos__<> : impl::Returns<Complex> {};
template <>
struct Complex::__neg__<> : impl::Returns<Complex> {};
template <>
struct Complex::__invert__<> : impl::Returns<Complex> {};


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


template <>
struct Complex::__eq__<Object> : impl::Returns<bool> {};
template <typename T>
struct Complex::__eq__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Complex::__eq__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Complex::__eq__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Complex::__eq__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<bool> {};

template <>
struct Complex::__ne__<Object> : impl::Returns<bool> {};
template <typename T>
struct Complex::__ne__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Complex::__ne__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Complex::__ne__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Complex::__ne__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<bool> {};


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


template <>
struct Complex::__add__<Object> : impl::Returns<Object> {};
template <typename T>
struct Complex::__add__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__add__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__add__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__add__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Complex::__sub__<Object> : impl::Returns<Object> {};
template <typename T>
struct Complex::__sub__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__sub__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__sub__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__sub__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Complex::__mul__<Object> : impl::Returns<Object> {};
template <typename T>
struct Complex::__mul__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__mul__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__mul__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__mul__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Complex::__truediv__<Object> : impl::Returns<Object> {};
template <typename T>
struct Complex::__truediv__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__truediv__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__truediv__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__truediv__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};


/////////////////////////////////
////    INPLACE OPERATORS    ////
/////////////////////////////////


template <typename T>
struct Complex::__iadd__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__iadd__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__iadd__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__iadd__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <typename T>
struct Complex::__isub__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__isub__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__isub__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__isub__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <typename T>
struct Complex::__imul__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__imul__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__imul__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__imul__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};

template <typename T>
struct Complex::__itruediv__<T, std::enable_if_t<impl::is_bool_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__itruediv__<T, std::enable_if_t<impl::is_int_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__itruediv__<T, std::enable_if_t<impl::is_float_like<T>>> : impl::Returns<Complex> {};
template <typename T>
struct Complex::__itruediv__<T, std::enable_if_t<impl::is_complex_like<T>>> : impl::Returns<Complex> {};


}  // namespace bertrand
}  // namespace python


BERTRAND_STD_HASH(bertrand::py::Complex)


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


#endif  // BERTRAND_PYTHON_COMPLEX_H
