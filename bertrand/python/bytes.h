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

#include "func.h"


namespace bertrand {
namespace py {


namespace impl {

    struct IBytesTag {};

    template <typename Derived>
    class IBytes : public Object, public IBytesTag {
        using Base = Object;

        Derived& self() { return static_cast<Derived&>(*this); }
        const Derived& self() const { return static_cast<const Derived&>(*this); }

    public:
        using Base::Base;

        /* Equivalent to Python `bytes.capitalize()`. */
        inline Derived capitalize() const {
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
            const Derived& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(self().ptr(), start, stop);
                if (slice == nullptr) {
                    Exception::from_python();
                }
                Py_ssize_t result = PySequence_Count(slice, value.ptr());
                Py_DECREF(slice);
                if (result < 0) {
                    Exception::from_python();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Count(self().ptr(), value.ptr());
                if (result < 0) {
                    Exception::from_python();
                }
                return result;
            }
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
            const Derived& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(self().ptr(), start, stop);
                if (slice == nullptr) {
                    Exception::from_python();
                }
                Py_ssize_t result = PySequence_Index(slice, value.ptr());
                Py_DECREF(slice);
                if (result < 0) {
                    Exception::from_python();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Index(self().ptr(), value.ptr());
                if (result < 0) {
                    Exception::from_python();
                }
                return result;
            }
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
        inline Tuple<Derived> partition(const Derived& sep) const {
            return reinterpret_steal<Tuple<Derived>>(attr<"partition">()(sep).release());
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
        inline Tuple<Derived> rpartition(const Derived& sep) const {
            return reinterpret_steal<Tuple<Derived>>(attr<"rpartition">()(sep).release());
        }

        /* Equivalent to Python `bytes.rsplit()`. */
        inline List<Derived> rsplit() const {
            return reinterpret_steal<List<Derived>>(attr<"rsplit">()().release());
        }

        /* Equivalent to Python `bytes.rsplit(sep, maxsplit=-1)`. */
        inline List<Derived> rsplit(
            const Derived& sep,
            Py_ssize_t maxsplit = -1
        ) const {
            return reinterpret_steal<List<Derived>>(attr<"rsplit">()(sep, maxsplit).release());
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
        inline List<Derived> split() const {
            return reinterpret_steal<List<Derived>>(attr<"split">()().release());
        }

        /* Equivalent to Python `bytes.split(sep, maxsplit=-1)`. */
        inline List<Derived> split(
            const Derived& sep,
            Py_ssize_t maxsplit = -1
        ) const {
            return reinterpret_steal<List<Derived>>(attr<"split">()(sep, maxsplit).release());
        }

        /* Equivalent to Python `bytes.splitlines()`. */
        inline List<Derived> splitlines(bool keepends = false) const {
            return reinterpret_steal<List<Derived>>(attr<"splitlines">()(keepends).release());
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

}


template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "capitalize">                             : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "center">                                 : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "count">                                  : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "decode">                                 : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "endswith">                               : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "expandtabs">                             : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "find">                                   : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "hex">                                    : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "index">                                  : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isalnum">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isalpha">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isascii">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isdigit">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "islower">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isspace">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "istitle">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isupper">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "maketrans">                              : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "partition">                              : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "removeprefix">                           : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "removesuffix">                           : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "replace">                                : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rfind">                                  : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rindex">                                 : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rjust">                                  : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rpartition">                             : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rsplit">                                 : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rstrip">                                 : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "split">                                  : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "splitlines">                             : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "startswith">                             : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "strip">                                  : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "swapcase">                               : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "title">                                  : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "translate">                              : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "upper">                                  : Returns<Function> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "zfill">                                  : Returns<Function> {};

template <std::derived_from<impl::IBytesTag> T>
struct __len__<T>                                               : Returns<size_t> {};
template <std::derived_from<Bytes> T>
struct __hash__<T>                                              : Returns<size_t> {};
template <std::derived_from<impl::IBytesTag> T>
struct __iter__<T>                                              : Returns<Int> {};
template <std::derived_from<impl::IBytesTag> T>
struct __reversed__<T>                                          : Returns<Int> {};
template <std::derived_from<impl::IBytesTag> T>
struct __contains__<T, Object>                                  : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> T, impl::int_like Key>
struct __contains__<T, Key>                                     : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> T, impl::anybytes_like Key>
struct __contains__<T, Key>                                     : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getitem__<T, Object>                                   : Returns<Object> {};
template <std::derived_from<impl::IBytesTag> T, impl::int_like Key>
struct __getitem__<T, Key>                                      : Returns<Int> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getitem__<T, Slice>                                    : Returns<T> {};
template <std::derived_from<impl::IBytesTag> L>
struct __lt__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> L, impl::anybytes_like R>
struct __lt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> L>
struct __le__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> L, impl::anybytes_like R>
struct __le__<L, R>                                             : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> L>
struct __gt__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> L, impl::anybytes_like R>
struct __gt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> L>
struct __ge__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> L, impl::anybytes_like R>
struct __ge__<L, R>                                             : Returns<bool> {};
template <std::derived_from<impl::IBytesTag> L>
struct __add__<L, Object>                                       : Returns<L> {};
template <std::derived_from<impl::IBytesTag> L, impl::anybytes_like R>
struct __add__<L, R>                                            : Returns<L> {};
template <std::derived_from<impl::IBytesTag> L>
struct __iadd__<L, Object>                                      : Returns<L&> {};
template <std::derived_from<impl::IBytesTag> L, impl::anybytes_like R>
struct __iadd__<L, R>                                           : Returns<L&> {};
template <std::derived_from<impl::IBytesTag> L>
struct __mul__<L, Object>                                       : Returns<L> {};
template <std::derived_from<impl::IBytesTag> L, impl::int_like R>
struct __mul__<L, R>                                            : Returns<L> {};
template <std::derived_from<impl::IBytesTag> L>
struct __imul__<L, Object>                                      : Returns<L&> {};
template <std::derived_from<impl::IBytesTag> L, impl::int_like R>
struct __imul__<L, R>                                           : Returns<L&> {};


/////////////////////
////    BYTES    ////
/////////////////////


/* Represents a statically-typed Python `bytes` object in C++. */
class Bytes : public impl::IBytes<Bytes> {
    using Base = impl::IBytes<Bytes>;

public:
    static const Type type;
    template <typename T>
    static consteval bool check() {
        return impl::bytes_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if (impl::cpp_like<T>) {
            return check<T>();

        } else if constexpr (check<T>()) {
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

    template <impl::pybind11_like T> requires (check<T>())
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

    /* Equivalent to Python (static) `bytes.fromhex(string)`. */
    inline static Bytes fromhex(const Str& string);

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

};


namespace impl {
namespace ops {

    template <typename Return, std::derived_from<Bytes> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return PyBytes_GET_SIZE(self.ptr());
        }
    };

    template <typename Return, std::derived_from<Bytes> L, typename R>
    struct iadd<Return, L, R> {
        static void operator()(L& lhs, const to_object<R>& rhs) {
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

}
}


/////////////////////////
////    BYTEARRAY    ////
/////////////////////////


/* Represents a statically-typed Python `bytearray` in C++. */
class ByteArray : public impl::IBytes<ByteArray> {
    using Base = impl::IBytes<ByteArray>;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return impl::bytearray_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
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

    template <impl::pybind11_like T> requires (check<T>())
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

    /* Equivalent to Python (static) `bytes.fromhex(string)`. */
    inline static ByteArray fromhex(const Str& string);

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

};


namespace impl {
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
            lhs = add<Return, L, R>::operator()(lhs, rhs);
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, ByteArray> || std::derived_from<R, ByteArray>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<ByteArray> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

}
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_BYTES_H
