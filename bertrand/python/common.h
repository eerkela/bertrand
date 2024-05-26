#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_INCLUDED

#include "common/declarations.h"
#include "common/except.h"
#include "common/ops.h"
#include "common/object.h"
#include "common/func.h"
#include "common/accessor.h"
#include "common/iter.h"
#include "common/control.h"


namespace bertrand {
namespace py {


////////////////////
////    NONE    ////
////////////////////


/* Represents the type of Python's `None` singleton in C++. */
class NoneType : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return impl::none_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && Py_IsNone(obj.ptr());
        } else {
            return false;
        }
    }

    NoneType(Handle h, const borrowed_t& t) : Base(h, t) {}
    NoneType(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    NoneType(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    NoneType(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<NoneType>(accessor).release(), stolen_t{})
    {}

    NoneType() : Base(Py_None, borrowed_t{}) {}
    NoneType(std::nullptr_t) : Base(Py_None, borrowed_t{}) {}
    NoneType(std::nullopt_t) : Base(Py_None, borrowed_t{}) {}

};


//////////////////////////////
////    NOTIMPLEMENTED    ////
//////////////////////////////


/* Represents the type of Python's `NotImplemented` singleton in C++. */
class NotImplementedType : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return std::derived_from<T, NotImplementedType>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();

        } else if constexpr (check<T>()) {
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

    NotImplementedType(Handle h, const borrowed_t& t) : Base(h, t) {}
    NotImplementedType(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    NotImplementedType(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    NotImplementedType(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<NotImplementedType>(accessor).release(), stolen_t{})
    {}

    NotImplementedType() : Base(Py_NotImplemented, borrowed_t{}) {}
};


////////////////////////
////    ELLIPSIS    ////
////////////////////////


/* Represents the type of Python's `Ellipsis` singleton in C++. */
class EllipsisType : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return std::derived_from<T, EllipsisType>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();

        } else if constexpr (check<T>()) {
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

    EllipsisType(Handle h, const borrowed_t& t) : Base(h, t) {}
    EllipsisType(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    EllipsisType(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    EllipsisType(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<EllipsisType>(accessor).release(), stolen_t{})
    {}

    EllipsisType() : Base(Py_Ellipsis, borrowed_t{}) {}
};


static const NoneType None;
static const EllipsisType Ellipsis;
static const NotImplementedType NotImplemented;


/////////////////////
////    SLICE    ////
/////////////////////


namespace impl {

    // TODO: SliceInitializer should be a std::variant of py::Int, py::None?
    // -> Does this work?

    /* An initializer that explicitly requires an integer or None. */
    struct SliceInitializer {
        Object value;
        template <typename T>
            requires (
                impl::int_like<std::decay_t<T>> ||
                std::same_as<std::decay_t<T>, std::nullopt_t> ||
                std::same_as<std::decay_t<T>, NoneType>
            )
        SliceInitializer(T&& value) : value(std::forward<T>(value)) {}
    };

}


template <std::derived_from<Slice> Self>
struct __getattr__<Self, "indices">                             : Returns<Function<
    Tuple<Int>(Arg<"length", const Int&>)
>> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "start">                               : Returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "stop">                                : Returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "step">                                : Returns<Object> {};


/* Represents a statically-typed Python `slice` object in C++.  Note that the start,
stop, and step values do not strictly need to be integers. */
class Slice : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return impl::slice_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PySlice_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Slice(Handle h, const borrowed_t& t) : Base(h, t) {}
    Slice(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Slice(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Slice(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Slice>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to all Nones. */
    Slice() : Base(
        PySlice_New(nullptr, nullptr, nullptr),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly construct a slice from a (possibly denormalized) stop object. */
    explicit Slice(const Object& stop) : Base(
        PySlice_New(nullptr, stop.ptr(), nullptr),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly construct a slice from (possibly denormalized) start and stop
    objects. */
    explicit Slice(const Object& start, const Object& stop) : Base(
        PySlice_New(start.ptr(), stop.ptr(), nullptr),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly construct a slice from (possibly denormalized) start, stop, and step
    objects. */
    explicit Slice(const Object& start, const Object& stop, const Object& step) : Base(
        PySlice_New(start.ptr(), stop.ptr(), step.ptr()),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

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

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the start object of the slice.  Note that this might not be an integer. */
    auto start() const {
        return attr<"start">().value();
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    auto stop() const {
        return attr<"stop">().value();
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    auto step() const {
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
    auto indices(size_t size) const {
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


//////////////////////
////    MODULE    ////
//////////////////////


/* Represents an imported Python module in C++. */
class Module : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return impl::module_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyModule_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Module(Handle h, const borrowed_t& t) : Base(h, t) {}
    Module(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Module(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Module(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Module>(accessor).release(), stolen_t{})
    {}

    /* Default module constructor deleted for clarity. */
    Module() = delete;

    /* Explicitly create a new module object from a statically-allocated (but
    uninitialized) PyModuleDef struct. */
    explicit Module(const char* name, const char* doc, PyModuleDef* def) :
        Base(nullptr, stolen_t{})
    {
        def = new (def) PyModuleDef{
            /* m_base */ PyModuleDef_HEAD_INIT,
            /* m_name */ name,
            /* m_doc */ pybind11::options::show_user_defined_docstrings() ? doc : nullptr,
            /* m_size */ -1,
            /* m_methods */ nullptr,
            /* m_slots */ nullptr,
            /* m_traverse */ nullptr,
            /* m_clear */ nullptr,
            /* m_free */ nullptr
        };
        m_ptr = PyModule_Create(def);
        try {
            if (m_ptr == nullptr) {
                if (PyErr_Occurred()) {
                    Exception::from_python();
                }
                pybind11::pybind11_fail(
                    "Internal error in pybind11::module_::create_extension_module()"
                );
            }
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    //////////////////////////////////
    ////    PYBIND11 INTERFACE    ////
    //////////////////////////////////

    // TODO: this can bind a py::Function instead of a pybind11::cpp_function
    // -> All of the relevant information can be inferred directly from the function
    // signature using inline annotations.  The docstring can be given immediately after
    // the function name, like so:

    //  m.def(
    //      "add",
    //      R"(
    //          Adds two numbers.
    //          
    //          Parameters
    //          ----------
    //          a : int
    //              The first number.
    //          b : int
    //              The second number.
    //
    //          Returns
    //          -------
    //          int
    //              The sum of the two numbers.
    //      )",
    //      [](py::Arg<"a", int> a, py::Arg<"b", int>::opt b) {
    //          return a.value + b.value;
    //      },
    //      py::arg_<"b"> = 2
    //  );

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

    /* Reload the module or throw an error. */
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
py::Module object. */
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
    using Return = typename __getitem__<Self, Slice>::Return;
    return ops::getitem<Return, Self, Slice>::operator()(
        self,
        Slice(slice)
    );
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


template <typename Obj, typename Wrapped>
template <typename Self> requires (__getitem__<Self, Slice>::enable)
auto impl::Proxy<Obj, Wrapped>::operator[](
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
