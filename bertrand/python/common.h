#ifndef BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_H

#include "common/declarations.h"
#include "common/except.h"
#include "common/ops.h"
#include "common/object.h"
#include "common/func.h"
#include "common/access.h"
#include "common/iter.h"
#include "common/control.h"


namespace bertrand {
namespace py {


////////////////////
////    NONE    ////
////////////////////


template <typename T>
struct __issubclass__<T, NoneType>                          : Returns<bool> {
    static constexpr bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return impl::none_like<T>; }
};


template <typename T>
struct __isinstance__<T, NoneType>                          : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, NoneType>();
        } else if constexpr (issubclass<T, NoneType>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && Py_IsNone(obj.ptr());
        } else {
            return false;
        }
    }
};


/* Represents the type of Python's `None` singleton in C++. */
class NoneType : public Object {
    using Base = Object;
    using Self = NoneType;

public:
    static const Type type;

    NoneType(Handle h, borrowed_t t) : Base(h, t) {}
    NoneType(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<NoneType, __init__<NoneType, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<NoneType, std::remove_cvref_t<Args>...>::enable
        )
    NoneType(Args&&... args) : Base((
        Interpreter::init(),
        __init__<NoneType, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<NoneType, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<NoneType, __explicit_init__<NoneType, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<NoneType, std::remove_cvref_t<Args>...>::enable
        )
    explicit NoneType(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<NoneType, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

};


template <>
struct __init__<NoneType>                                   : Returns<NoneType> {
    static auto operator()() {
        return reinterpret_borrow<NoneType>(Py_None);
    }
};


template <impl::none_like T>
struct __init__<NoneType, T>                                : Returns<NoneType> {
    static NoneType operator()(const T&) { return {}; }
};


//////////////////////////////
////    NOTIMPLEMENTED    ////
//////////////////////////////


template <typename T>
struct __issubclass__<T, NotImplementedType>                : Returns<bool> {
    static constexpr bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return impl::notimplemented_like<T>; }
};


template <typename T>
struct __isinstance__<T, NotImplementedType>                : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, NotImplementedType>();
        } else if constexpr (issubclass<T, NotImplementedType>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr) {
                return false;
            }
            int result = PyObject_IsInstance(
                obj.ptr(),
                (PyObject*) Py_TYPE(Py_NotImplemented)
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        } else {
            return false;
        }
    }
};


/* Represents the type of Python's `NotImplemented` singleton in C++. */
class NotImplementedType : public Object {
    using Base = Object;
    using Self = NotImplementedType;

public:
    static const Type type;

    NotImplementedType(Handle h, borrowed_t t) : Base(h, t) {}
    NotImplementedType(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<NotImplementedType, __init__<NotImplementedType, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<NotImplementedType, std::remove_cvref_t<Args>...>::enable
        )
    NotImplementedType(Args&&... args) : Base((
        Interpreter::init(),
        __init__<NotImplementedType, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<NotImplementedType, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<NotImplementedType, __explicit_init__<NotImplementedType, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<NotImplementedType, std::remove_cvref_t<Args>...>::enable
        )
    explicit NotImplementedType(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<NotImplementedType, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

};


template <>
struct __init__<NotImplementedType>                         : Returns<NotImplementedType> {
    static auto operator()() {
        return reinterpret_borrow<NotImplementedType>(Py_NotImplemented);
    }
};


template <impl::notimplemented_like T>
struct __init__<NotImplementedType, T>                      : Returns<NotImplementedType> {
    static NotImplementedType operator()(const T&) { return {}; }
};


////////////////////////
////    ELLIPSIS    ////
////////////////////////


template <typename T>
struct __issubclass__<T, EllipsisType>                      : Returns<bool> {
    static constexpr bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return impl::ellipsis_like<T>; }
};


template <typename T>
struct __isinstance__<T, EllipsisType>                      : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, EllipsisType>();
        } else if constexpr (issubclass<T, EllipsisType>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr) {
                return false;
            }
            int result = PyObject_IsInstance(
                obj.ptr(),
                (PyObject*) Py_TYPE(Py_Ellipsis)
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        } else {
            return false;
        }
    }
};


/* Represents the type of Python's `Ellipsis` singleton in C++. */
class EllipsisType : public Object {
    using Base = Object;
    using Self = EllipsisType;

public:
    static const Type type;

