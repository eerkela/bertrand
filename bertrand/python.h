#ifndef BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_INCLUDED
#define PYBIND11_DETAILED_ERROR_MESSAGES

#include <numeric>

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


namespace bertrand {
namespace py {


namespace literals {

    using namespace pybind11::literals;

    inline Code operator ""_python(const char* source, size_t size) {
        return Code::compile(std::string_view(source, size));
    }

}


// TODO: Regex and Decimal types?


////////////////////////////
////    TYPE MEMBERS    ////
////////////////////////////


/* Every Python type has a static `Type` member that gives access to the Python type
object associated with instances of that class. */
inline Type Type::type = Type{};
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
inline Type Method::type = reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyInstanceMethod_Type));
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


Module Module::def_submodule(const char* name, const char* doc) {
    const char* this_name = PyModule_GetName(m_ptr);
    if (this_name == nullptr) {
        throw error_already_set();
    }
    std::string full_name = std::string(this_name) + '.' + name;
    Handle submodule = PyImport_AddModule(full_name.c_str());
    if (!submodule) {
        throw error_already_set();
    }
    auto result = reinterpret_borrow<Module>(submodule);
    if (doc && pybind11::options::show_user_defined_docstrings()) {
        result.template attr<"__doc__">() = pybind11::str(doc);
    }
    pybind11::setattr(*this, name, result);
    return result;
}


template <typename T>
    requires (impl::python_like<T> && impl::str_like<T>)
inline Int::Int(const T& str, int base) :
    Base(PyLong_FromUnicodeObject(str.ptr(), base), stolen_t{})
{
    if (m_ptr == nullptr) {
        throw error_already_set();
    }
}


inline Float::Float(const Str& str) : Base(PyFloat_FromString(str.ptr()), stolen_t{}) {
    if (m_ptr == nullptr) {
        throw error_already_set();
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
        throw error_already_set();
    }
}


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


KeysView::KeysView(const Dict& dict) {
    m_ptr = dict.template attr<"keys">()().release().ptr();
}


inline KeysView Dict::keys() const {
    return reinterpret_steal<KeysView>(attr<"keys">()().release());
}


ValuesView::ValuesView(const Dict& dict) {
    m_ptr = dict.template attr<"values">()().release().ptr();
}


inline ValuesView Dict::values() const {
    return reinterpret_steal<ValuesView>(attr<"values">()().release());
}


