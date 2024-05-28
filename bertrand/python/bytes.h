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

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "capitalize", Args...>)
    [[nodiscard]] decltype(auto) capitalize(this const Self& self, Args&&... args) {
        return impl::call_method<"capitalize">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "center", Args...>)
    [[nodiscard]] decltype(auto) center(this const Self& self, Args&&... args) {
        return impl::call_method<"center">(self, std::forward<Args>(args)...);
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

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "decode", Args...>)
    [[nodiscard]] decltype(auto) decode(this const Self& self, Args&&... args) {
        return impl::call_method<"decode">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "endswith", Args...>)
    [[nodiscard]] decltype(auto) endswith(this const Self& self, Args&&... args) {
        return impl::call_method<"endswith">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "expandtabs", Args...>)
    [[nodiscard]] decltype(auto) expandtabs(this const Self& self, Args&&... args) {
        return impl::call_method<"expandtabs">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "find", Args...>)
    [[nodiscard]] decltype(auto) find(this const Self& self, Args&&... args) {
        return impl::call_method<"find">(self, std::forward<Args>(args)...);
    }

    template <typename... Args>
        requires (impl::invocable<Bytes, "fromhex", Args...>)
    [[nodiscard]] static decltype(auto) fromhex(Args&&... args) {
        return impl::call_static<Bytes, "fromhex">(std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "hex", Args...>)
    [[nodiscard]] decltype(auto) hex(this const Self& self, Args&&... args) {
        return impl::call_method<"hex">(self, std::forward<Args>(args)...);
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

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isalnum", Args...>)
    [[nodiscard]] decltype(auto) isalnum(this const Self& self, Args&&... args) {
        return impl::call_method<"isalnum">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isalpha", Args...>)
    [[nodiscard]] decltype(auto) isalpha(this const Self& self, Args&&... args) {
        return impl::call_method<"isalpha">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isascii", Args...>)
    [[nodiscard]] decltype(auto) isascii(this const Self& self, Args&&... args) {
        return impl::call_method<"isascii">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isdigit", Args...>)
    [[nodiscard]] decltype(auto) isdigit(this const Self& self, Args&&... args) {
        return impl::call_method<"isdigit">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "islower", Args...>)
    [[nodiscard]] decltype(auto) islower(this const Self& self, Args&&... args) {
        return impl::call_method<"islower">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isspace", Args...>)
    [[nodiscard]] decltype(auto) isspace(this const Self& self, Args&&... args) {
        return impl::call_method<"isspace">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "istitle", Args...>)
    [[nodiscard]] decltype(auto) istitle(this const Self& self, Args&&... args) {
        return impl::call_method<"istitle">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isupper", Args...>)
    [[nodiscard]] decltype(auto) isupper(this const Self& self, Args&&... args) {
        return impl::call_method<"isupper">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "join", Args...>)
    [[nodiscard]] decltype(auto) join(this const Self& self, Args&&... args) {
        return impl::call_method<"join">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "ljust", Args...>)
    [[nodiscard]] decltype(auto) ljust(this const Self& self, Args&&... args) {
        return impl::call_method<"ljust">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "lower", Args...>)
    [[nodiscard]] decltype(auto) lower(this const Self& self, Args&&... args) {
        return impl::call_method<"lower">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "lstrip", Args...>)
    [[nodiscard]] decltype(auto) lstrip(this const Self& self, Args&&... args) {
        return impl::call_method<"lstrip">(self, std::forward<Args>(args)...);
    }

    template <typename... Args>
        requires (impl::invocable<Bytes, "maketrans", Args...>)
    [[nodiscard]] static decltype(auto) maketrans(Args&&... args) {
        return impl::call_static<Bytes, "maketrans">(std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "partition", Args...>)
    [[nodiscard]] decltype(auto) partition(this const Self& self, Args&&... args) {
        return impl::call_method<"partition">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "removeprefix", Args...>)
    [[nodiscard]] decltype(auto) removeprefix(this const Self& self, Args&&... args) {
        return impl::call_method<"removeprefix">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "removesuffix", Args...>)
    [[nodiscard]] decltype(auto) removesuffix(this const Self& self, Args&&... args) {
        return impl::call_method<"removesuffix">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "replace", Args...>)
    [[nodiscard]] decltype(auto) replace(this const Self& self, Args&&... args) {
        return impl::call_method<"replace">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rfind", Args...>)
    [[nodiscard]] decltype(auto) rfind(this const Self& self, Args&&... args) {
        return impl::call_method<"rfind">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rindex", Args...>)
    [[nodiscard]] decltype(auto) rindex(this const Self& self, Args&&... args) {
        return impl::call_method<"rindex">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rjust", Args...>)
    [[nodiscard]] decltype(auto) rjust(this const Self& self, Args&&... args) {
        return impl::call_method<"rjust">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rpartition", Args...>)
    [[nodiscard]] decltype(auto) rpartition(this const Self& self, Args&&... args) {
        return impl::call_method<"rpartition">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rsplit", Args...>)
    [[nodiscard]] decltype(auto) rsplit(this const Self& self, Args&&... args) {
        return impl::call_method<"rsplit">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rstrip", Args...>)
    [[nodiscard]] decltype(auto) rstrip(this const Self& self, Args&&... args) {
        return impl::call_method<"rstrip">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "split", Args...>)
    [[nodiscard]] decltype(auto) split(this const Self& self, Args&&... args) {
        return impl::call_method<"split">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "splitlines", Args...>)
    [[nodiscard]] decltype(auto) splitlines(this const Self& self, Args&&... args) {
        return impl::call_method<"splitlines">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "startswith", Args...>)
    [[nodiscard]] decltype(auto) startswith(this const Self& self, Args&&... args) {
        return impl::call_method<"startswith">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "strip", Args...>)
    [[nodiscard]] decltype(auto) strip(this const Self& self, Args&&... args) {
        return impl::call_method<"strip">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "swapcase", Args...>)
    [[nodiscard]] decltype(auto) swapcase(this const Self& self, Args&&... args) {
        return impl::call_method<"swapcase">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "title", Args...>)
    [[nodiscard]] decltype(auto) title(this const Self& self, Args&&... args) {
        return impl::call_method<"title">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "translate", Args...>)
    [[nodiscard]] decltype(auto) translate(this const Self& self, Args&&... args) {
        return impl::call_method<"translate">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "upper", Args...>)
    [[nodiscard]] decltype(auto) upper(this const Self& self, Args&&... args) {
        return impl::call_method<"upper">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "zfill", Args...>)
    [[nodiscard]] decltype(auto) zfill(this const Self& self, Args&&... args) {
        return impl::call_method<"zfill">(self, std::forward<Args>(args)...);
    }

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
        PyByteArray_FromStringAndSize(nullptr, size),
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

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "capitalize", Args...>)
    [[nodiscard]] decltype(auto) capitalize(this const Self& self, Args&&... args) {
        return impl::call_method<"capitalize">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "center", Args...>)
    [[nodiscard]] decltype(auto) center(this const Self& self, Args&&... args) {
        return impl::call_method<"center">(self, std::forward<Args>(args)...);
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

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "decode", Args...>)
    [[nodiscard]] decltype(auto) decode(this const Self& self, Args&&... args) {
        return impl::call_method<"decode">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "endswith", Args...>)
    [[nodiscard]] decltype(auto) endswith(this const Self& self, Args&&... args) {
        return impl::call_method<"endswith">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "expandtabs", Args...>)
    [[nodiscard]] decltype(auto) expandtabs(this const Self& self, Args&&... args) {
        return impl::call_method<"expandtabs">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "find", Args...>)
    [[nodiscard]] decltype(auto) find(this const Self& self, Args&&... args) {
        return impl::call_method<"find">(self, std::forward<Args>(args)...);
    }

    template <typename... Args>
        requires (impl::invocable<ByteArray, "fromhex", Args...>)
    [[nodiscard]] static decltype(auto) fromhex(Args&&... args) {
        return impl::call_static<ByteArray, "fromhex">(std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "hex", Args...>)
    [[nodiscard]] decltype(auto) hex(this const Self& self, Args&&... args) {
        return impl::call_method<"hex">(self, std::forward<Args>(args)...);
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

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isalnum", Args...>)
    [[nodiscard]] decltype(auto) isalnum(this const Self& self, Args&&... args) {
        return impl::call_method<"isalnum">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isalpha", Args...>)
    [[nodiscard]] decltype(auto) isalpha(this const Self& self, Args&&... args) {
        return impl::call_method<"isalpha">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isascii", Args...>)
    [[nodiscard]] decltype(auto) isascii(this const Self& self, Args&&... args) {
        return impl::call_method<"isascii">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isdigit", Args...>)
    [[nodiscard]] decltype(auto) isdigit(this const Self& self, Args&&... args) {
        return impl::call_method<"isdigit">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "islower", Args...>)
    [[nodiscard]] decltype(auto) islower(this const Self& self, Args&&... args) {
        return impl::call_method<"islower">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isspace", Args...>)
    [[nodiscard]] decltype(auto) isspace(this const Self& self, Args&&... args) {
        return impl::call_method<"isspace">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "istitle", Args...>)
    [[nodiscard]] decltype(auto) istitle(this const Self& self, Args&&... args) {
        return impl::call_method<"istitle">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "isupper", Args...>)
    [[nodiscard]] decltype(auto) isupper(this const Self& self, Args&&... args) {
        return impl::call_method<"isupper">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "join", Args...>)
    [[nodiscard]] decltype(auto) join(this const Self& self, Args&&... args) {
        return impl::call_method<"join">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "ljust", Args...>)
    [[nodiscard]] decltype(auto) ljust(this const Self& self, Args&&... args) {
        return impl::call_method<"ljust">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "lower", Args...>)
    [[nodiscard]] decltype(auto) lower(this const Self& self, Args&&... args) {
        return impl::call_method<"lower">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "lstrip", Args...>)
    [[nodiscard]] decltype(auto) lstrip(this const Self& self, Args&&... args) {
        return impl::call_method<"lstrip">(self, std::forward<Args>(args)...);
    }

    template <typename... Args>
        requires (impl::invocable<ByteArray, "maketrans", Args...>)
    [[nodiscard]] static decltype(auto) maketrans(Args&&... args) {
        return impl::call_static<ByteArray, "maketrans">(std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "partition", Args...>)
    [[nodiscard]] decltype(auto) partition(this const Self& self, Args&&... args) {
        return impl::call_method<"partition">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "removeprefix", Args...>)
    [[nodiscard]] decltype(auto) removeprefix(this const Self& self, Args&&... args) {
        return impl::call_method<"removeprefix">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "removesuffix", Args...>)
    [[nodiscard]] decltype(auto) removesuffix(this const Self& self, Args&&... args) {
        return impl::call_method<"removesuffix">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "replace", Args...>)
    [[nodiscard]] decltype(auto) replace(this const Self& self, Args&&... args) {
        return impl::call_method<"replace">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rfind", Args...>)
    [[nodiscard]] decltype(auto) rfind(this const Self& self, Args&&... args) {
        return impl::call_method<"rfind">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rindex", Args...>)
    [[nodiscard]] decltype(auto) rindex(this const Self& self, Args&&... args) {
        return impl::call_method<"rindex">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rjust", Args...>)
    [[nodiscard]] decltype(auto) rjust(this const Self& self, Args&&... args) {
        return impl::call_method<"rjust">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rpartition", Args...>)
    [[nodiscard]] decltype(auto) rpartition(this const Self& self, Args&&... args) {
        return impl::call_method<"rpartition">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rsplit", Args...>)
    [[nodiscard]] decltype(auto) rsplit(this const Self& self, Args&&... args) {
        return impl::call_method<"rsplit">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "rstrip", Args...>)
    [[nodiscard]] decltype(auto) rstrip(this const Self& self, Args&&... args) {
        return impl::call_method<"rstrip">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "split", Args...>)
    [[nodiscard]] decltype(auto) split(this const Self& self, Args&&... args) {
        return impl::call_method<"split">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "splitlines", Args...>)
    [[nodiscard]] decltype(auto) splitlines(this const Self& self, Args&&... args) {
        return impl::call_method<"splitlines">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "startswith", Args...>)
    [[nodiscard]] decltype(auto) startswith(this const Self& self, Args&&... args) {
        return impl::call_method<"startswith">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "strip", Args...>)
    [[nodiscard]] decltype(auto) strip(this const Self& self, Args&&... args) {
        return impl::call_method<"strip">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "swapcase", Args...>)
    [[nodiscard]] decltype(auto) swapcase(this const Self& self, Args&&... args) {
        return impl::call_method<"swapcase">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "title", Args...>)
    [[nodiscard]] decltype(auto) title(this const Self& self, Args&&... args) {
        return impl::call_method<"title">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "translate", Args...>)
    [[nodiscard]] decltype(auto) translate(this const Self& self, Args&&... args) {
        return impl::call_method<"translate">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "upper", Args...>)
    [[nodiscard]] decltype(auto) upper(this const Self& self, Args&&... args) {
        return impl::call_method<"upper">(self, std::forward<Args>(args)...);
    }

    template <typename Self, typename... Args>
        requires (impl::invocable<Self, "zfill", Args...>)
    [[nodiscard]] decltype(auto) zfill(this const Self& self, Args&&... args) {
        return impl::call_method<"zfill">(self, std::forward<Args>(args)...);
    }

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
