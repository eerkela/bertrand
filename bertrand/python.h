#ifndef BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_INCLUDED
#define PYBIND11_DETAILED_ERROR_MESSAGES

#include "python/common.h"

#include "python/bool.h"
#include "python/int.h"
#include "python/float.h"
#include "python/complex.h"
#include "python/range.h"
#include "python/list.h"
#include "python/tuple.h"
#include "python/set.h"
#include "python/dict.h"
#include "python/str.h"
#include "python/bytes.h"
#include "python/func.h"
// #include "python/datetime.h"
#include "python/math.h"
#include "python/type.h"


#include "regex.h"


// TODO: the only remaining language feature left to emulate are:
// - context managers (RAII)  - use py::enter() to enter a context (which produces an RAII guard), and py::exit() or destructor to exit
// - decorators (?)


// TODO: numpy types can be implemented as constexpr structs that have an internal
// flyweight cache (which might be difficult wrt allocations).  They would inherit from
// a virtual base class that implements the full type interface.  This would allow me
// to pass polymorphic types by const reference to a base class, and use them to
// select overloads at compile time.  For instance, you could write several algorithms
// that accept different families of type tags, and the compiler would figure it out
// for you, resulting in zero overhead.  This would effectively mirror the Python
// @overload decorator, but using built-in C++ language features and no additional
// syntax.

// The only problem with this is that it seems like the presence of virtual methods
// makes such a tag not directly usable as a template parameter, though it may be used
// through its type directly, which could be piped into an array definition.  This
// would mean that we could statically dispatch on an array type by using C++20
// template constraints.

// I could also implement a using declaration that could resolve types by name
// entirely at compile time.  Something like:

//    using Type = bertrand::resolve<"Int">;  // Bonus points if I can do full compile-time regex matches





/* NOTES ON PERFORMANCE:
 * In general, bertrand should be as fast or faster than the equivalent Python code,
 * owing to the use of static typing, comp time, and optimized CPython API calls.  
 * There are a few things to keep in mind, however:
 *
 *  1.  A null pointer check followed by an isinstance() check is implicitly incurred
 *      whenever a generic py::Object is narrowed to a more specific type, such as
 *      py::Int or py::List.  This is necessary to ensure type safety, and is optimized
 *      for built-in types, but can become a pessimization if done frequently,
 *      especially in tight loops.  If you find yourself doing this, consider either
 *      converting to strict types earlier in the code (which eliminates runtime
 *      overhead and allows the compiler to enforce these checks at compile time) or
 *      keeping all object interactions fully generic to prevent thrashing.  Generally,
 *      the only cases where this can be a problem are when accessing a named attribute
 *      via `attr()`, calling a generic Python function using `()`, indexing into an
 *      untyped container with `[]`, or iterating over such a container in a range-based
 *      loop, all of which return py::Object instances by default.  Note that all of
 *      these can be made type-safe by using a typed container or writing a custom
 *      wrapper class that specializes the `py::impl::__call__`,
 *      `py::impl::__getitem__`, and `py::impl::__iter__` control structs.  Doing so
 *      eliminates the runtime check and promotes it to compile time.
 *  2.  For cases where the exact type of a generic object is known in advance, it is
 *      possible to bypass the runtime check by using `py::reinterpret_borrow<T>(obj)`
 *      or `py::reinterpret_steal<T>(obj.release())`.  These functions are not type
 *      safe, and should be used with caution (especially the latter, which can lead to
 *      memory leaks if used incorrectly).  However, they can be useful when working
 *      with custom types, as in most cases a method's return type and reference count
 *      will be known ahead of time, making the runtime check redundant.  In most other
 *      cases, it is not recommended to use these functions, as they can lead to subtle
 *      bugs and crashes if the assumptions they prove to be false.  Implementers
 *      seeking to write their own types should refer to the built-in types for
 *      examples of how to do this correctly.
 *  3.  There is a small penalty for copying data across the Python/C++ boundary.  This
 *      is generally tolerable (even for lists and other container types), but it can
 *      add up if done frequently.  If you find yourself repeatedly transferring large
 *      amounts of data between Python and C++, you should either reconsider your
 *      design to keep more of your operations within one language or another, or use
 *      the buffer protocol to eliminate the copy.  An easy way to do this is to use
 *      NumPy arrays, which can be accessed directly as C++ arrays without any copies.
 *  4.  Python (at least for now) does not play well with multithreaded code, and
 *      subsequently, neither does bertrand.  If you need to use Python objects in a
 *      multithreaded context, consider offloading the work to C++ and passing the
 *      results back to Python. This unlocks full native parallelism, with SIMD,
 *      OpenMP, and other tools at your disposal.  If you must use Python, first read
 *      the GIL chapter in the Python C API documentation, and then consider using the
 *      `py::gil_scoped_release` guard to release the GIL within a specific context,
 *      and automatically reacquire it using RAII before returning to Python.  This
 *      should only be attempted if you are 100% sure that your Python code is
 *      thread-safe and does not interfere with the GIL in any way.  If there is any
 *      doubt whatsoever, do not do this.
 *  5.  Additionally, Bertrand makes it possible to store arbitrary Python objects with
 *      static duration using the py::Static<> wrapper, which can reduce net
 *      allocations and further improve performance.  This is especially true for
 *      global objects like imported modules and compiled scripts, which can be cached
 *      and reused for the lifetime of the program.
 *
 * Even without these optimizations, bertrand should be quite competitive with native
 * Python code, and should trade blows with it in most cases.  If you find a case where
 * bertrand is significantly slower than Python, please file an issue on the GitHub
 * repository, and we will investigate it as soon as possible.
 */


namespace bertrand {
namespace py {

namespace literals {
    using namespace pybind11::literals;

