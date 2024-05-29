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


/* Represents a statically-typed Python string in C++. */
class Str : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool typecheck() {
        return impl::str_like<T>;
    }

    template <typename T>
    static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();
        } else if constexpr (typecheck<T>()) {
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

    /* Default constructor.  Initializes to empty string. */
    Str() : Base(PyUnicode_FromStringAndSize("", 0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Reinterpret_borrow/reinterpret_steal constructors. */
    Str(Handle h, const borrowed_t& t) : Base(h, t) {}
    Str(Handle h, const stolen_t& t) : Base(h, t) {}

    /* Convert an equivalent pybind11 type into a py::Str. */
    template <impl::pybind11_like T> requires (typecheck<T>())
    Str(T&& other) : Base(std::forward<T>(other)) {}

    /* Unwrap a pybind11 accessor into a py::Str. */
    template <typename Policy>
    Str(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Str>(accessor).release(), stolen_t{})
    {}

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
    explicit Str(const T& obj) : Base(PyObject_Str(as_object(obj).ptr()), stolen_t{}) {
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

    /* Make an explicit copy of the string. */
    [[nodiscard]] Str copy() const {
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

    /* Get the underlying unicode buffer. */
    [[nodiscard]] void* data() const noexcept {
        return PyUnicode_DATA(this->ptr());
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

    /* Get the kind of the string, indicating the size of the unicode points stored
    within. */
    [[nodiscard]] int kind() const noexcept {
        return PyUnicode_KIND(this->ptr());
    }

    /* Get the maximum code point that is suitable for creating another string based
    on this string. */
    [[nodiscard]] Py_UCS4 max_char() const noexcept {
        return PyUnicode_MAX_CHAR_VALUE(this->ptr());
    }

    /* Return a substring from this string. */
    [[nodiscard]] Str substring(Py_ssize_t start = 0, Py_ssize_t end = -1) const {
        PyObject* result = PyUnicode_Substring(this->ptr(), start, end);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    BERTRAND_METHOD(Str, [[nodiscard]], capitalize, const)
    BERTRAND_METHOD(Str, [[nodiscard]], casefold, const)
    BERTRAND_METHOD(Str, [[nodiscard]], center, const)

    [[nodiscard]] size_t count(
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

    BERTRAND_METHOD(Str, [[nodiscard]], encode, const)

    [[nodiscard]] bool endswith(
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

    BERTRAND_METHOD(Str, [[nodiscard]], expandtabs, const)

    [[nodiscard]] Py_ssize_t find(
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

    [[nodiscard]] Py_ssize_t find(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
    }

    BERTRAND_METHOD(Str, [[nodiscard]], format, const)
    BERTRAND_METHOD(Str, [[nodiscard]], format_map, const)

    [[nodiscard]] Py_ssize_t index(
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

    [[nodiscard]] Py_ssize_t index(
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

    BERTRAND_METHOD(Str, [[nodiscard]], isalnum, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isalpha, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isascii, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isdecimal, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isdigit, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isidentifier, const)
    BERTRAND_METHOD(Str, [[nodiscard]], islower, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isnumeric, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isprintable, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isspace, const)
    BERTRAND_METHOD(Str, [[nodiscard]], istitle, const)
    BERTRAND_METHOD(Str, [[nodiscard]], isupper, const)

    template <impl::is_iterable T>
    [[nodiscard]] Str join(const T& iterable) const {
        PyObject* result = PyUnicode_Join(
            this->ptr(),
            as_object(iterable).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

    [[nodiscard]] Str join(const std::initializer_list<Str>& iterable) const {
        return join(py::List(iterable));
    }

    BERTRAND_METHOD(Str, [[nodiscard]], ljust, const)
    BERTRAND_METHOD(Str, [[nodiscard]], lower, const)
    BERTRAND_METHOD(Str, [[nodiscard]], lstrip, const)
    BERTRAND_STATIC_METHOD(Str, [[nodiscard]], maketrans)

    [[nodiscard]] Tuple<Str> partition(const Str& sep) const {
        PyObject* result = PyUnicode_Partition(this->ptr(), sep.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Tuple<Str>>(result);
    }

    BERTRAND_METHOD(Str, [[nodiscard]], removeprefix, const)
    BERTRAND_METHOD(Str, [[nodiscard]], removesuffix, const)

    [[nodiscard]] Str replace(const Str& sub, const Str& repl, Py_ssize_t maxcount = -1) const {
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

    [[nodiscard]] Py_ssize_t rfind(
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

    [[nodiscard]] Py_ssize_t rfind(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, -1);
    }

    [[nodiscard]] Py_ssize_t rindex(
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

    [[nodiscard]] Py_ssize_t rindex(
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

    BERTRAND_METHOD(Str, [[nodiscard]], rjust, const)

    [[nodiscard]] Tuple<Str> rpartition(const Str& sep) const {
        PyObject* result = PyUnicode_RPartition(this->ptr(), sep.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Tuple<Str>>(result);
    }

    [[nodiscard]] List<Str> rsplit() const {
        PyObject* result = PyUnicode_RSplit(this->ptr(), nullptr, -1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    [[nodiscard]] List<Str> rsplit(const Str& sep, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_RSplit(this->ptr(), sep.ptr(), maxsplit);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    BERTRAND_METHOD(Str, [[nodiscard]], rstrip, const)

    [[nodiscard]] List<Str> split() const {
        PyObject* result = PyUnicode_Split(this->ptr(), nullptr, -1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    [[nodiscard]] List<Str> split(const Str& sep, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(this->ptr(), sep.ptr(), maxsplit);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    [[nodiscard]] List<Str> splitlines(bool keepends = false) const {
        PyObject* result = PyUnicode_Splitlines(this->ptr(), keepends);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Str>>(result);
    }

    [[nodiscard]] bool startswith(
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

    BERTRAND_METHOD(Str, [[nodiscard]], strip, const)
    BERTRAND_METHOD(Str, [[nodiscard]], swapcase, const)
    BERTRAND_METHOD(Str, [[nodiscard]], title, const)
    BERTRAND_METHOD(Str, [[nodiscard]], translate, const)
    BERTRAND_METHOD(Str, [[nodiscard]], upper, const)
    BERTRAND_METHOD(Str, [[nodiscard]], zfill, const)

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
