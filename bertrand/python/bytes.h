#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include bertrand/python.h instead."
#endif

#ifndef BERTRAND_PYTHON_BYTES_H
#define BERTRAND_PYTHON_BYTES_H

#include "common.h"
#include "int.h"
#include "str.h"
#include "list.h"
#include "tuple.h"
#include "set.h"
#include "dict.h"


namespace bertrand {
namespace py {


/////////////////////
////    BYTES    ////
/////////////////////


/* Represents a statically-typed Python `bytes` object in C++. */
class Bytes : public Object {
    using Base = Object;
    using Self = Bytes;

public:
    static const Type type;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        return impl::bytes_like<T>;
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyBytes_Check(obj.ptr());

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an uninitialized string with the given
    size, defaulting to zero. */
    Bytes(size_t size = 0) :
        Base(PyBytes_FromStringAndSize(nullptr, size), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Reinterpret_borrow/reinterpret_steal constructors. */
    Bytes(Handle h, const borrowed_t& t) : Base(h, t) {}
    Bytes(Handle h, const stolen_t& t) : Base(h, t) {}

    /* Convert an equivalent pybind11 type into a py::Bytes. */
    template <impl::pybind11_like T> requires (typecheck<T>())
    Bytes(T&& other) : Base(std::forward<T>(other)) {}

    /* Unwrap a pybind11 accessor into a py::Bytes. */
    template <typename Policy>
    Bytes(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Bytes>(accessor).release(), stolen_t{})
    {}

    /* Implicitly convert a raw sequence of bytes into a py::Bytes object. */
    Bytes(const void* data, size_t size) :
        Base(PyBytes_FromStringAndSize(static_cast<const char*>(data), size), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly convert a string literal into a py::Bytes object.  Note that this
    interprets the string as raw binary data rather than text, and it automatically
    excludes the terminating null byte.  This is primary useful for hardcoded binary
    data, rather than as a replacement for py::Str. */
    template <size_t N>
    Bytes(const char (&string)[N]) :
        Base(PyBytes_FromStringAndSize(string, N - 1), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Get a bytes representation of a Python object that implements the buffer
    protocol. */
    template <impl::python_like T> requires (!impl::bytes_like<T>)
    explicit Bytes(const T& obj) : Base(PyBytes_FromObject(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Access the internal buffer of the bytes object.  Note that this implicitly
    includes an extra null byte at the end of the buffer, regardless of any nulls that
    were already present.  The data should not be modified unless the bytes object was
    just created via `py::Bytes(size)`, and it should never be deallocated. */
    [[nodiscard]] char* data() const {
        return PyBytes_AS_STRING(this->ptr());
    }

    /* Make an explicit copy of the byte string. */
    [[nodiscard]] auto copy() const {
        PyObject* result = PyBytes_FromStringAndSize(
            PyBytes_AS_STRING(this->ptr()),
            PyBytes_GET_SIZE(this->ptr())
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    BERTRAND_METHOD([[nodiscard]], capitalize, const)
    BERTRAND_METHOD([[nodiscard]], center, const)

    [[nodiscard]] Py_ssize_t count(
        const Bytes& sub,
        Py_ssize_t start = 0,
        Py_ssize_t end = -1
    ) const {
        if (start != 0 || end != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, end);
            if (slice == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t result = PySequence_Count(slice, sub.ptr());
            Py_DECREF(slice);
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Count(this->ptr(), sub.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    BERTRAND_METHOD([[nodiscard]], decode, const)
    BERTRAND_METHOD([[nodiscard]], endswith, const)
    BERTRAND_METHOD([[nodiscard]], expandtabs, const)
    BERTRAND_METHOD([[nodiscard]], find, const)
    BERTRAND_STATIC_METHOD([[nodiscard]], fromhex)
    BERTRAND_METHOD([[nodiscard]], hex, const)

    [[nodiscard]] Py_ssize_t index(
        const Bytes& sub,
        Py_ssize_t start = 0,
        Py_ssize_t end = -1
    ) const {
        if (start != 0 || end != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, end);
            if (slice == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t result = PySequence_Index(slice, sub.ptr());
            Py_DECREF(slice);
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Index(this->ptr(), sub.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    BERTRAND_METHOD([[nodiscard]], isalnum, const)
    BERTRAND_METHOD([[nodiscard]], isalpha, const)
    BERTRAND_METHOD([[nodiscard]], isascii, const)
    BERTRAND_METHOD([[nodiscard]], isdigit, const)
    BERTRAND_METHOD([[nodiscard]], islower, const)
    BERTRAND_METHOD([[nodiscard]], isspace, const)
    BERTRAND_METHOD([[nodiscard]], istitle, const)
    BERTRAND_METHOD([[nodiscard]], isupper, const)
    BERTRAND_METHOD([[nodiscard]], join, const)
    BERTRAND_METHOD([[nodiscard]], ljust, const)
    BERTRAND_METHOD([[nodiscard]], lower, const)
    BERTRAND_METHOD([[nodiscard]], lstrip, const)
    BERTRAND_STATIC_METHOD([[nodiscard]], maketrans)
    BERTRAND_METHOD([[nodiscard]], partition, const)
    BERTRAND_METHOD([[nodiscard]], removeprefix, const)
    BERTRAND_METHOD([[nodiscard]], removesuffix, const)
    BERTRAND_METHOD([[nodiscard]], replace, const)
    BERTRAND_METHOD([[nodiscard]], rfind, const)
    BERTRAND_METHOD([[nodiscard]], rindex, const)
    BERTRAND_METHOD([[nodiscard]], rjust, const)
    BERTRAND_METHOD([[nodiscard]], rpartition, const)
    BERTRAND_METHOD([[nodiscard]], rsplit, const)
    BERTRAND_METHOD([[nodiscard]], rstrip, const)
    BERTRAND_METHOD([[nodiscard]], split, const)
    BERTRAND_METHOD([[nodiscard]], splitlines, const)
    BERTRAND_METHOD([[nodiscard]], startswith, const)
    BERTRAND_METHOD([[nodiscard]], strip, const)
    BERTRAND_METHOD([[nodiscard]], swapcase, const)
    BERTRAND_METHOD([[nodiscard]], title, const)
    BERTRAND_METHOD([[nodiscard]], translate, const)
    BERTRAND_METHOD([[nodiscard]], upper, const)
    BERTRAND_METHOD([[nodiscard]], zfill, const)

};


namespace ops {

    template <typename Return, std::derived_from<Bytes> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return PyBytes_GET_SIZE(self.ptr());
        }
    };

    template <typename Return, std::derived_from<Bytes> L, typename R>
    struct iadd<Return, L, R> {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = lhs.ptr();
            PyBytes_Concat(&result, Bytes(rhs).ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, Bytes> || std::derived_from<R, Bytes>)
    struct add<Return, L, R> {
        static Return operator()(const auto& lhs, const auto& rhs) {
            if constexpr (std::derived_from<L, Bytes>) {
                Return result = lhs.copy();
                iadd<Return, L, R>{}(result, rhs);
                return result;
            } else {
                Return result = rhs.copy();
                iadd<Return, L, R>{}(result, lhs);
                return result;
            }
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, Bytes> || std::derived_from<R, Bytes>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<Bytes> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

}


/////////////////////////
////    BYTEARRAY    ////
/////////////////////////


/* Represents a statically-typed Python `bytearray` in C++. */
class ByteArray : public Object {
    using Base = Object;
    using Self = ByteArray;

public:
    static const Type type;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        return impl::bytearray_like<T>;
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if (impl::cpp_like<T>) {
            return typecheck<T>();
        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyByteArray_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an uninitialized string with the given
    size, defaulting to zero. */
    ByteArray(size_t size = 0) : Base(
        PyByteArray_FromStringAndSize(nullptr, size),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Reinterpret_borrow/reinterpret_steal constructors. */
    ByteArray(Handle h, const borrowed_t& t) : Base(h, t) {}
    ByteArray(Handle h, const stolen_t& t) : Base(h, t) {}

    /* Convert an equivalent pybind11 type into a py::ByteArray. */
    template <impl::pybind11_like T> requires (typecheck<T>())
    ByteArray(T&& other) : Base(std::forward<T>(other)) {}

    /* Unwrap a pybind11 accessor into a py::ByteArray. */
    template <typename Policy>
    ByteArray(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<ByteArray>(accessor).release(), stolen_t{})
    {}

    /* Implicitly convert a raw sequence of bytes into a py::ByteArray object. */
    ByteArray(const void* data, size_t size) : Base(
        PyByteArray_FromStringAndSize(static_cast<const char*>(data), size),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly convert a string literal into a py::ByteArray object.  Note that this
    interprets the string as raw binary data rather than text, and it automatically
    excludes the terminating null byte.  This is primary useful for hardcoded binary
    data, rather than as a replacement for py::Str. */
    template <size_t N>
    ByteArray(const char (&string)[N]) : Base(
        PyByteArray_FromStringAndSize(string, N - 1),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Get a bytes representation of a Python object that implements the buffer
    protocol. */
    template <impl::python_like T> requires (!impl::bytearray_like<T>)
    explicit ByteArray(const T& obj) :
        Base(PyByteArray_FromObject(obj.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Access the internal buffer of the bytearray object.  The data can be modified
    in-place, but should never be deallocated. */
    [[nodiscard]] char* data() const {
        return PyByteArray_AS_STRING(this->ptr());
    }

    /* Make an explicit copy of the byte array. */
    [[nodiscard]] auto copy() const {
        PyObject* result = PyByteArray_FromStringAndSize(
            PyByteArray_AS_STRING(this->ptr()),
            PyByteArray_GET_SIZE(this->ptr())
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    BERTRAND_METHOD([[nodiscard]], capitalize, const)
    BERTRAND_METHOD([[nodiscard]], center, const)

    [[nodiscard]] Py_ssize_t count(
        const ByteArray& sub,
        Py_ssize_t start = 0,
        Py_ssize_t end = -1
    ) const {
        if (start != 0 || end != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, end);
            if (slice == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t result = PySequence_Count(slice, sub.ptr());
            Py_DECREF(slice);
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Count(this->ptr(), sub.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    BERTRAND_METHOD([[nodiscard]], decode, const)
    BERTRAND_METHOD([[nodiscard]], endswith, const)
    BERTRAND_METHOD([[nodiscard]], expandtabs, const)
    BERTRAND_METHOD([[nodiscard]], find, const)
    BERTRAND_STATIC_METHOD([[nodiscard]], fromhex)
    BERTRAND_METHOD([[nodiscard]], hex, const)

    [[nodiscard]] Py_ssize_t index(
        const ByteArray& sub,
        Py_ssize_t start = 0,
        Py_ssize_t end = -1
    ) const {
        if (start != 0 || end != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, end);
            if (slice == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t result = PySequence_Index(slice, sub.ptr());
            Py_DECREF(slice);
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Index(this->ptr(), sub.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    BERTRAND_METHOD([[nodiscard]], isalnum, const)
    BERTRAND_METHOD([[nodiscard]], isalpha, const)
    BERTRAND_METHOD([[nodiscard]], isascii, const)
    BERTRAND_METHOD([[nodiscard]], isdigit, const)
    BERTRAND_METHOD([[nodiscard]], islower, const)
    BERTRAND_METHOD([[nodiscard]], isspace, const)
    BERTRAND_METHOD([[nodiscard]], istitle, const)
    BERTRAND_METHOD([[nodiscard]], isupper, const)
    BERTRAND_METHOD([[nodiscard]], join, const)
    BERTRAND_METHOD([[nodiscard]], ljust, const)
    BERTRAND_METHOD([[nodiscard]], lower, const)
    BERTRAND_METHOD([[nodiscard]], lstrip, const)
    BERTRAND_STATIC_METHOD([[nodiscard]], maketrans)
    BERTRAND_METHOD([[nodiscard]], partition, const)
    BERTRAND_METHOD([[nodiscard]], removeprefix, const)
    BERTRAND_METHOD([[nodiscard]], removesuffix, const)
    BERTRAND_METHOD([[nodiscard]], replace, const)
    BERTRAND_METHOD([[nodiscard]], rfind, const)
    BERTRAND_METHOD([[nodiscard]], rindex, const)
    BERTRAND_METHOD([[nodiscard]], rjust, const)
    BERTRAND_METHOD([[nodiscard]], rpartition, const)
    BERTRAND_METHOD([[nodiscard]], rsplit, const)
    BERTRAND_METHOD([[nodiscard]], rstrip, const)
    BERTRAND_METHOD([[nodiscard]], split, const)
    BERTRAND_METHOD([[nodiscard]], splitlines, const)
    BERTRAND_METHOD([[nodiscard]], startswith, const)
    BERTRAND_METHOD([[nodiscard]], strip, const)
    BERTRAND_METHOD([[nodiscard]], swapcase, const)
    BERTRAND_METHOD([[nodiscard]], title, const)
    BERTRAND_METHOD([[nodiscard]], translate, const)
    BERTRAND_METHOD([[nodiscard]], upper, const)
    BERTRAND_METHOD([[nodiscard]], zfill, const)

};


namespace ops {

    template <typename Return, std::derived_from<ByteArray> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return PyByteArray_GET_SIZE(self.ptr());
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, ByteArray> || std::derived_from<R, ByteArray>)
    struct add<Return, L, R> {
        static Return operator()(const ByteArray& lhs, const ByteArray& rhs) {
            PyObject* result = PyByteArray_Concat(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, std::derived_from<ByteArray> L, typename R>
    struct iadd<Return, L, R> {
        static void operator()(L& lhs, const auto& rhs) {
            lhs = add<Return, L, R>{}(lhs, rhs);
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, ByteArray> || std::derived_from<R, ByteArray>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<ByteArray> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_BYTES_H