    inline Code operator ""_python(const char* source, size_t size) {
        return Code::compile(std::string_view(source, size));
    }

}


////////////////////////////
////    TYPE MEMBERS    ////
////////////////////////////


// TODO: Regex and Decimal types?


/* Every Python type has a static `Type` member that gives access to the Python type
object associated with instances of that class. */
inline Type Type::type {};
inline Type Object::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyBaseObject_Type));
inline Type NoneType::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(Py_TYPE(Py_None)));
inline Type NotImplementedType::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(Py_TYPE(Py_NotImplemented)));
inline Type EllipsisType::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyEllipsis_Type));
inline Type Module::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyModule_Type));
inline Type Bool::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyBool_Type));
inline Type Int::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyLong_Type));
inline Type Float::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyFloat_Type));
inline Type Complex::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyComplex_Type));
inline Type Slice::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PySlice_Type));
inline Type Range::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyRange_Type));
inline Type List::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyList_Type));
inline Type Tuple::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyTuple_Type));
inline Type Set::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PySet_Type));
inline Type FrozenSet::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyFrozenSet_Type));
inline Type Dict::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyDict_Type));
inline Type MappingProxy::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyDictProxy_Type));
inline Type KeysView::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyDictKeys_Type));
inline Type ValuesView::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyDictValues_Type));
inline Type ItemsView::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyDictItems_Type));
inline Type Str::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyUnicode_Type));
inline Type Bytes::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyBytes_Type));
inline Type ByteArray::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyByteArray_Type));
inline Type Code::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyCode_Type));
inline Type Frame::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyFrame_Type));
inline Type Function::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyFunction_Type));
inline Type ClassMethod::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyClassMethodDescr_Type));
inline Type StaticMethod::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyStaticMethod_Type));
inline Type Property::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyProperty_Type));
// inline Type Timedelta::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyDelta_Type->ptr());
//     } else {
//         return Type();
//     }
// }();
// inline Type Timezone::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyTZInfo_Type->ptr());
//     } else {
//         return Type();
//     }
// }();
// inline Type Date::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyDate_Type->ptr());
//     } else {
//         return Type();
//     }
// }();
// inline Type Time::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyTime_Type->ptr());
//     } else {
//         return Type();
//     }
// }();
// inline Type Datetime::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyDateTime_Type->ptr());
//     } else {
//         return Type();
//     }
// }();


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


