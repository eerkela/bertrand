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


/* Represents a statically-typed Python `bytes` object in C++. */
class Bytes : public Object {
    using Base = Object;

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

    Bytes(Handle h, const borrowed_t& t) : Base(h, t) {}
    Bytes(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (typecheck<T>())
    Bytes(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Bytes(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Bytes>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to an empty string with zero bytes. */
    Bytes(size_t size = 0) :
        Base(PyBytes_FromStringAndSize(nullptr, size), stolen_t{})
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

    /* Implicitly convert a raw sequence of bytes into a py::Bytes object. */
    Bytes(const void* data, size_t size) :
        Base(PyBytes_FromStringAndSize(static_cast<const char*>(data), size), stolen_t{})
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

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Access the internal buffer of the bytes object.  Note that this implicitly
    includes an extra null byte at the end of the buffer, regardless of any nulls that
    were already present.  The data should not be modified unless the bytes object was
    just created via `py::Bytes(size)`, and it should never be deallocated. */
    [[nodiscard]] char* data() const {
        return PyBytes_AS_STRING(this->ptr());
    }

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

    // TODO: need to find a way to mark methods as const, and potentially add a
    // [[nodiscard]] attribute in front of the definition.

    BERTRAND_NODISCARD_CONST_METHOD(capitalize)
    BERTRAND_NODISCARD_CONST_METHOD(center)
    BERTRAND_NODISCARD_CONST_METHOD(decode)
    BERTRAND_NODISCARD_CONST_METHOD(endswith)
    BERTRAND_NODISCARD_CONST_METHOD(expandtabs)
    BERTRAND_NODISCARD_CONST_METHOD(find)
    BERTRAND_NODISCARD_STATIC_METHOD(Bytes, fromhex)
    BERTRAND_NODISCARD_CONST_METHOD(hex)
    BERTRAND_NODISCARD_CONST_METHOD(isalnum)
    BERTRAND_NODISCARD_CONST_METHOD(isalpha)
    BERTRAND_NODISCARD_CONST_METHOD(isascii)
    BERTRAND_NODISCARD_CONST_METHOD(isdigit)
    BERTRAND_NODISCARD_CONST_METHOD(islower)
    BERTRAND_NODISCARD_CONST_METHOD(isspace)
    BERTRAND_NODISCARD_CONST_METHOD(istitle)
    BERTRAND_NODISCARD_CONST_METHOD(isupper)
    BERTRAND_NODISCARD_CONST_METHOD(join)
    BERTRAND_NODISCARD_CONST_METHOD(ljust)
    BERTRAND_NODISCARD_CONST_METHOD(lower)
    BERTRAND_NODISCARD_CONST_METHOD(lstrip)
    BERTRAND_NODISCARD_STATIC_METHOD(Bytes, maketrans)
    BERTRAND_NODISCARD_CONST_METHOD(partition)
    BERTRAND_NODISCARD_CONST_METHOD(removeprefix)
    BERTRAND_NODISCARD_CONST_METHOD(removesuffix)
    BERTRAND_NODISCARD_CONST_METHOD(replace)
    BERTRAND_NODISCARD_CONST_METHOD(rfind)
    BERTRAND_NODISCARD_CONST_METHOD(rindex)
    BERTRAND_NODISCARD_CONST_METHOD(rjust)
    BERTRAND_NODISCARD_CONST_METHOD(rpartition)
    BERTRAND_NODISCARD_CONST_METHOD(rsplit)
    BERTRAND_NODISCARD_CONST_METHOD(rstrip)
    BERTRAND_NODISCARD_CONST_METHOD(split)
    BERTRAND_NODISCARD_CONST_METHOD(splitlines)
    BERTRAND_NODISCARD_CONST_METHOD(startswith)
    BERTRAND_NODISCARD_CONST_METHOD(strip)
    BERTRAND_NODISCARD_CONST_METHOD(swapcase)
    BERTRAND_NODISCARD_CONST_METHOD(title)
    BERTRAND_NODISCARD_CONST_METHOD(translate)
    BERTRAND_NODISCARD_CONST_METHOD(upper)
    BERTRAND_NODISCARD_CONST_METHOD(zfill)

};


/* Represents a statically-typed Python `bytearray` in C++. */
class ByteArray : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool typecheck() {
        return impl::bytearray_like<T>;
    }

    template <typename T>
    static constexpr bool typecheck(const T& obj) {
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

    ByteArray(Handle h, const borrowed_t& t) : Base(h, t) {}
    ByteArray(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (typecheck<T>())
    ByteArray(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    ByteArray(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<ByteArray>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to an empty string with zero bytes. */
    ByteArray(size_t size = 0) : Base(
        PyBytes_FromStringAndSize(nullptr, size),
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

    /* Implicitly convert a raw sequence of bytes into a py::ByteArray object. */
    ByteArray(const void* data, size_t size) : Base(
        PyByteArray_FromStringAndSize(static_cast<const char*>(data), size),
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

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Access the internal buffer of the bytearray object.  The data can be modified
    in-place, but should never be deallocated. */
    [[nodiscard]] char* data() const {
        return PyByteArray_AS_STRING(this->ptr());
    }

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

    BERTRAND_NODISCARD_CONST_METHOD(capitalize)
    BERTRAND_NODISCARD_CONST_METHOD(center)
    BERTRAND_NODISCARD_CONST_METHOD(decode)
    BERTRAND_NODISCARD_CONST_METHOD(endswith)
    BERTRAND_NODISCARD_CONST_METHOD(expandtabs)
    BERTRAND_NODISCARD_CONST_METHOD(find)
    BERTRAND_NODISCARD_STATIC_METHOD(ByteArray, fromhex)
    BERTRAND_NODISCARD_CONST_METHOD(hex)
    BERTRAND_NODISCARD_CONST_METHOD(isalnum)
    BERTRAND_NODISCARD_CONST_METHOD(isalpha)
    BERTRAND_NODISCARD_CONST_METHOD(isascii)
    BERTRAND_NODISCARD_CONST_METHOD(isdigit)
    BERTRAND_NODISCARD_CONST_METHOD(islower)
    BERTRAND_NODISCARD_CONST_METHOD(isspace)
    BERTRAND_NODISCARD_CONST_METHOD(istitle)
    BERTRAND_NODISCARD_CONST_METHOD(isupper)
    BERTRAND_NODISCARD_CONST_METHOD(join)
    BERTRAND_NODISCARD_CONST_METHOD(ljust)
    BERTRAND_NODISCARD_CONST_METHOD(lower)
    BERTRAND_NODISCARD_CONST_METHOD(lstrip)
    BERTRAND_NODISCARD_STATIC_METHOD(ByteArray, maketrans)
    BERTRAND_NODISCARD_CONST_METHOD(partition)
    BERTRAND_NODISCARD_CONST_METHOD(removeprefix)
    BERTRAND_NODISCARD_CONST_METHOD(removesuffix)
    BERTRAND_NODISCARD_CONST_METHOD(replace)
    BERTRAND_NODISCARD_CONST_METHOD(rfind)
    BERTRAND_NODISCARD_CONST_METHOD(rindex)
    BERTRAND_NODISCARD_CONST_METHOD(rjust)
    BERTRAND_NODISCARD_CONST_METHOD(rpartition)
    BERTRAND_NODISCARD_CONST_METHOD(rsplit)
    BERTRAND_NODISCARD_CONST_METHOD(rstrip)
    BERTRAND_NODISCARD_CONST_METHOD(split)
    BERTRAND_NODISCARD_CONST_METHOD(splitlines)
    BERTRAND_NODISCARD_CONST_METHOD(startswith)
    BERTRAND_NODISCARD_CONST_METHOD(strip)
    BERTRAND_NODISCARD_CONST_METHOD(swapcase)
    BERTRAND_NODISCARD_CONST_METHOD(title)
    BERTRAND_NODISCARD_CONST_METHOD(translate)
    BERTRAND_NODISCARD_CONST_METHOD(upper)
    BERTRAND_NODISCARD_CONST_METHOD(zfill)

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
                iadd<Return, L, R>::operator()(result, rhs);
                return result;
            } else {
                Return result = rhs.copy();
                iadd<Return, L, R>::operator()(result, lhs);
                return result;
            }
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, Bytes> || std::derived_from<R, Bytes>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<Bytes> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

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
            lhs = add<Return, L, R>::operator()(lhs, rhs);
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
