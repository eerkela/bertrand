#ifndef PYBIND_COMMON_H
#define PYBIND_COMMON_H

#include <initializer_list>
#include <chrono>
#include <complex>
#include <sstream>  // std::ostringstream
#include <string>
#include <string_view>
#include <utility>  // std::pair, std::forward, std::move, etc.

#include <Python.h>
#include <pybind11/pybind11.h>
#include <pybind11/chrono.h>
#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/functional.h>
#include <pybind11/iostream.h>
#include <pybind11/numpy.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>



/* NOTE: this uses slightly different syntax from normal pybind11 in order to support
 * more of the Python standard library and to replicate its semantics more closely.
 * The only real differences are that types are capitalized in order to avoid
 * extraneous underscores and conflicts with non-member functions of the same name, and
 * they implement the same interface as the Python types they represent, without
 * requiring `attr()` syntax.  Containers can also be directly constructed using
 * initializer lists, and a wider variety of global methods are supported.
 *
 * For example, the following pybind11 code:
 *
 *     py::list foo = py::cast<py::list>(std::vector<int>{1, 2, 3});
 *     py::int_ bar = foo.attr("pop")();
 *
 * would be written as:
 *
 *     py::List foo{1, 2, 3};
 *     py::Int bar = foo.pop();
 *
 * Which is more concise and readable.
 */


