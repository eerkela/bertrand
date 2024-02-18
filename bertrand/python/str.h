#ifndef BERTRAND_PYTHON_STRING_H
#define BERTRAND_PYTHON_STRING_H

#include "common.h"
#include "int.h"
#include "list.h"
#include "tuple.h"
#include "set.h"
#include "dict.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::str that enables extra C API functionality. */
class Str :
    public pybind11::str,
    public impl::SequenceOps<Str>,
    public impl::FullCompare<Str>
{
    using Base = pybind11::str;
    using Ops = impl::SequenceOps<Str>;
    using Compare = impl::FullCompare<Str>;

    template <typename T>
    inline auto to_format_string(T&& arg) -> decltype(auto) {
        using U = std::decay_t<T>;
        if constexpr (std::is_base_of_v<pybind11::handle, U>) {
            return arg.ptr();
        } else if constexpr (std::is_base_of_v<std::string, U>) {
            return arg.c_str();
        } else if constexpr (std::is_base_of_v<std::string_view, U>) {
            return arg.data();
        } else {
            return std::forward<T>(arg);
        }
    }

    static PyObject* convert_to_str(PyObject* obj) {
        PyObject* result = PyObject_Str(obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(Str, PyUnicode_Check, convert_to_str);

    /* Default constructor.  Initializes to empty string. */
    inline Str() : Base([] {
        PyObject* result = PyUnicode_FromStringAndSize("", 0);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a unicode string from a null-terminated C string. */
    inline Str(const char* string) : Base(string) {}

    /* Construct a unicode string from a C++ std::string. */
    inline Str(const std::string& string) : Base(string) {}

    /* Construct a unicode string from a C++ std::string_view. */
    inline Str(const std::string_view& string) : Base(string) {}

    /* Construct a unicode string from a printf-style format string.  See the python
    docs for `PyUnicode_FromFormat()` for more details.  Note that this can segfault
    if the argument types do not match the format code(s). */
    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    explicit Str(const char* format, Args&&... args) : Base([&] {
        PyObject* result = PyUnicode_FromFormat(
            format,
            to_format_string(std::forward<Args>(args))...
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a unicode string from a printf-style format string.  See
    Str(const char*, ...) for more details. */
    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    explicit Str(const std::string& format, Args&&... args) : Str(
        format.c_str(), std::forward<Args>(args)...
    ) {}

    /* Construct a unicode string from a printf-style format string.  See
    Str(const char*, ...) for more details. */
    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    explicit Str(const std::string_view& format, Args&&... args) : Str(
        format.data(), std::forward<Args>(args)...
    ) {}

    /* Construct a unicode string from a printf-style format string.  See
    Str(const char*, ...) for more details. */
    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    explicit Str(const pybind11::str& format, Args&&... args) : Str(
        format.template cast<std::string>(), std::forward<Args>(args)...
    ) {}

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
    template <typename T>
    void fill(T&& ch) {
        Str str = cast<Str>(std::forward<T>(ch));
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
        return this->attr("capitalize")();
    }

    /* Equivalent to Python `str.casefold()`. */
    inline Str casefold() const {
        return this->attr("casefold")();
    }

    /* Equivalent to Python `str.center(width)`. */
    inline Str center(Py_ssize_t width) const {
        return this->attr("center")(py::cast(width));
    }

    /* Equivalent to Python `str.center(width, fillchar)`. */
    template <typename T>
    inline Str center(Py_ssize_t width, T&& fillchar) const {
        return this->attr("center")(
            py::cast(width),
            detail::object_or_cast(std::forward<T>(fillchar))
        );
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
    template <typename T>
    inline Py_ssize_t count(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Count(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(sub)).ptr(),
            start,
            stop
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.encode(encoding)`. */
    inline Bytes encode(
        const char* encoding = "utf-8",
        const char* errors = "strict"
    ) const {
        return this->attr("encode")(
            detail::object_or_cast(encoding),
            detail::object_or_cast(errors)
        );
    }

    /* Equivalent to Python `str.endswith(suffix[, start[, end]])`. */
    template <typename T>
    inline bool endswith(
        T&& suffix,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        int result = PyUnicode_Tailmatch(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(suffix)).ptr(),
            start,
            stop,
            1
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.expandtabs()`. */
    inline Str expandtabs(Py_ssize_t tabsize = 8) const {
        return this->attr("expandtabs")(tabsize);
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`. */
    template <typename T>
    inline Py_ssize_t find(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_Find(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(sub)).ptr(),
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
    inline Str format(Args&&... args) const {
        return this->attr("format")(
            detail::object_or_cast(std::forward<Args>(args))...
        );
    }

    /* Equivalent to Python `str.format_map(mapping)`. */
    template <typename T>
    inline Str format_map(T&& mapping) const {
        return this->attr("format_map")(
            detail::object_or_cast(std::forward<T>(mapping))
        );
    }

    /* Equivalent to Python `str.index(sub[, start[, end]])`. */
    template <typename T>
    inline Py_ssize_t index(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Find(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(sub)).ptr(),
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
        Py_ssize_t result = PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.isalnum()`. */
    inline bool isalnum() const {
        return this->attr("isalnum")().cast<bool>();
    }

    /* Equivalent to Python `str.isalpha()`. */
    inline bool isalpha() const {
        return this->attr("isalpha")().cast<bool>();
    }

    /* Equivalent to Python `str.isascii()`. */
    inline bool isascii() const {
        return this->attr("isascii")().cast<bool>();
    }

    /* Equivalent to Python `str.isdecimal()`. */
    inline bool isdecimal() const {
        return this->attr("isdecimal")().cast<bool>();
    }

    /* Equivalent to Python `str.isdigit()`. */
    inline bool isdigit() const {
        return this->attr("isdigit")().cast<bool>();
    }

    /* Equivalent to Python `str.isidentifier()`. */
    inline bool isidentifier() const {
        return this->attr("isidentifier")().cast<bool>();
    }

    /* Equivalent to Python `str.islower()`. */
    inline bool islower() const {
        return this->attr("islower")().cast<bool>();
    }

    /* Equivalent to Python `str.isnumeric()`. */
    inline bool isnumeric() const {
        return this->attr("isnumeric")().cast<bool>();
    }

    /* Equivalent to Python `str.isprintable()`. */
    inline bool isprintable() const {
        return this->attr("isprintable")().cast<bool>();
    }

    /* Equivalent to Python `str.isspace()`. */
    inline bool isspace() const {
        return this->attr("isspace")().cast<bool>();
    }

    /* Equivalent to Python `str.istitle()`. */
    inline bool istitle() const {
        return this->attr("istitle")().cast<bool>();
    }

    /* Equivalent to Python `str.isupper()`. */
    inline bool isupper() const {
        return this->attr("isupper")().cast<bool>();
    }

    /* Equivalent of Python `str.join(iterable)`. */
    template <typename T>
    inline Str join(T&& iterable) const {
        PyObject* result = PyUnicode_Join(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(iterable)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Equivalent of Python `str.join(iterable)`, where iterable is given as a braced
    initializer. */
    template <typename T>
    inline Str join(const std::initializer_list<T>& iterable) const {
        return join(py::List(iterable));
    }

    /* Equivalent of Python `str.join(iterable)`, where iterable is given as a braced
    initializer. */
    inline Str join(const std::initializer_list<impl::Initializer>& iterable) const {
        return join(py::List(iterable));
    }

    /* Equivalent to Python `str.ljust(width)`. */
    inline Str ljust(Py_ssize_t width) const {
        return this->attr("ljust")(py::cast(width));
    }

    /* Equivalent to Python `str.ljust(width, fillchar)`. */
    template <typename T>
    inline Str ljust(Py_ssize_t width, T&& fillchar) const {
        return this->attr("ljust")(
            py::cast(width),
            detail::object_or_cast(std::forward<T>(fillchar))
        );
    }

    /* Equivalent to Python `str.lower()`. */
    inline Str lower() const {
        return this->attr("lower")();
    }

    /* Equivalent to Python `str.lstrip()`. */
    inline Str lstrip() const {
        return this->attr("lstrip")();
    }

    /* Equivalent to Python `str.lstrip(chars)`. */
    template <typename T>
    inline Str lstrip(T&& chars) const {
        return this->attr("lstrip")(detail::object_or_cast(std::forward<T>(chars)));
    }

    /* Equivalent to Python (static) `str.maketrans(x)`. */
    template <typename T> 
    inline static Dict maketrans(T&& x) {
        pybind11::type cls(
            reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyUnicode_Type))
        );
        return cls.attr("maketrans")(detail::object_or_cast(std::forward<T>(x)));
    }

    /* Equivalent to Python (static) `str.maketrans(x, y)`. */
    template <typename T, typename U> 
    inline static Dict maketrans(T&& x, U&& y) {
        pybind11::type cls(
            reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyUnicode_Type))
        );
        return cls.attr("maketrans")(
            detail::object_or_cast(std::forward<T>(x)),
            detail::object_or_cast(std::forward<U>(y))
        );
    }

    /* Equivalent to Python (static) `str.maketrans(x, y, z)`. */
    template <typename T, typename U, typename V> 
    inline static Dict maketrans(T&& x, U&& y, V&& z) {
        pybind11::type cls(
            reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyUnicode_Type))
        );
        return cls.attr("maketrans")(
            detail::object_or_cast(std::forward<T>(x)),
            detail::object_or_cast(std::forward<U>(y)),
            detail::object_or_cast(std::forward<V>(z))
        );
    }

    /* Equivalent to Python `str.partition(sep)`. */
    template <typename T>
    inline Tuple partition(T&& sep) const {
        return this->attr("partition")(detail::object_or_cast(std::forward<T>(sep)));
    }

    /* Equivalent to Python `str.removeprefix(prefix)`. */
    template <typename T>
    inline Str removeprefix(T&& prefix) const {
        return this->attr("removeprefix")(
            detail::object_or_cast(std::forward<T>(prefix))
        );
    }

    /* Equivalent to Python `str.removesuffix(suffix)`. */
    template <typename T>
    inline Str removesuffix(T&& suffix) const {
        return this->attr("removesuffix")(
            detail::object_or_cast(std::forward<T>(suffix))
        );
    }

    /* Equivalent to Python `str.replace(old, new[, count])`. */
    template <typename T, typename U>
    inline Str replace(T&& substr, U&& replstr, Py_ssize_t maxcount = -1) const {
        PyObject* result = PyUnicode_Replace(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(substr)).ptr(),
            detail::object_or_cast(std::forward<U>(replstr)).ptr(),
            maxcount
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`. */
    template <typename T>
    inline Py_ssize_t rfind(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_Find(this->ptr(), sub, start, stop, -1);
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
    template <typename T>
    inline Py_ssize_t rindex(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Find(this->ptr(), sub, start, stop, -1);
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
    inline Str rjust(Py_ssize_t width) const {
        return this->attr("rjust")(py::cast(width));
    }

    /* Equivalent to Python `str.rjust(width, fillchar)`. */
    template <typename T>
    inline Str rjust(Py_ssize_t width, T&& fillchar) const {
        return this->attr("rjust")(
            py::cast(width),
            detail::object_or_cast(std::forward<T>(fillchar))
        );
    }

    /* Equivalent to Python `str.rpartition(sep)`. */
    template <typename T>
    inline Tuple rpartition(T&& sep) const {
        return this->attr("rpartition")(detail::object_or_cast(std::forward<T>(sep)));
    }

    /* Equivalent to Python `str.rsplit()`. */
    inline List rsplit() const {
        return this->attr("rsplit")();
    }

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`. */
    template <typename T>
    inline List rsplit(T&& sep, Py_ssize_t maxsplit = -1) const {
        return this->attr("rsplit")(
            detail::object_or_cast(std::forward<T>(sep)),
            py::cast(maxsplit)
        );
    }

    /* Equivalent to Python `str.rstrip()`. */
    inline Str rstrip() const {
        return this->attr("rstrip")();
    }

    /* Equivalent to Python `str.rstrip(chars)`. */
    template <typename T>
    inline Str rstrip(T&& chars) const {
        return this->attr("rstrip")(detail::object_or_cast(std::forward<T>(chars)));
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
    template <typename T>
    inline List split(T&& separator, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(separator)).ptr(),
            maxsplit
        );
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
    template <typename T>
    inline bool startswith(
        T&& prefix,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        int result = PyUnicode_Tailmatch(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(prefix)).ptr(),
            start,
            stop,
            -1
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.strip()`. */
    inline Str strip() const {
        return this->attr("strip")();
    }

    /* Equivalent to Python `str.strip(chars)`. */
    template <typename T>
    inline Str strip(T&& chars) const {
        return this->attr("strip")(detail::object_or_cast(std::forward<T>(chars)));
    }

    /* Equivalent to Python `str.swapcase()`. */
    inline Str swapcase() const {
        return this->attr("swapcase")();
    }

    /* Equivalent to Python `str.title()`. */
    inline Str title() const {
        return this->attr("title")();
    }

    /* Equivalent to Python `str.translate(table)`. */
    template <typename T>
    inline Str translate(T&& table) const {
        return this->attr("translate")(detail::object_or_cast(std::forward<T>(table)));
    }

    /* Equivalent to Python `str.upper()`. */
    inline Str upper() const {
        return this->attr("upper")();
    }

    /* Equivalent to Python `str.zfill(width)`. */
    inline Str zfill(Py_ssize_t width) const {
        return this->attr("zfill")(width);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to Python `sub in str`. */
    template <typename T>
    inline bool contains(T&& sub) const {
        int result = PyUnicode_Contains(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(sub)).ptr()
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Concatenate this string with another. */
    template <typename T>
    inline Str concat(T&& other) const {
        PyObject* result = PyUnicode_Concat(
            this->ptr(), detail::object_or_cast(std::forward<T>(other)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    using Base::operator[];
    using Ops::operator[];

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>=;
    using Compare::operator>;

    using Ops::operator+;
    using Ops::operator*;
    using Ops::operator*=;

    template <typename T>
    inline Str& operator+=(T&& other) {
        *this = concat(std::forward<T>(other));
        return *this;
    }

};


/* Equivalent to Python str(). */
inline Str str(const pybind11::handle& obj) {
    PyObject* string = PyObject_Str(obj.ptr());
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `ascii(obj)`.  Like `repr()`, but returns an ASCII-encoded
string. */
inline Str ascii(const pybind11::handle& obj) {
    PyObject* result = PyObject_ASCII(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(result);
}


/* Equivalent to Python `bin(obj)`.  Converts an integer or other object implementing
__index__() into a binary string representation. */
inline Str bin(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 2);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `oct(obj)`.  Converts an integer or other object implementing
__index__() into an octal string representation. */
inline Str oct(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 8);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `hex(obj)`.  Converts an integer or other object implementing
__index__() into a hexadecimal string representation. */
inline Str hex(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 16);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `chr(obj)`.  Converts an integer or other object implementing
__index__() into a unicode character. */
inline Str chr(const pybind11::handle& obj) {
    PyObject* string = PyUnicode_FromFormat("%llc", obj.cast<long long>());
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `ord(obj)`.  Converts a unicode character into an integer
representation. */
Int ord(const pybind11::handle& obj) {
    PyObject* ptr = obj.ptr();
    if (ptr == nullptr) {
        throw TypeError("cannot call ord() on a null object");
    }

    if (!PyUnicode_Check(ptr)) {
        std::ostringstream msg;
        msg << "ord() expected a string of length 1, but ";
        msg << Py_TYPE(ptr)->tp_name << "found";
        throw TypeError(msg.str());
    }

    Py_ssize_t length = PyUnicode_GET_LENGTH(ptr);
    if (length != 1) {
        std::ostringstream msg;
        msg << "ord() expected a character, but string of length " << length;
        msg << " found";
        throw TypeError(msg.str());
    }

    return PyUnicode_READ_CHAR(ptr, 0);
}


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_STRING_H
