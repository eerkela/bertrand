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


namespace impl {

    template <typename Derived>
    class IBytes : public Object {
        using Base = Object;

    public:
        using Base::Base;

        /* Equivalent to Python `bytes.capitalize()`. */
        inline Derived captialize() const {
            return reinterpret_steal<Derived>(attr<"capitalize">()().release());
        }

        /* Equivalent to Python `bytes.center(width)`. */
        inline Derived center(size_t width) const {
            return reinterpret_steal<Derived>(attr<"center">()(width).release());
        }

        /* Equivalent to Python `bytes.center(width, fillbyte)`. */
        inline Derived center(size_t width, const Derived& fillbyte) const {
            return reinterpret_steal<Derived>(attr<"center">()(width, fillbyte).release());
        }

        /* Equivalent to Python `bytes.count(sub[, start[, end]])`. */
        inline Py_ssize_t count(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<size_t>(attr<"count">()(sub, start, end));
        }

        /* Equivalent to Python `bytes.decode([encoding[, errors]])`. */
        inline Str decode(
            const Str& encoding = "utf-8",
            const Str& errors = "strict"
        ) const {
            return reinterpret_steal<Str>(attr<"decode">()(encoding, errors).release());
        }

        /* Equivalent to Python `bytes.endswith(suffix[, start[, end]])`. */
        inline bool endswith(
            const Derived& suffix,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<bool>(attr<"endswith">()(suffix, start, end));
        }

        /* Equivalent to Python `bytes.expandtabs(tabsize=8)`. */
        inline Derived expandtabs(size_t tabsize = 8) const {
            return reinterpret_steal<Derived>(attr<"expandtabs">()(tabsize).release());
        }

        /* Equivalent to Python `bytes.find(sub[, start[, end]])`. */
        inline Py_ssize_t find(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<size_t>(attr<"find">()(sub, start, end));
        }

        /* Equivalent to Python `bytes.hex()`. */
        inline Str hex() const {
            return reinterpret_steal<Str>(attr<"hex">()().release());
        }

        /* Equivalent to Python `bytes.hex(sep[, bytes_per_sep])`. */
        inline Str hex(const Derived& sep, Py_ssize_t bytes_per_sep = 1) const {
            return reinterpret_steal<Str>(attr<"hex">()(sep, bytes_per_sep).release());
        }

        /* Equivalent to Python `bytes.index(sub[, start[, end]])`. */
        inline Py_ssize_t index(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<size_t>(attr<"index">()(sub, start, end));
        }

        /* Equivalent to Python `bytes.isalnum()`. */
        inline bool isalnum() const {
            return static_cast<bool>(attr<"isalnum">()());
        }

        /* Equivalent to Python `bytes.isalpha()`. */
        inline bool isalpha() const {
            return static_cast<bool>(attr<"isalpha">()());
        }

        /* Equivalent to Python `bytes.isascii()`. */
        inline bool isascii() const {
            return static_cast<bool>(attr<"isascii">()());
        }

        /* Equivalent to Python `bytes.isdigit()`. */
        inline bool isdigit() const {
            return static_cast<bool>(attr<"isdigit">()());
        }

        /* Equivalent to Python `bytes.islower()`. */
        inline bool islower() const {
            return static_cast<bool>(attr<"islower">()());
        }

        /* Equivalent to Python `bytes.isspace()`. */
        inline bool isspace() const {
            return static_cast<bool>(attr<"isspace">()());
        }

        /* Equivalent to Python `bytes.istitle()`. */
        inline bool istitle() const {
            return static_cast<bool>(attr<"istitle">()());
        }

        /* Equivalent to Python `bytes.isupper()`. */
        inline bool isupper() const {
            return static_cast<bool>(attr<"isupper">()());
        }

        /* Equivalent to Python (static) `bytes.maketrans(x)`. */
        inline static Dict maketrans(const Derived& from, const Derived& to);

        /* Equivalent to Python `bytes.partition(sep)`. */
        inline Tuple partition(const Derived& sep) const {
            return reinterpret_steal<Tuple>(attr<"partition">()(sep).release());
        }

        /* Equivalent to Python `bytes.removeprefix(prefix)`. */
        inline Derived removeprefix(const Derived& prefix) const {
            return reinterpret_steal<Derived>(attr<"removeprefix">()(prefix).release());
        }

        /* Equivalent to Python `bytes.removesuffix(suffix)`. */
        inline Derived removesuffix(const Derived& suffix) const {
            return reinterpret_steal<Derived>(attr<"removesuffix">()(suffix).release());
        }

        /* Equivalent to Python `bytes.replace(old, new[, count])`. */
        inline Derived replace(
            const Derived& sub,
            const Derived& repl
        ) const {
            return reinterpret_steal<Derived>(attr<"replace">()(sub, repl).release());
        }

