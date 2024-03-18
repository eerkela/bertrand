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
            static const pybind11::str method = "capitalize";
            return reinterpret_steal<Derived>(attr(method)().release());
        }

        /* Equivalent to Python `bytes.center(width)`. */
        inline Derived center(size_t width) const {
            static const pybind11::str method = "center";
            return reinterpret_steal<Derived>(attr(method)(width).release());
        }

        /* Equivalent to Python `bytes.center(width, fillbyte)`. */
        inline Derived center(size_t width, const Derived& fillbyte) const {
            static const pybind11::str method = "center";
            return reinterpret_steal<Derived>(attr(method)(width, fillbyte).release());
        }

        /* Equivalent to Python `bytes.count(sub[, start[, end]])`. */
        inline Py_ssize_t count(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            static const pybind11::str method = "count";
            return static_cast<size_t>(attr(method)(sub, start, end));
        }

        /* Equivalent to Python `bytes.decode([encoding[, errors]])`. */
        inline Str decode(
            const Str& encoding = "utf-8",
            const Str& errors = "strict"
        ) const {
            static const pybind11::str method = "decode";
            return reinterpret_steal<Str>(attr(method)(encoding, errors).release());
        }

        /* Equivalent to Python `bytes.endswith(suffix[, start[, end]])`. */
        inline bool endswith(
            const Derived& suffix,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            static const pybind11::str method = "endswith";
            return static_cast<bool>(attr(method)(suffix, start, end));
        }

        /* Equivalent to Python `bytes.expandtabs(tabsize=8)`. */
        inline Derived expandtabs(size_t tabsize = 8) const {
            static const pybind11::str method = "expandtabs";
            return reinterpret_steal<Derived>(attr(method)(tabsize).release());
        }

        /* Equivalent to Python `bytes.find(sub[, start[, end]])`. */
        inline Py_ssize_t find(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            static const pybind11::str method = "find";
            return static_cast<size_t>(attr(method)(sub, start, end));
        }

        /* Equivalent to Python `bytes.index(sub[, start[, end]])`. */
        inline Py_ssize_t index(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            static const pybind11::str method = "index";
            return static_cast<size_t>(attr(method)(sub, start, end));
        }

        /* Equivalent to Python `bytes.isalnum()`. */
        inline bool isalnum() const {
            static const pybind11::str method = "isalnum";
            return static_cast<bool>(attr(method)());
        }

        /* Equivalent to Python `bytes.isalpha()`. */
        inline bool isalpha() const {
            static const pybind11::str method = "isalpha";
            return static_cast<bool>(attr(method)());
        }

        /* Equivalent to Python `bytes.isascii()`. */
        inline bool isascii() const {
            static const pybind11::str method = "isascii";
            return static_cast<bool>(attr(method)());
        }

        /* Equivalent to Python `bytes.isdigit()`. */
        inline bool isdigit() const {
            static const pybind11::str method = "isdigit";
            return static_cast<bool>(attr(method)());
        }

        /* Equivalent to Python `bytes.islower()`. */
        inline bool islower() const {
            static const pybind11::str method = "islower";
            return static_cast<bool>(attr(method)());
        }

        /* Equivalent to Python `bytes.isspace()`. */
        inline bool isspace() const {
            static const pybind11::str method = "isspace";
            return static_cast<bool>(attr(method)());
        }

        /* Equivalent to Python `bytes.istitle()`. */
        inline bool istitle() const {
            static const pybind11::str method = "istitle";
            return static_cast<bool>(attr(method)());
        }

        /* Equivalent to Python `bytes.isupper()`. */
        inline bool isupper() const {
            static const pybind11::str method = "isupper";
            return static_cast<bool>(attr(method)());
        }

        /* Equivalent to Python (static) `bytes.maketrans(x)`. */
        inline static Dict maketrans(const Derived& from, const Derived& to) {
            static const pybind11::str method = "maketrans";
            static const pybind11::type cls =
                reinterpret_borrow<pybind11::type>((PyObject*) &PyBytes_Type);
            return reinterpret_steal<Dict>(
                cls.attr(method)(detail::object_or_cast(from, to)).release()
            );
        }

        /* Equivalent to Python `bytes.partition(sep)`. */
        inline Tuple partition(const Derived& sep) const {
            static const pybind11::str method = "partition";
            return reinterpret_steal<Tuple>(attr(method)(sep).release());
        }

        /* Equivalent to Python `bytes.removeprefix(prefix)`. */
        inline Derived removeprefix(const Derived& prefix) const {
            static const pybind11::str method = "removeprefix";
            return reinterpret_steal<Derived>(attr(method)(prefix).release());
        }

        /* Equivalent to Python `bytes.removesuffix(suffix)`. */
        inline Derived removesuffix(const Derived& suffix) const {
            static const pybind11::str method = "removesuffix";
            return reinterpret_steal<Derived>(attr(method)(suffix).release());
        }

        /* Equivalent to Python `bytes.replace(old, new[, count])`. */
        inline Derived replace(
            const Derived& sub,
            const Derived& repl
        ) const {
            static const pybind11::str method = "replace";
            return reinterpret_steal<Derived>(attr(method)(sub, repl).release());
        }

        /* Equivalent to Python `bytes.replace(old, new[, count])`. */
        inline Derived replace(
            const Derived& sub,
            const Derived& repl,
            Py_ssize_t maxcount
        ) const {
            static const pybind11::str method = "replace";
            return reinterpret_steal<Derived>(
                attr(method)(sub, repl, maxcount).release()
            );
        }

        /* Equivalent to Python `bytes.rfind(sub[, start[, end]])`. */
        inline Py_ssize_t rfind(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            static const pybind11::str method = "rfind";
            return static_cast<size_t>(attr(method)(sub, start, end));
        }

        /* Equivalent to Python `bytes.rindex(sub[, start[, end]])`. */
        inline Py_ssize_t rindex(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            static const pybind11::str method = "rindex";
            return static_cast<size_t>(attr(method)(sub, start, end));
        }

        /* Equivalent to Python `bytes.rjust(width)`. */
        inline Derived rjust(size_t width) const {
            static const pybind11::str method = "rjust";
            return reinterpret_steal<Derived>(attr(method)(width).release());
        }

        /* Equivalent to Python `bytes.rjust(width, fillbyte)`. */
        inline Derived rjust(size_t width, const Derived& fillbyte) const {
            static const pybind11::str method = "rjust";
            return reinterpret_steal<Derived>(attr(method)(width, fillbyte).release());
        }

        /* Equivalent to Python `bytes.rpartition(sep)`. */
        inline Tuple rpartition(const Derived& sep) const {
            static const pybind11::str method = "rpartition";
            return reinterpret_steal<Tuple>(attr(method)(sep).release());
        }

        /* Equivalent to Python `bytes.rsplit()`. */
        inline List rsplit() const {
            static const pybind11::str method = "rsplit";
            return reinterpret_steal<List>(attr(method)().release());
        }

        /* Equivalent to Python `bytes.rsplit(sep, maxsplit=-1)`. */
        inline List rsplit(
            const Derived& sep,
            Py_ssize_t maxsplit = -1
        ) const {
            static const pybind11::str method = "rsplit";
            return reinterpret_steal<List>(attr(method)(sep, maxsplit).release());
        }

        /* Equivalent to Python `bytes.rstrip()`. */
        inline Derived rstrip() const {
            static const pybind11::str method = "rstrip";
            return reinterpret_steal<Derived>(attr(method)().release());
        }

        /* Equivalent to Python `bytes.rstrip(chars)`. */
        inline Derived rstrip(const Derived& chars) const {
            static const pybind11::str method = "rstrip";
            return reinterpret_steal<Derived>(attr(method)(chars).release());
        }

        /* Equivalent to Python `bytes.split()`. */
        inline List split() const {
            static const pybind11::str method = "split";
            return reinterpret_steal<List>(attr(method)().release());
        }

        /* Equivalent to Python `bytes.split(sep, maxsplit=-1)`. */
        inline List split(
            const Derived& sep,
            Py_ssize_t maxsplit = -1
        ) const {
            static const pybind11::str method = "split";
            return reinterpret_steal<List>(attr(method)(sep, maxsplit).release());
        }

        /* Equivalent to Python `bytes.splitlines()`. */
        inline List splitlines(bool keepends = false) const {
            static const pybind11::str method = "splitlines";
            return reinterpret_steal<List>(attr(method)(keepends).release());
        }

        /* Equivalent to Python `bytes.startswith(prefix[, start[, stop]])`. */
        inline bool startswith(
            const Derived& prefix,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            static const pybind11::str method = "startswith";
            return static_cast<bool>(attr(method)(prefix, start, stop));
        }

        /* Equivalent to Python `bytes.strip()`. */
        inline Derived strip() const {
            static const pybind11::str method = "strip";
            return reinterpret_steal<Derived>(attr(method)().release());
        }

        /* Equivalent to Python `bytes.strip(chars)`. */
        inline Derived strip(const Derived& chars) const {
            static const pybind11::str method = "strip";
            return reinterpret_steal<Derived>(attr(method)(chars).release());
        }

        /* Equivalent to Python `bytes.swapcase()`. */
        inline Derived swapcase() const {
            static const pybind11::str method = "swapcase";
            return reinterpret_steal<Derived>(attr(method)().release());
        }

        /* Equivalent to Python `bytes.title()`. */
        inline Derived title() const {
            static const pybind11::str method = "title";
            return reinterpret_steal<Derived>(attr(method)().release());
        }

        /* Equivalent to Python `bytes.translate(table)`. */
        inline Derived translate(const Dict& table, const Derived& del = "") const {
            static const pybind11::str method = "translate";
            return reinterpret_steal<Derived>(attr(method)(table, del).release());
        }

        /* Equivalent to Python `bytes.upper()`. */
        inline Derived upper() const {
            static const pybind11::str method = "upper";
            return reinterpret_steal<Derived>(attr(method)().release());
        }

        /* Equivalent to Python `bytes.zfill(width)`. */
        inline Derived zfill(size_t width) const {
            static const pybind11::str method = "zfill";
            return reinterpret_steal<Derived>(attr(method)(width).release());
        }

    };

    template <>
    struct __dereference__<Bytes>                               : Returns<detail::args_proxy> {};
    template <>
    struct __len__<Bytes>                                       : Returns<size_t> {};
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
    struct __dereference__<ByteArray>                           : Returns<detail::args_proxy> {};
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

    ////////////////////////
    ////    OPERATORS   ////
    ////////////////////////

    /* Get the size of the internal buffer. */
    inline size_t size() const {
        return static_cast<size_t>(PyBytes_GET_SIZE(this->ptr()));
    }

