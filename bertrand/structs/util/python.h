#ifndef BERTRAND_STRUCTS_UTIL_CONTAINER_H
#define BERTRAND_STRUCTS_UTIL_CONTAINER_H

#include <array>  // std::array
#include <cstddef>  // size_t
#include <cstdio>  // std::FILE, std::fopen
#include <deque>  // std::deque
#include <optional>  // std::optional
#include <sstream>
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <tuple>  // std::tuple
#include <type_traits>  // std::enable_if_t<>
#include <valarray>  // std::valarray
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python(), TypeError, KeyError, IndexError
#include "func.h"  // FuncTraits, identity
#include "iter.h"  // iter()


// TODO: this can import ops.h and then delegate all operators over there.  This
// would synchronize implementations and allow automatic conversion of C++ types
// to equivalent Python.


/* NOTE: Python is great, but working with its C API is not.  As such, this file
 * contains a collection of wrappers around the CPython API that make it easier to work
 * with Python objects in C++.  The goal is to make the API more pythonic and less
 * error-prone, while still providing access to the full range of Python's
 * capabilities.
 *
 * Included in this file are:
 *  1.  RAII-based wrappers for PyObject* pointers that automatically handle reference
 *      counts and behave more like their Python counterparts.
 *  2.  Automatic logging of reference counts to ensure that they remain balanced over
 *      the entire program.
 *  3.  Wrappers for built-in Python functions, which are overloaded to handle STL
 *      types and conform to C++ conventions.
 *  4.  Generic programming support for mixed C++/Python objects, including automatic
 *      conversions for basic C++ types into their Python equivalents where possible.
 */


namespace bertrand {
namespace python {


/* Reference counting protocols for correctly managing PyObject* lifetimes. */
enum class Ref {
    NEW,    // increment on construction, decrement on destruction.
    STEAL,  // decrement on destruction.  Assumes ownership over object.
    BORROW  // do not modify refcount.  Object is assumed to outlive the wrapper.
};


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <Ref ref>
struct Object;

template <Ref ref>
class Bool;

template <Ref ref>
class Int;

template <Ref ref>
class Float;

template <Ref ref>
class Complex;

template <Ref ref>
class Slice;

template <Ref ref>
class Tuple;

template <Ref ref>
class List;

template <Ref ref>
class Set;

template <Ref ref>
class Dict;

template <Ref ref>
class FastSequence;

template <Ref ref>
class String;

template <Ref ref>
class Code;

template <Ref ref>
class Function;

template <Ref ref>
class Method;

template <Ref ref>
class ClassMethod;


////////////////////////////////
////    REFERENCE COUNTS    ////
////////////////////////////////


/* Reference counting is among the worst parts of the CPython API.  It's the source of
 * a great many memory leaks and segfaults, most of which are extremely difficult to
 * debug.  Using these functions doesn't necessarily make it any easier, but it does
 * update the log to reflect changes, which is a start.  As long as all reference
 * counts are logged in this way, we can run a separate script on the log file itself
 * to check that they remain balanced over the entire program.
 *
 * NOTE: in the interest of performance, these functions do not check for null pointers
 * or convert to managed references.  Users should make sure to do that themselves if
 * necessary.
 */


/* Get the current reference count of a PyObject*.  The argument must not be null. */
inline size_t refcount(PyObject* obj) {
    return Py_REFCNT(obj);
}


/* Increment the reference count of a PyObject*.  The argument must not be null. */
inline void incref(PyObject* obj) {
    LOG(ref, "incref(", obj, ")");
    Py_INCREF(obj);
}


/* Increment the reference count of a (possibly null) PyObject*. */
inline void xincref(PyObject* obj) noexcept {
    LOG(ref, "xincref(", obj, ")");
    Py_XINCREF(obj);
}


/* Decrement the reference count of a PyObject*.  The argument must not be null. */
inline void decref(PyObject* obj) {
    LOG(ref, "decref(", obj, ")");
    Py_DECREF(obj);
}


/* Decrement the reference count of a (possibly null) PyObject*. */
inline void xdecref(PyObject* obj) noexcept {
    LOG(ref, "xdecref(", obj, ")");
    Py_XDECREF(obj);
}


/* Increment the reference count of a PyObject* and then return it.  The argument must
not be null. */
inline PyObject* newref(PyObject* obj) {
    LOG(ref, "newref(", obj, ")");
    return Py_NewRef(obj);
}


/* Increment the reference count of a (possibly null) PyObject* and then return it. */
inline PyObject* xnewref(PyObject* obj) noexcept {
    LOG(ref, "xnewref(", obj, ")");
    return Py_XNewRef(obj);
}


////////////////////////////////
////    ATTRIBUTE ACCESS    ////
////////////////////////////////


/* NOTE: There are several ways to access an object's attributes in the CPython API,
 * and they are not necessarily ergonomic in C++.  The following functions give a
 * more pythonic syntax in C++.
 *
 * NOTE: hasattr() silently ignores any errors emanating from an object's __getattr__()
 * or __getattribute__() methods.  For proper error handling, use getattr() instead.
 */


/* Check if an object has an attribute with the given name. */
inline bool hasattr(PyObject* obj, PyObject* attr) noexcept {
    return PyObject_HasAttr(obj, attr);
}


/* Check if an object has an attribute with the given name. */
inline bool hasattr(PyObject* obj, const char* attr) noexcept {
    return PyObject_HasAttrString(obj, attr);
}


/* Check if an object has an attribute with the given name. */
inline bool hasattr(PyObject* obj, const std::string& attr) noexcept {
    return hasattr(obj, attr.c_str());
}


/* Check if an object has an attribute with the given name. */
inline bool hasattr(PyObject* obj, const std::string_view& attr) noexcept {
    return hasattr(obj, attr.data());
}


/* Access an attribute from an object.  Can throw if the attribute does not exist. */
inline Object<Ref::STEAL> getattr(PyObject* obj, PyObject* attr) {
    PyObject* result = PyObject_GetAttr(obj, attr);
    if (result == nullptr) {
        throw catch_python();
    }
    return Object<Ref::STEAL>(result);
}


/* Access an attribute from an object.  Can throw if the attribute does not exist. */
inline Object<Ref::STEAL> getattr(PyObject* obj, const char* attr) {
    PyObject* result = PyObject_GetAttrString(obj, attr);
    if (result == nullptr) {
        throw catch_python();
    }
    return Object<Ref::STEAL>(result);
}


/* Access an attribute from an object.  Can throw if the attribute does not exist. */
inline Object<Ref::STEAL> getattr(PyObject* obj, const std::string& attr) {
    return getattr(obj, attr.c_str());
}


/* Access an attribute from an object.  Can throw if the attribute does not exist. */
inline Object<Ref::STEAL> getattr(PyObject* obj, const std::string_view& attr) {
    return getattr(obj, attr.data());
}


/* Set an attribute on an object.  Can throw if the attribute cannot be set. */
inline void setattr(PyObject* obj, PyObject* attr, PyObject* value) {
    if (PyObject_SetAttr(obj, attr, value)) {
        throw catch_python();
    }
}


/* Set an attribute on an object.  Can throw if the attribute cannot be set. */
inline void setattr(PyObject* obj, const char* attr, PyObject* value) {
    if (PyObject_SetAttrString(obj, attr, value)) {
        throw catch_python();
    }
}


/* Set an attribute on an object.  Can throw if the attribute cannot be set. */
inline void setattr(PyObject* obj, const std::string& attr, PyObject* value) {
    setattr(obj, attr.c_str(), value);
}


/* Set an attribute on an object.  Can throw if the attribute cannot be set. */
inline void setattr(PyObject* obj, const std::string_view& attr, PyObject* value) {
    setattr(obj, attr.data(), value);
}


/* Delete an attribute from an object.  Can throw if the attribute cannot be deleted. */
inline void delattr(PyObject* obj, PyObject* attr) {
    if (PyObject_DelAttr(obj, attr)) {
        throw catch_python();
    }
}


/* Delete an attribute from an object.  Can throw if the attribute cannot be deleted. */
inline void delattr(PyObject* obj, const char* attr) {
    if (PyObject_DelAttrString(obj, attr)) {
        throw catch_python();
    }
}


/* Delete an attribute from an object.  Can throw if the attribute cannot be deleted. */
inline void delattr(PyObject* obj, const std::string& attr) {
    delattr(obj, attr.c_str());
}


/* Delete an attribute from an object.  Can throw if the attribute cannot be deleted. */
inline void delattr(PyObject* obj, const std::string_view& attr) {
    delattr(obj, attr.data());
}


/* Get a list of strings representing named attributes of the object. */
inline List<Ref::STEAL> dir(PyObject* obj) {
    PyObject* result = PyObject_Dir(obj);
    if (result == nullptr) {
        throw TypeError("dir() argument must not be null");
    }
    return List<Ref::STEAL>(result);
}


/////////////////////////////
////    TYPE CHECKING    ////
/////////////////////////////


/* Check if a Python object is an instance of the given type.  Has all the same
semantics as the python built-in `isinstance()` function.  Can throw. */
inline bool isinstance(PyObject* obj, PyObject* type) {
    int result = PyObject_IsInstance(obj, type);
    if (result == -1) {
        throw catch_python();
    }
    return result;
}


/* Check if a Python object is an instance of the given type.  Has all the same
semantics as the python built-in `isinstance()` function.  Can throw. */
inline bool isinstance(PyObject* obj, PyTypeObject* type) {
    return isinstance(obj, reinterpret_cast<PyObject*>(type));
}


/* Check if a Python type is a subclass of another type.  Has all the same semantics
as the python built-in `issubclass()` function.  Can throw. */
inline bool issubclass(PyObject* derived, PyObject* base) {
    int result = PyObject_IsSubclass(derived, base);
    if (result == -1) {
        throw catch_python();
    }
    return result;
}


/* Check if a Python type is a subclass of another type.  Has all the same semantics
as the python built-in `issubclass()` function.  Can throw. */
inline bool issubclass(PyObject* derived, PyTypeObject* base) {
    return issubclass(derived, reinterpret_cast<PyObject*>(base));
}


/* Check if a Python type is a subclass of another type.  Has all the same semantics
as the python built-in `issubclass()` function.  Can throw. */
inline bool issubclass(PyTypeObject* derived, PyObject* base) {
    return issubclass(reinterpret_cast<PyObject*>(derived), base);
}


/* Check if a Python type is a subclass of another type.  Has all the same semantics
as the python built-in `issubclass()` function.  Can throw. */
inline bool issubclass(PyTypeObject* derived, PyTypeObject* base) {
    return issubclass(
        reinterpret_cast<PyObject*>(derived),
        reinterpret_cast<PyObject*>(base)
    );
}


/////////////////////////
////    ITERATION    ////
/////////////////////////


/* A C++ wrapper around a Python iterator that enables it to be used in standard
loops. */
template <typename Convert>
struct Iterator {
    PyObject* iterator;
    PyObject* curr;
    Convert convert;

    using iterator_category     = std::forward_iterator_tag;
    using difference_type       = std::ptrdiff_t;
    using value_type            = std::remove_reference_t<ReturnType>;
    using pointer               = value_type*;
    using reference             = value_type&;

    // NOTE: this steals a reference to the iterator, so we don't incref it here.  The
    // iterator should not be null.
    Iterator(PyObject* it, Convert&& f) :
        iterator(it), curr(nullptr), convert(std::forward<Convert>(f))
    {
        curr = PyIter_Next(iterator);
        if (curr == nullptr && PyErr_Occurred()) {
            Py_DECREF(iterator);
            throw catch_python();
        }
    }

    Iterator(Convert&& f) :
        iterator(nullptr), curr(nullptr), convert(std::forward<Convert>(f))
    {}

    Iterator(const Iterator& other) :
        convert(other.convert), iterator(Py_XNewRef(other.iterator)),
        curr(Py_XNewRef(other.curr))
    {}

    Iterator(Iterator&& other) :
        convert(std::move(other.convert)), iterator(other.iterator),
        curr(other.curr)
    {
        other.iterator = nullptr;
        other.curr = nullptr;
    }

    Iterator& operator=(const Iterator& other) {
        if (this == &other) {
            return *this;
        }
        Py_XINCREF(iterator);
        Py_XINCREF(curr);
        convert = other.convert;
        iterator = other.iterator;
        curr = other.curr;
        return *this;
    }

    Iterator& operator=(Iterator&& other) {
        if (this == &other) {
            return *this;
        }
        convert = std::move(other.convert);
        iterator = other.iterator;
        curr = other.curr;
        other.iterator = nullptr;
        other.curr = nullptr;
        return *this;
    }

    ~Iterator() {
        Py_XDECREF(iterator);
        Py_XDECREF(curr);
    }

    /* Get current item. */
    inline auto operator*() const {
        if constexpr (std::is_same_v<Convert, identity>) {
            return curr;
        } else {
            return convert(curr);
        }
    }

    /* Advance to next item. */
    inline Iterator& operator++() {
        Py_DECREF(curr);
        curr = PyIter_Next(iterator);
        if (curr == nullptr && PyErr_Occurred()) {
            throw catch_python();
        }
        return *this;
    }

