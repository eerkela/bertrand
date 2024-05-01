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
struct __getattr__<T, "capitalize">                             : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "casefold">                               : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "center">                                 : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "copy">                                   : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "count">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "encode">                                 : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "endswith">                               : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "expandtabs">                             : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "find">                                   : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "format">                                 : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "format_map">                             : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "index">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isalnum">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isalpha">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isascii">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isdecimal">                              : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isdigit">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isidentifier">                           : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "islower">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isnumeric">                              : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isprintable">                            : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isspace">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "istitle">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "isupper">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "join">                                   : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "ljust">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "lower">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "lstrip">                                 : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "maketrans">                              : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "partition">                              : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "removeprefix">                           : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "removesuffix">                           : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "replace">                                : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rfind">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rindex">                                 : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rjust">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rpartition">                             : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rsplit">                                 : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "rstrip">                                 : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "split">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "splitlines">                             : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "startswith">                             : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "strip">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "swapcase">                               : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "title">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "translate">                              : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "upper">                                  : Returns<Function> {};
template <std::derived_from<Str> T>
struct __getattr__<T, "zfill">                                  : Returns<Function> {};

template <std::derived_from<Str> T>
struct __len__<T>                                               : Returns<size_t> {};
template <std::derived_from<Str> T>
struct __iter__<T>                                              : Returns<Str> {};
template <std::derived_from<Str> T>
struct __reversed__<T>                                          : Returns<Str> {};
template <std::derived_from<Str> T>
struct __contains__<T, Object>                                  : Returns<bool> {};
template <std::derived_from<Str> T, impl::str_like Key>
struct __contains__<T, Key>                                     : Returns<bool> {};
template <std::derived_from<Str> T>
struct __getitem__<T, Object>                                   : Returns<Str> {};
template <std::derived_from<Str> T, impl::int_like Key>
struct __getitem__<T, Key>                                      : Returns<Str> {};
template <std::derived_from<Str> T>
struct __getitem__<T, Slice>                                    : Returns<Str> {};
template <std::derived_from<Str> L>
struct __lt__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __lt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Str> L>
struct __le__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __le__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Str> L>
struct __ge__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __ge__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Str> L>
struct __gt__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __gt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Str> L>
struct __add__<L, Object>                                       : Returns<Str> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __add__<L, R>                                            : Returns<Str> {};
template <std::derived_from<Str> L>
struct __iadd__<L, Object>                                      : Returns<Str> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __iadd__<L, R>                                           : Returns<Str> {};
template <std::derived_from<Str> L>
struct __mul__<L, Object>                                       : Returns<Str> {};
template <std::derived_from<Str> L, impl::int_like R>
struct __mul__<L, R>                                            : Returns<Str> {};
template <std::derived_from<Str> L>
struct __imul__<L, Object>                                      : Returns<Str> {};
template <std::derived_from<Str> L, impl::int_like R>
struct __imul__<L, R>                                           : Returns<Str> {};


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
    inline void* data() const noexcept {
        return PyUnicode_DATA(this->ptr());
    }

    /* Get the kind of the string, indicating the size of the unicode points stored
    within. */
    inline int kind() const noexcept {
        return PyUnicode_KIND(this->ptr());
    }

    /* Get the maximum code point that is suitable for creating another string based
    on this string. */
    inline Py_UCS4 max_char() const noexcept {
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
    inline void fill(Py_UCS4 ch) {
        if (PyUnicode_Fill(this->ptr(), 0, size(), ch) == -1) {
            Exception::from_python();
        }
    }

    /* Return a substring from this string. */
    inline Str substring(Py_ssize_t start = 0, Py_ssize_t end = -1) const {
        PyObject* result = PyUnicode_Substring(this->ptr(), start, end);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `str.capitalize()`. */
    inline Str capitalize() const;

    /* Equivalent to Python `str.casefold()`. */
    inline Str casefold() const;

    /* Equivalent to Python `str.center(width)`. */
    inline Str center(const Int& width) const;

    /* Equivalent to Python `str.center(width, fillchar)`. */
    inline Str center(const Int& width, const Str& fillchar) const;

    /* Equivalent to Python `str.copy()`. */
    inline Str copy() const {
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
    inline size_t count(
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

    /* Equivalent to Python `str.encode(encoding)`. */
    inline Bytes encode(
        const Str& encoding = "utf-8",
        const Str& errors = "strict"
    ) const;  // defined in bytes.h

    /* Equivalent to Python `str.endswith(suffix[, start[, end]])`. */
    inline bool endswith(
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

    /* Equivalent to Python `str.expandtabs()`. */
    inline Str expandtabs(const Int& tabsize = 8) const;

    /* Equivalent to Python `str.find(sub[, start[, stop]])`. */
    inline Py_ssize_t find(
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
    inline Py_ssize_t find(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
    }

    /* Equivalent to Python `str.format(*args, **kwargs)`. */
    template <typename... Args>
    inline Str format(Args&&... args) const;

    /* Equivalent to Python `str.format_map(mapping)`. */
    template <impl::dict_like T>
    inline Str format_map(const T& mapping) const;

    /* Equivalent to Python `str.index(sub[, start[, end]])`. */
    inline Py_ssize_t index(
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
    inline Py_ssize_t index(
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

    /* Equivalent to Python `str.isalnum()`. */
    inline bool isalnum() const;

    /* Equivalent to Python `str.isalpha()`. */
    inline bool isalpha() const;

    /* Equivalent to Python `str.isascii()`. */
    inline bool isascii() const;

    /* Equivalent to Python `str.isdecimal()`. */
    inline bool isdecimal() const;

    /* Equivalent to Python `str.isdigit()`. */
    inline bool isdigit() const;

    /* Equivalent to Python `str.isidentifier()`. */
    inline bool isidentifier() const;

    /* Equivalent to Python `str.islower()`. */
    inline bool islower() const;

    /* Equivalent to Python `str.isnumeric()`. */
    inline bool isnumeric() const;

    /* Equivalent to Python `str.isprintable()`. */
    inline bool isprintable() const;

    /* Equivalent to Python `str.isspace()`. */
    inline bool isspace() const;

    /* Equivalent to Python `str.istitle()`. */
    inline bool istitle() const;

    /* Equivalent to Python `str.isupper()`. */
    inline bool isupper() const;

    /* Equivalent of Python `str.join(iterable)`. */
    template <impl::is_iterable T>
    inline Str join(const T& iterable) const {
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
    inline Str join(const std::initializer_list<Str>& iterable) const {
        return join(py::List(iterable));
    }

    /* Equivalent to Python `str.ljust(width)`. */
    inline Str ljust(const Int& width) const;

    /* Equivalent to Python `str.ljust(width, fillchar)`. */
    inline Str ljust(const Int& width, const Str& fillchar) const;

    /* Equivalent to Python `str.lower()`. */
    inline Str lower() const;

    /* Equivalent to Python `str.lstrip()`. */
    inline Str lstrip() const;

    /* Equivalent to Python `str.lstrip(chars)`. */
    inline Str lstrip(const Str& chars) const;

    /* Equivalent to Python (static) `str.maketrans(x)`. */
    inline static Dict maketrans(const Object& x);

    /* Equivalent to Python (static) `str.maketrans(x, y)`. */
    inline static Dict maketrans(const Object& x, const Object& y);

    /* Equivalent to Python (static) `str.maketrans(x, y, z)`. */
    inline static Dict maketrans(const Object& x, const Object& y, const Object& z);

    /* Equivalent to Python `str.partition(sep)`. */
    inline Tuple<Str> partition(const Str& sep) const;

    /* Equivalent to Python `str.removeprefix(prefix)`. */
    inline Str removeprefix(const Str& prefix) const;

    /* Equivalent to Python `str.removesuffix(suffix)`. */
    inline Str removesuffix(const Str& suffix) const;

    /* Equivalent to Python `str.replace(old, new, count)`. */
    inline Str replace(const Str& sub, const Str& repl, Py_ssize_t maxcount = -1) const {
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
    inline Py_ssize_t rfind(
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
    inline Py_ssize_t rfind(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, -1);
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`. */
    inline Py_ssize_t rindex(
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
    inline Py_ssize_t rindex(
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

    /* Equivalent to Python `str.rjust(width)`. */
    inline Str rjust(const Int& width) const;

    /* Equivalent to Python `str.rjust(width, fillchar)`. */
    inline Str rjust(const Int& width, const Str& fillchar) const;

    /* Equivalent to Python `str.rpartition(sep)`. */
    inline Tuple<Str> rpartition(const Str& sep) const;

    /* Equivalent to Python `str.rsplit()`. */
    inline List<Str> rsplit() const;

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`. */
    inline List<Str> rsplit(const Str& sep, const Int& maxsplit = -1) const;

    /* Equivalent to Python `str.rstrip()`. */
    inline Str rstrip() const;

    /* Equivalent to Python `str.rstrip(chars)`. */
    inline Str rstrip(const Str& chars) const;

    /* Equivalent to Python `str.split()`. */
    inline List<Str> split() const {
        PyObject* result = PyUnicode_Split(this->ptr(), nullptr, -1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`. */
    inline List<Str> split(const Str& sep, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(this->ptr(), sep.ptr(), maxsplit);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    inline List<Str> splitlines(bool keepends = false) const {
        PyObject* result = PyUnicode_Splitlines(this->ptr(), keepends);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    /* Equivalent to Python `str.startswith(prefix[, start[, end]])`. */
    inline bool startswith(
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

    /* Equivalent to Python `str.strip()`. */
    inline Str strip() const;

    /* Equivalent to Python `str.strip(chars)`. */
    inline Str strip(const Str& chars) const;

    /* Equivalent to Python `str.swapcase()`. */
    inline Str swapcase() const;

    /* Equivalent to Python `str.title()`. */
    inline Str title() const;

    /* Equivalent to Python `str.translate(table)`. */
    inline Str translate(const Object& table) const;

    /* Equivalent to Python `str.upper()`. */
    inline Str upper() const;

    /* Equivalent to Python `str.zfill(width)`. */
    inline Str zfill(const Int& width) const;

};


/* Implicitly convert a py::Str into a C++ std::string. */
template <std::derived_from<Str> Self>
struct __cast__<Self, std::string> : Returns<std::string> {
    static std::string operator()(const Self& self) {
        Py_ssize_t length;
        const char* result = PyUnicode_AsUTF8AndSize(self.ptr(), &length);
        if (result == nullptr) {
            Exception::from_python();
        }
        return {result, static_cast<size_t>(length)};
    }
};


namespace impl {
namespace ops {

    template <typename Return, std::derived_from<Str> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return static_cast<size_t>(PyUnicode_GET_LENGTH(self.ptr()));
        }
    };

    template <typename Return, std::derived_from<Str> Self, typename Key>
    struct contains<Return, Self, Key> {
        static bool operator()(const Self& self, const to_object<Key>& key) {
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
        static Return operator()(const to_object<L>& lhs, to_object<R>& rhs) {
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
