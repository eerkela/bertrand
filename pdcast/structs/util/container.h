#ifndef BERTRAND_STRUCTS_UTIL_CONTAINER_H
#define BERTRAND_STRUCTS_UTIL_CONTAINER_H

#include <array>  // std::array
#include <cstddef>  // size_t
#include <deque>  // std::deque
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <tuple>  // std::tuple
#include <type_traits>  // std::enable_if_t<>
#include <valarray>  // std::valarray
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python(), TypeError, KeyError, IndexError
#include "iter.h"  // iter()
#include "ops.h"  // repr()


namespace bertrand {
namespace python {


////////////////////
////    BASE    ////
////////////////////


/* Reference count protocols for wrapping CPython Object pointers.  The options are as
follows:
    #.  NEW: increment the reference count of the object on construction and decrement
        it on destruction.
    #.  STEAL: do not modify the reference count of the object on construction, but
        decrement it on destruction.  This assumes ownership over the object.
    #.  BORROW: do not modify the reference count of the object on construction or
        destruction.  The object is assumed to outlive the wrapper.
 */
enum class Ref {
    NEW,
    STEAL,
    BORROW
};


/* Base class for all C++ wrappers around CPython object pointers. */
template <Ref ref>
struct Object {
    PyObject* obj;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a python::Object around a PyObject* pointer, applying the templated
    adoption rule. */
    Object(PyObject* obj) : obj(obj) {
        if (obj == nullptr) {
            throw TypeError("Object initializer must not be null");
        }
        if constexpr (ref == Ref::NEW) {
            Py_INCREF(obj);
        }
    }

    /* Copy constructor. */
    Object(const Object& other) : obj(other.obj) {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XINCREF(obj);
        }
    }

    /* Move constructor. */
    Object(Object&& other) : obj(other.obj) {
        other.obj = nullptr;
    }