inline Module Module::def_submodule(const char* name, const char* doc) {
    const char* this_name = PyModule_GetName(m_ptr);
    if (this_name == nullptr) {
        Exception::from_python();
    }
    std::string full_name = std::string(this_name) + '.' + name;
    Handle submodule = PyImport_AddModule(full_name.c_str());
    if (!submodule) {
        Exception::from_python();
    }
    auto result = reinterpret_borrow<Module>(submodule);
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


template <typename T> requires (impl::python_like<T> && impl::str_like<T>)
inline Int::Int(const T& str, int base) :
    Base(PyLong_FromUnicodeObject(str.ptr(), base), stolen_t{})
{
    if (m_ptr == nullptr) {
        Exception::from_python();
    }
}


inline Float::Float(const Str& str) : Base(PyFloat_FromString(str.ptr()), stolen_t{}) {
    if (m_ptr == nullptr) {
        Exception::from_python();
    }
}


inline Type::Type(const Str& name, const Tuple& bases, const Dict& dict) {
    m_ptr = PyObject_CallFunctionObjArgs(
        reinterpret_cast<PyObject*>(&PyType_Type),
        name.ptr(),
        bases.ptr(),
        dict.ptr(),
        nullptr
    );
    if (m_ptr == nullptr) {
        Exception::from_python();
    }
}


#if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

    /* Get the type's qualified name. */
    inline Str Type::qualname() const {
        PyObject* result = PyType_GetQualName(self());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

#endif


template <impl::is_iterable T>
inline void List::extend(const T& items) {
    if constexpr (impl::python_like<T>) {
        attr<"extend">()(detail::object_or_cast(items));
    } else {
        for (auto&& item : items) {
            append(std::forward<decltype(item)>(item));
        }
    }
}


template <typename T>
inline void List::remove(const T& value) {
    attr<"remove">()(detail::object_or_cast(value));
}


inline Object List::pop(Py_ssize_t index) {
    return attr<"pop">()(index);
}


inline void List::sort(const Bool& reverse) {
    attr<"sort">()(py::arg("reverse") = reverse);
}


inline void List::sort(const Function& key, const Bool& reverse) {
    attr<"sort">()(py::arg("key") = key, py::arg("reverse") = reverse);
}


template <typename Derived>
template <impl::is_iterable T>
inline bool impl::ISet<Derived>::isdisjoint(const T& other) const {
    if constexpr (impl::python_like<T>) {
        return static_cast<bool>(
            self()->template attr<"isdisjoint">()(other)
        );
    } else {
        for (auto&& item : other) {
            if (contains(std::forward<decltype(item)>(item))) {
                return false;
            }
        }
        return true;
    }
}


template <typename Derived>
template <impl::is_iterable T>
inline bool impl::ISet<Derived>::issuperset(const T& other) const {
    if constexpr (impl::python_like<T>) {
        return static_cast<bool>(
            self()->template attr<"issuperset">()(other)
        );
    } else {
        for (auto&& item : other) {
            if (!contains(std::forward<decltype(item)>(item))) {
                return false;
            }
        }
        return true;
    }
}


template <typename Derived>
template <impl::is_iterable T>
inline bool impl::ISet<Derived>::issubset(const T& other) const {
    return static_cast<bool>(self()->template attr<"issubset">()(
        detail::object_or_cast(other)
    ));
}


template <typename Derived>
inline bool impl::ISet<Derived>::issubset(
    const std::initializer_list<impl::HashInitializer>& other
) const {
    return static_cast<bool>(
        self()->template attr<"issubset">()(Derived(other))
    );
}


template <typename Derived>
template <impl::is_iterable... Args>
inline Derived impl::ISet<Derived>::union_(const Args&... others) const {
    return self()->template attr<"union">()(
        detail::object_or_cast(std::forward<Args>(others))...
    );
}


template <typename Derived>
template <impl::is_iterable... Args>
inline Derived impl::ISet<Derived>::intersection(const Args&... others) const {
    return self()->template attr<"intersection">()(
        detail::object_or_cast(std::forward<Args>(others))...
    );
}


template <typename Derived>
template <impl::is_iterable... Args>
inline Derived impl::ISet<Derived>::difference(const Args&... others) const {
    return self()->template attr<"difference">()(
        detail::object_or_cast(std::forward<Args>(others))...
    );
}


template <typename Derived>
template <impl::is_iterable T>
inline Derived impl::ISet<Derived>::symmetric_difference(const T& other) const {
    return self()->template attr<"symmetric_difference">()(
        detail::object_or_cast(other)
    );
}


template <impl::is_iterable... Args>
inline void Set::update(const Args&... others) {
    attr<"update">()(detail::object_or_cast(std::forward<Args>(others))...);
}


template <impl::is_iterable... Args>
inline void Set::intersection_update(const Args&... others) {
    attr<"intersection_update">()(
        detail::object_or_cast(std::forward<Args>(others))...
    );
}


inline void Set::intersection_update(
    const std::initializer_list<impl::HashInitializer>& other
) {
    attr<"intersection_update">()(Set(other));
}


template <impl::is_iterable... Args>
inline void Set::difference_update(const Args&... others) {
    attr<"difference_update">()(
        detail::object_or_cast(std::forward<Args>(others))...
    );
}


template <impl::is_iterable T>
inline void Set::symmetric_difference_update(const T& other) {
    attr<"symmetric_difference_update">()(detail::object_or_cast(other));
}


template <impl::is_iterable T>
inline bool KeysView::isdisjoint(const T& other) const {
    return static_cast<bool>(attr<"isdisjoint">()(detail::object_or_cast(other)));
}


inline bool KeysView::isdisjoint(
    const std::initializer_list<impl::HashInitializer>& other
) const {
    return static_cast<bool>(attr<"isdisjoint">()(Set(other)));
}


inline Object Dict::popitem() {
    return attr<"popitem">()();
}


inline KeysView::KeysView(const Dict& dict) {
    m_ptr = dict.template attr<"keys">()().release().ptr();
}


inline KeysView Dict::keys() const {
    return reinterpret_steal<KeysView>(attr<"keys">()().release());
}


inline ValuesView::ValuesView(const Dict& dict) {
    m_ptr = dict.template attr<"values">()().release().ptr();
}


inline ValuesView Dict::values() const {
    return reinterpret_steal<ValuesView>(attr<"values">()().release());
}


inline ItemsView::ItemsView(const Dict& dict) {
    m_ptr = dict.template attr<"items">()().release().ptr();
}


inline ItemsView Dict::items() const {
    return reinterpret_steal<ItemsView>(attr<"items">()().release());
}


inline Dict MappingProxy::copy() const {
    return reinterpret_steal<Dict>(attr<"copy">()().release());
}


template <impl::is_hashable K>
inline Object MappingProxy::get(const K& key) const {
    return attr<"get">()(detail::object_or_cast(key), py::None);
}


template <impl::is_hashable K, typename V>
inline Object MappingProxy::get(const K& key, const V& default_value) const {
    return attr<"get">()(
        detail::object_or_cast(key),
        detail::object_or_cast(default_value)
    );
}


inline KeysView MappingProxy::keys() const {
    return reinterpret_steal<KeysView>(attr<"keys">()().release());
}



inline ValuesView MappingProxy::values() const {
    return reinterpret_steal<ValuesView>(attr<"values">()().release());
}


inline ItemsView MappingProxy::items() const {
    return reinterpret_steal<ItemsView>(attr<"items">()().release());
}


inline Str Str::capitalize() const {
    return reinterpret_steal<Str>(attr<"capitalize">()().release());
}


inline Str Str::casefold() const {
    return reinterpret_steal<Str>(attr<"casefold">()().release());
}


inline Str Str::center(const Int& width) const {
    return reinterpret_steal<Str>(attr<"center">()(width).release());
}


inline Str Str::center(const Int& width, const Str& fillchar) const {
    return reinterpret_steal<Str>(attr<"center">()(width, fillchar).release());
}


inline Bytes Str::encode(const Str& encoding, const Str& errors) const {
    return reinterpret_steal<Bytes>(attr<"encode">()(encoding, errors).release());
}


inline Str Str::expandtabs(const Int& tabsize) const {
    return reinterpret_steal<Str>(attr<"expandtabs">()(tabsize).release());
}


template <typename... Args>
inline Str Str::format(Args&&... args) const {
    return reinterpret_steal<Str>(attr<"format">()(
        detail::object_or_cast(std::forward<Args>(args))...
    ).release());
}


template <impl::dict_like T>
inline Str Str::format_map(const T& mapping) const {
    return reinterpret_steal<Str>(
        attr<"format_map">()(detail::object_or_cast(mapping)).release()
    );
}


inline bool Str::isalnum() const {
    return static_cast<bool>(attr<"isalnum">()());
}


inline bool Str::isalpha() const {
    return static_cast<bool>(attr<"isalpha">()());
}


inline bool Str::isascii() const {
    return static_cast<bool>(attr<"isascii">()());
}


inline bool Str::isdecimal() const {
    return static_cast<bool>(attr<"isdecimal">()());
}


inline bool Str::isdigit() const {
    return static_cast<bool>(attr<"isdigit">()());
}


inline bool Str::isidentifier() const {
    return static_cast<bool>(attr<"isidentifier">()());
}


inline bool Str::islower() const {
    return static_cast<bool>(attr<"islower">()());
}


inline bool Str::isnumeric() const {
    return static_cast<bool>(attr<"isnumeric">()());
}


inline bool Str::isprintable() const {
    return static_cast<bool>(attr<"isprintable">()());
}


inline bool Str::isspace() const {
    return static_cast<bool>(attr<"isspace">()());
}


inline bool Str::istitle() const {
    return static_cast<bool>(attr<"istitle">()());
}


inline bool Str::isupper() const {
    return static_cast<bool>(attr<"isupper">()());
}


inline Str Str::ljust(const Int& width) const {
    return reinterpret_steal<Str>(attr<"ljust">()(width).release());
}


inline Str Str::ljust(const Int& width, const Str& fillchar) const {
    return reinterpret_steal<Str>(attr<"ljust">()(width, fillchar).release());
}


inline Str Str::lower() const {
    return reinterpret_steal<Str>(attr<"lower">()().release());
}


inline Str Str::lstrip() const {
    return reinterpret_steal<Str>(attr<"lstrip">()().release());
}


inline Str Str::lstrip(const Str& chars) const {
    return reinterpret_steal<Str>(attr<"lstrip">()(chars).release());
}


template <typename T>
inline Dict Str::maketrans(const T& x) {
    return reinterpret_steal<Dict>(
        type.template attr<"maketrans">()(x).release()
    );
}


template <typename T, typename U>
inline Dict Str::maketrans(const T& x, const U& y) {
    return reinterpret_steal<Dict>(
        type.template attr<"maketrans">()(x, y).release()
    );
}

template <typename T, typename U, typename V>
inline Dict Str::maketrans(const T& x, const U& y, const V& z) {
    return reinterpret_steal<Dict>(
        type.template attr<"maketrans">()(x, y, z).release()
    );
}


inline Tuple Str::partition(const Str& sep) const {
    return reinterpret_steal<Tuple>(attr<"partition">()(sep).release());
}


inline Str Str::removeprefix(const Str& prefix) const {
    return reinterpret_steal<Str>(attr<"removeprefix">()(prefix).release());
}


inline Str Str::removesuffix(const Str& suffix) const {
    return reinterpret_steal<Str>(attr<"removesuffix">()(suffix).release());
}


inline Str Str::rjust(const Int& width) const {
    return reinterpret_steal<Str>(attr<"rjust">()(width).release());
}


inline Str Str::rjust(const Int& width, const Str& fillchar) const {
    return reinterpret_steal<Str>(attr<"rjust">()(width, fillchar).release());
}


inline Tuple Str::rpartition(const Str& sep) const {
    return reinterpret_steal<Tuple>(attr<"rpartition">()(sep).release());
}


inline List Str::rsplit() const {
    return reinterpret_steal<List>(attr<"rsplit">()().release());
}


inline List Str::rsplit(const Str& sep, const Int& maxsplit) const {
    return reinterpret_steal<List>(attr<"rsplit">()(sep, maxsplit).release());
}


inline Str Str::rstrip() const {
    return reinterpret_steal<Str>(attr<"rstrip">()().release());
}


inline Str Str::rstrip(const Str& chars) const {
    return reinterpret_steal<Str>(attr<"rstrip">()(chars).release());
}


inline Str Str::strip() const {
    return reinterpret_steal<Str>(attr<"strip">()().release());
}


inline Str Str::strip(const Str& chars) const {
    return reinterpret_steal<Str>(attr<"strip">()(chars).release());
}


inline Str Str::swapcase() const {
    return reinterpret_steal<Str>(attr<"swapcase">()().release());
}


inline Str Str::title() const {
    return reinterpret_steal<Str>(attr<"title">()().release());
}


template <typename T>
inline Str Str::translate(const T& table) const {
    return reinterpret_steal<Str>(
        attr<"translate">()(detail::object_or_cast(table)).release()
    );
}


inline Str Str::upper() const {
    return reinterpret_steal<Str>(attr<"upper">()().release());
}


inline Str Str::zfill(const Int& width) const {
    return reinterpret_steal<Str>(attr<"zfill">()(width).release());
}


inline Bytes Bytes::fromhex(const Str& string) {
    return reinterpret_steal<Bytes>(
        type.template attr<"fromhex">()(string).release()
    );
}


template <typename Derived>
inline Dict impl::IBytes<Derived>::maketrans(const Derived& from, const Derived& to) {
    return reinterpret_steal<Dict>(
        type.template attr<"maketrans">()(from, to).release()
    );
}


inline ByteArray ByteArray::fromhex(const Str& string) {
    return reinterpret_steal<ByteArray>(
        type.template attr<"fromhex">()(string).release()
    );
}


inline Bytes Code::bytecode() const {
    return attr<"co_code">();
}


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


namespace impl {

    template <typename Obj, typename T>
    struct visit_helper {
        static constexpr bool value = false;
    };

    template <typename Obj, typename R, typename T, typename... A>
    struct visit_helper<Obj, R(*)(T, A...)> {
        using Arg = std::decay_t<T>;
        static_assert(
            std::derived_from<Arg, Obj>,
            "visitor argument must derive from the visited object"
        );
        static constexpr bool value = true;
    };

    template <typename Obj, typename R, typename C, typename T, typename... A>
    struct visit_helper<Obj, R(C::*)(T, A...)> : visit_helper<Obj, R(*)(T, A...)> {};
    template <typename Obj, typename R, typename C, typename T, typename... A>
    struct visit_helper<Obj, R(C::*)(T, A...) const> : visit_helper<Obj, R(*)(T, A...)> {};
    template <typename Obj, typename R, typename C, typename T, typename... A>
    struct visit_helper<Obj, R(C::*)(T, A...) volatile> : visit_helper<Obj, R(*)(T, A...)> {};
    template <typename Obj, typename R, typename C, typename T, typename... A>
    struct visit_helper<Obj, R(C::*)(T, A...) const volatile> : visit_helper<Obj, R(*)(T, A...)> {};

    template <typename Obj, has_call_operator T>
    struct visit_helper<Obj, T> : visit_helper<Obj, decltype(&T::operator())> {};

    template <typename T, typename Obj>
    concept visitable = visit_helper<std::decay_t<Obj>, std::decay_t<T>>::value;

    template <typename Obj, typename Func, typename... Rest>
    auto visit_recursive(Obj&& obj, Func&& func, Rest&&... rest) {
        using Arg = visit_helper<std::decay_t<Obj>, std::decay_t<Func>>::Arg;

        if (Arg::check(obj)) {
            return std::invoke(func, reinterpret_borrow<Arg>(obj.ptr()));

        } else {
            if constexpr (sizeof...(Rest) > 0) {
                return visit_recursive(
                    std::forward<Obj>(obj),
                    std::forward<Rest>(rest)...
                );

            } else {
                std::string message = (
                    "no suitable function found for object of type '" +
                    std::string(Type(obj).name()) + "'"
                );
                throw TypeError(message);
            }
        }
    }

    /* Base class for CallTraits tags, which contain SFINAE information about a
    callable Python/C++ object, as returned by `py::callable()`. */
    template <typename Func>
    class CallTraitsBase {
    protected:
        const Func& func;

    public:
        constexpr CallTraitsBase(const Func& func) : func(func) {}

        friend std::ostream& operator<<(std::ostream& os, const CallTraitsBase& traits) {
            if (traits) {
                os << "True";
            } else {
                os << "False";
            }
            return os;
        }

    };

    /* Return tag for `py::callable()` when one or more template parameters are
    supplied, representing hypothetical arguments to the function. */
    template <typename Func, typename... Args>
    struct CallTraits : public CallTraitsBase<Func> {
        struct NoReturn {};

    private:
        using Base = CallTraitsBase<Func>;

        /* SFINAE struct gets return type if Func is callable with the given arguments.
        Otherwise defaults to NoReturn. */
        template <typename T, typename = void>
        struct GetReturn { using type = NoReturn; };
        template <typename T>
        struct GetReturn<
            T, std::void_t<decltype(std::declval<T>()(std::declval<Args>()...))>
        > {
            using type = decltype(std::declval<T>()(std::declval<Args>()...));
        };

    public:
        using Base::Base;

        /* Get the return type of the function with the given arguments.  Defaults to
        NoReturn if the function is not callable with those arguments. */
        using Return = typename GetReturn<Func>::type;

        /* Implicitly convert the tag to a constexpr bool. */
        template <typename T = Func> requires (!python_like<T>)
        inline constexpr operator bool() const {
            return std::is_invocable_v<Func, Args...>;
        }

        /* Implicitly convert to a runtime boolean by directly inspecting a Python code
        object.  Note that the introspection is very lightweight and basic.  It first
        checks `std::is_invocable<Func, Args...>` to see if all arguments can be
        converted to Python objects, and then confirms that their number matches those
        of the underlying code object.  This includes accounting for default values and
        missing keyword-only arguments, while enforcing a C++-style calling convention.
        Note that this check does not account for variadic arguments, which are not
        represented in the code object itself. */
        template <typename T = Func> requires (python_like<T>)
        operator bool() const {
            if constexpr(std::is_same_v<Return, NoReturn>) {
                return false;
            } else {
                static constexpr Py_ssize_t expected = sizeof...(Args);

                // check Python object is callable
                if (!PyCallable_Check(this->func.ptr())) {
                    return false;
                }

                // Get code object associated with callable (borrowed ref)
                PyCodeObject* code = (PyCodeObject*) PyFunction_GetCode(this->func.ptr());
                if (code == nullptr) {
                    return false;
                }

                // get number of positional/positional-only arguments from code object
                Py_ssize_t n_args = code->co_argcount;
                if (expected > n_args) {
                    return false;  // too many arguments
                }

                // get number of positional defaults from function object (borrowed ref)
                PyObject* defaults = PyFunction_GetDefaults(this->func.ptr());
                Py_ssize_t n_defaults = 0;
                if (defaults != nullptr) {
                    n_defaults = PyTuple_Size(defaults);
                }
                if (expected < (n_args - n_defaults)) {
                    return false;  // too few arguments
                }

                // check for presence of unfilled keyword-only arguments
                if (code->co_kwonlyargcount > 0) {
                    PyObject* kwdefaults = PyObject_GetAttrString(
                        this->func.ptr(),
                        "__kwdefaults__"
                    );
                    if (kwdefaults == nullptr) {
                        PyErr_Clear();
                        return false;
                    }
                    Py_ssize_t n_kwdefaults = 0;
                    if (kwdefaults != Py_None) {
                        n_kwdefaults = PyDict_Size(kwdefaults);
                    }
                    Py_DECREF(kwdefaults);
                    if (n_kwdefaults < code->co_kwonlyargcount) {
                        return false;
                    }
                }

                // NOTE: we cannot account for variadic arguments, which are not
                // represented in the code object.  This is a limitation of the Python
                // C API

                return true;
            }
        }

    };

    /* Template specialization for wildcard callable matching.  Note that for technical
    reasons, it is easier to swap the meaning of the void parameter in this case, so
    that the behavior of each class is self-consistent. */
    template <typename Func>
    class CallTraits<Func, void> : public CallTraitsBase<Func> {
        using Base = CallTraitsBase<Func>;

    public:
        using Base::Base;

        // NOTE: Return type is not well-defined for wildcard matching.  Attempting to
        // access it will result in a compile error.

        /* Implicitly convert the tag to a constexpr bool. */
        template <typename T = Func> requires (!python_like<T>)
        inline constexpr operator bool() const {
            return is_callable_any<Func>;
        }

        /* Implicitly convert the tag to a runtime bool. */
        template <typename T = Func> requires (python_like<T>)
        inline operator bool() const {
            return PyCallable_Check(this->func.ptr());
        }

    };

}


/* An equivalent for std::visit, which applies one of a selection of functions
depending on the runtime type of a generic Python object.

This is roughly equivalent to std::visit, but with a few key differences:

    1.  Each function must accept at least one argument, which must be derived from
        `py::Object`.  They are free to accept additional arguments as long as those
        arguments have default values.
    2.  Each function is checked in order, which invokes a runtime type check against
        the type of the first argument.  The first function that matches the observed
        type of the object is called, and all other functions are ignored.
    3.  The generic `py::Object` type acts as a catch-all, since its check() method
        always returns true as long as the object is not null.  Including a function
        that accepts `py::Object` can therefore act as a default case, if no previous
        function matches.
    4.  If no function is found that matches the object's type, a `TypeError` is thrown
        with a message indicating the observed type of the object.
    5.  Overload sets are not supported, as the first argument to an overloaded
        function is not well-defined.  List the functions in order instead.
*/
template <typename Obj, impl::visitable<Obj>... Funcs>
    requires (std::derived_from<std::decay_t<Obj>, Object>)
auto visit(Obj&& obj, Funcs&&... funcs) {
    static_assert(
        sizeof...(Funcs) > 0,
        "visit() requires at least one function to dispatch to"
    );
    return impl::visit_recursive(
        std::forward<Obj>(obj),
        std::forward<Funcs>(funcs)...
    );
}


/* An equivalent for std::transform, which applies one of a selection of functions
depending on the runtime type of each object in a Python iterator.

This is equivalent to manually piping the iterator through std::transform and
immediately invoking py::visit() on each element.
*/
template <typename... Funcs>
auto transform(Funcs&&... funcs) {
    return std::views::transform(
        [&funcs...](auto&& item) {
            return visit(
                std::forward<decltype(item)>(item),
                std::forward<Funcs>(funcs)...
            );
        }
    );
}


/* Equivalent to Python `callable(obj)`, except that it supports extended C++ syntax to
account for C++ function pointers, lambdas, and constexpr SFINAE checks.

Here's how this function can be used:

    if (py::callable(func)) {
        // works just like normal Python. Enters the branch if func is a Python or C++
        // callable with arbitrary arguments.
    }

    if (py::callable<int, int>(func)) {
        // if used on a C++ function, inspects the function's signature at compile time
        // to determine if it can be called with the provided arguments.

        // if used on a Python callable, inspects the underlying code object to ensure
        // that all args can be converted to Python objects, and that their number
        // matches the function's signature (not accounting for variadic or keyword
        // arguments).
    }

    if (py::callable<void>(func)) {
        // specifically checks that the function is callable with zero arguments.
        // Note that the zero-argument template specialization is reserved for wildcard
        // matching, so we have to provide an explicit void argument here.
    }

Additionally, `py::callable()` can be used at compile time if the function allows it.
This is only enabled for C++ functions whose signatures can be fully determined at
compile time, and will result in compile errors if used on Python objects, which
require runtime introspection.  Users can refer to py::python_like<> to disambiguate.

    using Return = typename decltype(py::callable<int, int>(func))::Return;
        // gets the hypothetical return type of the function with the given arguments,
        // or an internal NoReturn placeholder if no overload could be found.  Always
        // refers to Object for Python callables, provided that the arguments are valid

    static_assert(py::callable<bool, bool>(func), "func must be callable with two bools");
        // raises a compile error if the function cannot be called with the given
        // arguments.  Throws a no-constexpr error if used on Python callables.

    if constexpr (py::callable<double, double>(func)) {
        // enters a constexpr branch if the function can be called with the given
        // arguments.  The compiler will discard the branch if the condition is not
        // met, or raise a no-constexpr error if used on Python callables.
    }

Various permutations of these examples are possible, allowing users to both statically
and dynamically dispatch based on an arbitrary function's signature, with the same
universal syntax in both languages. */
template <typename... Args, impl::is_callable_any Func>
constexpr auto callable(const Func& func) {
    // If no template arguments are given, default to wildcard matching
    if constexpr (sizeof...(Args) == 0) {
        return impl::CallTraits<Func, void>{func};

    // If void is given as a single argument, reinterpret as an empty argument list
    } else if constexpr (
        sizeof...(Args) == 1 &&
        std::is_same_v<std::tuple_element_t<0, std::tuple<Args...>>, void>
    ) {
        return impl::CallTraits<Func>{func};

    // Otherwise, pass the arguments through to the strict matching overload
    } else {
        return impl::CallTraits<Func, Args...>{func};
    }
}


// not implemented
// help() - not applicable
// input() - Use std::cin for now.  May be implemented in the future.
// open() - planned for a future release along with pathlib support


/* Equivalent to Python `all(args...)` */
template <typename First = void, typename... Rest> [[deprecated]]
bool all(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::all().  Use std::all_of() or its std::ranges "
        "equivalent with a boolean predicate instead."
    );
    return false;
}


/* Equivalent to Python `any(args...)` */
template <typename First = void, typename... Rest> [[deprecated]]
bool any(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::any().  Use std::any_of() or its std::ranges "
        "equivalent with a boolean predicate instead."
    );
    return false;
}


