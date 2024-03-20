#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_STRING_H
#define BERTRAND_PYTHON_STRING_H

// std::format is part of the C++20 standard, but was not fully implemented until GCC 
// 13+, clang 18+, or MSVC 19.29+
#if defined(__GNUC__) && !defined(__clang__)
    #if (__GNUC__ >= 13)
        #define BERTRAND_HAS_STD_FORMAT
    #endif
#elif defined(__clang__)
    #if (__clang_major__ >= 18)
        #define BERTRAND_HAS_STD_FORMAT
    #endif
#elif defined(_MSC_VER)
    #if (_MSC_VER >= 1929)
        #define BERTRAND_HAS_STD_FORMAT
    #endif
#endif

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


namespace impl {

template <>
struct __len__<Str>                                             : Returns<size_t> {};
template <>
struct __iter__<Str>                                            : Returns<Str> {};
template <>
struct __reversed__<Str>                                        : Returns<Str> {};
template <>
struct __contains__<Str, Object>                                : Returns<bool> {};
template <str_like T>
struct __contains__<Str, T>                                     : Returns<bool> {};
template <>
struct __getitem__<Str, Object>                                 : Returns<Str> {};
template <int_like T>
struct __getitem__<Str, T>                                      : Returns<Str> {};
template <>
struct __getitem__<Str, Slice>                                  : Returns<Str> {};
template <>
struct __lt__<Str, Object>                                      : Returns<bool> {};
template <str_like T>
struct __lt__<Str, T>                                           : Returns<bool> {};
template <>
struct __le__<Str, Object>                                      : Returns<bool> {};
template <str_like T>
struct __le__<Str, T>                                           : Returns<bool> {};
template <>
struct __ge__<Str, Object>                                      : Returns<bool> {};
template <str_like T>
struct __ge__<Str, T>                                           : Returns<bool> {};
template <>
struct __gt__<Str, Object>                                      : Returns<bool> {};
template <str_like T>
struct __gt__<Str, T>                                           : Returns<bool> {};
template <>
struct __add__<Str, Object>                                     : Returns<Str> {};
template <str_like T>
struct __add__<Str, T>                                          : Returns<Str> {};
template <>
struct __iadd__<Str, Object>                                    : Returns<Str> {};
template <str_like T>
struct __iadd__<Str, T>                                         : Returns<Str> {};
template <>
struct __mul__<Str, Object>                                     : Returns<Str> {};
template <int_like T>
struct __mul__<Str, T>                                          : Returns<Str> {};
template <>
struct __imul__<Str, Object>                                    : Returns<Str> {};
template <int_like T>
struct __imul__<Str, T>                                         : Returns<Str> {};

}


/* Bertrand equivalent for pybind11::str. */
class Str : public Object, public impl::SequenceOps<Str> {
    using Base = Object;

    template <typename T>
    inline auto to_format_string(T&& arg) -> decltype(auto) {
        using U = std::decay_t<T>;
        if constexpr (std::is_base_of_v<Handle, U>) {
            return arg.ptr();
        } else if constexpr (std::is_base_of_v<std::string, U>) {
            return arg.c_str();
        } else if constexpr (std::is_base_of_v<std::string_view, U>) {
            return arg.data();
        } else {
            return std::forward<T>(arg);
        }
    }

