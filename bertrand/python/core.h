#ifndef BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_H

#include "core/declarations.h"
#include "core/object.h"
#include "core/code.h"
#include "core/except.h"
#include "core/arg.h"
#include "core/ops.h"
#include "core/access.h"
#include "core/iter.h"
#include "core/union.h"
#include "core/func.h"
#include "core/type.h"
#include "core/module.h"


namespace py {


/* Equivalent to Python `print(args...)`. */
template <typename... Args>
    requires (
        Function<void(
            Arg<"args", const Str&>::args,
            Arg<"sep", const Str&>::opt,
            Arg<"end", const Str&>::opt,
            Arg<"file", const Object&>::opt,
            Arg<"flush", const Bool&>::opt
        )>::template bind<Args...>
    )
void print(Args&&... args) {
    static Object func = [] {
        PyObject* builtins = PyEval_GetBuiltins();
        if (builtins == nullptr) {
            Exception::from_python();
        }
        PyObject* func = PyDict_GetItem(builtins, impl::TemplateString<"print">::ptr);
        if (func == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(func);
    }();

    Function<void(
        Arg<"args", const Str&>::args,
        Arg<"sep", const Str&>::opt,
        Arg<"end", const Str&>::opt,
        Arg<"file", const Object&>::opt,
        Arg<"flush", const Bool&>::opt
    )>::invoke_py(func, std::forward<Args>(args)...);
}


//////////////////////
////    OBJECT    ////
//////////////////////


namespace impl {

    template <cpp T>
        requires (
            has_python<T> &&
            has_cpp<python_type<T>> &&
            is<T, cpp_type<python_type<T>>>
        )
    [[nodiscard]] auto wrap(T& obj) -> python_type<T> {
        using Wrapper = __cast__<T>::type;
        using Variant = decltype(ptr(std::declval<Wrapper>())->m_cpp);
        Type<Wrapper> type;
        PyTypeObject* type_ptr = ptr(type);
        PyObject* self = type_ptr->tp_alloc(type_ptr, 0);
        if (self == nullptr) {
            Exception::from_python();
        }
        new (&reinterpret_cast<typename Wrapper::__python__*>(self)->m_cpp) Variant(&obj);
        return reinterpret_steal<Wrapper>(self);
    }


    template <cpp T>
        requires (
            has_python<T> &&
            has_cpp<python_type<T>> &&
            is<T, cpp_type<python_type<T>>>
        )
    [[nodiscard]] auto wrap(const T& obj) -> python_type<T> {
        using Wrapper = __cast__<T>::type;
        using Variant = decltype(ptr(std::declval<Wrapper>())->m_cpp);
        Type<Wrapper> type;
        PyTypeObject* type_ptr = ptr(type);
        PyObject* self = type_ptr->tp_alloc(type_ptr, 0);
        if (self == nullptr) {
            Exception::from_python();
        }
        new (&reinterpret_cast<typename Wrapper::__python__*>(self)->m_cpp) Variant(&obj);
        return reinterpret_steal<Wrapper>(self);
    }


    template <python T> requires (has_cpp<T>)
    [[nodiscard]] auto& unwrap(T& obj) {
        if constexpr (impl::has_cpp<T>) {
            using CppType = impl::cpp_type<T>;
            return std::visit(
                Object::Visitor{
                    [](CppType& cpp) -> CppType& { return cpp; },
                    [](CppType* cpp) -> CppType& { return *cpp; },
                    [&obj](const CppType* cpp) -> CppType& {
                        throw TypeError(
                            "requested a mutable reference to const object: " +
                            repr(obj)
                        );
                    }
                },
                obj->m_cpp
            );
        } else {
            return obj;
        }
    }


    template <python T> requires (has_cpp<T>)
    [[nodiscard]] const auto& unwrap(const T& obj) {
        if constexpr (impl::has_cpp<T>) {
            using CppType = impl::cpp_type<T>;
            return std::visit(
                Object::Visitor{
                    [](const CppType& cpp) -> const CppType& { return cpp; },
                    [](const CppType* cpp) -> const CppType& { return *cpp; }
                },
                obj->m_cpp
            );
        } else {
            return obj;
        }
    }

}


template <impl::inherits<Object> From>
inline bool __explicit_cast__<From, bool>::operator()(From&& from) {
    if constexpr (
        impl::has_cpp<Self> &&
        impl::has_operator_bool<impl::cpp_type<Self>>
    ) {
        return static_cast<bool>(from_python(std::forward<From>(from)));

    } else {
        int result = PyObject_IsTrue(ptr(from));
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename Derived, impl::is<Object> Base>
constexpr bool __isinstance__<Derived, Base>::operator()(Derived obj, Base cls) {
    if constexpr (impl::python<Derived>) {
        int result = PyObject_IsInstance(
            ptr(to_python(std::forward<Derived>(obj))),
            ptr(std::forward<Base>(cls))
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    } else {
        return false;
    }
}


template <typename T, impl::is<Object> Base>
bool __issubclass__<T, Base>::operator()(T&& obj, Base&& cls) {
    int result = PyObject_IsSubclass(
        ptr(to_python(std::forward<T>(obj))),
        ptr(cls)
    );
    if (result == -1) {
        Exception::from_python();
    }
    return result;
}


template <impl::inherits<Object> From, impl::inherits<From> To>
    requires (!impl::is<From, To>)
auto __cast__<From, To>::operator()(From&& from) {
    if (isinstance<To>(from)) {
        if constexpr (std::is_lvalue_reference_v<From>) {
            return reinterpret_borrow<To>(ptr(from));
        } else {
            return reinterpret_steal<To>(release(from));
        }
    } else {
        /// TODO: Type<From> and Type<To> must apply std::remove_cvref_t<>?  Maybe that
        /// can be rolled into the Type<> class itself?
        /// -> The only way this can be handled universally is if these forward
        /// declarations are filled after type.h is included
        throw TypeError(
            "cannot convert Python object from type '" + repr(Type<From>()) +
            "' to type '" + repr(Type<To>()) + "'"
        );
    }
}


template <impl::inherits<Object> From, impl::cpp To>
    requires (__cast__<To>::enable && std::integral<To>)
To __explicit_cast__<From, To>::operator()(From&& from) {
    long long result = PyLong_AsLongLong(ptr(from));
    if (result == -1 && PyErr_Occurred()) {
        Exception::from_python();
    } else if (
        result < std::numeric_limits<To>::min() ||
        result > std::numeric_limits<To>::max()
    ) {
        throw OverflowError(
            "integer out of range for " + impl::demangle(typeid(To).name()) +
            ": " + std::to_string(result)
        );
    }
    return result;
}


template <impl::inherits<Object> From, impl::cpp To>
    requires (__cast__<To>::enable && std::floating_point<To>)
To __explicit_cast__<From, To>::operator()(From&& from) {
    double result = PyFloat_AsDouble(ptr(from));
    if (result == -1.0 && PyErr_Occurred()) {
        Exception::from_python();
    }
    return result;
}


template <impl::inherits<Object> From, typename Float>
auto __explicit_cast__<From, std::complex<Float>>::operator()(From&& from) {
    Py_complex result = PyComplex_AsCComplex(ptr(from));
    if (result.real == -1.0 && PyErr_Occurred()) {
        Exception::from_python();
    }
    return std::complex<Float>(result.real, result.imag);
}


/// TODO: this same logic should carry over for strings, bytes, and byte arrays to
/// allow conversion to any kind of basic string type.


template <impl::inherits<Object> From, typename Char>
auto __explicit_cast__<From, std::basic_string<Char>>::operator()(From&& from) {
    PyObject* str = PyObject_Str(reinterpret_cast<PyObject*>(ptr(from)));
    if (str == nullptr) {
        Exception::from_python();
    }
    if constexpr (sizeof(Char) == 1) {
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(str, &size);
        if (data == nullptr) {
            Py_DECREF(str);
            Exception::from_python();
        }
        std::basic_string<Char> result(data, size);
        Py_DECREF(str);
        return result;

    } else if constexpr (sizeof(Char) == 2) {
        PyObject* bytes = PyUnicode_AsUTF16String(str);
        Py_DECREF(str);
        if (bytes == nullptr) {
            Exception::from_python();
        }
        std::basic_string<Char> result(
            reinterpret_cast<const Char*>(PyBytes_AsString(bytes)) + 1,  // skip BOM marker
            (PyBytes_GET_SIZE(bytes) / sizeof(Char)) - 1
        );
        Py_DECREF(bytes);
        return result;

    } else if constexpr (sizeof(Char) == 4) {
        PyObject* bytes = PyUnicode_AsUTF32String(str);
        Py_DECREF(str);
        if (bytes == nullptr) {
            Exception::from_python();
        }
        std::basic_string<Char> result(
            reinterpret_cast<const Char*>(PyBytes_AsString(bytes)) + 1,  // skip BOM marker
            (PyBytes_GET_SIZE(bytes) / sizeof(Char)) - 1
        );
        Py_DECREF(bytes);
        return result;

    } else {
        static_assert(
            sizeof(Char) == 1 || sizeof(Char) == 2 || sizeof(Char) == 4,
            "unsupported character size for string conversion"
        );
    }
}


template <std::derived_from<std::ostream> Stream, impl::inherits<Object> Self>
Stream& __lshift__<Stream, Self>::operator()(Stream& stream, Self self) {
    PyObject* repr = PyObject_Str(ptr(self));
    if (repr == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t size;
    const char* data = PyUnicode_AsUTF8AndSize(repr, &size);
    if (data == nullptr) {
        Py_DECREF(repr);
        Exception::from_python();
    }
    stream.write(data, size);
    Py_DECREF(repr);
    return stream;
}


////////////////////
////    CODE    ////
////////////////////


template <std::convertible_to<std::string> Source>
auto __cast__<Source, Code>::operator()(const std::string& source) {
    std::string line;
    std::string parsed;
    std::istringstream stream(source);
    size_t min_indent = std::numeric_limits<size_t>::max();

    // find minimum indentation
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        size_t indent = line.find_first_not_of(" \t");
        if (indent != std::string::npos) {
            min_indent = std::min(min_indent, indent);
        }
    }

    // dedent if necessary
    if (min_indent != std::numeric_limits<size_t>::max()) {
        std::string temp;
        std::istringstream stream2(source);
        while (std::getline(stream2, line)) {
            if (line.empty() || line.find_first_not_of(" \t") == std::string::npos) {
                temp += '\n';
            } else {
                temp += line.substr(min_indent) + '\n';
            }
        }
        parsed = temp;
    } else {
        parsed = source;
    }

    PyObject* result = Py_CompileString(
        parsed.c_str(),
        "<embedded Python script>",
        Py_file_input
    );
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Code>(result);
}


/* Parse and compile a source file into a Python code object. */
[[nodiscard]] inline Code Interface<Code>::compile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw FileNotFoundError(std::string("'") + path + "'");
    }
    std::istreambuf_iterator<char> begin(file), end;
    PyObject* result = Py_CompileString(
        std::string(begin, end).c_str(),
        path.c_str(),
        Py_file_input
    );
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Code>(result);
}


/////////////////////
////    FRAME    ////
/////////////////////



inline auto __init__<Frame>::operator()() {
    PyFrameObject* frame = PyEval_GetFrame();
    if (frame == nullptr) {
        throw RuntimeError("no frame is currently executing");
    }
    return reinterpret_borrow<Frame>(reinterpret_cast<PyObject*>(frame));
}


template <std::convertible_to<int> T>
Frame __init__<Frame, T>::operator()(int skip) {
    PyFrameObject* frame = reinterpret_cast<PyFrameObject*>(
        Py_XNewRef(PyEval_GetFrame())
    );
    if (frame == nullptr) {
        throw RuntimeError("no frame is currently executing");
    }

    // negative indexing offsets from the most recent frame
    if (skip < 0) {
        for (int i = 0; i > skip; --i) {
            PyFrameObject* temp = PyFrame_GetBack(frame);
            if (temp == nullptr) {
                return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(frame));
            }
            Py_DECREF(frame);
            frame = temp;
        }
        return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(frame));
    }

    // positive indexing counts from the least recent frame
    std::vector<Frame> frames;
    while (frame != nullptr) {
        frames.push_back(reinterpret_steal<Frame>(
            reinterpret_cast<PyObject*>(frame))
        );
        frame = PyFrame_GetBack(frame);
    }
    if (skip >= frames.size()) {
        return frames.front();
    }
    return frames[skip];
}


template <impl::is<Frame> Self>
inline auto __call__<Self>::operator()(Self&& frame) {
    PyObject* result = PyEval_EvalFrame(ptr(frame));
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Object>(result);
}


[[nodiscard]] inline std::string Interface<Frame>::to_string(this auto&& self) {
    PyFrameObject* frame = ptr(self);
    PyCodeObject* code = PyFrame_GetCode(frame);

    std::string out;
    if (code != nullptr) {
        Py_ssize_t len;
        const char* name = PyUnicode_AsUTF8AndSize(code->co_filename, &len);
        if (name == nullptr) {
            Py_DECREF(code);
            Exception::from_python();
        }
        out += "File \"" + std::string(name, len) + "\", line ";
        out += std::to_string(PyFrame_GetLineNumber(frame)) + ", in ";
        name = PyUnicode_AsUTF8AndSize(code->co_name, &len);
        if (name == nullptr) {
            Py_DECREF(code);
            Exception::from_python();
        }
        out += std::string(name, len);
        Py_DECREF(code);
    } else {
        out += "File \"<unknown>\", line 0, in <unknown>";
    }

    return out;
}


[[nodiscard]] inline std::optional<Code> Interface<Frame>::_get_code(this auto&& self) {
    PyCodeObject* code = PyFrame_GetCode(ptr(self));
    if (code == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Code>(reinterpret_cast<PyObject*>(code));
}


[[nodiscard]] inline std::optional<Frame> Interface<Frame>::_get_back(this auto&& self) {
    PyFrameObject* result = PyFrame_GetBack(ptr(self));
    if (result == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
}


[[nodiscard]] inline size_t Interface<Frame>::_get_line_number(this auto&& self) {
    return PyFrame_GetLineNumber(ptr(self));
}


[[nodiscard]] inline size_t Interface<Frame>::_get_last_instruction(this auto&& self) {
    int result = PyFrame_GetLasti(ptr(self));
    if (result < 0) {
        throw RuntimeError("frame is not currently executing");
    }
    return result;
}


[[nodiscard]] inline std::optional<Object> Interface<Frame>::_get_generator(this auto&& self) {
    PyObject* result = PyFrame_GetGenerator(ptr(self));
    if (result == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Object>(result);
}


/////////////////////////
////    TRACEBACK    ////
/////////////////////////



template <impl::is<Traceback> Self>
[[nodiscard]] Frame __iter__<Self>::operator*() const {
    if (curr == nullptr) {
        throw StopIteration();
    }
    return reinterpret_borrow<Frame>(
        reinterpret_cast<PyObject*>(curr->tb_frame)
    );
}


template <impl::is<Traceback> Self>
[[nodiscard]] Frame __reversed__<Self>::operator*() const {
    if (index < 0) {
        throw StopIteration();
    }
    return reinterpret_borrow<Frame>(
        reinterpret_cast<PyObject*>(frames[index]->tb_frame)
    );
}


[[nodiscard]] inline std::string Interface<Traceback>::to_string(
    this const auto& self
) {
    std::string out = "Traceback (most recent call last):";
    PyTracebackObject* tb = ptr(self);
    while (tb != nullptr) {
        out += "\n  ";
        out += reinterpret_borrow<Frame>(
            reinterpret_cast<PyObject*>(tb->tb_frame)
        ).to_string();
        tb = tb->tb_next;
    }
    return out;
}


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


/* Fall back to the python-level __init__/__new__ constructors if no other constructor
is available. */
template <std::derived_from<Object> Self, typename... Args>
    requires (
        !__init__<Self, Args...>::enable &&
        !__explicit_init__<Self, Args...>::enable &&
        impl::attr_is_callable_with<Self, "__init__", Args...> ||
        impl::attr_is_callable_with<Self, "__new__", Args...>
    )
struct __explicit_init__<Self, Args...> : Returns<Self> {
    static auto operator()(Args&&... args) {
        static_assert(
            impl::attr_is_callable_with<Self, "__init__", Args...> ||
            impl::attr_is_callable_with<Self, "__new__", Args...>,
            "Type must have either an __init__ or __new__ method that is callable "
            "with the given arguments."
        );
        if constexpr (impl::attr_is_callable_with<Self, "__init__", Args...>) {
            return __getattr__<Self, "__init__">::type::template with_return<Self>::invoke_py(
                Type<Self>(),
                std::forward<Args>(args)...
            );
        } else {
            return __getattr__<Self, "__new__">::type::invoke_py(
                Type<Self>(),
                std::forward<Args>(args)...
            );
        }
    }
};


/* Invoke a type's metaclass to dynamically create a new Python type.  This 2-argument
form allows the base type to be specified as the template argument, and restricts the
type to single inheritance. */
template <typename T, typename... Args>
    requires (
        Function<Type<T>(
            py::Arg<"name", const Str&>,
            py::Arg<"dict", const Dict<Str, Object>&>)
        >::template invocable<Args...>
    )
struct __explicit_init__<Type<T>, Args...> : Returns<Type<T>> {
    static auto operator()(Args&&... args) {
        auto helper = [](
            py::Arg<"name", const Str&> name,
            py::Arg<"dict", const Dict<Str, Object>&> dict
        ) {
            Type<T> self;
            return Function<Type<T>(
                py::Arg<"name", const Str&>,
                py::Arg<"bases", const Tuple<Type<T>>&>,
                py::Arg<"dict", const Dict<Str, Object>&>)
            >::template invoke_py<Type<T>>(
                reinterpret_cast<PyObject*>(Py_TYPE(ptr(self))),
                name.value,
                Tuple<Type<T>>{self},
                dict.value
            );
        };
        return Function<decltype(helper)>::template invoke_cpp(
            std::forward<Args>(args)...
        );
    }
};


/* Invoke the `type` metaclass to dynamically create a new Python type.  This
3-argument form is only available for the root Type<Object> class, and allows a tuple
of bases to be passed to enable multiple inheritance. */
template <typename... Args>
    requires (
        Function<Type<Object>(
            py::Arg<"name", const Str&>,
            py::Arg<"bases", const Tuple<Type<Object>>&>,
            py::Arg<"dict", const Dict<Str, Object>&>)
        >::template invocable<Args...>
    )
struct __explicit_init__<Type<Object>, Args...> : Returns<Type<Object>> {
    static auto operator()(Args&&... args) {
        return Function<Type<Object>(
            py::Arg<"name", const Str&>,
            py::Arg<"bases", const Tuple<Type<Object>>&>,
            py::Arg<"dict", const Dict<Str, Object>&>)
        >::template invoke_py<Type<Object>>(
            reinterpret_cast<PyObject*>(&PyType_Type),
            std::forward<Args>(args)...
        );
    }
};


}  // namespace py


#endif
