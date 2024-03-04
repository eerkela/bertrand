#ifndef BERTRAND_PYTHON_INCLUDED
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

    template <typename Base, typename Derived>
    constexpr bool is_same_or_subclass_of = (
        std::is_same_v<Base, Derived> || std::is_base_of_v<Base, Derived>
    );

    // template <typename From, typename To>
    // class ExplicitlyConvertibleTo {

    //     template <typename F, typename T>
    //     static auto test(void*) -> decltype(static_cast<T>(std::declval<F>()), std::true_type{});

    //     template <typename F, typename T>
    //     static auto test(...) -> std::false_type;

    // public:
    //     static constexpr bool value = decltype(test<From, To>(0))::value;
    // };

    // template <typename From, typename To, typename = void>
    // constexpr bool explicitly_convertible_to = false;

    template <typename From, typename To, typename = void>
    constexpr bool explicitly_convertible_to = false;
    template <typename From, typename To>
    constexpr bool explicitly_convertible_to<
        From, To, std::void_t<decltype(static_cast<To>(std::declval<From>()))>
    > = true;

    namespace conversions {

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
            static constexpr bool tuplelike = true;
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

    };

    template <typename T>
    constexpr bool is_python = detail::is_pyobject<T>::value;

    template <typename T>
    constexpr bool is_accessor = (
        is_same_or_subclass_of<detail::obj_attr_accessor, T> ||
        is_same_or_subclass_of<detail::str_attr_accessor, T> ||
        is_same_or_subclass_of<detail::item_accessor, T> ||
        is_same_or_subclass_of<detail::sequence_accessor, T> ||
        is_same_or_subclass_of<detail::tuple_accessor, T> ||
        is_same_or_subclass_of<detail::list_accessor, T>
    );

    template <typename T>
    constexpr bool is_bool_like = (
        is_same_or_subclass_of<bool, T> ||
        is_same_or_subclass_of<Bool, T> ||
        is_same_or_subclass_of<pybind11::bool_, T>
    );

    template <typename T>
    constexpr bool is_int_like = (
        (std::is_integral_v<T> && !std::is_same_v<T, bool>) ||
        is_same_or_subclass_of<Int, T> ||
        is_same_or_subclass_of<pybind11::int_, T>
    );

    template <typename T>
    constexpr bool is_float_like = (
        std::is_floating_point_v<T> ||
        is_same_or_subclass_of<Float, T> ||
        is_same_or_subclass_of<pybind11::float_, T>
    );

    template <typename T>
    constexpr bool is_complex_like = (
        is_same_or_subclass_of<std::complex<float>, T> ||
        is_same_or_subclass_of<std::complex<double>, T> ||
        is_same_or_subclass_of<std::complex<long double>, T> ||
        is_same_or_subclass_of<Complex, T>
    );

    template <typename T>
    constexpr bool is_str_like = (
        std::is_same_v<const char*, T> ||
        is_same_or_subclass_of<std::string, T> ||
        is_same_or_subclass_of<std::string_view, T> ||
        is_same_or_subclass_of<Str, T> ||
        is_same_or_subclass_of<pybind11::str, T>
    );

    template <typename T>
    constexpr bool is_timedelta_like = (
        conversions::Traits<T>::timedeltalike ||
        is_same_or_subclass_of<Timedelta, T>
    );

    template <typename T>
    constexpr bool is_timezone_like = (
        conversions::Traits<T>::timezonelike ||
        is_same_or_subclass_of<Timezone, T>
    );

    template <typename T>
    constexpr bool is_date_like = (
        conversions::Traits<T>::datelike ||
        is_same_or_subclass_of<Date, T>
    );

    template <typename T>
    constexpr bool is_time_like = (
        conversions::Traits<T>::timelike ||
        is_same_or_subclass_of<Time, T>
    );

    template <typename T>
    constexpr bool is_datetime_like = (
        conversions::Traits<T>::datetimelike ||
        is_same_or_subclass_of<Datetime, T>
    );

    template <typename T>
    constexpr bool is_slice_like = (
        is_same_or_subclass_of<Slice, T> ||
        is_same_or_subclass_of<pybind11::slice, T>
    );

    template <typename T>
    constexpr bool is_range_like = (
        is_same_or_subclass_of<Range, T>
    );

    template <typename T>
    constexpr bool is_tuple_like = (
        conversions::Traits<T>::tuplelike ||
        is_same_or_subclass_of<Tuple, T> ||
        is_same_or_subclass_of<pybind11::tuple, T>
    );

    template <typename T>
    constexpr bool is_list_like = (
        conversions::Traits<T>::listlike ||
        is_same_or_subclass_of<List, T> ||
        is_same_or_subclass_of<pybind11::list, T>
    );

    template <typename T>
    constexpr bool is_set_like = (
        conversions::Traits<T>::setlike ||
        is_same_or_subclass_of<Set, T> ||
        is_same_or_subclass_of<pybind11::set, T>
    );

    template <typename T>
    constexpr bool is_frozenset_like = (
        conversions::Traits<T>::setlike ||
        is_same_or_subclass_of<FrozenSet, T> ||
        is_same_or_subclass_of<pybind11::frozenset, T>
    );

    template <typename T>
    constexpr bool is_anyset_like = is_set_like<T> || is_frozenset_like<T>;

    template <typename T>
    constexpr bool is_dict_like = (
        conversions::Traits<T>::dictlike ||
        is_same_or_subclass_of<Dict, T> ||
        is_same_or_subclass_of<pybind11::dict, T>
    );

    template <typename T>
    constexpr bool is_mappingproxy_like = (
        conversions::Traits<T>::dictlike ||
        is_same_or_subclass_of<MappingProxy, T>
    );

    template <typename T>
    constexpr bool is_anydict_like = is_dict_like<T> || is_mappingproxy_like<T>;

    template <typename T>
    constexpr bool is_type_like = (
        is_same_or_subclass_of<Type, T> ||
        is_same_or_subclass_of<pybind11::type, T>
    );

    template <typename T, typename = void>
    constexpr bool has_size = false;
    template <typename T>
    constexpr bool has_size<
        T, std::void_t<decltype(std::declval<T>().size())>
    > = true;

    template <typename T, typename = void>
    constexpr bool has_empty = false;
    template <typename T>
    constexpr bool has_empty<
        T, std::void_t<decltype(std::declval<T>().empty())>
    > = true;

    template <typename T, typename = void>
    constexpr bool is_hashable = false;
    template <typename T>
    constexpr bool is_hashable<
        T, std::void_t<decltype(std::hash<T>{}(std::declval<T>()))>
    > = true;

    template <typename T, typename = void>
    constexpr bool is_iterable = false;
    template <typename T>
    constexpr bool is_iterable<
        T, std::void_t<decltype(std::begin(std::declval<T>()), std::end(std::declval<T>()))>
    > = true;

    template <typename T, typename = void>
    constexpr bool has_to_string = false;
    template <typename T>
    constexpr bool has_to_string<
        T, std::void_t<decltype(std::to_string(std::declval<T>()))>
    > = true;

    template <typename T, typename = void>
    constexpr bool has_stream_insertion = false;
    template <typename T>
    constexpr bool has_stream_insertion<
        T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<T>())>
    > = true;

    template <typename T, typename = void>
    constexpr bool has_reserve = false;
    template <typename T>
    constexpr bool has_reserve<
        T, std::void_t<decltype(std::declval<T>().reserve(std::declval<size_t>()))>
    > = true;

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

    /* SFINAE condition allows py::iter() to work on both python and C++ types. */
    template <typename T, typename = void>
    constexpr bool pybind11_iterable = false;
    template <typename T>
    constexpr bool pybind11_iterable<
        T, std::void_t<decltype(pybind11::iter(std::declval<T>()))>
    > = true;

    template <typename T, typename = void>
    struct OverloadsCallable : std::false_type {};
    template <typename T>
    struct OverloadsCallable<T, std::void_t<decltype(&T::operator())>> :
        std::true_type
    {};

    /* SFINAE condition is used to recognize callable C++ types without regard to their
    argument signatures. */
    template <typename T>
    static constexpr bool is_callable_any = std::disjunction_v<
        std::is_function<std::remove_pointer_t<std::decay_t<T>>>,
        std::is_member_function_pointer<std::decay_t<T>>,
        OverloadsCallable<std::decay_t<T>>
    >;

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
        template <typename T = Func, std::enable_if_t<!impl::is_python<T>, int> = 0>
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
        template <typename T = Func, std::enable_if_t<impl::is_python<T>, int> = 0>
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
        template <typename T = Func, std::enable_if_t<!impl::is_python<T>, int> = 0>
        inline constexpr operator bool() const {
            return is_callable_any<Func>;
        }

        /* Implicitly convert the tag to a runtime bool. */
        template <typename T = Func, std::enable_if_t<impl::is_python<T>, int> = 0>
        inline operator bool() const {
            return PyCallable_Check(this->func.ptr());
        }

    };

    /* Tag class to identify wrappers during SFINAE checks. */
    struct WrapperTag {};

    template <typename T>
    constexpr bool is_wrapper = std::is_base_of_v<WrapperTag, T>;

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

    #define CONVERTIBLE_ACCESSOR(name, base)                                            \
        struct name : public detail::base {                                             \
            using detail::base::base;                                                   \
            using detail::base::operator=;                                              \
            name(const detail::base& accessor) : detail::base(accessor) {}              \
            name(detail::base&& accessor) : detail::base(std::move(accessor)) {}        \
                                                                                        \
            template <typename... Args>                                                 \
            inline Object operator()(Args&&... args) const;                             \
                                                                                        \
            inline explicit operator bool() const {                                     \
                int result = PyObject_IsTrue(this->ptr());                              \
                if (result == -1) {                                                     \
                    throw error_already_set();                                          \
                }                                                                       \
                return result;                                                          \
            }                                                                           \
                                                                                        \
            inline explicit operator std::string() const {                              \
                PyObject* str = PyObject_Str(this->ptr());                              \
                if (str == nullptr) {                                                   \
                    throw error_already_set();                                          \
                }                                                                       \
                Py_ssize_t size;                                                        \
                const char* data = PyUnicode_AsUTF8AndSize(str, &size);                 \
                if (data == nullptr) {                                                  \
                    Py_DECREF(str);                                                     \
                    throw error_already_set();                                          \
                }                                                                       \
                std::string result(data, size);                                         \
                Py_DECREF(str);                                                         \
                return result;                                                          \
            }                                                                           \
                                                                                        \
            template <typename T, std::enable_if_t<impl::is_wrapper<T>, int> = 0>       \
            inline operator T() const {                                                 \
                return T(this->operator typename T::Wrapped());                         \
            }                                                                           \
                                                                                        \
            template <                                                                  \
                typename T,                                                             \
                std::enable_if_t<!impl::is_wrapper<T> && !impl::is_python<T>, int> = 0  \
            >                                                                           \
            inline operator T() const {                                                 \
                return detail::base::template cast<T>();                                \
            }                                                                           \
                                                                                        \
            template <                                                                  \
                typename T,                                                             \
                std::enable_if_t<                                                       \
                    impl::is_same_or_subclass_of<pybind11::object, T>,                  \
                int> = 0                                                                \
            >                                                                           \
            inline operator T() const {                                                 \
                pybind11::object other(*this);                                          \
                if (!T::check(other)) {                                                 \
                    throw impl::noconvert<T>(this->ptr());                              \
                }                                                                       \
                return reinterpret_steal<T>(other.release());                           \
            }                                                                           \
        };                                                                              \

    CONVERTIBLE_ACCESSOR(ObjAttrAccessor, obj_attr_accessor)
    CONVERTIBLE_ACCESSOR(StrAttrAccessor, str_attr_accessor)
    CONVERTIBLE_ACCESSOR(ItemAccessor, item_accessor)
    CONVERTIBLE_ACCESSOR(SequenceAccessor, sequence_accessor)
    CONVERTIBLE_ACCESSOR(TupleAccessor, tuple_accessor)
    CONVERTIBLE_ACCESSOR(ListAccessor, list_accessor)

    #undef CONVERTIBLE_ACCESSOR

    class SliceInitializer;

}