/* Equivalent to Python `enumerate(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object enumerate(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::enumerate().  Pipe the container with "
        "std::views::enumerate() or use a simple loop counter instead."
    );
    return {};
}


/* Equivalent to Python `filter(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object filter(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::filter().  Pipe the container with "
        "std::views::filter() instead."
    );
    return {};
}


/* Equivalent to Python `map(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object map(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::map().  Pipe the container with "
        "std::views::transform() instead."
    );
    return {};
}


/* Equivalent to Python `max(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object max(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::max().  Use std::max(), std::max_element(), "
        "or their std::ranges equivalents instead."
    );
    return {};
}


/* Equivalent to Python `min(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object min(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::min().  Use std::min(), std::min_element(), "
        "or their std::ranges equivalents instead."
    );
    return {};
}


/* Equivalent to Python `next(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object next(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::next().  Use the iterator's ++ increment "
        "operator instead, and compare against an end iterator to check for "
        "completion."
    );
    return {};
}


/* Equivalent to Python `sum(obj)`, but also works on C++ containers. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object sum(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::sum().  Use std::accumulate() or an ordinary "
        "loop variable instead."
    );
    return {};
}


template <typename First = void, typename... Rest> [[deprecated]]
py::Object zip(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::zip().  If you are using C++23 or later, use "
        "std::views::zip() instead.  Otherwise, implement a coupled iterator class or "
        "keep the iterators separate during the loop body."
    );
    return {};
}


// Superceded by class wrappers
// bool()           -> py::Bool
// bytearray()      -> py::Bytearray
// bytes()          -> py::Bytes
// classmethod()    -> py::ClassMethod
// compile()        -> py::Code
// complex()        -> py::Complex
// dict()           -> py::Dict
// float()          -> py::Float
// frozenset()      -> py::FrozenSet
// int()            -> py::Int
// list()           -> py::List
// memoryview()     -> py::MemoryView
// object()         -> py::Object
// property()       -> py::Property
// range()          -> py::Range
// set()            -> py::Set
// slice()          -> py::Slice
// staticmethod()   -> py::StaticMethod
// str()            -> py::Str
// super()          -> py::Super
// tuple()          -> py::Tuple
// type()           -> py::Type


/* Get Python's builtin namespace as a dictionary.  This doesn't exist in normal
Python, but makes it much more convenient to interact with the standard library from
C++. */
inline Dict builtins() {
    PyObject* result = PyEval_GetBuiltins();
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Dict>(result);
}


