#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_INCLUDED

#include "common/declarations.h"
#include "common/concepts.h"
#include "common/exceptions.h"
#include "common/operators.h"
#include "common/object.h"
#include "common/proxies.h"
#include "common/iterators.h"


namespace bertrand {
namespace py {


/* Pybind11's wrapper classes cover most of the Python standard library, but not all of
 * it, and not with the same syntax as normal Python.  They're also all given in
 * lowercase C++ style, which can cause ambiguities with native C++ types (e.g.
 * pybind11::int_) and non-member functions of the same name.  As such, bertrand
 * provides its own set of wrappers that extend the pybind11 types to the whole CPython
 * API.  These wrappers are designed to be used with nearly identical semantics to the
 * Python types they represent, making them more self-documenting and easier to use
 * from C++.  For questions, refer to the Python documentation first and then the
 * source code for the types themselves, which are provided in named header files
 * within this directory.
 *
 * The final syntax is very similar to standard pybind11.  For example, the following
 * pybind11 code:
 *
 *    py::list foo = py::cast(std::vector<int>{1, 2, 3});
 *    py::int_ bar = foo.attr("pop")();
 *
 * Would be written as:
 *
 *    py::List foo = {1, 2, 3};
 *    py::Int bar = foo.pop();
 *
 * Which closely mimics Python:
 *
 *    foo = [1, 2, 3]
 *    bar = foo.pop()
 *
 * Note that the initializer list syntax is standardized for all container types, as
 * well as any function/method that expects a sequence.  This gives a direct equivalent
 * to Python's tuple, list, set, and dict literals, which can be expressed as:
 *
 *     py::Tuple{1, "a", true};                     // (1, "a", True)
 *     py::List{1, "a", true};                      // [1, 2, 3]
 *     py::Set{1, "a", true};                       // {1, 2, 3}
 *     py::Dict{{1, 3.0}, {"a", 2}, {true, "x"}};   // {1: 3.0, "a": 2, True: "x"}
 *
 * Note also that these initializer lists can contain any type, including mixed types.
 *
 * Built-in Python types:
 *    https://docs.python.org/3/library/stdtypes.html
 */


///////////////////////////////
////    PRIMITIVE TYPES    ////
///////////////////////////////


template <>
struct __hash__<Handle>                                 : Returns<size_t> {};
template <>
struct __hash__<Capsule>                                : Returns<size_t> {};
template <>
struct __hash__<WeakRef>                                : Returns<size_t> {};


template <>
struct __hash__<NoneType>                                   : Returns<size_t> {};


/* Object subclass that represents Python's global None singleton. */
class NoneType : public Object {
    using Base = Object;

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, NoneType, impl::none_like, Py_IsNone)
    BERTRAND_OBJECT_OPERATORS(NoneType)

    /* Default constructor.  Initializes to Python's global None singleton. */
    NoneType() : Base(Py_None, borrowed_t{}) {}

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    NoneType(T&& other) : Base(std::forward<T>(other)) {}

    /* Implicitly convert to pybind11::none. */
    operator pybind11::none() const {
        return reinterpret_borrow<pybind11::none>(m_ptr);
    }

};


template <>
struct __hash__<NotImplementedType>                         : Returns<size_t> {};


/* Object subclass that represents Python's global NotImplemented singleton. */
class NotImplementedType : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool comptime_check = std::is_base_of_v<NotImplementedType, T>;

    inline static int runtime_check(PyObject* obj) {
        int result = PyObject_IsInstance(
            obj,
            (PyObject*) Py_TYPE(Py_NotImplemented)
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, NotImplementedType, comptime_check, runtime_check)
    BERTRAND_OBJECT_OPERATORS(NotImplementedType)

    /* Default constructor.  Initializes to Python's global NotImplemented singleton. */
    NotImplementedType() : Base(Py_NotImplemented, borrowed_t{}) {}

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    NotImplementedType(T&& other) : Base(std::forward<T>(other)) {}

};


template <>
struct __hash__<EllipsisType>                               : Returns<size_t> {};


/* Object subclass representing Python's global Ellipsis singleton. */
class EllipsisType : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool comptime_check = std::is_base_of_v<EllipsisType, T>;