        /* Equivalent to Python `bytes.replace(old, new[, count])`. */
        inline Derived replace(
            const Derived& sub,
            const Derived& repl,
            Py_ssize_t maxcount
        ) const {
            return reinterpret_steal<Derived>(
                attr<"replace">()(sub, repl, maxcount).release()
            );
        }

        /* Equivalent to Python `bytes.rfind(sub[, start[, end]])`. */
        inline Py_ssize_t rfind(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<size_t>(attr<"rfind">()(sub, start, end));
        }

        /* Equivalent to Python `bytes.rindex(sub[, start[, end]])`. */
        inline Py_ssize_t rindex(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<size_t>(attr<"rindex">()(sub, start, end));
        }

        /* Equivalent to Python `bytes.rjust(width)`. */
        inline Derived rjust(size_t width) const {
            return reinterpret_steal<Derived>(attr<"rjust">()(width).release());
        }

        /* Equivalent to Python `bytes.rjust(width, fillbyte)`. */
        inline Derived rjust(size_t width, const Derived& fillbyte) const {
            return reinterpret_steal<Derived>(attr<"rjust">()(width, fillbyte).release());
        }

        /* Equivalent to Python `bytes.rpartition(sep)`. */
        inline Tuple rpartition(const Derived& sep) const {
            return reinterpret_steal<Tuple>(attr<"rpartition">()(sep).release());
        }

        /* Equivalent to Python `bytes.rsplit()`. */
        inline List rsplit() const {
            return reinterpret_steal<List>(attr<"rsplit">()().release());
        }

        /* Equivalent to Python `bytes.rsplit(sep, maxsplit=-1)`. */
        inline List rsplit(
            const Derived& sep,
            Py_ssize_t maxsplit = -1
        ) const {
            return reinterpret_steal<List>(attr<"rsplit">()(sep, maxsplit).release());
        }

        /* Equivalent to Python `bytes.rstrip()`. */
        inline Derived rstrip() const {
            return reinterpret_steal<Derived>(attr<"rstrip">()().release());
        }

        /* Equivalent to Python `bytes.rstrip(chars)`. */
        inline Derived rstrip(const Derived& chars) const {
            return reinterpret_steal<Derived>(attr<"rstrip">()(chars).release());
        }

        /* Equivalent to Python `bytes.split()`. */
        inline List split() const {
            return reinterpret_steal<List>(attr<"split">()().release());
        }

        /* Equivalent to Python `bytes.split(sep, maxsplit=-1)`. */
        inline List split(
            const Derived& sep,
            Py_ssize_t maxsplit = -1
        ) const {
            return reinterpret_steal<List>(attr<"split">()(sep, maxsplit).release());
        }

        /* Equivalent to Python `bytes.splitlines()`. */
        inline List splitlines(bool keepends = false) const {
            return reinterpret_steal<List>(attr<"splitlines">()(keepends).release());
        }

        /* Equivalent to Python `bytes.startswith(prefix[, start[, stop]])`. */
        inline bool startswith(
            const Derived& prefix,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            return static_cast<bool>(attr<"startswith">()(prefix, start, stop));
        }

        /* Equivalent to Python `bytes.strip()`. */
        inline Derived strip() const {
            return reinterpret_steal<Derived>(attr<"strip">()().release());
        }

        /* Equivalent to Python `bytes.strip(chars)`. */
        inline Derived strip(const Derived& chars) const {
            return reinterpret_steal<Derived>(attr<"strip">()(chars).release());
        }

        /* Equivalent to Python `bytes.swapcase()`. */
        inline Derived swapcase() const {
            return reinterpret_steal<Derived>(attr<"swapcase">()().release());
        }

        /* Equivalent to Python `bytes.title()`. */
        inline Derived title() const {
            return reinterpret_steal<Derived>(attr<"title">()().release());
        }

        /* Equivalent to Python `bytes.translate(table)`. */
        inline Derived translate(const Dict& table, const Derived& del = "") const {
            return reinterpret_steal<Derived>(attr<"translate">()(table, del).release());
        }

        /* Equivalent to Python `bytes.upper()`. */
        inline Derived upper() const {
            return reinterpret_steal<Derived>(attr<"upper">()().release());
        }

        /* Equivalent to Python `bytes.zfill(width)`. */
        inline Derived zfill(size_t width) const {
            return reinterpret_steal<Derived>(attr<"zfill">()(width).release());
        }

    };