using namespace pybind11::literals;
namespace py {


/////////////////////////////////////////
////     INHERITED FROM PYBIND11     ////
/////////////////////////////////////////


// exceptions
using pybind11::error_already_set;
using TypeError = pybind11::type_error;
using ValueError = pybind11::value_error;
using KeyError = pybind11::key_error;
using IndexError = pybind11::index_error;
using BufferError = pybind11::buffer_error;
using CastError = pybind11::cast_error;
using ReferenceCastError = pybind11::reference_cast_error;
using ImportError = pybind11::import_error;
using StopIteration = pybind11::stop_iteration;


// wrapper types
using Handle = pybind11::handle;
using Object = pybind11::object;
using Iterator = pybind11::iterator;
using Iterable = pybind11::iterable;
using NoneType = pybind11::none;
using EllipsisType = pybind11::ellipsis;
using WeakRef = pybind11::weakref;
using Slice = pybind11::slice;
using Capsule = pybind11::capsule;
using Sequence = pybind11::sequence;
using Buffer = pybind11::buffer;
using MemoryView = pybind11::memoryview;
using Bytes = pybind11::bytes;
using Bytearray = pybind11::bytearray;
class Module;
class Type;  // done
class NotImplemented;  // maybe?
class String;  // done
using Bool = pybind11::bool_;
class Int;  // done
class Float;  // done
class Complex;  // done (minus operators)
class Tuple;  // done
class Dict;  // done
class List;  // done
class Args;
class Kwargs;
class AnySet;
class Set;
class FrozenSet;
class Function;
class StaticMethod;


// python builtins
const static NoneType None;
const static EllipsisType Ellipsis;
using pybind11::globals;
using pybind11::exec;
using pybind11::eval;
using pybind11::eval_file;
using pybind11::isinstance;
using pybind11::hasattr;
using pybind11::delattr;
using pybind11::getattr;
using pybind11::setattr;
using pybind11::hash;
using pybind11::len;
using pybind11::len_hint;
using pybind11::repr;
using pybind11::iter;
Module import(const char* module);
Dict locals();
Type type(const pybind11::handle& obj);
template <typename T, typename U>
Type type(const pybind11::str& name, T&& bases, U&& dict);
template <typename T, typename U>
Type type(const char* name, T&& bases, U&& dict);
template <typename T, typename U>
Type type(const std::string& name, T&& bases, U&& dict);
template <typename T, typename U>
Type type(const std::string_view& name, T&& bases, U&& dict);
template <typename T>
bool issubclass(const pybind11::type& derived, T&& base);
List dir();
List dir(const pybind11::handle& obj);
bool callable(const pybind11::handle& obj);
Iterator reversed(const pybind11::handle& obj);
Iterator reversed(const Tuple& obj);
Iterator reversed(const List& obj);
Object next(const pybind11::iterator& iter);
template <typename T>
Object next(const pybind11::iterator& iter, T&& default_value);
Object abs(const pybind11::handle& obj);
Object floor(const pybind11::handle& number);
Object ceil(const pybind11::handle& number);
Object trunc(const pybind11::handle& number);
String str(const pybind11::handle& obj);
String ascii(const pybind11::handle& obj);
String bin(const pybind11::handle& obj);
String oct(const pybind11::handle& obj);
String hex(const pybind11::handle& obj);
String chr(const pybind11::handle& obj);
Int ord(const pybind11::handle& obj);
template <typename T, typename U>
Object pow(T&& base, U&& exp);
template <typename T, typename U, typename V>
Object pow(T&& base, U&& exp, V&& mod);
template <typename T, typename U>
Object inplace_pow(T&& base, U&& exp);
template <typename T, typename U, typename V>
Object inplace_pow(T&& base, U&& exp, V&& mod);
template <typename T, typename U>
Tuple divmod(T&& a, U&& b);
template <typename T, typename U>
Object floor_div(T&& obj, U&& divisor);
template <typename T, typename U>
Object inplace_floor_div(T&& obj, U&& divisor);
template <typename T, typename U>
Object matrix_multiply(T&& a, U&& b);
template <typename T, typename U>
Object inplace_matrix_multiply(T&& a, U&& b);


// binding functions
// PYBIND11_MODULE                      <- macros don't respect namespaces
// PYBIND11_EMBEDDED_MODULE
using pybind11::implicitly_convertible;
using pybind11::cast;
using pybind11::reinterpret_borrow;
using pybind11::reinterpret_steal;
using pybind11::args_are_all_keyword_or_ds;
using pybind11::make_tuple;
using pybind11::make_iterator;
using pybind11::make_key_iterator;
using pybind11::make_value_iterator;
using pybind11::initialize_interpreter;
using pybind11::scoped_interpreter;
// PYBIND11_OVERRIDE
// PYBIND11_OVERRIDE_PURE
// PYBIND11_OVERRIDE_NAME
// PYBIND11_OVERRIDE_PURE_NAME
using pybind11::get_override;
using pybind11::error_already_set;


// annotations
using pybind11::is_method;
using pybind11::is_setter;
using pybind11::is_operator;
using pybind11::is_final;
using pybind11::scope;
using pybind11::doc;
using pybind11::name;
using pybind11::sibling;
using pybind11::base;
using pybind11::keep_alive;
using pybind11::multiple_inheritance;
using pybind11::dynamic_attr;
using pybind11::buffer_protocol;
using pybind11::metaclass;
using pybind11::custom_type_setup;
using pybind11::module_local;
using pybind11::arithmetic;
using pybind11::prepend;
using pybind11::call_guard;
using pybind11::arg;
using pybind11::arg_v;
using pybind11::kw_only;
using pybind11::pos_only;


// stream redirection
using pybind11::scoped_ostream_redirect;
using pybind11::scoped_estream_redirect;
using pybind11::add_ostream_redirect;


///////////////////////////////
////    WRAPPER CLASSES    ////
///////////////////////////////


#define IMMUTABLE_SEQUENCE_METHODS()                                                    \
    /* Equivalent to Python `s.count(value)`, but also takes start/stop indices. */     \
    template <typename T>                                                               \
    inline Py_ssize_t count(                                                            \
        T&& value,                                                                      \
        Py_ssize_t start = 0,                                                           \
        Py_ssize_t stop = -1                                                            \
    ) const {                                                                           \
        if (start != 0 || stop != -1) {                                                 \
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);            \
            if (slice == nullptr) {                                                     \
                throw error_already_set();                                              \
            }                                                                           \
            Py_ssize_t result = PySequence_Count(                                       \
                slice, py::cast(std::forward<T>(value)).ptr()                           \
            );                                                                          \
            Py_DECREF(slice);                                                           \
            if (result == -1 && PyErr_Occurred()) {                                     \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        }                                                                               \
        Py_ssize_t result = PySequence_Count(                                           \
            this->ptr(), py::cast(std::forward<T>(value)).ptr()                         \
        );                                                                              \
        if (result == -1 && PyErr_Occurred()) {                                         \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `s.index(value, start, stop)`. */                           \
    template <typename T>                                                               \
    inline Py_ssize_t index(                                                            \
        T&& value,                                                                      \
        Py_ssize_t start = 0,                                                           \
        Py_ssize_t stop = -1                                                            \
    ) const {                                                                           \
        if (start != 0 || stop != -1) {                                                 \
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);            \
            if (slice == nullptr) {                                                     \
                throw error_already_set();                                              \
            }                                                                           \
            Py_ssize_t result = PySequence_Index(                                       \
                slice, py::cast(std::forward<T>(value)).ptr()                           \
            );                                                                          \
            Py_DECREF(slice);                                                           \
            if (result == -1 && PyErr_Occurred()) {                                     \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        }                                                                               \
        Py_ssize_t result = PySequence_Index(                                           \
            this->ptr(), py::cast(std::forward<T>(value)).ptr()                         \
        );                                                                              \
        if (result == -1 && PyErr_Occurred()) {                                         \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \


/* Wrapper around pybind11::module_ that does stuff (TODO). */
class Module : public pybind11::module_ {
    using Base = pybind11::module_;

public:
    using Base::Base;
    using Base::operator=;



};


/* Wrapper around pybind11::int_ that enables conversions from strings with different
bases, similar to Python's `int()` constructor. */
class Int : public pybind11::int_ {
    using Base = pybind11::int_;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct an Int from a string. */
    explicit Int(const pybind11::str& str, int base = 0) : Base([&] {
        PyObject* result = PyLong_FromUnicodeObject(str.ptr(), base);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Construct an Int from a string. */
    explicit Int(const char* str, int base = 0) : Base([&] {
        PyObject* result = PyLong_FromString(str, nullptr, base);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Construct an Int from a string. */
    explicit Int(const std::string& str, int base = 0) : Int(str.c_str(), base) {}

    /* Construct an Int from a string. */
    explicit Int(const std::string_view& str, int base = 0) : Int(str.data(), base) {}

    /* Implicitly */

};


/* Wrapper around pybind11::float_ that enables conversions from strings, similar to
Python's `float()` constructor. */
class Float : public pybind11::float_ {
    using Base = pybind11::float_;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Float from a string. */
    explicit Float(const pybind11::str& str) : Base([&] {
        PyObject* result = PyFloat_FromString(str.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Construct a Float from a string. */
    explicit Float(const char* str) : Base([&] {
        PyObject* string = PyUnicode_FromString(str);
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Construct a Float from a string. */
    explicit Float(const std::string& str) : Base([&] {
        PyObject* string = PyUnicode_FromStringAndSize(str.c_str(), str.size());
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Construct a Float from a string. */
    explicit Float(const std::string_view& str) : Base([&] {
        PyObject* string = PyUnicode_FromStringAndSize(str.data(), str.size());
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

};


/* Subclass of pybind11::object that represents a complex number at the Python
level. */
class Complex : public pybind11::object {
    using Base = pybind11::object;

public:
    using Base::Base;
    using Base::operator=;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    template <typename Policy_>
    Complex(const ::pybind11::detail::accessor<Policy_> &a) : Complex(object(a)) {}
    Complex(handle h, borrowed_t) : Base(h, borrowed_t{}) {}
    Complex(handle h, stolen_t) : Base(h, stolen_t{}) {}
    Complex(handle h, bool is_borrowed) :
        Base(is_borrowed ? Base(h, borrowed_t{}) : Base(h, stolen_t{}))
    {}

    /* Default constructor.  Initializes to 0+0j. */
    inline Complex() : Base([&] {
        PyObject* result = PyComplex_FromDoubles(0.0, 0.0);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Implicitly convert a pybind11::object into a Complex. */
    Complex(const pybind11::object& obj) : Base([&] {
        PyObject* ptr = obj.ptr();
        if (ptr == nullptr) {
            throw TypeError("Cannot convert null object to complex number");
        } else if (PyComplex_Check(ptr)) {
            return obj;
        } else {
            Py_complex complex_struct = PyComplex_AsCComplex(ptr);
            if (complex_struct.real == -1.0 && PyErr_Occurred()) {
                throw error_already_set();
            }
            PyObject* result = PyComplex_FromDoubles(
                complex_struct.real, complex_struct.imag
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Base>(result);
        }
    }()) {}

    /* Implicitly convert a pybind11::object into a Complex. */
    Complex(pybind11::object&& obj) : Base([&] {
        PyObject* ptr = obj.ptr();
        if (ptr == nullptr) {
            throw TypeError("Cannot convert null object to complex number");
        } else if (PyComplex_Check(ptr)) {
            return std::move(obj);
        } else {
            Py_complex complex_struct = PyComplex_AsCComplex(ptr);
            if (complex_struct.real == -1.0 && PyErr_Occurred()) {
                throw error_already_set();
            }
            PyObject* result = PyComplex_FromDoubles(
                complex_struct.real, complex_struct.imag
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Base>(result);
        }
    }()) {}

    /* Construct a Complex from a C++ std::complex. */
    template <typename T>
    inline Complex(const std::complex<T>& value) : Base([&] {
        PyObject* result = PyComplex_FromDoubles(value.real(), value.imag());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Construct a Complex from separate real and imaginary doubles. */
    inline Complex(double real, double imag = 0.0) : Base([&] {
        PyObject* result = PyComplex_FromDoubles(real, imag);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Construct a Complex from separate real and imaginary ints. */
    inline Complex(long long real, long long imag = 0) : Base([&] {
        PyObject* result = PyComplex_FromDoubles(
            static_cast<double>(real),
            static_cast<double>(imag)
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Implicitly convert a Complex into a C++ std::complex. */
    template <typename T>
    operator std::complex<T>() const {
        Py_complex complex_struct = PyComplex_AsCComplex(this->ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return std::complex<T>(complex_struct.real, complex_struct.imag);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the real part of the complex number. */
    inline double real() const noexcept {
        return PyComplex_RealAsDouble(this->ptr());
    }

    /* Get the imaginary part of the complex number. */
    inline double imag() const noexcept {
        return PyComplex_ImagAsDouble(this->ptr());
    }

    /* Get the magnitude of the complex number. */
    inline Complex conjugate() const {
        Py_complex complex_struct = PyComplex_AsCComplex(this->ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return {complex_struct.real, -complex_struct.imag};
    }

    // TODO: operators for complex numbers

    template <typename T>
    inline Complex operator+(T&& other) const {
        return Base::operator+(py::cast(std::forward<T>(other)));
    }

};


/* Wrapper around pybind11::tuple that allows it to be directly initialized using
std::initializer_list and enables extra C API functionality. */
class Tuple : public pybind11::tuple {
    using Base = pybind11::tuple;

public:
    using Base::Base;
    using Base::operator=;

    /* Pack the given arguments into a Tuple using an initializer list. */
    template <typename T>
    explicit Tuple(std::initializer_list<T> args) : Base([&] {
        PyObject* result = PyTuple_New(args.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (auto&& arg : args) {
                PyObject* conv = py::cast(std::forward<decltype(arg)>(arg)).release();
                if (conv == nullptr) {
                    throw error_already_set();
                }
                PyTuple_SET_ITEM(result, i++, conv);  // Steals a reference to conv
            }
            return reinterpret_steal<Base>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }()) {}

    ////////////////////////////////////
    ////    PyTuple* API METHODS    ////
    ////////////////////////////////////

    /* Get the size of the tuple. */
    inline size_t size() const noexcept {
        return PyTuple_GET_SIZE(this->ptr());
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item in the tuple without bounds checking or constructing a
    proxy. */
    inline Object GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<Object>(PyTuple_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item in the tuple without bounds checking or constructing a
    proxy.  Steals a reference to `value` and does not clear the previous item if one is
    present.  This is dangerous, and should only be used to fill in a newly-allocated
    (empty) tuple. */
    template <typename T>
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyTuple_SET_ITEM(this->ptr(), index, value);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    IMMUTABLE_SEQUENCE_METHODS();  // count(), index()

    struct ReverseIterator {
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = Object;
        using pointer               = value_type*;
        using reference             = value_type&;

        PyObject** array;
        Py_ssize_t index;

        inline ReverseIterator(PyObject** array, Py_ssize_t index) :
            array(array), index(index)
        {}
        inline ReverseIterator(Py_ssize_t index) : array(nullptr), index(index) {}
        inline ReverseIterator(const ReverseIterator&) = default;
        inline ReverseIterator(ReverseIterator&&) = default;
        inline ReverseIterator& operator=(const ReverseIterator&) = default;
        inline ReverseIterator& operator=(ReverseIterator&&) = default;

        inline Object operator*() const {
            return reinterpret_borrow<Object>(array[index]);
        }

        inline ReverseIterator& operator++() {
            --index;
            return *this;
        }

        inline bool operator==(const ReverseIterator& other) const {
            return index == other.index;
        }

        inline bool operator!=(const ReverseIterator& other) const {
            return index != other.index;
        }

    };

};


/* Wrapper around pybind11::list that allows it to be directly initialized using
std::initializer_list and enables extra C API functionality. */
class List : public pybind11::list {
    using Base = pybind11::list;

public:
    using Base::Base;
    using Base::operator=;

    /* Pack the given arguments into a List using an initializer list. */
    template <typename T>
    explicit List(std::initializer_list<T> args) : Base([&] {
        PyObject* result = PyList_New(args.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (auto&& arg : args) {
                PyObject* conv = py::cast(std::forward<decltype(arg)>(arg)).release().ptr();
                if (conv == nullptr) {
                    throw error_already_set();
                }
                PyList_SET_ITEM(result, i++, conv);  // Steals a reference to conv
            }
            return reinterpret_steal<Base>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }()) {}

    ///////////////////////////////////
    ////    PyList* API METHODS    ////
    ///////////////////////////////////

    /* Get the size of the list. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyList_GET_SIZE(this->ptr()));
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    IMMUTABLE_SEQUENCE_METHODS();  // count(), index()

    /* Equivalent to Python `list.append(value)`. */
    template <typename T>
    inline void append(T&& value) {
        if (PyList_Append(this->ptr(), py::cast(std::forward<T>(value)).ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.extend(items)`. */



    /* Equivalent to Python `list.insert(index, value)`. */
    template <typename T>
    inline void insert(Py_ssize_t index, T&& value) {
        if (PyList_Insert(this->ptr(), index, py::cast(std::forward<T>(value)).ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.sort()`. */
    inline void sort() {
        if (PyList_Sort(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.reverse()`. */
    inline void reverse() {
        if (PyList_Reverse(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.copy()`. */
    inline List copy() const {
        PyObject* result = PyList_GetSlice(this->ptr(), 0, size());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }



};


/* Wrapper around pybind11::set that allows it to be directly initialized using
std::initializer_list and enables extra C API functionality. */
class Set : public pybind11::set {
    using Base = pybind11::set;

public:
    using Base::Base;
    using Base::operator=;

    /* Pack the given arguments into a Set using an initializer list. */
    template <typename T>
    explicit Set(std::initializer_list<T> args) : Base([&] {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (auto&& arg : args) {
                if (PySet_Add(result, py::cast(std::forward<decltype(arg)>(arg)).ptr())) {
                    throw error_already_set();
                }
            }
            return reinterpret_steal<Base>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }()) {}

    /////////////////////////////
    ////   PySet_* METHODS   ////
    /////////////////////////////

    /* Get the size of the set. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PySet_GET_SIZE(this->ptr()));
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `key in set`. */
    template <typename T>
    inline bool contains(T&& key) const {
        int result = PySet_Contains(this->ptr, py::cast(std::forward<T>(key)).ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `set.add(key)`. */
    template <typename T>
    inline void add(T&& key) {
        if (PySet_Add(this->ptr(), py::cast(std::forward<T>(key)).ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.copy()`. */
    inline Set copy() const {
        return reinterpret_steal<Set>(PySet_New(this->ptr()));
    }

    /* Equivalent to Python `set.clear()`. */
    inline void clear() {
        if (PySet_Clear(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.remove(key)`. */
    template <typename T>
    inline void remove(T&& key) {
        Object obj = cast<Object>(std::forward<T>(key));
        int result = PySet_Discard(this->ptr(), obj.ptr());
        if (result == -1) {
            throw error_already_set();
        } else if (result == 0) {
            throw KeyError(repr(obj).cast<std::string>());
        }
    }

    /* Equivalent to Python `set.discard(key)`. */
    template <typename T>
    inline void discard(T&& key) {
        if (PySet_Discard(this->ptr(), py::cast(std::forward<T>(key)).ptr()) == -1) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.pop()`. */
    inline Object pop() {
        PyObject* result = PySet_Pop(this->ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

};


/* Wrapper around pybind11::dict that allows it to be directly initialized using
std::initializer_list and enables extra C API functionality. */
class Dict : public pybind11::dict {
    using Base = pybind11::dict; 

    /* A simple pair struct that is compatible with initializer list syntax. */
    struct Initializer {
        pybind11::object key;
        pybind11::object value;

        template <typename K, typename V>
        Initializer(K&& key, V&& value) :
            key(py::cast(std::forward<K>(key))),
            value(py::cast(std::forward<V>(value)))
        {}

    };

public:
    using Base::Base;
    using Base::operator=;

    /* Pack the given arguments into a Dict using an initializer list. */
    Dict(std::initializer_list<Initializer> items) : Base([&] {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (auto&& item : items) {
                if (PyDict_SetItem(result, item.key.ptr(), item.value.ptr())) {
                    throw error_already_set();
                }
            }
            return reinterpret_steal<Base>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }()) {}

    ////////////////////////////////////
    ////    PyDict_* API METHODS    ////
    ////////////////////////////////////

    /* Get the size of the dict. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyDict_Size(this->ptr()));
    }

    /* Equivalent to Python `dict.update(items)`, but assumes each item is a tuple of
    length 2 (key, value).  Note that this should not be used with types that map to
    py::Dict, as that only yields keys during iteration. */
    template <typename T>
    inline void update_pairs(T&& items) {
        if (PyDict_MergeFromSeq2(this->ptr(), py::cast(std::forward<T>(items)).ptr(), 1)) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `dict.update(items)`, except that it does not overwrite any
    existing keys. */
    template <typename T>
    inline void merge(T&& items) {
        if (PyDict_Merge(this->ptr(), py::cast(std::forward<T>(items)).ptr(), 0)) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `dict.update(items)`, except that it assumes that each item
    is a tuple of length 2 (key, value), and does not overwrite any existing keys.
    Note that this should not be used with types that map to py::Dict, as that only
    yields keys during iteration. */
    template <typename T>
    inline void merge_pairs(T&& items) {
        if (PyDict_MergeFromSeq2(this->ptr(), py::cast(std::forward<T>(items)).ptr(), 0)) {
            throw error_already_set();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `key in dict`. */
    template <typename T>
    inline bool contains(T&& key) const {
        int result = PyDict_Contains(this->ptr(), py::cast(std::forward<T>(key)).ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `dict.copy()`. */
    inline Dict copy() const {
        return reinterpret_steal<Dict>(PyDict_Copy(this->ptr()));
    }

    /* Equivalent to Python `dict.clear()`. */
    inline void clear() {
        PyDict_Clear(this->ptr());
    }

    /* Equivalent to Python `dict.setdefault(key, default_value)`. */
    template <typename K, typename V>
    inline Object setdefault(K&& key, V&& default_value) {
        PyObject* result = PyDict_SetDefault(
            this->ptr(),
            py::cast(std::forward<K>(key)).ptr(),
            py::cast(std::forward<V>(default_value)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.update(items)`. */
    template <typename T>
    inline void update(T&& items) {
        if (PyDict_Merge(this->ptr(), py::cast(std::forward<T>(items)).ptr(), 1)) {
            throw error_already_set();
        }
    }

};


/* Wrapper around pybind11::str that enables extra C API functionality. */
class String : public pybind11::str {
    using Base = pybind11::str;

    template <typename T>
    inline auto to_format_string(T&& arg) -> decltype(auto) {
        using U = std::decay_t<T>;
        if constexpr (std::is_base_of_v<pybind11::handle, U>) {
            return arg.ptr();
        } else if constexpr (std::is_base_of_v<std::string, U>) {
            return arg.c_str();
        } else if constexpr (std::is_base_of_v<std::string_view, U>) {
            return arg.data();
        } else {
            return std::forward<T>(arg);
        }
    }

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a unicode string from a printf-style format string.  See the python
    docs for `PyUnicode_FromFormat()` for more details.  Note that this can segfault
    if the argument types do not match the format code(s). */
    template <typename... Args>
    explicit String(const char* format, Args&&... args) : Base([&] {
        PyObject* result = PyUnicode_FromFormat(
            format,
            to_format_string(std::forward<Args>(args))...
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Construct a unicode string from a printf-style format string.  See
    String(const char*, ...) for more details. */
    template <typename... Args>
    explicit String(const std::string& format, Args&&... args) : String(
        format.c_str(), std::forward<Args>(args)...
    ) {}

    /* Construct a unicode string from a printf-style format string.  See
    String(const char*, ...) for more details. */
    template <typename... Args>
    explicit String(const std::string_view& format, Args&&... args) : String(
        format.data(), std::forward<Args>(args)...
    ) {}

    /* Construct a unicode string from a printf-style format string.  See
    String(const char*, ...) for more details. */
    template <typename... Args>
    explicit String(const pybind11::str& format, Args&&... args) : String(
        format.template cast<std::string>(), std::forward<Args>(args)...
    ) {}

    ///////////////////////////////////
    ////    PyUnicode_* METHODS    ////
    ///////////////////////////////////

    /* Get the underlying unicode buffer. */
    inline void* data() const noexcept {
        return PyUnicode_DATA(this->ptr());
    }

    /* Get the length of the string in unicode code points. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyUnicode_GET_LENGTH(this->ptr()));
    }

    /* Get the kind of the string, indicating the size of the unicode points stored
    within. */
    inline int kind() const noexcept {
        return PyUnicode_KIND(this->ptr());
    }

    /* Get the maximum code point that is suitable for creating another string based
    on this string. */
    inline Py_UCS4 max_char() const noexcept {
        return PyUnicode_MAX_CHAR_VALUE(this->ptr());
    }

    /* Concatenate this string with another. */
    template <typename T>
    inline String concat(T&& other) const {
        PyObject* result = PyUnicode_Concat(
            this->ptr(), py::cast(std::forward<T>(other)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<String>(result);
    }

    /* Fill the string with a given character.  The input must be convertible to a
    string with a single character. */
    template <typename T>
    void fill(T&& ch) {
        String str = cast<String>(std::forward<T>(ch));
        if (str.size() != 1) {
            std::ostringstream msg;
            msg << "fill character must be a single character, not '" << str << "'";
            throw ValueError(msg.str());
        }
        Py_UCS4 code = PyUnicode_ReadChar(str.ptr(), 0);
        if (code == (Py_UCS4)-1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        if (PyUnicode_Fill(this->ptr(), 0, size(), code) == -1) {
            throw error_already_set();
        }
    }

    /* Fill the string with a given character, given as a raw Python unicode point. */
    inline void fill(Py_UCS4 ch) {
        if (PyUnicode_Fill(this->ptr(), 0, size(), ch) == -1) {
            throw error_already_set();
        }
    }

    /* Return a substring from this string. */
    inline String substring(Py_ssize_t start = 0, Py_ssize_t end = -1) const {
        PyObject* result = PyUnicode_Substring(this->ptr(), start, end);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<String>(result);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `str.capitalize()`. */
    inline String capitalize() const {
        return this->attr("capitalize")();
    }

    /* Equivalent to Python `str.casefold()`. */
    inline String casefold() const {
        return this->attr("casefold")();
    }

    /* Equivalent to Python `str.center(width)`. */
    template <typename... Args>
    inline String center(Args&&... args) const {
        return this->attr("center")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `sub in str`. */
    template <typename T>
    inline bool contains(T&& sub) const {
        int result = PyUnicode_Contains(this->ptr(), py::cast(std::forward<T>(sub)).ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.copy()`. */
    inline String copy() const {
        PyObject* result = PyUnicode_New(size(), max_char());
        if (result == nullptr) {
            throw error_already_set();
        }
        if (PyUnicode_CopyCharacters(result, 0, this->ptr(), 0, size())) {
            Py_DECREF(result);
            throw error_already_set();
        }
        return reinterpret_steal<String>(result);
    }

    /* Count the number of occurrences of a substring within the string. */
    template <typename T>
    inline Py_ssize_t count(T&& sub, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        Py_ssize_t result = PyUnicode_Count(
            this->ptr(),
            py::cast(std::forward<T>(sub)).ptr(),
            start,
            stop
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.encode(encoding)`. */
    template <typename... Args>
    inline Bytes encode(Args&&... args) const {
        return this->attr("encode")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.endswith(suffix[, start[, end]])`. */
    template <typename T>
    inline bool endswith(T&& suffix, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        int result = PyUnicode_Tailmatch(
            this->ptr(),
            py::cast(std::forward<T>(suffix)).ptr(),
            start,
            stop,
            1
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.expandtabs()`. */
    inline String expandtabs(Py_ssize_t tabsize = 8) const {
        return this->attr("expandtabs")(tabsize);
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`. */
    template <typename T>
    inline Py_ssize_t find(T&& sub, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        return PyUnicode_Find(
            this->ptr(),
            py::cast(std::forward<T>(sub)).ptr(),
            start,
            stop,
            1
        );
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`, except that the substring
    is given as a single Python unicode character. */
    inline Py_ssize_t find(Py_UCS4 ch, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
    }

    /* Equivalent to Python `str.format(*args, **kwargs)`. */
    template <typename... Args>
    inline String format(Args&&... args) const {
        return this->attr("format")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.format_map(mapping)`. */
    template <typename T>
    inline String format_map(T&& mapping) const {
        return this->attr("format_map")(std::forward<T>(mapping));
    }

    /* Equivalent to Python `str.index(sub[, start[, end]])`. */
    template <typename T>
    inline Py_ssize_t index(T&& sub, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        Py_ssize_t result = PyUnicode_Find(
            this->ptr(),
            py::cast(std::forward<T>(sub)).ptr(),
            start,
            stop,
            1
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.index(sub[, start[, end]])`, except that the substring
    is given as a single Python unicode character. */
    inline Py_ssize_t index(Py_UCS4 ch, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        Py_ssize_t result = PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.isalnum()`. */
    inline bool isalnum() const {
        return this->attr("isalnum")().cast<bool>();
    }

    /* Equivalent to Python `str.isalpha()`. */
    inline bool isalpha() const {
        return this->attr("isalpha")().cast<bool>();
    }

    /* Equivalent to Python `str.isascii()`. */
    inline bool isascii() const {
        return this->attr("isascii")().cast<bool>();
    }

    /* Equivalent to Python `str.isdecimal()`. */
    inline bool isdecimal() const {
        return this->attr("isdecimal")().cast<bool>();
    }

    /* Equivalent to Python `str.isdigit()`. */
    inline bool isdigit() const {
        return this->attr("isdigit")().cast<bool>();
    }

    /* Equivalent to Python `str.isidentifier()`. */
    inline bool isidentifier() const {
        return this->attr("isidentifier")().cast<bool>();
    }

    /* Equivalent to Python `str.islower()`. */
    inline bool islower() const {
        return this->attr("islower")().cast<bool>();
    }

    /* Equivalent to Python `str.isnumeric()`. */
    inline bool isnumeric() const {
        return this->attr("isnumeric")().cast<bool>();
    }

    /* Equivalent to Python `str.isprintable()`. */
    inline bool isprintable() const {
        return this->attr("isprintable")().cast<bool>();
    }

    /* Equivalent to Python `str.isspace()`. */
    inline bool isspace() const {
        return this->attr("isspace")().cast<bool>();
    }

    /* Equivalent to Python `str.istitle()`. */
    inline bool istitle() const {
        return this->attr("istitle")().cast<bool>();
    }

    /* Equivalent to Python `str.isupper()`. */
    inline bool isupper() const {
        return this->attr("isupper")().cast<bool>();
    }

    /* Equivalent of Python `str.join(iterable)`. */
    template <typename T>
    inline String join(T&& iterable) const {
        PyObject* result = PyUnicode_Join(
            this->ptr(),
            py::cast(std::forward<T>(iterable)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<String>(result);
    }

    /* Equivalent to Python `str.ljust(width[, fillchar])`. */
    template <typename... Args>
    inline String ljust(Args&&... args) const {
        return this->attr("ljust")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.lower()`. */
    inline String lower() const {
        return this->attr("lower")();
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <typename... Args>
    inline String lstrip(Args&&... args) const {
        return this->attr("lstrip")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python (static) `str.maketrans(x)`. */
    template <typename... Args> 
    inline static Dict maketrans(Args&&... args) {
        pybind11::type cls(
            reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyUnicode_Type))
        );
        return cls.attr("maketrans")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.partition(sep)`. */
    template <typename T>
    inline Tuple partition(T&& sep) const {
        return this->attr("partition")(std::forward<T>(sep));
    }

    /* Equivalent to Python `str.removeprefix(prefix)`. */
    template <typename T>
    inline String removeprefix(T&& prefix) const {
        return this->attr("removeprefix")(std::forward<T>(prefix));
    }

    /* Equivalent to Python `str.removesuffix(suffix)`. */
    template <typename T>
    inline String removesuffix(T&& suffix) const {
        return this->attr("removesuffix")(std::forward<T>(suffix));
    }


    /* Equivalent to Python `str.replace(old, new[, count])`. */
    template <typename T, typename U>
    inline String replace(T&& substr, U&& replstr, Py_ssize_t maxcount = -1) const {
        PyObject* result = PyUnicode_Replace(
            this->ptr(),
            py::cast(std::forward<T>(substr)).ptr(),
            py::cast(std::forward<U>(replstr)).ptr(),
            maxcount
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<String>(result);
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`. */
    template <typename T>
    inline Py_ssize_t rfind(T&& sub, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        return PyUnicode_Find(this->ptr(), sub, start, stop, -1);
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`, except that the
    substring is given as a single Python unicode character. */
    inline Py_ssize_t rfind(Py_UCS4 ch, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, -1);
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`. */
    template <typename T>
    inline Py_ssize_t rindex(T&& sub, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        Py_ssize_t result = PyUnicode_Find(this->ptr(), sub, start, stop, -1);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`, except that the
    substring is given as a single Python unicode character. */
    inline Py_ssize_t rindex(Py_UCS4 ch, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        Py_ssize_t result = PyUnicode_FindChar(this->ptr(), ch, start, stop, -1);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <typename... Args>
    inline String rjust(Args&&... args) const {
        return this->attr("rjust")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.rpartition(sep)`. */
    template <typename T>
    inline Tuple rpartition(T&& sep) const {
        return this->attr("rpartition")(std::forward<T>(sep));
    }

    /* Equivalent to Python `str.rsplit([sep[, maxsplit]])`. */
    template <typename... Args>
    inline List rsplit(Args&&... args) const {
        return this->attr("rsplit")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.rstrip([chars])`. */
    template <typename... Args>
    inline String rstrip(Args&&... args) const {
        return this->attr("rstrip")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.split()`. */
    inline List split(PyObject* separator = nullptr, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(this->ptr(), separator, maxsplit);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`. */
    template <typename T>
    inline List split(T&& separator, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(
            this->ptr(),
            py::cast(std::forward<T>(separator)).ptr(),
            maxsplit
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    inline List splitlines(bool keepends = false) const {
        PyObject* result = PyUnicode_Splitlines(this->ptr(), keepends);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.startswith(prefix[, start[, end]])`. */
    template <typename T>
    inline bool startswith(T&& prefix, Py_ssize_t start = 0, Py_ssize_t stop = -1) const {
        int result = PyUnicode_Tailmatch(
            this->ptr(),
            py::cast(std::forward<T>(prefix)).ptr(),
            start,
            stop,
            -1
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.strip([chars])`. */
    template <typename... Args>
    inline String strip(Args&&... args) const {
        return this->attr("strip")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.swapcase()`. */
    inline String swapcase() const {
        return this->attr("swapcase")();
    }

    /* Equivalent to Python `str.title()`. */
    inline String title() const {
        return this->attr("title")();
    }

    /* Equivalent to Python `str.translate(table)`. */
    template <typename T>
    inline String translate(T&& table) const {
        return this->attr("translate")(std::forward<T>(table));
    }

    /* Equivalent to Python `str.upper()`. */
    inline String upper() const {
        return this->attr("upper")();
    }

    /* Equivalent to Python `str.zfill(width)`. */
    inline String zfill(Py_ssize_t width) const {
        return this->attr("zfill")(width);
    }

};


/* Wrapper around a pybind11::type that enables extra C API functionality, such as the
ability to create new types on the fly by calling the type() metaclass, or directly
querying PyTypeObject* fields. */
class Type : pybind11::type {
    using Base = pybind11::type;

public:
    using Base::Base;
    using Base::operator=;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to the built-in type metaclass. */
    Type() : Base(
        reinterpret_borrow<Base>(reinterpret_cast<PyObject*>(&PyType_Type))
    ) {}

    /* Dynamically create a new Python type by calling the type() metaclass. */
    template <typename T, typename U, typename V>
    explicit Type(T&& name, U&& bases, V&& dict) : Base([&] {
        PyObject* result = PyObject_CallFunctionObjArgs(
            reinterpret_cast<PyObject*>(&PyType_Type),
            py::cast(std::forward<T>(name)).ptr(),
            py::cast(std::forward<U>(bases)).ptr(),
            py::cast(std::forward<V>(dict)).ptr(),
            nullptr
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    /* Create a new heap type from a CPython PyType_Spec*.  Note that this is not
    exactly interchangeable with a standard call to the type metaclass directly, as it
    does not invoke any of the __init__(), __new__(), __init_subclass__(), or
    __set_name__() methods for the type or any of its bases. */
    explicit Type(PyType_Spec* spec) : Base([&] {
        PyObject* result = PyType_FromSpec(spec);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}

    
    /* Create a new heap type from a CPython PyType_Spec and bases.  See
    Type(PyType_Spec*) for more information. */
    template <typename T>
    explicit Type(PyType_Spec* spec, T&& bases) : Base([&] {
        PyObject* result = PyType_FromSpecWithBases(spec, bases);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Base>(result);
    }()) {}


    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Create a new heap type from a module name, CPython PyType_Spec, and bases.
        See Type(PyType_Spec*) for more information. */
        template <typename T, typename U>
        explicit Type(T&& module, PyType_Spec* spec, U&& bases) : Base([&] {
            PyObject* result = PyType_FromModuleAndSpec(
                py::cast(std::forward<T>(module)).ptr(),
                spec,
                py::cast(std::forward<U>(bases)).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Base>(result);
        }()) {}

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Create a new heap type from a full CPython metaclass, module name,
        PyType_Spec and bases.  See Type(PyType_Spec*) for more information. */
        template <typename T, typename U, typename V>
        explicit Type(T&& metaclass, U&& module, PyType_Spec* spec, V&& bases) : Base([&] {
            PyObject* result = PyType_FromMetaClass(
                py::cast(std::forward<T>(metaclass)).ptr(),
                py::cast(std::forward<U>(module)).ptr(),
                spec,
                py::cast(std::forward<V>(bases)).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Base>(result);
        }()) {}

    #endif

    ///////////////////////////////////
    ////    PyTypeObject* SLOTS    ////
    ///////////////////////////////////

    /* A proxy for the type's PyTypeObject* struct. */
    struct Slots {
        PyTypeObject* type_obj;

        /* Get type's tp_name slot. */
        inline const char* name() const noexcept {
            return type_obj->tp_name;
        }

        /* Get the type's tp_basicsize slot. */
        inline Py_ssize_t basicsize() const noexcept {
            return type_obj->tp_basicsize;
        }

        /* Get the type's tp_itemsize slot. */
        inline Py_ssize_t itemsize() const noexcept {
            return type_obj->tp_itemsize;
        }

        /* Get the type's tp_dealloc slot. */
        inline destructor dealloc() const noexcept {
            return type_obj->tp_dealloc;
        }

        /* Get the type's tp_vectorcall slot. */
        inline Py_ssize_t tp_vectorcall_offset() const noexcept {
            return type_obj->tp_vectorcall_offset;
        }

        /* Get the type's tp_as_async slot. */
        inline PyAsyncMethods* as_async() const noexcept {
            return type_obj->tp_as_async;
        }

        /* Get the type's tp_repr slot. */
        inline reprfunc repr() const noexcept {
            return type_obj->tp_repr;
        }

        /* Get the type's tp_as_number slot. */
        inline PyNumberMethods* as_number() const noexcept {
            return type_obj->tp_as_number;
        }

        /* Get the type's tp_as_sequence slot. */
        inline PySequenceMethods* as_sequence() const noexcept {
            return type_obj->tp_as_sequence;
        }

        /* Get the type's tp_as_mapping slot. */
        inline PyMappingMethods* as_mapping() const noexcept {
            return type_obj->tp_as_mapping;
        }

        /* Get the type's tp_hash slot. */
        inline hashfunc hash() const noexcept {
            return type_obj->tp_hash;
        }

        /* Get the type's tp_call slot. */
        inline ternaryfunc call() const noexcept {
            return type_obj->tp_call;
        }

        /* Get the type's tp_str slot. */
        inline reprfunc str() const noexcept {
            return type_obj->tp_str;
        }

        /* Get the type's tp_getattro slot. */
        inline getattrofunc getattro() const noexcept {
            return type_obj->tp_getattro;
        }

        /* Get the type's tp_setattro slot. */
        inline setattrofunc setattro() const noexcept {
            return type_obj->tp_setattro;
        }

        /* Get the type's tp_as_buffer slot. */
        inline PyBufferProcs* as_buffer() const noexcept {
            return type_obj->tp_as_buffer;
        }

        /* Get the type's tp_flags slot. */
        inline unsigned long flags() const noexcept {
            return type_obj->tp_flags;
        }

        /* Get the type's tp_doc slot. */
        inline const char* doc() const noexcept {
            return type_obj->tp_doc;
        }

        /* Get the type's tp_traverse slot. */
        inline traverseproc traverse() const noexcept {
            return type_obj->tp_traverse;
        }

        /* Get the type's tp_clear slot. */
        inline inquiry clear() const noexcept {
            return type_obj->tp_clear;
        }

        /* Get the type's tp_richcompare slot. */
        inline richcmpfunc richcompare() const noexcept {
            return type_obj->tp_richcompare;
        }

        /* Get the type's tp_iter slot. */
        inline getiterfunc iter() const noexcept {
            return type_obj->tp_iter;
        }

        /* Get the type's tp_iternext slot. */
        inline iternextfunc iternext() const noexcept {
            return type_obj->tp_iternext;
        }

        /* Get the type's tp_methods slot. */
        inline PyMethodDef* methods() const noexcept {
            return type_obj->tp_methods;
        }

        /* Get the type's tp_members slot. */
        inline PyMemberDef* members() const noexcept {
            return type_obj->tp_members;
        }

        /* Get the type's tp_getset slot. */
        inline PyGetSetDef* getset() const noexcept {
            return type_obj->tp_getset;
        }

        /* Get the type's tp_base slot. */
        inline PyTypeObject* base() const noexcept {
            return type_obj->tp_base;
        }

        /* Get the type's tp_dict slot. */
        inline PyObject* dict() const noexcept {
            return type_obj->tp_dict;
        }

        /* Get the type's tp_descr_get slot. */
        inline descrgetfunc descr_get() const noexcept {
            return type_obj->tp_descr_get;
        }

        /* Get the type's tp_descr_set slot. */
        inline descrsetfunc descr_set() const noexcept {
            return type_obj->tp_descr_set;
        }

        /* Get the type's tp_bases slot. */
        inline Tuple bases() const noexcept {
            return reinterpret_borrow<Tuple>(type_obj->tp_bases);
        }

        /* Get the type's tp_mro slot. */
        inline Tuple mro() const noexcept {
            return reinterpret_borrow<Tuple>(type_obj->tp_mro);
        }

        /* Get the type's tp_finalize slot. */
        inline destructor finalize() const noexcept {
            return type_obj->tp_finalize;
        }

        /* Get the type's tp_vectorcall slot. */
        inline Py_ssize_t vectorcall() const noexcept {
            return type_obj->tp_vectorcall_offset;
        }

    };

    /* Access the type's internal slots. */
    inline Slots slots() const {
        return {reinterpret_cast<PyTypeObject*>(this->ptr())};
    }

    ///////////////////////////
    ////    API METHODS    ////
    ///////////////////////////

    /* Finalize a type object in a C++ extension, filling in any inherited slots.  This
    should be called on a raw CPython type struct to finish its initialization. */
    inline static void READY(PyTypeObject* type) {
        if (PyType_Ready(type) < 0) {
            throw error_already_set();
        }
    }

    /* Clear the lookup cache for the type and all of its subtypes.  This method must
    be called after any manual modification to the attributes or this class or any of
    its bases. */
    inline void clear_cache() const noexcept {
        PyType_Modified(reinterpret_cast<PyTypeObject*>(this->ptr()));
    }

    /* Check whether this type is an actual subtype of a another type.  This avoids
    calling `__subclasscheck__()` on the parent type. */
    inline bool is_subtype(const pybind11::type& base) const {
        return PyType_IsSubtype(
            reinterpret_cast<PyTypeObject*>(this->ptr()),
            reinterpret_cast<PyTypeObject*>(base.ptr())
        );
    }

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Get the module that the type is defined in.  Can throw if called on a static
        type rather than a heap type (one that was created using PyType_FromModuleAndSpec()
        or higher). */
        inline Module module_() const noexcept {
            PyObject* result = PyType_GetModule(static_cast<PyTypeObject*>(this->ptr()));
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Module>(result);
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

        /* Get the qualified name of the type */
        inline String<Ref::STEAL> qualname() const {
            PyObject* result = PyType_GetQualname(static_cast<PyTypeObject*>(this->ptr()));
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<String>(result);
        }

    #endif

};


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* NOTE: pybind11 does not support all of Python's built-in functions by default, which
 * makes the code to be more verbose and less idiomatic.  This can sometimes lead users
 * to reach for the Python C API, which is both unfamiliar and error-prone.  In keeping
 * with the Principle of Least Surprise, we attempt to replicate as much of the Python
 * standard library as possible here, so that users can more or less just copy/paste
 * from one to another.
 */


/* Equivalent to Python `import module` */
inline Module import(const char* module) {
    return pybind11::module_::import(module);
}


/* Equivalent to Python `locals()`. */
inline Dict locals() {
    PyObject* result = PyEval_GetLocals();
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_borrow<Dict>(result);
}


/* Equivalent to Python `type(obj)`. */
inline Type type(const pybind11::handle& obj) {
    return pybind11::type::of(obj);
}


/* Equivalent to Python `type(name, bases, namespace)`. */
template <typename T, typename U>
inline Type type(const pybind11::str& name, T&& bases, U&& dict) {
    return Type(name, std::forward<T>(bases), std::forward<U>(dict));
}


/* Equivalent to Python `type(name, bases, namespace)`. */
template <typename T, typename U>
inline Type type(const char* name, T&& bases, U&& dict) {
    return Type(name, std::forward<T>(bases), std::forward<U>(dict));
}


/* Equivalent to Python `type(name, bases, namespace)`. */
template <typename T, typename U>
inline Type type(const std::string& name, T&& bases, U&& dict) {
    return Type(name, std::forward<T>(bases), std::forward<U>(dict));
}


/* Equivalent to Python `type(name, bases, namespace)`. */
template <typename T, typename U>
inline Type type(const std::string_view& name, T&& bases, U&& dict) {
    return Type(name, std::forward<T>(bases), std::forward<U>(dict));
}


/* Equivalent to Python `issubclass(derived, base)`. */
template <typename T>
inline bool issubclass(const pybind11::type& derived, T&& base) {
    int result = PyObject_IsSubclass(derived.ptr(), cast(std::forward<T>(base)).ptr());
    if (result == -1) {
        throw error_already_set();
    }
    return result;
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
inline List dir(const pybind11::handle& obj) {
    if (obj.ptr() == nullptr) {
        throw TypeError("cannot call dir() on a null object");
    }
    PyObject* result = PyObject_Dir(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `callable(obj)`. */
inline bool callable(const pybind11::handle& obj) {
    return PyCallable_Check(obj.ptr());
}


/* Equivalent to Python `reversed(obj)`. */
inline Iterator reversed(const pybind11::handle& obj) {
    return obj.attr("__reversed__")();
}


/* Specialization of `reversed()` for Tuple objects that uses direct array access
rather than going through the Python API. */
inline Iterator reversed(const Tuple& obj) {
    using Iter = Tuple::ReverseIterator;
    return pybind11::make_iterator(Iter(obj.data(), obj.size() - 1), Iter(-1));
}


/* Equivalent to Python `next(obj)`. */
inline Object next(const pybind11::iterator& iter) {
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
template <typename T>
inline Object next(const pybind11::iterator& iter, T&& default_value) {
    PyObject* result = PyIter_Next(iter.ptr());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            throw error_already_set();
        }
        return cast(std::forward<T>(default_value));
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `abs(obj)`. */
inline Object abs(const pybind11::handle& obj) {
    PyObject* result = PyNumber_Absolute(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `math.floor(number)`.  Used to implement the built-in `round()`
function. */
inline Object floor(const pybind11::handle& number) {
    return number.attr("__floor__")();
}


/* Equivalent to Python `math.ceil(number)`.  Used to implement the built-in `round()`
function. */
inline Object ceil(const pybind11::handle& number) {
    return number.attr("__ceil__")();
}


/* Equivalent to Python `math.trunc(number)`.  Used to implement the built-in `round()`
function. */
inline Object trunc(const pybind11::handle& number) {
    return number.attr("__trunc__")();
}


/* Equivalent to Python str(). */
inline String str(const pybind11::handle& obj) {
    PyObject* string = PyObject_Str(obj.ptr());
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<String>(string);
}


/* Equivalent to Python `ascii(obj)`.  Like `repr()`, but returns an ASCII-encoded
string. */
inline String ascii(const pybind11::handle& obj) {
    PyObject* result = PyObject_ASCII(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<String>(result);
}


/* Equivalent to Python `bin(obj)`.  Converts an integer or other object implementing
__index__() into a binary string representation. */
inline String bin(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 2);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<String>(string);
}


/* Equivalent to Python `oct(obj)`.  Converts an integer or other object implementing
__index__() into an octal string representation. */
inline String oct(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 8);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<String>(string);
}


/* Equivalent to Python `hex(obj)`.  Converts an integer or other object implementing
__index__() into a hexadecimal string representation. */
inline String hex(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 16);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<String>(string);
}


/* Equivalent to Python `chr(obj)`.  Converts an integer or other object implementing
__index__() into a unicode character. */
inline String chr(const pybind11::handle& obj) {
    PyObject* string = PyUnicode_FromFormat("%llc", obj.cast<long long>());
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<String>(string);
}


/* Equivalent to Python `ord(obj)`.  Converts a unicode character into an integer
representation. */
Int ord(const pybind11::handle& obj) {
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


/* NOTE: some functions are not reachable in C++ compared to Python simply due to
 * language limitations.  For instance, C++ has no `**`, `//`, or `@` operators, so
 * `pow()`, `floor_div()`, and `matrix_multiply()` must be used instead.  Similarly,
 * the semantics of C++'s division and modulo operators differ from Python's, and
 * pybind11 uses the Python versions by default.  If C-like semantics are desired, the
 * `c_div()`, `c_mod()`, and `c_divmod()` functions can be used instead.
 */


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename T, typename U>
inline Object pow(T&& base, U&& exp) {
    PyObject* result = PyNumber_Power(
        cast(std::forward<T>(base)).ptr(),
        cast(std::forward<U>(exp)).ptr(),
        Py_None
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <typename T, typename U, typename V>
inline Object pow(T&& base, U&& exp, V&& mod) {
    PyObject* result = PyNumber_Power(
        cast(std::forward<T>(base)).ptr(),
        cast(std::forward<U>(exp)).ptr(),
        cast(std::forward<V>(mod)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `base **= exp` (in-place exponentiation). */
template <typename T, typename U>
inline Object inplace_pow(T&& base, U&& exp) {
    PyObject* result = PyNumber_InPlacePower(
        cast(std::forward<T>(base)).ptr(),
        cast(std::forward<U>(exp)).ptr(),
        Py_None
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* In-place modular exponentation.  Not reachable in Python, but implemented here for
completeness. */
template <typename T, typename U, typename V>
inline Object inplace_pow(T&& base, U&& exp, V&& mod) {
    PyObject* result = PyNumber_InPlacePower(
        cast(std::forward<T>(base)).ptr(),
        cast(std::forward<U>(exp)).ptr(),
        cast(std::forward<V>(mod)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `divmod(a, b)`. */
template <typename T, typename U>
inline Tuple divmod(T&& a, U&& b) {
    PyObject* result = PyNumber_Divmod(
        cast(std::forward<T>(a)).ptr(),
        cast(std::forward<U>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Tuple>(result);
}


/* Equivalent to Python `a // b` (floor division). */
template <typename T, typename U>
inline Object floor_div(T&& obj, U&& divisor) {
    PyObject* result = PyNumber_FloorDivide(
        cast(std::forward<T>(obj)).ptr(),
        cast(std::forward<U>(divisor)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `a //= b` (in-place floor division). */
template <typename T, typename U>
inline Object inplace_floor_div(T&& obj, U&& divisor) {
    PyObject* result = PyNumber_InPlaceFloorDivide(
        cast(std::forward<T>(obj)).ptr(),
        cast(std::forward<U>(divisor)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `a @ b` (matrix multiplication). */
template <typename T, typename U>
inline Object matrix_multiply(T&& a, U&& b) {
    PyObject* result = PyNumber_MatrixMultiply(
        cast(std::forward<T>(a)).ptr(),
        cast(std::forward<U>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `a @= b` (in-place matrix multiplication). */
template <typename T, typename U>
inline Object inplace_matrix_multiply(T&& a, U&& b) {
    PyObject* result = PyNumber_InPlaceMatrixMultiply(
        cast(std::forward<T>(a)).ptr(),
        cast(std::forward<U>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}





// TODO: round, sum, min, max, sorted, open, input, format, vars,


} // namespace py



// type casters for custom pybind11 types
namespace pybind11 {
namespace detail {


template <>
struct type_caster<py::Complex> {
    PYBIND11_TYPE_CASTER(py::Complex, _("Complex"));

    /* Convert a Python object into a Complex value. */
    bool load(handle src, bool convert) {
        if (PyComplex_Check(src.ptr())) {
            value = reinterpret_borrow<py::Complex>(src);
            return true;
        }

        if (!convert) {
            return false;
        }

        Py_complex complex_struct = PyComplex_AsCComplex(src.ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }

        value = py::Complex(complex_struct.real, complex_struct.imag);
        return true;
    }

    /* Convert a Complex value into a Python object. */
    inline static handle cast(
        const py::Complex& src,
        return_value_policy /* policy */,
        handle /* parent */
    ) {
        return src.ptr();
    }

};


// NOTE: pybind11 already implements a type caster for the generic std::complex<T>, so
// we can't override it directly.  However, we can specialize it further to handle the
// specific std::complex<> types that we want to use, which effectively bypasses the
// pybind11 implementation.  This is a bit of a hack, but it works.
#define COMPLEX_CAST_HACKS(T)                                                           \
    /* Convert a Python object into a std::complex<T> value. */                         \
    bool load(handle src, bool convert) {                                               \
        if (src.ptr() == nullptr) {                                                     \
            return false;                                                               \
        }                                                                               \
        if (!convert && !PyComplex_Check(src.ptr())) {                                  \
            return false;                                                               \
        }                                                                               \
        Py_complex complex_struct = PyComplex_AsCComplex(src.ptr());                    \
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {                          \
            PyErr_Clear();                                                              \
            return false;                                                               \
        }                                                                               \
        value = std::complex<T>(                                                        \
            static_cast<T>(complex_struct.real),                                        \
            static_cast<T>(complex_struct.imag)                                         \
        );                                                                              \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* Convert a std::complex<T> value into a Python object. */                         \
    inline static handle cast(                                                          \
        const std::complex<T>& src,                                                     \
        return_value_policy /* policy */,                                               \
        handle /* parent */                                                             \
    ) {                                                                                 \
        return py::Complex(src).release();                                              \
    }                                                                                   \


template <>
struct type_caster<std::complex<float>> {
    PYBIND11_TYPE_CASTER(std::complex<float>, _("complex"));
    COMPLEX_CAST_HACKS(float);
};
template <>
struct type_caster<std::complex<double>> {
    PYBIND11_TYPE_CASTER(std::complex<double>, _("complex"));
    COMPLEX_CAST_HACKS(double);
};
template <>
struct type_caster<std::complex<long double>> {
    PYBIND11_TYPE_CASTER(std::complex<long double>, _("complex"));
    COMPLEX_CAST_HACKS(long double);
};


} // namespace detail
} // namespace pybind11



#undef COMPLEX_CASTER_HACKS
#undef IMMUTABLE_SEQUENCE_METHODS
#endif // PYBIND_COMMON_H
