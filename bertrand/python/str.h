#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_STRING_H
#define BERTRAND_PYTHON_STRING_H

// #include <format>  // part of the C++20 standard, but not available until GCC 13+

#include "common.h"
#include "int.h"
#include "list.h"
#include "tuple.h"
#include "set.h"
#include "dict.h"


namespace bertrand {
namespace py {


namespace impl {

    // TODO: encapsulate Str/Bytes/ByteArray interfaces here

    template <typename Derived>
    class IStr :
        public Object,
        public impl::SequenceOps<Derived>
    {
        using Base = Object;

        inline Derived* self() { return static_cast<Derived*>(this); }
        inline const Derived* self() const { return static_cast<const Derived*>(this); }

    protected:


    public:
        using Base::Base;

        ////////////////////////////////
        ////    PYTHON INTERFACE    ////
        ////////////////////////////////

        /* Equivalent to Python's `str.capitalize()`. */
        inline Derived capitalize() const {
            return reinterpret_steal<Derived>(self()->attr("capitalize")().release());
        }


    };


}  // namespace impl



namespace impl {

template <>
struct __dereference__<Str>                                     : Returns<detail::args_proxy> {};
template <>
struct __len__<Str>                                             : Returns<size_t> {};
template <>
struct __iter__<Str>                                            : Returns<Str> {};
template <>
struct __reversed__<Str>                                        : Returns<Str> {};
template <str_like T>
struct __contains__<Str, T>                                     : Returns<bool> {};
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
struct __iadd__<Str, Object>                                    : Returns<Str&> {};
template <str_like T>
struct __iadd__<Str, T>                                         : Returns<Str&> {};

}


/* Bertrand equivalent for pybind11::str. */
class Str : public Object {
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
    static constexpr bool constructor1 = 
        !impl::python_like<T> && std::is_convertible_v<T, std::string>;
    template <typename T>
    static constexpr bool constructor2 =
        !impl::python_like<T> && !std::is_convertible_v<T, std::string>;
    template <typename T>
    static constexpr bool constructor3 = impl::python_like<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::str_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Str, PyUnicode_Check)

