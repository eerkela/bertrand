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

#include "code.h"  // TODO: unnecessary, delete


namespace bertrand {
namespace py {


namespace impl {

    // TODO: delete this base class and just reimplement them in the Bytes classes for
    // consistency

    struct IBytesTag {};

    template <typename Derived>
    class IBytes : public Object, public IBytesTag {
        using Base = Object;

        Derived& self() { return static_cast<Derived&>(*this); }
        const Derived& self() const { return static_cast<const Derived&>(*this); }

    public:
        using Base::Base;

        Derived capitalize() const {
            return impl::call_method<"capitalize">(self());
        }

        Derived center(const Int& width) const {
            return impl::call_method<"center">(self(), width);
        }

        Derived center(size_t width, const Derived& fillbyte) const {
            return reinterpret_steal<Derived>(attr<"center">()(width, fillbyte).release());
        }

        Py_ssize_t count(
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

        Str decode(
            const Str& encoding = "utf-8",
            const Str& errors = "strict"
        ) const {
            return reinterpret_steal<Str>(attr<"decode">()(encoding, errors).release());
        }

        bool endswith(
            const Derived& suffix,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<bool>(attr<"endswith">()(suffix, start, end));
        }

        Derived expandtabs(size_t tabsize = 8) const {
            return reinterpret_steal<Derived>(attr<"expandtabs">()(tabsize).release());
        }

        Py_ssize_t find(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<size_t>(attr<"find">()(sub, start, end));
        }

        Str hex() const {
            return reinterpret_steal<Str>(attr<"hex">()().release());
        }

        Str hex(const Derived& sep, Py_ssize_t bytes_per_sep = 1) const {
            return reinterpret_steal<Str>(attr<"hex">()(sep, bytes_per_sep).release());
        }

        Py_ssize_t index(
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

        bool isalnum() const {
            return static_cast<bool>(attr<"isalnum">()());
        }

        bool isalpha() const {
            return static_cast<bool>(attr<"isalpha">()());
        }

        bool isascii() const {
            return static_cast<bool>(attr<"isascii">()());
        }

        bool isdigit() const {
            return static_cast<bool>(attr<"isdigit">()());
        }

        bool islower() const {
            return static_cast<bool>(attr<"islower">()());
        }

        bool isspace() const {
            return static_cast<bool>(attr<"isspace">()());
        }

        bool istitle() const {
            return static_cast<bool>(attr<"istitle">()());
        }

        bool isupper() const {
            return static_cast<bool>(attr<"isupper">()());
        }

        Derived join(const Object& iterable) const {
            return reinterpret_steal<Derived>(attr<"join">()(iterable).release());
        }

        Derived ljust(size_t width) const {
            return reinterpret_steal<Derived>(attr<"ljust">()(width).release());
        }

        Derived ljust(size_t width, const Derived& fillbyte) const {
            return reinterpret_steal<Derived>(attr<"ljust">()(width, fillbyte).release());
        }

        Derived lstrip() const {
            return reinterpret_steal<Derived>(attr<"lstrip">()().release());
        }

        Derived lstrip(const Derived& chars) const {
            return reinterpret_steal<Derived>(attr<"lstrip">()(chars).release());
        }

        static Dict<> maketrans(const Derived& from, const Derived& to);

        Tuple<Derived> partition(const Derived& sep) const {
            return reinterpret_steal<Tuple<Derived>>(attr<"partition">()(sep).release());
        }

        Derived removeprefix(const Derived& prefix) const {
            return reinterpret_steal<Derived>(attr<"removeprefix">()(prefix).release());
        }

        Derived removesuffix(const Derived& suffix) const {
            return reinterpret_steal<Derived>(attr<"removesuffix">()(suffix).release());
        }

        Derived replace(
            const Derived& sub,
            const Derived& repl
        ) const {
            return reinterpret_steal<Derived>(attr<"replace">()(sub, repl).release());
        }

        Derived replace(
            const Derived& sub,
            const Derived& repl,
            Py_ssize_t maxcount
        ) const {
            return reinterpret_steal<Derived>(
                attr<"replace">()(sub, repl, maxcount).release()
            );
        }

        Py_ssize_t rfind(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<size_t>(attr<"rfind">()(sub, start, end));
        }

        Py_ssize_t rindex(
            const Derived& sub,
            Py_ssize_t start = 0,
            Py_ssize_t end = -1
        ) const {
            return static_cast<size_t>(attr<"rindex">()(sub, start, end));
        }

        Derived rjust(size_t width) const {
            return reinterpret_steal<Derived>(attr<"rjust">()(width).release());
        }

        Derived rjust(size_t width, const Derived& fillbyte) const {
            return reinterpret_steal<Derived>(attr<"rjust">()(width, fillbyte).release());
        }

        Tuple<Derived> rpartition(const Derived& sep) const {
            return reinterpret_steal<Tuple<Derived>>(attr<"rpartition">()(sep).release());
        }

        List<Derived> rsplit() const {
            return reinterpret_steal<List<Derived>>(attr<"rsplit">()().release());
        }

        List<Derived> rsplit(
            const Derived& sep,
            Py_ssize_t maxsplit = -1
        ) const {
            return reinterpret_steal<List<Derived>>(attr<"rsplit">()(sep, maxsplit).release());
        }

        Derived rstrip() const {
            return reinterpret_steal<Derived>(attr<"rstrip">()().release());
        }

        Derived rstrip(const Derived& chars) const {
            return reinterpret_steal<Derived>(attr<"rstrip">()(chars).release());
        }

        List<Derived> split() const {
            return reinterpret_steal<List<Derived>>(attr<"split">()().release());
        }

        List<Derived> split(
            const Derived& sep,
            Py_ssize_t maxsplit = -1
        ) const {
            return reinterpret_steal<List<Derived>>(attr<"split">()(sep, maxsplit).release());
        }

        List<Derived> splitlines(bool keepends = false) const {
            return reinterpret_steal<List<Derived>>(attr<"splitlines">()(keepends).release());
        }

        bool startswith(
            const Derived& prefix,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            return static_cast<bool>(attr<"startswith">()(prefix, start, stop));
        }

        Derived strip() const {
            return reinterpret_steal<Derived>(attr<"strip">()().release());
        }

        Derived strip(const Derived& chars) const {
            return reinterpret_steal<Derived>(attr<"strip">()(chars).release());
        }

        Derived swapcase() const {
            return reinterpret_steal<Derived>(attr<"swapcase">()().release());
        }

        Derived title() const {
            return reinterpret_steal<Derived>(attr<"title">()().release());
        }

        Derived translate(const Dict<>& table, const Derived& del = "") const {
            return reinterpret_steal<Derived>(attr<"translate">()(table, del).release());
        }

        Derived upper() const {
            return reinterpret_steal<Derived>(attr<"upper">()().release());
        }

        Derived zfill(size_t width) const {
            return reinterpret_steal<Derived>(attr<"zfill">()(width).release());
        }

    };

}


template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "capitalize">                             : Returns<Function<
    T()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "center">                                 : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillchar", const T&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "count">                                  : Returns<Function<
    Int(
        typename Arg<"sub", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "decode">                                 : Returns<Function<
    Str(
        typename Arg<"encoding", const Str&>::opt,
        typename Arg<"errors", const Str&>::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "endswith">                               : Returns<Function<
    Bool(
        typename Arg<"suffix", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "expandtabs">                             : Returns<Function<
    T(typename Arg<"tabsize", const Int&>::opt)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "find">                                   : Returns<Function<
    Int(
        typename Arg<"sub", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "hex">                                    : Returns<Function<
    Str(
        typename Arg<"sep", const T&>::opt,
        typename Arg<"bytes_per_sep", const Int&>::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "index">                                  : Returns<Function<
    Int(
        typename Arg<"value", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isalnum">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isalpha">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isascii">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isdigit">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "islower">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isspace">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "istitle">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "isupper">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "join">                                   : Returns<Function<
    T(typename Arg<"iterable", const Object&>::pos)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "ljust">                                  : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillbyte", const T&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "lower">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "lstrip">                                 : Returns<Function<
    T(typename Arg<"chars", const T&>::opt)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "maketrans">                              : Returns<Function<
    Dict<T, T>(
        typename Arg<"from", const T&>::pos,
        typename Arg<"to", const T&>::pos
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "partition">                              : Returns<Function<
    Tuple<T>(typename Arg<"sep", const T&>::pos)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "removeprefix">                           : Returns<Function<
    T(typename Arg<"prefix", const T&>::pos)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "removesuffix">                           : Returns<Function<
    T(typename Arg<"suffix", const T&>::pos)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "replace">                                : Returns<Function<
    T(
        typename Arg<"old", const T&>::pos,
        typename Arg<"new", const T&>::pos,
        typename Arg<"count", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rfind">                                  : Returns<Function<
    Int(
        typename Arg<"sub", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rindex">                                 : Returns<Function<
    Int(
        typename Arg<"sub", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rjust">                                  : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillbyte", const T&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rpartition">                             : Returns<Function<
    Tuple<T>(typename Arg<"sep", const T&>::pos)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rsplit">                                 : Returns<Function<
    List<T>(
        typename Arg<"sep", const T&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "rstrip">                                 : Returns<Function<
    T(typename Arg<"chars", const T&>::opt)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "split">                                  : Returns<Function<
    List<T>(
        typename Arg<"sep", const T&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "splitlines">                             : Returns<Function<
    List<T>(typename Arg<"keepends", const Bool&>::opt)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "startswith">                             : Returns<Function<
    Bool(
        typename Arg<"prefix", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "strip">                                  : Returns<Function<
    T(typename Arg<"chars", const T&>::opt)
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "swapcase">                               : Returns<Function<
    T()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "title">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "translate">                              : Returns<Function<
    T(
        typename Arg<"table", const Dict<T, T>&>::pos,
        typename Arg<"delete", const T&>::opt
    )
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "upper">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<impl::IBytesTag> T>
struct __getattr__<T, "zfill">                                  : Returns<Function<
    T(typename Arg<"width", const Int&>::pos)
>> {};


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
    static Bytes fromhex(const Str& string);

    /* Access the internal buffer of the bytes object.  Note that this implicitly
    includes an extra null byte at the end of the buffer, regardless of any nulls that
    were already present.  The data should not be modified unless the bytes object was
    just created via `py::Bytes(size)`, and it should never be deallocated. */
    char* data() const {
        return PyBytes_AS_STRING(this->ptr());
    }

    /* Copy the contents of the buffer into a new py::Bytes object. */
    Bytes copy() const {
        return reinterpret_steal<Bytes>(PyBytes_FromStringAndSize(
            PyBytes_AS_STRING(this->ptr()),
            PyBytes_GET_SIZE(this->ptr())
        ));
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
    static ByteArray fromhex(const Str& string);

    /* Access the internal buffer of the bytearray object.  The data can be modified
    in-place, but should never be deallocated. */
    char* data() const {
        return PyByteArray_AS_STRING(this->ptr());
    }

    /* Copy the contents of the buffer into a new py::ByteArray object. */
    ByteArray copy() const {
        return reinterpret_steal<ByteArray>(PyByteArray_FromStringAndSize(
            PyByteArray_AS_STRING(this->ptr()),
            PyByteArray_GET_SIZE(this->ptr())
        ));
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
