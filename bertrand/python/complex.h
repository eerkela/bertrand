#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMPLEX_H
#define BERTRAND_PYTHON_COMPLEX_H

#include "common.h"


namespace bertrand {
namespace py {


/* Represents a statically-typed Python complex number in C++. */
class Complex : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        return impl::complex_like<T>;
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();
        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyComplex_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Complex(Handle h, const borrowed_t& t) : Base(h, t) {}
    Complex(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (typecheck<T>())
    Complex(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Complex(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Complex>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to 0+0j. */
    Complex() : Base(PyComplex_FromDoubles(0.0, 0.0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert a double into a py::Complex object as its real component. */
    explicit Complex(double real) :
        Base(PyComplex_FromDoubles(real, 0.0), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert a pair of doubles representing separate real, imaginary
    components into a py::Complex object. */
    explicit Complex(double real, double imag) :
        Base(PyComplex_FromDoubles(real, imag), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly convert any C++ type that implements `.real()` and `.imag()` into
    a py::Complex object. */
    template <impl::cpp_like T> requires (impl::complex_like<T>)
    Complex(const T& value) : Base(
        PyComplex_FromDoubles(value.real(), value.imag()),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert an arbitrary Python object into a complex number. */
    template <impl::python_like T> requires (!impl::complex_like<T>)
    explicit Complex(const T& obj) : Base(nullptr, stolen_t{}) {
        Py_complex complex = PyComplex_AsCComplex(obj.ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
        }
        m_ptr = PyComplex_FromDoubles(complex.real, complex.imag);
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Trigger explicit conversions to std::complex<double>. */
    template <impl::cpp_like T>
        requires (
            !impl::complex_like<T> &&
            impl::explicitly_convertible_to<T, std::complex<double>>
        )
    explicit Complex(const T& value) :
        Complex(static_cast<std::complex<double>>(value))
    {}

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the real part of the Complex number. */
    [[nodiscard]] double real() const noexcept {
        return PyComplex_RealAsDouble(this->ptr());
    }

    /* Get the imaginary part of the Complex number. */
    [[nodiscard]] double imag() const noexcept {
        return PyComplex_ImagAsDouble(this->ptr());
    }

    /* Get the magnitude of the Complex number. */
    [[nodiscard]] Complex conjugate() const {
        Py_complex complex = PyComplex_AsCComplex(this->ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
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

    /* Convert a py::Complex value into a Python object. */
    static handle cast(
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
        static handle cast(                                                             \
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