    inline static int runtime_check(PyObject* obj) {
        int result = PyObject_IsInstance(
            obj,
            (PyObject*) Py_TYPE(Py_Ellipsis)
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, EllipsisType, comptime_check, runtime_check)
    BERTRAND_OBJECT_OPERATORS(EllipsisType)

    /* Default constructor.  Initializes to Python's global Ellipsis singleton. */
    EllipsisType() : Base(Py_Ellipsis, borrowed_t{}) {}

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    EllipsisType(T&& other) : Base(std::forward<T>(other)) {}

    /* Implicitly convert to pybind11::ellipsis. */
    operator pybind11::ellipsis() const {
        return reinterpret_borrow<pybind11::ellipsis>(m_ptr);
    }

};


/* Singletons for immortal Python objects. */
static const NoneType None;
static const EllipsisType Ellipsis;
static const NotImplementedType NotImplemented;


namespace impl {

    /* A simple struct that converts a generic C++ object into a Python equivalent in
    its constructor.  This is used in conjunction with std::initializer_list to parse
    mixed-type lists in a type-safe manner. */
    struct Initializer {
        Object first;
        template <typename T> requires (!std::is_same_v<std::decay_t<T>, Handle>)
        Initializer(T&& value) : first(std::forward<T>(value)) {}
    };

    /* An Initializer that explicitly requires a string argument. */
    struct StringInitializer : Initializer {
        template <typename T> requires (impl::str_like<std::decay_t<T>>)
        StringInitializer(T&& value) : Initializer(std::forward<T>(value)) {}
    };

    /* An initializer that explicitly requires an integer or None. */
    struct SliceInitializer : Initializer {
        template <typename T>
            requires (
                impl::int_like<std::decay_t<T>> ||
                std::is_same_v<std::decay_t<T>, std::nullopt_t> ||
                std::is_same_v<std::decay_t<T>, NoneType>
            )
        SliceInitializer(T&& value) : Initializer(std::forward<T>(value)) {}
    };

    /* An Initializer that converts its argument to a python object and asserts that it
    is hashable, for static analysis. */
    struct HashInitializer : Initializer {
        template <typename K>
            requires (
                !std::is_same_v<std::decay_t<K>, Handle> &&
                impl::is_hashable<std::decay_t<K>>
            )
        HashInitializer(K&& key) : Initializer(std::forward<K>(key)) {}
    };

    /* A hashed Initializer that also stores a second item for dict-like access. */
    struct DictInitializer : Initializer {
        Object second;
        template <typename K, typename V>
            requires (
                !std::is_same_v<std::decay_t<K>, Handle> &&
                !std::is_same_v<std::decay_t<V>, Handle> &&
                impl::is_hashable<std::decay_t<K>>
            )
        DictInitializer(K&& key, V&& value) :
            Initializer(std::forward<K>(key)), second(std::forward<V>(value))
        {}
    };