    template <>
    struct __len__<Bytes>                                       : Returns<size_t> {};
    template <>
    struct __hash__<Bytes>                                      : Returns<size_t> {};
    template <>
    struct __iter__<Bytes>                                      : Returns<Int> {};
    template <>
    struct __reversed__<Bytes>                                  : Returns<Int> {};
    template <>
    struct __contains__<Bytes, Object>                          : Returns<bool> {};
    template <int_like T>
    struct __contains__<Bytes, T>                               : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __contains__<Bytes, T>                               : Returns<bool> {};
    template <>
    struct __getitem__<Bytes, Object>                           : Returns<Object> {};
    template <int_like T>
    struct __getitem__<Bytes, T>                                : Returns<Int> {};
    template <>
    struct __getitem__<Bytes, Slice>                            : Returns<Bytes> {};
    template <>
    struct __lt__<Bytes, Object>                                : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __lt__<Bytes, T>                                     : Returns<bool> {};
    template <>
    struct __le__<Bytes, Object>                                : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __le__<Bytes, T>                                     : Returns<bool> {};
    template <>
    struct __gt__<Bytes, Object>                                : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __gt__<Bytes, T>                                     : Returns<bool> {};
    template <>
    struct __ge__<Bytes, Object>                                : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __ge__<Bytes, T>                                     : Returns<bool> {};
    template <>
    struct __add__<Bytes, Object>                               : Returns<Bytes> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __add__<Bytes, T>                                    : Returns<Bytes> {};
    template <>
    struct __iadd__<Bytes, Object>                              : Returns<Bytes> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __iadd__<Bytes, T>                                   : Returns<Bytes> {};
    template <>
    struct __mul__<Bytes, Object>                               : Returns<Bytes> {};
    template <int_like T>
    struct __mul__<Bytes, T>                                    : Returns<Bytes> {};
    template <>
    struct __imul__<Bytes, Object>                              : Returns<Bytes> {};
    template <int_like T>
    struct __imul__<Bytes, T>                                   : Returns<Bytes> {};

    template <>
    struct __len__<ByteArray>                                   : Returns<size_t> {};
    template <>
    struct __iter__<ByteArray>                                  : Returns<Int> {};
    template <>
    struct __reversed__<ByteArray>                              : Returns<Int> {};
    template <>
    struct __contains__<ByteArray, Object>                      : Returns<bool> {};
    template <int_like T>
    struct __contains__<ByteArray, T>                           : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __contains__<ByteArray, T>                           : Returns<bool> {};
    template <>
    struct __getitem__<ByteArray, Object>                       : Returns<Object> {};
    template <int_like T>
    struct __getitem__<ByteArray, T>                            : Returns<Int> {};
    template <>
    struct __getitem__<ByteArray, Slice>                        : Returns<ByteArray> {};
    template <>
    struct __lt__<ByteArray, Object>                            : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __lt__<ByteArray, T>                                 : Returns<bool> {};
    template <>
    struct __le__<ByteArray, Object>                            : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __le__<ByteArray, T>                                 : Returns<bool> {};
    template <>
    struct __gt__<ByteArray, Object>                            : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __gt__<ByteArray, T>                                 : Returns<bool> {};
    template <>
    struct __ge__<ByteArray, Object>                            : Returns<bool> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __ge__<ByteArray, T>                                 : Returns<bool> {};
    template <>
    struct __add__<ByteArray, Object>                           : Returns<ByteArray> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __add__<ByteArray, T>                                : Returns<ByteArray> {};
    template <>
    struct __iadd__<ByteArray, Object>                          : Returns<ByteArray> {};
    template <typename T> requires (bytes_like<T> || bytearray_like<T>)
    struct __iadd__<ByteArray, T>                               : Returns<ByteArray> {};
    template <>
    struct __mul__<ByteArray, Object>                           : Returns<ByteArray> {};
    template <int_like T>
    struct __mul__<ByteArray, T>                                : Returns<ByteArray> {};
    template <>
    struct __imul__<ByteArray, Object>                          : Returns<ByteArray> {};
    template <int_like T>
    struct __imul__<ByteArray, T>                               : Returns<ByteArray> {};

}


/* Equivalent to Python `bytes` type. */
class Bytes : public impl::IBytes<Bytes>, public impl::SequenceOps<Bytes> {
    using Base = impl::IBytes<Bytes>;

