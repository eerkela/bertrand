#ifndef BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_INCLUDED
#define PYBIND11_DETAILED_ERROR_MESSAGES

#include "python/common.h"

#include "python/bool.h"
#include "python/int.h"
#include "python/float.h"
#include "python/complex.h"
#include "python/slice.h"
#include "python/range.h"
#include "python/list.h"
#include "python/tuple.h"
#include "python/set.h"
#include "python/dict.h"
#include "python/str.h"
#include "python/func.h"
// #include "python/datetime.h"
#include "python/math.h"
#include "python/type.h"


namespace bertrand {
namespace py {


/* TODO: What if I provided another wrapper besides static, like py::Typed<>?
 *      -> This would be easier to do with special TypedList<>, etc subclasses, which
 *         have a cleaner syntax and would apply the same Object-like semantics with
 *         regard to type narrowing, etc.  It would imply a loop through the container
 *         to check types, but from then on, it would be fully typed and checked by the
 *         compiler.
 *
 *      py::TypedList<py::Int> list = {1, 2, "a"};
 *      // compile error!  "a" is not implicitly convertible to int
 *
 *      py::TypedList<py::Int> list = {1, 2, 3};
 *      list.append("a"); 
 *      // compile error!  "a" is not implicitly convertible to int
 *
 *      py::TypedList<int> list = {1, 2, 3};
 *      list.append("a"); 
 *      // same as before, but with raw C++ integers.  Everything gets converted to a
 *      // py::Object anyways, so the only thing this does is enforce C++ type safety.
 */



/* Some attributes are forward declared to avoid circular dependencies.
 *
 * Since this file is the only valid entry point for the python ecosystem, it makes
 * sense to define them here, with full context.  This avoids complicated include paths
 * and decouples some of the types from one another.  It also relaxes some of the
 * strictness around inclusion order, and generally promotes code organization.
 */


// TODO: Regex and Decimal types?


////////////////////////////
////    TYPE MEMBERS    ////
////////////////////////////


/* Every Python type has a static `Type` member that gives access to the Python type
object associated with instances of that class. */
inline Type Type::type = Type{};  // metaprogramming in a nutshell
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


//////////////////////////////
////    CALL OPERATORS    ////
//////////////////////////////


/* Bertrand objects offer a smoother call interface than pybind11, and can convert from
 * a wider variety of types, including C++ function pointers, lambdas, etc.
 */


namespace impl {

