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
using pybind11::cast;
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
using Object = pybind11::object;
using NoneType = pybind11::none;
using EllipsisType = pybind11::ellipsis;
using Iterator = pybind11::iterator;
using WeakRef = pybind11::weakref;
using Capsule = pybind11::capsule;
using Buffer = pybind11::buffer;
using MemoryView = pybind11::memoryview;
using Bytes = pybind11::bytes;
using Bytearray = pybind11::bytearray;
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
class Regex;

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
            PyObject* result = py::cast(std::forward<T>(value)).release().ptr();
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }
    }

    /* Mixin holding operator overloads for numeric types.  For some reason, pybind11
    enables these operators, but does not allow implicit conversion from C++ operands,
    so we override them here. */
    template <typename Derived>
    struct NumericOps {

        #define BINARY_OPERATOR(op, endpoint)                                           \
            template <typename T>                                                       \
            inline Object op(T&& other) const {                                         \
                PyObject* result = PyNumber_##endpoint(                                 \
                    static_cast<const Derived*>(this)->ptr(),                           \
                    detail::object_or_cast(std::forward<T>(other)).ptr()                \
                );                                                                      \
                if (result == nullptr) {                                                \
                    throw error_already_set();                                          \
                }                                                                       \
                return reinterpret_steal<Object>(result);                               \
            }                                                                           \

        #define REVERSE_OPERATOR(op, endpoint)                                          \
            template <                                                                  \
                typename T,                                                             \
                std::enable_if_t<                                                       \
                    !std::is_base_of_v<Derived, std::decay_t<T>>,                       \
                    int                                                                 \
                > = 0                                                                   \
            >                                                                           \
            inline friend Object op(T&& other, const Derived& self) {                   \
                PyObject* result = PyNumber_##endpoint(                                 \
                    detail::object_or_cast(std::forward<T>(other)).ptr(),               \
                    self.ptr()                                                          \
                );                                                                      \
                if (result == nullptr) {                                                \
                    throw error_already_set();                                          \
                }                                                                       \
                return reinterpret_steal<Object>(result);                               \
            }                                                                           \

        #define INPLACE_OPERATOR(op, endpoint)                                          \
            template <typename T>                                                       \
            inline Object& op(T&& other) {                                              \
                Derived* self = static_cast<const Derived*>(this);                      \
                PyObject* result = PyNumber_InPlace##endpoint(                          \
                    self->ptr(),                                                        \
                    detail::object_or_cast(std::forward<T>(other)).ptr()                \
                );                                                                      \
                if (result == nullptr) {                                                \
                    throw error_already_set();                                          \
                }                                                                       \
                if (result == self->ptr()) {                                            \
                    Py_DECREF(result);                                                  \
                } else {                                                                \
                    *this = reinterpret_steal<Object>(result);                          \
                }                                                                       \
                return *this;                                                           \
            }                                                                           \

        BINARY_OPERATOR(operator+, Add);
        BINARY_OPERATOR(operator-, Subtract);
        BINARY_OPERATOR(operator*, Multiply);
        BINARY_OPERATOR(operator/, TrueDivide);
        BINARY_OPERATOR(operator%, Remainder);
        BINARY_OPERATOR(operator<<, Lshift);
        BINARY_OPERATOR(operator>>, Rshift);
        BINARY_OPERATOR(operator&, And);
        BINARY_OPERATOR(operator|, Or);
        BINARY_OPERATOR(operator^, Xor);
        REVERSE_OPERATOR(operator+, Add);
        REVERSE_OPERATOR(operator-, Subtract);
        REVERSE_OPERATOR(operator*, Multiply);
        REVERSE_OPERATOR(operator/, TrueDivide);
        REVERSE_OPERATOR(operator%, Remainder);
        REVERSE_OPERATOR(operator<<, Lshift);
        REVERSE_OPERATOR(operator>>, Rshift);
        REVERSE_OPERATOR(operator&, And);
        REVERSE_OPERATOR(operator|, Or);
        REVERSE_OPERATOR(operator^, Xor);
        INPLACE_OPERATOR(operator+=, Add);
        INPLACE_OPERATOR(operator-=, Subtract);
        INPLACE_OPERATOR(operator*=, Multiply);
        INPLACE_OPERATOR(operator/=, TrueDivide);
        INPLACE_OPERATOR(operator%=, Remainder);
        INPLACE_OPERATOR(operator<<=, Lshift);
        INPLACE_OPERATOR(operator>>=, Rshift);
        INPLACE_OPERATOR(operator&=, And);
        INPLACE_OPERATOR(operator|=, Or);
        INPLACE_OPERATOR(operator^=, Xor);

        #undef BINARY_OPERATOR
        #undef REVERSE_OPERATOR
        #undef INPLACE_OPERATOR
    };

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

        /* Slice operator.  Accepts an initializer list that expands to a Python slice
        object with generalized syntax. */
        using SliceIndex = std::variant<long long, pybind11::none>;
        inline auto operator[](const std::initializer_list<SliceIndex>& slice) const {
            if (slice.size() > 3) {
                throw ValueError(
                    "slices must be of the form {[start[, stop[, step]]]}"
                );
            }
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
            return static_cast<const Derived*>(this)->operator[](
                pybind11::slice(params[0], params[1], params[2])
            );
        }

    };

    #define COMPARISON_OPERATOR(op, endpoint)                                           \
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

    #define REVERSE_COMPARISON(op, endpoint)                                            \
        template <                                                                      \
            typename T,                                                                 \
            std::enable_if_t<                                                           \
                !std::is_base_of_v<Derived, std::decay_t<T>>,                           \
                int                                                                     \
            > = 0                                                                       \
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

    /* Mixin holding equality comparisons that always fall back to PyObject_RichCompare.
    This resolves the asymmetry between pybind11's `==`/`!=` operators and the Python
    equivalents. */
    template <typename Derived>
    struct EqualCompare {
        COMPARISON_OPERATOR(operator==, Py_EQ);
        COMPARISON_OPERATOR(operator!=, Py_NE);
        REVERSE_COMPARISON(operator==, Py_EQ);
        REVERSE_COMPARISON(operator!=, Py_NE);
    };

    /* Mixin holding a full suite of comparison operators that always fall back to
    PyObject_RichCompare. */
    template <typename Derived>
    struct FullCompare {
        COMPARISON_OPERATOR(operator<, Py_LT);
        COMPARISON_OPERATOR(operator<=, Py_LE);
        COMPARISON_OPERATOR(operator==, Py_EQ);
        COMPARISON_OPERATOR(operator!=, Py_NE);
        COMPARISON_OPERATOR(operator>, Py_GT);
        COMPARISON_OPERATOR(operator>=, Py_GE);
        REVERSE_COMPARISON(operator<, Py_GT);
        REVERSE_COMPARISON(operator<=, Py_GE);
        REVERSE_COMPARISON(operator==, Py_EQ);
        REVERSE_COMPARISON(operator!=, Py_NE);
        REVERSE_COMPARISON(operator>, Py_LT);
        REVERSE_COMPARISON(operator>=, Py_LE);
    };

    #undef COMPARISON_OPERATOR
    #undef REVERSE_COMPARISON

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
    #define CONSTRUCTORS(cls, check, convert)                                           \
        using Base::operator=;                                                          \
        PYBIND11_OBJECT_CVT(cls, Base, check, convert);                                 \
        template <typename T>                                                           \
        cls& operator=(const std::initializer_list<T>& init) {                          \
            Base::operator=(cls(init));                                                 \
            return *this;                                                               \
        }                                                                               \

}  // namespace impl