    /* Copy assignment. */
    Object& operator=(const Object& other) {
        if (this == &other) {
            return *this;
        }
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(obj);
        }
        obj = other.obj;
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XINCREF(obj);
        }
        return *this;
    }

    /* Move assignment. */
    Object& operator=(Object&& other) {
        if (this == &other) {
            return *this;
        }
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(obj);
        }
        obj = other.obj;
        other.obj = nullptr;
        return *this;
    }

    /* Release the Python object on destruction. */
    ~Object() {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(obj);
        }
    }

    //////////////////////////////////
    ////    PyObject_* METHODS    ////
    //////////////////////////////////

    /* Get a borrowed reference to the object's type struct. */
    inline PyTypeObject* type() const noexcept {
        return Py_TYPE(obj);
    }

    /* Check whether this object is an instance of the given type. */
    inline bool isinstance(PyObject* type) const noexcept {
        return PyObject_IsInstance(obj, type);
    }

    /* Get the current reference count of the object. */
    inline Py_ssize_t refcount() const noexcept {
        return Py_REFCNT(obj);
    }

    /* Return a (possibly empty) list of strings representing named attributes of the
    object. */
    std::vector<std::string> dir() const {
        PyObject* contents = PyObject_Dir(obj);
        if (contents == nullptr) {
            if (PyErr_Occurred()) {
                throw catch_python();
            } else {
                return {};
            }
        }

        std::vector<std::string> result;
        for (PyObject* attribute : iter(contents)) {
            Py_ssize_t size;
            const char* string = PyUnicode_AsUTF8AndSize(attribute, &size);
            result.emplace_back(string, static_cast<size_t>(size));
        }
        Py_DECREF(dir);
        return result;
    }

    /* Get a string representation of the object using Python repr(). */
    inline std::string repr() const {
        PyObject* string = PyObject_Repr(obj);
        if (string == nullptr) {
            throw catch_python();
        }
        Py_ssize_t size;
        std::string result {
            PyUnicode_AsUTF8AndSize(string, &size),
            static_cast<size_t>(size)
        };
        Py_DECREF(string);
        return result;
    }

    /* Get the hash of the object. */
    inline size_t hash() const noexcept {
        Py_ssize_t result = PyObject_Hash(obj);
        if (result == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    /* Get the length of the object. */
    inline size_t size() const {
        Py_ssize_t result = PyObject_Length(obj);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a python::Object into a PyObject* pointer. */
    inline operator PyObject*() const noexcept {
        return obj;
    }

    /* Implicitly convert a python::Object into a C integer.  Equivalent to Python
    int(). */
    inline operator long long() const {
        long long value = PyLong_AsLongLong(obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

    /* Implicitly convert a python::Object into a C float.  Equivalent to Python
    float(). */
    inline operator double() const {
        double value = PyFloat_AsDouble(obj);
        if (value == -1.0 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

    /* Implicitly Convert the object to a C++ boolean.  Equivalent to Python bool(). */
    inline operator bool() const {
        int result = PyObject_IsTrue(obj);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Implicitly Convert the object to a C++ string.  Equivalent to Python str(). */
    inline operator std::string() const {
        PyObject* string = PyObject_Str(obj);
        if (string == nullptr) {
            throw catch_python();
        }
        Py_ssize_t size;
        std::string result {
            PyUnicode_AsUTF8AndSize(string, &size),
            static_cast<size_t>(size)
        };
        Py_DECREF(string);
        return result;
    }

    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    inline auto begin() { return iter(obj).begin(); }
    inline auto begin() const { return iter(obj).begin(); }
    inline auto cbegin() const { return iter(obj).cbegin(); }
    inline auto end() { return iter(obj).end(); }
    inline auto end() const { return iter(obj).end(); }
    inline auto cend() const { return iter(obj).cend(); }
    inline auto rbegin() { return iter(obj).rbegin(); }
    inline auto rbegin() const { return iter(obj).rbegin(); }
    inline auto crbegin() const { return iter(obj).crbegin(); }
    inline auto rend() { return iter(obj).rend(); }
    inline auto rend() const { return iter(obj).rend(); }
    inline auto crend() const { return iter(obj).crend(); }

    ////////////////////////////////
    ////    ATTRIBUTE ACCESS    ////
    ////////////////////////////////

    inline bool has_attr(PyObject* attr) const noexcept {
        return PyObject_HasAttr(obj, attr);
    }

    inline bool has_attr(const char* attr) const noexcept {
        return PyObject_HasAttrString(obj, attr);
    }

    inline Object<Ref::STEAL> get_attr(PyObject* attr) const {
        PyObject* value = PyObject_GetAttr(obj, attr);
        if (value == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(value);
    }

    inline Object<Ref::STEAL> get_attr(const char* attr) const {
        PyObject* value = PyObject_GetAttrString(obj, attr);
        if (value == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(value);
    }

    inline void set_attr(PyObject* attr, PyObject* value) const {
        if (PyObject_SetAttr(obj, attr, value)) {
            throw catch_python();
        }
    }

    inline void set_attr(const char* attr, PyObject* value) const {
        if (PyObject_SetAttrString(obj, attr, value)) {
            throw catch_python();
        }
    }

    inline void del_attr(PyObject* attr) const {
        if (PyObject_DelAttr(obj, attr)) {
            throw catch_python();
        }
    }

    inline void del_attr(const char* attr) const {
        if (PyObject_DelAttrString(obj, attr)) {
            throw catch_python();
        }
    }

    /////////////////////////
    ////    CALLABLES    ////
    /////////////////////////

    /* Check if the object can be called like a function. */
    inline bool is_callable() const noexcept {
        return PyCallable_Check(obj);
    }

    /* Call the object using C-style positional arguments.  Returns a new reference. */
    template <
        typename... Args,
        typename std::enable_if_t<
            std::conjunction_v<std::is_convertible<Args, PyObject*>...>
        >
    >
    inline Object<Ref::STEAL> operator()(Args&&... args) const {
        // fastest: no arguments
        if constexpr (sizeof...(Args) == 0) {
            PyObject* result = PyObject_CallNoArgs(obj);
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);

        // faster: exactly one argument
        } else if constexpr (sizeof...(Args) == 1) {
            PyObject* result = PyObject_CallOneArg(obj, std::forward<Args>(args)...);
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);

        // fast: multiple arguments
        } else {
            PyObject* result = PyObject_CallFunctionObjArgs(
                obj, std::forward<Args>(args)...
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

    ///////////////////////////
    ////    COMPARISONS    ////
    ///////////////////////////

    /* Apply a Python-level `is` (identity) comparison to the object. */
    inline bool is(PyObject* other) const noexcept {
        return obj == other;
    }

    inline bool operator<(PyObject* other) const {
        int result = PyObject_RichCompareBool(obj, other, Py_LT);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    inline bool operator<=(PyObject* other) const {
        int result = PyObject_RichCompareBool(obj, other, Py_LE);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    inline bool operator==(PyObject* other) const {
        int result = PyObject_RichCompareBool(obj, other, Py_EQ);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    inline bool operator!=(PyObject* other) const {
        int result = PyObject_RichCompareBool(obj, other, Py_NE);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    inline bool operator>=(PyObject* other) const {
        int result = PyObject_RichCompareBool(obj, other, Py_GE);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    inline bool operator>(PyObject* other) const {
        int result = PyObject_RichCompareBool(obj, other, Py_GT);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    //////////////////////////
    ////    ARITHMETIC    ////
    //////////////////////////

    inline Object<Ref::STEAL> operator+() const {
        PyObject* result = PyNumber_Positive(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object<Ref::STEAL> operator+(PyObject* other) const {
        PyObject* result = PyNumber_Add(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator+=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceAdd(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator-() const {
        PyObject* result = PyNumber_Negative(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object<Ref::STEAL> operator-(PyObject* other) const {
        PyObject* result = PyNumber_Subtract(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator-=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceSubtract(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator*(PyObject* other) const {
        PyObject* result = PyNumber_Multiply(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator*=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceMultiply(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> matrix_multiply(PyObject* other) const {
        PyObject* result = PyNumber_MatrixMultiply(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& inplace_matrix_multiply(PyObject* other) {
        PyObject* result = PyNumber_InPlaceMatrixMultiply(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator/(PyObject* other) const {
        PyObject* result = PyNumber_TrueDivide(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator/=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceTrueDivide(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> floor_divide(PyObject* other) const {
        PyObject* result = PyNumber_FloorDivide(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& inplace_floor_divide(PyObject* other) {
        PyObject* result = PyNumber_InPlaceFloorDivide(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator%(PyObject* other) const {
        PyObject* result = PyNumber_Remainder(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator%=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceRemainder(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> divmod(PyObject* other) const {
        PyObject* result = PyNumber_Divmod(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object<Ref::STEAL> power(PyObject* other) const {
        PyObject* result = PyNumber_Power(obj, other, Py_None);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& inplace_power(PyObject* other) {
        PyObject* result = PyNumber_InPlacePower(obj, other, Py_None);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator~() const {
        PyObject* result = PyNumber_Invert(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object<Ref::STEAL> abs() const {
        PyObject* result = PyNumber_Absolute(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object<Ref::STEAL> operator<<(PyObject* other) const {
        PyObject* result = PyNumber_Lshift(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator<<=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceLshift(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator>>(PyObject* other) const {
        PyObject* result = PyNumber_Rshift(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator>>=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceRshift(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator|(PyObject* other) const {
        PyObject* result = PyNumber_Or(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator|=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceOr(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator&(PyObject* other) const {
        PyObject* result = PyNumber_And(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator&=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceAnd(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    inline Object<Ref::STEAL> operator^(PyObject* other) const {
        PyObject* result = PyNumber_Xor(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        return Object<Ref::STEAL>(result);
    }

    inline Object& operator^=(PyObject* other) {
        PyObject* result = PyNumber_InPlaceXor(obj, other);
        if (result == nullptr) {
            throw catch_python();
        }
        Py_DECREF(result);
        return *this;
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the object. */
    class Element {
        PyObject* obj;
        PyObject* key;

        friend Object;

        /* Construct an Element proxy for the specified key.  Borrows a reference to
        the key. */
        Element(PyObject* obj, PyObject* key) : obj(obj), key(key) {}

    public:

        /* Get the item with the specified key.  Returns a new reference. */
        inline Object<Ref::STEAL> get() const {
            PyObject* result = PyObject_GetItem(obj, key);
            if (result == nullptr) {
                throw catch_python();
            }
            return Object<Ref::STEAL>(result);
        }

        /* Set the item with the specified key.  Borrows a reference to the new value
        and releases a reference to a previous one if a conflict occurs. */
        inline void set(PyObject* value) {
            if (PyObject_SetItem(obj, key, value)) {
                throw catch_python();
            }
        }

        /* Delete the item with the specified key.  Releases a reference to the
        value. */
        inline void del() {
            if (PyObject_DelItem(obj, key)) {
                throw catch_python();
            }
        }

    };

    inline Element operator[](PyObject* key) {
        return {obj, key};
    }

    inline const Element operator[](PyObject* key) const {
        return {obj, key};
    }

};


///////////////////////
////    NUMBERS    ////
///////////////////////


/* An extension of python::Object that represents a Python boolean. */
template <Ref ref>
class Bool : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python boolean from an existing CPython boolean. */
    Bool(PyObject* obj) : Base(obj) {
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

    /* Copy constructor. */
    Bool(const Bool& other) : Base(other) {}

    /* Move constructor. */
    Bool(Bool&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Bool& operator=(const Bool& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Bool& operator=(Bool&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

};


/* An extension of python::Object that represents a Python integer. */
template <Ref ref>
class Int : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python integer from an existing CPython integer. */
    Int(PyObject* obj) : Base(obj) {
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

    /* Copy constructor. */
    Int(const Int& other) : Base(other) {}

    /* Move constructor. */
    Int(Int&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Int& operator=(const Int& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Int& operator=(Int&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

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

    /* Implicitly convert a python::Int into a C double. */
    inline operator double() const {
        double value = PyLong_AsDouble(this->obj);
        if (value == -1.0 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

};


/* An extension of python::Object that represents a Python float. */
template <Ref ref>
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

    /* Implicitly convert a python::Float into a C double. */
    inline operator double() const {
        return PyFloat_AS_DOUBLE(this->obj);
    }

};


/* An extension of python::Object that represents a complex number in Python. */
template <Ref ref>
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
template <Ref ref>
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
        Base(PySlice_New(nullptr, nullptr, nullptr)), _start(Py_None), _stop(Py_None),
        _step(Py_None)
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
template <Ref ref>
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

    /* Construct a new Python tuple containing the given objects. */
    template <
        typename... Args,
        typename std::enable_if_t<std::conjunction_v<std::is_convertible<Args, PyObject*>...>>
    >
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
        return {this->obj, index};
    }

    inline const Element operator[](size_t index) const {
        return {this->obj, index};
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
template <Ref ref>
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
        return {this->obj, index};
    }

    inline const Element operator[](size_t index) const {
        return {this->obj, index};
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
template <Ref ref>
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
            throw KeyError(repr(key));
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
template <Ref ref>
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
                throw KeyError(repr(key));
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
template <Ref ref>
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

    // TODO: iterator should directly index the sequence, not use a proxy.


    /* Iterate over the sequence. */
    // inline auto begin() const { return iter(obj).begin(); }
    // inline auto cbegin() const { return iter(obj).cbegin(); }
    // inline auto end() const { return iter(obj).end(); }
    // inline auto cend() const { return iter(obj).cend(); }
    // inline auto rbegin() const { return iter(obj).rbegin(); }
    // inline auto crbegin() const { return iter(obj).crbegin(); }
    // inline auto rend() const { return iter(obj).rend(); }
    // inline auto crend() const { return iter(obj).crend(); }

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
template <Ref ref>
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
        return {this->obj, index};
    }

    inline const Element operator[](Py_ssize_t index) const {
        return {this->obj, index};
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


}  // namespace python


/* A trait that controls which C++ types are passed through the sequence() helper
without modification.  These must be vector or array types that support a definite
`.size()` method as well as integer-based indexing via the `[]` operator.  If a type
does not appear here, then it is wrapped in a std::vector. */
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


// TODO: add std::span, std::u8string, std::u8string_view (C++20)


/* Unpack an arbitrary Python iterable or C++ container into a sequence that supports
random access.  If the input already supports these, then it is returned directly. */
template <typename Iterable>
inline auto sequence(Iterable&& iterable) {

    if constexpr (is_pyobject<Iterable>) {
        PyObject* seq = PySequence_Fast(iterable, "expected a sequence");
        python::FastSequence<python::Ref::BORROW> result(seq);
        Py_DECREF(seq);
        return result;

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
