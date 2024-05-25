#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_STRING_H
#define BERTRAND_PYTHON_STRING_H

#include "common.h"
#include "int.h"
#include "list.h"
#include "tuple.h"
#include "set.h"
#include "dict.h"
#ifdef BERTRAND_HAS_STD_FORMAT
    #include <format>
#endif


namespace bertrand {
namespace py {


template <std::derived_from<Str> T>
struct __getattr__<T, "capitalize">                             : Returns<Function<
    T()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "casefold">                               : Returns<Function<
    T()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "center">                                 : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillchar", const Str&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "copy">                                   : Returns<Function<
    T()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "count">                                  : Returns<Function<
    Int(
        typename Arg<"sub", const Str&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "encode">                                 : Returns<Function<
    Bytes(
        typename Arg<"encoding", const Str&>::opt,
        typename Arg<"errors", const Str&>::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "endswith">                               : Returns<Function<
    Bool(
        typename Arg<"suffix", const Str&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "expandtabs">                             : Returns<Function<
    T(typename Arg<"tabsize", const Int&>::opt)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "find">                                   : Returns<Function<
    Int(
        typename Arg<"sub", const Str&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "format">                                 : Returns<Function<
    T(
        typename Arg<"args", const Object&>::args,
        typename Arg<"kwargs", const Object&>::kwargs
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "format_map">                             : Returns<Function<
    T(typename Arg<"mapping", const Object&>::pos)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "index">                                  : Returns<Function<
    Int(
        typename Arg<"sub", const Str&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isalnum">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isalpha">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isascii">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isdecimal">                              : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isdigit">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isidentifier">                           : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "islower">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isnumeric">                              : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isprintable">                            : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isspace">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "istitle">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isupper">                                : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "join">                                   : Returns<Function<
    T(typename Arg<"iterable", const Object&>::pos)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "ljust">                                  : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillchar", const Str&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "lower">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "lstrip">                                 : Returns<Function<
    T(typename Arg<"chars", const Str&>::opt)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "maketrans">                              : Returns<Function<
    Dict<T, T>(
        typename Arg<"x", const Object&>::pos,
        typename Arg<"y", const Object&>::pos::opt,
        typename Arg<"z", const Object&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "partition">                              : Returns<Function<
    Tuple<T>(typename Arg<"sep", const Str&>::pos)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "removeprefix">                           : Returns<Function<
    T(typename Arg<"prefix", const Str&>::pos)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "removesuffix">                           : Returns<Function<
    T(typename Arg<"suffix", const Str&>::pos)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "replace">                                : Returns<Function<
    T(
        typename Arg<"old", const Str&>::pos,
        typename Arg<"new", const Str&>::pos,
        typename Arg<"count", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rfind">                                  : Returns<Function<
    Int(
        typename Arg<"sub", const Str&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rindex">                                 : Returns<Function<
    Int(
        typename Arg<"sub", const Str&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rjust">                                  : Returns<Function<
    T(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillchar", const Str&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rpartition">                             : Returns<Function<
    Tuple<T>(typename Arg<"sep", const Str&>::pos)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rsplit">                                 : Returns<Function<
    List<T>(
        typename Arg<"sep", const Str&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rstrip">                                 : Returns<Function<
    T(typename Arg<"chars", const Str&>::opt)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "split">                                  : Returns<Function<
    List<T>(
        typename Arg<"sep", const Str&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "splitlines">                             : Returns<Function<
    List<T>(typename Arg<"keepends", const Bool&>::opt)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "startswith">                             : Returns<Function<
    Bool(
        typename Arg<"prefix", const Str&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "strip">                                  : Returns<Function<
    T(typename Arg<"chars", const Str&>::opt)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "swapcase">                               : Returns<Function<
    T()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "title">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "translate">                              : Returns<Function<
    T(typename Arg<"table", const Dict<Str, Str>&>::pos)
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "upper">                                  : Returns<Function<
    T()
>> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "zfill">                                  : Returns<Function<
    T(typename Arg<"width", const Int&>::pos)
>> {};


/* Represents a statically-typed Python string in C++. */
class Str : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return impl::str_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyUnicode_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Str(Handle h, const borrowed_t& t) : Base(h, t) {}
    Str(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Str(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Str(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Str>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to empty string. */
    Str() : Base(PyUnicode_FromStringAndSize("", 0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly convert a string literal into a py::Str object. */
    template <size_t N>
    Str(const char(&string)[N]) : Base(
        PyUnicode_FromStringAndSize(string, N - 1),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly convert a C-style string array into a py::Str object. */
    template <impl::cpp_like T> requires (std::convertible_to<T, const char*>)
    Str(const T& string) : Base(PyUnicode_FromString(string), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    // TODO: this causes an ambiguity when converting from Object to Str

    /* Implicitly convert a C++ std::string into a py::Str object. */
    template <impl::cpp_like T>
        requires (
            !std::convertible_to<T, const char*> &&
            std::convertible_to<T, std::string>
        )
    Str(const T& string) : Base(nullptr, stolen_t{}) {
        std::string s = string;
        m_ptr = PyUnicode_FromStringAndSize(s.c_str(), s.size());
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Implicitly convert a C++ std::string_view into a py::Str object. */
    template <impl::cpp_like T>
        requires (
            !std::convertible_to<T, const char*> &&
            !std::convertible_to<T, std::string> &&
            std::convertible_to<T, std::string_view>
        )
    Str(const T& string) : Base(nullptr, stolen_t{}) {
        std::string_view s = string;
        m_ptr = PyUnicode_FromStringAndSize(s.data(), s.size());
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert an arbitrary Python object into a py::Str representation. */
    template <impl::python_like T> requires (!impl::str_like<T>)
    explicit Str(const T& obj) : Base(PyObject_Str(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly convert an arbitrary C++ object into a py::Str representation. */
    template <impl::cpp_like T>
        requires (
            !std::convertible_to<T, const char*> &&
            !std::convertible_to<T, std::string> &&
            !std::convertible_to<T, std::string_view>
        )
    explicit Str(const T& obj) : Base(PyObject_Str(Object(obj).ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    #ifdef BERTRAND_HAS_STD_FORMAT

        /* Construct a Python unicode string from a std::format()-style interpolated
        string. */
        template <typename... Args> requires (sizeof...(Args) > 0)
        explicit Str(const std::string_view& format, Args&&... args) :
            Base(nullptr, stolen_t{})
        {
            std::string result = std::vformat(
                format,
                std::make_format_args(std::forward<Args>(args))...
            );
            m_ptr = PyUnicode_FromStringAndSize(result.c_str(), result.size());
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
        }

        /* Construct a Python unicode string from a std::format()-style interpolated string
        with an optional locale. */
        template <typename... Args> requires (sizeof...(Args) > 0)
        explicit Str(
            const std::locale& locale,
            const std::string_view& format,
            Args&&... args
        ) : Base(nullptr, stolen_t{}) {
            std::string result = std::vformat(
                locale,
                format,
                std::make_format_args(std::forward<Args>(args))...
            );
            m_ptr = PyUnicode_FromStringAndSize(result.c_str(), result.size());
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
        }

        /* Construct a Python unicode string from a std::format()-style interpolated string.
        This overload is chosen when the format string is given as a Python unicode
        string. */
        template <impl::python_like T, typename... Args>
            requires (sizeof...(Args) > 0 && impl::str_like<T>)
        explicit Str(const T& format, Args&&... args) : Str(
            format.template cast<std::string>(),
            std::forward<Args>(args)...
        ) {}

        /* Construct a Python unicode string from a std::format()-style interpolated string
        with an optional locale. */
        template <impl::python_like T, typename... Args>
            requires (sizeof...(Args) > 0 && impl::str_like<T>)
        explicit Str(
            const std::locale& locale,
            const T& format,
            Args&&... args
        ) : Str(
            locale,
            format.template cast<std::string>(),
            std::forward<Args>(args)...
        ) {}

    #endif

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Get the underlying unicode buffer. */
    void* data() const noexcept {
        return PyUnicode_DATA(this->ptr());
    }

    /* Get the kind of the string, indicating the size of the unicode points stored
    within. */
    int kind() const noexcept {
        return PyUnicode_KIND(this->ptr());
    }

    /* Get the maximum code point that is suitable for creating another string based
    on this string. */
    Py_UCS4 max_char() const noexcept {
        return PyUnicode_MAX_CHAR_VALUE(this->ptr());
    }

    /* Fill the string with a given character.  The input must be convertible to a
    string with a single character. */
    void fill(const Str& str) {
        if (str.size() != 1) {
            std::ostringstream msg;
            msg << "fill character must be a single character, not '" << str << "'";
            throw ValueError(msg.str());
        }
        Py_UCS4 code = PyUnicode_ReadChar(str.ptr(), 0);
        if (code == (Py_UCS4)-1 && PyErr_Occurred()) {
            Exception::from_python();
        }
        if (PyUnicode_Fill(this->ptr(), 0, size(), code) == -1) {
            Exception::from_python();
        }
    }

    /* Fill the string with a given character, given as a raw Python unicode point. */
    void fill(Py_UCS4 ch) {
        if (PyUnicode_Fill(this->ptr(), 0, size(), ch) == -1) {
            Exception::from_python();
        }
    }

    /* Return a substring from this string. */
    Str substring(Py_ssize_t start = 0, Py_ssize_t end = -1) const {
        PyObject* result = PyUnicode_Substring(this->ptr(), start, end);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `str.copy()`. */
    Str copy() const {
        PyObject* result = PyUnicode_New(size(), max_char());
        if (result == nullptr) {
            Exception::from_python();
        }
        if (PyUnicode_CopyCharacters(
            result,
            0,
            this->ptr(),
            0,
            size()
        )) {
            Py_DECREF(result);
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Count the number of occurrences of a substring within the string. */
    size_t count(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Count(
            this->ptr(),
            sub.ptr(),
            start,
            stop
        );
        if (result < 0) {
            Exception::from_python();
        }
        return static_cast<size_t>(result);
    }

    /* Equivalent to Python `str.endswith(suffix[, start[, end]])`. */
    bool endswith(
        const Str& suffix,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        int result = PyUnicode_Tailmatch(
            this->ptr(),
            suffix.ptr(),
            start,
            stop,
            1
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`. */
    Py_ssize_t find(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_Find(
            this->ptr(),
            sub.ptr(),
            start,
            stop,
            1
        );
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`, except that the substring
    is given as a single Python unicode character. */
    Py_ssize_t find(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
    }

    /* Equivalent to Python `str.index(sub[, start[, end]])`. */
    Py_ssize_t index(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Find(
            this->ptr(),
            sub.ptr(),
            start,
            stop,
            1
        );
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.index(sub[, start[, end]])`, except that the substring
    is given as a single Python unicode character. */
    Py_ssize_t index(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_FindChar(
            this->ptr(),
            ch,
            start,
            stop,
            1
        );
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent of Python `str.join(iterable)`. */
    template <impl::is_iterable T>
    Str join(const T& iterable) const {
        PyObject* result = PyUnicode_Join(
            this->ptr(),
            Object(iterable).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Equivalent of Python `str.join(iterable)`, where iterable is given as a
    braced initializer list. */
    Str join(const std::initializer_list<Str>& iterable) const {
        return join(py::List(iterable));
    }

    /* Equivalent to Python `str.replace(old, new, count)`. */
    Str replace(const Str& sub, const Str& repl, Py_ssize_t maxcount = -1) const {
        PyObject* result = PyUnicode_Replace(
            this->ptr(),
            sub.ptr(),
            repl.ptr(),
            maxcount
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`. */
    Py_ssize_t rfind(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_Find(
            this->ptr(),
            sub.ptr(),
            start,
            stop,
            -1
        );
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`, except that the
    substring is given as a single Python unicode character. */
    Py_ssize_t rfind(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, -1);
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`. */
    Py_ssize_t rindex(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Find(
            this->ptr(),
            sub.ptr(),
            start,
            stop,
            -1
        );
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`, except that the
    substring is given as a single Python unicode character. */
    Py_ssize_t rindex(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_FindChar(
            this->ptr(),
            ch,
            start,
            stop,
            -1
        );
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.split()`. */
    List<Str> split() const {
        PyObject* result = PyUnicode_Split(this->ptr(), nullptr, -1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`. */
    List<Str> split(const Str& sep, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(this->ptr(), sep.ptr(), maxsplit);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    List<Str> splitlines(bool keepends = false) const {
        PyObject* result = PyUnicode_Splitlines(this->ptr(), keepends);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    /* Equivalent to Python `str.startswith(prefix[, start[, end]])`. */
    bool startswith(
        const Str& prefix,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        int result = PyUnicode_Tailmatch(
            this->ptr(),
            prefix.ptr(),
            start,
            stop,
            -1
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    BERTRAND_METHOD(capitalize)
    BERTRAND_METHOD(casefold)
    BERTRAND_METHOD(center)
    BERTRAND_METHOD(encode)
    BERTRAND_METHOD(expandtabs)
    BERTRAND_METHOD(format)
    BERTRAND_METHOD(format_map)
    BERTRAND_METHOD(isalnum)
    BERTRAND_METHOD(isalpha)
    BERTRAND_METHOD(isascii)
    BERTRAND_METHOD(isdecimal)
    BERTRAND_METHOD(isdigit)
    BERTRAND_METHOD(isidentifier)
    BERTRAND_METHOD(islower)
    BERTRAND_METHOD(isnumeric)
    BERTRAND_METHOD(isprintable)
    BERTRAND_METHOD(isspace)
    BERTRAND_METHOD(istitle)
    BERTRAND_METHOD(isupper)
    BERTRAND_METHOD(ljust)
    BERTRAND_METHOD(lower)
    BERTRAND_METHOD(lstrip)
    BERTRAND_STATIC_METHOD(Str, maketrans)
    BERTRAND_METHOD(partition)
    BERTRAND_METHOD(removeprefix)
    BERTRAND_METHOD(removesuffix)
    BERTRAND_METHOD(rjust)
    BERTRAND_METHOD(rpartition)
    BERTRAND_METHOD(rsplit)
    BERTRAND_METHOD(rstrip)
    BERTRAND_METHOD(strip)
    BERTRAND_METHOD(swapcase)
    BERTRAND_METHOD(title)
    BERTRAND_METHOD(translate)
    BERTRAND_METHOD(upper)
    BERTRAND_METHOD(zfill)

};


namespace ops {

    template <typename Return, std::derived_from<Str> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return static_cast<size_t>(PyUnicode_GET_LENGTH(self.ptr()));
        }
    };

    template <typename Return, std::derived_from<Str> Self, typename Key>
    struct contains<Return, Self, Key> {
        static bool operator()(const Self& self, const impl::as_object_t<Key>& key) {
            int result = PyUnicode_Contains(self.ptr(), key.ptr());
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, Str> || std::derived_from<R, Str>)
    struct add<Return, L, R> {
        static Return operator()(const impl::as_object_t<L>& lhs, impl::as_object_t<R>& rhs) {
            PyObject* result = PyUnicode_Concat(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }

    };

    template <typename Return, std::derived_from<Str> L, typename R>
    struct iadd<Return, L, R> {
        static void operator()(L& lhs, const R& rhs) {
            lhs = add<Return, L, R>::operator()(lhs, rhs);
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, Str> || std::derived_from<R, Str>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<Str> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

}


}  // namespace py
}  // namespace bertrand


namespace std {

    template <>
    struct hash<bertrand::py::Str> {
        size_t operator()(const bertrand::py::Str& str) const {
            // ASCII string special case (taken directly from CPython source)
            // see: cpython/objects/setobject.c  -> set_contains_key()
            Py_ssize_t result = _PyASCIIObject_CAST(str.ptr())->hash;
            if (result == -1) {
                result = PyObject_Hash(str.ptr());  // fall back to PyObject_Hash()
                if (result == -1 && PyErr_Occurred()) {
                    bertrand::py::Exception::from_python();
                }
            }
            return static_cast<size_t>(result);
        }
    };

}


#endif  // BERTRAND_PYTHON_STRING_H