class NotImplementedType :
    public pybind11::object,
    public impl::EqualCompare<NotImplementedType>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<NotImplementedType>;

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
    CONSTRUCTORS(NotImplementedType, check_not_implemented, convert_not_implemented);
    NotImplementedType() : Base(Py_NotImplemented, borrowed_t{}) {}

    using Compare::operator==;
    using Compare::operator!=;
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


// TODO: super()


// interoperable with C++
template <typename T>
auto abs(T&&);
Str ascii(const pybind11::handle&);  // TODO: accept py::Str and return std::string.
Str bin(const pybind11::handle&);
template <typename... Args, typename Func>
constexpr auto callable(const Func&);
template <typename T, typename U>
auto cdiv(T&&, U&&);
template <typename T, typename U>
auto cdivmod(T&&, U&&);
Str chr(const pybind11::handle&);
template <typename T, typename U>
auto cmod(T&&, U&&);
template <typename T, typename U>
auto divmod(T&&, U&&);
template <typename T, typename U>
auto floordiv(T&&, U&&);
std::string format(const Str&, const Str&);  // TODO: delegate to py::Str?
Str hex(const pybind11::handle&);
template <typename T, typename U>
auto inplace_cdiv(T&&, U&&);
template <typename T, typename U>
auto inplace_cmod(T&&, U&&);
template <typename T, typename U>
auto inplace_floordiv(T&&, U&&);
template <typename T, typename U>
auto inplace_matmul(T&&, U&&);
template <typename T, typename U>
auto inplace_mod(T&&, U&&);
template <typename T, typename U>
auto inplace_pow(T&&, U&&);
template <typename T, typename U, typename V>
auto inplace_pow(T&&, U&&, V&&);
template <typename T, typename U>
auto inplace_truediv(T&&, U&&);
template <typename T, typename U>
auto matmul(T&&, U&&);
template <typename T, typename U>
auto mod(T&&, U&&);
Str oct(const pybind11::handle&);  // TODO: make compatible with C++?
Int ord(const pybind11::handle&);  // TODO: make compatible with C++?
template <typename T, typename U>
auto pow(T&&, U&&);
template <typename T, typename U, typename V>
auto pow(T&&, U&&, V&&);
template <typename T>
auto round(T&&);  // TODO: implement with optional ndigits/algorithm tags
List sorted(const pybind11::handle&);  // TODO: accept C++ containers and return std::vector?
template <typename T, typename U>
auto truediv(T&&, U&&);


/* Equivalent to Python `all(obj)`, except that it also works on iterable C++
containers. */
template <typename T>
inline bool all(T&& obj) {
    return std::all_of(
        obj.begin(),
        obj.end(),
        [](auto&& item) {
            if constexpr (detail::is_pyobject<std::decay_t<decltype(item)>>::value) {
                return item.template cast<bool>();
            } else {
                return static_cast<bool>(item);
            }
        }
    );
}


/* Equivalent to Python `any(obj)`, except that it also works on iterable C++
containers. */
template <typename T>
inline bool any(T&& obj) {
    return std::any_of(
        obj.begin(),
        obj.end(),
        [](auto&& item) {
            if constexpr (detail::is_pyobject<std::decay_t<decltype(item)>>::value) {
                return item.template cast<bool>();
            } else {
                return static_cast<bool>(item);
            }
        }
    );
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
else fails, falls back to typeid().name(). */
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
            return typeid(U).name();
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


// #undef CONSTRUCTORS  // TODO: uncommenting this makes it impossible to use the
// CONSTRUCTORS macro in other classes.  Probably 

#endif // BERTRAND_PYTHON_COMMON_H