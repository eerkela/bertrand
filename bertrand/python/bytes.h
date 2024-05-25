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


template <std::derived_from<Bytes> T>
struct __getattr__<T, "capitalize">                             : Returns<Function<
    T()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "center">                                 : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillchar", const T&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "count">                                  : Returns<Function<
    Int(
        typename Arg<"sub", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "decode">                                 : Returns<Function<
    Str(
        typename Arg<"encoding", const Str&>::opt,
        typename Arg<"errors", const Str&>::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "endswith">                               : Returns<Function<
    Bool(
        typename Arg<"suffix", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "expandtabs">                             : Returns<Function<
    T(typename Arg<"tabsize", const Int&>::opt)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "find">                                   : Returns<Function<
    Int(
        typename Arg<"sub", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "hex">                                    : Returns<Function<
    Str(
        typename Arg<"sep", const T&>::opt,
        typename Arg<"bytes_per_sep", const Int&>::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "index">                                  : Returns<Function<
    Int(
        typename Arg<"value", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "isalnum">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "isalpha">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "isascii">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "isdigit">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "islower">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "isspace">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "istitle">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "isupper">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "join">                                   : Returns<Function<
    T(typename Arg<"iterable", const Object&>::pos)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "ljust">                                  : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillbyte", const T&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "lower">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "lstrip">                                 : Returns<Function<
    T(typename Arg<"chars", const T&>::opt)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "maketrans">                              : Returns<Function<
    Dict<T, T>(
        typename Arg<"from", const T&>::pos,
        typename Arg<"to", const T&>::pos
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "partition">                              : Returns<Function<
    Tuple<T>(typename Arg<"sep", const T&>::pos)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "removeprefix">                           : Returns<Function<
    T(typename Arg<"prefix", const T&>::pos)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "removesuffix">                           : Returns<Function<
    T(typename Arg<"suffix", const T&>::pos)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "replace">                                : Returns<Function<
    T(
        typename Arg<"old", const T&>::pos,
        typename Arg<"new", const T&>::pos,
        typename Arg<"count", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "rfind">                                  : Returns<Function<
    Int(
        typename Arg<"sub", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "rindex">                                 : Returns<Function<
    Int(
        typename Arg<"sub", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "rjust">                                  : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillbyte", const T&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "rpartition">                             : Returns<Function<
    Tuple<T>(typename Arg<"sep", const T&>::pos)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "rsplit">                                 : Returns<Function<
    List<T>(
        typename Arg<"sep", const T&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "rstrip">                                 : Returns<Function<
    T(typename Arg<"chars", const T&>::opt)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "split">                                  : Returns<Function<
    List<T>(
        typename Arg<"sep", const T&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "splitlines">                             : Returns<Function<
    List<T>(typename Arg<"keepends", const Bool&>::opt)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "startswith">                             : Returns<Function<
    Bool(
        typename Arg<"prefix", const T&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "strip">                                  : Returns<Function<
    T(typename Arg<"chars", const T&>::opt)
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "swapcase">                               : Returns<Function<
    T()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "title">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "translate">                              : Returns<Function<
    T(
        typename Arg<"table", const Dict<T, T>&>::pos,
        typename Arg<"delete", const T&>::opt
    )
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "upper">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<Bytes> T>
struct __getattr__<T, "zfill">                                  : Returns<Function<
    T(typename Arg<"width", const Int&>::pos)
>> {};


/////////////////////
////    BYTES    ////
/////////////////////


/* Represents a statically-typed Python `bytes` object in C++. */
class Bytes : public Object {
    using Base = Object;

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

    /* Access the internal buffer of the bytes object.  Note that this implicitly
    includes an extra null byte at the end of the buffer, regardless of any nulls that
    were already present.  The data should not be modified unless the bytes object was
    just created via `py::Bytes(size)`, and it should never be deallocated. */
    char* data() const {
        return PyBytes_AS_STRING(this->ptr());
    }

    auto copy() const {
        return reinterpret_steal<Bytes>(PyBytes_FromStringAndSize(
            PyBytes_AS_STRING(this->ptr()),
            PyBytes_GET_SIZE(this->ptr())
        ));
    }

    Py_ssize_t count(
        const Bytes& value,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        if (start != 0 || stop != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
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
            Py_ssize_t result = PySequence_Count(this->ptr(), value.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    Py_ssize_t index(
        const Bytes& value,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        if (start != 0 || stop != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
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
            Py_ssize_t result = PySequence_Index(this->ptr(), value.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    BERTRAND_STATIC_METHOD(Bytes, fromhex)
    BERTRAND_STATIC_METHOD(Bytes, maketrans)
    BERTRAND_METHOD(capitalize)
    BERTRAND_METHOD(center)
    BERTRAND_METHOD(decode)
    BERTRAND_METHOD(endswith)
    BERTRAND_METHOD(expandtabs)
    BERTRAND_METHOD(find)
    BERTRAND_METHOD(hex)
    BERTRAND_METHOD(isalnum)
    BERTRAND_METHOD(isalpha)
    BERTRAND_METHOD(isascii)
    BERTRAND_METHOD(isdigit)
    BERTRAND_METHOD(islower)
    BERTRAND_METHOD(isspace)
    BERTRAND_METHOD(istitle)
    BERTRAND_METHOD(isupper)
    BERTRAND_METHOD(join)
    BERTRAND_METHOD(ljust)
    BERTRAND_METHOD(lower)
    BERTRAND_METHOD(lstrip)
    BERTRAND_METHOD(partition)
    BERTRAND_METHOD(removeprefix)
    BERTRAND_METHOD(removesuffix)
    BERTRAND_METHOD(replace)
    BERTRAND_METHOD(rfind)
    BERTRAND_METHOD(rindex)
    BERTRAND_METHOD(rjust)
    BERTRAND_METHOD(rpartition)
    BERTRAND_METHOD(rsplit)
    BERTRAND_METHOD(rstrip)
    BERTRAND_METHOD(split)
    BERTRAND_METHOD(splitlines)
    BERTRAND_METHOD(startswith)
    BERTRAND_METHOD(strip)
    BERTRAND_METHOD(swapcase)
    BERTRAND_METHOD(title)
    BERTRAND_METHOD(translate)
    BERTRAND_METHOD(upper)
    BERTRAND_METHOD(zfill)

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
class ByteArray : public Object {
    using Base = Object;

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

    /* Access the internal buffer of the bytearray object.  The data can be modified
    in-place, but should never be deallocated. */
    char* data() const {
        return PyByteArray_AS_STRING(this->ptr());
    }

    auto copy() const {
        return reinterpret_steal<ByteArray>(PyByteArray_FromStringAndSize(
            PyByteArray_AS_STRING(this->ptr()),
            PyByteArray_GET_SIZE(this->ptr())
        ));
    }

    Py_ssize_t count(
        const ByteArray& value,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        if (start != 0 || stop != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
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
            Py_ssize_t result = PySequence_Count(this->ptr(), value.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    Py_ssize_t index(
        const ByteArray& value,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        if (start != 0 || stop != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
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
            Py_ssize_t result = PySequence_Index(this->ptr(), value.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    BERTRAND_STATIC_METHOD(ByteArray, fromhex)
    BERTRAND_STATIC_METHOD(ByteArray, maketrans)
    BERTRAND_METHOD(capitalize)
    BERTRAND_METHOD(center)
    BERTRAND_METHOD(decode)
    BERTRAND_METHOD(endswith)
    BERTRAND_METHOD(expandtabs)
    BERTRAND_METHOD(find)
    BERTRAND_METHOD(hex)
    BERTRAND_METHOD(isalnum)
    BERTRAND_METHOD(isalpha)
    BERTRAND_METHOD(isascii)
    BERTRAND_METHOD(isdigit)
    BERTRAND_METHOD(islower)
    BERTRAND_METHOD(isspace)
    BERTRAND_METHOD(istitle)
    BERTRAND_METHOD(isupper)
    BERTRAND_METHOD(join)
    BERTRAND_METHOD(ljust)
    BERTRAND_METHOD(lower)
    BERTRAND_METHOD(lstrip)
    BERTRAND_METHOD(partition)
    BERTRAND_METHOD(removeprefix)
    BERTRAND_METHOD(removesuffix)
    BERTRAND_METHOD(replace)
    BERTRAND_METHOD(rfind)
    BERTRAND_METHOD(rindex)
    BERTRAND_METHOD(rjust)
    BERTRAND_METHOD(rpartition)
    BERTRAND_METHOD(rsplit)
    BERTRAND_METHOD(rstrip)
    BERTRAND_METHOD(split)
    BERTRAND_METHOD(splitlines)
    BERTRAND_METHOD(startswith)
    BERTRAND_METHOD(strip)
    BERTRAND_METHOD(swapcase)
    BERTRAND_METHOD(title)
    BERTRAND_METHOD(translate)
    BERTRAND_METHOD(upper)
    BERTRAND_METHOD(zfill)

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