/* Equivalent to Python `globals()`. */
inline Dict globals() {
    PyObject* result = PyEval_GetGlobals();
    if (result == nullptr) {
        throw RuntimeError("cannot get globals - no frame is currently executing");
    }
    return reinterpret_borrow<Dict>(result);
}


/* Equivalent to Python `locals()`. */
inline Dict locals() {
    PyObject* result = PyEval_GetLocals();
    if (result == nullptr) {
        throw RuntimeError("cannot get locals - no frame is currently executing");
    }
    return reinterpret_borrow<Dict>(result);
}


/* Equivalent to Python `aiter(obj)`.  Only works on asynchronous Python iterators. */
inline Object aiter(const Object& obj) {
    static const Str s_aiter = "aiter";
    return builtins()[s_aiter](obj);
}


/* Equivalent to Python `anext(obj)`.  Only works on asynchronous Python iterators. */
inline Object anext(const Object& obj) {
    static const Str s_anext = "anext";
    return builtins()[s_anext](obj);
}


/* Equivalent to Python `anext(obj, default)`.  Only works on asynchronous Python
iterators. */
template <typename T>
inline Object anext(const Object& obj, const T& default_value) {
    static const Str s_anext = "anext";
    return builtins()[s_anext](obj, default_value);
}


/* Equivalent to Python `ascii(obj)`.  Like `repr()`, but returns an ASCII-encoded
string. */
inline Str ascii(const Handle& obj) {
    PyObject* result = PyObject_ASCII(obj.ptr());
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(result);
}