    /* Terminate iteration. */
    template <typename F>
    inline bool operator!=(const Iterator<F>& other) const {
        return curr != other.curr;
    }

};


/* A coupled pair of Iterators that can be directly iterated over. */
template <typename Begin, typename End>
class IteratorPair {
    Begin _begin;
    End _end;

    IteratorPair(Begin&& begin, End&& end) :
        _begin(std::forward<Begin>(begin)), _end(std::forward<End>(end))
    {}

public:

    /* Get the first iterator. */
    inline auto begin() const {
        return _begin;
    }

    /* Get the second iterator. */
    inline auto end() const {
        return _end;
    }

    /* Terminate sequence. */
    inline bool operator!=(const IteratorPair& other) const {
        return _begin != other._begin;
    }

    /* Advance to next item. */
    inline IteratorPair& operator++() {
        ++_begin;
        ++_end;
        return *this;
    }

    /* Get current item. */
    inline auto operator*() const {
        return std::make_tuple(*_begin, *_end);
    }

};


/* Get a C++ iterator over a Python object.  Returns an iterator pair that contains
both a begin() and end() member, any combination of which can be used in idiomatic C++
loops. */
inline auto iter(PyObject* obj)
    -> IteratorPair<Iterator<identity>, Iterator<identity>>
{
    Object<Ref::BORROW> wrapped(obj);
    return {wrapped.begin(), wrapped.end()}
}


/* Get a C++ iterator over a Python object.  Returns an iterator pair that contains
both a begin() and end() member, each of which can be used in idiomatic C++ loops.
Applies an optional conversion function to the iterator's dereference operator, which
takes a single PyObject* parameter.  The result of the conversion will be returned
at each dereference step. */
template <typename Convert>
inline auto iter(PyObject* obj, Convert&& convert)
    -> IteratorPair<Iterator<std::decay_t<Convert>>, Iterator<identity>>
{
    Object<Ref::BORROW> wrapped(obj);
    return {wrapped.begin(std::forward<Convert>(convert)), wrapped.end()}
}


/* Get a C++ reverse iterator over a Python object.  Returns an iterator pair that
contains both a begin() and end() member, any combination of which can be used in
idiomatic C++ loops. */
inline auto reversed(PyObject* obj)
    -> IteratorPair<Iterator<identity>, Iterator<identity>>
{
    Object<Ref::BORROW> wrapped(obj);
    return {wrapped.rbegin(), wrapped.rend()}
}


/* Get a C++ reverse iterator over a Python object.  Returns an iterator pair that
contains both a begin() and end() member, each of which can be used in idiomatic C++
loops.  Applies an optional conversion function to the iterator's dereference
operator, which takes a single PyObject* parameter.  The result of the conversion will
be returned at each dereference step. */
template <typename Convert>
inline auto reversed(PyObject* obj, Convert&& convert)
    -> IteratorPair<Iterator<std::decay_t<Convert>>, Iterator<identity>>
{
    Object<Ref::BORROW> wrapped(obj);
    return {wrapped.rbegin(std::forward<Convert>(convert)), wrapped.rend()}
}


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


/* Get the next item from an iterator.  The argument must be an iterator. */
inline Object<Ref::STEAL> next(PyObject* iter) {
    PyObject* result = PyIter_Next(iter);
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            throw catch_python();
        }
        throw StopIteration();
    }
    return Object<Ref::STEAL>(result);
}


/* Get the next item from an iterator, or a default value if the iterator is exhausted.
The argument must be an iterator.  Borrows a reference to the default value. */
inline Object<Ref::STEAL> next(PyObject* iter, PyObject* default_value) {
    PyObject* result = PyIter_Next(iter);
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(Py_NewRef(default_value));
    }
    return Object<Ref::STEAL>(result);
}


/* Get the length of a Python object.  Returns nullopt if the object does not support
the sequence protocol. */
inline std::optional<size_t> len(PyObject* obj) {
    if (!PyObject_HasAttrString(obj, "__len__")) {
        return std::nullopt;
    }

    Py_ssize_t result = PyObject_Length(obj);
    if (result == -1 && PyErr_Occurred()) {
        throw catch_python();
    }
    return std::make_optional(static_cast<size_t>(result));
}


/* Check if a Python object is considered truthy.  Equivalent to Python bool(). */
inline bool truthy(PyObject* obj) {
    int result = PyObject_IsTrue(obj);
    if (result == -1) {
        throw catch_python();
    }
    return result;
}


/* Check if a Python object is considered falsy.  Equivalent to Python `not`
expression. */
inline bool falsy(PyObject* obj) {
    int result = PyObject_Not(obj);
    if (result == -1) {
        throw catch_python();
    }
    return result;
}


/* Check if a Python object is callable.  Equivalent to Python callable(). */
inline bool callable(PyObject* obj) noexcept {
    return PyCallable_Check(obj);
}