/* A revised pybind11::object interface that allows implicit conversions to subtypes
(applying a type check on the way), explicit conversions to arbitrary C++ types via
static_cast<>, cross-language math operators, and generalized slice/attr syntax. */
class Object : public pybind11::object {
    using Base = pybind11::object;

public:
    static Type type;

    /* Check whether a templated type is considered object-like at compile time. */
    template <typename T>
    static constexpr bool check() {
        return impl::is_same_or_subclass_of<pybind11::object, T>;
    }

    /* Check whether a C++ value is considered object-like at compile time. */
    template <typename T, std::enable_if_t<!impl::is_python<T>, int> = 0>
    static constexpr bool check(const T& value) {
        return check<T>();
    }

    /* Check whether a Python value is considered object-like at runtime. */
    template <typename T, std::enable_if_t<impl::is_python<T>, int> = 0>
    static constexpr bool check(const T& value) {
        return value.ptr() != nullptr;
    }

    /* For compatibility with pybind11, which expects these methods. */
    template <typename T, std::enable_if_t<!impl::is_python<T>, int> = 0>
    static constexpr bool check_(const T& value) {
        return check(value);
    }
    template <typename T, std::enable_if_t<impl::is_python<T>, int> = 0>
    static constexpr bool check_(const T& value) {
        return check(value);
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    using Base::Base;
    using Base::operator=;

    /* Default constructor.  Initializes to None. */
    Object() : Base(pybind11::none()) {}

    /* Copy constructor.  Borrows a reference to an existing python object. */
    Object(const pybind11::object& o) : Base(o.ptr(), borrowed_t{}) {}

    /* Move constructor.  Steals a reference to a rvalue python object. */
    Object(pybind11::object&& o) : Base(o.release(), stolen_t{}) {}

    /* Convert an accessor into a generic Object. */
    template <typename Policy>
    Object(const detail::accessor<Policy> &a) : Base(pybind11::object(a)) {}

    /* Convert any non-callable C++ value into a generic python object. */
    template <
        typename T,
        std::enable_if_t<!impl::is_python<T> && !impl::is_callable_any<T>, int> = 0
    >
    Object(const T& value) : Base(pybind11::cast(value).release(), stolen_t{}) {}

    /* Convert any callable C++ value into a generic python object. */
    template <
        typename T,
        std::enable_if_t<!impl::is_python<T> && impl::is_callable_any<T>, int> = 0
    >
    Object(const T& value);

    /* Assign any C++ value to the object wrapper. */
    template <typename T, std::enable_if_t<!impl::is_python<T>, int> = 0>
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
     * type mismatches or triggering non-trivial conversion logic.  It allows us to
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
     * between subclasses and promotes any attempt to do so from a runtime error into a
     * compile-time one, which is significantly safer and easier to debug.  For
     * instance:
     *
     *      py::Bool b = true;
     *      py::Str s = b;  // fails to compile, calls a deleted function
     *
     * In general, this promotes the rule that assignment is type safe by default,
     * while explicit constructors are reserved for type conversions and/or packing in
     * the case of containers.
     */

    /* Implicitly convert an Object wrapper to one of its subclasses, applying a
    runtime type check to the underlying value. */
    template <typename T, std::enable_if_t<std::is_base_of_v<Object, T>, int> = 0>
    inline operator T() const {
        if (!T::check(*this)) {
            throw impl::noconvert<T>(this->ptr());
        }
        return reinterpret_borrow<T>(this->ptr());
    }

    /* Implicitly convert an Object wrapper to a wrapper class, which moves it into a
    customizable buffer for static storage duration, etc. */
    template <typename T, std::enable_if_t<impl::is_wrapper<T>, int> = 0>
    inline operator T() const {
        return T(this->operator typename T::Wrapped());
    }

    /* Explicitly convert to any other non-Object type using pybind11 to search for a
    matching type caster. */
    template <
        typename T,
        std::enable_if_t<!impl::is_wrapper<T> && !std::is_base_of_v<Object, T>, int> = 0
    >
    inline explicit operator T() const {
        return Base::cast<T>();
    }

    /* Contextually convert an Object into a boolean for use in if/else statements,
    with the same truthiness semantics as Python. */
    inline explicit operator bool() const {
        int result = PyObject_IsTrue(this->ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Explicitly cast to a string representation.  For some reason,
    pybind11::cast<std::string>() doesn't always work for arbitrary types.  This
    corrects that, giving the same results as Python `str(obj)`. */
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

    ////////////////////////////////
    ////    ATTRIBUTE ACCESS    ////
    ////////////////////////////////

    /* Bertrand streamlines pybind11's dotted attribute accessors by allowing them to
     * be implicitly converted to any C++ type or Python type, reducing the amount of
     * boilerplate needed to interact with Python objects in C++.  This brings them in
     * line with the generic Object API, and makes the code significantly more idiomatic
     * from both a Python and C++ perspective.
     */

    inline impl::ObjAttrAccessor attr(Handle key) const {
        return Base::attr(key);
    }

    inline impl::ObjAttrAccessor attr(Handle&& key) const {
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

    /////////////////////////////
    ////    CALL OPERATOR    ////
    /////////////////////////////

    /* Bertrand doesn't change the semantics of pybind11's call operator, but it does
     * expand the number of types that can be implicitly converted as arguments, and
     * similarly converts the return value into a type-safe Object wrapper rather than
     * the generic pybind11::object.  This makes the call operator more type-safe and
     * idiomatic in both languages.
     */

    template <typename... Args>
    inline Object operator()(Args&&... args) const;

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* Bertrand also simplifies pybind11's index interface by allowing accessors to be
     * implicitly converted to any C++ type.  In addition, it implements a generalized
     * slice syntax for sequence types using an initializer list to represent the
     * slice.  This is also type safe, and will raise compile errors if a slice is
     * constructed with any type other than integers or None.
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

    impl::ItemAccessor operator[](
        const std::initializer_list<impl::SliceInitializer>& slice
    ) const;

    template <typename T, std::enable_if_t<!impl::is_python<T>, int> = 0>
    inline impl::ItemAccessor operator[](const T& key) const {
        return (*this)[detail::object_or_cast(key)];
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* pybind11 exposes generalized operator overloads for python operands, but does
    * not allow mixed Python/C++ inputs.  For ease of use, we enable them here, along
    * with reverse operators and in-place equivalents.
    */

    #define UNARY_OPERATOR(op, endpoint)                                                \
        inline Object op() const {                                                      \
            PyObject* result = endpoint(this->ptr());                                   \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            return reinterpret_steal<Object>(result);                                   \
        }                                                                               \

    #define COMPARISON_OPERATOR(op, endpoint)                                           \
        template <typename T>                                                           \
        inline bool op(const T& other) const {                                          \
            int result = PyObject_RichCompareBool(                                      \
                this->ptr(),                                                            \
                detail::object_or_cast(other).ptr(),                                    \
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
                !impl::is_same_or_subclass_of<Object, T> &&                             \
                !impl::is_std_iterator<T>,                                              \
            int> = 0                                                                    \
        >                                                                               \
        inline friend bool op(const T& other, const Object& self) {                     \
            int result = PyObject_RichCompareBool(                                      \
                detail::object_or_cast(other).ptr(),                                    \
                self.ptr(),                                                             \
                endpoint                                                                \
            );                                                                          \
            if (result == -1) {                                                         \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        }                                                                               \

    #define BINARY_OPERATOR(op, endpoint)                                               \
        template <typename T>                                                           \
        inline Object op(const T& other) const {                                        \
            PyObject* result = endpoint(                                                \
                this->ptr(),                                                            \
                detail::object_or_cast(other).ptr()                                     \
            );                                                                          \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            return reinterpret_steal<Object>(result);                                   \
        }                                                                               \

    #define REVERSE_OPERATOR(op, endpoint)                                              \
        template <                                                                      \
            typename T,                                                                 \
            std::enable_if_t<                                                           \
                !impl::is_same_or_subclass_of<Object, T> &&                             \
                !impl::is_same_or_subclass_of<std::ostream, T> &&                       \
                !impl::is_std_iterator<T>,                                              \
            int> = 0                                                                    \
        >                                                                               \
        inline friend Object op(const T& other, const Object& self) {                   \
            PyObject* result = endpoint(                                                \
                detail::object_or_cast(other).ptr(),                                    \
                self.ptr()                                                              \
            );                                                                          \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            return reinterpret_steal<Object>(result);                                   \
        }                                                                               \

    #define INPLACE_OPERATOR(op, endpoint)                                              \
        template <typename T>                                                           \
        inline Object& op(const T& other) {                                             \
            pybind11::object o = detail::object_or_cast(other);                         \
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

    UNARY_OPERATOR(operator+, PyNumber_Positive);
    UNARY_OPERATOR(operator-, PyNumber_Negative);
    UNARY_OPERATOR(operator~, PyNumber_Invert);
    COMPARISON_OPERATOR(operator<, Py_LT);
    COMPARISON_OPERATOR(operator<=, Py_LE);
    COMPARISON_OPERATOR(operator==, Py_EQ);
    COMPARISON_OPERATOR(operator!=, Py_NE);
    COMPARISON_OPERATOR(operator>=, Py_GE);
    COMPARISON_OPERATOR(operator>, Py_GT);
    REVERSE_COMPARISON(operator<, Py_LT);
    REVERSE_COMPARISON(operator<=, Py_LE);
    REVERSE_COMPARISON(operator==, Py_EQ);
    REVERSE_COMPARISON(operator!=, Py_NE);
    REVERSE_COMPARISON(operator>=, Py_GE);
    REVERSE_COMPARISON(operator>, Py_GT);
    BINARY_OPERATOR(operator+, PyNumber_Add);
    BINARY_OPERATOR(operator-, PyNumber_Subtract);
    BINARY_OPERATOR(operator*, PyNumber_Multiply);
    BINARY_OPERATOR(operator/, PyNumber_TrueDivide);
    BINARY_OPERATOR(operator%, PyNumber_Remainder);
    BINARY_OPERATOR(operator<<, PyNumber_Lshift);
    BINARY_OPERATOR(operator>>, PyNumber_Rshift);
    BINARY_OPERATOR(operator&, PyNumber_And);
    BINARY_OPERATOR(operator|, PyNumber_Or);
    BINARY_OPERATOR(operator^, PyNumber_Xor);
    REVERSE_OPERATOR(operator+, PyNumber_Add);
    REVERSE_OPERATOR(operator-, PyNumber_Subtract);
    REVERSE_OPERATOR(operator*, PyNumber_Multiply);
    REVERSE_OPERATOR(operator/, PyNumber_TrueDivide);
    REVERSE_OPERATOR(operator%, PyNumber_Remainder);
    REVERSE_OPERATOR(operator<<, PyNumber_Lshift);
    REVERSE_OPERATOR(operator>>, PyNumber_Rshift);
    REVERSE_OPERATOR(operator&, PyNumber_And);
    REVERSE_OPERATOR(operator|, PyNumber_Or);
    REVERSE_OPERATOR(operator^, PyNumber_Xor);
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

    #undef UNARY_OPERATOR
    #undef COMPARISON_OPERATOR
    #undef REVERSE_COMPARISON
    #undef BINARY_OPERATOR
    #undef REVERSE_OPERATOR
    #undef INPLACE_OPERATOR

    inline Object* operator&() {
        return this;
    }

    inline const Object* operator&() const {
        return this;
    }

    inline auto operator*() {
        return Base::operator*();
    }

    inline auto operator*() const {
        return Base::operator*();
    }

};


/* Stream an Object to obtain its Python `repr()`. */
inline std::ostream& operator<<(std::ostream& os, const Object& obj) {
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


class NoneType;
namespace impl {

    /* A simple struct that converts a generic C++ object into a Python equivalent in
    its constructor.  This is used in conjunction with std::initializer_list to parse
    mixed-type lists in a type-safe manner. */
    struct Initializer {
        Object first;
        template <
            typename T,
            std::enable_if_t<!std::is_same_v<std::decay_t<T>, Handle>, int> = 0
        >
        Initializer(T&& value) : first(std::forward<T>(value)) {}
    };

    /* An Initializer that explicitly requires a string argument. */
    struct StringInitializer : Initializer {
        template <
            typename T,
            std::enable_if_t<impl::is_str_like<std::decay_t<T>>, int> = 0
        >
        StringInitializer(T&& value) : Initializer(std::forward<T>(value)) {}
    };

    /* An initializer that explicitly requires an integer or None. */
    struct SliceInitializer : Initializer {
        template <
            typename T,
            std::enable_if_t<
                impl::is_int_like<std::decay_t<T>> ||
                std::is_same_v<std::decay_t<T>, NoneType>,
            int> = 0
        >
        SliceInitializer(T&& value) : Initializer(std::forward<T>(value)) {}
    };

    /* An Initializer that converts its argument to a python object and asserts that it
    is hashable, for static analysis. */
    struct HashInitializer : Initializer {
        template <
            typename K,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<K>, Handle> &&
                impl::is_hashable<std::decay_t<K>>,
            int> = 0
        >
        HashInitializer(K&& key) : Initializer(std::forward<K>(key)) {}
    };

    /* A hashed Initializer that also stores a second item for dict-like access. */
    struct DictInitializer : Initializer {
        Object second;
        template <
            typename K,
            typename V,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<K>, Handle> &&
                !std::is_same_v<std::decay_t<V>, Handle> &&
                impl::is_hashable<std::decay_t<K>>,
            int> = 0
        >
        DictInitializer(K&& key, V&& value) :
            Initializer(std::forward<K>(key)), second(std::forward<V>(value))
        {}
    };

    template <typename T>
    constexpr bool is_initializer = std::is_base_of_v<Initializer, T>;

    /* A mixin class for transparent Object<> wrappers that forwards the basic interface. */
    template <typename T>
    class Wrapper : WrapperTag {
        static_assert(
            std::is_base_of_v<pybind11::object, T>,
            "Wrapper<T> requires T to be a subclass of pybind11::object"
        );

    protected:
        bool initialized;
        alignas (T) unsigned char buffer[sizeof(T)];

        struct alloc_t {};
        Wrapper(const alloc_t&) : initialized(false) {}

    public:
        using Wrapped = T;

        /* Explicitly create an empty wrapper with uninitialized memory. */
        inline static Wrapper alloc() {
            return Wrapper(alloc_t{});
        }

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Default constructor. */
        Wrapper() : initialized(true) {
            new (buffer) T();
        }

        /* Copy constructor. */
        Wrapper(const Wrapper& other) : initialized(true) {
            new (buffer) T(*other);
        }

        /* Move constructor. */
        Wrapper(Wrapper&& other) : initialized(true) {
            new (buffer) T(std::move(*other));
        }

        /* Forwarding copy constructor for wrapped type. */
        Wrapper(const T& other) : initialized(true) {
            new (buffer) T(other);
        }

        /* Forwarding move constructor for wrapped type. */
        Wrapper(T&& other) : initialized(true) {
            new (buffer) T(std::move(other));
        }

        /* Forwarding assignment operator. */
        template <typename U>
        Wrapper& operator=(U&& other) {
            **this = std::forward<U>(other);
            return *this;
        }

        /* Copy assignment operator. */
        Wrapper& operator=(const Wrapper& other) {
            if (&other != this) {
                bool old_initialized = initialized;
                bool new_initialized = other.initialized;
                T& old_wrapped = **this;
                const T& new_wrapped = *other;

                initialized = false;  // prevent this dereference during copy
                if (old_initialized) {
                    old_wrapped.~T();
                }

                if (new_initialized) {
                    new (buffer) T(new_wrapped);
                    initialized = true;  // allow this dereference
                }
            }
            return *this;
        }

        /* Move assignment operator. */
        Wrapper& operator=(Wrapper&& other) {
            if (&other != this) {
                bool old_initialized = initialized;
                bool new_initialized = other.initialized;
                T& old_wrapped = **this;
                T& new_wrapped = *other;

                initialized = false;  // prevent this dereference during move
                if (old_initialized) {
                    old_wrapped.~T();
                }

                if (new_initialized) {
                    other.initialized = false;  // prevent other dereference
                    new (buffer) T(std::move(new_wrapped));
                    initialized = true;  // allow this dereference
                }
            }
            return *this;
        }

        /* Destructor.  Subclasses can avoid calling this by manually clearing the
        `initialized` flag in their own destructor. */
        ~Wrapper() {
            if (initialized) {
                (**this).~T();
            }
        }

        /* Implicitly convert to the wrapped type. */
        inline operator T&() {
            return **this;
        }

        /* Implicitly convert to the wrapped type. */
        inline operator const T&() const {
            return **this;
        }

        /* Implicitly convert to the wrapped type. */
        inline operator T() {
            return **this;
        }

private:

        /* Helper function triggers implicit conversion operators and/or implicit
        constructors, but not explicit ones.  In contrast, static_cast<>() will trigger
        explicit constructors on the target type, which can give unexpected results and
        violate bertrand's strict type safety. */
        template <typename U>
        inline static decltype(auto) implicit_cast(U&& value) {
            return std::forward<U>(value);
        }

public:

        /* Forward all other implicit conversions to the wrapped type. */
        template <
            typename U,
            std::enable_if_t<
                !std::is_same_v<T, U> && std::is_convertible_v<T, U>,
            int> = 0
        >
        inline operator U() const {
            return implicit_cast<U>(**this);
        }

        /* Forward all other explicit conversions to the wrapped type. */
        template <
            typename U,
            std::enable_if_t<
                !std::is_same_v<T, U> && !std::is_convertible_v<T, U>,
            int> = 0
        >
        inline explicit operator U() const {
            return static_cast<U>(**this);
        }

        ////////////////////////////////////
        ////    FORWARDING INTERFACE    ////
        ////////////////////////////////////

        /* Wrappers can be used more or less just like a regular object, except that any
        * instance of the dot operator (.) should be replaced with the arrow operator (->)
        * instead.  This allows them to be treated like a pointer conceptually, except all
        * of the ordinary object operators are also forwarded at the same time.
        */

        /* Dereference to get the underlying object. */
        inline T& operator*() {
            if (initialized) {
                return reinterpret_cast<T&>(buffer);
            } else {
                throw ValueError(
                    "dereferencing an uninitialized wrapper.  Either the object was moved "
                    "from or not properly constructed to begin with."
                );
            }
        }

        /* Dereference to get the underlying object. */
        inline const T& operator*() const {
            if (initialized) {
                return reinterpret_cast<const T&>(buffer);
            } else {
                throw ValueError(
                    "dereferencing an uninitialized wrapper.  Either the object was moved "
                    "from or not properly constructed to begin with."
                );
            }
        }

        /* Use the arrow operator to access an attribute on the object. */
        inline T* operator->() {
            return &(**this);
        }

        /* Use the arrow operator to access an attribute on the object. */
        inline const T* operator->() const {
            return &(**this);
        }

        /* Forward to the object's call operator. */
        template <typename... Args>
        inline auto operator()(Args&&... args) const {
            return (**this)(std::forward<Args>(args)...);
        }

        /* Forward to the object's index operator. */
        template <typename U>
        inline auto operator[](U&& args) const {
            return (**this)[std::forward<U>(args)];
        }

        /* Forward to the object's iterator methods. */
        inline auto begin() const { return (**this).begin(); }
        inline auto end() const { return (**this).end(); }

        /////////////////////////
        ////    OPERATORS    ////
        /////////////////////////

        /* We also have to forward all math operators for consistent behavior. */

        #define UNARY_OPERATOR(opcode, op)                                              \
            inline auto opcode() const { return op(**this); }                           \

        #define BINARY_OPERATOR(opcode, op)                                             \
            template <typename U>                                                       \
            inline auto opcode(const U& other) const { return (**this) op other; }      \

        #define REVERSE_OPERATOR(opcode, op)                                            \
            template <                                                                  \
                typename U,                                                             \
                std::enable_if_t<                                                       \
                    !impl::is_same_or_subclass_of<Object, U> &&                         \
                    !impl::is_same_or_subclass_of<std::ostream, U> &&                   \
                    !impl::is_std_iterator<U>,                                          \
                int> = 0                                                                \
            >                                                                           \
            inline friend auto opcode(const U& other, const Wrapper& self) {            \
                return other op *self;                                                  \
            }                                                                           \

        #define INPLACE_OPERATOR(opcode, op)                                            \
            template <typename U>                                                       \
            inline Wrapper& opcode(const U& other) {                                    \
                **this op other;                                                        \
                return *this;                                                           \
            }                                                                           \

        UNARY_OPERATOR(operator~, ~)
        UNARY_OPERATOR(operator+, +)
        UNARY_OPERATOR(operator-, -)
        BINARY_OPERATOR(operator<, <)
        BINARY_OPERATOR(operator<=, <=)
        BINARY_OPERATOR(operator==, ==)
        BINARY_OPERATOR(operator!=, !=)
        BINARY_OPERATOR(operator>=, >=)
        BINARY_OPERATOR(operator>, >)
        REVERSE_OPERATOR(operator<, <)
        REVERSE_OPERATOR(operator<=, <=)
        REVERSE_OPERATOR(operator==, ==)
        REVERSE_OPERATOR(operator!=, !=)
        REVERSE_OPERATOR(operator>=, >=)
        REVERSE_OPERATOR(operator>, >)
        BINARY_OPERATOR(operator+, +)
        BINARY_OPERATOR(operator-, -)
        BINARY_OPERATOR(operator*, *)
        BINARY_OPERATOR(operator/, /)
        BINARY_OPERATOR(operator%, %)
        BINARY_OPERATOR(operator<<, <<)
        BINARY_OPERATOR(operator>>, >>)
        BINARY_OPERATOR(operator&, &)
        BINARY_OPERATOR(operator|, |)
        BINARY_OPERATOR(operator^, ^)
        REVERSE_OPERATOR(operator+, +)
        REVERSE_OPERATOR(operator-, -)
        REVERSE_OPERATOR(operator*, *)
        REVERSE_OPERATOR(operator/, /)
        REVERSE_OPERATOR(operator%, %)
        REVERSE_OPERATOR(operator<<, <<)
        REVERSE_OPERATOR(operator>>, >>)
        REVERSE_OPERATOR(operator&, &)
        REVERSE_OPERATOR(operator|, |)
        REVERSE_OPERATOR(operator^, ^)
        INPLACE_OPERATOR(operator+=, +=)
        INPLACE_OPERATOR(operator-=, -=)
        INPLACE_OPERATOR(operator*=, *=)
        INPLACE_OPERATOR(operator/=, /=)
        INPLACE_OPERATOR(operator%=, %=)
        INPLACE_OPERATOR(operator<<=, <<=)
        INPLACE_OPERATOR(operator>>=, >>=)
        INPLACE_OPERATOR(operator&=, &=)
        INPLACE_OPERATOR(operator|=, |=)
        INPLACE_OPERATOR(operator^=, ^=)

        #undef UNARY_OPERATOR
        #undef BINARY_OPERATOR
        #undef REVERSE_OPERATOR
        #undef INPLACE_OPERATOR

        inline Wrapper* operator&() {
            return this;
        }

        inline const Wrapper* operator&() const {
            return this;
        }

    };

    /* Intermediate base class that hides all operators from the generic Object class.
    Subclasses have to bring these back into scope via a `using` statement or a custom
    overload as needed. */
    struct Ops : public Object {
        using Object::Object;
        using Object::operator=;
        using Object::operator==;
        using Object::operator!=;

        inline Ops* operator&() {
            return this;
        }

        inline const Ops* operator&() const {
            return this;
        }

        inline auto operator*() {
            return Object::operator*();
        }

        inline auto operator*() const {
            return Object::operator*();
        }
    
        template <typename T, std::enable_if_t<std::is_base_of_v<Object, T>, int> = 0>
        operator T() const = delete;

        template <typename T, std::enable_if_t<impl::is_wrapper<T>, int> = 0>
        inline operator T() const {
            return Object::operator T();
        }

        template <
            typename T,
            std::enable_if_t<!impl::is_wrapper<T> && !std::is_base_of_v<Object, T>, int> = 0
        >
        inline explicit operator T() const {
            return Object::operator T();
        }

        inline explicit operator bool() const {
            return Object::operator bool();
        }

        inline explicit operator std::string() const {
            return Object::operator std::string();
        }

    protected:
        using Object::operator~;
        using Object::operator<;
        using Object::operator<=;
        using Object::operator>=;
        using Object::operator>;
        using Object::operator+;
        using Object::operator-;
        using Object::operator*;
        using Object::operator/;
        using Object::operator%;
        using Object::operator<<;
        using Object::operator>>;
        using Object::operator&;
        using Object::operator|;
        using Object::operator^;
        using Object::operator+=;
        using Object::operator-=;
        using Object::operator*=;
        using Object::operator/=;
        using Object::operator%=;
        using Object::operator<<=;
        using Object::operator>>=;
        using Object::operator&=;
        using Object::operator|=;
        using Object::operator^=;
        using Object::operator[];
        using Object::operator();
        using Object::begin;
        using Object::end;
    };

    /* Mixin holding operator overloads for types implementing the sequence protocol,
    which makes them both concatenatable and repeatable. */
    struct SequenceOps : public Ops {
        using Ops::Ops;
        using Ops::operator=;

        /* Equivalent to Python `sequence.count(value)`, but also takes optional
        start/stop indices similar to `sequence.index()`. */
        template <typename T>
        inline Py_ssize_t count(
            const T& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
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
                    this->ptr(),
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
                PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
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
                    this->ptr(),
                    detail::object_or_cast(value).ptr()
                );
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            }
        }

        /* Equivalent to Python `sequence + items`. */
        template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
        inline Object concat(const T& items) const {
            PyObject* result = PySequence_Concat(
                this->ptr(),
                detail::object_or_cast(items).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Object>(result);
        }

        /* Equivalent to Python `sequence * repetitions`. */
        inline Object repeat(Py_ssize_t repetitions) const {
            PyObject* result = PySequence_Repeat(
                this->ptr(),
                repetitions
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Object>(result);
        }

        template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
        inline Object operator+(const T& items) const {
            return this->concat(items);
        }

        inline Object operator*(Py_ssize_t repetitions) {
            return this->repeat(repetitions);
        }

        friend inline Object operator*(Py_ssize_t repetitions, const SequenceOps& seq) {
            return seq.repeat(repetitions);
        }

        template <typename T>
        inline Object& operator+=(const T& items) {
            PyObject* result = PySequence_InPlaceConcat(
                this->ptr(),
                detail::object_or_cast(items).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            if (result == this->ptr()) {
                Py_DECREF(result);
            } else {
                *this = reinterpret_steal<Object>(result);
            }
            return *this;
        }

        inline Object& operator*=(Py_ssize_t repetitions) {
            PyObject* result = PySequence_InPlaceRepeat(this->ptr(), repetitions);
            if (result == nullptr) {
                throw error_already_set();
            }
            if (result == this->ptr()) {
                Py_DECREF(result);
            } else {
                *this = reinterpret_steal<Object>(result);
            }
            return *this;
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

    /* Types for which is_reverse_iterable is true will use custom reverse iterators
    when py::reversed() is called on them.  These types must expose a ReverseIterator
    class that implements the reverse iteration. */
    template <typename T, typename = void>
    constexpr bool is_reverse_iterable = false;
    template <typename T>
    constexpr bool is_reverse_iterable<
        T,
        std::enable_if_t<impl::is_same_or_subclass_of<Tuple, T>>
    > = true;
    template <typename T>
    constexpr bool is_reverse_iterable<
        T,
        std::enable_if_t<impl::is_same_or_subclass_of<List, T>>
    > = true;

    /* All new subclasses of py::Object must define these constructors, which are taken
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
    #define BERTRAND_OBJECT_CONSTRUCTORS(parent, cls, check_func)                       \
        /* Overload check() for C++ values using template metaprogramming. */           \
        template <typename T, std::enable_if_t<!impl::is_python<T>, int> = 0>           \
        static constexpr bool check(const T&) {                                         \
            return check<T>();                                                          \
        }                                                                               \
                                                                                        \
        /* Overload check() for Python objects using check_func. */                     \
        template <typename T, std::enable_if_t<impl::is_python<T>, int> = 0>            \
        static constexpr bool check(const T& obj) {                                     \
            return obj.ptr() != nullptr && check_func(obj.ptr());                       \
        }                                                                               \
                                                                                        \
        /* Inherit tagged borrow/steal and copy/move constructors. */                   \
        cls(Handle h, const borrowed_t& t) : parent(h, t) {}                            \
        cls(Handle h, const stolen_t& t) : parent(h, t) {}                              \
        cls(const cls& value) : parent(value.ptr(), borrowed_t{}) {}                    \
        cls(cls&& value) : parent(value.release(), stolen_t{}) {}                       \
                                                                                        \
        /* Convert an accessor into this type. */                                       \
        template <typename Policy>                                                      \
        cls(const detail::accessor<Policy> &a) {                                        \
            pybind11::object obj(a);                                                    \
            if (!check(obj)) {                                                          \
                throw impl::noconvert<cls>(obj.ptr());                                  \
            }                                                                           \
            m_ptr = obj.release().ptr();                                                \
        }                                                                               \
                                                                                        \
        /* Trigger implicit conversions to this type via the assignment operator. */    \
        template <typename T, std::enable_if_t<std::is_convertible_v<T, cls>, int> = 0> \
        cls& operator=(T&& value) {                                                     \
            if constexpr (std::is_same_v<cls, std::decay_t<T>>) {                       \
                if (this == &value) {                                                   \
                    return *this;                                                       \
                }                                                                       \
            }                                                                           \
            parent::operator=(cls(std::forward<T>(value)));                             \
            return *this;                                                               \
        }                                                                               \
                                                                                        \
        /* Make sure address operators don't get lost during overloads. */              \
        inline cls* operator&() { return this; }                                        \
        inline const cls* operator&() const { return this; }                            \

    /* Type-safe operator overloads are crazy from a circular dependency standpoint, so
     * we have have to separate declarations from definitions.  The macros below ensure
     * that the syntax remains consistent across the built-in types.  They should not
     * be necessary for client code that uses the built-in types.
     */

    #define DECLARE_TYPED_UNARY_OPERATOR(cls, op, return_type)                          \
        inline return_type op() const;                                                  \

    #define DEFINE_TYPED_UNARY_OPERATOR(cls, op, return_type)                           \
        inline return_type cls::op() const {                                            \
            return reinterpret_steal<return_type>(Base::op().release());                \
        }                                                                               \

    #define DECLARE_TYPED_BINARY_OPERATOR(cls, op, constraint, return_type)             \
        template <typename T, std::enable_if_t<impl::constraint<T>, int> = 0>           \
        inline return_type op(const T& other) const;                                    \

    #define DEFINE_TYPED_BINARY_OPERATOR(cls, op, constraint, return_type)              \
        template <typename T, std::enable_if_t<impl::constraint<T>, int> = 0>           \
        inline return_type cls::op(const T& other) const {                              \
            return reinterpret_steal<return_type>(Base::op(other).release());           \
        }                                                                               \

        // template <
        //     typename T,
        //     std::enable_if_t<
        //         !impl::is_same_or_subclass_of<Object, U> &&
        //         !impl::is_same_or_subclass_of<std::ostream, U> &&
        //         !impl::is_std_iterator<U>,
        //     int> = 0
        // >
        // inline friend return_type opcode(const U& other, const cls& self) {
        //     return reinterpret_steal<return_type>(Base::op(other, self).release());
        // }

        // template <typename T, std::enable_if_t<impl::constraint<T>>

    // TODO: include reverse equivalents for all binary operators






    #define DELETE_OPERATOR(op)                                                         \
        template <typename... Args>                                                     \
        auto op(Args&&... args) = delete;                                               \

}  // namespace impl


/* Forward to the object's stream insertion operator. */
template <typename T>
inline std::ostream& operator<<(std::ostream& os, const impl::Wrapper<T>& obj) {
    os << *obj;
    return os;
}


/* A lightweight proxy that allows an arbitrary Python object to be stored with static
duration.

Normally, storing a static Python object is unsafe because it can lead to a situation
where the Python interpreter is in an invalid state at the time the object's destructor
is called, causing a memory access violation during shutdown.  This class avoids that
by checking `Py_IsInitialized()` and only invoking the destructor if it evaluates to
true.  This technically means that we leave an unbalanced reference to the object, but
since the Python interpreter is shutting down anyways, it doesn't matter.  Python will
clean up the object regardless of its reference count. */
template <typename T>
struct Static : public impl::Wrapper<T> {
    using impl::Wrapper<T>::Wrapper;
    using impl::Wrapper<T>::operator=;

    /* Explicitly create an empty wrapper with uninitialized memory. */
    inline static Static alloc() {
        return Static(typename impl::Wrapper<T>::alloc_t{});
    }

    /* Destructor avoids calling the Wrapper's destructor if the Python interpreter is
    not currently initialized. */
    ~Static() {
        this->initialized &= Py_IsInitialized();
    }

};


/* Object subclass that represents Python's global None singleton. */
class NoneType : public impl::Ops {
    using Base = impl::Ops;

    inline static int check_none(PyObject* obj) {
        return Py_IsNone(obj);
    }

public:
    static Type type;

    BERTRAND_OBJECT_CONSTRUCTORS(Base, NoneType, check_none)
    NoneType() : Base(Py_None, borrowed_t{}) {}
};


/* Object subclass that represents Python's global NotImplemented singleton. */
class NotImplementedType : public impl::Ops {
    using Base = impl::Ops;

    inline static int check_not_implemented(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) Py_TYPE(Py_NotImplemented));
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    BERTRAND_OBJECT_CONSTRUCTORS(Base, NotImplementedType, check_not_implemented)
    NotImplementedType() : Base(Py_NotImplemented, borrowed_t{}) {}
};


/* Object subclass representing Python's global Ellipsis singleton. */
class EllipsisType : public impl::Ops {
    using Base = impl::Ops;

    inline static int check_ellipsis(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) Py_TYPE(Py_Ellipsis));
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    BERTRAND_OBJECT_CONSTRUCTORS(Base, EllipsisType, check_ellipsis)
    EllipsisType() : Base(Py_Ellipsis, borrowed_t{}) {}
};


/* Singletons for immortal Python objects. */
static const NoneType None;
static const EllipsisType Ellipsis;
static const NotImplementedType NotImplemented;


/* Object subclass that represents an imported Python module. */
class Module : public impl::Ops {
    using Base = impl::Ops;

    inline static int check_module(PyObject* obj) {
        int result = PyModule_Check(obj);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_CONSTRUCTORS(Base, Module, check_module)

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


inline impl::ItemAccessor Object::operator[](
    const std::initializer_list<impl::SliceInitializer>& slice
) const {
    if (slice.size() > 3) {
        throw ValueError("slices must be of the form {[start[, stop[, step]]]}");
    }
    size_t i = 0;
    std::array<Object, 3> params {None, None, None};
    for (const impl::SliceInitializer& item : slice) {
        params[i++] = item.first;
    }
    return Base::operator[](pybind11::slice(params[0], params[1], params[2]));
}


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* Equivalent to Python `import module` */
inline Static<Module> import(const char* name) {
    if (Py_IsInitialized()) {
        PyObject *obj = PyImport_ImportModule(name);
        if (obj == nullptr) {
            throw error_already_set();
        }
        return Static<Module>(reinterpret_steal<Module>(obj));
    } else {
        return Static<Module>::alloc();  // return an empty wrapper
    }
}


/* Equivalent to Python `iter(obj)` except that it can also accept C++ containers and
generate Python iterators over them.  Note that C++ types as rvalues are not allowed,
and will trigger a compiler error. */
template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
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
    if constexpr (impl::is_python<T>) {
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