    EllipsisType(Handle h, borrowed_t t) : Base(h, t) {}
    EllipsisType(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<EllipsisType, __init__<EllipsisType, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<EllipsisType, std::remove_cvref_t<Args>...>::enable
        )
    EllipsisType(Args&&... args) : Base((
        Interpreter::init(),
        __init__<EllipsisType, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<EllipsisType, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<EllipsisType, __explicit_init__<EllipsisType, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<EllipsisType, std::remove_cvref_t<Args>...>::enable
        )
    explicit EllipsisType(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<EllipsisType, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

};


template <>
struct __init__<EllipsisType>                               : Returns<EllipsisType> {
    static auto operator()() {
        return reinterpret_borrow<EllipsisType>(Py_Ellipsis);
    }
};


template <impl::ellipsis_like T>
struct __init__<EllipsisType, T>                            : Returns<EllipsisType> {
    static EllipsisType operator()(const T&) { return {}; }
};


static const NoneType None;
static const EllipsisType Ellipsis;
static const NotImplementedType NotImplemented;


/////////////////////
////    SLICE    ////
/////////////////////


template <typename T>
struct __issubclass__<T, Slice>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) { return operator()(); }
    static consteval bool operator()() { return impl::slice_like<T>; }
};


template <typename T>
struct __isinstance__<T, Slice>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Slice>();
        } else if constexpr (issubclass<T, Slice>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PySlice_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


namespace impl {

    /* An initializer that explicitly requires an integer or None. */
    struct SliceInitializer {
        Object value;
        template <typename T>
            requires (impl::int_like<T> || impl::none_like<T>)
        SliceInitializer(T&& value) : value(std::forward<T>(value)) {}
    };

}


/* Represents a statically-typed Python `slice` object in C++.  Note that the start,
stop, and step values do not strictly need to be integers. */
class Slice : public Object {
    using Base = Object;
    using Self = Slice;

public:
    static const Type type;

    Slice(Handle h, borrowed_t t) : Base(h, t) {}
    Slice(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Slice, __init__<Slice, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Slice, std::remove_cvref_t<Args>...>::enable
        )
    Slice(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Slice, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Slice, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Slice, __explicit_init__<Slice, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Slice, std::remove_cvref_t<Args>...>::enable
        )
    explicit Slice(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Slice, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Initializer list constructor.  Unlike the other constructors (which can accept
    any kind of object), this syntax is restricted only to integers, py::None, and
    std::nullopt. */
    Slice(const std::initializer_list<impl::SliceInitializer>& indices) :
        Base(nullptr, stolen_t{})
    {
        if (indices.size() > 3) {
            throw ValueError("slices must be of the form {[start[, stop[, step]]]}");
        }
        size_t i = 0;
        std::array<Object, 3> params {None, None, None};
        for (const impl::SliceInitializer& item : indices) {
            params[i++] = item.value;
        }
        m_ptr = PySlice_New(params[0].ptr(), params[1].ptr(), params[2].ptr());
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Get the start object of the slice.  Note that this might not be an integer. */
    [[nodiscard]] auto start() const {
        return attr<"start">().value();
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    [[nodiscard]] auto stop() const {
        return attr<"stop">().value();
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    [[nodiscard]] auto step() const {
        return attr<"step">().value();
    }

    /* Normalize the indices of this slice against a container of the given length.
    This accounts for negative indices and clips those that are out of bounds.
    Returns a simple data struct with the following fields:

        * (Py_ssize_t) start: the normalized start index
        * (Py_ssize_t) stop: the normalized stop index
        * (Py_ssize_t) step: the normalized step size
        * (Py_ssize_t) length: the number of indices that are included in the slice

    It can be destructured using C++17 structured bindings:

        auto [start, stop, step, length] = slice.indices(size);
    */
    [[nodiscard]] auto indices(size_t size) const {
        struct Indices {
            Py_ssize_t start = 0;
            Py_ssize_t stop = 0;
            Py_ssize_t step = 0;
            Py_ssize_t length = 0;
        };

        Indices result;
        if (PySlice_GetIndicesEx(
            this->ptr(),
            size,
            &result.start,
            &result.stop,
            &result.step,
            &result.length
        )) {
            Exception::from_python();
        }
        return result;
    }

};


template <
    std::convertible_to<Object> Start,
    std::convertible_to<Object> Stop,
    std::convertible_to<Object> Step
>
struct __explicit_init__<Slice, Start, Stop, Step>           : Returns<Slice> {
    static auto operator()(const Object& start, const Object& stop, const Object& step) {
        PyObject* result = PySlice_New(
            start.ptr(),
            stop.ptr(),
            step.ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <
    std::convertible_to<Object> Start,
    std::convertible_to<Object> Stop
>
struct __explicit_init__<Slice, Start, Stop>                 : Returns<Slice> {
    static auto operator()(const Object& start, const Object& stop) {
        PyObject* result = PySlice_New(
            start.ptr(),
            stop.ptr(),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <std::convertible_to<Object> Stop>
struct __explicit_init__<Slice, Stop>                        : Returns<Slice> {
    static auto operator()(const Object& stop) {
        PyObject* result = PySlice_New(
            nullptr,
            stop.ptr(),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <>
struct __init__<Slice>                                      : Returns<Slice> {
    static auto operator()() {
        PyObject* result = PySlice_New(nullptr, nullptr, nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


//////////////////////
////    MODULE    ////
//////////////////////


template <typename T>
struct __issubclass__<T, Module>                            : Returns<bool> {
    static constexpr bool operator()(const T& obj) { return operator()(); }
    static consteval bool operator()() { return impl::module_like<T>; }
};


template <typename T>
struct __isinstance__<T, Module>                            : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Module>();
        } else if constexpr (issubclass<T, Module>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyModule_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


/* Represents an imported Python module in C++. */
class Module : public Object {
    using Base = Object;
    using Self = Module;

public:
    static const Type type;

    Module(Handle h, borrowed_t t) : Base(h, t) {}
    Module(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Module, __init__<Module, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Module, std::remove_cvref_t<Args>...>::enable
        )
    Module(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Module, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Module, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Module, __explicit_init__<Module, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Module, std::remove_cvref_t<Args>...>::enable
        )
    explicit Module(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Module, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    // TODO: there may be no need to have a special case for overloading, either.  This
    // would just be handled automatically by the modular descriptor objects.  If the
    // same method is defined multiple times, the first conflict would convert the
    // descriptor into an overload set, and subsequent conflicts would add to that set.
    // They would be resolved using a trie rather than a linear search.

    // TODO: check if the function is overwriting an existing attribute?

    template <typename Func, typename... Defaults>
    Module& def(const char* name, const char* doc, Func&& body, Defaults&&... defaults) {
        Function f(
            (name == nullptr) ? "" : name,
            (doc == nullptr) ? "" : doc,
            std::forward<Func>(body),
            std::forward<Defaults>(defaults)...
        );
        if (PyModule_AddObjectRef(this->ptr(), name, f.ptr())) {
            Exception::from_python();
        }
        return *this;
    }

    /* Equivalent to pybind11::module_::def_submodule(). */
    Module def_submodule(const char* name, const char* doc = nullptr) {
        const char* this_name = PyModule_GetName(m_ptr);
        if (this_name == nullptr) {
            Exception::from_python();
        }
        std::string full_name = std::string(this_name) + '.' + name;
        Handle submodule = PyImport_AddModule(full_name.c_str());
        if (!submodule) {
            Exception::from_python();
        }
        Module result = reinterpret_borrow<Module>(submodule);
        try {
            if (doc && pybind11::options::show_user_defined_docstrings()) {
                result.template attr<"__doc__">() = pybind11::str(doc);
            }
            pybind11::setattr(*this, name, result);
            return result;
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    void reload() {
        PyObject *obj = PyImport_ReloadModule(this->ptr());
        if (obj == nullptr) {
            Exception::from_python();
        }
        *this = reinterpret_steal<Module>(obj);
    }

};


/* Equivalent to Python `import module`.  Only recognizes absolute imports. */
template <StaticStr name>
Module import() {
    PyObject* obj = PyImport_Import(impl::TemplateString<name>::ptr);
    if (obj == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Module>(obj);
}


/* A replacement for PYBIND11_MODULE that reinterprets the resulting module as a
py::Module object.  The contents are taken directly from PYBIND11_MODULE, except that
we exchange their module type for our own. */
#define BERTRAND_MODULE(name, variable) \
    static ::pybind11::module_::module_def PYBIND11_CONCAT(pybind11_module_def_, name)            \
        PYBIND11_MAYBE_UNUSED;                                                                    \
    PYBIND11_MAYBE_UNUSED                                                                         \
    static void PYBIND11_CONCAT(pybind11_init_, name)(::bertrand::py::Module&);                   \
    PYBIND11_PLUGIN_IMPL(name) {                                                                  \
        PYBIND11_CHECK_PYTHON_VERSION                                                             \
        PYBIND11_ENSURE_INTERNALS_READY                                                           \
        ::bertrand::py::Module m = ::pybind11::module_::create_extension_module(                  \
            PYBIND11_TOSTRING(name), nullptr, &PYBIND11_CONCAT(pybind11_module_def_, name));      \
        try {                                                                                     \
            PYBIND11_CONCAT(pybind11_init_, name)(m);                                             \
            return m.ptr();                                                                       \
        }                                                                                         \
        PYBIND11_CATCH_INIT_EXCEPTIONS                                                            \
    }                                                                                             \
    void PYBIND11_CONCAT(pybind11_init_, name)(::bertrand::py::Module& variable)


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <typename Return, typename Self, typename Key>
auto ops::getitem<Return, Self, Key>::operator()(const Self& self, auto&& key) {
    return impl::Item<Self, Key>(self, std::forward<decltype(key)>(key));
}


template <typename Self> requires (__getitem__<Self, Slice>::enable)
auto Object::operator[](
    this const Self& self,
    const std::initializer_list<impl::SliceInitializer>& slice
) {
    using Return = typename __getitem__<Self, Slice>::type;
    return ops::getitem<Return, Self, Slice>{}(self, Slice(slice));
}


template <typename Return, typename Self>
auto ops::begin<Return, Self>::operator()(const Self& self) {
    PyObject* iter = PyObject_GetIter(self.ptr());
    if (iter == nullptr) {
        Exception::from_python();
    }
    return impl::Iterator<impl::GenericIter<Return>>(reinterpret_steal<Object>(iter));
}


template <typename Return, typename Self>
auto ops::end<Return, Self>::operator()(const Self& self) {
    return impl::Iterator<impl::GenericIter<Return>>();
}


template <typename Return, typename Self>
auto ops::rbegin<Return, Self>::operator()(const Self& self) {
    return impl::Iterator<impl::GenericIter<Return>>(
        impl::call_method<"__reversed__">(self)
    );
}


template <typename Return, typename Self>
auto ops::rend<Return, Self>::operator()(const Self& self) {
    return impl::Iterator<impl::GenericIter<Return>>();
}


template <typename Obj, typename T>
template <typename Self> requires (__getitem__<Self, Slice>::enable)
auto impl::Proxy<Obj, T>::operator[](
    const std::initializer_list<impl::SliceInitializer>& slice
) const {
    return get_value()[slice];
}


}  // namespace py
}  // namespace bertrand


////////////////////////////
////    TYPE CASTERS    ////
////////////////////////////


namespace pybind11 {
namespace detail {


template <std::derived_from<bertrand::py::Object> T>
struct type_caster<T> {
    PYBIND11_TYPE_CASTER(T, const_name("Object"));

    /* Convert Python object to a C++ py::Object. */
    bool load(handle src, bool convert) {
        if (!convert) {
            return false;
        }
        value = bertrand::py::reinterpret_borrow<bertrand::py::Object>(src);
        return true;
    }

    /* Convert a C++ Object into its wrapped object. */
    static handle cast(const T& src, return_value_policy policy, handle parent) {
        return Py_XNewRef(src.ptr());
    }

};


template <bertrand::py::impl::proxy_like T>
struct type_caster<T> {
    PYBIND11_TYPE_CASTER(T, const_name("Proxy"));

    /* Convert Python object to a C++ accessor proxy. */
    bool load(handle src, bool convert) {
        return false;
    }

    /* Convert a C++ Proxy into its wrapped object. */
    static handle cast(const T& src, return_value_policy policy, handle parent) {
        return Py_XNewRef(src.value().ptr());
    }

};


}  // namespace detail
}  // namespace pybind11


#endif // BERTRAND_PYTHON_COMMON_H