/* Equivalent to Python `bin(obj)`.  Converts an integer or other object implementing
__index__() into a binary string representation. */
inline Str bin(const Handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 2);
    if (string == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `chr(obj)`.  Converts an integer or other object implementing
__index__() into a unicode character. */
inline Str chr(const Handle& obj) {
    PyObject* string = PyUnicode_FromFormat("%llc", obj.cast<long long>());
    if (string == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `delattr(obj, name)`. */
inline void delattr(const Object& obj, const Str& name) {
    if (PyObject_DelAttr(obj.ptr(), name.ptr()) < 0) {
        Exception::from_python();
    }
}


/* Equivalent to Python `dir()` with no arguments.  Returns a list of names in the
current local scope. */
inline List dir() {
    PyObject* result = PyObject_Dir(nullptr);
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `dir(obj)`. */
inline List dir(const Handle& obj) {
    if (obj.ptr() == nullptr) {
        throw TypeError("cannot call dir() on a null object");
    }
    PyObject* result = PyObject_Dir(obj.ptr());
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `eval()` with a string expression. */
inline Object eval(const Str& expr) {
    try {
        return pybind11::eval(expr, globals(), locals());
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `eval()` with a string expression and global variables. */
inline Object eval(const Str& source, Dict& globals) {
    try {
        return pybind11::eval(source, globals, locals());
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `eval()` with a string expression and global/local variables. */
inline Object eval(const Str& source, Dict& globals, Dict& locals) {
    try {
        return pybind11::eval(source, globals, locals);
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `exec()` with a string expression. */
inline void exec(const Str& source) {
    try {
        pybind11::exec(source, globals(), locals());
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `exec()` with a string expression and global variables. */
inline void exec(const Str& source, Dict& globals) {
    try {
        pybind11::exec(source, globals, locals());
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `exec()` with a string expression and global/local variables. */
inline void exec(const Str& source, Dict& globals, Dict& locals) {
    try {
        pybind11::exec(source, globals, locals);
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `exec()` with a precompiled code object. */
inline void exec(const Code& code) {
    code(globals() | locals());
}


/* Equivalent to Python `getattr(obj, name)` with a dynamic attribute name. */
inline Object getattr(const Handle& obj, const Str& name) {
    PyObject* result = PyObject_GetAttr(obj.ptr(), name.ptr());
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `getattr(obj, name, default)` with a dynamic attribute name and
default value. */
inline Object getattr(const Handle& obj, const Str& name, const Object& default_value) {
    PyObject* result = PyObject_GetAttr(obj.ptr(), name.ptr());
    if (result == nullptr) {
        PyErr_Clear();
        return default_value;
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `hasattr(obj, name)`. */
inline bool hasattr(const Handle& obj, const Str& name) {
    return PyObject_HasAttr(obj.ptr(), name.ptr());
}


/* Equivalent to Python `hash(obj)`, but delegates to std::hash, which is overloaded
for the relevant Python types.  This promotes hash-not-implemented exceptions into
compile-time equivalents. */
template <typename T> requires (impl::is_hashable<T> || std::derived_from<T, Object>)
size_t hash(T&& obj) {
    static_assert(
        impl::is_hashable<T>,
        "hash() is not supported for this type.  Did you forget to specialize "
        "py::__hash__<T>?"
    );
    return std::hash<std::decay_t<T>>{}(std::forward<T>(obj));
}


/* Equivalent to Python `hex(obj)`.  Converts an integer or other object implementing
__index__() into a hexadecimal string representation. */
inline Str hex(const Int& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 16);
    if (string == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `id(obj)`, but also works with C++ values.  Casts the object's
memory address to a void pointer. */
template <typename T>
const void* id(const T& obj) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<void*>(obj);

    } else if constexpr (impl::python_like<T>) {
        return reinterpret_cast<void*>(obj.ptr());

    } else {
        return reinterpret_cast<void*>(&obj);
    }
}


/* Equivalent to Python `isinstance(derived, base)`, but base is provided as a template
parameter. */
template <impl::python_like T>
inline bool isinstance(const Handle& derived) {
    static_assert(!std::is_same_v<T, Handle>, "isinstance<py::Handle>() is not allowed");
    return T::check_(derived);
}


/* Equivalent to Python `isinstance(derived, base)`. */
template <typename T>
    requires (impl::type_like<T> || impl::tuple_like<T> || std::same_as<T, Object>)
inline bool isinstance(const Handle& derived, const T& base) {
    int result = PyObject_IsInstance(
        derived.ptr(),
        detail::object_or_cast(base).ptr()
    );
    if (result == -1) {
        Exception::from_python();
    }
    return result;
}


/* Equivalent to Python `issubclass(derived, base)`, but base is provided as a template
parameter. */
template <impl::python_like T>
inline bool issubclass(const Type& derived) {
    static_assert(!std::is_same_v<T, Handle>, "issubclass<py::Handle>() is not allowed");
    static_assert(
        std::is_base_of_v<Object, T>,
        "issubclass<T>() requires T to be a subclass of py::Object.  pybind11 types "
        "do not directly track their associated Python types, so this function is not "
        "supported for them."
    );
    int result = PyObject_IsSubclass(derived.ptr(), T::type.ptr());
    if (result == -1) {
        Exception::from_python();
    }
    return result;
}


/* Equivalent to Python `issubclass(derived, base)`. */
template <typename T>
    requires (impl::type_like<T> || impl::tuple_like<T> || std::same_as<T, Object>)
inline bool issubclass(const Type& derived, const T& base) {
    int result = PyObject_IsSubclass(
        derived.ptr(),
        detail::object_or_cast(base).ptr()
    );
    if (result == -1) {
        Exception::from_python();
    }
    return result;
}


/* Equivalent to Python `iter(obj)` except that it can also accept C++ containers and
generate Python iterators over them.  Note that requesting an iterator over an rvalue
is not allowed, and will trigger a compiler error. */
template <impl::is_iterable T>
inline pybind11::iterator iter(T&& obj) {
    static_assert(
        !std::is_rvalue_reference_v<T>,
        "passing a temporary container to py::iter() is unsafe"
    );
    try {
        return pybind11::make_iterator(std::begin(obj), std::end(obj));
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `len(obj)`, but also accepts C++ types implementing a .size()
method. */
template <typename T>
    requires (impl::has_size<T> || std::derived_from<T, pybind11::object>)
inline size_t len(const T& obj) {
    try {
        if constexpr (impl::has_size<T>) {
            return obj.size();
        } else {
            return pybind11::len(obj);
        }
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `oct(obj)`.  Converts an integer or other object implementing
__index__() into an octal string representation. */
inline Str oct(const Handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 8);
    if (string == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `ord(obj)`.  Converts a unicode character into an integer
representation. */
inline Int ord(const Handle& obj) {
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


/* Equivalent to Python `reversed(obj)` except that it can also accept C++ containers
and generate Python iterators over them.  Note that requesting an iterator over an
rvalue is not allowed, and will trigger a compiler error. */
template <impl::is_reverse_iterable T>
inline pybind11::iterator reversed(T&& obj) {
    static_assert(
        !std::is_rvalue_reference_v<T>,
        "passing a temporary container to py::reversed() is unsafe"
    );
    try {
        if constexpr (std::is_base_of_v<pybind11::object, std::decay_t<T>>) {
            static const pybind11::str lookup = "__reversed__";
            return reinterpret_steal<pybind11::iterator>(
                obj.attr(lookup)().release()
            );
        } else {
            return pybind11::make_iterator(std::rbegin(obj), std::rend(obj));
        }
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `setattr(obj, name, value)`. */
inline void setattr(const Handle& obj, const Str& name, const Object& value) {
    if (PyObject_SetAttr(obj.ptr(), name.ptr(), value.ptr()) < 0) {
        Exception::from_python();
    }
}


// TODO: sorted() for C++ types.  Still returns a List, but has to reimplement logic
// for key and reverse arguments.  std::ranges::sort() is a better alternative for C++
// types.


/* Equivalent to Python `sorted(obj)`. */
inline List sorted(const Handle& obj) {
    static const Str s_sorted = "sorted";
    return builtins()[s_sorted](obj);
}


/* Equivalent to Python `sorted(obj, key=key, reverse=reverse)`. */
inline List sorted(const Handle& obj, const Function& key, bool reverse = false) {
    static const Str s_sorted = "sorted";
    return builtins()[s_sorted](
        obj,
        py::arg("key") = key,
        py::arg("reverse") = reverse
    );
}


/* Equivalent to Python `vars()`. */
inline Dict vars() {
    return locals();
}


/* Equivalent to Python `vars(object)`. */
inline Dict vars(const Object& object) {
    static const Str lookup = "__dict__";
    return reinterpret_steal<Dict>(getattr(object, lookup).release());
}


}  // namespace py


auto Regex::Match::group(const py::args& args) const
    -> std::vector<std::optional<std::string>>
{
    auto tuple = py::Tuple::from_args(args);
    std::vector<std::optional<std::string>> result;
    result.reserve(tuple.size());

    for (const auto& arg : tuple) {
        if (py::Str::check(arg)) {
            result.push_back(group(static_cast<std::string>(arg)));
        } else if (py::Int::check(arg)) {
            result.push_back(group(static_cast<size_t>(arg)));
        } else {
            throw py::TypeError(
                "group() expects an integer or string argument"
            );
        }
    }

    return result;
}


}  // namespace bertrand


#undef PYBIND11_DETAILED_ERROR_MESSAGES
#undef BERTRAND_PYTHON_INCLUDED
#endif  // BERTRAND_PYTHON_H