    template <typename T>
    auto interpret_arg(T&& arg) {
        if constexpr (is_callable_any<std::decay_t<T>>) {
            return Function(std::forward<T>(arg));
        } else {
            return std::forward<T>(arg);
        }
    }

}


template <typename... Args>
inline Object Object::operator()(Args&&... args) const {
    return Base::operator()(impl::interpret_arg(std::forward<Args>(args))...);
}


#define ACCESSOR_CALL_OPERATOR(name, base)                                              \
    template <typename... Args>                                                         \
    Object impl::name::operator()(Args&&... args) const {                               \
        return detail::base::operator()(                                                \
            impl::interpret_arg(std::forward<Args>(args))...                            \
        );                                                                              \
    }                                                                                   \

ACCESSOR_CALL_OPERATOR(ObjAttrAccessor, obj_attr_accessor)
ACCESSOR_CALL_OPERATOR(StrAttrAccessor, str_attr_accessor)
ACCESSOR_CALL_OPERATOR(ItemAccessor, item_accessor)
ACCESSOR_CALL_OPERATOR(SequenceAccessor, sequence_accessor)
ACCESSOR_CALL_OPERATOR(TupleAccessor, tuple_accessor)
ACCESSOR_CALL_OPERATOR(ListAccessor, list_accessor)

#undef ACCESSOR_CALL_OPERATOR


////////////////////////////
////    CONSTRUCTORS    ////
////////////////////////////


/* Some constructors need to be defined out of line in order to avoid circular
 * dependencies.
 */


template <
    typename T,
    std::enable_if_t<!impl::is_python<T> && impl::is_callable_any<T>, int> = 0
>
inline Object::Object(const T& value) : Base(Function(value).release().ptr(), stolen_t{}) {}


template <
    typename T,
    std::enable_if_t<impl::is_python<T> && impl::str_like<T>, int> = 0
>
inline Int::Int(const T& str, int base) :
    Base(PyLong_FromUnicodeObject(str.ptr(), base), stolen_t{})
{
    if (m_ptr == nullptr) {
        throw error_already_set();
    }
}


template <
    typename T,
    std::enable_if_t<impl::is_python<T> && impl::str_like<T>, int> = 0
>
inline Float::Float(const T& str) : Base(PyFloat_FromString(str.ptr()), stolen_t{}) {
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


////////////////////////////////
////    MEMBER FUNCTIONS    ////
////////////////////////////////


inline void List::sort(const Function& key, const Bool& reverse) {
    this->attr("sort")(py::arg("key") = key, py::arg("reverse") = reverse);
}


/////////////////////////
////    OPERATORS    ////
/////////////////////////


/* By default, all generic operators are disabled for strict subclasses of
 * py::Object.  This means we have to specifically enable them for each type we
 * want to support, which promotes explicitness and type safety by design.  The
 * following structs allow users to easily assign static types to any of these
 * operators, which will automatically be preferred when operands of those
 * types are detected at compile time.  By using template specialization, we
 * allow users to do this from outside the class itself, allowing the type
 * system to grow as needed to cover any environment.  Here's an example:
 *
 *      template <>
 *      struct py::Bool::template __add__<int> {
 *          static constexpr bool enable = true;
 *          using Return = py::Int;
 *      };
 *
 * It's that simple.  Now, whenever we call `py::Bool + int`, it will
 * successfully compile and interpret the result as a strict `py::Int` type,
 * eliminating runtime overhead and granting static type safety.  It is also
 * possible to apply template constraints to these types using an optional
 * second template parameter, which allows users to enable or disable whole
 * categories of types at once.  Here's another example:
 *
 *      template <typename T>
 *      struct py::Bool::template __add__<
 *          T, std::enable_if_t<py::impl::bool_like<T>>
 *      > {
 *          static constexpr bool enable = true;
 *          using Return = py::Int;
 *      };
 *
 * As long as the constraint does not conflict with any other existing
 * template overloads, this will compile and work as expected.  Note that
 * specific overloads will always take precedence over generic ones, and any
 * ambiguities between templates will result in compile errors.
 *
 * There are several benefits to this architecture.  First, it significantly
 * reduces the number of runtime type checks that must be performed to ensure
 * strict type safety, and promotes those checks to compile time instead, which
 * is always preferable.  Second, it enables syntactically correct implicit
 * conversions between C++ and Python types, which is a huge win for usability.
 * Third, it allows us to follow traditional Python naming conventions for its
 * operator special methods, which makes it easier to remember and use them in
 * practice.  Finally, it disambiguates the internal behavior of these
 * operators, reducing the number of gotchas and making the code more idiomatic
 * and easier to reason about.
 */




// DEFINE_TYPED_UNARY_OPERATOR(Bool, operator~, Int)
// DEFINE_TYPED_BINARY_OPERATOR(Bool, operator+, bool_like, Int)
// DEFINE_TYPED_BINARY_OPERATOR(Bool, operator+, int_like, Int)
// DEFINE_TYPED_BINARY_OPERATOR(Bool, operator+, float_like, Float)
// DEFINE_TYPED_BINARY_OPERATOR(Bool, operator+, complex_like, Complex)


// #undef DECLARE_TYPED_UNARY_OPERATOR
// #undef DECLARE_TYPED_BINARY_OPERATOR
// #undef DEFINE_TYPED_UNARY_OPERATOR
// #undef DEFINE_TYPED_BINARY_OPERATOR


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


// not implemented
// enumerate() - use a standard range variable
// filter() - not implemented for now due to technical challenges
// help() - not applicable for compiled C++ code
// input() - causes Python interpreter to hang, which brings problems in compiled C++
// map() - not implemented for now due to technical challenges
// open() - not implemented for now - use C++ alternatives
// zip() - use C++ iterators directly.


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


// Python-only (no C++ support)
using pybind11::delattr;
using pybind11::eval;  // TODO: superceded by py::Code
using pybind11::eval_file;  // just make py::Code::exec() instead
using pybind11::exec;
using pybind11::getattr;
using pybind11::globals;
using pybind11::hasattr;
using pybind11::isinstance;
using pybind11::len_hint;
using pybind11::setattr;


/* Get Python's builtin namespace as a dictionary.  This doesn't exist in normal
Python, but makes it much more convenient to interact with generic Python code from
C++. */
inline Dict builtins() {
    PyObject* result = PyEval_GetBuiltins();
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Dict>(result);
}


/* Equivalent to Python `aiter(obj)`. */
inline Object aiter(const Handle& obj) {
    return builtins()["aiter"](obj);
}


/* Equivalent to Python `all(obj)`, except that it also works on iterable C++
containers. */
template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
inline bool all(const T& obj) {
    return std::all_of(obj.begin(), obj.end(), [](auto&& item) {
        return static_cast<bool>(item);
    });
}


/* Equivalent to Python `anext(obj)`. */
inline Object anext(const Handle& obj) {
    return builtins()["anext"](obj);
}


/* Equivalent to Python `anext(obj, default)`. */
template <typename T>
inline Object anext(const Handle& obj, const T& default_value) {
    return builtins()["anext"](obj, default_value);
}


/* Equivalent to Python `any(obj)`, except that it also works on iterable C++
containers. */
template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
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
require runtime introspection.  Users can refer to py::is_python<> to disambiguate.

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


/* Equivalent to Python `hash(obj)`, but delegates to std::hash, which is overloaded
for the relevant Python types.  This promotes hash-not-implemented exceptions into
compile-time errors. */
template <typename T>
inline size_t hash(const T& obj) {
    static_assert(
        impl::is_hashable<T>,
        "hash() is not supported for this type.  Did you forget to overload std::hash?"
    );
    return std::hash<T>{}(obj);
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

    } else if constexpr (impl::is_python<T>) {
        return reinterpret_cast<void*>(obj.ptr());

    } else {
        return reinterpret_cast<void*>(&obj);
    }
}


/* Equivalent to Python `issubclass(derived, base)`. */
template <typename T>
inline bool issubclass(const Type& derived, const T& base) {
    int result = PyObject_IsSubclass(derived.ptr(), detail::object_or_cast(base).ptr());
    if (result == -1) {
        throw error_already_set();
    }
    return result;
}


/* Equivalent to Python `locals()`. */
inline Dict locals() {
    PyObject* result = PyEval_GetLocals();
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_borrow<Dict>(result);
}


/* Equivalent to Python `next(obj)`. */
inline Object next(const Iterator& iter) {
    PyObject* result = PyIter_Next(iter.ptr());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            throw error_already_set();
        }
        throw StopIteration();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `next(obj, default)`. */
inline Object next(const Iterator& iter, const Object& default_value) {
    PyObject* result = PyIter_Next(iter.ptr());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            throw error_already_set();
        }
        return default_value;
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `max(obj)`, but also works on iterable C++ containers. */
template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
inline auto max(const T& obj) {
    return *std::max_element(obj.begin(), obj.end());
}


/* Equivalent to Python `min(obj)`, but also works on iterable C++ containers. */
template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
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


/* Equivalent to Python `print(args...)`, except that it can take arbitrary C++ objects
using the py::Str constructor. */
template <typename... Args>
inline void print(const Args&... args) {
    auto convert = [](auto&& arg) {
        if constexpr (impl::is_wrapper<std::decay_t<decltype(arg)>>) {
            return Str(*arg);
        } else {
            return Str(arg);
        }
    };

    pybind11::print(convert(args)...);
}


/* Equivalent to Python `reversed(obj)` except that it can also accept C++ containers
and generate Python iterators over them.  Note that C++ types as rvalues are not
allowed, and will trigger a compile-time error. */
template <typename T>
inline Iterator reversed(T&& obj) {
    if constexpr (impl::is_python<std::decay_t<T>>) {
        return obj.attr("__reversed__")();
    } else {
        static_assert(
            !std::is_rvalue_reference_v<decltype(obj)>,
            "passing an rvalue to py::reversed() is unsafe"
        );
        return pybind11::make_iterator(obj.rbegin(), obj.rend());
    }
}


/* Specialization of `reversed()` for Tuple and List objects that use direct array
access rather than going through the Python API. */
template <typename T, std::enable_if_t<impl::is_reverse_iterable<T>, int> = 0>
inline Iterator reversed(const T& obj) {
    using Iter = typename T::ReverseIterator;
    return pybind11::make_iterator(Iter(obj.data(), obj.size() - 1), Iter(-1));
}


/* Equivalent to Python `sorted(obj)`. */
inline List sorted(const Handle& obj) {
    return builtins()["sorted"](obj);
}


/* Equivalent to Python `sum(obj)`, but also works on C++ containers. */
template <typename T>
inline auto sum(const T& obj) {
    return std::accumulate(obj.begin(), obj.end(), T{});
}


/* Equivalent to Python `sorted(obj, key=key, reverse=reverse)`. */
template <typename Func>
inline List sorted(const Handle& obj, const Function& key, bool reverse = false) {
    return builtins()["sorted"](
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
inline Dict vars(const Handle& object) {
    return object.attr("__dict__");
}


// TODO: sorted() for C++ types


}  // namespace py
}  // namespace bertrand


////////////////////////////
////    TYPE CASTERS    ////
////////////////////////////


/* Pybind11 uses templated type casters to handle conversions between C++ and Python.
 * Bertrand expands these to include its own wrapper types.
 */


// TODO: implement type casters for range, MappingProxy, KeysView, ValuesView,
// ItemsView, Method, ClassMethod, StaticMethod, Property


#undef BERTRAND_OBJECT_CONSTRUCTORS
#undef DELETE_OPERATOR
#undef BERTRAND_STD_HASH
#undef BERTRAND_STD_EQUAL_TO
#undef PYBIND11_DETAILED_ERROR_MESSAGES
#undef BERTRAND_PYTHON_INCLUDED
#endif  // BERTRAND_PYTHON_H