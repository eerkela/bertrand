#ifndef BERTRAND_PYTHON_INT_H
#define BERTRAND_PYTHON_INT_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::int_ that enables conversions from strings with different
bases, similar to Python's `int()` constructor, as well as converting math operators
that account for C++ inputs. */
struct Int : public Object, public impl::Ops<Int> {
    static py::Type Type;
    BERTRAND_PYTHON_OPERATORS(impl::Ops<Int>)
    BERTRAND_PYTHON_CONSTRUCTORS(Object, Int, PyLong_Check, PyNumber_Long)

    /* Default constructor.  Initializes to 0. */
    Int() : Object(PyLong_FromLong(0), stolen_t{}) {}

    /* Implicitly convert a C++ integer into a Python int. */
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    Int(T value) {
        if constexpr (sizeof(T) <= sizeof(long)) {
            if constexpr (std::is_signed_v<T>) {
                m_ptr = PyLong_FromLong(value);
            } else {
                m_ptr = PyLong_FromUnsignedLong(value);
            }
        } else {
            if constexpr (std::is_signed_v<T>) {
                m_ptr = PyLong_FromLongLong(value);
            } else {
                m_ptr = PyLong_FromUnsignedLongLong(value);
            }
        }
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct an Int from a floating point value. */
    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    explicit Int(T value) {
        m_ptr = PyLong_FromDouble(value);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct an Int from a string with an optional base. */
    explicit Int(const char* str, int base = 0) {
        m_ptr = PyLong_FromString(str, nullptr, base);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct an Int from a string with an optional base. */
    explicit Int(const std::string& str, int base = 0) : Int(str.c_str(), base) {}

    /* Construct an Int from a string with an optional base. */
    explicit Int(const std::string_view& str, int base = 0) : Int(str.data(), base) {}

    /* Construct an Int from a string with an optional base. */
    explicit Int(const py::Str& str, int base = 0);  // defined in str.h

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a Python int into a C++ integer. */
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    inline operator T() const {
        if constexpr (sizeof(T) <= sizeof(long)) {
            if constexpr (std::is_signed_v<T>) {
                return PyLong_AsLong(m_ptr);
            } else {
                return PyLong_AsUnsignedLong(m_ptr);
            }
        } else {
            if constexpr (std::is_signed_v<T>) {
                return PyLong_AsLongLong(m_ptr);
            } else {
                return PyLong_AsUnsignedLongLong(m_ptr);
            }
        }
    }

    /* Implicitly convert a Python int into a C++ float. */
    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    inline operator T() const {
        return PyLong_AsDouble(m_ptr);
    }

    /* Allow explicit conversion to any type. */
    template <typename T, std::enable_if_t<!std::is_arithmetic_v<T>, int> = 0>
    inline explicit operator T() const {
        return Object::operator T();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Using Python-style true division for the in-place /= operator is not type-safe
     * in C++, since Python implicitly converts the result to a float and reassigns it
     * to the original variable.  If we replicated this in C++, it would make it
     * possible to accidentally create py::Int wrappers around floating point values,
     * which violates type safety.  As such, I have chosen to use C-style division for
     * the /= operator, which guarantees that the result is always an integer.  This
     * might be slightly surprising to Python users, but it is the only way to ensure
     * that the python wrapper always matches the underlying C++ type.  The standard
     * `/` operator is still available for true division if needed.
     */

    template <typename T>
    inline Int& operator/=(const T& other) {
        Object obj = detail::object_or_cast(other);

        py::print("hello, world!");

        if (PyLong_Check(obj.ptr())) {
            PyObject* temp = PyNumber_FloorDivide(this->ptr(), obj.ptr());
            if (temp == nullptr) {
                throw error_already_set();
            }
            Int result = reinterpret_steal<Int>(temp);
            if (result < 0) {
                result += Int(*this - (result * reinterpret_borrow<Int>(obj.ptr()))) != 0;
            }
            *this = result;
            return *this;
        }

        return impl::Ops<Int>::operator/=(obj);
    }

};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Int)


#endif  // BERTRAND_PYTHON_INT_H