ItemsView::ItemsView(const Dict& dict) {
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


// not implemented
// enumerate() - use a standard loop index
// filter() - use std::ranges or std::algorithms instead
// help() - not applicable
// input() - Use std::cin for now.  May be implemented in the future.
// map() - use std::ranges or std::algorithms instead
// next() - use C++ iterators directly
// open() - planned for a future release along with pathlib support
// zip() - use C++ iterators directly


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
        throw error_already_set();
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


/* Equivalent to Python `all(obj)`, except that it also works on iterable C++
containers. */
template <impl::is_iterable T>
inline bool all(const T& obj) {
    return std::all_of(obj.begin(), obj.end(), [](auto&& item) {
        return static_cast<bool>(item);
    });
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


/* Equivalent to Python `any(obj)`, except that it also works on iterable C++
containers. */
template <impl::is_iterable T>
inline bool any(const T& obj) {
    return std::any_of(obj.begin(), obj.end(), [](auto&& item) {
        return static_cast<bool>(item);
    });
}


/* Equivalent to Python `ascii(obj)`.  Like `repr()`, but returns an ASCII-encoded
string. */
inline Str ascii(const Handle& obj) {
    PyObject* result = PyObject_ASCII(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(result);
}


/* Equivalent to Python `bin(obj)`.  Converts an integer or other object implementing
__index__() into a binary string representation. */
inline Str bin(const Handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 2);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
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
template <typename... Args, typename Func>
inline constexpr auto callable(const Func& func) {
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


/* Equivalent to Python `chr(obj)`.  Converts an integer or other object implementing
__index__() into a unicode character. */
inline Str chr(const Handle& obj) {
    PyObject* string = PyUnicode_FromFormat("%llc", obj.cast<long long>());
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `delattr(obj, name)`. */
inline void delattr(const Object& obj, const Str& name) {
    if (PyObject_DelAttr(obj.ptr(), name.ptr()) < 0) {
        throw error_already_set();
    }
}


/* Equivalent to Python `dir()` with no arguments.  Returns a list of names in the
current local scope. */
inline List dir() {
    PyObject* result = PyObject_Dir(nullptr);
    if (result == nullptr) {
        throw error_already_set();
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
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `eval()` with a string expression. */
inline Object eval(const Str& expr) {
    return pybind11::eval(expr, globals(), locals());
}


/* Equivalent to Python `eval()` with a string expression and global variables. */
inline Object eval(const Str& source, Dict& globals) {
    return pybind11::eval(source, globals, locals());
}


/* Equivalent to Python `eval()` with a string expression and global/local variables. */
inline Object eval(const Str& source, Dict& globals, Dict& locals) {
    return pybind11::eval(source, globals, locals);
}


/* Equivalent to Python `exec()` with a string expression. */
inline void exec(const Str& source) {
    pybind11::exec(source, globals(), locals());
}


/* Equivalent to Python `exec()` with a string expression and global variables. */
inline void exec(const Str& source, Dict& globals) {
    pybind11::exec(source, globals, locals());
}


/* Equivalent to Python `exec()` with a string expression and global/local variables. */
inline void exec(const Str& source, Dict& globals, Dict& locals) {
    pybind11::exec(source, globals, locals);
}


/* Equivalent to Python `exec()` with a precompiled code object. */
inline void exec(const Code& code) {
    code(globals() | locals());
}


/* Equivalent to Python `getattr(obj, name)` with a dynamic attribute name. */
inline Object getattr(const Handle& obj, const Str& name) {
    PyObject* result = PyObject_GetAttr(obj.ptr(), name.ptr());
    if (result == nullptr) {
        throw error_already_set();
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
template <typename T>
inline size_t hash(T&& obj) {
    static_assert(
        impl::is_hashable<T>,
        "hash() is not supported for this type.  Did you forget to overload std::hash?"
    );
    return std::hash<std::decay_t<T>>{}(std::forward<T>(obj));
}


/* Equivalent to Python `hex(obj)`.  Converts an integer or other object implementing
__index__() into a hexadecimal string representation. */
inline Str hex(const Handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 16);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `id(obj)`, but also works with C++ values.  Casts the object's
memory address to a void pointer. */
template <typename T>
inline const void* id(const T& obj) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<void*>(obj);

    } else if constexpr (impl::python_like<T>) {
        return reinterpret_cast<void*>(obj.ptr());

    } else {
        return reinterpret_cast<void*>(&obj);
    }
}


/* Equivalent to Python `isinstance(derived, base)`. */
template <typename T>
inline bool isinstance(const Handle& derived, const Type& base) {
    int result = PyObject_IsInstance(
        derived.ptr(),
        detail::object_or_cast(base).ptr()
    );
    if (result == -1) {
        throw error_already_set();
    }
    return result;
}


/* Equivalent to Python `isinstance(derived, base)`, but base is provided as a template
parameter. */
template <impl::python_like T>
inline bool isinstance(const Handle& derived) {
    static_assert(!std::is_same_v<T, Handle>, "isinstance<py::Handle>() is not allowed");
    return T::check_(derived);
}


/* Equivalent to Python `issubclass(derived, base)`. */
template <typename T>
inline bool issubclass(const Type& derived, const T& base) {
    int result = PyObject_IsSubclass(
        derived.ptr(),
        detail::object_or_cast(base).ptr()
    );
    if (result == -1) {
        throw error_already_set();
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
        throw error_already_set();
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
        "passing an rvalue to py::iter() is unsafe"
    );
    return pybind11::make_iterator(obj.begin(), obj.end());
}


/* Equivalent to Python `len(obj)`, but also accepts C++ types implementing a .size()
method.  Returns nullopt if the size could not be determined.  Use `.size()` directly
if you'd prefer a compile error instead. */
template <typename T>
inline std::optional<size_t> len(const T& obj) {
    try {
        if constexpr (impl::has_size<T>) {
            return obj.size();  // prefers custom overloads of .size()
        } else if constexpr (impl::python_like<T>) {
            return pybind11::len(obj);  // fallback to Python __len__
        } else {
            return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }
}


/* Equivalent to Python `max(obj)`, but also works on iterable C++ containers. */
template <impl::is_iterable T>
inline auto max(const T& obj) {
    return *std::max_element(obj.begin(), obj.end());
}


/* Equivalent to Python `min(obj)`, but also works on iterable C++ containers. */
template <impl::is_iterable T>
inline auto min(const T& obj) {
    return *std::min_element(obj.begin(), obj.end());
}


/* Equivalent to Python `oct(obj)`.  Converts an integer or other object implementing
__index__() into an octal string representation. */
inline Str oct(const Handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 8);
    if (string == nullptr) {
        throw error_already_set();
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
template <typename T>
inline pybind11::iterator reversed(T&& obj) {
    static_assert(
        !std::is_rvalue_reference_v<T>,
        "passing an rvalue to py::reversed() is unsafe"
    );
    if constexpr (std::is_base_of_v<pybind11::object, std::decay_t<T>>) {
        return reinterpret_steal<pybind11::iterator>(
            obj.attr("__reversed__")().release()
        );
    } else {
        return pybind11::make_iterator(obj.rbegin(), obj.rend());
    }
}


/* Equivalent to Python `setattr(obj, name, value)`. */
inline void setattr(const Handle& obj, const Str& name, const Object& value) {
    if (PyObject_SetAttr(obj.ptr(), name.ptr(), value.ptr()) < 0) {
        throw error_already_set();
    }
}


// TODO: sorted() for C++ types.  Still returns a List, but has to reimplement logic
// for key and reverse arguments.


/* Equivalent to Python `sorted(obj)`. */
inline List sorted(const Handle& obj) {
    static const Str s_sorted = "sorted";
    return builtins()[s_sorted](obj);
}


/* Equivalent to Python `sorted(obj, key=key, reverse=reverse)`. */
template <typename Func>
inline List sorted(const Handle& obj, const Function& key, bool reverse = false) {
    static const Str s_sorted = "sorted";
    return builtins()[s_sorted](
        obj,
        py::arg("key") = key,
        py::arg("reverse") = reverse
    );
}


/* Equivalent to Python `sum(obj)`, but also works on C++ containers. */
template <typename T>
inline auto sum(const T& obj) {
    return std::accumulate(obj.begin(), obj.end(), T{});
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
}  // namespace bertrand


#undef PYBIND11_DETAILED_ERROR_MESSAGES
#undef BERTRAND_PYTHON_INCLUDED
#endif  // BERTRAND_PYTHON_H