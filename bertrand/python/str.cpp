module;

#ifdef BERTRAND_HAS_STD_FORMAT
    #include <format>
#endif

export module bertrand.python:str;

import :common;
import :int_;
import :tuple;
import :list;
import :set;
import :dict;


export namespace bertrand {
namespace py {


template <typename T>
struct __issubclass__<T, Str>                               : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return impl::str_like<T>; }
};


template <typename T>
struct __isinstance__<T, Str>                               : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Str>();
        } else if constexpr (issubclass<T, Str>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyUnicode_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


template <std::derived_from<Str> Self>
struct __len__<Self>                                        : Returns<size_t> {
    static size_t operator()(const Self& self) {
        return PyUnicode_GET_LENGTH(self.ptr());
    }
};


/* Represents a statically-typed Python string in C++. */
class Str : public Object {
    using Base = Object;
    using Self = Str;

public:
    static const Type type;

    Str(Handle h, borrowed_t t) : Base(h, t) {}
    Str(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Str, __init__<Str, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Str, std::remove_cvref_t<Args>...>::enable
        )
    Str(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Str, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Str, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Str, __explicit_init__<Str, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Str, std::remove_cvref_t<Args>...>::enable
        )
    explicit Str(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Str, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Make an explicit copy of the string. */
    [[nodiscard]] Str copy() const {
        size_t size = len(*this);
        PyObject* result = PyUnicode_New(size, max_char());
        if (result == nullptr) {
            Exception::from_python();
        }
        if (PyUnicode_CopyCharacters(
            result,
            0,
            this->ptr(),
            0,
            size
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
        if (len(str) != 1) {
            std::ostringstream msg;
            msg << "fill character must be a single character, not '" << str << "'";
            throw ValueError(msg.str());
        }
        Py_UCS4 code = PyUnicode_ReadChar(str.ptr(), 0);
        if (code == (Py_UCS4)-1 && PyErr_Occurred()) {
            Exception::from_python();
        }
        if (PyUnicode_Fill(
            this->ptr(),
            0,
            len(*this),
            code
        ) < 0) {
            Exception::from_python();
        }
    }

    /* Fill the string with a given character, given as a raw Python unicode point. */
    void fill(Py_UCS4 ch) {
        if (PyUnicode_Fill(
            this->ptr(),
            0,
            len(*this),
            ch
        ) < 0) {
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

    BERTRAND_METHOD([[nodiscard]], capitalize, const)
    BERTRAND_METHOD([[nodiscard]], casefold, const)
    BERTRAND_METHOD([[nodiscard]], center, const)

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

    BERTRAND_METHOD([[nodiscard]], encode, const)

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

    BERTRAND_METHOD([[nodiscard]], expandtabs, const)

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

    BERTRAND_METHOD([[nodiscard]], format, const)
    BERTRAND_METHOD([[nodiscard]], format_map, const)

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

    BERTRAND_METHOD([[nodiscard]], isalnum, const)
    BERTRAND_METHOD([[nodiscard]], isalpha, const)
    BERTRAND_METHOD([[nodiscard]], isascii_, const)
    BERTRAND_METHOD([[nodiscard]], isdecimal, const)
    BERTRAND_METHOD([[nodiscard]], isdigit, const)
    BERTRAND_METHOD([[nodiscard]], isidentifier, const)
    BERTRAND_METHOD([[nodiscard]], islower, const)
    BERTRAND_METHOD([[nodiscard]], isnumeric, const)
    BERTRAND_METHOD([[nodiscard]], isprintable, const)
    BERTRAND_METHOD([[nodiscard]], isspace, const)
    BERTRAND_METHOD([[nodiscard]], istitle, const)
    BERTRAND_METHOD([[nodiscard]], isupper, const)

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

    BERTRAND_METHOD([[nodiscard]], ljust, const)
    BERTRAND_METHOD([[nodiscard]], lower, const)
    BERTRAND_METHOD([[nodiscard]], lstrip, const)
    BERTRAND_STATIC_METHOD([[nodiscard]], maketrans)

    [[nodiscard]] Tuple<Str> partition(const Str& sep) const {
        PyObject* result = PyUnicode_Partition(this->ptr(), sep.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Tuple<Str>>(result);
    }

    BERTRAND_METHOD([[nodiscard]], removeprefix, const)
    BERTRAND_METHOD([[nodiscard]], removesuffix, const)

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

    BERTRAND_METHOD([[nodiscard]], rjust, const)

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

    BERTRAND_METHOD([[nodiscard]], rstrip, const)

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

    BERTRAND_METHOD([[nodiscard]], strip, const)
    BERTRAND_METHOD([[nodiscard]], swapcase, const)
    BERTRAND_METHOD([[nodiscard]], title, const)
    BERTRAND_METHOD([[nodiscard]], translate, const)
    BERTRAND_METHOD([[nodiscard]], upper, const)
    BERTRAND_METHOD([[nodiscard]], zfill, const)

};


template <>
struct __init__<Str>                                        : Returns<Str> {
    static auto operator()() {
        PyObject* result = PyUnicode_FromStringAndSize("", 0);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <>
struct __init__<Str, char>                                   : Returns<Str> {
    static auto operator()(char ch) {
        PyObject* result = PyUnicode_FromOrdinal(ch);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <size_t N>
struct __init__<Str, char[N]>                               : Returns<Str> {
    static auto operator()(const char(&string)[N]) {
        PyObject* result = PyUnicode_FromStringAndSize(string, N - 1);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


// TODO: force explicit const char* rather than convertible_to?
template <impl::cpp_like T>
    requires (
        std::convertible_to<T, const char*> &&
        impl::not_proxy_like<T>
    )
struct __init__<Str, T>                                     : Returns<Str> {
    static auto operator()(const T& string) {
        PyObject* result = PyUnicode_FromString(string);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <impl::cpp_like T>
    requires (
        !std::convertible_to<T, const char*> &&
        std::convertible_to<T, std::string> &&
        impl::not_proxy_like<T>
    )
struct __init__<Str, T>                                     : Returns<Str> {
    static auto operator()(const T& string) {
        std::string s = string;
        PyObject* result = PyUnicode_FromStringAndSize(s.c_str(), s.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <impl::cpp_like T>
    requires (
        !std::convertible_to<T, const char*> &&
        !std::convertible_to<T, std::string> &&
        std::convertible_to<T, std::string_view> &&
        impl::not_proxy_like<T>
    )
struct __init__<Str, T>                                     : Returns<Str> {
    static auto operator()(const T& string) {
        std::string_view s = string;
        PyObject* result = PyUnicode_FromStringAndSize(s.data(), s.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <impl::python_like T> requires (!impl::str_like<T>)
struct __explicit_init__<Str, T>                            : Returns<Str> {
    static auto operator()(const T& obj) {
        PyObject* result = PyObject_Str(obj.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <impl::cpp_like T>
    requires (
        !std::convertible_to<T, const char*> &&
        !std::convertible_to<T, std::string> &&
        !std::convertible_to<T, std::string_view>
    )
struct __explicit_init__<Str, T>                            : Returns<Str> {
    static auto operator()(const T& obj) {
        PyObject* result = PyObject_Str(as_object(obj).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


#ifdef BERTRAND_HAS_STD_FORMAT


    template <std::convertible_to<std::string_view> T, typename... Args>
        requires (sizeof...(Args) > 0)
    struct __explicit_init__<Str, T, Args...>               : Returns<Str> {
        static auto operator()(const T& format, const Args&... args) {
            std::string result = std::vformat(format, std::make_format_args(args)...);
            PyObject* str = PyUnicode_FromStringAndSize(result.c_str(), result.size());
            if (str == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Str>(str);
        }
    };


    template <std::convertible_to<std::string_view> T, typename... Args>
        requires (sizeof...(Args) > 0)
    struct __explicit_init__<Str, std::locale, T, Args...>  : Returns<Str> {
        static auto operator()(const std::locale& locale, const T& format, const Args&... args) {
            std::string result = std::vformat(locale, format, std::make_format_args(args)...);
            PyObject* str = PyUnicode_FromStringAndSize(result.c_str(), result.size());
            if (str == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Str>(str);
        }
    };


    template <impl::python_like T, typename... Args>
        requires (impl::str_like<T> && sizeof...(Args) > 0)
    struct __explicit_init__<Str, T, Args...>               : Returns<Str> {
        static auto operator()(const T& format, const Args&... args) {
            if constexpr (impl::pybind11_like<T>) {
                return Str(format.template cast<std::string>(), args...);
            } else {
                return Str(static_cast<std::string>(format), args...);
            }
        }
    };


    template <impl::python_like T, typename... Args>
        requires (impl::str_like<T> && sizeof...(Args) > 0)
    struct __explicit_init__<Str, std::locale, T, Args...>  : Returns<Str> {
        static auto operator()(const std::locale& locale, const T& format, const Args&... args) {
            if constexpr (impl::pybind11_like<T>) {
                return Str(locale, format.template cast<std::string>(), args...);
            } else {
                return Str(locale, static_cast<std::string>(format), args...);
            }
        }
    };


#endif


template <std::derived_from<Str> From>
struct __cast__<From, std::string> : Returns<std::string> {
    static std::string operator()(const From& from) {
        Py_ssize_t length;
        const char* result = PyUnicode_AsUTF8AndSize(from.ptr(), &length);
        if (result == nullptr) {
            Exception::from_python();
        }
        return {result, static_cast<size_t>(length)};
    }
};


template <std::derived_from<Str> Self, std::convertible_to<Str> Key>
struct __contains__<Self, Key>                              : Returns<bool> {
    static bool operator()(const Self& self, const Str& key) {
        int result = PyUnicode_Contains(
            self.ptr(),
            key.ptr()
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


template <std::derived_from<Str> L, impl::str_like R>
struct __add__<L, R>                                        : Returns<Str> {
    static auto operator()(const L& lhs, R& rhs) {
        PyObject* result = PyUnicode_Concat(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <impl::str_like L, std::derived_from<Str> R> requires (!std::derived_from<L, Str>)
struct __add__<L, R>                                        : Returns<Str> {
    static auto operator()(const L& lhs, R& rhs) {
        PyObject* result = PyUnicode_Concat(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <std::derived_from<Str> L, impl::str_like R>
struct __iadd__<L, R>                                       : Returns<Str&> {
    static void operator()(L& lhs, const R& rhs) {
        lhs = lhs + rhs;
    }
};


template <std::derived_from<Str> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Str> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Repeat(as_object(lhs).ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <impl::int_like L, std::derived_from<Str> R>
struct __mul__<L, R>                                        : Returns<Str> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Repeat(as_object(rhs).ptr(), lhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }
};


template <std::derived_from<Str> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Str&> {
    static void operator()(L& lhs, const R& rhs) {
        PyObject* result = PySequence_InPlaceRepeat(lhs.ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }
};


}  // namespace py
}  // namespace bertrand


export namespace std {

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
