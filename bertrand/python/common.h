#include <cstddef>
#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_H

#include <algorithm>
#include <chrono>
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


/* NOTES ON PERFORMANCE:
 * In general, bertrand should be quite efficient, and generally trade blows with
 * native Python in most respects.  It expands out to raw CPython API calls, so
 * properly optimized (i.e. type safe) code should retain as much performance as
 * possible, and may even gain some due to specific optimizations at the C++ level.
 * There are, however, a few things to keep in mind:
 *
 *  1.  A null pointer check followed by a type check is implicitly incurred whenever a
 *      generalized py::Object is narrowed to a more specific type, such as py::Int or
 *      py::List.  This is necessary to ensure type safety, and is optimized for
 *      built-in types, but can become a pessimization if done frequently, especially
 *      in tight loops.  If you find yourself doing this, consider either converting to
 *      strict types earlier in the code, which will allow the compiler to enforce
 *      these rules at compile time, or keeping all object interactions generic to
 *      prevent thrashing.  Generally, the most common case where this can be a problem
 *      is when assigning the result of a generic attribute lookup (`.attr()`), index
 *      (`[]`), or call (`()`) operator to a strict type, since these operators return
 *      py::Object instances by default.  If you naively bind these to a strict type
 *      (e.g. `py::Int x = py::List{1, 2, 3}[1]` or `py::Str y = x.attr("__doc__")`),
 *      then the runtime check will be implicitly incurred to make the operation type
 *      safe.  See point #2 below for a workaround.  In the future, bertrand will
 *      likely offer typed alternatives for the basic containers, which can promote
 *      some of these checks to compile time, but they can never be completely
 *      eliminated.
 *  2.  For cases where the type of a generic object is known in advance, it is
 *      possible to bypass the runtime check by using `py::reinterpret_borrow<T>(obj)`
 *      or `py::reinterpret_steal<T>(obj.release())`.  These functions are not type
 *      safe, and should be used with caution (especially the latter, which can lead to
 *      memory leaks if used incorrectly).  However, they can be useful when working
 *      with custom types, as in most cases a method's return type and reference count
 *      will be known ahead of time, making the runtime check redundant.  In most other
 *      cases, it is not recommended to use these functions, as they can lead to subtle
 *      bugs and crashes if their assumptions are incorrect.
 *  3.  There is a penalty for copying data across the Python/C++ boundary.  This is
 *      generally quite small (even for lists and other container types), but it can
 *      add up if done frequently.  If you find yourself repeatedly copying large
 *      amounts of data between Python and C++, you should reconsider your design or
 *      use the buffer protocol to avoid the copy.  This is especially true for NumPy
 *      arrays, which can be accessed directly as C++ arrays without copying.
 *  4.  Python (at least for now) does not play well with multithreaded code, and
 *      neither does bertrand.  If you need to use Python in a multithreaded context,
 *      consider offloading the work to C++ and passing the results back to Python.
 *      This unlocks full native parallelism, with SIMD, OpenMP, and other tools at
 *      your disposal.  If you must use Python, consider using the
 *      `py::gil_scoped_release` guard to release the GIL while doing C++ work, and
 *      then reacquire it via RAII before returning to Python.
 *  5.  Lastly, Bertrand makes it possible to store arbitrary Python objects with
 *      static duration using the py::Static<> wrapper, which can reduce net
 *      allocations and improve performance.  This is especially true for global
 *      objects like modules and scripts, which can be cached and reused across the
 *      lifetime of the program.
 */


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
using pybind11::make_tuple;  // TODO: unnecessary
using pybind11::make_iterator;  // TODO: roll into Iterator() constructor
using pybind11::make_key_iterator;  // offer as static method in Iterator:: namespace
using pybind11::make_value_iterator;  // same as above
using pybind11::initialize_interpreter;
using pybind11::scoped_interpreter;
// PYBIND11_MODULE                      <- macros don't respect namespaces
// PYBIND11_EMBEDDED_MODULE
// PYBIND11_OVERRIDE
// PYBIND11_OVERRIDE_PURE
// PYBIND11_OVERRIDE_NAME
// PYBIND11_OVERRIDE_PURE_NAME
using pybind11::get_override;
using pybind11::cpp_function;  // TODO: unnecessary, use py::Function() instead
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
using Iterator = pybind11::iterator;
using WeakRef = pybind11::weakref;
using Capsule = pybind11::capsule;
using Buffer = pybind11::buffer;  // TODO: place in buffer.h along with memoryview
using MemoryView = pybind11::memoryview;
using Bytes = pybind11::bytes;  // TODO: place in str.h with bytearray.  They use an API mixin
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

// TODO: Regex should be placed in bertrand:: namespace.  It doesn't actually wrap a
// python object, so it shouldn't be in the py:: namespace

