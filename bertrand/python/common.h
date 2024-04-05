#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_H

#include <algorithm>
#include <cstddef>
#include <chrono>
#include <fstream>
#include <initializer_list>
#include <iterator>
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
// #include <pybind11/stl_bind.h>  // complicates error messages for operator overloads

#include "bertrand/static_str.h"


// TODO: refactor Object to contain a PyObject* directly and decouple from pybind11.
// This means I don't need to worry about inheriting unwanted behaviors from pybind11,
// and get the best possible error messages from the compiler.  It also allows me to
// bypass the pybind11 destructor, which means all objects can be statically allocated
// by default.  These are huge wins for developer experience.





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
using pybind11::make_iterator;  // TODO: roll into Iterator() constructor
using pybind11::make_key_iterator;  // offer as static Iterator::keys() method
using pybind11::make_value_iterator;  // same as above for Iterator::values()
using pybind11::initialize_interpreter;
using pybind11::scoped_interpreter;
// PYBIND11_MODULE                      <- macros don't respect namespaces
// PYBIND11_EMBEDDED_MODULE
// PYBIND11_OVERRIDE
// PYBIND11_OVERRIDE_PURE
// PYBIND11_OVERRIDE_NAME
// PYBIND11_OVERRIDE_PURE_NAME
using pybind11::get_override;
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


// wrapper types
template <typename... Args>
using Class = pybind11::class_<Args...>;
using Handle = pybind11::handle;
using WeakRef = pybind11::weakref;
using Capsule = pybind11::capsule;
using Buffer = pybind11::buffer;  // TODO: delete this and force users to use memoryview instead
using MemoryView = pybind11::memoryview;  // TODO: place in buffer.h along with memoryview
class Object;
class NoneType;
class NotImplementedType;
class EllipsisType;
class Slice;
class Module;
class Bool;
class Int;
class Float;
class Complex;
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
class Bytes;
class ByteArray;
class Type;
class Super;
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

// TODO: Regex should be placed in bertrand:: namespace.  It doesn't actually wrap a
// python object, so it shouldn't be in the py:: namespace

class Regex;  // TODO: incorporate more fully (write pybind11 bindings so that it can be passed into Python scripts)

// Regex bindings would allow users to directly use PCRE2 regular expressions in Python.

// -> Regex class itself should be in bertrand:: namespace, but 2-way Python bindings
// are listed under py::Regex.


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
using CastError = pybind11::cast_error;
using ReferenceCastError = pybind11::reference_cast_error;