    template <typename T>
    constexpr bool is_initializer = std::is_base_of_v<Initializer, T>;

}


template <std::derived_from<Slice> T>
struct __getattr__<T, "indices">                            : Returns<Function> {};
template <std::derived_from<Slice> T>
struct __getattr__<T, "start">                              : Returns<Object> {};
template <std::derived_from<Slice> T>
struct __getattr__<T, "stop">                               : Returns<Object> {};
template <std::derived_from<Slice> T>
struct __getattr__<T, "step">                               : Returns<Object> {};

template <>
struct __lt__<Slice, Object>                                : Returns<bool> {};
template <impl::slice_like T>
struct __lt__<Slice, T>                                     : Returns<bool> {};
template <>
struct __le__<Slice, Object>                                : Returns<bool> {};
template <impl::slice_like T>
struct __le__<Slice, T>                                     : Returns<bool> {};
template <>
struct __ge__<Slice, Object>                                : Returns<bool> {};
template <impl::slice_like T>
struct __ge__<Slice, T>                                     : Returns<bool> {};
template <>
struct __gt__<Slice, Object>                                : Returns<bool> {};
template <impl::slice_like T>
struct __gt__<Slice, T>                                     : Returns<bool> {};


/* Wrapper around pybind11::slice that allows it to be instantiated with non-integer
inputs in order to represent denormalized slices at the Python level, and provides more
pythonic access to its members. */
class Slice : public Object {
    using Base = Object;

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Slice, impl::slice_like, PySlice_Check)
    BERTRAND_OBJECT_OPERATORS(Slice)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to all Nones. */
    Slice() : Base(PySlice_New(nullptr, nullptr, nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Slice(T&& other) : Base(std::forward<T>(other)) {}

    /* Initializer list constructor. */
    Slice(std::initializer_list<impl::SliceInitializer> indices) {
        if (indices.size() > 3) {
            throw ValueError("slices must be of the form {[start[, stop[, step]]]}");
        }
        size_t i = 0;
        std::array<Object, 3> params {None, None, None};
        for (const impl::SliceInitializer& item : indices) {
            params[i++] = item.first;
        }
        m_ptr = PySlice_New(params[0].ptr(), params[1].ptr(), params[2].ptr());
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly construct a slice from a (possibly denormalized) stop object. */
    template <typename Stop>
    explicit Slice(const Stop& stop) {
        m_ptr = PySlice_New(nullptr, detail::object_or_cast(stop).ptr(), nullptr);
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly construct a slice from (possibly denormalized) start and stop
    objects. */
    template <typename Start, typename Stop>
    explicit Slice(const Start& start, const Stop& stop) {
        m_ptr = PySlice_New(
            detail::object_or_cast(start).ptr(),
            detail::object_or_cast(stop).ptr(),
            nullptr
        );
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly construct a slice from (possibly denormalized) start, stop, and step
    objects. */
    template <typename Start, typename Stop, typename Step>
    explicit Slice(const Start& start, const Stop& stop, const Step& step) {
        m_ptr = PySlice_New(
            detail::object_or_cast(start).ptr(),
            detail::object_or_cast(stop).ptr(),
            detail::object_or_cast(step).ptr()
        );
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert to pybind11::slice. */
    operator pybind11::slice() const {
        return reinterpret_borrow<pybind11::slice>(m_ptr);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the start object of the slice.  Note that this might not be an integer. */
    inline Object start() const {
        return attr<"start">();
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    inline Object stop() const {
        return attr<"stop">();
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    inline Object step() const {
        return attr<"step">();
    }

    /* Data struct containing normalized indices obtained from a py::Slice object. */
    struct Indices {
        Py_ssize_t start;
        Py_ssize_t stop;
        Py_ssize_t step;
        Py_ssize_t length;
    };

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
    inline Indices indices(size_t size) const {
        Py_ssize_t start, stop, step, length = 0;
        if (PySlice_GetIndicesEx(
            this->ptr(),
            static_cast<Py_ssize_t>(size),
            &start,
            &stop,
            &step,
            &length
        )) {
            Exception::from_python();
        }
        return {start, stop, step, length};
    }

};


template <>
struct __hash__<Module>                                     : Returns<size_t> {};
template <StaticStr name> requires (!impl::getattr_helper<name>::enable)
struct __getattr__<Module, name>                            : Returns<Object> {};
template <StaticStr name, typename Value> requires (!impl::setattr_helper<name>::enable)
struct __setattr__<Module, name, Value>                     : Returns<void> {};
template <StaticStr name> requires (!impl::delattr_helper<name>::enable)
struct __delattr__<Module, name>                            : Returns<void> {};


/* Object subclass that represents an imported Python module. */
class Module : public Object {
    using Base = Object;

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Module, impl::module_like, PyModule_Check)
    BERTRAND_OBJECT_OPERATORS(Module)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default module constructor deleted for clarity. */
    Module() = delete;

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Module(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly create a new module object from a statically-allocated (but
    uninitialized) PyModuleDef struct. */
    explicit Module(const char* name, const char* doc, PyModuleDef* def) {
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

    /* Implicitly convert to pybind11::module_. */
    operator pybind11::module_() const {
        return reinterpret_borrow<pybind11::module_>(m_ptr);
    }

    /* Equivalent to pybind11::module_::def(). */
    template <typename Func, typename... Extra>
    Module& def(const char* name_, Func&& f, const Extra&... extra) {
        try {
            pybind11::cpp_function func(
                std::forward<Func>(f),
                pybind11::name(name_),
                pybind11::scope(*this),
                pybind11::sibling(
                    pybind11::getattr(*this, name_, None)
                ),
                extra...
            );
            // NB: allow overwriting here because cpp_function sets up a chain with the
            // intention of overwriting (and has already checked internally that it isn't
            // overwriting non-functions).
            add_object(name_, func, true /* overwrite */);
            return *this;
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    /* Equivalent to pybind11::module_::def_submodule(). */
    inline Module def_submodule(const char* name, const char* doc = nullptr);

    /* Reload the module or throw an error. */
    inline void reload() {
        PyObject *obj = PyImport_ReloadModule(this->ptr());
        if (obj == nullptr) {
            Exception::from_python();
        }
        *this = reinterpret_steal<Module>(obj);
    }

    /* Equivalent to pybind11::module_::add_object(). */
    PYBIND11_NOINLINE void add_object(
        const char* name,
        Handle obj,
        bool overwrite = false
    ) {
        try {
            if (!overwrite && pybind11::hasattr(*this, name)) {
                pybind11::pybind11_fail(
                    "Error during initialization: multiple incompatible "
                    "definitions with name \"" + std::string(name) + "\""
                );
            }
            PyModule_AddObjectRef(ptr(), name, obj.ptr());
        } catch (...) {
            Exception::from_pybind11();
        }
    }

};


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <bertrand::StaticStr name>
inline impl::Attr<Object, name> Object::attr() const {
    return impl::Attr<Object, name>(*this);
}


template <typename Return, typename T, typename Key>
inline impl::Item<T, std::decay_t<Key>> Object::operator_getitem(const T& obj, Key&& key) {
    return impl::Item<T, std::decay_t<Key>>(obj, std::forward<Key>(key));
}


template <typename Return, typename T>
inline impl::Item<T, Slice> Object::operator_getitem(
    const T& obj,
    std::initializer_list<impl::SliceInitializer> slice
) {
    return impl::Item<T, Slice>(obj, Slice(slice));
}


template <typename Return, typename T>
inline impl::Iterator<impl::GenericIter<Return>> Object::operator_begin(const T& obj) {
    PyObject* iter = PyObject_GetIter(obj.ptr());
    if (iter == nullptr) {
        Exception::from_python();
    }
    return impl::Iterator<impl::GenericIter<Return>>(reinterpret_steal<Object>(iter));
}


template <typename Return, typename T>
inline impl::Iterator<impl::GenericIter<Return>> Object::operator_end(const T& obj) {
    return impl::Iterator<impl::GenericIter<Return>>();
}


template <typename Return, typename T>
inline impl::Iterator<impl::GenericIter<Return>> Object::operator_rbegin(const T& obj) {
    return impl::Iterator<impl::GenericIter<Return>>(obj.template attr<"__reversed__">()());
}


template <typename Return, typename T>
inline impl::Iterator<impl::GenericIter<Return>> Object::operator_rend(const T& obj) {
    return impl::Iterator<impl::GenericIter<Return>>();
}


template <typename Obj, typename Wrapped>
template <typename T> requires (__getitem__<T, Slice>::enable)
inline auto impl::Proxy<Obj, Wrapped>::operator[](
    std::initializer_list<impl::SliceInitializer> slice
) const {
    return get_value()[slice];
}


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* Equivalent to Python `import module`.  Only recognizes absolute imports. */
template <StaticStr name>
inline Module import() {
    static const pybind11::str lookup = static_cast<const char*>(name);
    PyObject *obj = PyImport_Import(lookup.ptr());
    if (obj == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Module>(obj);
}


/* Equivalent to Python `print(args...)`. */
template <typename... Args>
inline void print(Args&&... args) {
    try {
        pybind11::print(std::forward<Args>(args)...);
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `repr(obj)`, but returns a std::string and attempts to
represent C++ types using std::to_string or the stream insertion operator (<<).  If all
else fails, falls back to typeid(obj).name(). */
template <typename T>
inline std::string repr(const T& obj) {
    if constexpr (impl::has_stream_insertion<T>) {
        std::ostringstream stream;
        stream << obj;
        return stream.str();

    } else if constexpr (impl::has_to_string<T>) {
        return std::to_string(obj);

    } else {
        try {
            return pybind11::repr(obj).template cast<std::string>();
        } catch (...) {
            return typeid(obj).name();
        }
    }
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
    PYBIND11_TYPE_CASTER(T, _("Object"));

    /* Convert Python object to a C++ Object. */
    inline bool load(handle src, bool convert) {
        if (!convert) {
            return false;
        }
        value = bertrand::py::reinterpret_borrow<bertrand::py::Object>(src);
        return true;
    }

    /* Convert a C++ Object into its wrapped object. */
    inline static handle cast(const T& src, return_value_policy policy, handle parent) {
        return Py_XNewRef(src.ptr());
    }

};


template <bertrand::py::impl::proxy_like T>
struct type_caster<T> {
    PYBIND11_TYPE_CASTER(T, const_name("Proxy"));

    /* Convert Python object to a C++ Proxy. */
    inline bool load(handle src, bool convert) {
        return false;
    }

    /* Convert a C++ Proxy into its wrapped object. */
    inline static handle cast(const T& src, return_value_policy policy, handle parent) {
        return Py_XNewRef(src.value().ptr());
    }

};

}  // namespace detail
}  // namespace pybind11


//////////////////////////////
////    STL EXTENSIONS    ////
//////////////////////////////


/* Bertrand overloads std::hash<> for all Python objects hashable so that they can be
 * used in STL containers like std::unordered_map and std::unordered_set.  This is
 * accomplished by overloading std::hash<> for the relevant types, which also allows
 * us to promote hash-not-implemented errors to compile-time.
 */


namespace std {

    template <typename T> requires (bertrand::py::__hash__<T>::enable)
    struct hash<T> {
        static_assert(
            std::is_same_v<typename bertrand::py::__hash__<T>::Return, size_t>,
            "std::hash<> must return size_t for compatibility with other C++ types.  "
            "Check your specialization of __hash__ for this type and ensure the "
            "Return type is set to size_t."
        );

        inline size_t operator()(const T& obj) const {
            try {
                return pybind11::hash(obj);
            } catch (...) {
                bertrand::py::Exception::from_pybind11();
            }
        }
    };

};


#define BERTRAND_STD_EQUAL_TO(cls)                                                      \
namespace std {                                                                         \
    template <>                                                                         \
    struct equal_to<cls> {                                                              \
        bool operator()(const cls& a, const cls& b) const {                             \
            return a.equal(b);                                                          \
        }                                                                               \
    };                                                                                  \
}                                                                                       \


BERTRAND_STD_EQUAL_TO(bertrand::py::Handle)
BERTRAND_STD_EQUAL_TO(bertrand::py::WeakRef)
BERTRAND_STD_EQUAL_TO(bertrand::py::Capsule)


#undef BERTRAND_STD_EQUAL_TO
#endif // BERTRAND_PYTHON_COMMON_H
