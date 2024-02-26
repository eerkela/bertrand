#ifndef BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_H

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <Python.h>
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/functional.h>
#include <pybind11/iostream.h>
#include <pybind11/numpy.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>


using namespace pybind11::literals;
namespace bertrand {
namespace py {


/////////////////////////////////////////
////     INHERITED FROM PYBIND11     ////
/////////////////////////////////////////


/* Pybind11 has rich support for converting between Python and C++ types, calling
 * Python functions from C++ (and vice versa), and exposing C++ types to Python.  We
 * don't change any of this behavior, meaning extensions should work with pybind11 as
 * expected.
 *
 * Pybind11 documentation:
 *     https://pybind11.readthedocs.io/en/stable/
 */


// binding functions
// using pybind11::cast;
using pybind11::reinterpret_borrow;
using pybind11::reinterpret_steal;
using pybind11::implicitly_convertible;
using pybind11::args_are_all_keyword_or_ds;
using pybind11::make_tuple;
using pybind11::make_iterator;
using pybind11::make_key_iterator;
using pybind11::make_value_iterator;
using pybind11::initialize_interpreter;
using pybind11::scoped_interpreter;
// PYBIND11_MODULE                      <- macros don't respect namespaces
// PYBIND11_EMBEDDED_MODULE
// PYBIND11_OVERRIDE
// PYBIND11_OVERRIDE_PURE
// PYBIND11_OVERRIDE_NAME
// PYBIND11_OVERRIDE_PURE_NAME
using pybind11::get_override;
using pybind11::cpp_function;
using pybind11::scoped_ostream_redirect;
using pybind11::scoped_estream_redirect;
using pybind11::add_ostream_redirect;


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

namespace detail = pybind11::detail;


//////////////////////////
////    EXCEPTIONS    ////
//////////////////////////


/* Pybind11 exposes some, but not all of the built-in Python errors.  We expand them
 * here so that users never reach for an error that doesn't exist, and we replicate the
 * standard error hierarchy so that users can use identical semantics to normal Python.
 *
 * CPython exception types:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * Hierarchy:
 *      https://docs.python.org/3/library/exceptions.html#exception-hierarchy
 */


// These exceptions have no python equivalent
using pybind11::error_already_set;
using Exception = pybind11::builtin_exception;
using CastError = pybind11::cast_error;
using ReferenceCastError = pybind11::reference_cast_error;


#define PYTHON_EXCEPTION(base, cls, exc)                                                \
    class PYBIND11_EXPORT_EXCEPTION cls : public base {                                 \
    public:                                                                             \
        using base::base;                                                               \
        cls() : cls("") {}                                                              \
        void set_error() const override { PyErr_SetString(exc, what()); }               \
    };                                                                                  \


PYTHON_EXCEPTION(Exception, ArithmeticError, PyExc_ArithmeticError)
    PYTHON_EXCEPTION(ArithmeticError, FloatingPointError, PyExc_OverflowError)
    PYTHON_EXCEPTION(ArithmeticError, OverflowError, PyExc_OverflowError)
    PYTHON_EXCEPTION(ArithmeticError, ZeroDivisionError, PyExc_ZeroDivisionError)
PYTHON_EXCEPTION(Exception, AssertionError, PyExc_AssertionError)
PYTHON_EXCEPTION(Exception, AttributeError, PyExc_AttributeError)
PYTHON_EXCEPTION(Exception, BufferError, PyExc_BufferError)
PYTHON_EXCEPTION(Exception, EOFError, PyExc_EOFError)
PYTHON_EXCEPTION(Exception, ImportError, PyExc_ImportError)
    PYTHON_EXCEPTION(ImportError, ModuleNotFoundError, PyExc_ModuleNotFoundError)
PYTHON_EXCEPTION(Exception, LookupError, PyExc_LookupError)
    PYTHON_EXCEPTION(LookupError, IndexError, PyExc_IndexError)
    PYTHON_EXCEPTION(LookupError, KeyError, PyExc_KeyError)
PYTHON_EXCEPTION(Exception, MemoryError, PyExc_MemoryError)
PYTHON_EXCEPTION(Exception, NameError, PyExc_NameError)
    PYTHON_EXCEPTION(NameError, UnboundLocalError, PyExc_UnboundLocalError)
PYTHON_EXCEPTION(Exception, OSError, PyExc_OSError)
    PYTHON_EXCEPTION(OSError, BlockingIOError, PyExc_BlockingIOError)
    PYTHON_EXCEPTION(OSError, ChildProcessError, PyExc_ChildProcessError)
    PYTHON_EXCEPTION(OSError, ConnectionError, PyExc_ConnectionError)
        PYTHON_EXCEPTION(ConnectionError, BrokenPipeError, PyExc_BrokenPipeError)
        PYTHON_EXCEPTION(ConnectionError, ConnectionAbortedError, PyExc_ConnectionAbortedError)
        PYTHON_EXCEPTION(ConnectionError, ConnectionRefusedError, PyExc_ConnectionRefusedError)
        PYTHON_EXCEPTION(ConnectionError, ConnectionResetError, PyExc_ConnectionResetError)
    PYTHON_EXCEPTION(OSError, FileExistsError, PyExc_FileExistsError)
    PYTHON_EXCEPTION(OSError, FileNotFoundError, PyExc_FileNotFoundError)
    PYTHON_EXCEPTION(OSError, InterruptedError, PyExc_InterruptedError)
    PYTHON_EXCEPTION(OSError, IsADirectoryError, PyExc_IsADirectoryError)
    PYTHON_EXCEPTION(OSError, NotADirectoryError, PyExc_NotADirectoryError)
    PYTHON_EXCEPTION(OSError, PermissionError, PyExc_PermissionError)
    PYTHON_EXCEPTION(OSError, ProcessLookupError, PyExc_ProcessLookupError)
    PYTHON_EXCEPTION(OSError, TimeoutError, PyExc_TimeoutError)
PYTHON_EXCEPTION(Exception, ReferenceError, PyExc_ReferenceError)
PYTHON_EXCEPTION(Exception, RuntimeError, PyExc_RuntimeError)
    PYTHON_EXCEPTION(RuntimeError, NotImplementedError, PyExc_NotImplementedError)
    PYTHON_EXCEPTION(RuntimeError, RecursionError, PyExc_RecursionError)
PYTHON_EXCEPTION(Exception, StopAsyncIteration, PyExc_StopAsyncIteration)
PYTHON_EXCEPTION(Exception, StopIteration, PyExc_StopIteration)
PYTHON_EXCEPTION(Exception, SyntaxError, PyExc_SyntaxError)
    PYTHON_EXCEPTION(SyntaxError, IndentationError, PyExc_IndentationError)
        PYTHON_EXCEPTION(IndentationError, TabError, PyExc_TabError)
PYTHON_EXCEPTION(Exception, SystemError, PyExc_SystemError)
PYTHON_EXCEPTION(Exception, TypeError, PyExc_TypeError)
PYTHON_EXCEPTION(Exception, ValueError, PyExc_ValueError)
    PYTHON_EXCEPTION(ValueError, UnicodeError, PyExc_UnicodeError)
        PYTHON_EXCEPTION(UnicodeError, UnicodeDecodeError, PyExc_UnicodeDecodeError)
        PYTHON_EXCEPTION(UnicodeError, UnicodeEncodeError, PyExc_UnicodeEncodeError)
        PYTHON_EXCEPTION(UnicodeError, UnicodeTranslateError, PyExc_UnicodeTranslateError)


#undef PYTHON_EXCEPTION


///////////////////////////////
////    WRAPPER CLASSES    ////
///////////////////////////////


/* Pybind11's wrapper classes cover most of the Python standard library, but not all of
 * it, and not with the same syntax.  They're also all given in lowercase C++ style,
 * which can cause ambiguities with native C++ types (e.g. pybind11::int_) and
 * non-member functions of the same name.  As such, we provide our own set of wrappers
 * that extend the pybind11 types and provide a more pythonic interface, backed by
 * optimized API calls.  These wrappers are designed to be used with nearly identical
 * semantics to the Python types they represent, making them more intuitive and easier
 * to use from C++.  They should be largely self-documenting.  For questions, refer to
 * the Python documentation first and then the source code for the types themselves,
 * which are provided in named header files within this directory.
 *
 * The final syntax is very similar to standard pybind11.  For example, the following
 * pybind11 code:
 *
 *    py::list foo = py::cast(std::vector<int>{1, 2, 3});
 *    py::int_ bar = foo.attr("pop")();
 *
 * Would be written as:
 *
 *    py::List foo {1, 2, 3};
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
 * Note also that these initializer lists can be of any type, including mixed types.
 *
 * Built-in Python types:
 *    https://docs.python.org/3/library/stdtypes.html
 */


// wrapper types
template <typename... Args>
using Class = pybind11::class_<Args...>;
using Module = pybind11::module_;
using Handle = pybind11::handle;
using NoneType = pybind11::none;  // TODO: lower into Object subclasses
using EllipsisType = pybind11::ellipsis;  // TODO: lower into Object subclasses
using Iterator = pybind11::iterator;
using WeakRef = pybind11::weakref;
using Capsule = pybind11::capsule;
using Buffer = pybind11::buffer;
using MemoryView = pybind11::memoryview;
using Bytes = pybind11::bytes;
using Bytearray = pybind11::bytearray;
class Object;
class NotImplementedType;
class Bool;
class Int;
class Float;
class Complex;
class Slice;
class Range;
class List;
class Tuple;
class Set;
class FrozenSet;
class KeysView;
class ItemsView;
class ValuesView;
class Dict;
class MappingProxy;
class Str;
class Type;
class Code;
class Frame;
class Function;
class Method;
class ClassMethod;
class StaticMethod;
class Property;
class Timedelta;
class Timezone;
class Date;
class Time;
class Datetime;
class Regex;  // incorporate more fully (write pybind11 bindings so that it can be passed into Python scripts)


namespace impl {