    /* Default constructor.  Initializes to empty string. */
    Str() : Base(PyUnicode_FromStringAndSize("", 0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert C++ string literals into py::Str. */
    Str(const char* string) : Base(PyUnicode_FromString(string), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert C++ std::string into py::Str. */
    Str(const std::string& string) :
        Base(PyUnicode_FromStringAndSize(string.c_str(), string.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert C++ std::string_view into py::Str. */
    Str(const std::string_view& string) :
        Base(PyUnicode_FromStringAndSize(string.data(), string.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Trigger implicit C++ conversions to std::string. */
    template <typename T> requires (constructor1<T>)
    explicit Str(const T& string) : Str(static_cast<std::string>(string)) {}

    /* Explicitly convert an arbitrary C++ object into a py::Str representation. */
    template <typename T> requires (constructor2<T>)
    explicit Str(const T& obj) : Base(PyObject_Str(pybind11::cast(obj).ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert an arbitrary Python object into a py::Str representation. */
    template <typename T> requires (constructor3<T>)
    explicit Str(const T& obj) : Base(PyObject_Str(obj.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    // NOTE: <format> header is only available in GCC 13+

    // /* Construct a Python unicode string from a std::format()-style interpolated
    // string. */
    // template <typename... Args> requires (sizeof...(Args) > 0)
    // explicit Str(const std::string_view& format, Args&&... args) {
    //     std::string result = std::vformat(
    //         format,
    //         std::make_format_args(std::forward<Args>(args))...
    //     );
    //     m_ptr = PyUnicode_FromStringAndSize(result.c_str(), result.size());
    //     if (m_ptr == nullptr) {
    //         throw error_already_set();
    //     }
    // }

    // /* Construct a Python unicode string from a std::format()-style interpolated string
    // with an optional locale. */
    // template <typename... Args> requires (sizeof...(Args) > 0)
    // explicit Str(
    //     const std::locale& locale,
    //     const std::string_view& format,
    //     Args&&... args
    // ) {
    //     std::string result = std::vformat(
    //         locale,
    //         format,
    //         std::make_format_args(std::forward<Args>(args))...
    //     );
    //     m_ptr = PyUnicode_FromStringAndSize(result.c_str(), result.size());
    //     if (m_ptr == nullptr) {
    //         throw error_already_set();
    //     }
    // }

    // /* Construct a Python unicode string from a std::format()-style interpolated string.
    // This overload is chosen when the format string is given as a Python unicode
    // string. */
    // template <typename T, typename... Args>
    //     requires (sizeof...(Args) > 0 && impl::str_like<T> && impl::python_like<T>)
    // explicit Str(const T& format, Args&&... args) : Str(
    //     format.template cast<std::string>(),
    //     std::forward<Args>(args)...
    // ) {}

    // /* Construct a Python unicode string from a std::format()-style interpolated string
    // with an optional locale. */
    // template <typename T, typename... Args>
    //     requires (sizeof...(Args) > 0 && impl::str_like<T> && impl::python_like<T>)
    // explicit Str(
    //     const std::locale& locale,
    //     const T& format,
    //     Args&&... args
    // ) : Str(
    //     locale,
    //     format.template cast<std::string>(),
    //     std::forward<Args>(args)...
    // ) {}

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    // TODO: convert to const char*, std::string_view?  Since Python caches the UTF8
    // representation within the string itself, this can be possibly made safe

    /* Implicitly convert a py::Str into a C++ std::string. */
    inline operator std::string() const {
        Py_ssize_t length;
        const char* result = PyUnicode_AsUTF8AndSize(this->ptr(), &length);
        if (result == nullptr) {
            throw error_already_set();
        }
        return std::string(result, length);
    }

    ///////////////////////////////////
    ////    PyUnicode_* METHODS    ////
    ///////////////////////////////////

    /* Get the underlying unicode buffer. */
    inline void* data() const noexcept {
        return PyUnicode_DATA(this->ptr());
    }

    /* Get the length of the string in unicode code points. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyUnicode_GET_LENGTH(this->ptr()));
    }

    /* Check if the string is empty. */
    inline bool empty() const noexcept {
        return size() == 0;
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
        return reinterpret_steal<Str>(this->attr("capitalize")().release());
    }

    /* Equivalent to Python `str.casefold()`. */
    inline Str casefold() const {
        return reinterpret_steal<Str>(this->attr("casefold")().release());
    }

    /* Equivalent to Python `str.center(width)`. */
    inline Str center(const Int& width) const {
        return reinterpret_steal<Str>(this->attr("center")(width).release());
    }

    /* Equivalent to Python `str.center(width, fillchar)`. */
    template <typename T>
    inline Str center(const Int& width, const Str& fillchar) const {
        return reinterpret_steal<Str>(this->attr("center")(width, fillchar).release());
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
    inline Py_ssize_t count(
        const Str& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Count(this->ptr(), sub.ptr(), start, stop);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.encode(encoding)`. */
    inline Bytes encode(
        const Str& encoding = "utf-8",
        const Str& errors = "strict"
    ) const {
        return this->attr("encode")(encoding, errors);  // TODO: wrap Bytes
    }

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
        return reinterpret_steal<Str>(this->attr("expandtabs")(tabsize).release());
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
        return reinterpret_steal<Str>(this->attr("format")(
            detail::object_or_cast(std::forward<Args>(args))...
        ).release());
    }

    /* Equivalent to Python `str.format_map(mapping)`. */
    template <impl::dict_like T>
    inline Str format_map(const T& mapping) const {
        return reinterpret_steal<Str>(
            this->attr("format_map")(detail::object_or_cast(mapping)).release()
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
        return static_cast<bool>(this->attr("isalnum")());
    }

    /* Equivalent to Python `str.isalpha()`. */
    inline bool isalpha() const {
        return static_cast<bool>(this->attr("isalpha")());
    }

    /* Equivalent to Python `str.isascii()`. */
    inline bool isascii() const {
        return static_cast<bool>(this->attr("isascii")());
    }

    /* Equivalent to Python `str.isdecimal()`. */
    inline bool isdecimal() const {
        return static_cast<bool>(this->attr("isdecimal")());
    }

    /* Equivalent to Python `str.isdigit()`. */
    inline bool isdigit() const {
        return static_cast<bool>(this->attr("isdigit")());
    }

    /* Equivalent to Python `str.isidentifier()`. */
    inline bool isidentifier() const {
        return static_cast<bool>(this->attr("isidentifier")());
    }

    /* Equivalent to Python `str.islower()`. */
    inline bool islower() const {
        return static_cast<bool>(this->attr("islower")());
    }

    /* Equivalent to Python `str.isnumeric()`. */
    inline bool isnumeric() const {
        return static_cast<bool>(this->attr("isnumeric")());
    }

    /* Equivalent to Python `str.isprintable()`. */
    inline bool isprintable() const {
        return static_cast<bool>(this->attr("isprintable")());
    }

    /* Equivalent to Python `str.isspace()`. */
    inline bool isspace() const {
        return static_cast<bool>(this->attr("isspace")());
    }

    /* Equivalent to Python `str.istitle()`. */
    inline bool istitle() const {
        return static_cast<bool>(this->attr("istitle")());
    }

    /* Equivalent to Python `str.isupper()`. */
    inline bool isupper() const {
        return static_cast<bool>(this->attr("isupper")());
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
        return reinterpret_steal<Str>(this->attr("ljust")(width).release());
    }

    /* Equivalent to Python `str.ljust(width, fillchar)`. */
    inline Str ljust(const Int& width, const Str& fillchar) const {
        return reinterpret_steal<Str>(this->attr("ljust")(width, fillchar).release());
    }

    /* Equivalent to Python `str.lower()`. */
    inline Str lower() const {
        return reinterpret_steal<Str>(this->attr("lower")().release());
    }

    /* Equivalent to Python `str.lstrip()`. */
    inline Str lstrip() const {
        return reinterpret_steal<Str>(this->attr("lstrip")().release());
    }

    /* Equivalent to Python `str.lstrip(chars)`. */
    inline Str lstrip(const Str& chars) const {
        return reinterpret_steal<Str>(this->attr("lstrip")(chars).release());
    }

    /* Equivalent to Python (static) `str.maketrans(x)`. */
    template <typename T> 
    inline static Dict maketrans(const T& x) {
        pybind11::type cls =
            reinterpret_borrow<pybind11::type>((PyObject*) &PyUnicode_Type);
        return reinterpret_steal<Dict>(
            cls.attr("maketrans")(detail::object_or_cast(x)).release()
        );
    }

    /* Equivalent to Python (static) `str.maketrans(x, y)`. */
    template <typename T, typename U> 
    inline static Dict maketrans(const T& x, const U& y) {
        pybind11::type cls =
            reinterpret_borrow<pybind11::type>((PyObject*) &PyUnicode_Type);
        return reinterpret_steal<Dict>(cls.attr("maketrans")(
            detail::object_or_cast(x),
            detail::object_or_cast(y)
        ).release());
    }

    /* Equivalent to Python (static) `str.maketrans(x, y, z)`. */
    template <typename T, typename U, typename V> 
    inline static Dict maketrans(const T& x, const U& y, const V& z) {
        pybind11::type cls =
            reinterpret_borrow<pybind11::type>((PyObject*) &PyUnicode_Type);
        return reinterpret_steal<Dict>(cls.attr("maketrans")(
            detail::object_or_cast(x),
            detail::object_or_cast(y),
            detail::object_or_cast(z)
        ).release());
    }

    /* Equivalent to Python `str.partition(sep)`. */
    inline Tuple partition(const Str& sep) const {
        return reinterpret_steal<Tuple>(this->attr("partition")(sep).release());
    }

    /* Equivalent to Python `str.removeprefix(prefix)`. */
    inline Str removeprefix(const Str& prefix) const {
        return reinterpret_steal<Str>(this->attr("removeprefix")(prefix).release());
    }

    /* Equivalent to Python `str.removesuffix(suffix)`. */
    inline Str removesuffix(const Str& suffix) const {
        return reinterpret_steal<Str>(this->attr("removesuffix")(suffix).release());
    }

    /* Equivalent to Python `str.replace(old, new[, count])`. */
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
        return reinterpret_steal<Str>(this->attr("rjust")(width).release());
    }

    /* Equivalent to Python `str.rjust(width, fillchar)`. */
    inline Str rjust(const Int& width, const Str& fillchar) const {
        return reinterpret_steal<Str>(this->attr("rjust")(width, fillchar).release());
    }

    /* Equivalent to Python `str.rpartition(sep)`. */
    inline Tuple rpartition(const Str& sep) const {
        return reinterpret_steal<Tuple>(this->attr("rpartition")(sep).release());
    }

    /* Equivalent to Python `str.rsplit()`. */
    inline List rsplit() const {
        return reinterpret_steal<List>(this->attr("rsplit")().release());
    }

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`. */
    inline List rsplit(const Str& sep, const Int& maxsplit = -1) const {
        return reinterpret_steal<List>(this->attr("rsplit")(sep, maxsplit).release());
    }

    /* Equivalent to Python `str.rstrip()`. */
    inline Str rstrip() const {
        return reinterpret_steal<Str>(this->attr("rstrip")().release());
    }

    /* Equivalent to Python `str.rstrip(chars)`. */
    inline Str rstrip(const Str& chars) const {
        return reinterpret_steal<Str>(this->attr("rstrip")(chars).release());
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
        return reinterpret_steal<Str>(this->attr("strip")().release());
    }

    /* Equivalent to Python `str.strip(chars)`. */
    inline Str strip(const Str& chars) const {
        return reinterpret_steal<Str>(this->attr("strip")(chars).release());
    }

    /* Equivalent to Python `str.swapcase()`. */
    inline Str swapcase() const {
        return reinterpret_steal<Str>(this->attr("swapcase")().release());
    }

    /* Equivalent to Python `str.title()`. */
    inline Str title() const {
        return reinterpret_steal<Str>(this->attr("title")().release());
    }

    /* Equivalent to Python `str.translate(table)`. */
    template <typename T>
    inline Str translate(const T& table) const {
        return reinterpret_steal<Str>(
            this->attr("translate")(detail::object_or_cast(table)).release()
        );
    }

    /* Equivalent to Python `str.upper()`. */
    inline Str upper() const {
        return reinterpret_steal<Str>(this->attr("upper")().release());
    }

    /* Equivalent to Python `str.zfill(width)`. */
    inline Str zfill(const Int& width) const {
        return reinterpret_steal<Str>(this->attr("zfill")(width).release());
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to Python `sub in str`. */
    inline bool contains(const Str& sub) const {
        int result = PyUnicode_Contains(this->ptr(), sub.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Concatenate this string with another. */
    inline Str concat(const Str& other) const {
        PyObject* result = PyUnicode_Concat(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    using Base::operator*;
    using Base::operator*=;

};





/* TODO: Bertrand equivalent for pybind11::bytes.

bytes/bytearray interface:

count(sub[, start[, end]]) -> int
removeprefix(prefix, /) -> bytes
removesuffix(suffix, /) -> bytes
decode(encoding="utf-8", errors="strict") -> str
endswith(suffix[, start[, end]]) -> bool
find(sub[, start[, end]]) -> int
index(sub[, start[, end]]) -> int
join(iterable) -> bytes/bytearray
maketrans(from, to) -> dict
partition(sep) -> tuple
replace(old, new[, count]) -> bytes/bytearray
rfind(sub[, start[, end]]) -> int
rindex(sub[, start[, end]]) -> int
rpartition(sep) -> tuple
startswith(prefix[, start[, end]]) -> bool
translate(table) -> bytes/bytearray

center(width[, fillbyte]) -> bytes/bytearray
ljust(width[, fillbyte]) -> bytes/bytearray
lstrip([chars]) -> bytes/bytearray
rjust(width[, fillbyte]) -> bytes/bytearray
rsplit(sep=None, maxsplit=-1) -> list
rstrip([chars]) -> bytes/bytearray
split(sep=None, maxsplit=-1) -> list
strip([chars]) -> bytes/bytearray

capitalize() -> bytes/bytearray
expandtabs(tabsize=8) -> bytes/bytearray
isalnum() -> bool
isalpha() -> bool
isascii() -> bool
isdigit() -> bool
islower() -> bool
isspace() -> bool
istitle() -> bool
isupper() -> bool
lower() -> bytes/bytearray
splitlines([keepends]) -> list
swapcase() -> bytes/bytearray
title() -> bytes/bytearray
upper() -> bytes/bytearray
zfill(width) -> bytes/bytearray




*/




}  // namespace python
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