#define PYTHON_EXCEPTION(base, cls, exc)                                                \
    class PYBIND11_EXPORT_EXCEPTION cls : public base {                                 \
    public:                                                                             \
        using base::base;                                                               \
        cls() : cls("") {}                                                              \
        explicit cls(Handle obj) : base([&obj] {                                        \
            PyObject* string = PyObject_Str(obj.ptr());                                 \
            if (string == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            Py_ssize_t size;                                                            \
            const char* data = PyUnicode_AsUTF8AndSize(string, &size);                  \
            if (data == nullptr) {                                                      \
                Py_DECREF(string);                                                      \
                throw error_already_set();                                              \
            }                                                                           \
            std::string result(data, size);                                             \
            Py_DECREF(string);                                                          \
            return result;                                                              \
        }()) {}                                                                         \
        void set_error() const override { PyErr_SetString(exc, what()); }               \
    };                                                                                  \


using Exception = pybind11::builtin_exception;
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


//////////////////////////////
////    BUILT-IN TYPES    ////
//////////////////////////////


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


namespace impl {

    /* Tag class to identify object proxies during SFINAE checks. */
    struct ProxyTag {};

    /* Helper function triggers implicit conversion operators and/or implicit
    constructors, but not explicit ones.  In contrast, static_cast<>() will trigger
    explicit constructors on the target type, which can give unexpected results and
    violate bertrand's strict type safety. */
    template <typename U>
    inline static decltype(auto) implicit_cast(U&& value) {
        return std::forward<U>(value);
    }

    namespace categories {

        struct Base {
            static constexpr bool boollike = false;
            static constexpr bool intlike = false;
            static constexpr bool floatlike = false;
            static constexpr bool complexlike = false;
            static constexpr bool strlike = false;
            static constexpr bool timedeltalike = false;
            static constexpr bool timezonelike = false;
            static constexpr bool datelike = false;
            static constexpr bool timelike = false;
            static constexpr bool datetimelike = false;
            static constexpr bool tuplelike = false;
            static constexpr bool listlike = false;
            static constexpr bool setlike = false;
            static constexpr bool dictlike = false;
        };

        template <typename T>
        class Traits : public Base {};

        template <typename T>
        struct Traits<std::complex<T>> : public Base {
            static constexpr bool complexlike = true;
        };

        template <typename... Args>
        struct Traits<std::chrono::duration<Args...>> : public Base {
            static constexpr bool timedeltalike = true;
        };

        template <typename... Args>
        struct Traits<std::chrono::time_point<Args...>> : public Base {
            static constexpr bool timelike = true;
        };

        // TODO: std::time_t?

        template <typename... Args>
        struct Traits<std::pair<Args...>> : public Base {
            static constexpr bool tuplelike = true;
        };

        template <typename... Args>
        struct Traits<std::tuple<Args...>> : public Base {
            static constexpr bool tuplelike = true;
        };

        template <typename T, size_t N>
        struct Traits<std::array<T, N>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::vector<Args...>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::deque<Args...>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::list<Args...>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::forward_list<Args...>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::set<Args...>> : public Base {
            static constexpr bool setlike = true;
        };

        template <typename... Args>
        struct Traits<std::unordered_set<Args...>> : public Base {
            static constexpr bool setlike = true;
        };

        template <typename... Args>
        struct Traits<std::map<Args...>> : public Base {
            static constexpr bool dictlike = true;
        };

        template <typename... Args>
        struct Traits<std::unordered_map<Args...>> : public Base {
            static constexpr bool dictlike = true;
        };

    }

    template <typename T>
    concept python_like = detail::is_pyobject<std::remove_cvref_t<T>>::value;

    template <typename T>
    concept proxy_like = std::is_base_of_v<ProxyTag, std::remove_cvref_t<T>>;

    template <typename T>
    concept accessor_like = requires(const T& t) {
        { []<typename Policy>(const detail::accessor<Policy>){}(t) } -> std::same_as<void>;
    };

    template <typename T>
    concept sequence_like = requires(const T& t) {
        { std::begin(t) } -> std::input_or_output_iterator;
        { std::end(t) } -> std::input_or_output_iterator;
        { t.size() } -> std::convertible_to<size_t>;
        { t[0] };
    };

    template <typename T>
    concept iterator_like = requires(T it, T end) {
        { *it } -> std::convertible_to<typename T::value_type>;
        { ++it } -> std::same_as<T&>;
        { it++ } -> std::same_as<T>;
        { it == end } -> std::convertible_to<bool>;
        { it != end } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept none_like = (
        std::is_same_v<std::nullptr_t, std::remove_cvref_t<T>> ||
        std::is_base_of_v<py::NoneType, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::none, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept slice_like = (
        std::is_base_of_v<Slice, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::slice, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept module_like = (
        std::is_base_of_v<py::Module, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::module, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept bool_like = (
        std::is_same_v<bool, std::remove_cvref_t<T>> ||
        std::is_base_of_v<py::Bool, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::bool_, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept int_like = (
        std::is_base_of_v<Int, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::int_, std::remove_cvref_t<T>> ||
        (
            std::is_integral_v<std::remove_cvref_t<T>> &&
            !std::is_same_v<bool, std::remove_cvref_t<T>>
        )
    );

    template <typename T>
    concept float_like = (
        std::is_floating_point_v<std::remove_cvref_t<T>> ||
        std::is_base_of_v<Float, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::float_, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept complex_like = requires(const T& t) {
        { t.real() } -> std::convertible_to<double>;
        { t.imag() } -> std::convertible_to<double>;
    };

    template <typename T>
    concept string_literal = requires(const T& t) {
        { []<size_t N>(const char(&)[N]){}(t) } -> std::same_as<void>;
    };

    template <typename T>
    concept str_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::is_same_v<const char*, std::remove_cvref_t<T>> ||
        std::is_same_v<std::string, std::remove_cvref_t<T>> ||
        std::is_same_v<std::string_view, std::remove_cvref_t<T>> ||
        std::is_base_of_v<Str, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::str, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept bytes_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::is_same_v<void*, std::remove_cvref_t<T>> ||
        std::is_base_of_v<Bytes, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::bytes, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept bytearray_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::is_same_v<std::remove_cvref_t<T>, void*> ||
        std::is_base_of_v<ByteArray, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::bytearray, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept timedelta_like = (
        categories::Traits<std::remove_cvref_t<T>>::timedeltalike ||
        std::is_base_of_v<Timedelta, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept timezone_like = (
        categories::Traits<std::remove_cvref_t<T>>::timezonelike ||
        std::is_base_of_v<Timezone, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept date_like = (
        categories::Traits<std::remove_cvref_t<T>>::datelike ||
        std::is_base_of_v<Date, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept time_like = (
        categories::Traits<std::remove_cvref_t<T>>::timelike ||
        std::is_base_of_v<Time, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept datetime_like = (
        categories::Traits<std::remove_cvref_t<T>>::datetimelike ||
        std::is_base_of_v<Datetime, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept range_like = (
        std::is_base_of_v<Range, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept tuple_like = (
        categories::Traits<std::remove_cvref_t<T>>::tuplelike ||
        std::is_base_of_v<Tuple, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::tuple, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept list_like = (
        categories::Traits<std::remove_cvref_t<T>>::listlike ||
        std::is_base_of_v<List, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::list, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept set_like = (
        categories::Traits<std::remove_cvref_t<T>>::setlike ||
        std::is_base_of_v<Set, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::set, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept frozenset_like = (
        categories::Traits<std::remove_cvref_t<T>>::setlike ||
        std::is_base_of_v<FrozenSet, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::frozenset, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept anyset_like = set_like<T> || frozenset_like<T>;

    template <typename T>
    concept dict_like = (
        categories::Traits<std::remove_cvref_t<T>>::dictlike ||
        std::is_base_of_v<Dict, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::dict, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept mappingproxy_like = (
        categories::Traits<std::remove_cvref_t<T>>::dictlike ||
        std::is_base_of_v<MappingProxy, std::remove_cvref_t<T>>
    );

    template <typename T>
    concept anydict_like = dict_like<T> || mappingproxy_like<T>;

    template <typename T>
    concept type_like = (
        std::is_base_of_v<Type, std::remove_cvref_t<T>> ||
        std::is_base_of_v<pybind11::type, std::remove_cvref_t<T>>
    );

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(const From& from) {
        static_cast<To>(from);
    };

    template <typename From, typename To>
    concept has_conversion_operator = requires(const From& from) {
        from.operator To();
    };

    template <typename T>
    concept has_size = requires(const T& t) {
        { t.size() } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_empty = requires(const T& t) {
        { t.empty() } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept has_reserve = requires(T& t, size_t n) {
        { t.reserve(n) } -> std::same_as<void>;
    };

    // NOTE: decay is necessary to treat `const char[N]` like `const char*`
    template <typename T>
    concept is_hashable = requires(T&& t) {
        { std::hash<std::decay_t<T>>{}(std::forward<T>(t)) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept is_iterable = requires(const T& t) {
        { std::begin(t) } -> std::input_or_output_iterator;
        { std::end(t) } -> std::input_or_output_iterator;
    };

    template <typename T>
    concept reverse_iterable = requires(const T& t) {
        { std::rbegin(t) } -> std::input_or_output_iterator;
        { std::rend(t) } -> std::input_or_output_iterator;
    };

    template <typename T>
    concept has_to_string = requires(const T& t) {
        { std::to_string(t) } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_stream_insertion = requires(std::ostream& os, const T& t) {
        { os << t } -> std::convertible_to<std::ostream&>;
    };

    template <typename T>
    concept pybind11_iterable = requires(const T& t) {
        { pybind11::iter(t) } -> std::convertible_to<pybind11::iterator>;
    };

    /* SFINAE condition is used to recognize callable C++ types without regard to their
    argument signatures. */
    template <typename T>
    concept is_callable_any = 
        std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> ||
        std::is_member_function_pointer_v<std::decay_t<T>> ||
        requires { &std::decay_t<T>::operator(); };

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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* By default, all generic operators are disabled for subclasses of py::Object.
    * This means we have to specifically enable them for each type we want to support,
    * which promotes explicitness and type safety by design.  The following structs
    * allow users to easily assign static types to any of these operators, which will
    * automatically be preferred when operands of those types are detected at compile
    * time.  By using template specialization, we allow users to do this from outside
    * the class itself, allowing the type system to grow as needed to cover any
    * environment.  Here's an example:
    *
    *      template <>
    *      struct py::impl::__add__<py::Bool, int> : py::impl::Returns<py::Int> {};
    *
    * It's that simple.  Now, whenever we call `py::Bool + int`, it will successfully
    * compile and interpret the result as a strict `py::Int` type, eliminating runtime
    * overhead and granting static type safety.  It is also possible to apply C++20
    * template constraints to these types using an optional second template parameter,
    * which allows users to enable or disable whole categories of types at once.
    * Here's another example:
    *
    *      template <py::impl::int_like T>
    *      struct py::impl::__add__<py::Bool, T> : py::impl::Returns<py::Int> {};
    *
    * As long as the constraint does not conflict with any other existing template
    * overloads, this will compile and work as expected.  Note that specific overloads
    * will always take precedence over generic ones, and any ambiguities between
    * templates will result in compile errors when used.
    *
    * There are several benefits to this architecture.  First, it significantly reduces
    * the number of runtime type checks that must be performed to ensure strict type
    * safety, and promotes those checks to compile time instead, which is always
    * preferable.  Second, it enables syntactically correct implicit conversions
    * between C++ and Python types, which is a huge win for usability.  Third, it
    * allows us to follow traditional Python naming conventions for its operator
    * special methods, which makes it easier to remember and use them in practice.
    * Finally, it disambiguates the internal behavior of these operators, reducing the
    * number of gotchas and making the code more idiomatic and easier to reason about.
    */

    template <typename T, typename... Args>
    struct __call__ { static constexpr bool enable = false; };
    template <typename T>
    struct __len__ { static constexpr bool enable = false; };
    template <typename T>
    struct __iter__ { static constexpr bool enable = false; };
    template <typename T>
    struct __reversed__ { static constexpr bool enable = false; };
    template <typename T, typename Key>
    struct __contains__ { static constexpr bool enable = false; };
    template <typename T, typename Key>
    struct __getitem__ { static constexpr bool enable = false; };
    template <typename T, typename Key, typename Value>
    struct __setitem__ { static constexpr bool enable = false; };
    template <typename T, typename Key>
    struct __delitem__ { static constexpr bool enable = false; };
    template <typename T, StaticStr name>
    struct __getattr__ { static constexpr bool enable = false; };
    template <typename T, StaticStr name>
    struct __setattr__ { static constexpr bool enable = false; };
    template <typename T, StaticStr name>
    struct __delattr__ { static constexpr bool enable = false; };
    template <typename T>
    struct __pos__ { static constexpr bool enable = false; };
    template <typename T>
    struct __neg__ { static constexpr bool enable = false; };
    template <typename T>
    struct __abs__ { static constexpr bool enable = false; };
    template <typename T>
    struct __invert__ { static constexpr bool enable = false; };
    template <typename T>
    struct __increment__ { static constexpr bool enable = false; };
    template <typename T>
    struct __decrement__ { static constexpr bool enable = false; };
    template <typename T>
    struct __hash__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __lt__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __le__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __eq__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __ne__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __ge__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __gt__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __add__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __sub__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __mul__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __truediv__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __mod__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __lshift__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __rshift__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __and__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __xor__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __or__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __iadd__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __isub__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __imul__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __itruediv__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __imod__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __ilshift__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __irshift__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __iand__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __ixor__ { static constexpr bool enable = false; };
    template <typename L, typename R>
    struct __ior__ { static constexpr bool enable = false; };

    /* Base class for enabled operators.  Encodes the return type as a template
    parameter. */
    template <typename T>
    struct Returns {
        static constexpr bool enable = true;
        using Return = T;
    };

    #define BERTRAND_OBJECT_OPERATORS(cls)                                              \
        template <typename... Args> requires (impl::__call__<cls, Args...>::enable)     \
        inline auto operator()(Args&&... args) const {                                  \
            using Return = typename impl::__call__<cls, Args...>::Return;               \
            static_assert(                                                              \
                std::is_same_v<Return, void> || std::is_base_of_v<Return, Object>,      \
                "Call operator must return either void or a py::Object subclass.  "     \
                "Check your specialization of __call__ for the given arguments and "    \
                "ensure that it is derived from py::Object."                            \
            );                                                                          \
            return operator_call<Return>(*this, std::forward<Args>(args)...);           \
        }                                                                               \
                                                                                        \
        template <typename Key>                                                         \
            requires (impl::__getitem__<cls, Key>::enable && !impl::proxy_like<Key>)    \
        inline auto operator[](const Key& key) const {                                  \
            using Return = typename impl::__getitem__<cls, Key>::Return;                \
            return operator_getitem<Return>(*this, key);                                \
        }                                                                               \
                                                                                        \
        template <typename Key> requires (impl::proxy_like<Key>)                        \
        inline auto operator[](const Key& key) const {                                  \
            return (*this)[key.value()];                                                \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__getitem__<T, Slice>::enable)      \
        inline auto operator[](                                                         \
            std::initializer_list<impl::SliceInitializer> slice                         \
        ) const {                                                                       \
            using Return = typename impl::__getitem__<T, Slice>::Return;                \
            return operator_getitem<Return>(*this, slice);                              \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__iter__<T>::enable)                \
        inline auto operator*() const {                                                 \
            using Return = typename impl::__iter__<T>::Return;                          \
            return operator_dereference<Return>(*this);                                 \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__iter__<T>::enable)                \
        inline auto begin() const {                                                     \
            using Return = typename impl::__iter__<T>::Return;                          \
            static_assert(                                                              \
                std::is_base_of_v<Object, Return>,                                      \
                "iterator must dereference to a subclass of Object.  Check your "       \
                "specialization of __iter__ for this types and ensure the Return type " \
                "is a subclass of py::Object."                                          \
            );                                                                          \
            return operator_begin<Return>(*this);                                       \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__iter__<T>::enable)                \
        inline auto end() const {                                                       \
            using Return = typename impl::__iter__<T>::Return;                          \
            static_assert(                                                              \
                std::is_base_of_v<Object, Return>,                                      \
                "iterator must dereference to a subclass of Object.  Check your "       \
                "specialization of __iter__ for this types and ensure the Return type " \
                "is a subclass of py::Object."                                          \
            );                                                                          \
            return operator_end<Return>(*this);                                         \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__reversed__<T>::enable)            \
        inline auto rbegin() const {                                                    \
            using Return = typename impl::__reversed__<T>::Return;                      \
            static_assert(                                                              \
                std::is_base_of_v<Object, Return>,                                      \
                "iterator must dereference to a subclass of Object.  Check your "       \
                "specialization of __reversed__ for this types and ensure the Return "  \
                "type is a subclass of py::Object."                                     \
            );                                                                          \
            return operator_rbegin<Return>(*this);                                      \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__reversed__<T>::enable)            \
        inline auto rend() const {                                                      \
            using Return = typename impl::__reversed__<T>::Return;                      \
            static_assert(                                                              \
                std::is_base_of_v<Object, Return>,                                      \
                "iterator must dereference to a subclass of Object.  Check your "       \
                "specialization of __reversed__ for this types and ensure the Return "  \
                "type is a subclass of py::Object."                                     \
            );                                                                          \
            return operator_rend<Return>(*this);                                        \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__contains__<cls, T>::enable && !impl::proxy_like<T>)       \
        inline bool contains(const T& key) const {                                      \
            using Return = typename impl::__contains__<cls, T>::Return;                 \
            static_assert(                                                              \
                std::is_same_v<Return, bool>,                                           \
                "contains() operator must return a boolean value.  Check your "         \
                "specialization of __contains__ for these types and ensure the Return " \
                "type is set to bool."                                                  \
            );                                                                          \
            return operator_contains<Return>(*this, key);                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::proxy_like<T>)                            \
        inline bool contains(const T& key) const {                                      \
            return this->contains(key.value());                                         \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__len__<T>::enable)                 \
        inline size_t size() const {                                                    \
            using Return = typename impl::__len__<T>::Return;                           \
            static_assert(                                                              \
                std::is_same_v<Return, size_t>,                                         \
                "size() operator must return a size_t for compatibility with C++ "      \
                "containers.  Check your specialization of __len__ for these types "    \
                "and ensure the Return type is set to size_t."                          \
            );                                                                          \
            return operator_len<Return>(*this);                                         \
        }                                                                               \

    template <typename ... Args>
    struct __call__<Object, Args...>                        : Returns<Object> {};
    template <>
    struct __len__<Object>                                  : Returns<size_t> {};
    template <typename T>
    struct __contains__<Object, T>                          : Returns<bool> {};
    template <>
    struct __iter__<Object>                                 : Returns<Object> {};
    template <>
    struct __reversed__<Object>                             : Returns<Object> {};
    template <typename Key>
    struct __getitem__<Object, Key>                         : Returns<Object> {};
    template <typename Key, typename Value>
    struct __setitem__<Object, Key, Value>                  : Returns<void> {};
    template <typename Key>
    struct __delitem__<Object, Key>                         : Returns<void> {};
    template <StaticStr name>
    struct __getattr__<Object, name>                        : Returns<Object> {};
    template <StaticStr name>
    struct __setattr__<Object, name>                        : Returns<void> {};
    template <StaticStr name>
    struct __delattr__<Object, name>                        : Returns<void> {};
    template <>
    struct __pos__<Object>                                  : Returns<Object> {};
    template <>
    struct __neg__<Object>                                  : Returns<Object> {};
    template <>
    struct __abs__<Object>                                  : Returns<Object> {};
    template <>
    struct __invert__<Object>                               : Returns<Object> {};
    template <>
    struct __increment__<Object>                            : Returns<Object> {};
    template <>
    struct __decrement__<Object>                            : Returns<Object> {};
    template <>
    struct __hash__<Object>                                 : Returns<size_t> {};
    template <typename T>
    struct __lt__<Object, T>                                : Returns<bool> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __lt__<T, Object>                                : Returns<bool> {};
    template <typename T>
    struct __le__<Object, T>                                : Returns<bool> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __le__<T, Object>                                : Returns<bool> {};
    template <typename T1, typename T2> requires (std::is_base_of_v<Object, T1>)
    struct __eq__<T1, T2>                                   : Returns<bool> {};
    template <typename T1, typename T2> requires (!std::is_base_of_v<Object, T1> && std::is_base_of_v<Object, T2>)
    struct __eq__<T1, T2>                                   : Returns<bool> {};
    template <typename T1, typename T2> requires (std::is_base_of_v<Object, T1>)
    struct __ne__<T1, T2>                                   : Returns<bool> {};
    template <typename T1, typename T2> requires (!std::is_base_of_v<Object, T1> && std::is_base_of_v<Object, T2>)
    struct __ne__<T1, T2>                                   : Returns<bool> {};
    template <typename T>
    struct __ge__<Object, T>                                : Returns<bool> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __ge__<T, Object>                                : Returns<bool> {};
    template <typename T>
    struct __gt__<Object, T>                                : Returns<bool> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __gt__<T, Object>                                : Returns<bool> {};
    template <typename T>
    struct __add__<Object, T>                               : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __add__<T, Object>                               : Returns<Object> {};
    template <typename T>
    struct __sub__<Object, T>                               : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __sub__<T, Object>                               : Returns<Object> {};
    template <typename T>
    struct __mul__<Object, T>                               : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __mul__<T, Object>                               : Returns<Object> {};
    template <typename T>
    struct __truediv__<Object, T>                           : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __truediv__<T, Object>                           : Returns<Object> {};
    template <typename T>
    struct __mod__<Object, T>                               : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __mod__<T, Object>                               : Returns<Object> {};
    template <typename T>
    struct __lshift__<Object, T>                            : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __lshift__<T, Object>                            : Returns<Object> {};
    template <typename T>
    struct __rshift__<Object, T>                            : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __rshift__<T, Object>                            : Returns<Object> {};
    template <typename T>
    struct __and__<Object, T>                               : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __and__<T, Object>                               : Returns<Object> {};
    template <typename T>
    struct __or__<Object, T>                                : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __or__<T, Object>                                : Returns<Object> {};
    template <typename T>
    struct __xor__<Object, T>                               : Returns<Object> {};
    template <typename T> requires (!std::is_base_of_v<Object, T>)
    struct __xor__<T, Object>                               : Returns<Object> {};
    template <typename T>
    struct __iadd__<Object, T>                              : Returns<Object> {};
    template <typename T>
    struct __isub__<Object, T>                              : Returns<Object> {};
    template <typename T>
    struct __imul__<Object, T>                              : Returns<Object> {};
    template <typename T>
    struct __itruediv__<Object, T>                          : Returns<Object> {};
    template <typename T>
    struct __imod__<Object, T>                              : Returns<Object> {};
    template <typename T>
    struct __ilshift__<Object, T>                           : Returns<Object> {};
    template <typename T>
    struct __irshift__<Object, T>                           : Returns<Object> {};
    template <typename T>
    struct __iand__<Object, T>                              : Returns<Object> {};
    template <typename T>
    struct __ior__<Object, T>                               : Returns<Object> {};
    template <typename T>
    struct __ixor__<Object, T>                              : Returns<Object> {};

    template <>
    struct __hash__<Handle>                                     : Returns<size_t> {};
    template <>
    struct __hash__<Capsule>                                    : Returns<size_t> {};
    template <>
    struct __hash__<WeakRef>                                    : Returns<size_t> {};
    template <>
    struct __hash__<NoneType>                                   : Returns<size_t> {};
    template <>
    struct __hash__<NotImplementedType>                         : Returns<size_t> {};
    template <>
    struct __hash__<EllipsisType>                               : Returns<size_t> {};
    template <>
    struct __hash__<Module>                                     : Returns<size_t> {};

    /* Standardized error message for type narrowing via pybind11 accessors or the
    generic Object wrapper. */
    template <typename Derived>
    TypeError noconvert(PyObject* obj) {
        pybind11::type source = pybind11::type::of(obj);
        pybind11::type dest = Derived::type;
        const char* source_name = reinterpret_cast<PyTypeObject*>(source.ptr())->tp_name;
        const char* dest_name = reinterpret_cast<PyTypeObject*>(dest.ptr())->tp_name;

        std::ostringstream msg;
        msg << "cannot convert python object from type '" << source_name;
        msg << "' to type '" << dest_name << "'";
        return TypeError(msg.str());
    }

    template <typename Obj, typename Key> requires (__getitem__<Obj, Key>::enable)
    class Item;
    template <typename Obj>
    class AttrProxy;
    template <typename Policy>
    class Iterator;
    template <typename Policy>
    class ReverseIterator;
    template <typename Deref>
    class GenericIter;

    struct SliceInitializer;

}  // namespace impl


/* A revised pybind11::object interface that allows implicit conversions to subtypes
(applying a type check on the way), explicit conversions to arbitrary C++ types via
static_cast<>, cross-language math operators, and generalized slice/attr syntax. */
class Object : public pybind11::object {
    using Base = pybind11::object;

    template <typename L, typename R>
    static constexpr bool noproxy = !impl::proxy_like<L> && !impl::proxy_like<R>;

protected:

    /* Default constructor.  Initializes to a null object, which should always be
    filled in before being returned to the user.  Protecting this reduces the risk of
    null pointers being accidentally introduced in client code.  As it stands, the only
    that can happen is by interacting with an object after it has been moved from,
    which is undefined behavior anyways. */
    Object() = default;

public:
    static Type type;

    /* Check whether the templated type is considered object-like at compile time. */
    template <typename T>
    static constexpr bool check() { return std::is_base_of_v<pybind11::object, T>; }

    /* Check whether a C++ value is considered object-like at compile time. */
    template <typename T> requires (!impl::python_like<T>)
    static constexpr bool check(const T& value) { return check<T>(); }

    /* Check whether a Python value is considered object-like at runtime. */
    template <typename T> requires (impl::python_like<T>)
    static constexpr bool check(const T& value) { return value.ptr() != nullptr; }

    /* Identical to the above, but pybind11 expects a method of this name. */
    template <typename T> requires (!impl::python_like<T>)
    static constexpr bool check_(const T& value) { return check(value); }
    template <typename T> requires (impl::python_like<T>)
    static constexpr bool check_(const T& value) { return check(value); }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* reinterpret_borrow()/reinterpret_steal() constructors.  The tags themselves are
    protected and only accessible within subclasses of pybind11::object. */
    Object(pybind11::handle h, const borrowed_t& t) : Base(h, t) {}
    Object(pybind11::handle h, const stolen_t& t) : Base(h, t) {}

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const pybind11::object& o) : Base(o) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(pybind11::object&& o) : Base(std::move(o)) {}

    /* Convert a pybind11 accessor into a generic Object. */
    template <typename Policy>
    Object(const detail::accessor<Policy> &a) {
        pybind11::object obj(a);
        if (check(obj)) {
            m_ptr = obj.release().ptr();
        } else {
            throw impl::noconvert<Object>(obj.ptr());
        }
    }

    /* Convert any C++ value into a generic python object. */
    template <typename T> requires (!impl::python_like<T>)
    Object(const T& value) : Base(pybind11::cast(value).release(), stolen_t{}) {}

    /* Trigger implicit conversions to this type via the assignment operator. */
    template <typename T> requires (std::is_convertible_v<T, Object>)
    Object& operator=(T&& value) {
        if constexpr (std::is_same_v<Object, std::decay_t<T>>) {
            if (this != &value) {
                Base::operator=(std::forward<T>(value));
            }
        } else if constexpr (impl::has_conversion_operator<std::decay_t<T>, Object>) {
            Base::operator=(value.operator Object());
        } else {
            Base::operator=(Object(std::forward<T>(value)));
        }
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* NOTE: the Object wrapper can be implicitly converted to any of its subclasses by
     * applying a runtime type check as part of the assignment.  This allows us to
     * safely convert from a generic object to a more specialized type without worrying
     * about type mismatches or triggering arbitrary conversion logic.  It allows us to
     * write code like this:
     *
     *      py::Object obj = true;
     *      py::Bool b = obj;
     *
     * But not like this:
     *
     *      py::Object obj = true;
     *      py::Str s = obj;  // throws a TypeError
     *
     * While simultaneously preserving the ability to explicitly convert using a normal
     * constructor call:
     *
     *      py::Object obj = true;
     *      py::Str s(obj);
     *
     * Which is identical to calling `str()` at the python level.  Note that the
     * implicit conversion operator is only enabled for Object itself, and is deleted
     * in all of its subclasses.  This prevents implicit conversions between subclasses
     * and promotes any attempt to do so into a compile-time error.  For instance:
     *
     *      py::Bool b = true;
     *      py::Str s = b;  // fails to compile, calls a deleted function
     *
     * In general, this makes assignment via the `=` operator type-safe by default,
     * while explicit constructors are reserved for non-trivial conversions and/or
     * packing in the case of containers.
     */

    /* Implicitly convert an Object wrapper to one of its subclasses, applying a
    runtime type check against the underlying value. */
    template <typename T> requires (std::is_base_of_v<Object, T>)
    inline operator T() const {
        if (!T::check(*this)) {
            throw impl::noconvert<T>(this->ptr());
        }
        return reinterpret_borrow<T>(this->ptr());
    }

    /* Implicitly convert an Object to a proxy class, which moves it into a managed
    buffer for static storage duration, attribute access, etc. */
    template <typename T> requires (impl::proxy_like<T>)
    inline operator T() const {
        return T(this->operator typename T::Wrapped());
    }

    /* Explicitly convert to any other non-Object type using pybind11's type casting
    mechanism. */
    template <typename T>
        requires (!impl::proxy_like<T> && !std::is_base_of_v<Object, T>)
    inline explicit operator T() const {
        return Base::cast<T>();
    }

    /* Contextually convert an Object into a boolean for use in if/else statements,
    with the same semantics as in Python. */
    inline explicit operator bool() const {
        int result = PyObject_IsTrue(this->ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Explicitly cast to a string representation.  For some reason,
    pybind11::cast<std::string>() doesn't always work for arbitrary types.  This
    corrects that and gives the same results as Python `str(obj)`. */
    inline explicit operator std::string() const {
        PyObject* str = PyObject_Str(this->ptr());
        if (str == nullptr) {
            throw error_already_set();
        }
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(str, &size);
        if (data == nullptr) {
            Py_DECREF(str);
            throw error_already_set();
        }
        std::string result(data, size);
        Py_DECREF(str);
        return result;
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    BERTRAND_OBJECT_OPERATORS(Object)

    template <StaticStr key>
    inline impl::AttrProxy<Object> attr() const;

    template <typename T> requires (!impl::__invert__<T>::enable)
    inline friend bool operator~(const T& self) = delete;
    template <typename T> requires (impl::__invert__<T>::enable)
    inline friend auto operator~(const T& self) {
        using Return = typename impl::__invert__<T>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Bitwise NOT operator must return a py::Object subclass.  Check your "
            "specialization of __invert__ for this type and ensure the Return type "
            "is set to a py::Object subclass."
        );
        return T::template operator_invert<Return>(self);
    }

    template <typename L, typename R>
        requires (!impl::__lt__<L, R>::enable && noproxy<L, R>)
    inline friend bool operator<(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__lt__<L, R>::enable)
    inline friend auto operator<(const L& lhs, const R& rhs) {
        using Return = typename impl::__lt__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, bool>,
            "Less-than operator must return a boolean value.  Check your "
            "specialization of __lt__ for these types and ensure the Return type "
            "is set to bool."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_lt<Return>(lhs, rhs);
        } else {
            return R::template operator_lt<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__le__<L, R>::enable && noproxy<L, R>)
    inline friend bool operator<=(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__le__<L, R>::enable)
    inline friend auto operator<=(const L& lhs, const R& rhs) {
        using Return = typename impl::__le__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, bool>,
            "Less-than-or-equal operator must return a boolean value.  Check your "
            "specialization of __le__ for this type and ensure the Return type is "
            "set to bool."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_le<Return>(lhs, rhs);
        } else {
            return R::template operator_le<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__eq__<L, R>::enable && noproxy<L, R>)
    inline friend bool operator==(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__eq__<L, R>::enable)
    inline friend auto operator==(const L& lhs, const R& rhs) {
        using Return = typename impl::__eq__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, bool>,
            "Equality operator must return a boolean value.  Check your "
            "specialization of __eq__ for this type and ensure the Return type is "
            "set to bool."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_eq<Return>(lhs, rhs);
        } else {
            return R::template operator_eq<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__ne__<L, R>::enable && noproxy<L, R>)
    inline friend bool operator!=(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__ne__<L, R>::enable)
    inline friend auto operator!=(const L& lhs, const R& rhs) {
        using Return = typename impl::__ne__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, bool>,
            "Inequality operator must return a boolean value.  Check your "
            "specialization of __ne__ for this type and ensure the Return type is "
            "set to bool."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_ne<Return>(lhs, rhs);
        } else {
            return R::template operator_ne<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__ge__<L, R>::enable && noproxy<L, R>)
    inline friend bool operator>=(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__ge__<L, R>::enable)
    inline friend auto operator>=(const L& lhs, const R& rhs) {
        using Return = typename impl::__ge__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, bool>,
            "Greater-than-or-equal operator must return a boolean value.  Check "
            "your specialization of __ge__ for this type and ensure the Return "
            "type is set to bool."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_ge<Return>(lhs, rhs);
        } else {
            return R::template operator_ge<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__gt__<L, R>::enable && noproxy<L, R>)
    inline friend bool operator>(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__gt__<L, R>::enable)
    inline friend auto operator>(const L& lhs, const R& rhs) {
        using Return = typename impl::__gt__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, bool>,
            "Greater-than operator must return a boolean value.  Check your "
            "specialization of __gt__ for this type and ensure the Return type is "
            "set to bool."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_gt<Return>(lhs, rhs);
        } else {
            return R::template operator_gt<Return>(lhs, rhs);
        }
    }

    template <typename T> requires (!impl::__pos__<T>::enable)
    inline friend auto operator+(const T& self) = delete;
    template <typename T> requires (impl::__pos__<T>::enable)
    inline friend auto operator+(const T& self) {
        using Return = typename impl::__pos__<T>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Unary positive operator must return a py::Object subclass.  Check "
            "your specialization of __pos__ for this type and ensure the Return "
            "type is set to a py::Object subclass."
        );
        return T::template operator_pos<Return>(self);
    }

    template <typename T> requires (!impl::__increment__<T>::enable)
    inline friend T& operator++(T& self) = delete;
    template <typename T> requires (impl::__increment__<T>::enable)
    inline friend T& operator++(T& self) {
        using Return = typename impl::__increment__<T>::Return;
        static_assert(
            std::is_same_v<Return, T>,
            "Increment operator must return a reference to the derived type.  "
            "Check your specialization of __increment__ for this type and ensure "
            "the Return type is set to the derived type."
        );
        T::template operator_increment<Return>(self);
        return self;
    }

    template <typename T> requires (!impl::__increment__<T>::enable)
    inline friend T operator++(T& self, int) = delete;
    template <typename T> requires (impl::__increment__<T>::enable)
    inline friend T operator++(T& self, int) {
        using Return = typename impl::__increment__<T>::Return;
        static_assert(
            std::is_same_v<Return, T>,
            "Increment operator must return a reference to the derived type.  "
            "Check your specialization of __increment__ for this type and ensure "
            "the Return type is set to the derived type."
        );
        T copy = self;
        T::template operator_increment<Return>(self);
        return copy;
    }

    template <typename L, typename R>
        requires (!impl::__add__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator+(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__add__<L, R>::enable)
    inline friend auto operator+(const L& lhs, const R& rhs) {
        using Return = typename impl::__add__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Addition operator must return a py::Object subclass.  Check your "
            "specialization of __add__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_add<Return>(lhs, rhs);
        } else {
            return R::template operator_add<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__iadd__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator+=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__iadd__<L, R>::enable)
    inline friend L& operator+=(L& lhs, const R& rhs) {
        using Return = typename impl::__iadd__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place addition operator must return a mutable reference to the left "
            "operand.  Check your specialization of __iadd__ for these types and "
            "ensure the Return type is set to the left operand."
        );
        L::template operator_iadd<Return>(lhs, rhs);
        return lhs;
    }

    template <typename T> requires (!impl::__neg__<T>::enable)
    inline friend auto operator-(const T& self) = delete;
    template <typename T> requires (impl::__neg__<T>::enable)
    inline friend auto operator-(const T& self) {
        using Return = typename impl::__neg__<T>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Unary negative operator must return a py::Object subclass.  Check "
            "your specialization of __neg__ for this type and ensure the Return "
            "type is set to a py::Object subclass."
        );
        return T::template operator_neg<Return>(self);
    }

    template <typename T> requires (!impl::__decrement__<T>::enable)
    inline friend T& operator--(T& self) = delete;
    template <typename T> requires (impl::__decrement__<T>::enable)
    inline friend T& operator--(T& self) {
        using Return = typename impl::__decrement__<T>::Return;
        static_assert(
            std::is_same_v<Return, T>,
            "Decrement operator must return a reference to the derived type.  "
            "Check your specialization of __decrement__ for this type and ensure "
            "the Return type is set to the derived type."
        );
        T::template operator_decrement<Return>(self);
        return self;
    }

    template <typename T> requires (!impl::__decrement__<T>::enable)
    inline friend T operator--(T& self, int) = delete;
    template <typename T> requires (impl::__decrement__<T>::enable)
    inline friend T operator--(T& self, int) {
        using Return = typename impl::__decrement__<T>::Return;
        static_assert(
            std::is_same_v<Return, T>,
            "Decrement operator must return a reference to the derived type.  "
            "Check your specialization of __decrement__ for this type and ensure "
            "the Return type is set to the derived type."
        );
        T copy = self;
        T::template operator_decrement<Return>(self);
        return copy;
    }

    template <typename L, typename R>
        requires (!impl::__sub__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator-(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__sub__<L, R>::enable)
    inline friend auto operator-(const L& lhs, const R& rhs) {
        using Return = typename impl::__sub__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Subtraction operator must return a py::Object subclass.  Check your "
            "specialization of __sub__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_sub<Return>(lhs, rhs);
        } else {
            return R::template operator_sub<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__isub__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator-=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__isub__<L, R>::enable)
    inline friend L& operator-=(L& lhs, const R& rhs) {
        using Return = typename impl::__isub__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place addition operator must return a mutable reference to the left "
            "operand.  Check your specialization of __isub__ for these types and "
            "ensure the Return type is set to the left operand."
        );
        L::template operator_isub<Return>(lhs, rhs);
        return lhs;
    }

    template <typename L, typename R>
        requires (!impl::__mul__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator*(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__mul__<L, R>::enable)
    inline friend auto operator*(const L& lhs, const R& rhs) {
        using Return = typename impl::__mul__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Multiplication operator must return a py::Object subclass.  Check "
            "your specialization of __mul__ for this type and ensure the Return "
            "type is derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_mul<Return>(lhs, rhs);
        } else {
            return R::template operator_mul<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__imul__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator*=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__imul__<L, R>::enable)
    inline friend L& operator*=(L& lhs, const R& rhs) {
        using Return = typename impl::__imul__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place multiplication operator must return a mutable reference to the "
            "left operand.  Check your specialization of __imul__ for these types "
            "and ensure the Return type is set to the left operand."
        );
        L::template operator_imul<Return>(lhs, rhs);
        return lhs;
    }

    template <typename L, typename R>
        requires (!impl::__truediv__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator/(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__truediv__<L, R>::enable)
    inline friend auto operator/(const L& lhs, const R& rhs) {
        using Return = typename impl::__truediv__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "True division operator must return a py::Object subclass.  Check "
            "your specialization of __truediv__ for this type and ensure the "
            "Return type is derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_truediv<Return>(lhs, rhs);
        } else {
            return R::template operator_truediv<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__itruediv__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator/=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__itruediv__<L, R>::enable)
    inline friend L& operator/=(L& lhs, const R& rhs) {
        using Return = typename impl::__itruediv__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place true division operator must return a mutable reference to the "
            "left operand.  Check your specialization of __itruediv__ for these "
            "types and ensure the Return type is set to the left operand."
        );
        L::template operator_itruediv<Return>(lhs, rhs);
        return lhs;
    }

    template <typename L, typename R>
        requires (!impl::__mod__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator%(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__mod__<L, R>::enable)
    inline friend auto operator%(const L& lhs, const R& rhs) {
        using Return = typename impl::__mod__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Modulus operator must return a py::Object subclass.  Check your "
            "specialization of __mod__ for this type and ensure the Return type "
            "is derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_mod<Return>(lhs, rhs);
        } else {
            return R::template operator_mod<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__imod__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator%=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__imod__<L, R>::enable)
    inline friend L& operator%=(L& lhs, const R& rhs) {
        using Return = typename impl::__imod__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place modulus operator must return a mutable reference to the left "
            "operand.  Check your specialization of __imod__ for these types and "
            "ensure the Return type is set to the left operand."
        );
        L::template operator_imod<Return>(lhs, rhs);
        return lhs;
    }

    template <typename L, typename R>
        requires (!impl::__lshift__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator<<(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__lshift__<L, R>::enable)
    inline friend auto operator<<(const L& lhs, const R& rhs) {
        using Return = typename impl::__lshift__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Left shift operator must return a py::Object subclass.  Check your "
            "specialization of __lshift__ for this type and ensure the Return "
            "type is derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_lshift<Return>(lhs, rhs);
        } else {
            return R::template operator_lshift<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__ilshift__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator<<=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__ilshift__<L, R>::enable)
    inline friend L& operator<<=(L& lhs, const R& rhs) {
        using Return = typename impl::__ilshift__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place left shift operator must return a mutable reference to the left "
            "operand.  Check your specialization of __ilshift__ for these types "
            "and ensure the Return type is set to the left operand."
        );
        L::template operator_ilshift<Return>(lhs, rhs);
        return lhs;
    }

    template <typename L, typename R>
        requires (!impl::__rshift__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator>>(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__rshift__<L, R>::enable)
    inline friend auto operator>>(const L& lhs, const R& rhs) {
        using Return = typename impl::__rshift__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Right shift operator must return a py::Object subclass.  Check your "
            "specialization of __rshift__ for this type and ensure the Return "
            "type is derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_rshift<Return>(lhs, rhs);
        } else {
            return R::template operator_rshift<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__irshift__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator>>=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__irshift__<L, R>::enable)
    inline friend L& operator>>=(L& lhs, const L& rhs) {
        using Return = typename impl::__irshift__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place right shift operator must return a mutable reference to the left "
            "operand.  Check your specialization of __irshift__ for these types "
            "and ensure the Return type is set to the left operand."
        );
        L::template operator_irshift<Return>(lhs, rhs);
        return lhs;
    }

    template <typename L, typename R>
        requires (!impl::__and__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator&(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__and__<L, R>::enable)
    inline friend auto operator&(const L& lhs, const R& rhs) {
        using Return = typename impl::__and__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Bitwise AND operator must return a py::Object subclass.  Check your "
            "specialization of __and__ for this type and ensure the Return type "
            "is derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_and<Return>(lhs, rhs);
        } else {
            return R::template operator_and<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__iand__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator&=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__iand__<L, R>::enable)
    inline friend L& operator&=(L& lhs, const R& rhs) {
        using Return = typename impl::__iand__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place bitwise AND operator must return a mutable reference to the left "
            "operand.  Check your specialization of __iand__ for these types and "
            "ensure the Return type is set to the left operand."
        );
        L::template operator_iand<Return>(lhs, rhs);
        return lhs;
    }

    template <typename L, typename R>
        requires (!impl::__or__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator|(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__or__<L, R>::enable)
    inline friend auto operator|(const L& lhs, const R& rhs) {
        using Return = typename impl::__or__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Bitwise OR operator must return a py::Object subclass.  Check your "
            "specialization of __or__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_or<Return>(lhs, rhs);
        } else {
            return R::template operator_or<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__ior__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator|=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__ior__<L, R>::enable)
    inline friend L& operator|=(L& lhs, const R& rhs) {
        using Return = typename impl::__ior__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place bitwise OR operator must return a mutable reference to the left "
            "operand.  Check your specialization of __ior__ for these types and "
            "ensure the Return type is set to the left operand."
        );
        L::template operator_ior<Return>(lhs, rhs);
        return lhs;
    }

    template <typename L, typename R>
        requires (!impl::__xor__<L, R>::enable && noproxy<L, R>)
    inline friend auto operator^(const L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__xor__<L, R>::enable)
    inline friend auto operator^(const L& lhs, const R& rhs) {
        using Return = typename impl::__xor__<L, R>::Return;
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Bitwise XOR operator must return a py::Object subclass.  Check your "
            "specialization of __xor__ for this type and ensure the Return type "
            "is derived from py::Object."
        );
        if constexpr (std::is_base_of_v<Object, L>) {
            return L::template operator_xor<Return>(lhs, rhs);
        } else {
            return R::template operator_xor<Return>(lhs, rhs);
        }
    }

    template <typename L, typename R>
        requires (!impl::__ixor__<L, R>::enable && noproxy<L, R>)
    inline friend L& operator^=(L& lhs, const R& rhs) = delete;
    template <typename L, typename R> requires (impl::__ixor__<L, R>::enable)
    inline friend L& operator^=(L& lhs, const R& rhs) {
        using Return = typename impl::__ixor__<L, R>::Return;
        static_assert(
            std::is_same_v<Return, L&>,
            "In-place bitwise XOR operator must return a mutable reference to the left "
            "operand.  Check your specialization of __ixor__ for these types and "
            "ensure the Return type is set to the left operand."
        );
        L::template operator_ixor<Return>(lhs, rhs);
        return lhs;
    }

    template <typename T>
    inline friend std::ostream& operator<<(std::ostream& os, const T& obj) {
        PyObject* repr = PyObject_Repr(obj.ptr());
        if (repr == nullptr) {
            throw error_already_set();
        }
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(repr, &size);
        if (data == nullptr) {
            Py_DECREF(repr);
            throw error_already_set();
        }
        os.write(data, size);
        Py_DECREF(repr);
        return os;
    }

private:

    template <typename... Args>
    inline auto operator_call_impl(Args&&... args) const {
        return pybind11::object::operator()(std::forward<Args>(args)...);
    }

    template <typename T>
    inline bool operator_contains_impl(const T& key) const {
        return pybind11::object::contains(key);
    }

    inline auto operator_dereference_impl() const {
        return pybind11::object::operator*();
    }

protected:

    template <typename Return, typename T, typename... Args>
    inline static Return operator_call(const T& obj, Args&&... args) {
        if constexpr (std::is_void_v<Return>) {
            obj.operator_call_impl(std::forward<Args>(args)...);
        } else {
            return reinterpret_steal<Return>(
                obj.operator_call_impl(std::forward<Args>(args)...).release()
            );
        }
    }

    template <typename Return, typename T, typename Key>
    inline static auto operator_getitem(const T& obj, Key&& key)
        -> impl::Item<T, std::decay_t<Key>>;

    template <typename Return, typename T>
    inline static auto operator_getitem(
        const T& obj,
        std::initializer_list<impl::SliceInitializer> slice
    ) -> impl::Item<T, Slice>;

    template <typename Return, typename T>
    inline static auto operator_begin(const T& obj)
        -> impl::Iterator<impl::GenericIter<Return>>;

    template <typename Return, typename T>
    inline static auto operator_end(const T& obj)
        -> impl::Iterator<impl::GenericIter<Return>>;

    template <typename Return, typename T>
    inline static auto operator_rbegin(const T& obj)
        -> impl::Iterator<impl::GenericIter<Return>>;

    template <typename Return, typename T>
    inline static auto operator_rend(const T& obj)
        -> impl::Iterator<impl::GenericIter<Return>>;

    template <typename Return, typename L, typename R>
    inline static bool operator_contains(const L& lhs, const R& rhs) {
        return lhs.operator_contains_impl(rhs);
    }

    template <typename Return, typename T>
    inline static size_t operator_len(const T& obj) {
        Py_ssize_t size = PyObject_Size(obj.ptr());
        if (size < 0) {
            throw error_already_set();
        }
        return size;
    }

    template <typename Return, typename T>
    inline static auto operator_dereference(const T& obj) {
        return obj.operator_dereference_impl();
    }

    template <typename Return, typename T>
    inline static auto operator_pos(const T& obj) {
        PyObject* result = PyNumber_Positive(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename T>
    inline static auto operator_neg(const T& obj) {
        PyObject* result = PyNumber_Negative(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename T>
    inline static auto operator_invert(const T& obj) {
        PyObject* result = PyNumber_Invert(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename T>
    inline static void operator_increment(T& obj) {
        static const pybind11::int_ one = 1;
        PyObject* result = PyNumber_InPlaceAdd(
            detail::object_or_cast(obj).ptr(),
            one.ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        if (result == obj.ptr()) {
            Py_DECREF(result);
        } else {
            obj = reinterpret_steal<Return>(result);
        }
    }

    template <typename Return, typename T>
    inline static void operator_decrement(T& obj) {
        static const pybind11::int_ one = 1;
        PyObject* result = PyNumber_InPlaceSubtract(
            detail::object_or_cast(obj).ptr(),
            one.ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        if (result == obj.ptr()) {
            Py_DECREF(result);
        } else {
            obj = reinterpret_steal<Return>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_lt(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_LT
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_le(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_LE
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_eq(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_EQ
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_ne(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_NE
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_ge(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_GE
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_gt(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_GT
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_add(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Add(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_iadd(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceAdd(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_sub(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Subtract(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_isub(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceAdd(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_mul(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Multiply(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_imul(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceMultiply(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_truediv(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_TrueDivide(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_itruediv(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceTrueDivide(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_mod(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Remainder(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_imod(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceRemainder(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_lshift(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Lshift(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_ilshift(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceLshift(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_rshift(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Rshift(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_irshift(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceRshift(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_and(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_And(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_iand(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceAnd(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_or(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Or(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_ior(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceOr(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

    template <typename Return, typename L, typename R>
    inline static auto operator_xor(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Xor(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename L, typename R>
    inline static void operator_ixor(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceXor(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }

};


namespace impl {

    /* All subclasses of py::Object must define these constructors, which are taken
    directly from PYBIND11_OBJECT_COMMON and cover the basic object creation and
    conversion logic.

    The check() function will be called whenever a generic Object wrapper is implicitly
    converted to this type.  It should return true if and only if the object has a
    compatible type, and it will never be passed a null pointer.  If it returns false,
    a TypeError will be raised during the assignment.

    Also included are generic copy and move constructors that work with any object type
    that is like this one.  It depends on each type implementing the `like` trait
    before invoking this macro.

    Lastly, a generic assignment operator is provided that triggers implicit
    conversions to this type for any inputs that support them.  This is especially
    nice for initializer-list syntax, enabling containers to be assigned to like this:

        py::List list = {1, 2, 3, 4, 5};
        list = {5, 4, 3, 2, 1};
    */
    #define BERTRAND_OBJECT_COMMON(parent, cls, comptime_check, runtime_check)          \
        /* Implement check() for compile-time C++ types. */                             \
        template <typename T>                                                           \
        static consteval bool check() { return comptime_check<T>; }                     \
                                                                                        \
        /* Implement check() for runtime C++ values. */                                 \
        template <typename T> requires (!impl::python_like<T>)                          \
        static consteval bool check(const T&) {                                         \
            return check<T>();                                                          \
        }                                                                               \
                                                                                        \
        /* Implement check() for runtime Python values. */                              \
        template <typename T> requires (impl::python_like<T>)                           \
        static bool check(const T& obj) {                                               \
            return obj.ptr() != nullptr && runtime_check(obj.ptr());                    \
        }                                                                               \
                                                                                        \
        /* pybind11 expects check_ internally, but the idea is the same. */             \
        template <typename T> requires (impl::python_like<T>)                           \
        static constexpr bool check_(const T& value) { return check(value); }           \
        template <typename T> requires (!impl::python_like<T>)                          \
        static constexpr bool check_(const T& value) { return check(value); }           \
                                                                                        \
        /* Inherit tagged borrow/steal constructors. */                                 \
        cls(Handle h, const borrowed_t& t) : parent(h, t) {}                            \
        cls(Handle h, const stolen_t& t) : parent(h, t) {}                              \
                                                                                        \
        /* Convert a pybind11 accessor into this type. */                               \
        template <typename Policy>                                                      \
        cls(const detail::accessor<Policy>& a) {                                        \
            pybind11::object obj(a);                                                    \
            if (check(obj)) {                                                           \
                m_ptr = obj.release().ptr();                                            \
            } else {                                                                    \
                throw impl::noconvert<cls>(obj.ptr());                                  \
            }                                                                           \
        }                                                                               \
                                                                                        \
        /* Trigger implicit conversions to this type via the assignment operator. */    \
        template <typename T> requires (std::is_convertible_v<T, cls>)                  \
        cls& operator=(T&& value) {                                                     \
            if constexpr (std::is_same_v<cls, std::decay_t<T>>) {                       \
                if (this != &value) {                                                   \
                    parent::operator=(std::forward<T>(value));                          \
                }                                                                       \
            } else if constexpr (impl::has_conversion_operator<std::decay_t<T>, cls>) { \
                parent::operator=(value.operator cls());                                \
            } else {                                                                    \
                parent::operator=(cls(std::forward<T>(value)));                         \
            }                                                                           \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        /* Delete type narrowing operator inherited from Object. */                     \
        template <typename T> requires (std::is_base_of_v<Object, T>)                   \
        operator T() const = delete;                                                    \
                                                                                        \
    protected:                                                                          \
                                                                                        \
        template <typename T> requires (impl::__invert__<T>::enable)                    \
        friend auto operator~(const T& self);                                           \
                                                                                        \
        template <typename L, typename R> requires (impl::__lt__<L, R>::enable)         \
        friend auto operator<(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__le__<L, R>::enable)         \
        friend auto operator<=(const L& lhs, const R& rhs);                             \
                                                                                        \
        template <typename L, typename R> requires (impl::__eq__<L, R>::enable)         \
        friend auto operator==(const L& lhs, const R& rhs);                             \
                                                                                        \
        template <typename L, typename R> requires (impl::__ne__<L, R>::enable)         \
        friend auto operator!=(const L& lhs, const R& rhs);                             \
                                                                                        \
        template <typename L, typename R> requires (impl::__ge__<L, R>::enable)         \
        friend auto operator>=(const L& lhs, const R& rhs);                             \
                                                                                        \
        template <typename L, typename R> requires (impl::__gt__<L, R>::enable)         \
        friend auto operator>(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename T> requires (impl::__pos__<T>::enable)                       \
        friend auto operator+(const T& self);                                           \
                                                                                        \
        template <typename T> requires (impl::__increment__<T>::enable)                 \
        friend auto operator++(T& self);                                                \
                                                                                        \
        template <typename T> requires (impl::__increment__<T>::enable)                 \
        friend auto operator++(T& self, int);                                           \
                                                                                        \
        template <typename L, typename R> requires (impl::__add__<L, R>::enable)        \
        friend auto operator+(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__iadd__<L, R>::enable)       \
        friend L& operator+=(L& lhs, const R& rhs);                                     \
                                                                                        \
        template <typename T> requires (impl::__neg__<T>::enable)                       \
        friend auto operator-(const T& self);                                           \
                                                                                        \
        template <typename T> requires (impl::__decrement__<T>::enable)                 \
        friend auto operator--(T& self);                                                \
                                                                                        \
        template <typename T> requires (impl::__decrement__<T>::enable)                 \
        friend auto operator--(T& self, int);                                           \
                                                                                        \
        template <typename L, typename R> requires (impl::__sub__<L, R>::enable)        \
        friend auto operator-(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__isub__<L, R>::enable)       \
        friend L& operator-=(L& lhs, const R& rhs);                                     \
                                                                                        \
        template <typename L, typename R> requires (impl::__mul__<L, R>::enable)        \
        friend auto operator*(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__imul__<L, R>::enable)       \
        friend L& operator*=(L& lhs, const R& rhs);                                     \
                                                                                        \
        template <typename L, typename R> requires (impl::__truediv__<L, R>::enable)    \
        friend auto operator/(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__itruediv__<L, R>::enable)   \
        friend L& operator/=(L& lhs, const R& rhs);                                     \
                                                                                        \
        template <typename L, typename R> requires (impl::__mod__<L, R>::enable)        \
        friend auto operator%(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__imod__<L, R>::enable)       \
        friend L& operator%=(L& lhs, const R& rhs);                                     \
                                                                                        \
        template <typename L, typename R> requires (impl::__lshift__<L, R>::enable)     \
        friend auto operator<<(const L& lhs, const R& rhs);                             \
                                                                                        \
        template <typename L, typename R> requires (impl::__ilshift__<L, R>::enable)    \
        friend L& operator<<=(L& lhs, const R& rhs);                                    \
                                                                                        \
        template <typename L, typename R> requires (impl::__rshift__<L, R>::enable)     \
        friend auto operator>>(const L& lhs, const R& rhs);                             \
                                                                                        \
        template <typename L, typename R> requires (impl::__irshift__<L, R>::enable)    \
        friend L& operator>>=(L& lhs, const R& rhs);                                    \
                                                                                        \
        template <typename L, typename R> requires (impl::__and__<L, R>::enable)        \
        friend auto operator&(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__iand__<L, R>::enable)       \
        friend L& operator&=(L& lhs, const R& rhs);                                     \
                                                                                        \
        template <typename L, typename R> requires (impl::__or__<L, R>::enable)         \
        friend auto operator|(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__ior__<L, R>::enable)        \
        friend L& operator|=(L& lhs, const R& rhs);                                     \
                                                                                        \
        template <typename L, typename R> requires (impl::__xor__<L, R>::enable)        \
        friend auto operator^(const L& lhs, const R& rhs);                              \
                                                                                        \
        template <typename L, typename R> requires (impl::__ixor__<L, R>::enable)       \
        friend L& operator^=(L& lhs, const R& rhs);                                     \
                                                                                        \
    public:                                                                             \

    /* Base class for all accessor proxies.  Stores an arbitrary object in a buffer and
    forwards its interface using pointer semantics. */
    template <typename Obj, typename Derived>
    class Proxy : public ProxyTag {
    public:
        using Wrapped = Obj;

    protected:
        alignas (Wrapped) mutable unsigned char buffer[sizeof(Wrapped)];
        mutable bool initialized;

    private:

        Wrapped& deref() { return static_cast<Derived&>(*this).value(); }
        const Wrapped& deref() const { return static_cast<const Derived&>(*this).value(); }

    public:

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Default constructor.  Creates an empty proxy */
        Proxy() : initialized(false) {}

        /* Forwarding copy constructor for wrapped object. */
        Proxy(const Wrapped& other) : initialized(true) {
            new (buffer) Wrapped(other);
        }

        /* Forwarding move constructor for wrapped object. */
        Proxy(Wrapped&& other) : initialized(true) {
            new (buffer) Wrapped(std::move(other));
        }

        /* Copy constructor for proxy. */
        Proxy(const Proxy& other) : initialized(other.initialized) {
            if (initialized) {
                new (buffer) Wrapped(reinterpret_cast<Wrapped&>(other.buffer));
            }
        }

        /* Move constructor for proxy. */
        Proxy(Proxy&& other) : initialized(other.initialized) {
            if (initialized) {
                other.initialized = false;
                new (buffer) Wrapped(std::move(reinterpret_cast<Wrapped&>(other.buffer)));
            }
        }

        /* Forwarding copy assignment for wrapped object. */
        Proxy& operator=(const Wrapped& other) {
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer) = other;
            } else {
                new (buffer) Wrapped(other);
                initialized = true;
            }
            return *this;
        }

        /* Forwarding move assignment for wrapped object. */
        Proxy& operator=(Wrapped&& other) {
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer) = std::move(other);
            } else {
                new (buffer) Wrapped(std::move(other));
                initialized = true;
            }
            return *this;
        }

        /* Copy assignment operator. */
        Proxy& operator=(const Proxy& other) {
            if (&other != this) {
                if (initialized) {
                    initialized = false;
                    reinterpret_cast<Wrapped&>(buffer).~Wrapped();
                }
                if (other.initialized) {
                    new (buffer) Wrapped(reinterpret_cast<Wrapped&>(other.buffer));
                    initialized = true;
                }
            }
            return *this;
        }

        /* Move assignment operator. */
        Proxy& operator=(Proxy&& other) {
            if (&other != this) {
                if (initialized) {
                    initialized = false;
                    reinterpret_cast<Wrapped&>(buffer).~Wrapped();
                }
                if (other.initialized) {
                    other.initialized = false;
                    new (buffer) Wrapped(
                        std::move(reinterpret_cast<Wrapped&>(other.buffer))
                    );
                    initialized = true;
                }
            }
            return *this;
        }

        /* Destructor.  Can be avoided by manually clearing the initialized flag. */
        ~Proxy() {
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer).~Wrapped();
            }
        }

        ///////////////////////////
        ////    DEREFERENCE    ////
        ///////////////////////////

        inline bool has_value() const {
            return initialized;
        }

        inline Wrapped& value() {
            if (!initialized) {
                throw ValueError(
                    "attempt to dereference an uninitialized accessor.  Either the "
                    "accessor was moved from or not properly constructed to begin with."
                );
            }
            return reinterpret_cast<Wrapped&>(buffer);
        }

        inline const Wrapped& value() const {
            if (!initialized) {
                throw ValueError(
                    "attempt to dereference an uninitialized accessor.  Either the "
                    "accessor was moved from or not properly constructed to begin with."
                );
            }
            return reinterpret_cast<const Wrapped&>(buffer);
        }

        ////////////////////////////////////
        ////    FORWARDING INTERFACE    ////
        ////////////////////////////////////

        inline auto operator*() {
            return *deref();
        }

        // all attributes of wrapped type are forwarded using the arrow operator.  Just
        // replace all instances of `.` with `->`
        inline Wrapped* operator->() {
            return &deref();
        };

        inline const Wrapped* operator->() const {
            return &deref();
        };

        // TODO: make sure that converting proxy to wrapped object does not create a copy.
        // -> This matters when modifying kwonly defaults in func.h

        inline operator Wrapped&() {
            return deref();
        }

        inline operator const Wrapped&() const {
            return deref();
        }

        template <typename T>
            requires (!std::is_same_v<T, Wrapped> && std::is_convertible_v<Wrapped, T>)
        inline operator T() const {
            return implicit_cast<T>(deref());
        }

        template <typename T> requires (!std::is_convertible_v<Wrapped, T>)
        inline explicit operator T() const {
            return static_cast<T>(deref());
        }

        /////////////////////////
        ////    OPERATORS    ////
        /////////////////////////

        template <typename... Args>
        inline auto operator()(Args&&... args) const {
            return deref()(std::forward<Args>(args)...);
        }

        template <typename T>
        inline auto operator[](T&& key) const {
            return deref()[std::forward<T>(key)];
        }

        template <typename T = Wrapped> requires (impl::__getitem__<T, Slice>::enable)
        inline auto operator[](std::initializer_list<impl::SliceInitializer> slice) const;

        template <typename T>
        inline auto contains(const T& key) const { return deref().contains(key); }
        inline auto size() const { return deref().size(); }
        inline auto begin() const { return deref().begin(); }
        inline auto end() const { return deref().end(); }
        inline auto rbegin() const { return deref().rbegin(); }
        inline auto rend() const { return deref().rend(); }

        inline auto operator~() const { return ~deref(); }

        template <typename R>
        inline auto operator<(const R& rhs) {
            return deref() < rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator<(const L& lhs, const Proxy& rhs) {
            return lhs < rhs.deref();
        }

        template <typename R>
        inline auto operator<=(const R& rhs) {
            return deref() <= rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator<=(const L& lhs, const Proxy& rhs) {
            return lhs <= rhs.deref();
        }

        template <typename R>
        inline auto operator==(const R& rhs) {
            return deref() == rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator==(const L& lhs, const Proxy& rhs) {
            return lhs == rhs.deref();
        }

        template <typename R>
        inline auto operator!=(const R& rhs) {
            return deref() != rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator!=(const L& lhs, const Proxy& rhs) {
            return lhs != rhs.deref();
        }

        template <typename R>
        inline auto operator>=(const R& rhs) {
            return deref() >= rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator>=(const L& lhs, const Proxy& rhs) {
            return lhs >= rhs.deref();
        }

        template <typename R>
        inline auto operator>(const R& rhs) {
            return deref() > rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator>(const L& lhs, const Proxy& rhs) {
            return lhs > rhs.deref();
        }

        inline auto operator+() { return +deref(); }
        inline auto operator++() { return ++deref(); }
        inline auto operator++(int) { return deref()++; }
        template <typename R>
        inline auto operator+(const R& rhs) {
            return deref() + rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator+(const L& lhs, const Proxy& rhs) {
            return lhs + rhs.deref();
        }
        template <typename R>
        inline auto operator+=(const R& rhs) {
            deref() += rhs;
            return *this;
        }

        inline auto operator-() { return -deref(); }
        inline auto operator--() { return --deref(); }
        inline auto operator--(int) { return deref()--; }
        template <typename R>
        inline auto operator-(const R& rhs) {
            return deref() - rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator-(const L& lhs, const Proxy& rhs) {
            return lhs - rhs.deref();
        }
        template <typename R>
        inline auto operator-=(const R& rhs) {
            deref() -= rhs;
            return *this;
        }

        template <typename R>
        inline auto operator*(const R& rhs) {
            return deref() * rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator*(const L& lhs, const Proxy& rhs) {
            return lhs * rhs.deref();
        }
        template <typename R>
        inline auto operator*=(const R& rhs) {
            deref() *= rhs;
            return *this;
        }

        template <typename R>
        inline auto operator/(const R& rhs) {
            return deref() / rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator/(const L& lhs, const Proxy& rhs) {
            return lhs / rhs.deref();
        }
        template <typename R>
        inline auto operator/=(const R& rhs) {
            deref() /= rhs;
            return *this;
        }

        template <typename R>
        inline auto operator%(const R& rhs) {
            return deref() % rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator%(const L& lhs, const Proxy& rhs) {
            return lhs % rhs.deref();
        }
        template <typename R>
        inline auto operator%=(const R& rhs) {
            deref() %= rhs;
            return *this;
        }

        template <typename R>
        inline auto operator<<(const R& rhs) {
            return deref() << rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator<<(const L& lhs, const Proxy& rhs) {
            return lhs << rhs.deref();
        }
        template <typename R>
        inline auto operator<<=(const R& rhs) {
            deref() <<= rhs;
            return *this;
        }

        template <typename R>
        inline auto operator>>(const R& rhs) {
            return deref() >> rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator>>(const L& lhs, const Proxy& rhs) {
            return lhs >> rhs.deref();
        }
        template <typename R>
        inline auto operator>>=(const R& rhs) {
            deref() >>= rhs;
            return *this;
        }

        template <typename R>
        inline auto operator&(const R& rhs) {
            return deref() & rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator&(const L& lhs, const Proxy& rhs) {
            return lhs & rhs.deref();
        }
        template <typename R>
        inline auto operator&=(const R& rhs) {
            deref() &= rhs;
            return *this;
        }

        template <typename R>
        inline auto operator|(const R& rhs) {
            return deref() | rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator|(const L& lhs, const Proxy& rhs) {
            return lhs | rhs.deref();
        }
        template <typename R>
        inline auto operator|=(const R& rhs) {
            deref() |= rhs;
            return *this;
        }

        template <typename R>
        inline auto operator^(const R& rhs) {
            return deref() ^ rhs;
        }
        template <typename L> requires (!impl::proxy_like<L>)
        inline friend auto operator^(const L& lhs, const Proxy& rhs) {
            return lhs ^ rhs.deref();
        }
        template <typename R>
        inline auto operator^=(const R& rhs) {
            deref() ^= rhs;
            return *this;
        }

        inline friend std::ostream& operator<<(std::ostream& os, const Proxy& self) {
            os << self.deref();
            return os;
        }

        inline friend std::istream& operator>>(std::istream& os, const Proxy& self) {
            os >> self.deref();
            return os;
        }

    };

    // TODO: add an attribute name for strong typing.
    // NOTE: there has to be a generic and a templated version of this to account for
    // runtime vs compile-time attribute naming.  Maybe .attr() is only used for
    // compile-time names, and getattr() is only used for attributes whose names may be
    // determined at runtime.
    // -> There still has to be a distinction between runtime names and compile-time
    // ones.  Perhaps Attr<Obj, name> vs GenericAttr<Obj>?  Maybe I can unify them but
    // reserve the empty string for dynamic attributes?  Separating them allows me to
    // explicitly disallow empty strings in the compile-time version, which is a good
    // thing.

    /* A subclass of Proxy that replaces the result of pybind11's `.attr()` method.
    This does not (and can not) enforce any strict typing rules, but it brings the
    syntax more in line with the rest of bertrand's other operator overloads. */
    template <typename Obj>
    class AttrProxy : public Proxy<Obj, AttrProxy<Obj>> {
        using Base = Proxy<Obj, AttrProxy>;
        Object obj;
        Object key;

        void get_attr() const {
            if (obj.ptr() == nullptr) {
                throw ValueError(
                    "attempt to dereference an uninitialized accessor.  Either the "
                    "accessor was moved from or not properly constructed to begin with."
                );
            }
            PyObject* result = PyObject_GetAttr(obj.ptr(), key.ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            new (Base::buffer) Obj(reinterpret_steal<Obj>(result));
            Base::initialized = true;
        }

    public:

        template <typename T> requires (str_like<T> && python_like<T>)
        explicit AttrProxy(const Object& obj, T&& key) :
            obj(obj), key(std::forward<T>(key))
        {}

        template <size_t N>
        explicit AttrProxy(const Object& obj, const char(&key)[N]) :
            obj(obj), key(reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(key, N - 1))
            )
        {
            if (this->key.ptr() == nullptr) {
                throw error_already_set();
            }
        }

        template <typename T> requires (std::is_convertible_v<T, const char*>)
        explicit AttrProxy(const Object& obj, const T& key) :
            obj(obj), key(reinterpret_steal<Object>(PyUnicode_FromString(key)))
        {
            if (this->key.ptr() == nullptr) {
                throw error_already_set();
            }
        }

        explicit AttrProxy(const Object& obj, const std::string& key) :
            obj(obj), key(reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(key.c_str(), key.size())
            ))
        {
            if (this->key.ptr() == nullptr) {
                throw error_already_set();
            }
        }

        explicit AttrProxy(const Object& obj, const std::string_view& key) :
            obj(obj), key(reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(key.data(), key.size())
            ))
        {
            if (this->key.ptr() == nullptr) {
                throw error_already_set();
            }
        }

        AttrProxy(const AttrProxy& other) :
            Base(other), obj(other.obj), key(other.key)
        {}

        AttrProxy(AttrProxy&& other) :
            Base(std::move(other)), obj(std::move(other.obj)),
            key(std::move(other.key))
        {}

        /* pybind11's attribute accessors only perform the lookup when the accessor is
         * converted to a value, which we hook to provide string type safety.  In this
         * case, the accessor is treated like a generic object, and will forward all
         * conversions to py::Object.  This allows us to write code like this:
         *
         *      py::Object obj = ...;
         *      py::Int i = obj.attr<"some_int">();  // runtime type check
         *
         * But not like this:
         *
         *      py::Str s = obj.attr<"some_int">();  // runtime error, some_int is not a string
         *
         * Unfortunately, it is not possible to promote these errors to compile time,
         * since Python attributes are inherently dynamic and can't be known in
         * advance.  This is the best we can do without creating a custom type and
         * strictly enforcing attribute types at the C++ level.  If this cannot be
         * done, then the only way to avoid extra runtime overhead is to use
         * reinterpret_steal to bypass the type check, which can be dangerous.
         *
         *      py::Int i = reinterpret_steal<py::Int>(obj.attr<"some_int">().release());
         */

        inline Obj& value() {
            if (!Base::initialized) {
                get_attr();
            }
            return reinterpret_cast<Obj&>(Base::buffer);
        }

        inline const Obj& value() const {
            if (!Base::initialized) {
                get_attr();
            }
            return reinterpret_cast<Obj&>(Base::buffer);
        }

        /* Similarly, assigning to a pybind11 wrapper corresponds to a Python
         * __setattr__ call.  Due to the same restrictions as above, we can't enforce
         * strict typing here, but we can at least make the syntax more consistent and
         * intuitive in mixed Python/C++ code.
         *
         *      py::Object obj = ...;
         *      obj.attr<"some_int">() = 5;  // valid: translates to Python.
         */

        template <typename T> requires (!proxy_like<std::decay_t<T>>)
        inline AttrProxy& operator=(T&& value) {
            new (Base::buffer) Obj(std::forward<T>(value));
            Base::initialized = true;
            if (PyObject_SetAttr(
                obj.ptr(),
                key.ptr(),
                reinterpret_cast<Obj&>(Base::buffer).ptr()
            ) < 0) {
                throw error_already_set();
            }
            return *this;
        }

        template <typename T> requires (proxy_like<std::decay_t<T>>)
        inline AttrProxy& operator=(T&& value) {
            operator=(value.value());
            return *this;
        }

        /* C++'s delete operator does not directly correspond to Python's `del`
         * statement, so we can't piggyback off it here.  Instead, we offer a separate
         * `.del()` method that behaves the same way.
         *
         *      py::Object obj = ...;
         *      obj.attr<"some_int">().del();  // Equivalent to Python `del obj.some_int`
         */

        inline void del() {
            if (PyObject_DelAttr(obj.ptr(), key.ptr()) < 0) {
                throw error_already_set();
            }
            if (Base::initialized) {
                reinterpret_cast<Obj&>(Base::buffer).~Obj();
                Base::initialized = false;
            }
        }

    };

    /* A generic policy for getting, setting, or deleting an item at a particular
    index of a Python container. */
    template <typename Obj, typename Key>
    struct ItemPolicy {
        Handle obj;
        Object key;

        ItemPolicy(Handle obj, const Key& key) : obj(obj), key(key) {}
        ItemPolicy(Handle obj, Key&& key) : obj(obj), key(std::move(key)) {}
        ItemPolicy(const ItemPolicy& other) : obj(other.obj), key(other.key) {}
        ItemPolicy(ItemPolicy&& other) : obj(other.obj), key(std::move(other.key)) {}

        inline PyObject* get() const {
            PyObject* result = PyObject_GetItem(obj.ptr(), key.ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }

        inline void set(PyObject* value) {
            int result = PyObject_SetItem(obj.ptr(), key.ptr(), value);
            if (result < 0) {
                throw error_already_set();
            }
        }

        inline void del() {
            int result = PyObject_DelItem(obj.ptr(), key.ptr());
            if (result < 0) {
                throw error_already_set();
            }
        }

    };

    /* A specialization of ItemPolicy that is specifically optimized for integer
    indices into Python tuple objects. */
    template <typename Obj, typename Key>
        requires (std::is_base_of_v<Tuple, Obj> && std::is_integral_v<Key>)
    struct ItemPolicy<Obj, Key> {
        Handle obj;
        Py_ssize_t key;

        ItemPolicy(Handle obj, Py_ssize_t key) : obj(obj), key(key) {}
        ItemPolicy(const ItemPolicy& other) : obj(other.obj), key(other.key) {}
        ItemPolicy(ItemPolicy&& other) : obj(other.obj), key(other.key) {}

        PyObject* get() const {
            Py_ssize_t size = PyTuple_GET_SIZE(obj.ptr());
            Py_ssize_t norm = key + size * (key < 0);
            if (norm < 0 || norm >= size) {
                throw IndexError("tuple index out of range");
            }
            PyObject* result = PyTuple_GET_ITEM(obj.ptr(), norm);
            if (result == nullptr) {
                throw error_already_set();
            }
            return Py_NewRef(result);
        }

    };

    /* A specialization of ItemPolicy that is specifically optimized for integer
    indices into Python list objects. */
    template <typename Obj, typename Key>
        requires (std::is_base_of_v<List, Obj> && std::is_integral_v<Key>)
    struct ItemPolicy<Obj, Key> {
        Handle obj;
        Py_ssize_t key;

        ItemPolicy(Handle obj, Py_ssize_t key) : obj(obj), key(key) {}
        ItemPolicy(const ItemPolicy& other) : obj(other.obj), key(other.key) {}
        ItemPolicy(ItemPolicy&& other) : obj(other.obj), key(other.key) {}

        inline Py_ssize_t normalize(Py_ssize_t index) const {
            Py_ssize_t size = PyList_GET_SIZE(obj.ptr());
            Py_ssize_t result = index + size * (index < 0);
            if (result < 0 || result >= size) {
                throw IndexError("list index out of range");
            }
            return result;
        }

        inline PyObject* get() const {
            PyObject* result = PyList_GET_ITEM(obj.ptr(), normalize(key));
            if (result == nullptr) {
                throw error_already_set();
            }
            return Py_NewRef(result);
        }

        inline void set(PyObject* value) {
            Py_ssize_t normalized = normalize(key);
            PyObject* previous = PyList_GET_ITEM(obj.ptr(), normalized);
            PyList_SET_ITEM(obj.ptr(), normalized, Py_NewRef(value));
            Py_XDECREF(previous);
        }

        inline void del() {
            PyObject* index_obj = PyLong_FromSsize_t(normalize(key));
            if (PyObject_DelItem(obj.ptr(), index_obj) < 0) {
                throw error_already_set();
            }
            Py_DECREF(index_obj);
        }

    };

    /* A subclass of Proxy that replaces the result of pybind11's array index (`[]`)
    operator.  This uses the __getitem__, __setitem__, and __delitem__ control structs
    to selectively enable/disable these operations for particular types, and to assign
    a corresponding return type to which the proxy can be converted. */
    template <typename Obj, typename Key> requires (__getitem__<Obj, Key>::enable)
    class Item :
        public Proxy<typename __getitem__<Obj, Key>::Return, Item<Obj, Key>>
    {
    public:
        using Wrapped = typename __getitem__<Obj, Key>::Return;
        static_assert(
            std::is_base_of_v<Object, Wrapped>,
            "index operator must return a subclass of py::Object.  Check your "
            "specialization of __getitem__ for these types and ensure the Return "
            "type is set to a subclass of py::Object."
        );

    private:
        using Base = Proxy<Wrapped, Item>;
        ItemPolicy<Obj, Key> policy;

    public:

        template <typename... Args>
        explicit Item(Args&&... args) : policy(std::forward<Args>(args)...) {}
        Item(const Item& other) : Base(other), policy(other.policy) {}
        Item(Item&& other) : Base(std::move(other)), policy(std::move(other.policy)) {}

        /* pybind11's item accessors only perform the lookup when the accessor is
         * converted to a value, which we can hook to provide strong type safety.  In
         * this case, the accessor is only convertible to the return type specified by
         * __getitem__, and forwards all other conversions to that type specifically.
         * This allows us to write code like this:
         *
         *      template <>
         *      struct impl::__getitem__<List, Slice> : impl::Returns<List> {};
         *
         *      py::List list = {1, 2, 3, 4};
         *      py::List slice = list[{1, 3}];
         *
         *      void foo(const std::vector<int>& vec) {}
         *      foo(list[{1, 3}]);  // List is implicitly convertible to vector
         *
         * But not like this:
         *
         *      py::Int item = list[{1, 3}];  // compile error, List is not convertible to Int
         */

        inline Wrapped& value() {
            if (!Base::initialized) {
                new (Base::buffer) Wrapped(reinterpret_steal<Wrapped>(policy.get()));
                Base::initialized = true;
            }
            return reinterpret_cast<Wrapped&>(Base::buffer);
        }

        inline const Wrapped& value() const {
            if (!Base::initialized) {
                new (Base::buffer) Wrapped(reinterpret_steal<Wrapped>(policy.get()));
                Base::initialized = true;
            }
            return reinterpret_cast<Wrapped&>(Base::buffer);
        }

        /* Similarly, assigning to a pybind11 wrapper corresponds to a Python
         * __setitem__ call, and we can carry strong type safety here as well.  By
         * specializing __setitem__ for the accessor's key type, we can constrain the
         * types that can be assigned to the container, allowing us to enforce
         * compile-time type safety.  We can thus write code like this:
         *
         *      template <impl::list_like Value>
         *      struct impl::__setitem__<List, Slice, Value> : impl::Returns<void> {};
         *
         *      py::List list = {1, 2, 3, 4};
         *      list[{1, 3}] = py::List{5, 6};
         *      list[{1, 3}] = std::vector<int>{7, 8};
         *
         * But not like this:
         *
         *      list[{1, 3}] = 5;  // compile error, int is not list-like
         */

        template <typename T> requires (__setitem__<Obj, Key, T>::enable && !proxy_like<T>)
        inline Item& operator=(T&& value) {
            static_assert(
                std::is_void_v<typename __setitem__<Obj, Key, T>::Return>,
                "index assignment operator must return void.  Check your "
                "specialization of __setitem__ for these types and ensure the Return "
                "type is set to void."
            );
            new (Base::buffer) Wrapped(std::forward<T>(value));
            Base::initialized = true;
            policy.set(reinterpret_cast<Wrapped&>(Base::buffer).ptr());
            return *this;
        }

        template <typename T> requires (proxy_like<T>)
        inline Item& operator=(T&& value) {
            operator=(value.value());
            return *this;
        }

        /* C++'s delete operator does not directly correspond to Python's `del`
         * statement, so we can't piggyback off it here.  Instead, we offer a separate
         * `.del()` method that behaves the same way and is only enabled if the
         * __delitem__ struct is specialized for the accessor's key type.
         *
         *      template <impl::int_like T>
         *      struct impl::__delitem__<List, T> : impl::Returns<void> {};
         *      template <>
         *      struct impl::__delitem__<List, Slice> : impl::Returns<void> {};
         *
         *      py::List list = {1, 2, 3, 4};
         *      list1[0].del();  // valid, single items can be deleted
         *      list[{0, 2}].del();  // valid, slices can be deleted
         *
         * If __delitem__ is not specialized for a given key type, the `.del()` method
         * will result in a compile error, giving full control over the types that can
         * be deleted from the container.
         */

        template <typename T = Key> requires (__delitem__<Obj, T>::enable)
        inline void del() {
            static_assert(
                std::is_void_v<typename __delitem__<Obj, T>::Return>,
                "index deletion operator must return void.  Check your specialization "
                "of __delitem__ for these types and ensure the Return type is set to "
                "void."
            );
            policy.del();
            if (Base::initialized) {
                reinterpret_cast<Wrapped&>(Base::buffer).~Wrapped();
                Base::initialized = false;
            }
        }

    };

    /* An optimized iterator that directly accesses tuple or list elements through the
    CPython API. */
    template <typename Policy>
    class Iterator {
        static_assert(
            std::is_base_of_v<Object, typename Policy::value_type>,
            "Iterator must dereference to a subclass of py::Object.  Check your "
            "specialization of __iter__ for this type and ensure the Return type is "
            "derived from py::Object."
        );

    protected:
        Policy policy;

        static constexpr bool random_access = std::is_same_v<
            typename Policy::iterator_category,
            std::random_access_iterator_tag
        >;
        static constexpr bool bidirectional = random_access || std::is_same_v<
            typename Policy::iterator_category,
            std::bidirectional_iterator_tag
        >;

    public:
        using iterator_category        = Policy::iterator_category;
        using difference_type          = Policy::difference_type;
        using value_type               = Policy::value_type;
        using pointer                  = Policy::pointer;
        using reference                = Policy::reference;

        /* Default constructor.  Initializes to a sentinel iterator. */
        template <typename... Args>
        Iterator(Args&&... args) : policy(std::forward<Args>(args)...) {}

        /* Copy constructor. */
        Iterator(const Iterator& other) : policy(other.policy) {}

        /* Move constructor. */
        Iterator(Iterator&& other) : policy(std::move(other.policy)) {}

        /* Copy assignment operator. */
        Iterator& operator=(const Iterator& other) {
            policy = other.policy;
            return *this;
        }

        /* Move assignment operator. */
        Iterator& operator=(Iterator&& other) {
            policy = std::move(other.policy);
            return *this;
        }

        /////////////////////////////////
        ////    ITERATOR PROTOCOL    ////
        /////////////////////////////////

        /* Dereference the iterator. */
        inline value_type operator*() const {
            return policy.deref();
        }

        /* Dereference the iterator. */
        inline pointer operator->() const {
            return &(**this);
        }

        /* Advance the iterator. */
        inline Iterator& operator++() {
            policy.advance();
            return *this;
        }

        /* Advance the iterator. */
        inline Iterator operator++(int) {
            Iterator copy = *this;
            policy.advance();
            return copy;
        }

        /* Compare two iterators for equality. */
        inline bool operator==(const Iterator& other) const {
            return policy.compare(other.policy);
        }

        /* Compare two iterators for inequality. */
        inline bool operator!=(const Iterator& other) const {
            return !policy.compare(other.policy);
        }

        ///////////////////////////////////////
        ////    BIDIRECTIONAL ITERATORS    ////
        ///////////////////////////////////////

        /* Retreat the iterator. */
        template <typename T = Iterator> requires (bidirectional)
        inline Iterator& operator--() {
            policy.retreat();
            return *this;
        }

        /* Retreat the iterator. */
        template <typename T = Iterator> requires (bidirectional)
        inline Iterator operator--(int) {
            Iterator copy = *this;
            policy.retreat();
            return copy;
        }

        ///////////////////////////////////////
        ////    RANDOM ACCESS ITERATORS    ////
        ///////////////////////////////////////

        /* Advance the iterator by n steps. */
        template <typename T = Iterator> requires (random_access)
        inline Iterator operator+(difference_type n) const {
            Iterator copy = *this;
            copy += n;
            return copy;
        }

        /* Advance the iterator by n steps. */
        template <typename T = Iterator> requires (random_access)
        inline Iterator& operator+=(difference_type n) {
            policy.advance(n);
            return *this;
        }

        /* Retreat the iterator by n steps. */
        template <typename T = Iterator> requires (random_access)
        inline Iterator operator-(difference_type n) const {
            Iterator copy = *this;
            copy -= n;
            return copy;
        }

        /* Retreat the iterator by n steps. */
        template <typename T = Iterator> requires (random_access)
        inline Iterator& operator-=(difference_type n) {
            policy.retreat(n);
            return *this;
        }

        /* Calculate the distance between two iterators. */
        template <typename T = Iterator> requires (random_access)
        inline difference_type operator-(const Iterator& other) const {
            return policy.distance(other.policy);
        }

        /* Access the iterator at an offset. */
        template <typename T = Iterator> requires (random_access)
        inline value_type operator[](difference_type n) const {
            return *(*this + n);
        }

        /* Compare two iterators for ordering. */
        template <typename T = Iterator> requires (random_access)
        inline bool operator<(const Iterator& other) const {
            return !!policy && (*this - other) < 0;
        }

        /* Compare two iterators for ordering. */
        template <typename T = Iterator> requires (random_access)
        inline bool operator<=(const Iterator& other) const {
            return !!policy && (*this - other) <= 0;
        }

        /* Compare two iterators for ordering. */
        template <typename T = Iterator> requires (random_access)
        inline bool operator>=(const Iterator& other) const {
            return !policy || (*this - other) >= 0;
        }

        /* Compare two iterators for ordering. */
        template <typename T = Iterator> requires (random_access)
        inline bool operator>(const Iterator& other) const {
            return !policy || (*this - other) > 0;
        }

    };

    template <typename Policy>
    class ReverseIterator : public Iterator<Policy> {
        using Base = Iterator<Policy>;
        static_assert(
            Base::bidirectional,
            "ReverseIterator can only be used with bidirectional iterators."
        );

    public:
        using Base::Base;

        /* Advance the iterator. */
        inline ReverseIterator& operator++() {
            Base::operator--();
            return *this;
        }

        /* Advance the iterator. */
        inline ReverseIterator operator++(int) {
            ReverseIterator copy = *this;
            Base::operator--();
            return copy;
        }

        /* Retreat the iterator. */
        inline ReverseIterator& operator--() {
            Base::operator++();
            return *this;
        }

        /* Retreat the iterator. */
        inline ReverseIterator operator--(int) {
            ReverseIterator copy = *this;
            Base::operator++();
            return copy;
        }

        ////////////////////////////////////////
        ////    RANDOM ACCESS ITERATORS     ////
        ////////////////////////////////////////

        /* Advance the iterator by n steps. */
        template <typename T = ReverseIterator> requires (Base::random_access)
        inline ReverseIterator operator+(typename Base::difference_type n) const {
            ReverseIterator copy = *this;
            copy -= n;
            return copy;
        }

        /* Advance the iterator by n steps. */
        template <typename T = ReverseIterator> requires (Base::random_access)
        inline ReverseIterator& operator+=(typename Base::difference_type n) {
            Base::operator-=(n);
            return *this;
        }

        /* Retreat the iterator by n steps. */
        template <typename T = ReverseIterator> requires (Base::random_access)
        inline ReverseIterator operator-(typename Base::difference_type n) const {
            ReverseIterator copy = *this;
            copy += n;
            return copy;
        }

        /* Retreat the iterator by n steps. */
        template <typename T = ReverseIterator> requires (Base::random_access)
        inline ReverseIterator& operator-=(typename Base::difference_type n) {
            Base::operator+=(n);
            return *this;
        }

    };

    /* A generic iterator policy that uses Python's existing iterator protocol. */
    template <typename Deref>
    class GenericIter {
        Object iter;
        PyObject* curr;

    public:
        using iterator_category         = std::input_iterator_tag;
        using difference_type           = std::ptrdiff_t;
        using value_type                = Deref;
        using pointer                   = Deref*;
        using reference                 = Deref&;

        /* Default constructor.  Initializes to a sentinel iterator. */
        GenericIter() :
            iter(reinterpret_steal<Object>(nullptr)), curr(nullptr)
        {}

        /* Wrap a raw Python iterator. */
        GenericIter(Object&& iterator) : iter(std::move(iterator)) {
            curr = PyIter_Next(iter.ptr());
            if (curr == nullptr &&PyErr_Occurred()) {
                throw error_already_set();
            }
        }

        /* Copy constructor. */
        GenericIter(const GenericIter& other) : iter(other.iter), curr(other.curr) {
            Py_XINCREF(curr);
        }

        /* Move constructor. */
        GenericIter(GenericIter&& other) : iter(std::move(other.iter)), curr(other.curr) {
            other.curr = nullptr;
        }

        /* Copy assignment operator. */
        GenericIter& operator=(const GenericIter& other) {
            if (&other != this) {
                iter = other.iter;
                PyObject* temp = curr;
                Py_XINCREF(curr);
                curr = other.curr;
                Py_XDECREF(temp);
            }
            return *this;
        }

        /* Move assignment operator. */
        GenericIter& operator=(GenericIter&& other) {
            if (&other != this) {
                iter = std::move(other.iter);
                PyObject* temp = curr;
                curr = other.curr;
                other.curr = nullptr;
                Py_XDECREF(temp);
            }
            return *this;
        }

        ~GenericIter() {
            Py_XDECREF(curr);
        }

        /* Dereference the iterator. */
        inline Deref deref() const {
            if (curr == nullptr) {
                throw ValueError("attempt to dereference a null iterator.");
            }
            return reinterpret_borrow<Deref>(curr);
        }

        /* Advance the iterator. */
        inline void advance() {
            PyObject* temp = curr;
            curr = PyIter_Next(iter.ptr());
            Py_XDECREF(temp);
            if (curr == nullptr && PyErr_Occurred()) {
                throw error_already_set();
            }
        }

        /* Compare two iterators for equality. */
        inline bool compare(const GenericIter& other) const {
            return curr == other.curr;
        }

        inline explicit operator bool() const {
            return curr != nullptr;
        }

    };

    /* A random access iterator policy that directly addresses tuple elements using the
    CPython API. */
    template <typename Deref>
    class TupleIter {
        Object tuple;
        PyObject* curr;
        Py_ssize_t index;

    public:
        using iterator_category         = std::random_access_iterator_tag;
        using difference_type           = std::ptrdiff_t;
        using value_type                = Deref;
        using pointer                   = Deref*;
        using reference                 = Deref&;

        /* Sentinel constructor. */
        TupleIter(Py_ssize_t index) :
            tuple(reinterpret_steal<Object>(nullptr)), curr(nullptr), index(index)
        {}

        /* Construct an iterator from a tuple and a starting index. */
        TupleIter(const Object& tuple, Py_ssize_t index) :
            tuple(tuple), index(index)
        {
            if (index >= 0 && index < PyTuple_GET_SIZE(tuple.ptr())) {
                curr = PyTuple_GET_ITEM(tuple.ptr(), index);
            } else {
                curr = nullptr;
            }
        }

        /* Copy constructor. */
        TupleIter(const TupleIter& other) :
            tuple(other.tuple), curr(other.curr), index(other.index)
        {}

        /* Move constructor. */
        TupleIter(TupleIter&& other) :
            tuple(std::move(other.tuple)), curr(other.curr), index(other.index)
        {
            other.curr = nullptr;
        }

        /* Copy assignment operator. */
        TupleIter& operator=(const TupleIter& other) {
            if (&other != this) {
                tuple = other.tuple;
                curr = other.curr;
                index = other.index;
            }
            return *this;
        }

        /* Move assignment operator. */
        TupleIter& operator=(TupleIter&& other) {
            if (&other != this) {
                tuple = other.tuple;
                curr = other.curr;
                index = other.index;
                other.curr = nullptr;
            }
            return *this;
        }

        /* Dereference the iterator. */
        inline Deref deref() const {
            if (curr == nullptr) {
                throw ValueError("attempt to dereference a null iterator.");
            }
            return reinterpret_borrow<Deref>(curr);
        }

        /* Advance the iterator. */
        inline void advance(Py_ssize_t n = 1) {
            index += n;
            if (index >= 0 && index < PyTuple_GET_SIZE(tuple.ptr())) {
                curr = PyTuple_GET_ITEM(tuple.ptr(), index);
            } else {
                curr = nullptr;
            }
        }

        /* Compare two iterators for equality. */
        inline bool compare(const TupleIter& other) const {
            return curr == other.curr;
        }

        /* Retreat the iterator. */
        inline void retreat(Py_ssize_t n = 1) {
            index -= n;
            if (index >= 0 && index < PyTuple_GET_SIZE(tuple.ptr())) {
                curr = PyTuple_GET_ITEM(tuple.ptr(), index);
            } else {
                curr = nullptr;
            }
        }

        /* Calculate the distance between two iterators. */
        inline difference_type distance(const TupleIter& other) const {
            return index - other.index;
        }

        inline explicit operator bool() const {
            return curr != nullptr;
        }

    };

    /* A random access iterator policy that directly addresses list elements using the
    CPython API. */
    template <typename Deref>
    class ListIter {
        Object list;
        PyObject* curr;
        Py_ssize_t index;

    public:
        using iterator_category         = std::random_access_iterator_tag;
        using difference_type           = std::ptrdiff_t;
        using value_type                = Deref;
        using pointer                   = Deref*;
        using reference                 = Deref&;

        /* Default constructor.  Initializes to a sentinel iterator. */
        ListIter(Py_ssize_t index) :
            list(reinterpret_steal<Object>(nullptr)), curr(nullptr), index(index)
        {}

        /* Construct an iterator from a list and a starting index. */
        ListIter(const Object& list, Py_ssize_t index) :
            list(list), index(index)
        {
            if (index >= 0 && index < PyList_GET_SIZE(list.ptr())) {
                curr = PyList_GET_ITEM(list.ptr(), index);
            } else {
                curr = nullptr;
            }
        }

        /* Copy constructor. */
        ListIter(const ListIter& other) :
            list(other.list), curr(other.curr), index(other.index)
        {}

        /* Move constructor. */
        ListIter(ListIter&& other) :
            list(std::move(other.list)), curr(other.curr), index(other.index)
        {
            other.curr = nullptr;
        }

        /* Copy assignment operator. */
        ListIter& operator=(const ListIter& other) {
            if (&other != this) {
                list = other.list;
                curr = other.curr;
                index = other.index;
            }
            return *this;
        }

        /* Move assignment operator. */
        ListIter& operator=(ListIter&& other) {
            if (&other != this) {
                list = other.list;
                curr = other.curr;
                index = other.index;
                other.curr = nullptr;
            }
            return *this;
        }

        /* Dereference the iterator. */
        inline Deref deref() const {
            if (curr == nullptr) {
                throw IndexError("list index out of range");
            }
            return reinterpret_borrow<Deref>(curr);
        }

        /* Advance the iterator. */
        inline void advance(Py_ssize_t n = 1) {
            index += n;
            if (index >= 0 && index < PyList_GET_SIZE(list.ptr())) {
                curr = PyList_GET_ITEM(list.ptr(), index);
            } else {
                curr = nullptr;
            }
        }

        /* Compare two iterators for equality. */
        inline bool compare(const ListIter& other) const {
            return curr == other.curr;
        }

        /* Retreat the iterator. */
        inline void retreat(Py_ssize_t n = 1) {
            index -= n;
            if (index >= 0 && index < PyList_GET_SIZE(list.ptr())) {
                curr = PyList_GET_ITEM(list.ptr(), index);
            } else {
                curr = nullptr;
            }
        }

        /* Calculate the distance between two iterators. */
        inline difference_type distance(const ListIter& other) const {
            return index - other.index;
        }

        inline explicit operator bool() const {
            return curr != nullptr;
        }

    };

    // TODO: KeyIter, ValueIter, ItemIter using PyDict_Next

}  // namespace impl



// TODO: eliminate Static proxy and insert Py_IsInitialized check directly into
// subclass destructors where necessary.  This allows any object to be stored with
// static duration at a small performance penalty (which you only pay if you use)




/* A Proxy policy that allows any Python object to be stored with static duration.

Normally, storing a static Python object is unsafe because it causes the Python
interpreter to be in an invalid state at the time the object's destructor is
called, triggering a memory access violation during shutdown.  This class avoids
that issue by checking `Py_IsInitialized()` and only invoking the destructor if it
evaluates to true.  This technically means that we leave an unbalanced reference to
the object, but since the Python interpreter is shutting down anyway, it doesn't
actually matter.  Python will clean up the object regardless of its reference count.

Note that storing objects that require explicit cleanup - such as open file handles
or remote connections - is still unsafe, as the object's destructor will not be
called at shutdown. */
template <typename T>
class Static : public impl::Proxy<T, Static<T>> {
    using Base = impl::Proxy<T, Static<T>>;

public:
    using Base::Base;

    /* Default constructor. */
    Static() : Base(T()) {}

    /* Destructor only called if Py_IsInitialized() evalutes to true. */
    ~Static() {
        Base::initialized &= Py_IsInitialized();
    }

};


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

};


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
            throw error_already_set();
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
            throw error_already_set();
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

    /* Mixin holding operator overloads for types implementing the sequence protocol,
    which makes them both concatenatable and repeatable. */
    template <typename Derived>
    class SequenceOps {

        inline Derived& self() { return static_cast<Derived&>(*this); }
        inline const Derived& self() const { return static_cast<const Derived&>(*this); }

    public:
        /* Equivalent to Python `sequence.count(value)`, but also takes optional
        start/stop indices similar to `sequence.index()`. */
        template <typename T>
        inline Py_ssize_t count(
            const T& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(self().ptr(), start, stop);
                if (slice == nullptr) {
                    throw error_already_set();
                }
                Py_ssize_t result = PySequence_Count(
                    slice,
                    detail::object_or_cast(value).ptr()
                );
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Count(
                    self().ptr(),
                    detail::object_or_cast(value).ptr()
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
            const T& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(self().ptr(), start, stop);
                if (slice == nullptr) {
                    throw error_already_set();
                }
                Py_ssize_t result = PySequence_Index(
                    slice,
                    detail::object_or_cast(value).ptr()
                );
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Index(
                    self().ptr(),
                    detail::object_or_cast(value).ptr()
                );
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            }
        }

    protected:

        template <typename Return, typename L, typename R>
        inline static auto operator_add(const L& lhs, const R& rhs) {
            PyObject* result = PySequence_Concat(
                detail::object_or_cast(lhs).ptr(),
                detail::object_or_cast(rhs).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Return>(result);
        }

        template <typename Return, typename L, typename R>
        inline static void operator_iadd(L& lhs, const R& rhs) {
            PyObject* result = PySequence_InPlaceConcat(
                lhs.ptr(),
                detail::object_or_cast(rhs).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }

        template <typename Return, typename L>
        inline static auto operator_mul(const L& lhs, Py_ssize_t repetitions) {
            PyObject* result = PySequence_Repeat(
                detail::object_or_cast(lhs).ptr(),
                repetitions
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Return>(result);
        }

        template <typename Return, typename L>
        inline static void operator_imul(L& lhs, Py_ssize_t repetitions) {
            PyObject* result = PySequence_InPlaceRepeat(
                lhs.ptr(),
                repetitions
            );
            if (result == nullptr) {
                throw error_already_set();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }

    };

    template <>
    struct __lt__<Slice, Object> : Returns<bool> {};
    template <slice_like T>
    struct __lt__<Slice, T> : Returns<bool> {};

    template <>
    struct __le__<Slice, Object> : Returns<bool> {};
    template <slice_like T>
    struct __le__<Slice, T> : Returns<bool> {};

    template <>
    struct __ge__<Slice, Object> : Returns<bool> {};
    template <slice_like T>
    struct __ge__<Slice, T> : Returns<bool> {};

    template <>
    struct __gt__<Slice, Object> : Returns<bool> {};
    template <slice_like T>
    struct __gt__<Slice, T> : Returns<bool> {};

}  // namespace impl


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
            throw error_already_set();
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
            throw error_already_set();
        }
    }

    /* Explicitly construct a slice from a (possibly denormalized) stop object. */
    template <typename Stop>
    explicit Slice(const Stop& stop) {
        m_ptr = PySlice_New(nullptr, detail::object_or_cast(stop).ptr(), nullptr);
        if (m_ptr == nullptr) {
            throw error_already_set();
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
            throw error_already_set();
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
            throw error_already_set();
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
            throw error_already_set();
        }
        return {start, stop, step, length};
    }

};


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
        if (m_ptr == nullptr) {
            if (PyErr_Occurred()) {
                throw error_already_set();
            }
            pybind11::pybind11_fail(
                "Internal error in pybind11::module_::create_extension_module()"
            );
        }
    }

    /* Destructor allows Code objects to be stored with static duration. */
    ~Module() {
        if (!Py_IsInitialized()) {
            m_ptr = nullptr;  // avoid calling XDECREF if Python is shutting down
        }
    }

    //////////////////////////////////
    ////    PYBIND11 INTERFACE    ////
    //////////////////////////////////

    /* Equivalent to pybind11::module_::def(). */
    template <typename Func, typename... Extra>
    Module& def(const char* name_, Func&& f, const Extra&... extra) {
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
    }

    /* Equivalent to pybind11::module_::def_submodule(). */
    Module def_submodule(const char* name, const char* doc = nullptr) {
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

    /* Reload the module or throws `error_already_set`. */
    inline void reload() {
        PyObject *obj = PyImport_ReloadModule(this->ptr());
        if (obj == nullptr) {
            throw error_already_set();
        }
        *this = reinterpret_steal<Module>(obj);
    }

    /* Equivalent to pybind11::module_::add_object(). */
    PYBIND11_NOINLINE void add_object(
        const char* name,
        Handle obj,
        bool overwrite = false
    ) {
        if (!overwrite && pybind11::hasattr(*this, name)) {
            pybind11::pybind11_fail(
                "Error during initialization: multiple incompatible definitions with name \""
                + std::string(name) + "\"");
        }
        PyModule_AddObjectRef(ptr(), name, obj.ptr());
    }

};


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <bertrand::StaticStr key>
inline impl::AttrProxy<Object> Object::attr() const {
    static const pybind11::str lookup = static_cast<std::string>(key);
    return impl::AttrProxy<Object>(*this, lookup);
}


template <typename Return, typename T, typename Key>
inline auto Object::operator_getitem(const T& obj, Key&& key)
    -> impl::Item<T, std::decay_t<Key>>
{
    return impl::Item<T, std::decay_t<Key>>(obj, std::forward<Key>(key));
}


template <typename Return, typename T>
inline auto Object::operator_getitem(
    const T& obj,
    std::initializer_list<impl::SliceInitializer> slice
) -> impl::Item<T, Slice>
{
    if (slice.size() > 3) {
        throw ValueError("slices must be of the form {[start[, stop[, step]]]}");
    }
    std::array<Object, 3> params {None, None, None};
    size_t i = 0;
    for (const impl::SliceInitializer& item : slice) {
        params[i++] = item.first;
    }
    return impl::Item<T, Slice>(obj, Slice(params[0], params[1], params[2]));
}


template <typename Return, typename T>
inline auto Object::operator_begin(const T& obj)
    -> impl::Iterator<impl::GenericIter<Return>>
{
    PyObject* iter = PyObject_GetIter(obj.ptr());
    if (iter == nullptr) {
        throw error_already_set();
    }
    return {reinterpret_steal<Object>(iter)};
}


template <typename Return, typename T>
inline auto Object::operator_end(const T& obj)
    -> impl::Iterator<impl::GenericIter<Return>>
{
    return {};
}


template <typename Return, typename T>
inline auto Object::operator_rbegin(const T& obj)
    -> impl::Iterator<impl::GenericIter<Return>>
{
    return {obj.template attr<"__reversed__">()()};
}


template <typename Return, typename T>
inline auto Object::operator_rend(const T& obj)
    -> impl::Iterator<impl::GenericIter<Return>>
{
    return {};
}


template <typename Obj, typename Wrapped>
template <typename T> requires (impl::__getitem__<T, Slice>::enable)
inline auto impl::Proxy<Obj, Wrapped>::operator[](
    std::initializer_list<impl::SliceInitializer> slice
) const {
    return deref()[slice];
}


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


using pybind11::print;


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <typename T> requires (impl::python_like<T> && impl::__abs__<T>::enable)
inline auto abs(const T& value) {
    using Return = impl::__abs__<T>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Absolute value operator must return a py::Object subclass.  Check your "
        "specialization of __abs__ for this type and ensure the Return type is set to "
        "a py::Object subclass."
    );
    PyObject* result = PyNumber_Absolute(value.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Return>(result);
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <typename T> requires (!impl::python_like<T>)
inline auto abs(const T& value) {
    return std::abs(value);
}


/* Equivalent to Python `import module`.  Only recognizes absolute imports. */
template <StaticStr name>
inline Module import() {
    static const pybind11::str lookup = static_cast<const char*>(name);
    PyObject *obj = PyImport_Import(lookup.ptr());
    if (obj == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Module>(obj);
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

template <bertrand::py::impl::proxy_like T>
struct type_caster<T> {
    PYBIND11_TYPE_CASTER(T, const_name("proxy"));

    /* Convert Python object to a C++ Proxy. */
    inline bool load(handle src, bool convert) {
        return false;
    }

    /* Convert a C++ Proxy into its wrapped object. */
    inline static handle cast(const T& src, return_value_policy policy, handle parent) {
        return src.value();
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

    template <typename T> requires (bertrand::py::impl::__hash__<T>::enable)
    struct hash<T> {
        static_assert(
            std::is_same_v<typename bertrand::py::impl::__hash__<T>::Return, size_t>,
            "std::hash<> must return size_t for compatibility with other C++ types.  "
            "Check your specialization of __hash__ for this type and ensure the "
            "Return type is set to size_t."
        );

        inline size_t operator()(const T& obj) const {
            return pybind11::hash(obj);
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