    #define UNARY_OPERATOR(Derived, op, endpoint)                                       \
        inline Object op() const {                                                      \
            PyObject* result = endpoint(static_cast<const Derived*>(this)->ptr());      \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            return reinterpret_steal<Object>(result);                                   \
        }                                                                               \

    #define COMPARISON_OPERATOR(Derived, op, endpoint)                                  \
        template <typename T>                                                           \
        inline bool op(T&& other) const {                                               \
            int result = PyObject_RichCompareBool(                                      \
                static_cast<const Derived*>(this)->ptr(),                               \
                detail::object_or_cast(std::forward<T>(other)).ptr(),                   \
                endpoint                                                                \
            );                                                                          \
            if (result == -1) {                                                         \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        }                                                                               \

    #define REVERSE_COMPARISON(Derived, op, endpoint)                                   \
        template <                                                                      \
            typename T,                                                                 \
            std::enable_if_t<!std::is_base_of_v<Object, std::decay_t<T>>, int> = 0      \
        >                                                                               \
        inline friend bool op(T&& other, const Derived& self) {                         \
            int result = PyObject_RichCompareBool(                                      \
                detail::object_or_cast(std::forward<T>(other)).ptr(),                   \
                self.ptr(),                                                             \
                endpoint                                                                \
            );                                                                          \
            if (result == -1) {                                                         \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        }                                                                               \

    #define BINARY_OPERATOR(Derived, op, endpoint)                                      \
        template <typename T>                                                           \
        inline Object op(T&& other) const {                                             \
            PyObject* result = endpoint(                                                \
                static_cast<const Derived*>(this)->ptr(),                               \
                detail::object_or_cast(std::forward<T>(other)).ptr()                    \
            );                                                                          \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            return reinterpret_steal<Object>(result);                                   \
        }                                                                               \

    #define REVERSE_OPERATOR(Derived, op, endpoint)                                     \
        template <                                                                      \
            typename T,                                                                 \
            std::enable_if_t<!std::is_base_of_v<Object, std::decay_t<T>>, int> = 0      \
        >                                                                               \
        inline friend Object op(T&& other, const Derived& self) {                       \
            PyObject* result = endpoint(                                                \
                detail::object_or_cast(std::forward<T>(other)).ptr(),                   \
                self.ptr()                                                              \
            );                                                                          \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            return reinterpret_steal<Object>(result);                                   \
        }                                                                               \

    #define CONVERTABLE_ACCESSOR(name, base)                                            \
        struct name : public detail::base {                                             \
            using detail::base::base;                                                   \
            using detail::base::operator=;                                              \
            name(const detail::base& accessor) : detail::base(accessor) {}              \
            name(detail::base&& accessor) : detail::base(std::move(accessor)) {}        \
                                                                                        \
            template <                                                                  \
                typename T,                                                             \
                std::enable_if_t<!detail::is_pyobject<T>::value, int> = 0               \
            >                                                                           \
            inline operator T() const {                                                 \
                return detail::base::template cast<T>();                                \
            }                                                                           \
        };                                                                              \

    CONVERTABLE_ACCESSOR(ObjAttrAccessor, obj_attr_accessor)
    CONVERTABLE_ACCESSOR(StrAttrAccessor, str_attr_accessor)
    CONVERTABLE_ACCESSOR(ItemAccessor, item_accessor)
    CONVERTABLE_ACCESSOR(SequenceAccessor, sequence_accessor)
    CONVERTABLE_ACCESSOR(TupleAccessor, tuple_accessor)
    CONVERTABLE_ACCESSOR(ListAccessor, list_accessor)

    #undef CONVERTABLE_ACCESSOR

}


/* A revised pybind11::object interface that allows explicit conversion to any C++ type
as well as cross-language math operators, which can convert C++ inputs into Python
objects before calling the CPython API. */
class Object : public pybind11::object {
    using Base = pybind11::object;

protected:
    using SliceIndex = std::variant<long long, pybind11::none>;

    template <typename Derived>
    static TypeError noconvert(PyObject* obj) {
        pybind11::type source = pybind11::type::of(obj);
        pybind11::type dest = Derived::Type;
        const char* source_name = reinterpret_cast<PyTypeObject*>(source.ptr())->tp_name;
        const char* dest_name = reinterpret_cast<PyTypeObject*>(dest.ptr())->tp_name;

        std::ostringstream msg;
        msg << "could not construct '" << dest_name << "' from object of type '";
        msg << source_name << "'";
        return TypeError(msg.str());
    }

public:
    static py::Type Type;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    using Base::Base;
    using Base::operator=;

    /* Default constructor.  Initializes to None. */
    Object() : Base(pybind11::none()) {}

    /* Adopt an existing object with proper reference counting. */
    Object(const pybind11::object& o) : Base(o) {}
    Object(pybind11::object&& o) : Base(std::move(o)) {}

    /* Convert an accessor proxy into a new object. */
    template <typename Policy>
    Object(const ::pybind11::detail::accessor<Policy> &a) :
        Base(pybind11::object(a))
    {}

    /* Convert any C++ value into an arbitrary python object and wrap the result. */
    template <typename T, std::enable_if_t<!detail::is_pyobject<T>::value, int> = 0>
    Object(T&& value) :
        Base(pybind11::cast(std::forward<T>(value)).release(), stolen_t{})
    {}

    /* Assign an arbitrary value to the object wrapper. */
    template <typename T, std::enable_if_t<!detail::is_pyobject<T>::value, int> = 0>
    Object& operator=(T&& value) {
        Base::operator=(Object(std::forward<T>(value)));
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert an Object into a bool.  Equivalent to Python `bool(obj)`. */
    inline operator bool() const {
        int result = PyObject_IsTrue(this->ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Explicitly cast to a string representation.  Equivalent to Python `str(obj)`. */
    inline explicit operator std::string() const {
        PyObject* str = PyObject_Str(this->ptr());
        if (str == nullptr) {
            throw error_already_set();
        }
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(str, &size);
        Py_DECREF(str);
        if (data == nullptr) {
            throw error_already_set();
        }
        return std::string(data, size);
    }

    /* Explicitly cast to any other type.  Uses pybind11 to search for a conversion. */
    template <typename T>
    inline explicit operator T() const {
        return Base::cast<T>();
    }

    ////////////////////////////////
    ////    ATTRIBUTE ACCESS    ////
    ////////////////////////////////

    /* Bertrand streamlines pybind11's dotted attribute accessors by allowing them to
     * be implicitly converted to any C++ type, reducing the amount of boilerplate
     * needed to interact with Python objects in C++.  This brings them in line with
     * the generic Object API, and makes the code significantly more idiomatic from
     * both a Python and C++ perspective.
     */

    inline impl::ObjAttrAccessor attr(handle key) const {
        return Base::attr(key);
    }

    inline impl::ObjAttrAccessor attr(Object&& key) const {
        return Base::attr(std::move(key));
    }

    inline impl::StrAttrAccessor attr(const char* key) const {
        return Base::attr(key);
    }

    inline impl::StrAttrAccessor attr(const std::string& key) const {
        return Base::attr(key.c_str());
    }

    inline impl::StrAttrAccessor attr(const std::string_view& key) const {
        return Base::attr(key.data());
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* Bertrand streamlines pybind11's index interface by allowing accessors to be
     * implicitly converted to any C++ type, reducing the amount of boilerplate needed
     * to interact with Python objects.  It also implements a generalized slice syntax
     * for sequence types using an initializer list to represent the slice.  This is
     * equivalent to Python's `obj[start:stop:step]` syntax, and is directly converted
     * to it in the background.  This allows arbitrary Objects to be sliced as if they
     * were sequences, and throws compilation errors if the slice is not composed of
     * integers or None.
     */

    inline impl::ItemAccessor operator[](handle key) const {
        return Base::operator[](key);
    }

    inline impl::ItemAccessor operator[](Object&& key) const {
        return Base::operator[](std::move(key));
    }

    inline impl::ItemAccessor operator[](const char* key) const {
        return Base::operator[](key);
    }

    /* Access an item from the dict using a string. */
    inline impl::ItemAccessor operator[](const std::string& key) const {
        return (*this)[key.c_str()];
    }

    /* Access an item from the dict using a string. */
    inline impl::ItemAccessor operator[](const std::string_view& key) const {
        return (*this)[key.data()];
    }

    impl::ItemAccessor operator[](const std::initializer_list<SliceIndex>& slice) const {
        if (slice.size() > 3) {
            throw ValueError(
                "slices must be of the form {[start[, stop[, step]]]}"
            );
        }
        pybind11::none None;
        size_t i = 0;
        std::array<Object, 3> params {None, None, None};
        for (const SliceIndex& item : slice) {
            params[i++] = std::visit(
                [](auto&& arg) -> Object {
                    return detail::object_or_cast(arg);
                },
                item
            );
        }
        return Base::operator[](pybind11::slice(params[0], params[1], params[2]));
    }

    template <typename T, std::enable_if_t<!detail::is_pyobject<T>::value, int> = 0>
    inline impl::ItemAccessor operator[](T&& key) const {
        return (*this)[detail::object_or_cast(std::forward<T>(key))];
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* pybind11 exposes generalized operator overloads for python operands, but does
    * not allow mixed Python/C++ inputs.  For ease of use, we enable them here, along
    * with reverse operators and in-place equivalents.
    */

    #define INPLACE_OPERATOR(op, endpoint)                                              \
        template <typename T>                                                           \
        inline Object& op(T&& other) {                                                  \
            pybind11::object o = detail::object_or_cast(std::forward<T>(other));        \
            PyObject* result = endpoint(this->ptr(), o.ptr());                          \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            if (result == this->ptr()) {                                                \
                Py_DECREF(result);                                                      \
            } else {                                                                    \
                *this = reinterpret_steal<Object>(result);                              \
            }                                                                           \
            return *this;                                                               \
        }                                                                               \

    UNARY_OPERATOR(Object, operator+, PyNumber_Positive);
    UNARY_OPERATOR(Object, operator-, PyNumber_Negative);
    UNARY_OPERATOR(Object, operator~, PyNumber_Invert);
    COMPARISON_OPERATOR(Object, operator<, Py_LT);
    COMPARISON_OPERATOR(Object, operator<=, Py_LE);
    COMPARISON_OPERATOR(Object, operator==, Py_EQ);
    COMPARISON_OPERATOR(Object, operator!=, Py_NE);
    COMPARISON_OPERATOR(Object, operator>, Py_GT);
    COMPARISON_OPERATOR(Object, operator>=, Py_GE);
    REVERSE_COMPARISON(Object, operator<, Py_GT);
    REVERSE_COMPARISON(Object, operator<=, Py_GE);
    REVERSE_COMPARISON(Object, operator==, Py_EQ);
    REVERSE_COMPARISON(Object, operator!=, Py_NE);
    REVERSE_COMPARISON(Object, operator>, Py_LT);
    REVERSE_COMPARISON(Object, operator>=, Py_LE);
    BINARY_OPERATOR(Object, operator+, PyNumber_Add);
    BINARY_OPERATOR(Object, operator-, PyNumber_Subtract);
    BINARY_OPERATOR(Object, operator*, PyNumber_Multiply);
    BINARY_OPERATOR(Object, operator/, PyNumber_TrueDivide);
    BINARY_OPERATOR(Object, operator%, PyNumber_Remainder);
    BINARY_OPERATOR(Object, operator<<, PyNumber_Lshift);
    BINARY_OPERATOR(Object, operator>>, PyNumber_Rshift);
    BINARY_OPERATOR(Object, operator&, PyNumber_And);
    BINARY_OPERATOR(Object, operator|, PyNumber_Or);
    BINARY_OPERATOR(Object, operator^, PyNumber_Xor);
    REVERSE_OPERATOR(Object, operator+, PyNumber_Add);
    REVERSE_OPERATOR(Object, operator-, PyNumber_Subtract);
    REVERSE_OPERATOR(Object, operator*, PyNumber_Multiply);
    REVERSE_OPERATOR(Object, operator/, PyNumber_TrueDivide);
    REVERSE_OPERATOR(Object, operator%, PyNumber_Remainder);
    REVERSE_OPERATOR(Object, operator<<, PyNumber_Lshift);
    REVERSE_OPERATOR(Object, operator>>, PyNumber_Rshift);
    REVERSE_OPERATOR(Object, operator&, PyNumber_And);
    REVERSE_OPERATOR(Object, operator|, PyNumber_Or);
    REVERSE_OPERATOR(Object, operator^, PyNumber_Xor);
    INPLACE_OPERATOR(operator+=, PyNumber_InPlaceAdd);
    INPLACE_OPERATOR(operator-=, PyNumber_InPlaceSubtract);
    INPLACE_OPERATOR(operator*=, PyNumber_InPlaceMultiply);
    INPLACE_OPERATOR(operator/=, PyNumber_InPlaceTrueDivide);
    INPLACE_OPERATOR(operator%=, PyNumber_InPlaceRemainder);
    INPLACE_OPERATOR(operator<<=, PyNumber_InPlaceLshift);
    INPLACE_OPERATOR(operator>>=, PyNumber_InPlaceRshift);
    INPLACE_OPERATOR(operator&=, PyNumber_InPlaceAnd);
    INPLACE_OPERATOR(operator|=, PyNumber_InPlaceOr);
    INPLACE_OPERATOR(operator^=, PyNumber_InPlaceXor);

    #undef INPLACE_OPERATOR

    inline Object* operator&() {
        return this;
    }

    inline const Object* operator&() const {
        return this;
    }

    inline friend std::ostream& operator<<(std::ostream& os, const Object& obj) {
        os << static_cast<std::string>(obj);
        return os;
    }

};


const static NoneType None;
const static EllipsisType Ellipsis;


namespace impl {

    /* A simple struct that converts a generic C++ object into a Python equivalent in
    its constructor.  This is used in conjunction with std::initializer_list to parse
    mixed-type lists in a type-safe manner.

    NOTE: this incurs a small performance penalty, effectively requiring an extra loop
    over the initializer list before the function is called.  Users can avoid this by
    providing a homogenous `std::initializer_list<T>` overload alongside
    `std::iniitializer_list<Initializer>`.  This causes conversions to be deferred to
    the function body, which may be able to handle them more efficiently in a single
    loop. */
    struct Initializer {
        Object value;

        /* We disable initializer construction from pybind11::handle in order not to
        * interfere with pybind11's reinterpret_borrow/reinterpret_steal helper functions,
        * which use initializer lists internally.  Disabling the generic initializer list
        * constructor whenever a handle is passed in allows reinterpret_borrow/
        * reinterpret_steal to continue to work as expected.
        */

        template <
            typename T,
            std::enable_if_t<!std::is_same_v<pybind11::handle, std::decay_t<T>>, int> = 0
        >
        Initializer(T&& value) : value(detail::object_or_cast(std::forward<T>(value))) {}
    };

    /* A mixin that adds type-safe in-place operators.  These will throw a CastError
    if an in-place operator modifies the type of its left operand. */
    template <typename Derived>
    struct Ops {

        #define INPLACE_OPERATOR(op, endpoint)                                          \
            template <typename T>                                                       \
            inline Derived& op(T&& other) {                                             \
                Derived* self = static_cast<Derived*>(this);                            \
                pybind11::object o = detail::object_or_cast(std::forward<T>(other));    \
                PyObject* result = endpoint(self->ptr(), o.ptr());                      \
                if (result == nullptr) {                                                \
                    throw error_already_set();                                          \
                }                                                                       \
                if (result == self->ptr()) {                                            \
                    Py_DECREF(result);                                                  \
                } else if (!Derived::check_(result)) {                                  \
                    pybind11::str otype = pybind11::repr(pybind11::type::of(o));        \
                    pybind11::str rtype = pybind11::repr(pybind11::type::of(result));   \
                    std::ostringstream msg;                                             \
                    msg << "applying " << #op << " to operands of type (";              \
                    msg << pybind11::repr(pybind11::type::of(*self)) << ", " << otype;  \
                    msg << ") gave result of type " << rtype;                           \
                    throw CastError(msg.str());                                         \
                } else {                                                                \
                    *self = reinterpret_steal<Derived>(result);                         \
                }                                                                       \
                return *self;                                                           \
            }                                                                           \

        UNARY_OPERATOR(Derived, operator+, PyNumber_Positive);
        UNARY_OPERATOR(Derived, operator-, PyNumber_Negative);
        UNARY_OPERATOR(Derived, operator~, PyNumber_Invert);
        COMPARISON_OPERATOR(Derived, operator<, Py_LT);
        COMPARISON_OPERATOR(Derived, operator<=, Py_LE);
        COMPARISON_OPERATOR(Derived, operator==, Py_EQ);
        COMPARISON_OPERATOR(Derived, operator!=, Py_NE);
        COMPARISON_OPERATOR(Derived, operator>, Py_GT);
        COMPARISON_OPERATOR(Derived, operator>=, Py_GE);
        REVERSE_COMPARISON(Derived, operator<, Py_GT);
        REVERSE_COMPARISON(Derived, operator<=, Py_GE);
        REVERSE_COMPARISON(Derived, operator==, Py_EQ);
        REVERSE_COMPARISON(Derived, operator!=, Py_NE);
        REVERSE_COMPARISON(Derived, operator>, Py_LT);
        REVERSE_COMPARISON(Derived, operator>=, Py_LE);
        BINARY_OPERATOR(Derived, operator+, PyNumber_Add);
        BINARY_OPERATOR(Derived, operator-, PyNumber_Subtract);
        BINARY_OPERATOR(Derived, operator*, PyNumber_Multiply);
        BINARY_OPERATOR(Derived, operator/, PyNumber_TrueDivide);
        BINARY_OPERATOR(Derived, operator%, PyNumber_Remainder);
        BINARY_OPERATOR(Derived, operator<<, PyNumber_Lshift);
        BINARY_OPERATOR(Derived, operator>>, PyNumber_Rshift);
        BINARY_OPERATOR(Derived, operator&, PyNumber_And);
        BINARY_OPERATOR(Derived, operator|, PyNumber_Or);
        BINARY_OPERATOR(Derived, operator^, PyNumber_Xor);
        REVERSE_OPERATOR(Derived, operator+, PyNumber_Add);
        REVERSE_OPERATOR(Derived, operator-, PyNumber_Subtract);
        REVERSE_OPERATOR(Derived, operator*, PyNumber_Multiply);
        REVERSE_OPERATOR(Derived, operator/, PyNumber_TrueDivide);
        REVERSE_OPERATOR(Derived, operator%, PyNumber_Remainder);
        REVERSE_OPERATOR(Derived, operator<<, PyNumber_Lshift);
        REVERSE_OPERATOR(Derived, operator>>, PyNumber_Rshift);
        REVERSE_OPERATOR(Derived, operator&, PyNumber_And);
        REVERSE_OPERATOR(Derived, operator|, PyNumber_Or);
        REVERSE_OPERATOR(Derived, operator^, PyNumber_Xor);
        INPLACE_OPERATOR(operator+=, PyNumber_InPlaceAdd);
        INPLACE_OPERATOR(operator-=, PyNumber_InPlaceSubtract);
        INPLACE_OPERATOR(operator*=, PyNumber_InPlaceMultiply);
        INPLACE_OPERATOR(operator/=, PyNumber_InPlaceTrueDivide);
        INPLACE_OPERATOR(operator%=, PyNumber_InPlaceRemainder);
        INPLACE_OPERATOR(operator<<=, PyNumber_InPlaceLshift);
        INPLACE_OPERATOR(operator>>=, PyNumber_InPlaceRshift);
        INPLACE_OPERATOR(operator&=, PyNumber_InPlaceAnd);
        INPLACE_OPERATOR(operator|=, PyNumber_InPlaceOr);
        INPLACE_OPERATOR(operator^=, PyNumber_InPlaceXor);

        #undef INPLACE_OPERATOR
    };

    #undef UNARY_OPERATOR
    #undef COMPARISON_OPERATOR
    #undef REVERSE_COMPARISON
    #undef BINARY_OPERATOR
    #undef REVERSE_OPERATOR

    #define BERTRAND_PYTHON_OPERATORS(Mixin)                                            \
        using Mixin::operator~;                                                         \
        using Mixin::operator<;                                                         \
        using Mixin::operator<=;                                                        \
        using Mixin::operator==;                                                        \
        using Mixin::operator!=;                                                        \
        using Mixin::operator>=;                                                        \
        using Mixin::operator>;                                                         \
        using Mixin::operator+;                                                         \
        using Mixin::operator-;                                                         \
        using Mixin::operator*;                                                         \
        using Mixin::operator/;                                                         \
        using Mixin::operator%;                                                         \
        using Mixin::operator<<;                                                        \
        using Mixin::operator>>;                                                        \
        using Mixin::operator&;                                                         \
        using Mixin::operator|;                                                         \
        using Mixin::operator^;                                                         \
        using Mixin::operator+=;                                                        \
        using Mixin::operator-=;                                                        \
        using Mixin::operator*=;                                                        \
        using Mixin::operator/=;                                                        \
        using Mixin::operator%=;                                                        \
        using Mixin::operator<<=;                                                       \
        using Mixin::operator>>=;                                                       \
        using Mixin::operator&=;                                                        \
        using Mixin::operator|=;                                                        \
        using Mixin::operator^=;                                                        \

    /* Mixin holding operator overloads for types implementing the sequence protocol,
    which makes them both concatenatable and repeatable. */
    template <typename Derived>
    struct SequenceOps {

        /* Equivalent to Python `sequence.count(value)`, but also takes optional
        start/stop indices similar to `sequence.index()`. */
        template <typename T>
        inline Py_ssize_t count(
            T&& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(
                    static_cast<const Derived*>(this)->ptr(),
                    start,
                    stop
                );
                if (slice == nullptr) {
                    throw error_already_set();
                }
                Py_ssize_t result = PySequence_Count(
                    slice,
                    detail::object_or_cast(std::forward<T>(value)).ptr()
                );
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Count(
                    static_cast<const Derived*>(this)->ptr(),
                    detail::object_or_cast(std::forward<T>(value)).ptr()
                );
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            }
        }

        /* Equivalent to Python `s.index(value[, start[, stop]])`. */
        template <typename T>
        inline Py_ssize_t index(
            T&& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(
                    static_cast<const Derived*>(this)->ptr(),
                    start,
                    stop
                );
                if (slice == nullptr) {
                    throw error_already_set();
                }
                Py_ssize_t result = PySequence_Index(
                    slice,
                    detail::object_or_cast(std::forward<T>(value)).ptr()
                );
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Index(
                    static_cast<const Derived*>(this)->ptr(),
                    detail::object_or_cast(std::forward<T>(value)).ptr()
                );
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            }
        }

        /* Equivalent to Python `sequence + items`. */
        template <typename T>
        inline Derived concat(T&& items) const {
            PyObject* result = PySequence_Concat(
                static_cast<const Derived*>(this)->ptr(),
                detail::object_or_cast(std::forward<T>(items)).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Derived>(result);
        }

        /* Equivalent to Python `sequence * repetitions`. */
        inline Derived repeat(Py_ssize_t repetitions) const {
            PyObject* result = PySequence_Repeat(
                static_cast<const Derived*>(this)->ptr(),
                repetitions
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Derived>(result);
        }

        template <typename T>
        inline Derived operator+(T&& items) const {
            return static_cast<const Derived*>(this)->concat(std::forward<T>(items));
        }

        inline Derived operator*(Py_ssize_t repetitions) {
            return static_cast<const Derived*>(this)->repeat(repetitions);
        }

        friend inline Derived operator*(Py_ssize_t repetitions, const Derived& seq) {
            return seq.repeat(repetitions);
        }

        template <typename T>
        inline Derived& operator+=(T&& items) {
            Derived* self = static_cast<Derived*>(this);
            PyObject* result = PySequence_InPlaceConcat(
                self->ptr(),
                detail::object_or_cast(std::forward<T>(items)).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            if (result == self->ptr()) {
                Py_DECREF(result);
            } else {
                *self = reinterpret_steal<Derived>(result);
            }
            return *self;
        }

        inline Derived& operator*=(Py_ssize_t repetitions) {
            Derived* self = static_cast<Derived*>(this);
            PyObject* result = PySequence_InPlaceRepeat(self->ptr(), repetitions);
            if (result == nullptr) {
                throw error_already_set();
            }
            if (result == self->ptr()) {
                Py_DECREF(result);
            } else {
                *self = reinterpret_steal<Derived>(result);
            }
            return *self;
        }

    };

    /* Mixin holding an optimized reverse iterator for data structures that allow
    direct access to the underlying array.  This avoids the overhead of going through
    the Python interpreter to obtain a reverse iterator, and brings it up to parity
    with forward iteration. */
    template <typename Derived>
    struct ReverseIterable {
        /* An optimized reverse iterator that bypasses the python interpreter. */
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

            inline Handle operator*() const {
                return array[index];
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

    /* Convert an arbitrary Python/C++ value into a pybind11 object and then return a
    new reference.  This is used in the constructors of list and tuple objects, whose
    SET_ITEM() functions steal a reference.  Converts null pointers into Python None. */
    template <typename T>
    static inline PyObject* convert_newref(T&& value) {
        if constexpr (detail::is_pyobject<T>::value) {
            PyObject* result = value.ptr();
            if (result == nullptr) {
                result = Py_None;
            }
            return Py_NewRef(result);
        } else {
            PyObject* result = pybind11::cast(std::forward<T>(value)).release().ptr();
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }
    }

    template <typename Base, typename T>
    using enable_for = std::enable_if_t<std::is_base_of_v<Base, T>>;

    /* Types for which is_reverse_iterable is true will use custom reverse iterators
    when py::reversed() is called on them.  These types must expose a ReverseIterator
    class that implements the reverse iteration. */
    template <typename T, typename = void>
    constexpr bool is_reverse_iterable = false;
    template <typename T>
    constexpr bool is_reverse_iterable<T, enable_for<Tuple, T>> = true;
    template <typename T>
    constexpr bool is_reverse_iterable<T, enable_for<List, T>> = true;

    /* All new subclasses of pybind11::object must define these constructors, which are
    taken directly from PYBIND11_OBJECT_COMMON.  The check() function will be called
    if the constructor is invoked with a generic object as input, and the convert()
    function will be called if it fails that check.  Both should operate on raw
    PyObject* pointers, and convert() should always return a new reference.  They will
    never be passed null objects. */
    #define BERTRAND_PYTHON_CONSTRUCTORS(parent, cls, check, convert)                   \
        PYBIND11_OBJECT_CVT(cls, parent, check, convert);                               \
        using parent::operator=;                                                        \
        cls& operator=(const pybind11::object& obj) {                                   \
            if (check_(obj.ptr())) {                                                    \
                parent::operator=(reinterpret_borrow<cls>(obj.ptr()));                  \
            } else {                                                                    \
                std::ostringstream msg;                                                 \
                msg << "cannot convert " << pybind11::repr(pybind11::type::of(obj));    \
                msg << " to " << #cls;                                                  \
                throw CastError(msg.str());                                             \
            }                                                                           \
            return *this;                                                               \
        }                                                                               \
        template <typename T, std::enable_if_t<!detail::is_pyobject<T>::value, int> = 0>\
        cls& operator=(T&& value) {                                                     \
            parent::operator=(cls(std::forward<T>(value)));                             \
            return *this;                                                               \
        }                                                                               \
        template <typename T>                                                           \
        cls& operator=(const std::initializer_list<T>& init) {                          \
            parent::operator=(cls(init));                                               \
            return *this;                                                               \
        }                                                                               \
        inline cls* operator&() {                                                       \
            return this;                                                                \
        }                                                                               \
        inline const cls* operator&() const {                                           \
            return this;                                                                \
        }                                                                               \

}  // namespace impl


/* New subclass of pybind11::object that represents Python's global NotImplemented
object. */
class NotImplementedType : public Object, public impl::Ops<NotImplementedType> {

    inline static int check_not_implemented(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) Py_TYPE(Py_NotImplemented));
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_not_implemented(PyObject* obj) {
        throw TypeError("cannot convert object to NotImplemented");
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(
        Object,
        NotImplementedType,
        check_not_implemented,
        convert_not_implemented
    );

    NotImplementedType() : Object(Py_NotImplemented, borrowed_t{}) {}

    using impl::Ops<NotImplementedType>::operator==;
    using impl::Ops<NotImplementedType>::operator!=;
};


const static NotImplementedType NotImplemented;


/* TODO: at some point in the future, we could potentially support static typing for
 * python containers:
 *
 *     py::List foo {1, 2, 3};
 *     py::TypedList<int> bar = foo;
 *
 * List would just be a type alias for `TypedList<void>`, which would enable dynamic
 * typing.  If specialized on a subclass of `pybind11::object`, we would apply an
 * isinstance check whenever an item is added to the list.  If specialized on a C++
 * type, then we could use the compiler to enable static checks.  This would add a lot
 * of code, but it would be really awesome for cross-language type safety.  We could
 * also potentially optimize for C++ types that are supported in the CPython API.  But
 * again, that would create a lot of extra code.
 */


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* Similarly, pybind11 exposes a fair number of built-in Python functions, but not all.
 * This can cause confusion when users try to port Python code to C++, only to find that
 * they need to do some arcane trickery to get simple things like `range()` to work as
 * expected.  As such, we provide our own set of global functions that exactly replicate
 * the Python builtins, and can be used with identical semantics.
 *
 * Built-in Python functions:
 *      https://docs.python.org/3/library/functions.html
 *
 * NOTE: many of these functions are overloaded to work with both Python and C++ types
 * interchangeably, which makes it possible to mix and match Python and C++ types in a
 * single expression.  Here's a list of built-in functions that support this:
 *
 *  py::hash() - delegates to std::hash for all types, which is overloaded to redirect
 *      to Python __hash__ if it exists.  Unhashable types will raise a compile-time
 *      error.
 *
 *  py::len() - returns either Python len() or C++ size() for containers that have it,
 *      or std::nullopt for types that don't.
 *
 *  py::iter() - either returns Python __iter__() or generates a new Python iterator
 *      from a C++ container's begin() and end() methods.  Passing an rvalue raises a
 *      compile-time error.
 *
 *  py::reversed() - either returns Python __reversed__() or generates a new Python
 *      iterator from a C++ container's rbegin() and rend() methods.  Passing an rvalue
 *      raises a compile-time error.
 *
 *  py::callable<Args...>() - for Python objects, checks callable(obj) and then
 *      optionally inspects the signature and ensures a match.  For C++ objects,
 *      delegates to std::is_invocable_v<Args...>.
 *
 *          NOTE: currently, python callables are only checked for the number of
 *          arguments, not their types.  It might be possible to add type safety in the
 *          future, but there are many blockers before that can happen.
 *
 *  py::str() - delegates to std::to_string() and the stream insertion operator (<<)
 *      for C++ types that support it.  Otherwise, attempts to convert to a Python type
 *      and then calls str() on it.
 *  
 *  py::repr() - similar to py::str(), except that it calls Python repr() rather than
 *      str(), and adds a fallback to typeid().name() if all else fails.
 *
 *  ... As well as all math operators (e.g. abs(), pow(), truediv(), etc.) which are
 *  generic and can be used with any combination of Python and C++ types.
 */


namespace impl {

    /* SFINAE struct allows py::iter() to work on both python and C++ types. */
    template <typename T, typename = void>
    static constexpr bool use_pybind11_iter = false;
    template <typename T>
    static constexpr bool use_pybind11_iter<
        T,
        std::void_t<decltype(pybind11::iter(std::declval<T>()))>
    > = true;

    /* SFINAE struct allows py::hash() to raise informative error messages for
    unhashable types. */
    template <typename T, typename = void>
    static constexpr bool has_std_hash = false;
    template <typename T>
    static constexpr bool has_std_hash<T, std::void_t<decltype(std::hash<T>{})>> = true;

    /* SFINAE trait that detects whether a C++ container has an STL-style `size()`
    method. */
    template <typename T, typename = void>
    constexpr bool has_size = false;
    template <typename T>
    constexpr bool has_size<T, std::void_t<decltype(std::declval<T>().size())>> = true;

    template <typename T, typename = void>
    static constexpr bool has_empty = false;
    template <typename T>
    static constexpr bool has_empty<
        T, std::void_t<decltype(std::declval<T>().empty())>
    > = true;

    /* SFINAE trait that detects whether std::to_string is invocable with a particular
    type. */
    template <typename T, typename = void>
    constexpr bool has_to_string = false;
    template <typename T>
    constexpr bool has_to_string<
        T,
        std::void_t<decltype(std::to_string(std::declval<T>()))>
    > = true;

    /* SFINAE trait that detects whether the given type overloads the stream insertion
    operator (<<) for string representation. */
    template <typename T, typename = void>
    constexpr bool has_stream_insertion = false;
    template <typename T>
    constexpr bool has_stream_insertion<
        T,
        std::void_t<decltype(std::declval<std::ostream&>() << std::declval<T>())>
    > = true;

}  // namespace impl


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
Dict builtins();
using pybind11::delattr;
List dir();
List dir(const pybind11::handle&);
using pybind11::eval;
using pybind11::eval_file;
using pybind11::exec;
using pybind11::getattr;
using pybind11::globals;
using pybind11::hasattr;
using pybind11::isinstance;
Dict locals();
using pybind11::len_hint;
using pybind11::print;
using pybind11::setattr;
Dict vars();
Dict vars(const pybind11::handle&);


/* Equivalent to Python `aiter(obj)`. */
inline Object aiter(const pybind11::handle& obj) {
    PyObject* result = PyObject_CallOneArg(
        PyDict_GetItemString(PyEval_GetBuiltins(), "aiter"),
        obj.ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `anext(obj)`. */
inline Object anext(const pybind11::handle& obj) {
    PyObject* result = PyObject_CallOneArg(
        PyDict_GetItemString(PyEval_GetBuiltins(), "anext"),
        obj.ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `anext(obj, default)`. */
template <typename T>
inline Object anext(const pybind11::handle& obj, T&& default_value) {
    PyObject* result = PyObject_CallFunctionObjArgs(
        PyDict_GetItemString(PyEval_GetBuiltins(), "anext"),
        obj.ptr(),
        detail::object_or_cast(std::forward<T>(default_value)).ptr(),
        nullptr
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `import module` */
inline Module import(const char* module) {
    return std::move(pybind11::module_::import(module));
}


/* Equivalent to Python `issubclass(derived, base)`. */
template <typename T>
inline bool issubclass(const pybind11::type& derived, T&& base) {
    int result = PyObject_IsSubclass(
        derived.ptr(),
        detail::object_or_cast(std::forward<T>(base)).ptr()
    );
    if (result == -1) {
        throw error_already_set();
    }
    return result;
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
        return detail::object_or_cast(std::forward<T>(default_value));
    }
    return reinterpret_steal<Object>(result);
}


// interoperable with C++
template <typename T>
inline auto abs(T&&);
inline Str ascii(const pybind11::handle&);  // TODO: accept py::Str and return std::string.
inline Str bin(const pybind11::handle&);
template <typename... Args, typename Func>
inline constexpr auto callable(const Func&);
inline Str chr(const pybind11::handle&);
// template <typename L, typename R, typename Mode>
// inline auto div(const L&, const R&, const Mode&);
// template <typename L, typename R, typename Mode>
// inline auto divmod(const L&, const R&, const Mode&);
inline std::string format(const Str&, const Str&);  // TODO: delegate to py::Str?
inline Str hex(const pybind11::handle&);
// template <typename L, typename R, typename Mode>
// inline auto mod(const L&, const R&, const Mode&);
inline Str oct(const pybind11::handle&);  // TODO: make compatible with C++?
inline Int ord(const pybind11::handle&);  // TODO: make compatible with C++?
// template <typename L, typename R>
// inline auto pow(const L&, const R&);
// template <typename L, typename R, typename E>
// inline auto pow(const L&, const R&, const E&);
// template <typename T, typename Mode>
// inline auto round(const T&, int, const Mode&);
inline List sorted(const pybind11::handle&);  // TODO: accept C++ containers and return std::vector?


/* Equivalent to Python `all(obj)`, except that it also works on iterable C++
containers. */
template <typename T>
inline bool all(T&& obj) {
    return std::all_of(obj.begin(), obj.end(), [](auto&& item) {
        return static_cast<bool>(item);
    });
}


/* Equivalent to Python `any(obj)`, except that it also works on iterable C++
containers. */
template <typename T>
inline bool any(T&& obj) {
    return std::any_of(obj.begin(), obj.end(), [](auto&& item) {
        return static_cast<bool>(item);
    });
}


/* Equivalent to Python `hash(obj)`, but also accepts C++ types and converts hash not
implemented errors into compile-time errors. */
template <typename T>
inline size_t hash(T&& obj) {
    static_assert(
        impl::has_std_hash<std::decay_t<T>>,
        "hash() is not supported for this type.  Did you forget to overload std::hash?"
    );
    return std::hash<std::decay_t<T>>{}(std::forward<T>(obj));
}


/* Equivalent to Python `id(obj)`, but also works with C++ values.  Returns an object's
actual memory address casted to an integer. */
template <typename T>
size_t id(T&& obj) {
    using U = std::decay_t<T>;

    if constexpr (std::is_pointer_v<U>) {
        return reinterpret_cast<size_t>(obj);

    } else if constexpr (detail::is_pyobject<U>::value) {
        return reinterpret_cast<size_t>(obj.ptr());

    } else {
        return reinterpret_cast<size_t>(&obj);
    }
}


/* Equivalent to Python `iter(obj)` except that it can also accept C++ containers and
generate Python iterators over them.  Note that C++ types as rvalues are not allowed,
and will trigger a compiler error. */
template <typename T>
inline Iterator iter(T&& obj) {
    if constexpr (impl::use_pybind11_iter<T>) {
        return pybind11::iter(std::forward<T>(obj));
    } else {
        static_assert(
            !std::is_rvalue_reference_v<decltype(obj)>,
            "passing an rvalue reference to py::iter() is unsafe"
        );
        return pybind11::make_iterator(obj.begin(), obj.end());
    }
}


/* Equivalent to Python `len(obj)`, but also accepts C++ types implementing a .size()
method.  Returns nullopt if the size could not be determined. */
template <typename T>
inline std::optional<size_t> len(T&& obj) {
    if constexpr (detail::is_pyobject<T>::value) {
        try {
            return pybind11::len(std::forward<T>(obj));
        } catch (...) {
            return std::nullopt;
        }
    } else if constexpr (impl::has_size<std::decay_t<T>>) {
        return obj.size();
    } else {
        return std::nullopt;
    }
}


/* Equivalent to Python `max(obj)`, but also works on iterable C++ containers. */
template <typename T>
inline auto max(T&& obj) {
    return *std::max_element(obj.begin(), obj.end());
}


/* Equivalent to Python `min(obj)`, but also works on iterable C++ containers. */
template <typename T>
inline auto min(T&& obj) {
    return *std::min_element(obj.begin(), obj.end());
}


/* Equivalent to Python `repr(obj)`, but returns a std::string and attempts to
represent C++ types using std::to_string or the stream insertion operator (<<).  If all
else fails, falls back to typeid(obj).name(). */
template <typename T>
inline std::string repr(T&& obj) {
    using U = std::decay_t<T>;

    if constexpr (impl::has_to_string<U>) {
        return std::to_string(std::forward<T>(obj));

    } else if constexpr (impl::has_stream_insertion<U>) {
        std::ostringstream stream;
        stream << std::forward<T>(obj);
        return stream.str();

    } else {
        try {
            return pybind11::repr(std::forward<T>(obj)).template cast<std::string>();
        } catch (...) {
            return typeid(obj).name();
        }
    }
}


/* Equivalent to Python `reversed(obj)` except that it can also accept C++ containers
and generate Python iterators over them.  Note that C++ types as rvalues are not
allowed, and will trigger a compiler error. */
template <typename T>
inline Iterator reversed(T&& obj) {
    if constexpr (detail::is_pyobject<T>::value) {
        return obj.attr("__reversed__")();
    } else {
        static_assert(
            !std::is_rvalue_reference_v<decltype(obj)>,
            "passing an rvalue reference to py::reversed() is unsafe"
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


/* Equivalent to Python `sum(obj)`, but also works on C++ containers. */
template <typename T>
inline auto sum(T&& obj) {
    return std::accumulate(obj.begin(), obj.end(), T{});
}


}  // namespace py
}  // namespace bertrand


/* NOTE: for compatibility with C++, py::hash() always delegates to std::hash, which
 * can be specialized for any type.  This allows pybind11 types to be used as keys in
 * std::unordered_map/std::unordered_set just like any other C++ type, and means that
 * we can catch non-hashable types at compile time rather than runtime.  The downside
 * is that we need to specialize std::hash for every pybind11 type that is hashable at
 * the python level, which is a bit cumbersome.  To facilitate this, the
 * BERTRAND_STD_HASH() macro should be invoked for every pybind11 type that can be
 * hashed.  This just delegates to pybind11::hash, which invokes Python's __hash__
 * special method like normal.
 *
 * NOTE: BERTRAND_STD_HASH() should always be invoked in the global namespace, and it
 * cannot be used if the type accepts template parameters.
 */


namespace std {

    template <typename... Args>
    struct hash<bertrand::py::Class<Args...>> {
        size_t operator()(const bertrand::py::Class<Args...>& obj) const {
            return pybind11::hash(obj);
        }
    };

}


#define BERTRAND_STD_HASH(cls)                                                          \
namespace std {                                                                         \
    template <>                                                                         \
    struct hash<cls> {                                                                  \
        size_t operator()(const cls& obj) const {                                       \
            return pybind11::hash(obj);                                                 \
        }                                                                               \
    };                                                                                  \
}                                                                                       \


BERTRAND_STD_HASH(bertrand::py::Module)
BERTRAND_STD_HASH(bertrand::py::Handle)
BERTRAND_STD_HASH(bertrand::py::Object)
BERTRAND_STD_HASH(bertrand::py::NoneType)
BERTRAND_STD_HASH(bertrand::py::EllipsisType)
BERTRAND_STD_HASH(bertrand::py::Iterator)
BERTRAND_STD_HASH(bertrand::py::WeakRef)
BERTRAND_STD_HASH(bertrand::py::Capsule)
BERTRAND_STD_HASH(bertrand::py::Buffer)
BERTRAND_STD_HASH(bertrand::py::MemoryView)
BERTRAND_STD_HASH(bertrand::py::Bytes)
BERTRAND_STD_HASH(bertrand::py::Bytearray)
BERTRAND_STD_HASH(bertrand::py::NotImplementedType)


/* pybind11 doesn't carry over python semantics for equality comparisons, so in order
 * to use raw pybind11 types (not our custom wrappers) in std::unordered_map and
 * std::unordered_set, we need to specialize std::equal_to for each type.  This should
 * not be necessary for any derived types, as they should use Python semantics by
 * default.
 */


namespace std {

    template <typename... Args>
    struct equal_to<bertrand::py::Class<Args...>> {
        bool operator()(
            const bertrand::py::Class<Args...>& a,
            const bertrand::py::Class<Args...>& b
        ) const {
            return a.equal(b);
        }
    };

}


#define BERTRAND_STD_EQUAL_TO(cls)                                                      \
namespace std {                                                                         \
    template <>                                                                         \
    struct equal_to<cls> {                                                              \
        bool operator()(const cls& a, const cls& b) const {                             \
            return a.equal(b);                                                          \
        }                                                                               \
    };                                                                                  \
}                                                                                       \


BERTRAND_STD_EQUAL_TO(bertrand::py::Module)
BERTRAND_STD_EQUAL_TO(bertrand::py::Handle)
BERTRAND_STD_EQUAL_TO(bertrand::py::Object)
BERTRAND_STD_EQUAL_TO(bertrand::py::NoneType)
BERTRAND_STD_EQUAL_TO(bertrand::py::EllipsisType)
BERTRAND_STD_EQUAL_TO(bertrand::py::Iterator)
BERTRAND_STD_EQUAL_TO(bertrand::py::WeakRef)
BERTRAND_STD_EQUAL_TO(bertrand::py::Capsule)
BERTRAND_STD_EQUAL_TO(bertrand::py::Buffer)
BERTRAND_STD_EQUAL_TO(bertrand::py::MemoryView)
BERTRAND_STD_EQUAL_TO(bertrand::py::Bytes)
BERTRAND_STD_EQUAL_TO(bertrand::py::Bytearray)


// TODO: implement type casters for range, MappingProxy, KeysView, ValuesView,
// ItemsView, Method, ClassMethod, StaticMethod, Property


#endif // BERTRAND_PYTHON_COMMON_H
