#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMPLEX_H
#define BERTRAND_PYTHON_COMPLEX_H

#include "common.h"


namespace bertrand {
namespace py {


namespace impl {

template <std::derived_from<Complex> T>
struct __pos__<T>                                               : Returns<Complex> {};
template <std::derived_from<Complex> T>
struct __neg__<T>                                               : Returns<Complex> {};
template <std::derived_from<Complex> T>
struct __abs__<T>                                               : Returns<Complex> {};
template <std::derived_from<Complex> T>
struct __invert__<T>                                            : Returns<Complex> {};
template <std::derived_from<Complex> T>
struct __increment__<T>                                         : Returns<Complex> {};
template <std::derived_from<Complex> T>
struct __decrement__<T>                                         : Returns<Complex> {};
template <std::derived_from<Complex> T>
struct __hash__<T>                                              : Returns<size_t> {};
template <std::derived_from<Complex> L>
struct __add__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Complex> L, bool_like R>
struct __add__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, int_like R>
struct __add__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, float_like R>
struct __add__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, complex_like R>
struct __add__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, bool_like R>
struct __iadd__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, int_like R>
struct __iadd__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, float_like R>
struct __iadd__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, complex_like R>
struct __iadd__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L>
struct __sub__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Complex> L, bool_like R>
struct __sub__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, int_like R>
struct __sub__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, float_like R>
struct __sub__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, complex_like R>
struct __sub__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, bool_like R>
struct __isub__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, int_like R>
struct __isub__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, float_like R>
struct __isub__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, complex_like R>
struct __isub__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L>
struct __mul__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Complex> L, bool_like R>
struct __mul__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, int_like R>
struct __mul__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, float_like R>
struct __mul__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, complex_like R>
struct __mul__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Complex> L, bool_like R>
struct __imul__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, int_like R>
struct __imul__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, float_like R>
struct __imul__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L, complex_like R>
struct __imul__<L, R>                                           : Returns<L&> {};
template <std::derived_from<Complex> L>
struct __truediv__<L, Object>                                   : Returns<Object> {};
template <std::derived_from<Complex> L, bool_like R>
struct __truediv__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, int_like R>
struct __truediv__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, float_like R>
struct __truediv__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, complex_like R>
struct __truediv__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, bool_like R>
struct __itruediv__<L, R>                                       : Returns<L&> {};
template <std::derived_from<Complex> L, int_like R>
struct __itruediv__<L, R>                                       : Returns<L&> {};
template <std::derived_from<Complex> L, float_like R>
struct __itruediv__<L, R>                                       : Returns<L&> {};
template <std::derived_from<Complex> L, complex_like R>
struct __itruediv__<L, R>                                       : Returns<L&> {};

template <std::derived_from<Complex> T>
struct __getattr__<T, "conjugate">                              : Returns<Function> {};
template <std::derived_from<Complex> T>
struct __getattr__<T, "real">                                   : Returns<Float> {};
template <std::derived_from<Complex> T>
struct __getattr__<T, "imag">                                   : Returns<Float> {};

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

    BERTRAND_OBJECT_COMMON(Base, Complex, impl::complex_like, PyComplex_Check)
    BERTRAND_OBJECT_OPERATORS(Complex)

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
            value = bertrand::py::reinterpret_borrow<bertrand::py::Complex>(src.ptr());
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