    template <typename T>
    static constexpr bool py_constructor = impl::python_like<T> && impl::str_like<T>;
    template <typename T>
    static constexpr bool c_string_constructor =
        !impl::python_like<T> && std::is_convertible_v<T, const char*>;
    template <typename T>
    static constexpr bool cpp_string_constructor =
        !impl::python_like<T> &&
        !std::is_convertible_v<T, const char*> &&
        std::is_convertible_v<T, std::string>;
    template <typename T>
    static constexpr bool cpp_string_view_constructor =
        !impl::python_like<T> &&
        !std::is_convertible_v<T, const char*> &&
        !std::is_convertible_v<T, std::string> &&
        std::is_convertible_v<T, std::string_view>;
    template <typename T>
    static constexpr bool py_converting_constructor =
        impl::python_like<T> && !impl::str_like<T>;
    template <typename T>
    static constexpr bool cpp_converting_constructor =
        !impl::python_like<T> &&
        !std::is_convertible_v<T, const char*> &&
        !std::is_convertible_v<T, std::string> &&
        !std::is_convertible_v<T, std::string_view>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::str_like<T>; }

    BERTRAND_OBJECT_COMMON(Base, Str, PyUnicode_Check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to empty string. */
    Str() : Base(PyUnicode_FromStringAndSize("", 0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (py_constructor<T>)
    Str(T&& other) : Base(std::forward<T>(other)) {}

    /* Implicitly convert a string literal into a py::Str object. */
    template <size_t N>
    Str(const char(&string)[N]) : Base(
        PyUnicode_FromStringAndSize(string, N - 1),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert a C-style string array into a py::Str object. */
    template <typename T> requires (c_string_constructor<T>)
    Str(const T& string) : Base(PyUnicode_FromString(string), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert a C++ std::string into a py::Str object. */
    template <typename T> requires (cpp_string_constructor<T>)
    Str(const T& string) {
        std::string s = string;
        m_ptr = PyUnicode_FromStringAndSize(s.c_str(), s.size());
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert a C++ std::string_view into a py::Str object. */
    template <typename T> requires (cpp_string_view_constructor<T>)
    Str(const T& string) {
        std::string_view s = string;
        m_ptr = PyUnicode_FromStringAndSize(s.data(), s.size());
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert an arbitrary Python object into a py::Str representation. */
    template <typename T> requires (py_converting_constructor<T>)
    explicit Str(const T& obj) : Base(PyObject_Str(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert an arbitrary C++ object into a py::Str representation. */
    template <typename T> requires (cpp_converting_constructor<T>)
    explicit Str(const T& obj) : Base(PyObject_Str(pybind11::cast(obj).ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    #ifdef BERTRAND_HAS_STD_FORMAT

        /* Construct a Python unicode string from a std::format()-style interpolated
        string. */
        template <typename... Args> requires (sizeof...(Args) > 0)
        explicit Str(const std::string_view& format, Args&&... args) {
            std::string result = std::vformat(
                format,
                std::make_format_args(std::forward<Args>(args))...
            );
            m_ptr = PyUnicode_FromStringAndSize(result.c_str(), result.size());
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        }

        /* Construct a Python unicode string from a std::format()-style interpolated string
        with an optional locale. */
        template <typename... Args> requires (sizeof...(Args) > 0)
        explicit Str(
            const std::locale& locale,
            const std::string_view& format,
            Args&&... args
        ) {
            std::string result = std::vformat(
                locale,
                format,
                std::make_format_args(std::forward<Args>(args))...
            );
            m_ptr = PyUnicode_FromStringAndSize(result.c_str(), result.size());
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        }

        /* Construct a Python unicode string from a std::format()-style interpolated string.
        This overload is chosen when the format string is given as a Python unicode
        string. */
        template <typename T, typename... Args>
            requires (sizeof...(Args) > 0 && impl::str_like<T> && impl::python_like<T>)
        explicit Str(const T& format, Args&&... args) : Str(
            format.template cast<std::string>(),
            std::forward<Args>(args)...
        ) {}

        /* Construct a Python unicode string from a std::format()-style interpolated string
        with an optional locale. */
        template <typename T, typename... Args>
            requires (sizeof...(Args) > 0 && impl::str_like<T> && impl::python_like<T>)
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

    /* Explicitly convert a py::Str into a C-style UTF8 byte array.  Note that Python
    caches the result in the string itself, making this operation unsafe for
    rvalue-qualified objects. */
    inline explicit operator const char*() const && = delete;
    inline explicit operator const char*() const & {
        const char* result = PyUnicode_AsUTF8(this->ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

    /* Implicitly convert a py::Str into a C++ std::string. */
    inline operator std::string() const {
        Py_ssize_t length;
        const char* result = PyUnicode_AsUTF8AndSize(this->ptr(), &length);
        if (result == nullptr) {
            throw error_already_set();
        }
        return std::string(result, length);
    }

    /* Implicitly convert a py::Str into a C++ std::string_view.  Note that this
    provides a view into an internal buffer stored in the string itself, making this
    operation unsafe for rvalue-qualified objects. */
    inline operator std::string_view() const && = delete;
    inline operator std::string_view() const & {
        Py_ssize_t length;
        const char* result = PyUnicode_AsUTF8AndSize(this->ptr(), &length);
        if (result == nullptr) {
            throw error_already_set();
        }
        return std::string_view(result, length);
    }

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
            throw error_already_set();
        }
        if (PyUnicode_Fill(this->ptr(), 0, size(), code) == -1) {
            throw error_already_set();
        }
    }

    /* Fill the string with a given character, given as a raw Python unicode point. */
    inline void fill(Py_UCS4 ch) {
        if (PyUnicode_Fill(this->ptr(), 0, size(), ch) == -1) {
            throw error_already_set();
        }
    }

    /* Return a substring from this string. */
    inline Str substring(Py_ssize_t start = 0, Py_ssize_t end = -1) const {
        PyObject* result = PyUnicode_Substring(this->ptr(), start, end);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `str.capitalize()`. */
    inline Str capitalize() const {
        static const pybind11::str method = "capitalize";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.casefold()`. */
    inline Str casefold() const {
        static const pybind11::str method = "casefold";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.center(width)`. */
    inline Str center(const Int& width) const {
        static const pybind11::str method = "center";
        return reinterpret_steal<Str>(attr(method)(width).release());
    }

    /* Equivalent to Python `str.center(width, fillchar)`. */
    template <typename T>
    inline Str center(const Int& width, const Str& fillchar) const {
        static const pybind11::str method = "center";
        return reinterpret_steal<Str>(attr(method)(width, fillchar).release());
    }

    /* Equivalent to Python `str.copy()`. */
    inline Str copy() const {
        PyObject* result = PyUnicode_New(size(), max_char());
        if (result == nullptr) {
            throw error_already_set();
        }
        if (PyUnicode_CopyCharacters(result, 0, this->ptr(), 0, size())) {
            Py_DECREF(result);
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Count the number of occurrences of a substring within the string. */
    inline size_t count(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Count(this->ptr(), sub.ptr(), start, stop);
        if (result < 0) {
            throw error_already_set();
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
        int result = PyUnicode_Tailmatch(this->ptr(), suffix.ptr(), start, stop, 1);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.expandtabs()`. */
    inline Str expandtabs(const Int& tabsize = 8) const {
        static const pybind11::str method = "expandtabs";
        return reinterpret_steal<Str>(attr(method)(tabsize).release());
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`. */
    inline Py_ssize_t find(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_Find(this->ptr(), sub.ptr(), start, stop, 1);
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
    inline Str format(Args&&... args) const {
        static const pybind11::str method = "format";
        return reinterpret_steal<Str>(attr(method)(
            detail::object_or_cast(std::forward<Args>(args))...
        ).release());
    }

    /* Equivalent to Python `str.format_map(mapping)`. */
    template <impl::dict_like T>
    inline Str format_map(const T& mapping) const {
        static const pybind11::str method = "format_map";
        return reinterpret_steal<Str>(
            attr(method)(detail::object_or_cast(mapping)).release()
        );
    }

    /* Equivalent to Python `str.index(sub[, start[, end]])`. */
    template <typename T>
    inline Py_ssize_t index(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Find(this->ptr(), sub.ptr(), start, stop, 1);
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
        Py_ssize_t result = PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.isalnum()`. */
    inline bool isalnum() const {
        static const pybind11::str method = "isalnum";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isalpha()`. */
    inline bool isalpha() const {
        static const pybind11::str method = "isalpha";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isascii()`. */
    inline bool isascii() const {
        static const pybind11::str method = "isascii";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isdecimal()`. */
    inline bool isdecimal() const {
        static const pybind11::str method = "isdecimal";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isdigit()`. */
    inline bool isdigit() const {
        static const pybind11::str method = "isdigit";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isidentifier()`. */
    inline bool isidentifier() const {
        static const pybind11::str method = "isidentifier";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.islower()`. */
    inline bool islower() const {
        static const pybind11::str method = "islower";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isnumeric()`. */
    inline bool isnumeric() const {
        static const pybind11::str method = "isnumeric";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isprintable()`. */
    inline bool isprintable() const {
        static const pybind11::str method = "isprintable";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isspace()`. */
    inline bool isspace() const {
        static const pybind11::str method = "isspace";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.istitle()`. */
    inline bool istitle() const {
        static const pybind11::str method = "istitle";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent to Python `str.isupper()`. */
    inline bool isupper() const {
        static const pybind11::str method = "isupper";
        return static_cast<bool>(attr(method)());
    }

    /* Equivalent of Python `str.join(iterable)`. */
    template <impl::is_iterable T>
    inline Str join(const T& iterable) const {
        PyObject* result = PyUnicode_Join(
            this->ptr(),
            detail::object_or_cast(iterable).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Equivalent of Python `str.join(iterable)`, where iterable is given as a
    braced initializer list. */
    inline Str join(
        const std::initializer_list<impl::StringInitializer>& iterable
    ) const {
        return join(py::List(iterable));
    }

    /* Equivalent to Python `str.ljust(width)`. */
    inline Str ljust(const Int& width) const {
        static const pybind11::str method = "ljust";
        return reinterpret_steal<Str>(attr(method)(width).release());
    }

    /* Equivalent to Python `str.ljust(width, fillchar)`. */
    inline Str ljust(const Int& width, const Str& fillchar) const {
        static const pybind11::str method = "ljust";
        return reinterpret_steal<Str>(attr(method)(width, fillchar).release());
    }

    /* Equivalent to Python `str.lower()`. */
    inline Str lower() const {
        static const pybind11::str method = "lower";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.lstrip()`. */
    inline Str lstrip() const {
        static const pybind11::str method = "lstrip";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.lstrip(chars)`. */
    inline Str lstrip(const Str& chars) const {
        static const pybind11::str method = "lstrip";
        return reinterpret_steal<Str>(attr(method)(chars).release());
    }

    /* Equivalent to Python (static) `str.maketrans(x)`. */
    template <typename T>
    inline static Dict maketrans(const T& x) {
        static const pybind11::str method = "maketrans";
        static const pybind11::type cls =
            reinterpret_borrow<pybind11::type>((PyObject*) &PyUnicode_Type);
        return reinterpret_steal<Dict>(
            cls.attr(method)(detail::object_or_cast(x)).release()
        );
    }

    /* Equivalent to Python (static) `str.maketrans(x, y)`. */
    template <typename T, typename U>
    inline static Dict maketrans(const T& x, const U& y) {
        static const pybind11::str method = "maketrans";
        static const pybind11::type cls =
            reinterpret_borrow<pybind11::type>((PyObject*) &PyUnicode_Type);
        return reinterpret_steal<Dict>(cls.attr(method)(
            detail::object_or_cast(x),
            detail::object_or_cast(y)
        ).release());
    }

    /* Equivalent to Python (static) `str.maketrans(x, y, z)`. */
    template <typename T, typename U, typename V>
    inline static Dict maketrans(const T& x, const U& y, const V& z) {
        static const pybind11::str method = "maketrans";
        static const pybind11::type cls =
            reinterpret_borrow<pybind11::type>((PyObject*) &PyUnicode_Type);
        return reinterpret_steal<Dict>(cls.attr(method)(
            detail::object_or_cast(x),
            detail::object_or_cast(y),
            detail::object_or_cast(z)
        ).release());
    }

    /* Equivalent to Python `str.partition(sep)`. */
    inline Tuple partition(const Str& sep) const {
        static const pybind11::str method = "partition";
        return reinterpret_steal<Tuple>(attr(method)(sep).release());
    }

    /* Equivalent to Python `str.removeprefix(prefix)`. */
    inline Str removeprefix(const Str& prefix) const {
        static const pybind11::str method = "removeprefix";
        return reinterpret_steal<Str>(attr(method)(prefix).release());
    }

    /* Equivalent to Python `str.removesuffix(suffix)`. */
    inline Str removesuffix(const Str& suffix) const {
        static const pybind11::str method = "removesuffix";
        return reinterpret_steal<Str>(attr(method)(suffix).release());
    }

    /* Equivalent to Python `str.replace(old, new, count)`. */
    inline Str replace(const Str& sub, const Str& repl, Py_ssize_t maxcount = -1) const {
        PyObject* result = PyUnicode_Replace(
            this->ptr(),
            sub.ptr(),
            repl.ptr(),
            maxcount
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`. */
    inline Py_ssize_t rfind(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_Find(this->ptr(), sub.ptr(), start, stop, -1);
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
        Py_ssize_t result = PyUnicode_Find(this->ptr(), sub.ptr(), start, stop, -1);
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
        Py_ssize_t result = PyUnicode_FindChar(this->ptr(), ch, start, stop, -1);
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.rjust(width)`. */
    inline Str rjust(const Int& width) const {
        static const pybind11::str method = "rjust";
        return reinterpret_steal<Str>(attr(method)(width).release());
    }

    /* Equivalent to Python `str.rjust(width, fillchar)`. */
    inline Str rjust(const Int& width, const Str& fillchar) const {
        static const pybind11::str method = "rjust";
        return reinterpret_steal<Str>(attr(method)(width, fillchar).release());
    }

    /* Equivalent to Python `str.rpartition(sep)`. */
    inline Tuple rpartition(const Str& sep) const {
        static const pybind11::str method = "rpartition";
        return reinterpret_steal<Tuple>(attr(method)(sep).release());
    }

    /* Equivalent to Python `str.rsplit()`. */
    inline List rsplit() const {
        static const pybind11::str method = "rsplit";
        return reinterpret_steal<List>(this->attr(method)().release());
    }

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`. */
    inline List rsplit(const Str& sep, const Int& maxsplit = -1) const {
        static const pybind11::str method = "rsplit";
        return reinterpret_steal<List>(attr(method)(sep, maxsplit).release());
    }

    /* Equivalent to Python `str.rstrip()`. */
    inline Str rstrip() const {
        static const pybind11::str method = "rstrip";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.rstrip(chars)`. */
    inline Str rstrip(const Str& chars) const {
        static const pybind11::str method = "rstrip";
        return reinterpret_steal<Str>(attr(method)(chars).release());
    }

    /* Equivalent to Python `str.split()`. */
    inline List split() const {
        PyObject* result = PyUnicode_Split(this->ptr(), nullptr, -1);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`. */
    inline List split(const Str& sep, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(this->ptr(), sep.ptr(), maxsplit);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    inline List splitlines(bool keepends = false) const {
        PyObject* result = PyUnicode_Splitlines(this->ptr(), keepends);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.startswith(prefix[, start[, end]])`. */
    inline bool startswith(
        const Str& prefix,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        int result = PyUnicode_Tailmatch(this->ptr(), prefix.ptr(), start, stop, -1);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.strip()`. */
    inline Str strip() const {
        static const pybind11::str method = "strip";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.strip(chars)`. */
    inline Str strip(const Str& chars) const {
        static const pybind11::str method = "strip";
        return reinterpret_steal<Str>(attr(method)(chars).release());
    }

    /* Equivalent to Python `str.swapcase()`. */
    inline Str swapcase() const {
        static const pybind11::str method = "swapcase";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.title()`. */
    inline Str title() const {
        static const pybind11::str method = "title";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.translate(table)`. */
    template <typename T>
    inline Str translate(const T& table) const {
        static const pybind11::str method = "translate";
        return reinterpret_steal<Str>(
            attr(method)(detail::object_or_cast(table)).release()
        );
    }

    /* Equivalent to Python `str.upper()`. */
    inline Str upper() const {
        static const pybind11::str method = "upper";
        return reinterpret_steal<Str>(attr(method)().release());
    }

    /* Equivalent to Python `str.zfill(width)`. */
    inline Str zfill(const Int& width) const {
        static const pybind11::str method = "zfill";
        return reinterpret_steal<Str>(attr(method)(width).release());
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

protected:

    using impl::SequenceOps<Str>::operator_mul;
    using impl::SequenceOps<Str>::operator_imul;

    template <typename Return, typename T>
    inline static size_t operator_len(const T& self) {
        return static_cast<size_t>(PyUnicode_GET_LENGTH(self.ptr()));
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_contains(const L& self, const R& key) {
        int result = PyUnicode_Contains(
            self.ptr(),
            detail::object_or_cast(key).ptr()
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_add(const L& lhs, const R& rhs) {
        PyObject* result = PyUnicode_Concat(
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
                    throw bertrand::py::error_already_set();
                }
            }
            return static_cast<size_t>(result);
        }
    };

}


#endif  // BERTRAND_PYTHON_STRING_H