/* Hash a Python object.  Can throw if the object does not support hashing. */
inline size_t hash(PyObject* obj) {
    // ASCII string special case (taken from CPython source)
    // see: cpython/objects/setobject.c; set_contains_key()
    Py_ssize_t result;
    if (!PyUnicode_CheckExact(obj) || (result = _PyASCIIObject_CAST(obj)->hash) == -1) {
        result = PyObject_Hash(obj);  // fall back to PyObject_Hash()
        if (result == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
    }
    return static_cast<size_t>(result);
}


/* Get the absolute value of a Python object.  Can throw if the object is not numeric. */
inline Object<Ref::STEAL> abs(PyObject* obj) {
    PyObject* result = PyNumber_Absolute(obj);
    if (result == nullptr) {
        throw catch_python();
    }
    return Object<Ref::STEAL>(result);
}


/* Get the negation of a Python object.  Can throw if the object is not numeric. */
inline Object<Ref::STEAL> neg(PyObject* obj) {
    PyObject* result = PyNumber_Negative(obj);
    if (result == nullptr) {
        throw catch_python();
    }
    return Object<Ref::STEAL>(result);
}


/* Get the bitwise negation of a Python object.  Can throw if the object is not numeric. */
inline Object<Ref::STEAL> invert(PyObject* obj) {
    PyObject* result = PyNumber_Invert(obj);
    if (result == nullptr) {
        throw catch_python();
    }
    return Object<Ref::STEAL>(result);
}


/* Get a string representation of a Python object.  Equivalent to Python str(). */
inline String<Ref::STEAL> str(PyObject* obj) {
    PyObject* string = PyObject_Str(obj);
    if (string == nullptr) {
        throw catch_python();
    }
    return String<Ref::STEAL>(string);
}


/* Get a string representation of a Python object.  Equivalent to Python repr(). */
inline String<Ref::STEAL> repr(PyObject* obj) {
    PyObject* string = PyObject_Repr(obj);
    if (string == nullptr) {
        throw catch_python();
    }
    return String<Ref::STEAL>(string);
}


/* Get a string representation of a Python object with non-ASCII characters escaped.
Equivalent to Python ascii(). */
inline String<Ref::STEAL> ascii(PyObject* obj) {
    PyObject* string = PyObject_ASCII(obj);
    if (string == nullptr) {
        throw catch_python();
    }
    return String<Ref::STEAL>(string);
}


/* Convert an integer or integer-like object (one that implements __index__()) into a
binary string representation.  Equivalent to Python bin(). */
inline String<Ref::STEAL> bin(PyObject* obj) {
    PyObject* string = PyNumber_ToBase(integer, 2);
    if (string == nullptr) {
        throw catch_python();
    }
    return String<Ref::STEAL>(string);
}


/* Convert an integer or integer-like object (one that implements __index__()) into an
octal string representation.  Equivalent to Python oct(). */
inline String<Ref::STEAL> oct(PyObject* obj) {
    PyObject* string = PyNumber_ToBase(integer, 8);
    if (string == nullptr) {
        throw catch_python();
    }
    return String<Ref::STEAL>(string);
}


/* Convert an integer or integer-like object (one that implements __index__()) into a
hexadecimal string representation.  Equivalent to Python hext(). */
inline String<Ref::STEAL> hex(PyObject* obj) {
    PyObject* string = PyNumber_ToBase(integer, 16);
    if (string == nullptr) {
        throw catch_python();
    }
    return String<Ref::STEAL>(string);
}


/* Convert a Python integer into a unicode character.  Can throw if the object is not
an integer, or if it is outside the range for a valid unicode character. */
inline String<Ref::STEAL> chr(PyObject* obj) {
    long long val = PyLong_AsLongLong(obj);
    if (val == -1 && PyErr_Occurred()) {
        throw catch_python();
    }
    return chr(val);
}


/* Convert a C integer into a unicode character.  Can throw if the integer is outside
the range for a valid unicode character. */
inline String<Ref::STEAL> chr(long long val) {
    PyObject* string = PyUnicode_FromFormat("%llc", val);
    if (string == nullptr) {
        throw catch_python();
    }
    return String<Ref::STEAL>(string);
}


/* Convert a unicode character into an integer.  Can throw if the argument is null or
not a string of length 1. */
long long ord(PyObject* obj) {
    if (obj == nullptr) {
        throw TypeError("ord() argument must not be null");
    }

    if (!PyUnicode_Check(obj)) {
        std::ostringstream msg;
        msg << "ord() expected a string of length 1, but " << Py_TYPE(obj)->tp_name;
        msg << "found";
        throw TypeError(msg.str());
    }

    Py_ssize_t length = PyUnicode_GET_LENGTH(obj);
    if (length != 1) {
        std::ostringstream msg;
        msg << "ord() expected a character, but string of length " << length;
        msg << " found";
        throw TypeError(msg.str());
    }

    return PyUnicode_READ_CHAR(obj, 0);
}


//////////////////////
////    OBJECT    ////
//////////////////////


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Object<Ref::BORROW> as_object(PyObject* obj) {
    return Object<Ref::BORROW>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Type<Ref::BORROW> as_object(PyTypeObject* obj) {
    return Type<Ref::BORROW>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Bool<Ref::NEW> as_object(bool obj) {
    return Bool<Ref::NEW>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Int<Ref::NEW> as_object(long long obj) {
    return Int<Ref::STEAL>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Int<Ref::NEW> as_object(unsigned long long obj) {
    return Int<Ref::NEW>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Float<Ref::NEW> as_object(double obj) {
    return Float<Ref::STEAL>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline String<Ref::NEW> as_object(const char* obj) {
    return String<Ref::STEAL>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline String<Ref::NEW> as_object(const std::string& obj) {
    return String<Ref::STEAL>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline String<Ref::NEW> as_object(const std::string_view& obj) {
    return String<Ref::STEAL>(obj);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline Tuple<Ref::NEW> as_object(const std::tuple<T>& obj) {
    return Tuple<Ref::NEW>(obj, size);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline List<Ref::NEW> as_object(const std::vector<T>& obj) {
    return List<Ref::NEW>(obj, size);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline List<Ref::NEW> as_object(const std::list<T>& obj) {
    return List<Ref::NEW>(obj, size);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline Set<Ref::NEW> as_object(const std::unordered_set<T>& obj) {
    return Set<Ref::NEW>(obj, size);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline Set<Ref::NEW> as_object(const std::set<T>& obj) {
    return Set<Ref::NEW>(obj, size);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename K, typename V>
inline Dict<Ref::NEW> as_object(const std::unordered_map<K, V>& obj) {
    return Dict<Ref::NEW>(obj, size);
}


/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename K, typename V>
inline Dict<Ref::NEW> as_object(const std::map<K, V>& obj) {
    return Dict<Ref::NEW>(obj, size);
}


/* Base class for all C++ wrappers around CPython object pointers. */
template <Ref ref = Ref::STEAL>
struct Object {
    PyObject* obj;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to a null pointer. */
    Object() : obj(nullptr) {}

    /* Construct a python::Object around a PyObject* pointer, applying the templated
    adoption rule. */
    explicit Object(PyObject* obj) : obj(obj) {
        if constexpr (ref == Ref::NEW) {
            python::incref(obj);
        }
    }

    /* Copy constructor. */
    template <Ref R>
    Object(const Object<R>& other) : obj(other.obj) {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            python::incref(obj);
        }
    }

    /* Move constructor. */
    template <Ref R>
    Object(Object<R>&& other) : obj(other.obj) {
        static_assert(
            !(ref == Ref::BORROW && (R == Ref::NEW || R == Ref::STEAL)),
            "cannot move an owning reference into a non-owning reference"
        );
        static_assert(
            !(ref == Ref::NEW || ref == Ref::STEAL) && R == Ref::BORROW,
            "cannot move a non-owning reference into an owning reference"
        );
        other.obj = nullptr;
    }

    /* Copy assignment operator. */
    template <Ref R>
    Object& operator=(const Object<R>& other) {
        if constexpr (R == ref) {
            if (this == &other) {
                return *this;
            }
        }
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            python::xdecref(obj);
            python::xincref(other.obj);
        }
        obj = other.obj;
        return *this;
    }

    /* Move assignment operator. */
    template <Ref R>
    Object& operator=(Object<R>&& other) {
        static_assert(
            !(ref == Ref::BORROW && (R == Ref::NEW || R == Ref::STEAL)),
            "cannot move an owning reference into a non-owning reference"
        );
        static_assert(
            !(ref == Ref::NEW || ref == Ref::STEAL) && R == Ref::BORROW,
            "cannot move a non-owning reference into an owning reference"
        )
        if constexpr (R == ref) {
            if (this == &other) {
                return *this;
            }
        }
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            python::xdecref(obj);
        }
        obj = other.obj;
        other.obj = nullptr;
        return *this;
    }

    /* Release the Python object on destruction. */
    ~Object() {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            python::xdecref(obj);
        }
    }

    /* Retrieve the wrapped object and relinquish ownership over it. */
    inline PyObject* unwrap() {
        PyObject* result = obj;
        obj = nullptr;
        return result;
    }

    /* Implicitly convert a python::Object into a PyObject* pointer.  Returns a
    borrowed reference to the python object. */
    inline operator PyObject*() const noexcept {
        return obj;
    }

    ////////////////////////////////
    ////    ATTRIBUTE ACCESS    ////
    ////////////////////////////////

    /* Check if the object has an attribute with the given name. */
    template <typename T>
    inline bool hasattr(T&& attr) const noexcept {
        return python::hasattr(obj, std::forward<T>(attr));
    }

    /* Get an attribute from the object.  Can throw if the attribute does not exist. */
    template <typename T>
    inline Object<Ref::STEAL> getattr(T&& attr) const {
        return python::getattr(obj, std::forward<T>(attr));
    }

    /* Set an attribute on the object.  Can throw if the attribute cannot be set. */
    template <typename T>
    inline void setattr(T&& attr, PyObject* value) {
        python::setattr(obj, std::forward<T>(attr), value);
    }

    /* Delete an attribute from the object.  Can throw if the attribute cannot be
    deleted. */
    template <typename T>
    inline void delattr(T&& attr) {
        python::delattr(obj, std::forward<T>(attr));
    }

    /* Get a list of strings representing named attributes of the object. */
    inline List<Ref::STEAL> dir() const {
        return python::dir(obj);
    }

    /////////////////////////////
    ////    CALL PROTOCOL    ////
    /////////////////////////////

    /* Check if a Python object is callable.  Equivalent to Python callable(). */
    inline bool callable() const noexcept {
        return python::callable(obj);
    }

    /* Call the object using C-style positional arguments.  Returns a new reference. */
    template <typename... Args>
    Object<Ref::STEAL> operator()(Args&&... args) const {
        if constexpr (sizeof...(Args) == 0) {
            PyObject* result = PyObject_CallNoArgs(obj);
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);

        } else if constexpr (sizeof...(Args) == 1) {
            PyObject* result = PyObject_CallOneArg(obj, std::forward<Args>(args)...);
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);

        } else {
            PyObject* result = PyObject_CallFunctionObjArgs(
                obj, std::forward<Args>(args)..., nullptr
            );
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);
        }
    }

    /* Call the object using Python-style positional arguments.  Returns a new
    reference. */
    inline Object<Ref::STEAL> call(PyObject* args) const {
        PyObject* result = PyObject_CallObject(obj, args);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    /* Call the object using Python-style positional and keyword arguments.  Returns a
    new reference. */
    inline Object<Ref::STEAL> call(PyObject* args, PyObject* kwargs) const {
        PyObject* result = PyObject_Call(obj, args, kwargs);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    /* Call the object using Python's vectorcall protocol.  Returns a new reference. */
    inline Object<Ref::STEAL> call(
        PyObject* const* args,
        size_t npositional,
        PyObject* kwnames
    ) const {
        PyObject* result = PyObject_Vectorcall(obj, args, npositional, kwnames);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    /////////////////////////////
    ////    TYPE CHECKING    ////
    /////////////////////////////

    /* Get the type of a Python object. */
    inline Type<Ref::NEW> type() const noexcept {
        return Type<Ref::NEW>(obj);
    }

    /* Check whether this object is an instance of the given type. */
    template <typename T>
    inline bool isinstance(T&& type) const noexcept {
        return python::isinstance(obj, std::forward<T>(type));
    }

    /* Check whether this object is a subclass of the given type. */
    template <typename T>
    inline bool issubclass(T&& base) const noexcept {
        return python::issubclass(obj, std::forward<T>(base));
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the object. */
    class Element {
        friend Object;
        PyObject* obj;
        PyObject* key;

        Element(PyObject* obj, PyObject* key) : obj(obj), key(key) {}

    public:

        /* Get the item with the specified key.  Can throw if the object does not
        support the `__getitem__()` method. */
        inline Object<Ref::STEAL> get() const {
            PyObject* result = PyObject_GetItem(obj, key);
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);
        }

        /* Set the item with the specified key.  Releases a reference to the previous
        value in case of an error, and then holds a reference to the new value.  Can
        throw if the object does not support the `__getitem__()` method. */
        inline void set(PyObject* value) {
            if (PyObject_SetItem(obj, key, value)) {
                throw catch_python();
            }
        }

        /* Delete the item with the specified key.  Releases a reference to the value.
        Can throw if the object does not support the `__delitem__()` method. */
        inline void del() {
            if (PyObject_DelItem(obj, key)) {
                throw catch_python();
            }
        }

    };

    /* Index into a Python object, returning a proxy that follows the sequence
    protocol. */
    inline Element operator[](PyObject* key) noexcept {
        return Element(obj, key);
    }

    /* Index into a Python object, returning a proxy that follows the sequence
    protocol. */
    inline const Element operator[](PyObject* key) const noexcept {
        return Element(obj, key);
    }

    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    inline Iterator<identity> begin() const {
        PyObject* iter = PyObject_GetIter(obj);
        if (iter == nullptr) {
            throw catch_python();
        }
        return {iter, identity()};
    }

    inline Iterator<identity> rbegin() const {
        PyObject* attr = PyObject_GetAttrString(obj, "__reversed__");
        if (attr == nullptr && PyErr_Occurred()) {
            throw catch_python();
        }

        PyObject* iter = PyObject_CallNoArgs(attr);
        Py_DECREF(attr);
        if (iter == nullptr && PyErr_Occurred()) {
            throw catch_python();
        }
        return {iter, identity()};
    }

    inline Iterator<identity> cbegin() const {
        return begin();
    }

    inline Iterator<identity> crbegin() const {
        return rbegin();
    }

    template <typename Convert>
    inline Iterator<std::decay_t<Convert>> begin(Convert&& convert) {
        PyObject* iter = PyObject_GetIter(obj);
        if (iter == nullptr) {
            throw catch_python();
        }
        return {iter, std::forward<Convert>(convert)};
    }

    template <typename Convert>
    inline Iterator<std::decay_t<Convert>> rbegin(Convert&& convert) const {
        PyObject* attr = PyObject_GetAttrString(obj, "__reversed__");
        if (attr == nullptr && PyErr_Occurred()) {
            throw catch_python();
        }

        PyObject* iter = PyObject_CallNoArgs(attr);
        Py_DECREF(attr);
        if (iter == nullptr && PyErr_Occurred()) {
            throw catch_python();
        }
        return {iter, std::forward<Convert>(convert)};
    }

    template <typename Convert>
    inline Iterator<std::decay_t<Convert>> cbegin(Convert&& convert) {
        return begin(std::forward<Convert>(convert));
    }

    template <typename Convert>
    inline Iterator<std::decay_t<Convert>> crbegin(Convert&& convert) const {
        return rbegin(std::forward<Convert>(convert));
    }

    inline Iterator<identity> end() const { return {identity()}; }
    inline Iterator<identity> rend() const { return {identity()}; }
    inline Iterator<identity> cend() const { return end(); }
    inline Iterator<identity> crend() const { return rend(); }

    ///////////////////////////////
    ////    UNARY OPERATORS    ////
    ///////////////////////////////

    /* Get the next item from an iterator.  The object must be an iterator. */
    inline Object<Ref::STEAL> next() const {
        return python::next(obj);
    }

    /* Get the next item from an iterator, or a default value if the iterator is
    exhausted.  The object must be an iterator.  Borrows a reference to the default
    value. */
    inline Object<Ref::STEAL> next(PyObject* default_value) const {
        return python::next(obj, default_value);
    }

    /* Get the length of the object.  Returns nullopt if the object does not support the
    sequence protocol. */
    inline std::optional<size_t> len() const {
        return python::len(obj);
    }

    /* Check if a Python object is considered truthy.  Equivalent to Python bool(). */
    inline bool truthy() const {
        return python::truthy(obj);
    }

    /* Check if a Python object is considered falsy.  Equivalent to Python `not`
    expression. */
    inline bool falsy() const {
        return python::falsy(obj);
    }

    /* Get the hash of the object.  Can throw if the object does not support hashing. */
    inline size_t hash() const {
        return python::hash(obj);
    }

    inline Object<Ref::STEAL> operator+() const {
        PyObject* result = PyNumber_Positive(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object<Ref::STEAL> operator-() const {
        return python::neg(obj);
    }

    inline Object<Ref::STEAL> operator~() const {
        return python::invert(obj);
    }

    /* Get the absolute value of the object.  Can throw if the object is not numeric. */
    inline Object<Ref::STEAL> abs() const {
        return python::abs(obj);
    }

    /* Get a string representation of a Python object.  Equivalent to Python str(). */
    inline String<Ref::STEAL> str() const {
        return python::str(obj);
    }

    /* Get a string representation of a Python object.  Equivalent to Python repr(). */
    inline String<Ref::STEAL> repr() const {
        return python::repr(obj);
    }

    /* Get a string representation of a Python object with non-ASCII characters escaped.
    Equivalent to Python ascii(). */
    inline String<Ref::STEAL> ascii() const {
        return python::ascii(obj);
    }

    /* Convert an integer or integer-like object (one that implements __index__()) into
    a binary string representation.  Equivalent to Python bin(). */
    inline String<Ref::STEAL> bin() const {
        return python::bin(obj);
    }

    /* Convert an integer or integer-like object (one that implements __index__()) into
    an octal string representation.  Equivalent to Python oct(). */
    inline String<Ref::STEAL> oct() const {
        return python::oct(obj);
    }

    /* Convert an integer or integer-like object (one that implements __index__()) into
    a hexadecimal string representation.  Equivalent to Python hext(). */
    inline String<Ref::STEAL> hex() const {
        return python::hex(obj);
    }

    /* Convert a Python integer into a unicode character.  Can throw if the object is
    not an integer, or if it is outside the range for a valid unicode character. */
    inline String<Ref::STEAL> chr() const {
        return python::chr(obj);
    }

    /* Convert a unicode character into an integer.  Can throw if the argument is null
    or not a string of length 1. */
    inline long long ord() const {
        return python::ord(obj);
    }

    ////////////////////////////////
    ////    BINARY OPERATORS    ////
    ////////////////////////////////

    /* Apply a Python-level identity comparison to the object.  Equivalent to
    `obj is other`.  Always succeeds. */
    inline bool is(PyObject* other) const noexcept {
        return obj == other;
    }

    /* Check whether the object contains a given value.  Equivalent to `value in obj`.
    Can throw if the object does not support the sequence protocol. */
    inline bool contains(PyObject* value) const {
        int result = PyObject_Contains(obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator<(T&& other) const {
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_LT
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator<=(T&& other) const {
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_LE
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator==(T&& other) const {
        if constexpr (std::is_pointer_v<std::decay_t<T>>) {
            if (obj == other) {
                return true;
            }
        }
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_EQ
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator!=(T&& other) const {
        if constexpr (std::is_pointer_v<std::decay_t<T>>) {
            if (obj == other) {
                return false;
            }
        }
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_NE
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator>=(T&& other) const {
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_GE
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator>(T&& other) const {
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_GT
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    ///////////////////////////////
    ////    NUMBER PROTOCOL    ////
    ///////////////////////////////

    template <typename T>
    inline Object<Ref::STEAL> operator+(T&& other) const {
        PyObject* result = PyNumber_Add(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator+=(T&& other) {
        PyObject* result = PyNumber_InPlaceAdd(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator-(T&& other) const {
        PyObject* result = PyNumber_Subtract(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator-=(T&& other) {
        PyObject* result = PyNumber_InPlaceSubtract(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator*(T&& other) const {
        PyObject* result = PyNumber_Multiply(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator*=(T&& other) {
        PyObject* result = PyNumber_InPlaceMultiply(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> matrix_multiply(T&& other) const {
        PyObject* result = PyNumber_MatrixMultiply(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& inplace_matrix_multiply(T&& other) {
        PyObject* result = PyNumber_InPlaceMatrixMultiply(
            obj, as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator/(T&& other) const {
        PyObject* result = PyNumber_TrueDivide(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator/=(T&& other) {
        PyObject* result = PyNumber_InPlaceTrueDivide(
            obj, as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> floor_divide(T&& other) const {
        PyObject* result = PyNumber_FloorDivide(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& inplace_floor_divide(T&& other) {
        PyObject* result = PyNumber_InPlaceFloorDivide(
            obj, as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator%(T&& other) const {
        PyObject* result = PyNumber_Remainder(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator%=(T&& other) {
        PyObject* result = PyNumber_InPlaceRemainder(
            obj, as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> divmod(T&& other) const {
        PyObject* result = PyNumber_Divmod(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object<Ref::STEAL> power(T&& other) const {
        PyObject* result = PyNumber_Power(
            obj, as_object(std::forward<T>(other)), Py_None
        );
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& inplace_power(T&& other) {
        PyObject* result = PyNumber_InPlacePower(
            obj, as_object(std::forward<T>(other)), Py_None
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator<<(T&& other) const {
        PyObject* result = PyNumber_Lshift(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator<<=(T&& other) {
        PyObject* result = PyNumber_InPlaceLshift(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator>>(T&& other) const {
        PyObject* result = PyNumber_Rshift(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator>>=(T&& other) {
        PyObject* result = PyNumber_InPlaceRshift(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator|(T&& other) const {
        PyObject* result = PyNumber_Or(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator|=(T&& other) {
        PyObject* result = PyNumber_InPlaceOr(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator&(T&& other) const {
        PyObject* result = PyNumber_And(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator&=(T&& other) {
        PyObject* result = PyNumber_InPlaceAnd(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator^(T&& other) const {
        PyObject* result = PyNumber_Xor(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    template <typename T>
    inline Object& operator^=(T&& other) {
        PyObject* result = PyNumber_InPlaceXor(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

};


/////////////////////
////    TYPES    ////
/////////////////////


/* An extension of python::Object that represents a Python type. */
template <Ref ref = Ref::STEAL>
class Type : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python type from an existing CPython type. */
    explicit Type(PyTypeObject* type) : Base(reinterpret_cast<PyObject*>(type)) {
        if (!PyType_Check(this->obj)) {
            throw TypeError("expected a type");
        }
    }

    /* Get the type of an arbitrary Python object. */
    explicit Type(PyObject* obj) : Base([&] {
        if (obj == nullptr) {
            throw TypeError("expected a type");
        }
        if constexpr (ref == Ref::STEAL) {
            return Py_NewRef(Py_TYPE(obj));
        } else {
            return Py_TYPE(obj);
        }
    }()) {}

    /* Create a new dynamic type by calling Python's type() function. */
    Type(const char* name, PyObject* bases, PyObject* dict) :
        Base(PyType_Type.tp_new(&PyType_Type, nullptr, nullptr))
    {
        if (this->obj == nullptr) {
            throw catch_python();
        }

        PyObject* args = PyTuple_New(3);
        if (args == nullptr) {
            throw catch_python();
        }

        PyTuple_SET_ITEM(args, 0, PyUnicode_FromString(name.c_str()));
        PyTuple_SET_ITEM(args, 1, bases);
        PyTuple_SET_ITEM(args, 2, dict);

        PyObject* result = PyObject_CallObject(Base::obj, args);
        Py_DECREF(args);
        if (result == nullptr) {
            throw catch_python();
        }

        Base::obj = result;
    }

    /* Implicitly convert a python::Type into a PyTypeObject* pointer.  Returns a
    borrowed reference. */
    inline operator PyTypeObject*() const noexcept {
        return reinterpret_cast<PyTypeObject*>(this->obj);
    }

    /*  */

};


///////////////////////////////////
////    FUNCTIONS & METHODS    ////
///////////////////////////////////


/* An extension of python::Object that represents a Python function. */
template <Ref ref = Ref::STEAL>
class Function : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python function from an existing CPython function. */
    Function(PyObject* obj) : Base(obj) {
        if (!PyFunction_Check(obj)) {
            throw TypeError("expected a function");
        }
    }

    /* Copy constructor. */
    Function(const Function& other) : Base(other) {}

    /* Move constructor. */
    Function(Function&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Function& operator=(const Function& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Function& operator=(Function&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ////////////////////////////////////
    ////    PyFunction_* METHODS    ////
    ////////////////////////////////////

    /* Implicitly convert a python::List into a PyFunctionObject* pointer. */
    inline operator PyFunctionObject*() const noexcept {
        return reinterpret_cast<PyFunctionObject*>(this->obj);
    }

    /* Get the function's code object. */
    inline Code<Ref::BORROW> code() const noexcept {
        return Code<Ref::BORROW>(PyFunction_GetCode(this->obj));
    }

    /* Get the function's base name. */
    inline std::string name() const {
        return code().name();
    }

    /* Get the function's qualified name. */
    inline std::string qualname() const {
        return code().qualname();
    }

    /* Get the total number of positional arguments for the function, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return code().n_args();
    }

    /* Get the number of positional-only arguments for the function, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t positional_only() const noexcept {
        return code().positional_only();
    }

    /* Get the number of keyword-only arguments for the function, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t keyword_only() const noexcept {
        return code().keyword_only();
    }

    /* Get the number of local variables used by the function (including all
    parameters). */
    inline size_t n_locals() const noexcept {
        return code().n_locals();
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string file_name() const {
        return code().file_name();
    }

    /* Get the first line number of the function. */
    inline size_t line_number() const noexcept {
        return code().line_number();
    }

    /* Get the required stack space for the code object. */
    inline size_t stack_size() const noexcept {
        return code().stack_size();
    }

    /* Get the globals dictionary associated with the function object. */
    inline Dict<Ref::BORROW> globals() const noexcept {
        return Dict<Ref::BORROW>(PyFunction_GetGlobals(this->obj));
    }

    /* Get the module that the function is defined in. */
    inline std::optional<Object<Ref::BORROW>> module() const noexcept {
        PyObject* module = PyFunction_GetModule(this->obj);
        if (module == nullptr) {
            return std::nullopt;
        } else {
            return std::make_optional(Object<Ref::BORROW>(module));
        }
    }

    /* Get the default values for the function's arguments. */
    inline std::optional<Tuple<Ref::BORROW>> defaults() const noexcept {
        PyObject* defaults = PyFunction_GetDefaults(this->obj);
        if (defaults == nullptr) {
            return std::nullopt;
        } else {
            return std::make_optional(Tuple<Ref::BORROW>(defaults));
        }
    }

    /* Set the default values for the function's arguments.  Input must be Py_None or
    a tuple. */
    inline void defaults(PyObject* defaults) {
        if (PyFunction_SetDefaults(this->obj, defaults)) {
            throw catch_python();
        }
    }

    /* Get the closure associated with the function.  This is a tuple of cell objects
    containing data captured by the function. */
    inline std::optional<Tuple<Ref::BORROW>> closure() const noexcept {
        PyObject* closure = PyFunction_GetClosure(this->obj);
        if (closure == nullptr) {
            return std::nullopt;
        } else {
            return std::make_optional(Tuple<Ref::BORROW>(closure));
        }
    }

    /* Set the closure associated with the function.  Input must be Py_None or a
    tuple. */
    inline void closure(PyObject* closure) {
        if (PyFunction_SetClosure(this->obj, closure)) {
            throw catch_python();
        }
    }

    /* Get the annotations for the function object.  This is a mutable dictionary or
    nullopt if no annotations are present. */
    inline std::optional<Dict<Ref::BORROW>> annotations() const noexcept {
        PyObject* annotations = PyFunction_GetAnnotations(this->obj);
        if (annotations == nullptr) {
            return std::nullopt;
        } else {
            return std::make_optional(Dict<Ref::BORROW>(annotations));
        }
    }

    /* Set the annotations for the function object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(PyObject* annotations) {
        if (PyFunction_SetAnnotations(this->obj, annotations)) {
            throw catch_python();
        }
    }

};


/* An extension of python::Object that represents a bound Python method. */
template <Ref ref = Ref::STEAL>
class Method : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python method from an existing CPython method. */
    Method(PyObject* obj) : Base(obj) {
        if (!PyMethod_Check(obj)) {
            throw TypeError("expected a method");
        }
    }

    /* Copy constructor. */
    Method(const Method& other) : Base(other) {}

    /* Move constructor. */
    Method(Method&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Method& operator=(const Method& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Method& operator=(Method&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    //////////////////////////////////
    ////    PyMethod_* METHODS    ////
    //////////////////////////////////

    /* Get the instance to which the method is bound. */
    inline Object<Ref::BORROW> self() const noexcept {
        return Object<Ref::BORROW>(PyMethod_GET_SELF(this->obj));
    }

    /* Get the function object associated with the method. */
    inline Function<Ref::BORROW> function() const noexcept {
        return Function<Ref::BORROW>(PyMethod_GET_FUNCTION(this->obj));
    }

    /* Get the code object wrapped by this method. */
    inline Code<Ref::BORROW> code() const noexcept {
        return function().code();
    }

    /* Get the method's base name. */
    inline std::string name() const {
        return function().name();
    }

    /* Get the method's qualified name. */
    inline std::string qualname() const {
        return function().qualname();
    }

    /* Get the total number of positional arguments for the method, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return function().n_args();
    }

    /* Get the number of positional-only arguments for the method, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t positional_only() const noexcept {
        return function().positional_only();
    }

    /* Get the number of keyword-only arguments for the method, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t keyword_only() const noexcept {
        return function().keyword_only();
    }

    /* Get the number of local variables used by the method (including all
    parameters). */
    inline size_t n_locals() const noexcept {
        return function().n_locals();
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string file_name() const {
        return function().file_name();
    }

    /* Get the first line number of the method. */
    inline size_t line_number() const noexcept {
        return function().line_number();
    }

    /* Get the required stack space for the code object. */
    inline size_t stack_size() const noexcept {
        return function().stack_size();
    }

    /* Get the globals dictionary associated with the method object. */
    inline Dict<Ref::BORROW> globals() const noexcept {
        return function().globals();
    }

    /* Get the module that the method is defined in. */
    inline std::optional<Object<Ref::BORROW>> module() const noexcept {
        return function().module();
    }

    /* Get the default values for the method's arguments. */
    inline std::optional<Tuple<Ref::BORROW>> defaults() const noexcept {
        return function().defaults();
    }

    /* Set the default values for the method's arguments.  Input must be Py_None or a
    tuple. */
    inline void defaults(PyObject* defaults) {
        function().defaults(defaults);
    }

    /* Get the closure associated with the method.  This is a tuple of cell objects
    containing data captured by the method. */
    inline std::optional<Tuple<Ref::BORROW>> closure() const noexcept {
        return function().closure();
    }

    /* Set the closure associated with the method.  Input must be Py_None or a tuple. */
    inline void closure(PyObject* closure) {
        function().closure(closure);
    }

    /* Get the annotations for the method object.  This is a mutable dictionary or
    nullopt if no annotations are present. */
    inline std::optional<Dict<Ref::BORROW>> annotations() const noexcept {
        return function().annotations();
    }

    /* Set the annotations for the method object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(PyObject* annotations) {
        function().annotations(annotations);
    }

};


/* An extension of python::Object that represents a Python class method. */
template <Ref ref = Ref::STEAL>
class ClassMethod : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python class method from an existing CPython class method. */
    ClassMethod(PyObject* obj) : Base(obj) {
        if (!PyInstanceMethod_Check(obj)) {
            throw TypeError("expected a class method");
        }
    }

    /* Copy constructor. */
    ClassMethod(const ClassMethod& other) : Base(other) {}

    /* Move constructor. */
    ClassMethod(ClassMethod&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    ClassMethod& operator=(const ClassMethod& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    ClassMethod& operator=(ClassMethod&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    //////////////////////////////////////////
    ////    PyInstanceMethod_* METHODS    ////
    //////////////////////////////////////////

    /* Get the function object associated with the class method. */
    inline Function<Ref::BORROW> function() const noexcept {
        return Function<Ref::BORROW>(PyInstanceMethod_GET_FUNCTION(this->obj));
    }

    /* Get the code object wrapped by this class method. */
    inline Code<Ref::BORROW> code() const noexcept {
        return function().code();
    }

    /* Get the class method's base name. */
    inline std::string name() const {
        return function().name();
    }

    /* Get the class method's qualified name. */
    inline std::string qualname() const {
        return function().qualname();
    }

    /* Get the total number of positional arguments for the class method, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return function().n_args();
    }

    /* Get the number of positional-only arguments for the class method, including
    those with default values.  Does not include variable positional or keyword
    arguments. */
    inline size_t positional_only() const noexcept {
        return function().positional_only();
    }

    /* Get the number of keyword-only arguments for the class method, including those
    with default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t keyword_only() const noexcept {
        return function().keyword_only();
    }

    /* Get the number of local variables used by the class method (including all
    parameters). */
    inline size_t n_locals() const noexcept {
        return function().n_locals();
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string file_name() const {
        return function().file_name();
    }

    /* Get the first line number of the class method. */
    inline size_t line_number() const noexcept {
        return function().line_number();
    }

    /* Get the required stack space for the code object. */
    inline size_t stack_size() const noexcept {
        return function().stack_size();
    }

    /* Get the globals dictionary associated with the class method object. */
    inline Dict<Ref::BORROW> globals() const noexcept {
        return function().globals();
    }

    /* Get the module that the class method is defined in. */
    inline std::optional<Object<Ref::BORROW>> module() const noexcept {
        return function().module();
    }

    /* Get the default values for the class method's arguments. */
    inline std::optional<Tuple<Ref::BORROW>> defaults() const noexcept {
        return function().defaults();
    }

    /* Set the default values for the class method's arguments.  Input must be Py_None
    or a tuple. */
    inline void defaults(PyObject* defaults) {
        function().defaults(defaults);
    }

    /* Get the closure associated with the class method.  This is a tuple of cell
    objects containing data captured by the class method. */
    inline std::optional<Tuple<Ref::BORROW>> closure() const noexcept {
        return function().closure();
    }

    /* Set the closure associated with the class method.  Input must be Py_None or a
    tuple. */
    inline void closure(PyObject* closure) {
        function().closure(closure);
    }

    /* Get the annotations for the class method object.  This is a mutable dictionary
    or nullopt if no annotations are present. */
    inline std::optional<Dict<Ref::BORROW>> annotations() const noexcept {
        return function().annotations();
    }

    /* Set the annotations for the class method object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(PyObject* annotations) {
        function().annotations(annotations);
    }

};


///////////////////////
////    MODULES    ////
///////////////////////


/* An extension of python::Object that represents a Python module. */
template <Ref ref = Ref::STEAL>
class Module : public Object<ref> {
    using Base = Object<ref>;

public:


};


//////////////////////////
////    REFLECTION    ////
//////////////////////////


/* An extension of python::Object that represents a bytecode execution frame. */
template <Ref ref = Ref::STEAL>
class Frame : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;



};


/* Get the current frame's builtin namespace as a reference-counted dictionary.  Can
throw if no frame is currently executing. */
inline Dict<Ref::NEW> builtins() {
    PyObject* result = PyEval_GetBuiltins();
    if (result == nullptr) {
        throw catch_python();
    }
    return Dict<Ref::NEW>(result);
}


/* Get the current frame's global namespace as a reference-counted dictionary.  Can
throw if no frame is currently executing. */
inline Dict<Ref::NEW> globals() {
    PyObject* result = PyEval_GetGlobals();
    if (result == nullptr) {
        throw catch_python();
    }
    return Dict<Ref::NEW>(result);
}


/* Get the current frame's local namespace as a reference-counted dictionary.  Can
throw if no frame is currently executing. */
inline Dict<Ref::NEW> locals() {
    PyObject* result = PyEval_GetLocals();
    if (result == nullptr) {
        throw catch_python();
    }
    return Dict<Ref::NEW>(result);
}


// TODO: create PyFrameObject wrapper.

/* Return the current thread's execution frame.  Automatically handles reference
counts.  Can throw if no frame is currently executing. */
inline Frame<Ref::NEW> frame() {
    PyFrameObject* result = PyEval_GetFrame();
    if (result == nullptr) {
        throw catch_python();
    }
    return Frame<Ref::NEW>(result);
}


/* Return the name of `func` if it is a callable function, class or instance object.
Otherwise, return type(func).__name__. */
inline std::string func_name(PyObject* func) {
    if (func == nullptr) {
        throw TypeError("func_name() argument must not be null");
    }
    return std::string(PyEval_GetFuncName(func));
}


/* Return a string describing the kind of function that was passed in.  Return values
include "()" for functions and methods, " constructor", " instance", and " object".
When concatenated with func_name(), the result will be a description of `func`. */
inline std::string func_kind(PyObject* func) {
    if (func == nullptr) {
        throw TypeError("func_kind() argument must not be null");
    }
    return std::string(PyEval_GetFuncDesc(func));
}


///////////////////////
////    NUMBERS    ////
///////////////////////


/* An extension of python::Object that represents a Python boolean. */
template <Ref ref = Ref::STEAL>
class Bool : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python boolean from an existing CPython boolean. */
    explicit Bool(PyObject* obj) : Base(obj) {
        if (!PyBool_Check(obj)) {
            throw TypeError("expected a boolean");
        }
    }

    /* Construct a Python boolean from a C++ boolean. */
    Bool(long value) : Base(PyBool_FromLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Bool from a long int requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Get the Python boolean type. */
    static Type<Ref::NEW> type() noexcept {
        return Type<Ref::NEW>(&PyBool_Type);
    }

    inline operator bool() const noexcept {
        return PyLong_AsLong(this->obj);
    }

};


/* An extension of python::Object that represents a Python integer. */
template <Ref ref = Ref::STEAL>
class Int : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python integer from an existing CPython integer. */
    explicit Int(PyObject* obj) : Base(obj) {
        if (!PyLong_Check(obj)) {
            throw TypeError("expected an integer");
        }
    }

    /* Construct a Python integer from a C long. */
    Int(long value) : Base(PyLong_FromLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a long requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
    }

    /* Construct a Python integer from a C long long. */
    Int(long long value) : Base(PyLong_FromLongLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a long long requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C unsigned long. */
    Int(unsigned long value) : Base(PyLong_FromUnsignedLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from an unsigned long requires the use of Ref::STEAL "
            "to avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C unsigned long long. */
    Int(unsigned long long value) : Base(PyLong_FromUnsignedLongLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from an unsigned long long requires the use of "
            "Ref::STEAL to avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C double. */
    Int(double value) : Base(PyLong_FromDouble(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a double requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C string. */
    Int(const char* value, int base) :
        Base(PyLong_FromString(value, nullptr, base))
    {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a string requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C++ string. */
    Int(const std::string& value, int base) :
        Base(PyLong_FromString(value.c_str(), nullptr, base))
    {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a string requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C++ string view. */
    Int(const std::string_view& value, int base) :
        Base(PyLong_FromString(value.data(), nullptr, base))
    {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a string view requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a PyUnicode string. */
    Int(PyObject* value, int base) : Base(PyLong_FromUnicodeObject(value, base)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a PyUnicode object requires the use of "
            "Ref::STEAL to avoid memory leaks"
        );
    }

    /* Implicitly convert a python::List into a PyLongObject* pointer. */
    inline operator PyLongObject*() const noexcept {
        return reinterpret_cast<PyLongObject*>(this->obj);
    }

    /* Implicitly convert a python::Int into a C long. */
    inline operator long() const {
        long value = PyLong_AsLong(this->obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

    /* Implicitly convert a python::Int into a C long long. */
    inline operator long long() const {
        long long value = PyLong_AsLongLong(this->obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

    /* Implicitly convert a python::Int into a C unsigned long. */
    inline operator unsigned long() const {
        unsigned long value = PyLong_AsUnsignedLong(this->obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

    /* Implicitly convert a python::Int into a C unsigned long long. */
    inline operator unsigned long long() const {
        unsigned long long value = PyLong_AsUnsignedLongLong(this->obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

};


/* An extension of python::Object that represents a Python float. */
template <Ref ref = Ref::STEAL>
class Float : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python float from an existing CPython float. */
    Float(PyObject* obj) : Base(obj) {
        if (!PyFloat_Check(obj)) {
            throw TypeError("expected a float");
        }
    }

    /* Construct a Python float from a C double. */
    Float(double value) : Base(PyFloat_FromDouble(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Float from a double requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python float from a C++ string. */
    Float(const std::string& value) : Base([&value] {
        PyObject* string = PyUnicode_FromStringAndSize(value.c_str(), value.size());
        if (string == nullptr) {
            throw catch_python();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        return result;
    }()) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Float from a string requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python float from a C++ string view. */
    Float(const std::string_view& value) : Base([&value] {
        PyObject* string = PyUnicode_FromStringAndSize(value.data(), value.size());
        if (string == nullptr) {
            throw catch_python();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        return result;
    }()) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Float from a string view requires the use of Ref::STEAL "
            "to avoid memory leaks"
        );
    }

    /* Copy constructor. */
    Float(const Float& other) : Base(other) {}

    /* Move constructor. */
    Float(Float&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Float& operator=(const Float& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Float& operator=(Float&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a python::List into a PyFloatObject* pointer. */
    inline operator PyFloatObject*() const noexcept {
        return reinterpret_cast<PyFloatObject*>(this->obj);
    }

    /* Implicitly convert a python::Float into a C double. */
    inline operator double() const {
        return PyFloat_AS_DOUBLE(this->obj);
    }

};


/* An extension of python::Object that represents a complex number in Python. */
template <Ref ref = Ref::STEAL>
class Complex : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python complex number from an existing CPython complex number. */
    Complex(PyObject* obj) : Base(obj) {
        if (!PyComplex_Check(obj)) {
            throw TypeError("expected a complex number");
        }
    }

    /* Construct a Python complex number from separate real/imaginary components as
    doubles. */
    Complex(double real, double imag) : Base(PyComplex_FromDoubles(real, imag)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Complex from a double requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Copy constructor. */
    Complex(const Complex& other) : Base(other) {}

    /* Move constructor. */
    Complex(Complex&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Complex& operator=(const Complex& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Complex& operator=(Complex&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a python::List into a PyComplexObject* pointer. */
    inline operator PyComplexObject*() const noexcept {
        return reinterpret_cast<PyComplexObject*>(this->obj);
    }

    /* Get the real component of the complex number as a C double. */
    inline double real() const {
        return PyComplex_RealAsDouble(this->obj);
    }

    /* Get the imaginary component of the complex number as a C double. */
    inline double imag() const {
        return PyComplex_ImagAsDouble(this->obj);
    }

    /* Implicitly convert a python::Complex into a C double representing the real
    component. */
    inline operator double() const {
        return real();
    }

};


/* An extension of python::Object that represents a Python slice. */
template <Ref ref = Ref::STEAL>
class Slice : public Object<ref> {
    using Base = Object<ref>;

    PyObject* _start;
    PyObject* _stop;
    PyObject* _step;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python slice. */
    Slice() :
        Base(PySlice_New(nullptr, nullptr, nullptr)), _start(Py_NewRef(Py_None)),
        _stop(Py_NewRef(Py_None)), _step(Py_NewRef(Py_None))
    {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty Slice requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::Slice around an existing CPython slice. */
    Slice(PyObject* obj) : Base(obj) {
        if (!PySlice_Check(obj)) {
            throw TypeError("expected a slice");
        }

        _start = PyObject_GetAttrString(obj, "start");
        if (_start == nullptr) {
            throw catch_python();
        }
        _stop = PyObject_GetAttrString(obj, "stop");
        if (_stop == nullptr) {
            Py_DECREF(_start);
            throw catch_python();
        }
        _step = PyObject_GetAttrString(obj, "step");
        if (_step == nullptr) {
            Py_DECREF(_start);
            Py_DECREF(_stop);
            throw catch_python();
        }
    }

    /* Copy constructor. */
    Slice(const Slice& other) :
        Base(other), _start(other._start), _stop(other._stop), _step(other._step)
    {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XINCREF(other._start);
            Py_XINCREF(other._stop);
            Py_XINCREF(other._step);
        }
    }

    /* Move constructor. */
    Slice(Slice&& other) :
        Base(std::move(other)), _start(other._start), _stop(other._stop),
        _step(other._step)
     {
        other._start = nullptr;
        other._stop = nullptr;
        other._step = nullptr;
    }

    /* Copy assignment. */
    Slice& operator=(const Slice& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
        _start = other._start;
        _stop = other._stop;
        _step = other._step;
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XINCREF(_start);
            Py_XINCREF(_stop);
            Py_XINCREF(_step);
        }
        return *this;
    }

    /* Move assignment. */
    Slice& operator=(Slice&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
        _start = other._start;
        _stop = other._stop;
        _step = other._step;
        other._start = nullptr;
        other._stop = nullptr;
        other._step = nullptr;
        return *this;
    }

    /* Release the Python slice on destruction. */
    ~Slice() {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
    }

    /////////////////////////////////
    ////    PySlice_* METHODS    ////
    /////////////////////////////////

    /* Get the start index of the slice. */
    inline Object<Ref::BORROW> start() const {
        return Object<Ref::BORROW>(_start);
    }

    /* Get the stop index of the slice. */
    inline Object<Ref::BORROW> stop() const {
        return Object<Ref::BORROW>(_stop);
    }

    /* Get the step index of the slice. */
    inline Object<Ref::BORROW> step() const {
        return Object<Ref::BORROW>(_step);
    }

    /* Normalize the slice for a given sequence length, returning a 4-tuple containing
    the start, stop, step, and number of elements included in the slice. */
    inline auto normalize(Py_ssize_t length) const
        -> std::tuple<Py_ssize_t, Py_ssize_t, Py_ssize_t, size_t>
    {
        Py_ssize_t nstart, nstop, nstep, nlength;
        if (PySlice_GetIndicesEx(this->obj, length, &nstart, &nstop, &nstep, &nlength)) {
            throw catch_python();
        }
        return std::make_tuple(nstart, nstop, nstep, nlength);
    }

};


//////////////////////////
////    CONTAINERS    ////
//////////////////////////


/* An extension of python::Object that represents a Python tuple. */
template <Ref ref = Ref::STEAL>
class Tuple : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python tuple with the specified size. */
    Tuple(Py_ssize_t size) : Base(PyTuple_New(size)) {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty Tuple requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::Tuple around an existing CPython tuple. */
    Tuple(PyObject* obj) : Base(obj) {
        if (!PyTuple_Check(obj)) {
            throw TypeError("expected a tuple");
        }
    }

    /* Copy constructor. */
    Tuple(const Tuple& other) : Base(other) {}

    /* Move constructor. */
    Tuple(Tuple&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Tuple& operator=(const Tuple& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Tuple& operator=(Tuple&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    /////////////////////////////////
    ////    PyTuple_* METHODS    ////
    /////////////////////////////////

    /* Implicitly convert a python::List into a PyTupleObject* pointer. */
    inline operator PyTupleObject*() const noexcept {
        return reinterpret_cast<PyTupleObject*>(this->obj);
    }

    /* Construct a new Python tuple containing the given objects. */
    template <typename... Args>
    inline static Tuple pack(Args&&... args) {
        static_assert(
            ref == Ref::STEAL,
            "pack() must use Ref::STEAL to avoid memory leaks"
        );

        PyObject* tuple = PyTuple_Pack(sizeof...(Args), std::forward<Args>(args)...);
        if (tuple == nullptr) {
            throw catch_python();
        }
        return Tuple<Ref::STEAL>(tuple);
    }

    /* Get the size of the tuple. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyTuple_GET_SIZE(this->obj));
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->obj);
    }

    ////////////////////////////////////
    ////    PySequence_* METHODS    ////
    ////////////////////////////////////

    /* Check if the tuple contains a specific item. */
    inline bool contains(PyObject* value) const noexcept {
        int result = PySequence_Contains(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Get the index of the first occurrence of the specified item. */
    inline size_t index(PyObject* value) const {
        Py_ssize_t result = PySequence_Index(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    /* Count the number of occurrences of the specified item. */
    inline size_t count(PyObject* value) const {
        Py_ssize_t result = PySequence_Count(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the tuple. */
    class Element {
        PyObject* tuple;
        Py_ssize_t index;

        friend Tuple;

        Element(PyObject* tuple, Py_ssize_t index) : tuple(tuple), index(index) {}

    public:

        /* Get the item at this index.  Returns a new reference. */
        inline Object<Ref::STEAL> get() const {
            PyObject* result = PyTuple_GetItem(tuple, index);
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);
        }

        /* Set the item at this index.  Borrows a reference to the new value and
        releases a previous one if a conflict occurs. */
        inline void set(PyObject* value) {
            if (PyTuple_SetItem(tuple, index, Py_XNewRef(value))) {
                throw catch_python();
            };
        }

    };

    inline Element operator[](size_t index) {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    inline const Element operator[](size_t index) const {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    /* Directly access an item within the tuple, without bounds checking or
    constructing a proxy. */
    inline Object<Ref::BORROW> GET_ITEM(Py_ssize_t index) const {
        return Object<Ref::BORROW>(PyTuple_GET_ITEM(this->obj, index));
    }

    /* Directly set an item within the tuple, without bounds checking or constructing a
    proxy.  Steals a reference to `value` and does not clear the previous item if one is
    present. */
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyTuple_SET_ITEM(this->obj, index, value);
    }

    /* Get a new Tuple representing a slice from this Tuple. */
    inline Tuple<Ref::STEAL> get_slice(size_t start, size_t stop) const {
        if (start > size()) {
            throw IndexError("start index out of range");
        }
        if (stop > size()) {
            throw IndexError("stop index out of range");
        }
        if (start > stop) {
            throw IndexError("start index greater than stop index");
        }
        return Tuple<Ref::STEAL>(PyTuple_GetSlice(this->obj, start, stop));
    }

};


/* An extension of python::Object that represents a Python list. */
template <Ref ref = Ref::STEAL>
class List : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python list of the specified size. */
    List(Py_ssize_t size) : Base(PyList_New(size)) {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty List requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::List around an existing CPython list. */
    List(PyObject* obj) : Base(obj) {
        if (!PyList_Check(obj)) {
            throw TypeError("expected a list");
        }
    }

    /* Copy constructor. */
    List(const List& other) : Base(other) {}

    /* Move constructor. */
    List(List&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    List& operator=(const List& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    List& operator=(List&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ////////////////////////////////
    ////    PyList_* METHODS    ////
    ////////////////////////////////

    /* Implicitly convert a python::List into a PyListObject* pointer. */
    inline operator PyListObject*() const noexcept {
        return reinterpret_cast<PyListObject*>(this->obj);
    }

    /* Get the size of the list. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyList_GET_SIZE(this->obj));
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->obj);
    }

    /* Append an element to a mutable list.  Borrows a reference to the value. */
    inline void append(PyObject* value) {
        if (PyList_Append(this->obj, value)) {
            throw catch_python();
        }
    }

    /* Insert an element into a mutable list at the specified index.  Borrows a
    reference to the value. */
    inline void insert(Py_ssize_t index, PyObject* value) {
        if (PyList_Insert(this->obj, index, value)) {
            throw catch_python();
        }
    }

    /* Sort a mutable list. */
    inline void sort() {
        if (PyList_Sort(this->obj)) {
            throw catch_python();
        }
    }

    /* Reverse a mutable list. */
    inline void reverse() {
        if (PyList_Reverse(this->obj)) {
            throw catch_python();
        }
    }

    /* Return a shallow copy of the list. */
    inline List<Ref::STEAL> copy() const {
        return List<Ref::STEAL>(PyList_GetSlice(this->obj, 0, size()));
    }

    /* Convert the list into an equivalent tuple. */
    inline Tuple<Ref::STEAL> as_tuple() const {
        return Tuple<Ref::STEAL>(PyList_AsTuple(this->obj));
    }

    ////////////////////////////////////
    ////    PySequence_* METHODS    ////
    ////////////////////////////////////

    /* Check if the tuple contains a specific item. */
    inline bool contains(PyObject* value) const noexcept {
        int result = PySequence_Contains(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Get the index of the first occurrence of the specified item. */
    inline size_t index(PyObject* value) const {
        Py_ssize_t result = PySequence_Index(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    /* Count the number of occurrences of the specified item. */
    inline size_t count(PyObject* value) const {
        Py_ssize_t result = PySequence_Count(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* An assignable proxy for a particular index of the list. */
    class Element {
        PyObject* list;
        Py_ssize_t index;

        friend List;

        Element(PyObject* list, Py_ssize_t index) : list(list), index(index) {}

    public:

        /* Get the item at this index.  Returns a new reference. */
        inline Object<Ref::STEAL> get() const {
            PyObject* result = PyList_GetItem(list, index);
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);
        }

        /* Set the item at this index.  Borrows a reference to the new value and
        releases a reference to the previous one if a conflict occurs. */
        inline void set(PyObject* value) {
            if (PyList_SetItem(list, index, Py_XNewRef(value))) {
                throw catch_python();
            };
        }

    };

    inline Element operator[](size_t index) {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    inline const Element operator[](size_t index) const {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    /* Directly access an item within the list, without bounds checking or
    constructing a proxy.  Borrows a reference to the current value. */
    inline Object<Ref::BORROW> GET_ITEM(Py_ssize_t index) const {
        return Object<Ref::BORROW>(PyList_GET_ITEM(this->obj, index));
    }

    /* Directly set an item within the list, without bounds checking or constructing a
    proxy.  Steals a reference to the new value and does not release the previous one
    if a conflict occurs.  This is dangerous, and should only be used when constructing
    a new list, where the current values are known to be empty. */
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyList_SET_ITEM(this->obj, index, value);
    }

    /* Get a new list representing a slice within this list. */
    inline List<Ref::STEAL> get_slice(size_t start, size_t stop) const {
        if (start > size()) {
            throw IndexError("start index out of range");
        }
        if (stop > size()) {
            throw IndexError("stop index out of range");
        }
        if (start > stop) {
            throw IndexError("start index greater than stop index");
        }
        return List<Ref::STEAL>(PyList_GetSlice(this->obj, start, stop));
    }

    /* Set a slice within a mutable list.  Releases references to the current values
    and then borrows new references to the new ones. */
    inline void set_slice(size_t start, size_t stop, PyObject* value) {
        if (start > size()) {
            throw IndexError("start index out of range");
        }
        if (stop > size()) {
            throw IndexError("stop index out of range");
        }
        if (start > stop) {
            throw IndexError("start index greater than stop index");
        }
        if (PyList_SetSlice(this->obj, start, stop, value)) {
            throw catch_python();
        }
    }

};


/* An extension of python::Object that represents a Python set. */
template <Ref ref = Ref::STEAL>
class Set : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python set. */
    Set() : Base(PySet_New(nullptr)) {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty Set requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::Set around an existing CPython set. */
    Set(PyObject* obj) : Base(obj) {
        if (!PyAnySet_Check(obj)) {
            throw TypeError("expected a set");
        }
    }

    /* Copy constructor. */
    Set(const Set& other) : Base(other) {}

    /* Move constructor. */
    Set(Set&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Set& operator=(const Set& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Set& operator=(Set&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    /////////////////////////////
    ////   PySet_* METHODS   ////
    /////////////////////////////

    /* Implicitly convert a python::List into a PySetObject* pointer. */
    inline operator PySetObject*() const noexcept {
        return reinterpret_cast<PySetObject*>(this->obj);
    }

    /* Get the size of the set. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PySet_GET_SIZE(this->obj));
    }

    /* Check if the set contains a particular key. */
    inline bool contains(PyObject* key) const {
        int result = PySet_Contains(this->obj, key);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Add an element to a mutable set.  Borrows a reference to the key. */
    inline void add(PyObject* key) {
        if (PySet_Add(this->obj, key)) {
            throw catch_python();
        }
    }

    /* Remove an element from a mutable set.  Releases a reference to the key */
    inline void remove(PyObject* key) {
        int result = PySet_Discard(this->obj, key);
        if (result == -1) {
            throw catch_python();
        }
        if (result == 0) {
            PyObject* py_repr = PyObject_Repr(key);
            if (py_repr == nullptr) {
                throw catch_python();
            }
            Py_ssize_t size;
            const char* c_repr = PyUnicode_AsUTF8AndSize(py_repr, &size);
            Py_DECREF(py_repr);
            if (c_repr == nullptr) {
                throw catch_python();
            }
            std::string result(c_repr, size);
            throw KeyError(result);
        }
    }

    /* Remove an element from a mutable set if it is present.  Releases a reference to
    the key if found. */
    inline void discard(PyObject* key) {
        if (PySet_Discard(this->obj, key) == -1) {
            throw catch_python();
        }
    }

    /* Remove and return an arbitrary element from a mutable set.  Transfers a
    reference to the caller. */
    inline Object<Ref::STEAL> pop() {
        PyObject* result = PySet_Pop(this->obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    /* Return a shallow copy of the set. */
    inline Set<Ref::STEAL> copy() const {
        return Set<Ref::STEAL>(PySet_New(this->obj));
    }

    /* Remove all elements from a mutable set. */
    inline void clear() {
        if (PySet_Clear(this->obj)) {
            throw catch_python();
        }
    }

};


/* An extension of python::Object that represents a Python dict. */
template <Ref ref = Ref::STEAL>
class Dict : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python dict. */
    Dict() : Base(PyDict_New()) {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty Dict requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::Dict around an existing CPython dict. */
    Dict(PyObject* obj) : Base(obj) {
        if (!PyDict_Check(obj)) {
            throw TypeError("expected a dict");
        }
    }

    /* Copy constructor. */
    Dict(const Dict& other) : Base(other) {}

    /* Move constructor. */
    Dict(Dict&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Dict& operator=(const Dict& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Dict& operator=(Dict&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ////////////////////////////////
    ////    PyDict_* METHODS    ////
    ////////////////////////////////

    /* Implicitly convert a python::List into a PyDictObject* pointer. */
    inline operator PyDictObject*() const noexcept {
        return reinterpret_cast<PyDictObject*>(this->obj);
    }

    /* Get the size of the dict. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyDict_Size(this->obj));
    }

    /* Check if the dict contains a particular key. */
    inline bool contains(PyObject* key) const {
        int result = PyDict_Contains(this->obj, key);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Return a shallow copy of the dictionary. */
    inline Dict<Ref::STEAL> copy() const {
        return Dict<Ref::STEAL>(PyDict_Copy(this->obj));
    }

    /* Remove all elements from a mutable dict. */
    inline void clear() {
        if (PyDict_Clear(this->obj)) {
            throw catch_python();
        }
    }

    /* Get the value associated with a key or set it to the default value if it is not
    already present.  Returns a new reference. */
    inline Object<Ref::STEAL> set_default(PyObject* key, PyObject* default_value) {
        PyObject* result = PyDict_SetDefault(this->obj, key, default_value);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    /* Update this dictionary with another Python mapping, overriding the current
    values on collision.  Borrows a reference to any keys/values that weren't in the
    original dictionary or conflict with those that are already present.  Releases a
    reference to any values that were overwritten. */
    inline void update(PyObject* other) {
        if (PyDict_Merge(this->obj, other, 1)) {
            throw catch_python();
        }
    }

    /* Equivalent to update(), except that the other container is assumed to contain
    key-value pairs of length 2. */
    inline void update_pairs(PyObject* other) {
        if (PyDict_MergeFromSeq2(this->obj, other, 1)) {
            throw catch_python();
        }
    }

    /* Update this dictionary with another Python mapping, keeping the current values
    on collision.  Borrows a reference to any keys/values that weren't in the original
    dictionary. */
    inline void merge(PyObject* other) {
        if (PyDict_Merge(this->obj, other, 0)) {
            throw catch_python();
        }
    }

    /* Equivalent to merge(), except that the other container is assumed to contain
    key-value pairs of length 2. */
    inline void merge_pairs(PyObject* other) {
        if (PyDict_MergeFromSeq2(this->obj, other, 0)) {
            throw catch_python();
        }
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the dict. */
    template <typename Key>
    class Element {
        PyObject* dict;
        Key& key;

        friend Dict;

        /* Construct an Element proxy for the specified key.  Borrows a reference to
        the key. */
        Element(PyObject* dict, Key& key) : dict(dict), key(key) {}
    
    public:

        /* Get the item with the specified key or throw an error if it is not found.
        Returns a new reference. */
        inline Object<Ref::STEAL> get() const {
            PyObject* value;
            if constexpr (std::is_same_v<Key, const char*>) {
                value = PyDict_GetItemString(dict, key);
            } else {
                value = PyDict_GetItem(dict, key);
            }
            if (value == nullptr) {
                PyObject* py_repr = PyObject_Repr(key);
                if (py_repr == nullptr) {
                    throw catch_python();
                }
                Py_ssize_t size;
                const char* c_repr = PyUnicode_AsUTF8AndSize(py_repr, &size);
                Py_DECREF(py_repr);
                if (c_repr == nullptr) {
                    throw catch_python();
                }
                std::string result(c_repr, size);
                throw KeyError(result);
            }
            return Object<Ref::STEAL>(value);
        }

        /* Set the item with the specified key.  Borrows a reference to the new value
        and releases a reference to the previous one if a conflict occurs. */
        inline void set(PyObject* value) {
            if constexpr (std::is_same_v<Key, const char*>) {
                if (PyDict_SetItemString(dict, key, value)) {
                    throw catch_python();
                }
            } else {
                if (PyDict_SetItem(dict, key, value)) {
                    throw catch_python();
                }
            }
        }

        /* Delete the item with the specified key.  Releases a reference to both the
        key and value. */
        inline void del() {
            if constexpr (std::is_same_v<Key, const char*>) {
                if (PyDict_DelItemString(dict, key)) {
                    throw catch_python();
                }
            } else {
                if (PyDict_DelItem(dict, key)) {
                    throw catch_python();
                }
            }
        }

    };

    inline Element<PyObject*> operator[](PyObject* key) {
        return {this->obj, key};
    }

    inline const Element<PyObject*> operator[](PyObject* key) const {
        return {this->obj, key};
    }

    inline Element<const char*> operator[](const char* key) {
        return {this->obj, key};
    }

    inline const Element<const char*> operator[](const char* key) const {
        return {this->obj, key};
    }

    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    /* An iterator that yield key-value pairs rather than just keys. */
    class Iterator {
        PyObject* dict;
        Py_ssize_t pos;  // required by PyDict_Next
        std::pair<PyObject*, PyObject*> curr;

        friend Dict;

        /* Construct a key-value iterator over the dictionary. */
        Iterator(PyObject* dict) : dict(dict), pos(0) {
            if (!PyDict_Next(dict, &pos, &curr.first, &curr.second)) {
                curr.first = nullptr;
                curr.second = nullptr;
            }
        }

        /* Construct an empty iterator to terminate the loop. */
        Iterator() : dict(nullptr), pos(0), curr(nullptr, nullptr) {}

    public:
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = std::pair<PyObject*, PyObject*>;
        using pointer               = value_type*;
        using reference             = value_type&;

        /* Copy constructor. */
        Iterator(const Iterator& other) :
            dict(other.dict), pos(other.pos), curr(other.curr)
        {}

        /* Move constructor. */
        Iterator(Iterator&& other) :
            dict(other.dict), pos(other.pos), curr(std::move(other.curr))
        {
            other.curr.first = nullptr;
            other.curr.second = nullptr;
        }

        /* Get the current key-value pair. */
        inline std::pair<PyObject*, PyObject*> operator*() {
            return curr;
        }

        /* Get the current key-value pair for a const dictionary. */
        inline const std::pair<PyObject*, PyObject*>& operator*() const {
            return curr;
        }

        /* Advance to the next key-value pair. */
        inline Iterator& operator++() {
            if (!PyDict_Next(dict, &pos, &curr.first, &curr.second)) {
                curr.first = nullptr;
                curr.second = nullptr;
            }
            return *this;
        }

        /* Compare to terminate the loop. */
        inline bool operator!=(const Iterator& other) const {
            return curr.first != other.curr.first && curr.second != other.curr.second;
        }

    };

    inline auto begin() { return Iterator(this->obj); }
    inline auto begin() const { return Iterator(this->obj); }
    inline auto cbegin() const { return Iterator(this->obj); }
    inline auto end() { return Iterator(); }
    inline auto end() const { return Iterator(); }
    inline auto cend() const { return Iterator(); }

    /* Return a python::List containing all the keys in this dictionary. */
    inline List<Ref::STEAL> keys() const {
        return List<Ref::STEAL>(PyDict_Keys(this->obj));
    }

    /* Return a python::List containing all the values in this dictionary. */
    inline List<Ref::STEAL> values() const {
        return List<Ref::STEAL>(PyDict_Values(this->obj));
    }

    /* Return a python::List containing all the key-value pairs in this dictionary. */
    inline List<Ref::STEAL> items() const {
        return List<Ref::STEAL>(PyDict_Items(this->obj));
    }

};


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
template <Ref ref = Ref::STEAL>
class FastSequence : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a PySequence from an iterable or other sequence. */
    FastSequence(PyObject* obj) : Base(obj) {
        if (!PyTuple_Check(obj) && !PyList_Check(obj)) {
            throw TypeError("expected a tuple or list");
        }
    }

    /* Copy constructor. */
    FastSequence(const FastSequence& other) : Base(other) {}

    /* Move constructor. */
    FastSequence(FastSequence&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    FastSequence& operator=(const FastSequence& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    FastSequence& operator=(FastSequence&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    /////////////////////////////////////////
    ////    PySequence_Fast_* METHODS    ////
    /////////////////////////////////////////

    /* Get the size of the sequence. */
    inline size_t size() const {
        return static_cast<size_t>(PySequence_Fast_GET_SIZE(this->obj));
    }

    /* Get underlying PyObject* array. */
    inline PyObject** data() const {
        return PySequence_Fast_ITEMS(this->obj);
    }

    /* Directly get an item within the sequence without boundschecking.  Returns a
    borrowed reference. */
    inline PyObject* GET_ITEM(Py_ssize_t index) const {
        return PySequence_Fast_GET_ITEM(this->obj, index);
    }

    /* Get the value at a particular index of the sequence.  Returns a borrowed
    reference. */
    inline PyObject* operator[](size_t index) const {
        if (index >= size()) {
            throw IndexError("index out of range");
        }
        return GET_ITEM(index);
    }

};


/* An extension of python::Object that represents a Python unicode string. */
template <Ref ref = Ref::STEAL>
class String : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python unicode string from an existing CPython unicode string. */
    String(PyObject* obj) : Base(obj) {
        if (!PyUnicode_Check(obj)) {
            throw TypeError("expected a unicode string");
        }
    }

    /* Construct a Python unicode string from a C-style character array. */
    String(const char* value) : Base(PyUnicode_FromString(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a String from a C-style string requires the use of "
            "Ref::STEAL to avoid memory leaks"
        );
    }

    /* Construct a Python unicode string from a C++ string. */
    String(const std::string& value) : Base(PyUnicode_FromStringAndSize(
        value.c_str(), value.size()
    )) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a String from a string requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python unicode string from a C++ string view. */
    String(const std::string_view& value) : Base(PyUnicode_FromStringAndSize(
        value.data(), value.size()
    )) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a String from a string view requires the use of Ref::STEAL "
            "to avoid memory leaks"
        );
    }

    /* Construct a Python unicode string from a printf-style format string.  See the
    Python docs for PyUnicode_FromFormat() for more details. */
    template <typename... Args>
    String(const char* format, Args&&... args) : Base(PyUnicode_FromFormat(
        format, std::forward<Args>(args)...
    )) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a String from a format string requires the use of Ref::STEAL "
            "to avoid memory leaks"
        );
    }

    /* Copy constructor. */
    String(const String& other) : Base(other) {}

    /* Move constructor. */
    String(String&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    String& operator=(const String& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    String& operator=(String&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ///////////////////////////////////
    ////    PyUnicode_* METHODS    ////
    ///////////////////////////////////

    /* Implicitly convert a python::List into a PyUnicodeObject* pointer. */
    inline operator PyUnicodeObject*() const noexcept {
        return reinterpret_cast<PyUnicodeObject*>(this->obj);
    }

    /* Get the underlying unicode buffer. */
    inline void* data() const noexcept {
        return PyUnicode_DATA(this->obj);
    }

    /* Get the kind of the string, indicating the size of the unicode points. */
    inline int kind() const noexcept {
        return PyUnicode_KIND(this->obj);
    }

    /* Get the maximum code point that is suitable for creating another string based
    on this string. */
    inline Py_UCS4 max_char() const noexcept {
        return PyUnicode_MAX_CHAR_VALUE(this->obj);
    }

    /* Get the length of the string. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyUnicode_GET_LENGTH(this->obj));
    }

    /* Fill this string with the given unicode character. */
    inline void fill(Py_UCS4 ch) {
        if (PyUnicode_FillChar(this->obj, ch)) {
            throw catch_python();
        }
    }

    /* Return a shallow copy of the string. */
    inline String<Ref::STEAL> copy() const {
        PyObject* result = PyUnicode_New(size(), max_char());
        if (result == nullptr) {
            throw catch_python();
        }
        if (PyUnicode_CopyCharacters(result, 0, this->obj, 0, size())) {
            Py_DECREF(result);
            throw catch_python();
        }
        return String<Ref::STEAL>(result);
    }

    /* Check if the string contains a given substring. */
    inline bool contains(PyObject* substr) const {
        int result = PyUnicode_Contains(this->obj, substr);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Return a substring from this string. */
    inline String<Ref::STEAL> substring(Py_ssize_t start, Py_ssize_t end) const {
        return String<Ref::STEAL>(PyUnicode_Substring(this->obj, start, end));
    }

    /* Concatenate this string with another. */
    inline String<Ref::STEAL> concat(PyObject* other) const {
        return String<Ref::STEAL>(PyUnicode_Concat(this->obj, other));
    }

    /* Split this string on the given separator, returning the components in a
    python::List. */
    inline List<Ref::STEAL> split(PyObject* separator, Py_ssize_t maxsplit) const {
        return List<Ref::STEAL>(PyUnicode_Split(this->obj, separator, maxsplit));
    }

    /* Split this string at line breaks, returning the components in a python::List. */
    inline List<Ref::STEAL> splitlines(bool keepends) const {
        return List<Ref::STEAL>(PyUnicode_Splitlines(this->obj, keepends));
    }

    /* Join a sequence of strings with this string as a separator. */
    inline String<Ref::STEAL> join(PyObject* iterable) const {
        return String<Ref::STEAL>(PyUnicode_Join(this->obj, iterable));
    }

    /* Check whether the string starts with the given substring. */
    inline bool startswith(PyObject* prefix, Py_ssize_t start = 0) const {
        int result = PyUnicode_Tailmatch(this->obj, prefix, start, size(), -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Check whether the string starts with the given substring. */
    inline bool startswith(PyObject* prefix, Py_ssize_t start, Py_ssize_t stop) const {
        int result = PyUnicode_Tailmatch(this->obj, prefix, start, stop, -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Check whether the string ends with the given substring. */
    inline bool endswith(PyObject* suffix, Py_ssize_t start = 0) const {
        int result = PyUnicode_Tailmatch(this->obj, suffix, start, size(), 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Check whether the string ends with the given substring. */
    inline bool endswith(PyObject* suffix, Py_ssize_t start, Py_ssize_t stop) const {
        int result = PyUnicode_Tailmatch(this->obj, suffix, start, stop, 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the first occurrence of a substring within the string. */
    inline Py_ssize_t find(PyObject* sub, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_Find(this->obj, sub, start, size(), 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the first occurrence of a substring within the string. */
    inline Py_ssize_t find(PyObject* sub, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_Find(this->obj, sub, start, stop, 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the first occurrence of a character within the string. */
    inline Py_ssize_t find(Py_UCS4 ch, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_FindChar(this->obj, ch, start, size(), 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the first occurrence of a character within the string. */
    inline Py_ssize_t find(Py_UCS4 ch, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_FindChar(this->obj, ch, start, stop, 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the last occurrence of a substring within the string. */
    inline Py_ssize_t rfind(PyObject* sub, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_Find(this->obj, sub, start, size(), -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the last occurrence of a substring within the string. */
    inline Py_ssize_t rfind(PyObject* sub, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_Find(this->obj, sub, start, stop, -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the last occurrence of a character within the string. */
    inline Py_ssize_t rfind(Py_UCS4 ch, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_FindChar(this->obj, ch, start, size(), -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the last occurrence of a character within the string. */
    inline Py_ssize_t rfind(Py_UCS4 ch, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_FindChar(this->obj, ch, start, stop, -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Count the number of occurrences of a substring within the string. */
    inline Py_ssize_t count(PyObject* sub, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_Count(this->obj, sub, start, size());
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Count the number of occurrences of a substring within the string. */
    inline Py_ssize_t count(PyObject* sub, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_Count(this->obj, sub, start, stop);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Return a new string with at most maxcount occurrences of a substring replaced
    with another substring. */
    inline String<Ref::STEAL> replace(
        PyObject* substr, PyObject* replstr, Py_ssize_t maxcount = -1
    ) const {
        return String<Ref::STEAL>(
            PyUnicode_Replace(this->obj, substr, replstr, maxcount)
        );
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the string. */
    class Element {
        PyObject* string;
        Py_ssize_t index;

        friend String;

        Element(PyObject* string, Py_ssize_t index) : string(string), index(index) {}

    public:
            
        /* Get the character at this index. */
        inline Py_UCS4 get() const {
            return PyUnicode_ReadChar(string, index);
        }

        /* Set the character at this index. */
        inline void set(Py_UCS4 ch) {
            PyUnicode_WriteChar(string, index, ch);
        }

    };

    inline Element operator[](Py_ssize_t index) {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    inline const Element operator[](Py_ssize_t index) const {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    /* Directly access a character within the string, without bounds checking or
    constructing a proxy. */
    inline Py_UCS4 READ_CHAR(Py_ssize_t index) const {
        return PyUnicode_READ_CHAR(this->obj, index);
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a python::String into a C-style character array. */
    inline operator const char*() const {
        const char* result = PyUnicode_AsUTF8(this->obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }

    /* Implicitly convert a python::String into a C++ string. */
    inline operator std::string() const {
        Py_ssize_t length;
        const char* data = PyUnicode_AsUTF8AndSize(this->obj, &length);
        if (data == nullptr) {
            throw catch_python();
        }
        return std::string(data, static_cast<size_t>(length));
    }

    /* Implicitly convert a python::String into a C++ string view. */
    inline operator std::string_view() const {
        Py_ssize_t length;
        const char* data = PyUnicode_AsUTF8AndSize(this->obj, &length);
        if (data == nullptr) {
            throw catch_python();
        }
        return std::string_view(data, static_cast<size_t>(length));
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Concatenate this string with another. */
    inline String<Ref::STEAL> operator+(PyObject* other) const {
        return concat(other);
    }

    /* Check if this string is less than another string. */
    inline bool operator<(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result < 0;
        }
        return Base::operator<(other);
    }

    /* Check if this string is less than or equal to another string. */
    inline bool operator<=(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result <= 0;
        }
        return Base::operator<=(other);
    }

    /* Check if this string is greater than another string. */
    inline bool operator>(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result > 0;
        }
        return Base::operator>(other);
    }

    /* Check if this string is greater than or equal to another string. */
    inline bool operator>=(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result >= 0;
        }
        return Base::operator>=(other);
    }

    /* Check if this string is equal to another string. */
    inline bool operator==(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result == 0;
        }
        return Base::operator==(other);
    }

};


//////////////////////////
////    EVALUATION    ////
//////////////////////////


/* Code evaluation using the C++ API is very confusing.  The following functions
 * attempt to replicate the behavior of Python's built-in compile(), exec(), and eval()
 * functions, but with a more C++-friendly interface.
 *
 * NOTE: these functions should not be used on unfiltered user input, as they trigger
 * the execution of arbitrary Python code.  This can lead to security vulnerabilities
 * if not handled properly.
 */


/* An extension of python::Object that represents a Python code object. */
template <Ref ref = Ref::STEAL>
class Code : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python code object from an existing CPython code object. */
    Code(PyObject* obj) : Base(obj) {
        if (!PyCode_Check(obj)) {
            throw TypeError("expected a code object");
        }
    }

    /* Implicitly convert a python::List into a PyCodeObject* pointer. */
    inline operator PyCodeObject*() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj);
    }

    ////////////////////////////////
    ////    PyCode_* METHODS    ////
    ////////////////////////////////

    /* Get the function's base name. */
    inline std::string name() const {
        String<Ref::BORROW> name(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_name
        );
        return name.str();
    }

    /* Get the function's qualified name. */
    inline std::string qualname() const {
        String<Ref::BORROW> qualname(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_qualname
        );
        return qualname.str();
    }

    /* Get the total number of positional arguments for the function, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_argcount
        );
    }

    /* Get the number of positional-only arguments for the function, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t positional_only() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_posonlyargcount
        );
    }

    /* Get the number of keyword-only arguments for the function, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t keyword_only() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_kwonlyargcount
        );
    }

    /* Get the number of local variables used by the function (including all
    parameters). */
    inline size_t n_locals() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_nlocals
        );
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string file_name() const {
        Object<Ref::BORROW> filename(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_filename
        );
        Py_ssize_t size;
        const char* result = PyUnicode_AsUTF8AndSize(filename, &size);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result, static_cast<size_t>(size)};
    }

    /* Get the first line number of the function. */
    inline size_t line_number() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_firstlineno
        );
    }

    /* Get the required stack space for the code object. */
    inline size_t stack_size() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_stacksize
        );
    }

};


// TODO: compile() should just be the constructor for Code objects, and the top-level
// compile() function just forwards to the Code constructor.


/* Parse and compile a source string into a Python code object.  The filename is used
in to construct the code object and may appear in tracebacks or exception messages.
The mode is used to constrain the code wich can be compiled, and must be one of
`Py_eval_input`, `Py_file_input`, or `Py_single_input` for multiline strings, file
contents, and single-line, REPL-style statements respectively. */
inline Code<Ref::STEAL> compile(
    const char* source,
    const char* filename = nullptr,
    int mode = Py_eval_input
) {
    if (filename == nullptr) {
        filename = "<anonymous file>";
    }
    if (mode != Py_file_input && mode != Py_eval_input && mode != Py_single_input) {
        std::ostringstream msg;
        msg << "invalid compilation mode: " << mode << " <- must be one of ";
        msg << "Py_file_input, Py_eval_input, or Py_single_input";
        throw ValueError(msg.str());
    }

    PyObject* result = Py_CompileString(source, filename, mode);
    if (result == nullptr) {
        throw catch_python();
    }
    return Code<Ref::STEAL>(result);
}


/* See const char* overload. */
inline Code<Ref::STEAL> compile(
    const char* source,
    const std::string& filename,
    int mode = Py_eval_input
) {
    return compile(source, filename.c_str(), mode);
}


/* See const char* overload. */
inline Code<Ref::STEAL> compile(
    const char* source,
    const std::string_view& filename,
    int mode = Py_eval_input
) {
    return compile(source, filename.data(), mode);
}


/* See const char* overload. */
inline Code<Ref::STEAL> compile(
    const std::string& source,
    const char* filename = nullptr,
    int mode = Py_eval_input
) {
    return compile(source.c_str(), filename, mode);
}


/* See const char* overload. */
inline Code<Ref::STEAL> compile(
    const std::string& source,
    const std::string& filename,
    int mode = Py_eval_input
) {
    return compile(source.c_str(), filename.c_str(), mode);
}


/* See const char* overload. */
inline Code<Ref::STEAL> compile(
    const std::string& source,
    const std::string_view& filename,
    int mode = Py_eval_input
) {
    return compile(source.c_str(), filename.data(), mode);
}


/* See const char* overload. */
inline Code<Ref::STEAL> compile(
    const std::string_view& source,
    const char* filename = nullptr,
    int mode = Py_eval_input
) {
    return compile(source.data(), filename, mode);
}


/* See const char* overload. */
inline Code<Ref::STEAL> compile(
    const std::string_view& source,
    const std::string& filename,
    int mode = Py_eval_input
) {
    return compile(source.data(), filename.c_str(), mode);
}


/* See const char* overload. */
inline Code<Ref::STEAL> compile(
    const std::string_view& source,
    const std::string_view& filename,
    int mode = Py_eval_input
) {
    return compile(source.data(), filename.data(), mode);
}


/* Execute a pre-compiled Python code object. */
inline PyObject* exec(PyObject* code, PyObject* globals, PyObject* locals) {
    PyObject* result = PyEval_EvalCode(code, globals, locals);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Execute an interpreter frame using its associated context.  The code object within
the frame will be executed, interpreting bytecode and executing calls as needed until
it reaches the end of its code path. */
inline PyObject* exec(PyFrameObject* frame) {
    PyObject* result = PyEval_EvalFrame(frame);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
void run(const char* filename) {
    // NOTE: Python recommends that on windows platforms, we open the file in binary
    // mode to avoid issues with the newline character.
    #if defined(_WIN32) || defined(_WIN64)
        std::FILE* file = _wfopen(filename.c_str(), "rb");
    #else
        std::FILE* file = std::fopen(filename.c_str(), "r");
    #endif

    if (file == nullptr) {
        std::ostringstream msg;
        msg << "could not open file '" << filename << "'";
        throw FileNotFoundError(msg.str());
    }

    // NOTE: PyRun_SimpleFileEx() launches an interpreter, executes the file, and then
    // closes the file connection automatically.  It returns 0 on success and -1 on
    // failure, with no way of recovering the original error message if one is raised.
    if (PyRun_SimpleFileEx(file, filename.c_str(), 1)) {
        std::ostringstream msg;
        msg << "error occurred while running file '" << filename << "'";
        throw RuntimeError(msg.str());
    }
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
inline void run(const std::string& filename) {
    run(filename.c_str());
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
inline void run(const std::string_view& filename) {
    run(filename.data());
}


/* Evaluate an arbitrary Python statement encoded as a string. */
inline PyObject* eval(const char* statement, PyObject* globals, PyObject* locals) {
    PyObject* result = PyRun_String(statement, Py_eval_input, globals, locals);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Evaluate an arbitrary Python statement encoded as a string. */
inline PyObject* eval(
    const std::string& statement,
    PyObject* globals,
    PyObject* locals
) {
    return eval(statement.c_str(), globals, locals);
}


/* Evaluate an arbitrary Python statement encoded as a string. */
inline PyObject* eval(
    const std::string_view& statement,
    PyObject* globals,
    PyObject* locals
) {
    return eval(statement.data(), globals, locals);
}


}  // namespace python


/* A trait that controls which C++ types are passed through the sequence() helper
without modification.  These must be vector or array types that support a definite
`.size()` method as well as integer-based indexing via the `[]` operator.  If a type
does not appear here, then it is converted into a std::vector instead. */
template <typename T>
struct SequenceFilter : std::false_type {};

template <typename T, typename Alloc>
struct SequenceFilter<std::vector<T, Alloc>> : std::true_type {};

template <typename T, size_t N> 
struct SequenceFilter<std::array<T, N>> : std::true_type {};

template <typename T, typename Alloc>
struct SequenceFilter<std::deque<T, Alloc>> : std::true_type {};

template <>
struct SequenceFilter<std::string> : std::true_type {};
template <>
struct SequenceFilter<std::wstring> : std::true_type {};
template <>
struct SequenceFilter<std::u16string> : std::true_type {};
template <>
struct SequenceFilter<std::u32string> : std::true_type {};

template <>
struct SequenceFilter<std::string_view> : std::true_type {};
template <>
struct SequenceFilter<std::wstring_view> : std::true_type {};
template <>
struct SequenceFilter<std::u16string_view> : std::true_type {};
template <>
struct SequenceFilter<std::u32string_view> : std::true_type {};

template <typename T>
struct SequenceFilter<std::valarray<T>> : std::true_type {};

#if __cplusplux >= 202002L  // C++20 or later

    template <typename T>
    struct SequenceFilter<std::span<T>> : std::true_type {};

    template <>
    struct SequenceFilter<std::u8string> : std::true_type {};

    template <>
    struct SequenceFilter<std::u8string_view> : std::true_type {};

#endif



/* Unpack an arbitrary Python iterable or C++ container into a sequence that supports
random access.  If the input already supports these, then it is returned directly. */
template <typename Iterable>
auto sequence(Iterable&& iterable) {

    if constexpr (is_pyobject<Iterable>) {
        PyObject* seq = PySequence_Fast(iterable, "expected a sequence");
        return python::FastSequence<python::Ref::STEAL>(seq);

    } else {
        using Traits = ContainerTraits<Iterable>;
        static_assert(Traits::forward_iterable, "container must be forward iterable");

        // if the container already supports random access, then return it directly
        if constexpr (
            SequenceFilter<
                std::remove_cv_t<
                    std::remove_reference_t<Iterable>
                >
            >::value
        ) {
            return std::forward<Iterable>(iterable);
        } else {
            auto proxy = iter(iterable);
            auto it = proxy.begin();
            auto end = proxy.end();
            using Deref = decltype(*std::declval<decltype(it)>());
            return std::vector<Deref>(it, end);
        }
    }
}


}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_CONTAINER_H