protected:

    using impl::SequenceOps<Bytes>::operator_mul;
    using impl::SequenceOps<Bytes>::operator_imul;

    template <typename Return, typename L, typename R>
    inline static auto operator_add(const L& lhs, const R& rhs) {
        PyObject* result = PyBytes_Concat(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
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


/* Equivalent to Python `bytearray` type. */
class ByteArray : public impl::IBytes<ByteArray>, public impl::SequenceOps<ByteArray> {
    using Base = impl::IBytes<ByteArray>;

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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Get the size of the internal buffer. */
    inline size_t size() const {
        return static_cast<size_t>(PyByteArray_GET_SIZE(this->ptr()));
    }

protected:

    using impl::SequenceOps<ByteArray>::operator_mul;
    using impl::SequenceOps<ByteArray>::operator_imul;

    template <typename Return, typename L, typename R>
    inline static auto operator_add(const L& lhs, const R& rhs) {
        PyObject* result = PyByteArray_Concat(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
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


inline Bytes Str::encode(
    const Str& encoding,
    const Str& errors
) const {
    static const pybind11::str method = "encode";
    return reinterpret_steal<Bytes>(attr(method)(encoding, errors).release());
}


}  // namespace py
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Bytes)


#endif  // BERTRAND_PYTHON_BYTES_H
