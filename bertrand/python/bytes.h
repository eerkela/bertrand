#ifndef BERTRAND_PYTHON_BYTES_H
#define BERTRAND_PYTHON_BYTES_H

#include "common.h"
#include "int.h"
#include "str.h"
#include "list.h"
#include "tuple.h"
#include "set.h"
#include "dict.h"


namespace py {


/////////////////////
////    BYTES    ////
/////////////////////


template <typename T>
struct __issubclass__<T, Bytes>                             : Returns<bool> {
    static consteval bool operator()() { return impl::bytes_like<T>; }
};


template <typename T>
struct __isinstance__<T, Bytes>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return PyBytes_Check(ptr(obj));
        } else {
            return issubclass<T, Bytes>();
        }
    }
};


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
    Bytes(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Bytes, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Bytes, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Bytes, __explicit_init__<Bytes, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Bytes, std::remove_cvref_t<Args>...>::enable
        )
    explicit Bytes(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Bytes, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

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

    template <typename... Args> requires (impl::invocable<Self, "capitalize", Args...>)
    [[nodiscard]] decltype(auto) capitalize(Args&&... args) const {
        return impl::call_method<"capitalize">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "center", Args...>)
    [[nodiscard]] decltype(auto) center(Args&&... args) const {
        return impl::call_method<"center">(*this, std::forward<Args>(args)...);
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

    template <typename... Args> requires (impl::invocable<Self, "decode", Args...>)
    [[nodiscard]] decltype(auto) decode(Args&&... args) const {
        return impl::call_method<"decode">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "endswith", Args...>)
    [[nodiscard]] decltype(auto) endswith(Args&&... args) const {
        return impl::call_method<"endswith">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "expandtabs", Args...>)
    [[nodiscard]] decltype(auto) expandtabs(Args&&... args) const {
        return impl::call_method<"expandtabs">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "find", Args...>)
    [[nodiscard]] decltype(auto) find(Args&&... args) const {
        return impl::call_method<"find">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "fromhex", Args...>)
    [[nodiscard]] static decltype(auto) fromhex(Args&&... args) {
        return impl::call_static<Self, "fromhex">(std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "hex", Args...>)
    [[nodiscard]] decltype(auto) hex(Args&&... args) const {
        return impl::call_method<"hex">(*this, std::forward<Args>(args)...);
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

    template <typename... Args> requires (impl::invocable<Self, "isalnum", Args...>)
    [[nodiscard]] decltype(auto) isalnum(Args&&... args) const {
        return impl::call_method<"isalnum">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isalpha", Args...>)
    [[nodiscard]] decltype(auto) isalpha(Args&&... args) const {
        return impl::call_method<"isalpha">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isascii", Args...>)
    [[nodiscard]] decltype(auto) isascii_(Args&&... args) const {
        return impl::call_method<"isascii">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isdigit", Args...>)
    [[nodiscard]] decltype(auto) isdigit(Args&&... args) const {
        return impl::call_method<"isdigit">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "islower", Args...>)
    [[nodiscard]] decltype(auto) islower(Args&&... args) const {
        return impl::call_method<"islower">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isspace", Args...>)
    [[nodiscard]] decltype(auto) isspace(Args&&... args) const {
        return impl::call_method<"isspace">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "istitle", Args...>)
    [[nodiscard]] decltype(auto) istitle(Args&&... args) const {
        return impl::call_method<"istitle">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isupper", Args...>)
    [[nodiscard]] decltype(auto) isupper(Args&&... args) const {
        return impl::call_method<"isupper">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "join", Args...>)
    [[nodiscard]] decltype(auto) join(Args&&... args) const {
        return impl::call_method<"join">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "ljust", Args...>)
    [[nodiscard]] decltype(auto) ljust(Args&&... args) const {
        return impl::call_method<"ljust">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "lower", Args...>)
    [[nodiscard]] decltype(auto) lower(Args&&... args) const {
        return impl::call_method<"lower">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "lstrip", Args...>)
    [[nodiscard]] decltype(auto) lstrip(Args&&... args) const {
        return impl::call_method<"lstrip">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "maketrans", Args...>)
    [[nodiscard]] static decltype(auto) maketrans(Args&&... args) {
        return impl::call_static<Self, "maketrans">(std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "partition", Args...>)
    [[nodiscard]] decltype(auto) partition(Args&&... args) const {
        return impl::call_method<"partition">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "removeprefix", Args...>)
    [[nodiscard]] decltype(auto) removeprefix(Args&&... args) const {
        return impl::call_method<"removeprefix">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "removesuffix", Args...>)
    [[nodiscard]] decltype(auto) removesuffix(Args&&... args) const {
        return impl::call_method<"removesuffix">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "replace", Args...>)
    [[nodiscard]] decltype(auto) replace(Args&&... args) const {
        return impl::call_method<"replace">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rfind", Args...>)
    [[nodiscard]] decltype(auto) rfind(Args&&... args) const {
        return impl::call_method<"rfind">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rindex", Args...>)
    [[nodiscard]] decltype(auto) rindex(Args&&... args) const {
        return impl::call_method<"rindex">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rjust", Args...>)
    [[nodiscard]] decltype(auto) rjust(Args&&... args) const {
        return impl::call_method<"rjust">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rpartition", Args...>)
    [[nodiscard]] decltype(auto) rpartition(Args&&... args) const {
        return impl::call_method<"rpartition">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rsplit", Args...>)
    [[nodiscard]] decltype(auto) rsplit(Args&&... args) const {
        return impl::call_method<"rsplit">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rstrip", Args...>)
    [[nodiscard]] decltype(auto) rstrip(Args&&... args) const {
        return impl::call_method<"rstrip">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "split", Args...>)
    [[nodiscard]] decltype(auto) split(Args&&... args) const {
        return impl::call_method<"split">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "splitlines", Args...>)
    [[nodiscard]] decltype(auto) splitlines(Args&&... args) const {
        return impl::call_method<"splitlines">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "startswith", Args...>)
    [[nodiscard]] decltype(auto) startswith(Args&&... args) const {
        return impl::call_method<"startswith">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "strip", Args...>)
    [[nodiscard]] decltype(auto) strip(Args&&... args) const {
        return impl::call_method<"strip">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "swapcase", Args...>)
    [[nodiscard]] decltype(auto) swapcase(Args&&... args) const {
        return impl::call_method<"swapcase">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "title", Args...>)
    [[nodiscard]] decltype(auto) title(Args&&... args) const {
        return impl::call_method<"title">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "translate", Args...>)
    [[nodiscard]] decltype(auto) translate(Args&&... args) const {
        return impl::call_method<"translate">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "upper", Args...>)
    [[nodiscard]] decltype(auto) upper(Args&&... args) const {
        return impl::call_method<"upper">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "zfill", Args...>)
    [[nodiscard]] decltype(auto) zfill(Args&&... args) const {
        return impl::call_method<"zfill">(*this, std::forward<Args>(args)...);
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


template <std::derived_from<Bytes> Self>
struct __len__<Self>                                        : Returns<size_t> {
    static size_t operator()(const Self& self) {
        return PyBytes_GET_SIZE(self.ptr());
    }
};


template <std::derived_from<Bytes> L, impl::anybytes_like R>
struct __add__<L, R>                                        : Returns<Bytes> {
    static auto operator()(const L& lhs, const R& rhs) {
        Bytes result = lhs.copy();
        result += rhs;
        return result;
    }
};


template <impl::anybytes_like L, std::derived_from<Bytes> R>
    requires (!std::derived_from<L, Bytes>)
struct __add__<L, R>                                        : Returns<Bytes> {
    static auto operator()(const L& lhs, const R& rhs) {
        Bytes result = rhs.copy();
        result += lhs;
        return result;
    }
};


template <std::derived_from<Bytes> L, impl::anybytes_like R>
struct __iadd__<L, R>                                       : Returns<Bytes&> {
    static void operator()(L& lhs, const R& rhs) {
        PyObject* result = lhs.ptr();
        PyBytes_Concat(&result, as_object(rhs).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
    }
};


template <std::derived_from<Bytes> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Bytes> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Repeat(lhs.ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
    }
};


template <impl::int_like L, std::derived_from<Bytes> R>
struct __mul__<L, R>                                        : Returns<Bytes> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Repeat(rhs.ptr(), lhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Bytes>(result);
    }
};


template <std::derived_from<Bytes> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Bytes&> {
    static void operator()(L& lhs, Py_ssize_t rhs) {
        PyObject* result = PySequence_InPlaceRepeat(lhs.ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }
};


/////////////////////////
////    BYTEARRAY    ////
/////////////////////////


template <typename T>
struct __issubclass__<T, ByteArray>                         : Returns<bool> {
    static consteval bool operator()() { return impl::bytearray_like<T>; }
};


template <typename T>
struct __isinstance__<T, ByteArray>                         : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return PyByteArray_Check(ptr(obj));
        } else {
            return issubclass<T, ByteArray>();
        }
    }
};


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
    ByteArray(Args&&... args) : Base((
        Interpreter::init(),
        __init__<ByteArray, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<ByteArray, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<ByteArray, __explicit_init__<ByteArray, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<ByteArray, std::remove_cvref_t<Args>...>::enable
        )
    explicit ByteArray(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<ByteArray, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

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

    template <typename... Args> requires (impl::invocable<Self, "capitalize", Args...>)
    [[nodiscard]] decltype(auto) capitalize(Args&&... args) const {
        return impl::call_method<"capitalize">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "center", Args...>)
    [[nodiscard]] decltype(auto) center(Args&&... args) const {
        return impl::call_method<"center">(*this, std::forward<Args>(args)...);
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

    template <typename... Args> requires (impl::invocable<Self, "decode", Args...>)
    [[nodiscard]] decltype(auto) decode(Args&&... args) const {
        return impl::call_method<"decode">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "endswith", Args...>)
    [[nodiscard]] decltype(auto) endswith(Args&&... args) const {
        return impl::call_method<"endswith">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "expandtabs", Args...>)
    [[nodiscard]] decltype(auto) expandtabs(Args&&... args) const {
        return impl::call_method<"expandtabs">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "find", Args...>)
    [[nodiscard]] decltype(auto) find(Args&&... args) const {
        return impl::call_method<"find">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "fromhex", Args...>)
    [[nodiscard]] static decltype(auto) fromhex(Args&&... args) {
        return impl::call_static<Self, "fromhex">(std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "hex", Args...>)
    [[nodiscard]] decltype(auto) hex(Args&&... args) const {
        return impl::call_method<"hex">(*this, std::forward<Args>(args)...);
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

    template <typename... Args> requires (impl::invocable<Self, "isalnum", Args...>)
    [[nodiscard]] decltype(auto) isalnum(Args&&... args) const {
        return impl::call_method<"isalnum">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isalpha", Args...>)
    [[nodiscard]] decltype(auto) isalpha(Args&&... args) const {
        return impl::call_method<"isalpha">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isascii", Args...>)
    [[nodiscard]] decltype(auto) isascii_(Args&&... args) const {
        return impl::call_method<"isascii">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isdigit", Args...>)
    [[nodiscard]] decltype(auto) isdigit(Args&&... args) const {
        return impl::call_method<"isdigit">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "islower", Args...>)
    [[nodiscard]] decltype(auto) islower(Args&&... args) const {
        return impl::call_method<"islower">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isspace", Args...>)
    [[nodiscard]] decltype(auto) isspace(Args&&... args) const {
        return impl::call_method<"isspace">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "istitle", Args...>)
    [[nodiscard]] decltype(auto) istitle(Args&&... args) const {
        return impl::call_method<"istitle">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "isupper", Args...>)
    [[nodiscard]] decltype(auto) isupper(Args&&... args) const {
        return impl::call_method<"isupper">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "join", Args...>)
    [[nodiscard]] decltype(auto) join(Args&&... args) const {
        return impl::call_method<"join">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "ljust", Args...>)
    [[nodiscard]] decltype(auto) ljust(Args&&... args) const {
        return impl::call_method<"ljust">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "lower", Args...>)
    [[nodiscard]] decltype(auto) lower(Args&&... args) const {
        return impl::call_method<"lower">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "lstrip", Args...>)
    [[nodiscard]] decltype(auto) lstrip(Args&&... args) const {
        return impl::call_method<"lstrip">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "maketrans", Args...>)
    [[nodiscard]] static decltype(auto) maketrans(Args&&... args) {
        return impl::call_static<Self, "maketrans">(std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "partition", Args...>)
    [[nodiscard]] decltype(auto) partition(Args&&... args) const {
        return impl::call_method<"partition">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "removeprefix", Args...>)
    [[nodiscard]] decltype(auto) removeprefix(Args&&... args) const {
        return impl::call_method<"removeprefix">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "removesuffix", Args...>)
    [[nodiscard]] decltype(auto) removesuffix(Args&&... args) const {
        return impl::call_method<"removesuffix">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "replace", Args...>)
    [[nodiscard]] decltype(auto) replace(Args&&... args) const {
        return impl::call_method<"replace">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rfind", Args...>)
    [[nodiscard]] decltype(auto) rfind(Args&&... args) const {
        return impl::call_method<"rfind">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rindex", Args...>)
    [[nodiscard]] decltype(auto) rindex(Args&&... args) const {
        return impl::call_method<"rindex">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rjust", Args...>)
    [[nodiscard]] decltype(auto) rjust(Args&&... args) const {
        return impl::call_method<"rjust">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rpartition", Args...>)
    [[nodiscard]] decltype(auto) rpartition(Args&&... args) const {
        return impl::call_method<"rpartition">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rsplit", Args...>)
    [[nodiscard]] decltype(auto) rsplit(Args&&... args) const {
        return impl::call_method<"rsplit">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "rstrip", Args...>)
    [[nodiscard]] decltype(auto) rstrip(Args&&... args) const {
        return impl::call_method<"rstrip">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "split", Args...>)
    [[nodiscard]] decltype(auto) split(Args&&... args) const {
        return impl::call_method<"split">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "splitlines", Args...>)
    [[nodiscard]] decltype(auto) splitlines(Args&&... args) const {
        return impl::call_method<"splitlines">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "startswith", Args...>)
    [[nodiscard]] decltype(auto) startswith(Args&&... args) const {
        return impl::call_method<"startswith">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "strip", Args...>)
    [[nodiscard]] decltype(auto) strip(Args&&... args) const {
        return impl::call_method<"strip">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "swapcase", Args...>)
    [[nodiscard]] decltype(auto) swapcase(Args&&... args) const {
        return impl::call_method<"swapcase">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "title", Args...>)
    [[nodiscard]] decltype(auto) title(Args&&... args) const {
        return impl::call_method<"title">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "translate", Args...>)
    [[nodiscard]] decltype(auto) translate(Args&&... args) const {
        return impl::call_method<"translate">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "upper", Args...>)
    [[nodiscard]] decltype(auto) upper(Args&&... args) const {
        return impl::call_method<"upper">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "zfill", Args...>)
    [[nodiscard]] decltype(auto) zfill(Args&&... args) const {
        return impl::call_method<"zfill">(*this, std::forward<Args>(args)...);
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


template <std::derived_from<ByteArray> Self>
struct __len__<Self>                                        : Returns<size_t> {
    static size_t operator()(const Self& self) {
        return PyByteArray_GET_SIZE(self.ptr());
    }
};


template <std::derived_from<ByteArray> L, impl::anybytes_like R>
struct __add__<L, R>                                        : Returns<ByteArray> {
    static auto operator()(const ByteArray& lhs, const ByteArray& rhs) {
        PyObject* result = PyByteArray_Concat(lhs.ptr(), rhs.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};


template <impl::anybytes_like L, std::derived_from<ByteArray> R>
    requires (!std::derived_from<L, ByteArray>)
struct __add__<L, R>                                        : Returns<ByteArray> {
    static auto operator()(const ByteArray& lhs, const ByteArray& rhs) {
        PyObject* result = PyByteArray_Concat(lhs.ptr(), rhs.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};


template <std::derived_from<ByteArray> L, impl::anybytes_like R>
struct __iadd__<L, R>                                       : Returns<ByteArray&> {
    static void operator()(L& lhs, const R& rhs) {
        lhs = lhs + rhs;
    }
};


template <std::derived_from<ByteArray> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<ByteArray> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Repeat(lhs.ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};


template <impl::int_like L, std::derived_from<ByteArray> R>
struct __mul__<L, R>                                        : Returns<ByteArray> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Repeat(rhs.ptr(), lhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<ByteArray>(result);
    }
};


template <std::derived_from<ByteArray> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<ByteArray&> {
    static void operator()(L& lhs, const R& rhs) {
        PyObject* result = PySequence_InPlaceRepeat(lhs.ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }
};


}  // namespace py


#endif
