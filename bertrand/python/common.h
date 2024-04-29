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


template <>
struct __hash__<Handle>                                         : Returns<size_t> {};
template <>
struct __hash__<Capsule>                                        : Returns<size_t> {};
template <>
struct __hash__<WeakRef>                                        : Returns<size_t> {};


////////////////////
////    NONE    ////
////////////////////


template <>
struct __hash__<NoneType>                                       : Returns<size_t> {};


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
        if constexpr (impl::python_like<T>) {
            return obj.ptr() != nullptr && Py_IsNone(obj.ptr());
        } else {
            return check<T>();
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
};


//////////////////////////////
////    NOTIMPLEMENTED    ////
//////////////////////////////


template <>
struct __hash__<NotImplementedType>                             : Returns<size_t> {};


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
        if constexpr (impl::python_like<T>) {
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
            return check<T>();
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


template <>
struct __hash__<EllipsisType>                                   : Returns<size_t> {};


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
        if constexpr (impl::python_like<T>) {
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
            return check<T>();
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


namespace impl {

    // TODO: initializers have to be nested classes in order to properly account for
    // template types?
    // -> Actually, as long as I'm careful about how I write the constructor, then it
    // does actually work.  I just need to make sure to forward to the templated type
    // using a secondarily templated constructor, rather than just using the top-level
    // template type.
    // -> In fact, I might be able to do away with these entirely if I forward the
    // template type to the initializer_list directly.  If I static assert that the
    // element type is hashable and derived from Object, then this should achieve the
    // same effect.  DictInitializer just uses std::pair, so we don't need any changes.

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

    /* An Initializer that converts its argument to a python object and asserts that it
    is hashable, for static analysis. */
    struct HashInitializer {
        Object value;
        template <typename K>
            requires (
                !std::same_as<std::decay_t<K>, Handle> &&
                impl::is_hashable<std::decay_t<K>>
            )
        HashInitializer(K&& key) : value(std::forward<K>(key)) {}
    };

    /* A hashed Initializer that also stores a second item for dict-like access. */
    struct DictInitializer {
        Object key;
        Object value;
        template <typename K, typename V>
            requires (
                !std::same_as<std::decay_t<K>, Handle> &&
                !std::same_as<std::decay_t<V>, Handle> &&
                impl::is_hashable<std::decay_t<K>>
            )
        DictInitializer(K&& key, V&& value) :
            key(std::forward<K>(key)), value(std::forward<V>(value))
        {}
    };

}


/////////////////////
////    SLICE    ////
/////////////////////


template <std::derived_from<Slice> Self>
struct __getattr__<Self, "indices">                             : Returns<Function> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "start">                               : Returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "stop">                                : Returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "step">                                : Returns<Object> {};

template <>
struct __lt__<Slice, Object>                                    : Returns<bool> {};
template <impl::slice_like T>
struct __lt__<Slice, T>                                         : Returns<bool> {};
template <>
struct __le__<Slice, Object>                                    : Returns<bool> {};
template <impl::slice_like T>
struct __le__<Slice, T>                                         : Returns<bool> {};
template <>
struct __ge__<Slice, Object>                                    : Returns<bool> {};
template <impl::slice_like T>
struct __ge__<Slice, T>                                         : Returns<bool> {};
template <>
struct __gt__<Slice, Object>                                    : Returns<bool> {};
template <impl::slice_like T>
struct __gt__<Slice, T>                                         : Returns<bool> {};


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
        if constexpr (impl::python_like<T>) {
            return obj.ptr() != nullptr && PySlice_Check(obj.ptr());
        } else {
            return check<T>();
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
    inline auto indices(size_t size) const {
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


template <>
struct __hash__<Module>                                         : Returns<size_t> {};
template <StaticStr name> requires (!impl::getattr_helper<name>::enable)
struct __getattr__<Module, name>                                : Returns<Object> {};
template <StaticStr name, typename Value> requires (!impl::setattr_helper<name>::enable)
struct __setattr__<Module, name, Value>                         : Returns<void> {};
template <StaticStr name> requires (!impl::delattr_helper<name>::enable)
struct __delattr__<Module, name>                                : Returns<void> {};


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
        if constexpr (impl::python_like<T>) {
            return obj.ptr() != nullptr && PyModule_Check(obj.ptr());
        } else {
            return check<T>();
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


template <typename Return, typename Self, typename Key>
auto impl::ops::getitem<Return, Self, Key>::operator()(const Self& self, auto&& key) {
    return impl::Item<Self, Key>(self, std::forward<decltype(key)>(key));
}


template <typename Self> requires (__getitem__<Self, Slice>::enable)
auto Object::operator[](
    this const Self& self,
    const std::initializer_list<impl::SliceInitializer>& slice
) {
    using Return = typename __getitem__<Self, Slice>::Return;
    return impl::ops::getitem<Return, Self, Slice>::operator()(
        self,
        Slice(slice)
    );
}


template <typename Return, typename Self>
auto impl::ops::begin<Return, Self>::operator()(const Self& self) {
    PyObject* iter = PyObject_GetIter(self.ptr());
    if (iter == nullptr) {
        Exception::from_python();
    }
    return impl::Iterator<impl::GenericIter<Return>>(reinterpret_steal<Object>(iter));
}


template <typename Return, typename Self>
auto impl::ops::end<Return, Self>::operator()(const Self& self) {
    return impl::Iterator<impl::GenericIter<Return>>();
}


template <typename Return, typename Self>
auto impl::ops::rbegin<Return, Self>::operator()(const Self& self) {
    return impl::Iterator<impl::GenericIter<Return>>(
        self.template attr<"__reversed__">()()
    );
}


template <typename Return, typename Self>
auto impl::ops::rend<Return, Self>::operator()(const Self& self) {
    return impl::Iterator<impl::GenericIter<Return>>();
}


template <typename Obj, typename Wrapped>
template <typename Self> requires (__getitem__<Self, Slice>::enable)
auto impl::Proxy<Obj, Wrapped>::operator[](
    const std::initializer_list<impl::SliceInitializer>& slice
) const {
    return get_value()[slice];
}


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* Equivalent to Python `import module`.  Only recognizes absolute imports. */
template <StaticStr name>
Module import() {
    static const pybind11::str lookup = static_cast<const char*>(name);
    PyObject *obj = PyImport_Import(lookup.ptr());
    if (obj == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Module>(obj);
}


/* Equivalent to Python `print(args...)`. */
template <typename... Args>
void print(Args&&... args) {
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
std::string repr(const T& obj) {
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
    PYBIND11_TYPE_CASTER(T, const_name("Object"));

    /* Convert Python object to a C++ Object. */
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

    /* Convert Python object to a C++ Proxy. */
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
            std::same_as<typename bertrand::py::__hash__<T>::Return, size_t>,
            "std::hash<> must return size_t for compatibility with other C++ types.  "
            "Check your specialization of __hash__ for this type and ensure the "
            "Return type is set to size_t."
        );

        static size_t operator()(const T& obj) {
            return pybind11::hash(obj);
        }
    };

    #define BERTRAND_STD_EQUAL_TO(cls)                                                  \
        template <>                                                                     \
        struct equal_to<cls> {                                                          \
            static bool operator()(const cls& a, const cls& b) {                        \
                return a.equal(b);                                                      \
            }                                                                           \
        };                                                                              \

    BERTRAND_STD_EQUAL_TO(bertrand::py::Handle)
    BERTRAND_STD_EQUAL_TO(bertrand::py::WeakRef)
    BERTRAND_STD_EQUAL_TO(bertrand::py::Capsule)


    #undef BERTRAND_STD_EQUAL_TO

};


#endif // BERTRAND_PYTHON_COMMON_H