    template <typename T>
    static constexpr bool py_constructor =
        impl::bytes_like<T> && impl::python_like<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::bytes_like<T>; }

    BERTRAND_OBJECT_COMMON(Base, Bytes, PyBytes_Check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty string with zero bytes. */
    Bytes(size_t size = 0) : Base(
        PyBytes_FromStringAndSize(nullptr, size),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (py_constructor<T>)
    Bytes(T&& obj) : Base(std::forward<T>(obj)) {}

    /* Implicitly convert a string literal into a py::Bytes object.  Note that this
    interprets the string as raw binary data rather than text, and it automatically
    excludes the terminating null byte.  This is primary useful for hardcoded binary
    data, rather than as a replacement for py::Str. */
    template <size_t N>
    Bytes(const char (&string)[N]) : Base(
        PyBytes_FromStringAndSize(string, N - 1),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert a raw sequence of bytes into a py::Bytes object. */
    Bytes(const void* data, size_t size) : Base(
        PyBytes_FromStringAndSize(static_cast<const char*>(data), size),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Get a bytes representation of a Python object that implements the buffer
    protocol. */
    template <impl::python_like T>
    explicit Bytes(const T& obj) : Base(PyBytes_FromObject(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Access the internal buffer of the bytes object.  Note that this implicitly
    includes an extra null byte at the end of the buffer, regardless of any nulls that
    were already present.  The data should not be modified unless the bytes object was
    just created via `py::Bytes(size)`, and it should never be deallocated. */
    inline char* data() const {
        return PyBytes_AS_STRING(this->ptr());
    }

    /* Copy the contents of the buffer into a new py::Bytes object. */
    inline Bytes copy() const {
        return reinterpret_steal<Bytes>(PyBytes_FromStringAndSize(
            PyBytes_AS_STRING(this->ptr()),
            PyBytes_GET_SIZE(this->ptr())
        ));
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python (static) `bytes.fromhex(string)`. */
    inline static Bytes fromhex(const Str& string);

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

protected:

    using impl::SequenceOps<Bytes>::operator_mul;
    using impl::SequenceOps<Bytes>::operator_imul;

    template <typename Return>
    inline static auto operator_len(const Bytes& obj) {
        return static_cast<size_t>(PyBytes_GET_SIZE(obj.ptr()));
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_add(const L& lhs, const R& rhs) {
        if constexpr (std::is_base_of_v<Bytes, L>) {
            Return result = lhs.copy();
            operator_iadd<Return>(result, rhs);
            return result;
        } else {
            Return result = rhs.copy();
            operator_iadd<Return>(result, lhs);
            return result;
        }
    }

    template <typename Return, typename L, typename R>
    inline static void operator_iadd(L& lhs, const R& rhs) {
        PyObject* result = lhs.ptr();
        PyBytes_Concat(&result, Bytes(rhs).ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
    }

};


/* Equivalent to Python `bytearray` type. */
class ByteArray : public impl::IBytes<ByteArray>, public impl::SequenceOps<ByteArray> {
    using Base = impl::IBytes<ByteArray>;

    template <typename T>
    static constexpr bool py_constructor =
        impl::bytearray_like<T> && impl::python_like<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::bytearray_like<T>; }

    BERTRAND_OBJECT_COMMON(Base, ByteArray, PyByteArray_Check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty string with zero bytes. */
    ByteArray(size_t size = 0) : Base(
        PyBytes_FromStringAndSize(nullptr, size),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (py_constructor<T>)
    ByteArray(T&& obj) : Base(std::forward<T>(obj)) {}

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
            throw error_already_set();
        }
    }

    /* Implicitly convert a raw sequence of bytes into a py::ByteArray object. */
    ByteArray(const void* data, size_t size) : Base(
        PyByteArray_FromStringAndSize(static_cast<const char*>(data), size),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Get a bytes representation of a Python object that implements the buffer
    protocol. */
    template <impl::python_like T>
    explicit ByteArray(const T& obj) : Base(PyByteArray_FromObject(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Access the internal buffer of the bytearray object.  The data can be modified
    in-place, but should never be deallocated. */
    inline char* data() const {
        return PyByteArray_AS_STRING(this->ptr());
    }

    /* Copy the contents of the buffer into a new py::ByteArray object. */
    inline ByteArray copy() const {
        return reinterpret_steal<ByteArray>(PyByteArray_FromStringAndSize(
            PyByteArray_AS_STRING(this->ptr()),
            PyByteArray_GET_SIZE(this->ptr())
        ));
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python (static) `bytes.fromhex(string)`. */
    inline static ByteArray fromhex(const Str& string);

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

protected:

    using impl::SequenceOps<ByteArray>::operator_mul;
    using impl::SequenceOps<ByteArray>::operator_imul;

    template <typename Return>
    inline static auto operator_len(const ByteArray& obj) {
        return static_cast<size_t>(PyByteArray_GET_SIZE(obj.ptr()));
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_add(const L& lhs, const R& rhs) {
        PyObject* result;
        if constexpr (std::is_base_of_v<ByteArray, L>) {
            result = PyByteArray_Concat(lhs.ptr(), ByteArray(rhs).ptr());
        } else {
            result = PyByteArray_Concat(ByteArray(lhs).ptr(), rhs.ptr());
        }
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_iadd(L& lhs, const R& rhs) {
        lhs = operator_add<Return>(lhs, rhs);
    }

};


inline Bytes Str::encode(const Str& encoding, const Str& errors) const {
    return reinterpret_steal<Bytes>(attr<"encode">()(encoding, errors).release());
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_BYTES_H
