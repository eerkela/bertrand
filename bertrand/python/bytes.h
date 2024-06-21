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

    Bytes(Handle h, borrowed_t t) : Base(h, t) {}
    Bytes(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Bytes, __init__<Bytes, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Bytes, std::remove_cvref_t<Args>...>::enable
        )
    Bytes(Args&&... args) : Base(
        __init__<Bytes, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    template <typename... Args>
        requires (
            !__init__<Bytes, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Bytes, __explicit_init__<Bytes, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Bytes, std::remove_cvref_t<Args>...>::enable
        )
    explicit Bytes(Args&&... args) : Base(
        __explicit_init__<Bytes, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

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


template <typename T>
struct __issubclass__<T, Bytes>                             : Returns<bool> {
    static consteval bool operator()() {
        return impl::bytes_like<T>;
    }
    static consteval bool operator()(const T& obj) {
        return operator()(obj);
    }
};


template <typename T>
struct __isinstance__<T, Bytes>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Bytes>();
        } else if constexpr (issubclass<T, Bytes>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyBytes_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


template <std::convertible_to<size_t> T>
struct __init__<Bytes, T>                                   : Returns<Bytes> {
    static auto operator()(size_t size) {
        PyObject* result = PyBytes_FromStringAndSize(nullptr, size);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
    }
};
template <>
struct __init__<Bytes>                                      : Returns<Bytes> {
    static auto operator()() { return Bytes(0); }
};


template <>
struct __init__<Bytes, char>                                : Returns<Bytes> {
    static auto operator()(char value) {
        PyObject* result = PyBytes_FromStringAndSize(&value, 1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
    }
};
template <size_t N>
struct __init__<Bytes, char[N]>                             : Returns<Bytes> {
    static auto operator()(const char (&string)[N]) {
        PyObject* result = PyBytes_FromStringAndSize(string, N - 1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
    }
};
template <>
struct __init__<Bytes, const char*>                        : Returns<Bytes> {
    static auto operator()(const char* string) {
        PyObject* result = PyBytes_FromStringAndSize(string, strlen(string));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
    }
};


template <>
struct __init__<Bytes, const void*, size_t>                : Returns<Bytes> {
    static auto operator()(const void* data, size_t size) {
        PyObject* result = PyBytes_FromStringAndSize(static_cast<const char*>(data), size);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
    }
};


template <impl::python_like T> requires (!impl::bytes_like<T>)
struct __explicit_init__<Bytes, T>                          : Returns<Bytes> {
    static auto operator()(const T& obj) {
        PyObject* result = PyBytes_FromObject(obj.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
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

    ByteArray(Handle h, borrowed_t t) : Base(h, t) {}
    ByteArray(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<ByteArray, __init__<ByteArray, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<ByteArray, std::remove_cvref_t<Args>...>::enable
        )
    ByteArray(Args&&... args) : Base(
        __init__<ByteArray, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    template <typename... Args>
        requires (
            !__init__<ByteArray, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<ByteArray, __explicit_init__<ByteArray, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<ByteArray, std::remove_cvref_t<Args>...>::enable
        )
    explicit ByteArray(Args&&... args) : Base(
        __explicit_init__<ByteArray, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

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


template <typename T>
struct __issubclass__<T, ByteArray>                         : Returns<bool> {
    static consteval bool operator()() {
        return impl::bytearray_like<T>;
    }
    static consteval bool operator()(const T& obj) {
        return operator()(obj);
    }
};


template <typename T>
struct __isinstance__<T, ByteArray>                         : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, ByteArray>();
        } else if constexpr (issubclass<T, ByteArray>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyByteArray_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


template <std::convertible_to<size_t> T>
struct __init__<ByteArray, T>                               : Returns<ByteArray> {
    static auto operator()(size_t size) {
        PyObject* result = PyByteArray_FromStringAndSize(nullptr, size);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};
template <>
struct __init__<ByteArray>                                  : Returns<ByteArray> {
    static auto operator()() { return ByteArray(0); }
};


template <>
struct __init__<ByteArray, char>                            : Returns<ByteArray> {
    static auto operator()(char value) {
        PyObject* result = PyByteArray_FromStringAndSize(&value, 1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};
template <size_t N>
struct __init__<ByteArray, char[N]>                         : Returns<ByteArray> {
    static auto operator()(const char (&string)[N]) {
        PyObject* result = PyByteArray_FromStringAndSize(string, N - 1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};
template <>
struct __init__<ByteArray, const char*>                     : Returns<ByteArray> {
    static auto operator()(const char* string) {
        PyObject* result = PyByteArray_FromStringAndSize(string, strlen(string));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};


template <>
struct __init__<ByteArray, const void*, size_t>             : Returns<ByteArray> {
    static auto operator()(const void* data, size_t size) {
        PyObject* result = PyByteArray_FromStringAndSize(static_cast<const char*>(data), size);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};


template <impl::python_like T> requires (!impl::bytearray_like<T>)
struct __explicit_init__<ByteArray, T>                       : Returns<ByteArray> {
    static auto operator()(const T& obj) {
        PyObject* result = PyByteArray_FromObject(obj.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
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