class Regex;  // TODO: incorporate more fully (write pybind11 bindings so that it can be passed into Python scripts)


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

    namespace concepts {

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
        concept python_like = detail::is_pyobject<T>::value;

        template <typename T>
        concept proxy_like = std::is_base_of_v<ProxyTag, T>;

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
        concept bool_like = (
            std::is_same_v<bool, T> ||
            std::is_base_of_v<py::Bool, T> ||
            std::is_base_of_v<pybind11::bool_, T>
        );

        template <typename T>
        concept int_like = (
            (std::is_integral_v<T> && !std::is_same_v<T, bool>) ||
            std::is_base_of_v<Int, T> ||
            std::is_base_of_v<pybind11::int_, T>
        );

        template <typename T>
        concept float_like = (
            std::is_floating_point_v<T> ||
            std::is_base_of_v<Float, T> ||
            std::is_base_of_v<pybind11::float_, T>
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
            std::is_constructible_v<std::string, T> ||
            std::is_constructible_v<std::string_view, T> ||
            std::is_base_of_v<Str, T> ||
            std::is_base_of_v<pybind11::str, T>
        );

        template <typename T>
        concept timedelta_like = (
            categories::Traits<T>::timedeltalike ||
            std::is_base_of_v<Timedelta, T>
        );

        template <typename T>
        concept timezone_like = (
            categories::Traits<T>::timezonelike ||
            std::is_base_of_v<Timezone, T>
        );

        template <typename T>
        concept date_like = (
            categories::Traits<T>::datelike ||
            std::is_base_of_v<Date, T>
        );

        template <typename T>
        concept time_like = (
            categories::Traits<T>::timelike ||
            std::is_base_of_v<Time, T>
        );

        template <typename T>
        concept datetime_like = (
            categories::Traits<T>::datetimelike ||
            std::is_base_of_v<Datetime, T>
        );

        template <typename T>
        concept slice_like = (
            std::is_base_of_v<Slice, T> ||
            std::is_base_of_v<pybind11::slice, T>
        );

        template <typename T>
        concept range_like = (
            std::is_base_of_v<Range, T>
        );

        template <typename T>
        concept tuple_like = (
            categories::Traits<T>::tuplelike ||
            std::is_base_of_v<Tuple, T> ||
            std::is_base_of_v<pybind11::tuple, T>
        );

        template <typename T>
        concept list_like = (
            categories::Traits<T>::listlike ||
            std::is_base_of_v<List, T> ||
            std::is_base_of_v<pybind11::list, T>
        );

        template <typename T>
        concept set_like = (
            categories::Traits<T>::setlike ||
            std::is_base_of_v<Set, T> ||
            std::is_base_of_v<pybind11::set, T>
        );

        template <typename T>
        concept frozenset_like = (
            categories::Traits<T>::setlike ||
            std::is_base_of_v<FrozenSet, T> ||
            std::is_base_of_v<pybind11::frozenset, T>
        );

        template <typename T>
        concept anyset_like = set_like<T> || frozenset_like<T>;

        template <typename T>
        concept dict_like = (
            categories::Traits<T>::dictlike ||
            std::is_base_of_v<Dict, T> ||
            std::is_base_of_v<pybind11::dict, T>
        );

        template <typename T>
        concept mappingproxy_like = (
            categories::Traits<T>::dictlike ||
            std::is_base_of_v<MappingProxy, T>
        );

        template <typename T>
        concept anydict_like = dict_like<T> || mappingproxy_like<T>;

        template <typename T>
        concept type_like = (
            std::is_base_of_v<Type, T> ||
            std::is_base_of_v<pybind11::type, T>
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

        // TODO: what happens if we remove is_std_iterator?

        /* NOTE: reverse operators sometimes conflict with standard library iterators, so
        we need some way of detecting them.  This is somewhat hacky, but it seems to
        work. */
        template <typename T, typename = void>
        constexpr bool is_std_iterator = false;
        template <typename T>
        constexpr bool is_std_iterator<
            T, std::void_t<decltype(
                std::declval<typename std::iterator_traits<T>::iterator_category>()
            )>
        > = true;

        template <typename T>
        concept pybind11_iterable = requires(const T& t) {
            { pybind11::iter(t) } -> std::convertible_to<py::Iterator>;
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

    }
    using namespace concepts;

    template <typename T>
    struct __dereference__ { static constexpr bool enable = false; };  // TODO: rename to __unpack__?
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
    template <typename T>
    struct __pos__ { static constexpr bool enable = false; };
    template <typename T>
    struct __neg__ { static constexpr bool enable = false; };
    template <typename T>
    struct __abs__ { static constexpr bool enable = false; };  // TODO: enable/disable py::abs() and set return value
    template <typename T>
    struct __invert__ { static constexpr bool enable = false; };
    template <typename T>
    struct __increment__ { static constexpr bool enable = false; };  // ++
    template <typename T>
    struct __decrement__ { static constexpr bool enable = false; };  // --
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
            return operator_call<Return>(*this, std::forward<Args>(args)...);           \
        }                                                                               \
                                                                                        \
        template <typename Key> requires (impl::__getitem__<cls, Key>::enable)          \
        inline auto operator[](const Key& key) const {                                  \
            return operator_getitem(*this, key);                                        \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__getitem__<T, Slice>::enable)      \
        inline auto operator[](                                                         \
            std::initializer_list<impl::SliceInitializer> slice                         \
        ) const {                                                                       \
            return operator_getitem(*this, slice);                                      \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__iter__<T>::enable)                \
        inline auto begin() const {                                                     \
            using Return = typename impl::__iter__<T>::Return;                          \
            return operator_begin<Return>(*this);                                       \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__iter__<T>::enable)                \
        inline auto end() const {                                                       \
            using Return = typename impl::__iter__<T>::Return;                          \
            return operator_end<Return>(*this);                                         \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__reversed__<T>::enable)            \
        inline auto rbegin() const {                                                    \
            using Return = typename impl::__reversed__<T>::Return;                      \
            return operator_rbegin<Return>(*this);                                      \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__reversed__<T>::enable)            \
        inline auto rend() const {                                                      \
            using Return = typename impl::__reversed__<T>::Return;                      \
            return operator_rend<Return>(*this);                                        \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__contains__<cls, T>::enable)             \
        inline bool contains(const T& key) const {                                      \
            using Return = typename impl::__contains__<cls, T>::Return;                 \
            return operator_contains<Return>(*this, key);                               \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__len__<T>::enable)                 \
        inline size_t size() const {                                                    \
            using Return = typename impl::__len__<T>::Return;                           \
            return operator_len<Return>(*this);                                         \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__dereference__<T>::enable)         \
        inline auto operator*() const {                                                 \
            using Return = typename impl::__dereference__<T>::Return;                   \
            return operator_dereference<Return>(*this);                                 \
        }                                                                               \
                                                                                        \
        inline cls* operator&() { return this; }                                        \
        inline const cls* operator&() const { return this; }                            \
                                                                                        \
        template <typename T = cls> requires (impl::__pos__<T>::enable)                 \
        inline auto operator+() const {                                                 \
            using Return = typename impl::__pos__<T>::Return;                           \
            return operator_pos<Return>(*this);                                         \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__neg__<T>::enable)                 \
        inline auto operator-() const {                                                 \
            using Return = typename impl::__neg__<T>::Return;                           \
            return operator_neg<Return>(*this);                                         \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__invert__<T>::enable)              \
        inline auto operator~() const {                                                 \
            using Return = typename impl::__invert__<T>::Return;                        \
            return operator_invert<Return>(*this);                                      \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__increment__<T>::enable)           \
        inline cls& operator++() {                                                      \
            using Return = typename impl::__increment__<T>::Return;                     \
            operator_increment<Return>(*this);                                          \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__increment__<T>::enable)           \
        inline cls operator++(int) {                                                    \
            using Return = typename impl::__increment__<T>::Return;                     \
            cls copy = *this;                                                           \
            operator_increment<Return>(*this);                                          \
            return copy;                                                                \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__decrement__<T>::enable)           \
        inline cls& operator--() {                                                      \
            using Return = typename impl::__decrement__<T>::Return;                     \
            operator_decrement<Return>(*this);                                          \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T = cls> requires (impl::__decrement__<T>::enable)           \
        inline cls operator--(int) {                                                    \
            using Return = typename impl::__decrement__<T>::Return;                     \
            cls copy = *this;                                                           \
            operator_decrement<Return>(*this);                                          \
            return copy;                                                                \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__lt__<cls, T>::enable)                   \
        inline auto operator<(const T& value) const {                                   \
            using Return = typename impl::__lt__<cls, T>::Return;                       \
            return operator_lt<Return>(*this, value);                                   \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__lt__<T, cls>::enable && !impl::__lt__<cls, T>::enable)    \
        inline friend auto operator<(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__lt__<T, cls>::Return;                       \
            return operator_lt<Return>(lhs, rhs);                                       \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__le__<cls, T>::enable)                   \
        inline auto operator<=(const T& value) const {                                  \
            using Return = typename impl::__le__<cls, T>::Return;                       \
            return operator_le<Return>(*this, value);                                   \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__le__<T, cls>::enable && !impl::__lt__<cls, T>::enable)    \
        inline friend auto operator<=(const T& lhs, const cls& rhs) {                   \
            using Return = typename impl::__le__<T, cls>::Return;                       \
            return operator_le<Return>(lhs, rhs);                                       \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__eq__<cls, T>::enable)                   \
        inline auto operator==(const T& value) const {                                  \
            using Return = typename impl::__eq__<cls, T>::Return;                       \
            return operator_eq<Return>(*this, value);                                   \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__eq__<T, cls>::enable && !impl::__eq__<cls, T>::enable)    \
        inline friend auto operator==(const T& lhs, const cls& rhs) {                   \
            using Return = typename impl::__eq__<T, cls>::Return;                       \
            return operator_eq<Return>(lhs, rhs);                                       \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__ne__<cls, T>::enable)                   \
        inline auto operator!=(const T& value) const {                                  \
            using Return = typename impl::__ne__<cls, T>::Return;                       \
            return operator_ne<Return>(*this, value);                                   \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__ne__<T, cls>::enable && !impl::__ne__<cls, T>::enable)    \
        inline friend auto operator!=(const T& lhs, const cls& rhs) {                   \
            using Return = typename impl::__ne__<T, cls>::Return;                       \
            return operator_ne<Return>(lhs, rhs);                                       \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__ge__<cls, T>::enable)                   \
        inline auto operator>=(const T& value) const {                                  \
            using Return = typename impl::__ge__<cls, T>::Return;                       \
            return operator_ge<Return>(*this, value);                                   \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__ge__<T, cls>::enable && !impl::__ge__<cls, T>::enable)    \
        inline friend auto operator>=(const T& lhs, const cls& rhs) {                   \
            using Return = typename impl::__ge__<T, cls>::Return;                       \
            return operator_ge<Return>(lhs, rhs);                                       \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__gt__<cls, T>::enable)                   \
        inline auto operator>(const T& value) const {                                   \
            using Return = typename impl::__gt__<cls, T>::Return;                       \
            return operator_gt<Return>(*this, value);                                   \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__gt__<T, cls>::enable && !impl::__gt__<cls, T>::enable)    \
        inline friend auto operator>(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__gt__<T, cls>::Return;                       \
            return operator_gt<Return>(lhs, rhs);                                       \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__add__<cls, T>::enable)                  \
        inline auto operator+(const T& value) const {                                   \
            using Return = typename impl::__add__<cls, T>::Return;                      \
            return operator_add<Return>(*this, value);                                  \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__add__<T, cls>::enable && !impl::__add__<cls, T>::enable)  \
        inline friend auto operator+(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__add__<T, cls>::Return;                      \
            return operator_add<Return>(lhs, rhs);                                      \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__iadd__<cls, T>::enable)                 \
        inline cls& operator+=(const T& value) {                                        \
            using Return = typename impl::__iadd__<cls, T>::Return;                     \
            operator_iadd<Return>(*this, value);                                        \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__sub__<cls, T>::enable)                  \
        inline auto operator-(const T& value) const {                                   \
            using Return = typename impl::__sub__<cls, T>::Return;                      \
            return operator_sub<Return>(*this, value);                                  \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__sub__<T, cls>::enable && !impl::__sub__<cls, T>::enable)  \
        inline friend auto operator-(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__sub__<T, cls>::Return;                      \
            return operator_sub<Return>(lhs, rhs);                                      \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__isub__<cls, T>::enable)                 \
        inline cls& operator-=(const T& value) {                                        \
            using Return = typename impl::__isub__<cls, T>::Return;                     \
            operator_isub<Return>(*this, value);                                        \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__mul__<cls, T>::enable)                  \
        inline auto operator*(const T& value) const {                                   \
            using Return = typename impl::__mul__<cls, T>::Return;                      \
            return operator_mul<Return>(*this, value);                                  \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__mul__<T, cls>::enable && !impl::__mul__<cls, T>::enable)  \
        inline friend auto operator*(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__mul__<T, cls>::Return;                      \
            return operator_mul<Return>(lhs, rhs);                                      \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__imul__<cls, T>::enable)                 \
        inline cls& operator*=(const T& value) {                                        \
            using Return = typename impl::__imul__<cls, T>::Return;                     \
            operator_imul<Return>(*this, value);                                        \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__truediv__<cls, T>::enable)              \
        inline auto operator/(const T& value) const {                                   \
            using Return = typename impl::__truediv__<cls, T>::Return;                  \
            return operator_truediv<Return>(*this, value);                              \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (                                                                  \
                impl::__truediv__<T, cls>::enable &&                                    \
                !impl::__truediv__<cls, T>::enable                                      \
            )                                                                           \
        inline friend auto operator/(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__truediv__<T, cls>::Return;                  \
            return operator_truediv<Return>(lhs, rhs);                                  \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__itruediv__<cls, T>::enable)             \
        inline cls& operator/=(const T& value) {                                        \
            using Return = typename impl::__itruediv__<cls, T>::Return;                 \
            operator_itruediv<Return>(*this, value);                                    \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__mod__<cls, T>::enable)                  \
        inline auto operator%(const T& value) const {                                   \
            using Return = typename impl::__mod__<cls, T>::Return;                      \
            return operator_mod<Return>(*this, value);                                  \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__mod__<T, cls>::enable && !impl::__mod__<cls, T>::enable)  \
        inline friend auto operator%(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__mod__<T, cls>::Return;                      \
            return operator_mod<Return>(lhs, rhs);                                      \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__imod__<cls, T>::enable)                 \
        inline cls& operator%=(const T& value) {                                        \
            using Return = typename impl::__imod__<cls, T>::Return;                     \
            operator_imod<Return>(*this, value);                                        \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__lshift__<cls, T>::enable)               \
        inline auto operator<<(const T& value) const {                                  \
            using Return = typename impl::__lshift__<cls, T>::Return;                   \
            return operator_lshift<Return>(*this, value);                               \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (                                                                  \
                impl::__lshift__<T, cls>::enable &&                                     \
                !impl::__lshift__<cls, T>::enable                                       \
            )                                                                           \
        inline friend auto operator<<(const T& lhs, const cls& rhs) {                   \
            using Return = typename impl::__lshift__<T, cls>::Return;                   \
            return operator_lshift<Return>(lhs, rhs);                                   \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__ilshift__<cls, T>::enable)              \
        inline cls& operator<<=(const T& value) {                                       \
            using Return = typename impl::__ilshift__<cls, T>::Return;                  \
            operator_ilshift<Return>(*this, value);                                     \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__rshift__<cls, T>::enable)               \
        inline auto operator>>(const T& value) const {                                  \
            using Return = typename impl::__rshift__<cls, T>::Return;                   \
            return operator_rshift<Return>(*this, value);                               \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (                                                                  \
                impl::__rshift__<T, cls>::enable &&                                     \
                !impl::__rshift__<cls, T>::enable                                       \
            )                                                                           \
        inline friend auto operator>>(const T& lhs, const cls& rhs) {                   \
            using Return = typename impl::__rshift__<T, cls>::Return;                   \
            return operator_rshift<Return>(lhs, rhs);                                   \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__irshift__<cls, T>::enable)              \
        inline cls& operator>>=(const T& value) {                                       \
            using Return = typename impl::__irshift__<cls, T>::Return;                  \
            operator_irshift<Return>(*this, value);                                     \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__and__<cls, T>::enable)                  \
        inline auto operator&(const T& value) const {                                   \
            using Return = typename impl::__and__<cls, T>::Return;                      \
            return operator_and<Return>(*this, value);                                  \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__and__<T, cls>::enable && !impl::__and__<cls, T>::enable)  \
        inline friend auto operator&(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__and__<T, cls>::Return;                      \
            return operator_and<Return>(lhs, rhs);                                      \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__iand__<cls, T>::enable)                 \
        inline cls& operator&=(const T& value) {                                        \
            using Return = typename impl::__iand__<cls, T>::Return;                     \
            operator_iand<Return>(*this, value);                                        \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__or__<cls, T>::enable)                   \
        inline auto operator|(const T& value) const {                                   \
            using Return = typename impl::__or__<cls, T>::Return;                       \
            return operator_or<Return>(*this, value);                                   \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__or__<T, cls>::enable && !impl::__or__<cls, T>::enable)    \
        inline friend auto operator|(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__or__<T, cls>::Return;                       \
            return operator_or<Return>(lhs, rhs);                                       \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__ior__<cls, T>::enable)                  \
        inline cls& operator|=(const T& value) {                                        \
            using Return = typename impl::__ior__<cls, T>::Return;                      \
            operator_ior<Return>(*this, value);                                         \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__xor__<cls, T>::enable)                  \
        inline auto operator^(const T& value) const {                                   \
            using Return = typename impl::__xor__<cls, T>::Return;                      \
            return operator_xor<Return>(*this, value);                                  \
        }                                                                               \
                                                                                        \
        template <typename T>                                                           \
            requires (impl::__xor__<T, cls>::enable && !impl::__xor__<cls, T>::enable)  \
        inline friend auto operator^(const T& lhs, const cls& rhs) {                    \
            using Return = typename impl::__xor__<T, cls>::Return;                      \
            return operator_xor<Return>(lhs, rhs);                                      \
        }                                                                               \
                                                                                        \
        template <typename T> requires (impl::__ixor__<cls, T>::enable)                 \
        inline cls& operator^=(const T& value) {                                        \
            using Return = typename impl::__ixor__<cls, T>::Return;                     \
            operator_ixor<Return>(*this, value);                                        \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        inline friend std::ostream& operator<<(std::ostream& os, const cls& obj) {      \
            PyObject* repr = PyObject_Repr(obj.ptr());                                  \
            if (repr == nullptr) {                                                      \
                throw error_already_set();                                              \
            }                                                                           \
            Py_ssize_t size;                                                            \
            const char* data = PyUnicode_AsUTF8AndSize(repr, &size);                    \
            if (data == nullptr) {                                                      \
                Py_DECREF(repr);                                                        \
                throw error_already_set();                                              \
            }                                                                           \
            os.write(data, size);                                                       \
            Py_DECREF(repr);                                                            \
            return os;                                                                  \
        }                                                                               \

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
    #define BERTRAND_OBJECT_COMMON(parent, cls, check_func)                             \
        /* Overload check() for C++ values using template metaprogramming. */           \
        template <typename T> requires (!impl::python_like<T>)                          \
        static constexpr bool check(const T&) {                                         \
            return check<T>();                                                          \
        }                                                                               \
                                                                                        \
        /* Overload check() for Python objects using check_func. */                     \
        template <typename T> requires(impl::python_like<T>)                            \
        static constexpr bool check(const T& obj) {                                     \
            return obj.ptr() != nullptr && check_func(obj.ptr());                       \
        }                                                                               \
                                                                                        \
        /* For compatibility with pybind11, which expects these methods. */             \
        template <typename T> requires (!impl::python_like<T>)                          \
        static constexpr bool check_(const T& value) { return check(value); }           \
        template <typename T> requires (impl::python_like<T>)                           \
        static constexpr bool check_(const T& value) { return check(value); }           \
                                                                                        \
        /* Inherit tagged borrow/steal and copy/move constructors. */                   \
        cls(Handle h, const borrowed_t& t) : parent(h, t) {}                            \
        cls(Handle h, const stolen_t& t) : parent(h, t) {}                              \
        cls(const cls& value) : parent(value) {}                                        \
        cls(cls&& value) : parent(std::move(value)) {}                                  \
                                                                                        \
        /* Convert a pybind11 accessor into this type. */                               \
        template <typename Policy>                                                      \
        cls(const detail::accessor<Policy> &a) {                                        \
            pybind11::object obj(a);                                                    \
            if (!check(obj)) {                                                          \
                throw impl::noconvert<cls>(obj.ptr());                                  \
            }                                                                           \
            m_ptr = obj.release().ptr();                                                \
        }                                                                               \
                                                                                        \
        /* Convert a bertrand accessor into this type. */                               \
        template <typename Policy>                                                      \
        cls(const impl::Proxy<Policy>& value) : parent(*value) {}                       \
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
        BERTRAND_OBJECT_OPERATORS(cls)                                                  \

    template <>
    struct __dereference__<Object>                          : Returns<detail::args_proxy> {};
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

    template <typename Policy>
    class Proxy;
    class AttrAccessor;
    template <typename Obj, typename Key>
    class ItemAccessor;

    struct SliceInitializer;

}  // namespace impl


// template <typename T> requires (impl::__abs__<T>::enable)
// inline auto abs(const T& value) {
//     using Return = impl::__abs__<T>::Return;
//     static_assert(
//         std::is_base_of_v<Object, Return>,
//         "Absolute value operator must return a py::Object subclass.  Check your "
//         "specialization of __abs__ for this type and ensure the Return type is set to "
//         "a py::Object subclass."
//     );
//     PyObject* result = PyNumber_Absolute(detail::object_or_cast(value).ptr());
//     if (result == nullptr) {
//         throw error_already_set();
//     }
//     return reinterpret_steal<Return>(result);
// }


/* A revised pybind11::object interface that allows implicit conversions to subtypes
(applying a type check on the way), explicit conversions to arbitrary C++ types via
static_cast<>, cross-language math operators, and generalized slice/attr syntax. */
class Object : public pybind11::object {
    using Base = pybind11::object;

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

    /* Default constructor.  Initializes to a null object, which should not be used
    without initialization.  Note that this is one of the only ways in which a null
    pointer can be injected into the bertrand type system, the other being interactions
    with a wrapper that has been moved from. */
    Object() = default;  // TODO: make this protected?

    /* reinterpret_borrow/steal constructors.  The tags themselves are protected and
    only accessible within subclasses of pybind11::object. */
    Object(pybind11::handle h, const borrowed_t& t) : Base(h, t) {}
    Object(pybind11::handle h, const stolen_t& t) : Base(h, t) {}

    /* Copy constructor.  Borrows a reference to an existing python object. */
    Object(const pybind11::object& o) : Base(o) {}

    /* Move constructor.  Steals a reference to a rvalue python object. */
    Object(pybind11::object&& o) : Base(std::move(o)) {}

    /* Convert a pybind11 accessor into a generic Object. */
    template <typename Policy>
    Object(const detail::accessor<Policy> &a) : Base(pybind11::object(a)) {}

    /* Convert a bertrand accessor into a generic Object. */
    template <typename Policy>
    Object(const impl::Proxy<Policy>& value) : Base(*value) {}

    /* Convert any non-callable C++ value into a generic python object. */
    template <typename T> requires (!impl::python_like<T> && !impl::is_callable_any<T>)
    Object(const T& value) : Base(pybind11::cast(value).release(), stolen_t{}) {}

    /* Convert any callable C++ value into a generic python object. */
    template <typename T> requires (!impl::python_like<T> && impl::is_callable_any<T>)
    Object(const T& value);  // defined in python.h

    using Base::operator=;

    /* Assign any C++ value to the object wrapper. */
    template <typename T> requires (!impl::python_like<T>)
    Object& operator=(T&& value) {
        Base::operator=(Object(std::forward<T>(value)));
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* NOTE: the Object wrapper can be implicitly converted to any of its subclasses by
     * applying a runtime type check during the assignment.  This allows us to safely
     * convert from a generic object to a more specialized type without worrying about
     * type mismatches or triggering arbitrary conversion logic.  It allows us to
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
     * Which is identical to calling the `str()` type at the python level.  Note that
     * the implicit conversion operator is only enabled for Object itself, and is
     * explicitly deleted in all of its subclasses.  This prevents implicit conversions
     * between subclasses and promotes any attempt to do so into a compile-time error.
     * For instance:
     *
     *      py::Bool b = true;
     *      py::Str s = b;  // fails to compile, calls a deleted function
     *
     * In general, this makes assignment via the `=` operator type-safe by default,
     * while explicit constructors are reserved for non-trivial conversions and/or
     * packing in the case of containers.
     */

    /* Implicitly convert an Object wrapper to one of its subclasses, applying a
    runtime type check to the underlying value. */
    template <typename T> requires (std::is_base_of_v<Object, T>)
    inline operator T() const {
        if (!T::check(*this)) {
            throw impl::noconvert<T>(this->ptr());
        }
        return reinterpret_borrow<T>(this->ptr());
    }

    /* Implicitly convert an Object to a wrapper class, which moves it into a managed
    buffer for static storage duration, etc. */
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
    with the same semantics as Python. */
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

    // TODO: update docs

    /* Type-safe operator overloads are easily the most complicated thing about
     * bertrand's `Object` interface.  They are implemented using the control structs
     * defined in the `impl::` namespace, which can be specialized to selectively
     * enable supported operators and assign appropriate return types for static
     * analysis.
     *
     * Some operators can only be defined as member functions within this class, which
     * complicates the control struct approach to operator overloading.  We can work
     * around this by using the impl::Inherits helper to reset these operators for
     * each subclass.  This is fairly ugly, but until C++23's "deducing this" feature,
     * it's the best we can do.
     */

    template <typename Key> requires (impl::str_like<Key>)
    inline impl::Proxy<impl::AttrAccessor> attr(Key&& key) const;

    BERTRAND_OBJECT_OPERATORS(Object)

private:

    template <typename... Args>
    inline auto operator_call_impl(Args&&... args) const {
        return Base::operator()(std::forward<Args>(args)...);
    }

    inline Iterator operator_begin_impl() const { return Base::begin(); }
    inline Iterator operator_end_impl() const { return Base::end(); }
    inline Iterator operator_rbegin_impl() const;
    inline Iterator operator_rend_impl() const { return Iterator::sentinel(); }

    template <typename T>
    inline bool operator_contains_impl(const T& key) const {
        return Base::contains(key);
    }

    inline auto operator_dereference_impl() const {
        return Base::operator*();
    }

protected:

    template <typename Return, typename T, typename... Args>
    inline static Return operator_call(const T& obj, Args&&... args);

    template <typename T, typename Key>
    inline static auto operator_getitem(const T& obj, Key&& key)
        -> impl::Proxy<impl::ItemAccessor<T, std::decay_t<Key>>>;

    template <typename T>
    inline static auto operator_getitem(
        const T& obj,
        std::initializer_list<impl::SliceInitializer> slice
    ) -> impl::Proxy<impl::ItemAccessor<T, Slice>>;

    template <typename Return, typename T>
    inline static Iterator operator_begin(const T& obj) {
        static_assert(
            std::is_base_of_v<Object, Return>,
            "iterator must dereference to a subclass of Object.  Check your "
            "specialization of __iter__ for this types and ensure the Return type "
            "is a subclass of py::Object."
        );
        return obj.operator_begin_impl();
    }

    template <typename Return, typename T>
    inline static Iterator operator_end(const T& obj) {
        static_assert(
            std::is_base_of_v<Object, Return>,
            "iterator must dereference to a subclass of Object.  Check your "
            "specialization of __iter__ for this types and ensure the Return type "
            "is a subclass of py::Object."
        );
        return obj.operator_end_impl();
    }

    template <typename Return, typename T>
    inline static Iterator operator_rbegin(const T& obj) {
        static_assert(
            std::is_base_of_v<Object, Return>,
            "iterator must dereference to a subclass of Object.  Check your "
            "specialization of __reversed__ for this types and ensure the Return type "
            "is a subclass of py::Object."
        );
        return obj.operator_rbegin_impl();
    }

    template <typename Return, typename T>
    inline static Iterator operator_rend(const T& obj) {
        static_assert(
            std::is_base_of_v<Object, Return>,
            "iterator must dereference to a subclass of Object.  Check your "
            "specialization of __reversed__ for this types and ensure the Return type "
            "is a subclass of py::Object."
        );
        return obj.operator_rend_impl();
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_contains(const L& lhs, const R& rhs) {
        static_assert(
            std::is_same_v<Return, bool>,
            "contains() operator must return a boolean value.  Check your "
            "specialization of __contains__ for these types and ensure the Return "
            "type is set to bool."
        );
        return lhs.operator_contains_impl(rhs);
    }

    template <typename Return, typename T>
    inline static size_t operator_len(const T& obj) {
        static_assert(
            std::is_same_v<Return, size_t>,
            "size() operator must return a size_t for compatibility with C++ "
            "containers.  Check your specialization of __len__ for these types and "
            "ensure the Return type is set to size_t."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Unary positive operator must return a py::Object subclass.  Check your "
            "specialization of __pos__ for this type and ensure the Return type is "
            "set to a py::Object subclass."
        );
        PyObject* result = PyNumber_Positive(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename T>
    inline static auto operator_neg(const T& obj) {
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Unary negative operator must return a py::Object subclass.  Check your "
            "specialization of __neg__ for this type and ensure the Return type is "
            "set to a py::Object subclass."
        );
        PyObject* result = PyNumber_Negative(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename T>
    inline static auto operator_invert(const T& obj) {
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Bitwise NOT operator must return a py::Object subclass.  Check your "
            "specialization of __invert__ for this type and ensure the Return type is "
            "set to a py::Object subclass."
        );
        PyObject* result = PyNumber_Invert(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Return>(result);
    }

    template <typename Return, typename T>
    inline static void operator_increment(T& obj) {
        static_assert(
            std::is_same_v<Return, T>,
            "Increment operator must return a reference to the derived type.  Check "
            "your specialization of __increment__ for this type and ensure the Return "
            "type is set to the derived type."
        );
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
        static_assert(
            std::is_same_v<Return, T>,
            "Decrement operator must return a reference to the derived type.  Check "
            "your specialization of __decrement__ for this type and ensure the Return "
            "type is set to the derived type."
        );
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
        static_assert(
            std::is_same_v<Return, bool>,
            "Less-than operator must return a boolean value.  Check your "
            "specialization of __lt__ for these types and ensure the Return type is "
            "set to bool."
        );
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
        static_assert(
            std::is_same_v<Return, bool>,
            "Less-than-or-equal operator must return a boolean value.  Check your "
            "specialization of __le__ for this type and ensure the Return type is "
            "set to bool."
        );
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
        static_assert(
            std::is_same_v<Return, bool>,
            "Equality operator must return a boolean value.  Check your "
            "specialization of __eq__ for this type and ensure the Return type is "
            "set to bool."
        );
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

    // TODO: maybe __ne__ can be inferred from __eq__ or vice versa?
    template <typename Return, typename L, typename R>
    inline static bool operator_ne(const L& lhs, const R& rhs) {
        static_assert(
            std::is_same_v<Return, bool>,
            "Inequality operator must return a boolean value.  Check your "
            "specialization of __ne__ for this type and ensure the Return type is "
            "set to bool."
        );
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
        static_assert(
            std::is_same_v<Return, bool>,
            "Greater-than-or-equal operator must return a boolean value.  Check your "
            "specialization of __ge__ for this type and ensure the Return type is "
            "set to bool."
        );
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
        static_assert(
            std::is_same_v<Return, bool>,
            "Greater-than operator must return a boolean value.  Check your "
            "specialization of __gt__ for this type and ensure the Return type is "
            "set to bool."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Addition operator must return a py::Object subclass.  Check your "
            "specialization of __add__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place addition operator must return the same type as the left "
            "operand.  Check your specialization of __iadd__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Subtraction operator must return a py::Object subclass.  Check your "
            "specialization of __sub__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place addition operator must return the same type as the left "
            "operand.  Check your specialization of __iadd__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Multiplication operator must return a py::Object subclass.  Check your "
            "specialization of __mul__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place multiplication operator must return the same type as the left "
            "operand.  Check your specialization of __imul__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "True division operator must return a py::Object subclass.  Check your "
            "specialization of __truediv__ for this type and ensure the Return type "
            "is derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place true division operator must return the same type as the left "
            "operand.  Check your specialization of __itruediv__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Modulus operator must return a py::Object subclass.  Check your "
            "specialization of __mod__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place modulus operator must return the same type as the left "
            "operand.  Check your specialization of __imod__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Left shift operator must return a py::Object subclass.  Check your "
            "specialization of __lshift__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place left shift operator must return the same type as the left "
            "operand.  Check your specialization of __ilshift__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Right shift operator must return a py::Object subclass.  Check your "
            "specialization of __rshift__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place right shift operator must return the same type as the left operand.  "
            "Check your specialization of __irshift__ for these types and ensure the "
            "Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Bitwise AND operator must return a py::Object subclass.  Check your "
            "specialization of __and__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place bitwise AND operator must return the same type as the left "
            "operand.  Check your specialization of __iand__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Bitwise OR operator must return a py::Object subclass.  Check your "
            "specialization of __or__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place bitwise OR operator must return the same type as the left "
            "operand.  Check your specialization of __ior__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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
        static_assert(
            std::is_base_of_v<Object, Return>,
            "Bitwise XOR operator must return a py::Object subclass.  Check your "
            "specialization of __xor__ for this type and ensure the Return type is "
            "derived from py::Object."
        );
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
        static_assert(
            std::is_same_v<Return, L>,
            "In-place bitwise XOR operator must return the same type as the left "
            "operand.  Check your specialization of __ixor__ for these types and "
            "ensure the Return type is set to the left operand."
        );
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

    struct PolicyTag {};

    /* Base class for all accessor proxies.  Stores an arbitrary object in a buffer and
    forwards its interface using pointer semantics. */
    template <typename Policy>
    class Proxy : public ProxyTag {

        template <typename... Args>
        static constexpr bool has_assignment_operator = requires(Policy p, Args&&... args) {
            { p.set(std::forward<Args>(args)...) } -> std::same_as<void>;
        };

        template <typename... Args>
        static constexpr bool has_deletion_operator = requires(Policy p, Args&&... args) {
            { p.del(std::forward<Args>(args)...) } -> std::same_as<void>;
        };

    public:
        using Wrapped = Policy::Wrapped;
        Policy policy;

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Forwarding constructor for templated policy. */
        template <typename... Args>
        Proxy(Args&&... args) : policy(std::forward<Args>(args)...) {}

        /* Copy constructor. */
        Proxy(const Proxy& other) : policy(other.policy) {}

        /* Move constructor. */
        Proxy(Proxy&& other) : policy(std::move(other.policy)) {}

        // /* Copy assignment operator. */
        // Proxy& operator=(const Proxy& other) {
        //     if (&other != this) {
        //         if (initialized) {
        //             initialized = false;
        //             reinterpret_cast<Wrapped&>(buffer).~Wrapped();
        //         }
        //         if (other.initialized) {
        //             new (buffer) Wrapped(reinterpret_cast<Wrapped&>(other.buffer));
        //             initialized = true;
        //         }
        //     }
        //     return *this;
        // }

        // /* Move assignment operator. */
        // Proxy& operator=(Proxy&& other) {
        //     if (&other != this) {
        //         if (initialized) {
        //             initialized = false;
        //             reinterpret_cast<Wrapped&>(buffer).~Wrapped();
        //         }
        //         if (other.initialized) {
        //             other.initialized = false;
        //             new (buffer) Wrapped(
        //                 std::move(reinterpret_cast<Wrapped&>(other.buffer))
        //             );
        //             initialized = true;
        //         }
        //     }
        //     return *this;
        // }

        ///////////////////////////
        ////    GET/SET/DEL    ////
        ///////////////////////////

        inline Wrapped& operator*() {
            return policy.get();
        }

        inline const Wrapped& operator*() const {
            return policy.get();
        }

        template <typename... Args> requires (has_assignment_operator<Args...>)
        inline Proxy& operator=(Args&&... args) {
            policy.set(std::forward<Args>(args)...);
            return *this;
        }

        template <typename... Args> requires (has_deletion_operator<Args...>)
        inline void del(Args&&... args) {
            return policy.del(std::forward<Args>(args)...);
        }

        ////////////////////////////////////
        ////    FORWARDING INTERFACE    ////
        ////////////////////////////////////

        // all attributes of wrapped type are forwarded using the arrow operator.  Just
        // replace all instances of `.` with `->`
        inline Wrapped* operator->() {
            return &(**this);
        };

        inline const Wrapped* operator->() const {
            return &(**this);
        };

        inline operator Wrapped() const {
            return **this;
        }

        template <typename T> requires (std::is_convertible_v<Wrapped, T>)
        inline operator T() const {
            return implicit_cast<T>(**this);
        }

        template <typename T> requires (!std::is_convertible_v<Wrapped, T>)
        inline explicit operator T() const {
            return static_cast<T>(**this);
        }

        /////////////////////////
        ////    OPERATORS    ////
        /////////////////////////

        inline Proxy* operator&() { return this; }
        inline const Proxy* operator&() const { return this; }

        template <typename... Args>
        inline auto operator()(Args&&... args) const {
            return (**this)(std::forward<Args>(args)...);
        }

        template <typename T>
        inline auto operator[](T&& key) const {
            return (**this)[std::forward<T>(key)];
        }

        template <typename T = Wrapped> requires (impl::__getitem__<T, Slice>::enable)
        inline auto operator[](std::initializer_list<impl::SliceInitializer> slice) const;

        template <typename T>
        inline auto contains(const T& key) const { return (**this).contains(key); }
        inline auto size() const { return (**this).size(); }
        inline auto begin() const { return (**this).begin(); }
        inline auto end() const { return (**this).end(); }
        inline auto rbegin() const { return (**this).rbegin(); }
        inline auto rend() const { return (**this).rend(); }

        #define BINARY_OPERATOR(op)                                                     \
            template <typename T>                                                       \
            inline auto operator op(const T& value) const {                             \
                return **this op value;                                                 \
            }                                                                           \
            template <typename T>                                                       \
            inline friend auto operator op(                                             \
                const T& value, const Proxy& self                                       \
            ) { return value op *self; }                                                \

        #define INPLACE_OPERATOR(op)                                                    \
            template <typename T>                                                       \
            inline Proxy& operator op(const T& value) {                                 \
                **this op value;                                                        \
                return *this;                                                           \
            }                                                                           \

        inline auto operator+() const { return +(**this); }
        inline auto operator-() const { return -(**this); }
        inline auto operator~() const { return ~(**this); }
        inline auto operator++() { return ++(**this); }
        inline auto operator--() { return --(**this); }
        inline auto operator++(int) { return (**this)++; }
        inline auto operator--(int) { return (**this)--; }
        BINARY_OPERATOR(<)
        BINARY_OPERATOR(<=)
        BINARY_OPERATOR(==)
        BINARY_OPERATOR(!=)
        BINARY_OPERATOR(>=)
        BINARY_OPERATOR(>)
        BINARY_OPERATOR(+)
        BINARY_OPERATOR(-)
        BINARY_OPERATOR(*)
        BINARY_OPERATOR(/)
        BINARY_OPERATOR(%)
        BINARY_OPERATOR(<<)
        BINARY_OPERATOR(>>)
        BINARY_OPERATOR(&)
        BINARY_OPERATOR(|)
        BINARY_OPERATOR(^)
        INPLACE_OPERATOR(+=)
        INPLACE_OPERATOR(-=)
        INPLACE_OPERATOR(*=)
        INPLACE_OPERATOR(/=)
        INPLACE_OPERATOR(%=)
        INPLACE_OPERATOR(<<=)
        INPLACE_OPERATOR(>>=)
        INPLACE_OPERATOR(&=)
        INPLACE_OPERATOR(|=)
        INPLACE_OPERATOR(^=)

        #undef BINARY_OPERATOR
        #undef INPLACE_OPERATOR

        inline friend std::ostream& operator<<(std::ostream& os, const Proxy& self) {
            os << *self;
            return os;
        }

    };

    /* A Proxy policy that replaces the result of pybind11's `.attr()` method.  This
    does not (and can not) enforce any strict typing rules, but it brings the syntax
    more in line with the rest of bertrand's expanded operator overloads. */
    class AttrAccessor : PolicyTag {
        alignas (Object) mutable unsigned char buffer[sizeof(Object)];
        mutable bool initialized;
        Handle obj;
        Object key;

    public:
        using Wrapped = Object;

        template <typename T> requires (str_like<T> && python_like<T>)
        AttrAccessor(Handle obj, T&& key) :
            initialized(false), obj(obj), key(std::forward<T>(key))
        {}

        AttrAccessor(Handle obj, const char* key) :
            initialized(false), obj(obj),
            key(reinterpret_steal<Object>(PyUnicode_FromString(key)))
        {
            if (this->key.ptr() == nullptr) {
                throw error_already_set();
            }
        }

        AttrAccessor(Handle obj, const std::string& key) :
            initialized(false), obj(obj), key(reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(key.c_str(), key.size())
            ))
        {
            if (this->key.ptr() == nullptr) {
                throw error_already_set();
            }
        }

        AttrAccessor(Handle obj, const std::string_view& key) :
            initialized(false), obj(obj), key(reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(key.data(), key.size())
            ))
        {
            if (this->key.ptr() == nullptr) {
                throw error_already_set();
            }
        }

        AttrAccessor(const AttrAccessor& other) :
            initialized(other.initialized), obj(other.obj), key(other.key)
        {
            if (initialized) {
                new (buffer) Object(reinterpret_cast<Object&>(other.buffer));
            }
        }

        AttrAccessor(AttrAccessor&& other) :
            initialized(other.initialized), obj(other.obj), key(std::move(other.key))
        {
            other.initialized = false;
            if (initialized) {
                new (buffer) Object(std::move(reinterpret_cast<Object&>(other.buffer)));
            }
        }

        ~AttrAccessor() {
            if (initialized) {
                reinterpret_cast<Object&>(buffer).~Object();
            }
        }

        /* pybind11's attribute accessors only perform the lookup when the accessor is
         * converted to a value, which we hook to provide string type safety.  In this
         * case, the accessor is treated like a generic object, and will forward all
         * conversions to py::Object.  This allows us to write code like this:
         *
         *      py::Object obj = ...;
         *      py::Int i = obj.attr("some_int");  // runtime type check
         *
         * But not like this:
         *
         *      py::Str s = obj.attr("some_int");  // runtime error, some_int is not a string
         *
         * Unfortunately, it is not possible to promote these errors to compile time,
         * since Python attributes are inherently dynamic and can't be known in
         * advance.  This is the best we can do without creating a custom type and
         * strictly enforcing attribute types at the C++ level.  If this cannot be
         * done, then the only way to avoid extra runtime overhead is to use
         * reinterpret_steal to bypass the type check, which can be dangerous.
         *
         *      py::Int i = reinterpret_steal<py::Int>(obj.attr("some_int").release());
         */

        Object& get() const {
            if (!initialized) {
                if (obj.ptr() == nullptr) {
                    throw ValueError(
                        "dereferencing an uninitialized accessor.  Either the accessor "
                        "was moved from or not properly constructed to begin with."
                    );
                }
                PyObject* result = PyObject_GetAttr(obj.ptr(), key.ptr());
                if (result == nullptr) {
                    throw error_already_set();
                }
                new (buffer) Object(reinterpret_steal<Object>(result));
                initialized = true;
            }
            return reinterpret_cast<Object&>(buffer);
        }

        /* Similarly, assigning to a pybind11 wrapper corresponds to a Python
         * __setattr__ call.  Due to the same restrictions as above, we can't enforce
         * strict typing here, but we can at least make the syntax more consistent and
         * intuitive in mixed Python/C++ code.
         *
         *      py::Object obj = ...;
         *      obj.attr("some_int") = 5;  // valid: translates to Python.
         */

        template <typename T>
        void set(T&& value) {
            new (buffer) Object(std::forward<T>(value));
            initialized = true;
            if (PyObject_SetAttr(
                obj.ptr(),
                key.ptr(),
                reinterpret_cast<Object&>(buffer).ptr()
            ) < 0) {
                throw error_already_set();
            }
        }

        /* C++'s delete operator does not directly correspond to Python's `del`
         * statement, so we can't piggyback off it here.  Instead, we offer a separate
         * `.del()` method that behaves the same way.
         *
         *      py::Object obj = ...;
         *      obj.attr("some_int").del();  // Equivalent to Python `del obj.some_int`
         */

        void del() {
            if (PyObject_DelAttr(obj.ptr(), key.ptr()) < 0) {
                throw error_already_set();
            }
            if (initialized) {
                reinterpret_cast<Object&>(buffer).~Object();
                initialized = false;
            }
        }

    };

    /* A Proxy policy that replaces the result of pybind11's `[]` operator and promotes
    static type safety.  Uses the __getitem__, __setitem__, and __delitem__ control
    structs to selectively enable/disable the index operator for particular types, as
    well as assignment and deletion on the resulting proxy. */
    template <typename Obj, typename Key>
    class ItemAccessor : PolicyTag {
    public:
        using Wrapped = typename __getitem__<Obj, Key>::Return;
        static_assert(
            std::is_base_of_v<Object, Wrapped>,
            "index operator must return a subclass of py::Object.  Check your "
            "specialization of __getitem__ for these types and ensure the Return "
            "type is set to a subclass of py::Object."
        );

    private:
        alignas (Wrapped) mutable unsigned char buffer[sizeof(Wrapped)];
        mutable bool initialized;
        Handle obj;
        Object key;

    public:

        ItemAccessor(Handle obj, const Key& key) :
            initialized(false), obj(obj), key(key) {}

        ItemAccessor(Handle obj, Key&& key) :
            initialized(false), obj(obj), key(std::move(key))
        {}

        ItemAccessor(const ItemAccessor& other) :
            initialized(other.initialized), obj(other.obj), key(other.key)
        {
            if (initialized) {
                new (buffer) Wrapped(reinterpret_cast<Wrapped&>(other.buffer));
            }
        }

        ItemAccessor(ItemAccessor&& other) :
            initialized(other.initialized), obj(other.obj), key(std::move(other.key))
        {
            other.initialized = false;
            if (initialized) {
                new (buffer) Wrapped(std::move(reinterpret_cast<Wrapped&>(other.buffer)));
            }
        }

        ~ItemAccessor() {
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer).~Wrapped();
            }
        }

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

        Wrapped& get() const {
            if (!initialized) {
                if (obj.ptr() == nullptr) {
                    throw ValueError(
                        "dereferencing an uninitialized accessor.  Either the accessor "
                        "was moved from or not properly constructed to begin with."
                    );
                }
                PyObject* result = PyObject_GetItem(obj.ptr(), key.ptr());
                if (result == nullptr) {
                    throw error_already_set();
                }
                new (buffer) Wrapped(reinterpret_steal<Wrapped>(result));
                initialized = true;
            }
            return reinterpret_cast<Wrapped&>(buffer);
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

        template <typename T> requires (__setitem__<Obj, Key, T>::enable)
        void set(T&& value) {
            static_assert(
                std::is_void_v<typename __setitem__<Obj, Key, T>::Return>,
                "index assignment operator must return void.  Check your "
                "specialization of __setitem__ for these types and ensure the Return "
                "type is set to void."
            );
            new (buffer) Wrapped(std::forward<T>(value));
            initialized = true;
            if (PyObject_SetItem(
                obj.ptr(),
                key.ptr(),
                reinterpret_cast<Wrapped&>(buffer).ptr()
            ) < 0) {
                throw error_already_set();
            }
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
        void del() {
            static_assert(
                std::is_void_v<typename __delitem__<Obj, T>::Return>,
                "index deletion operator must return void.  Check your specialization "
                "of __delitem__ for these types and ensure the Return type is set to "
                "void."
            );
            if (PyObject_DelItem(obj.ptr(), key.ptr()) < 0) {
                throw error_already_set();
            }
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer).~Wrapped();
                initialized = false;
            }
        }

    };

    /* A specialization of Item that is optimized for Tuple instances. */
    template <typename Obj, typename Key>
        requires (std::is_base_of_v<Tuple, Obj> && std::is_integral_v<Key>)
    class ItemAccessor<Obj, Key> : PolicyTag {
    public:
        using Wrapped = typename __getitem__<Obj, Key>::Return;
        static_assert(
            std::is_base_of_v<Object, Wrapped>,
            "index operator must return a subclass of py::Object.  Check your "
            "specialization of __getitem__ for these types and ensure the Return "
            "type is set to a subclass of py::Object."
        );

    private:
        alignas (Wrapped) mutable unsigned char buffer[sizeof(Wrapped)];
        mutable bool initialized;
        Handle obj;
        Py_ssize_t index;

    public:

        ItemAccessor(Handle obj, Py_ssize_t index) :
            initialized(false), obj(obj), index(index)
        {}

        ItemAccessor(const ItemAccessor& other) :
            initialized(other.initialized), obj(other.obj), index(other.index)
        {
            if (initialized) {
                new (buffer) Wrapped(reinterpret_cast<Wrapped&>(other.buffer));
            }
        }

        ItemAccessor(ItemAccessor&& other) noexcept :
            initialized(other.initialized), obj(other.obj), index(other.index)
        {
            other.initialized = false;
            if (initialized) {
                new (buffer) Wrapped(std::move(reinterpret_cast<Wrapped&>(other.buffer)));
            }
        }

        ~ItemAccessor() {
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer).~Wrapped();
            }
        }

        Wrapped& get() const {
            if (!initialized) {
                if (obj.ptr() == nullptr) {
                    throw ValueError(
                        "dereferencing an uninitialized accessor.  Either the accessor "
                        "was moved from or not properly constructed to begin with."
                    );
                }
                Py_ssize_t size = PyTuple_GET_SIZE(obj.ptr());
                Py_ssize_t norm = index + size * (index < 0);
                if (norm < 0 || norm >= size) {
                    throw IndexError("tuple index out of range");
                }
                PyObject* result = PyTuple_GET_ITEM(obj.ptr(), norm);
                if (result == nullptr) {
                    throw error_already_set();
                }
                new (buffer) Wrapped(reinterpret_steal<Wrapped>(result));
                initialized = true;
            }
            return reinterpret_cast<Wrapped&>(buffer);
        }

    };

    /* A specialization of Item that is optimized for List instances. */
    template <typename Obj, typename Key>
        requires (std::is_base_of_v<List, Obj> && std::is_integral_v<Key>)
    class ItemAccessor<Obj, Key> : PolicyTag {
    public:
        using Wrapped = typename __getitem__<Obj, Key>::Return;
        static_assert(
            std::is_base_of_v<Object, Wrapped>,
            "index operator must return a subclass of py::Object.  Check your "
            "specialization of __getitem__ for these types and ensure the Return "
            "type is set to a subclass of py::Object."
        );

    private:
        alignas (Wrapped) mutable unsigned char buffer[sizeof(Wrapped)];
        mutable bool initialized;
        Handle obj;
        Py_ssize_t index;

        inline Py_ssize_t normalized() const {
            Py_ssize_t size = PyList_GET_SIZE(obj.ptr());
            Py_ssize_t result = index + size * (index < 0);
            if (result < 0 || result >= size) {
                throw IndexError("list index out of range");
            }
            return result;
        }

    public:

        ItemAccessor(Handle obj, Py_ssize_t index) :
            initialized(false), obj(obj), index(index)
        {}

        ItemAccessor(const ItemAccessor& other) :
            initialized(other.initialized), obj(other.obj), index(other.index)
        {
            if (initialized) {
                new (buffer) Wrapped(reinterpret_cast<Wrapped&>(other.buffer));
            }
        }

        ItemAccessor(ItemAccessor&& other) noexcept :
            initialized(other.initialized), obj(other.obj), index(other.index)
        {
            other.initialized = false;
            if (initialized) {
                new (buffer) Wrapped(std::move(reinterpret_cast<Wrapped&>(other.buffer)));
            }
        }
    
        Wrapped& get() const {
            if (!initialized) {
                if (obj.ptr() == nullptr) {
                    throw ValueError(
                        "dereferencing an uninitialized accessor.  Either the accessor "
                        "was moved from or not properly constructed to begin with."
                    );
                }
                Py_ssize_t norm = normalized();
                PyObject* result = PyList_GET_ITEM(obj.ptr(), norm);
                if (result == nullptr) {
                    throw error_already_set();
                }
                new (buffer) Wrapped(reinterpret_steal<Wrapped>(result));
                initialized = true;
            }
            return reinterpret_cast<Wrapped&>(buffer);
        }

        template <typename T> requires (__setitem__<Obj, Key, T>::enable)
        void set(T&& value) {
            static_assert(
                std::is_void_v<typename __setitem__<Obj, Key, T>::Return>,
                "index assignment operator must return void.  Check your "
                "specialization of __setitem__ for these types and ensure the Return "
                "type is set to void."
            );
            Py_ssize_t norm = normalized();
            new (buffer) Wrapped(std::forward<T>(value));
            initialized = true;

            // Since type and index safety is guaranteed within this context, we can
            // avoid error checking and use PyList_SET_ITEM directly.  Note that this
            // steals a reference to the new object and does not clear the previous
            // value, so we need to account for that manually.
            PyObject* previous = PyList_GET_ITEM(obj.ptr(), norm);
            PyList_SET_ITEM(
                obj.ptr(),
                norm,
                Py_NewRef(reinterpret_cast<Wrapped&>(buffer).ptr())
            );
            Py_XDECREF(previous);
        }

        template <typename T = Key> requires (__delitem__<Obj, T>::enable)
        void del() {
            static_assert(
                std::is_void_v<typename __delitem__<Obj, T>::Return>,
                "index deletion operator must return void.  Check your specialization "
                "of __delitem__ for these types and ensure the Return type is set to "
                "void."
            );
            Py_ssize_t norm = normalized();
            if (PyObject_DelItem(
                obj.ptr(),
                pybind11::cast(norm).ptr()
            ) < 0) {
                throw error_already_set();
            }
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer).~Wrapped();
                initialized = false;
            }
        }

    };

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
    class StaticAccessor : PolicyTag {
        alignas (T) mutable unsigned char buffer[sizeof(T)];
        mutable bool initialized;

    public:
        using Wrapped = T;

        /* Default constructor. */
        StaticAccessor() : initialized(true) {
            new (buffer) Wrapped();
        }

        /* Forwarding copy constructor for wrapped type. */
        StaticAccessor(const Wrapped& value) : initialized(true) {
            new (buffer) Wrapped(value);
        }

        /* Forwarding move constructor for wrapped type. */
        StaticAccessor(Wrapped&& value) : initialized(true) {
            new (buffer) Wrapped(std::move(value));
        }

        /* Copy constructor. */
        StaticAccessor(const StaticAccessor& other) : initialized(other.initialized) {
            if (initialized) {
                new (buffer) Wrapped(reinterpret_cast<Wrapped&>(other.buffer));
            }
        }

        /* Move constructor. */
        StaticAccessor(StaticAccessor&& other) : initialized(other.initialized) {
            other.initialized = false;
            if (initialized) {
                new (buffer) Wrapped(std::move(reinterpret_cast<Wrapped&>(other.buffer)));
            }
        }

        /* Destructor only called if Py_IsInitialized() evalutes to true. */
        ~StaticAccessor() {
            if (initialized && Py_IsInitialized()) {
                reinterpret_cast<Wrapped&>(buffer).~Wrapped();
            }
        }

        Wrapped& get() const {
            if (!initialized) {
                throw ValueError(
                    "dereferencing an uninitialized accessor.  Either the accessor was "
                    "moved from or not properly constructed to begin with."
                );
            }
            return reinterpret_cast<Wrapped&>(buffer);
        }

        void set(const Wrapped& value) {
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer) = value;
            } else {
                new (buffer) Wrapped(value);
                initialized = true;
            }
        }

        void set(Wrapped&& value) {
            if (initialized) {
                reinterpret_cast<Wrapped&>(buffer) = std::move(value);
            } else {
                new (buffer) Wrapped(std::move(value));
                initialized = true;
            }
        }

        /* Copy assignment operator. */
        StaticAccessor& operator=(const StaticAccessor& other) {
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
        StaticAccessor& operator=(StaticAccessor&& other) {
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

    };

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

        /* Equivalent to Python `sequence + items`. */
        template <typename T>
        inline Derived concat(const T& items) const {
            PyObject* result = PySequence_Concat(
                self().ptr(),
                detail::object_or_cast(items).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Derived>(result);
        }

        /* Equivalent to Python `sequence * repetitions`. */
        inline Derived repeat(Py_ssize_t repetitions) const {
            PyObject* result = PySequence_Repeat(
                self().ptr(),
                repetitions
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Derived>(result);
        }

        template <typename T> requires (impl::__add__<Derived, T>::enable)
        inline auto operator+(const T& items) const
            -> typename impl::__add__<Derived, T>::Return
        {
            using Return = typename impl::__add__<Derived, T>::Return;
            static_assert(
                std::is_base_of_v<Derived, Return>,
                "concatenation operator must return a subclass of the left operand.  "
                "Check your specialization of __add__ for these types and ensure the "
                "Return type is compatible with the left operand."
            );
            return reinterpret_steal<Return>(self().concat(items).release());
        }

        template <typename T>
            requires (impl::__mul__<Derived, T>::enable && std::is_integral_v<T>)
        inline Derived operator*(T repetitions) {
            using Return = typename impl::__mul__<Derived, T>::Return;
            static_assert(
                std::is_base_of_v<Derived, Return>,
                "repetition operator must return a subclass of the left operand.  "
                "Check your specialization of __mul__ for these types and ensure the "
                "Return type is compatible with the left operand."
            );
            return reinterpret_steal<Return>(self().repeat(repetitions).release());
        }

        template <typename T>
            requires (impl::__mul__<T, Derived>::enable && std::is_integral_v<T>)
        friend inline Derived operator*(T repetitions, const Derived& seq) {
            using Return = typename impl::__mul__<T, Derived>::Return;
            static_assert(
                std::is_base_of_v<Derived, Return>,
                "repetition operator must return a subclass of the right operand.  "
                "Check your specialization of __mul__ for these types and ensure the "
                "Return type is compatible with the right operand."
            );
            return reinterpret_steal<Return>(seq.repeat(repetitions).release());
        }

        template <typename T> requires (impl::__iadd__<Derived, T>::enable)
        inline Derived& operator+=(const T& items) {
            static_assert(
                std::is_same_v<typename impl::__iadd__<Derived, T>::Return, Derived>,
                "in-place concatenation operator must return the same type as the left "
                "operand.  Check your specialization of __iadd__ for these types and "
                "ensure the Return type is set to the left operand."
            );
            PyObject* result = PySequence_InPlaceConcat(
                self().ptr(),
                detail::object_or_cast(items).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            if (result == self().ptr()) {
                Py_DECREF(result);
            } else {
                self() = reinterpret_steal<Derived>(result);
            }
            return self();
        }

        template <typename T>
            requires (impl::__imul__<Derived, T>::enable && std::is_integral_v<T>)
        inline Derived& operator*=(T repetitions) {
            static_assert(
                std::is_same_v<typename impl::__imul__<Derived, T>::Return, Derived>,
                "in-place repetition operator must return the same type as the left "
                "operand.  Check your specialization of __imul__ for these types and "
                "ensure the Return type is set to the left operand."
            );

            PyObject* result = PySequence_InPlaceRepeat(
                self().ptr(),
                repetitions
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            if (result == self().ptr()) {
                Py_DECREF(result);
            } else {
                self() = reinterpret_steal<Derived>(result);
            }
            return self();
        }

    };


    // TODO: Revisit with templated iterators


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

    /* Types for which is_reverse_iterable is true will use custom reverse iterators
    when py::reversed() is called on them.  These types must expose a ReverseIterator
    class that implements the reverse iteration. */
    template <typename T, typename = void>
    constexpr bool is_reverse_iterable = false;
    template <typename T>
    constexpr bool is_reverse_iterable<
        T, std::enable_if_t<std::is_base_of_v<Tuple, T>>
    > = true;
    template <typename T>
    constexpr bool is_reverse_iterable<
        T, std::enable_if_t<std::is_base_of_v<List, T>>
    > = true;

}  // namespace impl


// TODO: implement Static as an actual subclass of Proxy<>?  This would simplify error
// messages and might make it easier to implement the assignment operator.


template <typename T>
using Static = impl::Proxy<impl::StaticAccessor<T>>;


/* Object subclass that represents Python's global None singleton. */
class NoneType : public Object {
    using Base = Object;

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, NoneType, Py_IsNone)
    NoneType() : Base(Py_None, borrowed_t{}) {}
};


/* Object subclass that represents Python's global NotImplemented singleton. */
class NotImplementedType : public Object {
    using Base = Object;

    inline static int check_not_implemented(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) Py_TYPE(Py_NotImplemented));
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, NotImplementedType, check_not_implemented)
    NotImplementedType() : Base(Py_NotImplemented, borrowed_t{}) {}
};


/* Object subclass representing Python's global Ellipsis singleton. */
class EllipsisType : public Object {
    using Base = Object;

    inline static int check_ellipsis(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) Py_TYPE(Py_Ellipsis));
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, EllipsisType, check_ellipsis)
    EllipsisType() : Base(Py_Ellipsis, borrowed_t{}) {}
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

    template <typename T>
    static constexpr bool check() { return impl::slice_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Slice, PySlice_Check)

    /* Default constructor.  Initializes to all Nones. */
    Slice() : Base(PySlice_New(nullptr, nullptr, nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

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
        return this->attr("start");
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    inline Object stop() const {
        return this->attr("stop");
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    inline Object step() const {
        return this->attr("step");
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

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Module, PyModule_Check)

    /* Default module constructor deleted for clarity. */
    Module() = delete;

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
            pybind11::sibling(getattr(*this, name_, None)),
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
            result.attr("__doc__") = pybind11::str(doc);
        }
        attr(name) = result;
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

    // TODO: is create_extension_module() necessary?  It might be called by internal
    // pybind11 code, but I'm not sure.

    /* Equivalent to pybind11::module_::create_extension_module() */
    static Module create_extension_module(
        const char* name,
        const char* doc,
        PyModuleDef* def
    ) {
        return Module(name, doc, def);
    }

};


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <typename Key> requires (impl::str_like<Key>)
inline impl::Proxy<impl::AttrAccessor> Object::attr(Key&& key) const {
    return {*this, std::forward<Key>(key)};
}


template <typename T, typename Key>
inline auto Object::operator_getitem(const T& obj, Key&& key)
    -> impl::Proxy<impl::ItemAccessor<T, std::decay_t<Key>>>
{
    return {obj, std::forward<Key>(key)};
}


template <typename T>
inline auto Object::operator_getitem(
    const T& obj,
    std::initializer_list<impl::SliceInitializer> slice
) -> impl::Proxy<impl::ItemAccessor<T, Slice>>
{
    if (slice.size() > 3) {
        throw ValueError("slices must be of the form {[start[, stop[, step]]]}");
    }
    std::array<Object, 3> params {None, None, None};
    size_t i = 0;
    for (const impl::SliceInitializer& item : slice) {
        params[i++] = item.first;
    }
    return {obj, Slice(params[0], params[1], params[2])};
}


inline Iterator Object::operator_rbegin_impl() const {
    return reinterpret_steal<Iterator>(attr("__reversed__")().release());
}


template <typename Policy>
template <typename T> requires (impl::__getitem__<T, Slice>::enable)
inline auto impl::Proxy<Policy>::operator[](
    std::initializer_list<impl::SliceInitializer> slice
) const {
    return (**this)[slice];
}


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* Equivalent to Python `import module` */
inline Static<Module> import(const char* name) {
    // if (Py_IsInitialized()) {
        PyObject *obj = PyImport_ImportModule(name);
        if (obj == nullptr) {
            throw error_already_set();
        }
        return Static<Module>(reinterpret_steal<Module>(obj));
    // } else {
    //     return Static<Module>::alloc();  // return an empty wrapper
    // }
}


/* Equivalent to Python `iter(obj)` except that it can also accept C++ containers and
generate Python iterators over them.  Note that C++ types as rvalues are not allowed,
and will trigger a compiler error. */
template <impl::is_iterable T>
inline Iterator iter(T&& obj) {
    if constexpr (impl::pybind11_iterable<std::decay_t<T>>) {
        return pybind11::iter(obj);
    } else {
        static_assert(
            !std::is_rvalue_reference_v<decltype(obj)>,
            "passing an rvalue to py::iter() is unsafe"
        );
        return pybind11::make_iterator(obj.begin(), obj.end());
    }
}


/* Equivalent to Python `len(obj)`, but also accepts C++ types implementing a .size()
method.  Returns nullopt if the size could not be determined. */
template <typename T>
inline std::optional<size_t> len(const T& obj) {
    if constexpr (impl::python_like<T>) {
        try {
            return pybind11::len(obj);
        } catch (...) {
            return std::nullopt;
        }
    } else if constexpr (impl::has_size<T>) {
        return obj.size();
    } else {
        return std::nullopt;
    }
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


//////////////////////////////
////    STL EXTENSIONS    ////
//////////////////////////////


/* Bertrand overloads std::hash<> for all Python objects hashable so that they can be
 * used in STL containers like std::unordered_map and std::unordered_set.  This is
 * accomplished by overloading std::hash<> for the relevant types, which also allows
 * us to promote hash-not-implemented errors to compile-time.
 */


#define BERTRAND_STD_HASH(cls)                                                          \
namespace std {                                                                         \
    template <>                                                                         \
    struct hash<cls> {                                                                  \
        size_t operator()(const cls& obj) const {                                       \
            return pybind11::hash(obj);                                                 \
        }                                                                               \
    };                                                                                  \
}                                                                                       \


BERTRAND_STD_HASH(bertrand::py::Buffer)
BERTRAND_STD_HASH(bertrand::py::Bytearray)
BERTRAND_STD_HASH(bertrand::py::Bytes)
BERTRAND_STD_HASH(bertrand::py::Capsule)
BERTRAND_STD_HASH(bertrand::py::EllipsisType)
BERTRAND_STD_HASH(bertrand::py::Handle)
BERTRAND_STD_HASH(bertrand::py::Iterator)
BERTRAND_STD_HASH(bertrand::py::MemoryView)
BERTRAND_STD_HASH(bertrand::py::Module)
BERTRAND_STD_HASH(bertrand::py::NoneType)
BERTRAND_STD_HASH(bertrand::py::NotImplementedType)
BERTRAND_STD_HASH(bertrand::py::Object)
BERTRAND_STD_HASH(bertrand::py::WeakRef)


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
BERTRAND_STD_EQUAL_TO(bertrand::py::Iterator)
BERTRAND_STD_EQUAL_TO(bertrand::py::WeakRef)
BERTRAND_STD_EQUAL_TO(bertrand::py::Capsule)
BERTRAND_STD_EQUAL_TO(bertrand::py::Buffer)
BERTRAND_STD_EQUAL_TO(bertrand::py::MemoryView)
BERTRAND_STD_EQUAL_TO(bertrand::py::Bytes)
BERTRAND_STD_EQUAL_TO(bertrand::py::Bytearray)


#endif // BERTRAND_PYTHON_COMMON_H
