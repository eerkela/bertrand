#define PYBIND11_DETAILED_ERROR_MESSAGES

#include "core.h"

#include "bool.h"
#include "int.h"
#include "float.h"
#include "complex.h"
#include "range.h"
#include "list.h"
#include "tuple.h"
#include "set.h"
#include "dict.h"
#include "str.h"
#include "bytes.h"
#include "code.h"
// #include "datetime.h"
#include "math.h"
#include "type.h"

// #include "../regex.h"


// TODO: the only remaining language feature left to emulate are:
// - context managers (RAII)  - use py::enter() to enter a context (which produces an RAII guard), and py::exit() or destructor to exit
// - decorators (?)

// -> model context managers using auto guard = py::enter(object);


// TODO: after upgrading to gcc-14, these are now supported:
// - static operator()
// - static operator[]
// - multidimensional operator[]
// - std::format()
// - std::stacktrace()
// - std::ranges::zip() and zip_transform()
// - std::expected
// - user-generated static assertions



// TODO: numpy types can be implemented as constexpr structs that have an internal
// flyweight cache (which might be difficult wrt allocations).  They would inherit from
// a virtual base class that implements the full type interface.  This would allow me
// to pass polymorphic types by const reference to a base class, and use them to
// select overloads at compile time.  For instance, you could write several algorithms
// that accept different families of type tags, and the compiler would figure it out
// for you, resulting in zero overhead.  This would effectively mirror the Python
// @overload decorator, but using built-in C++ language features and no additional
// syntax.

// The only problem with this is that it seems like the presence of virtual methods
// makes such a tag not directly usable as a template parameter, though it may be used
// through its type directly, which could be piped into an array definition.  This
// would mean that we could statically dispatch on an array type by using C++20
// template constraints.

// I could also implement a using declaration that could resolve types by name
// entirely at compile time.  Something like:

//    using Type = bertrand::resolve<"Int">;  // Bonus points if I can do full compile-time regex matches



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


namespace py {

namespace literals {
    using namespace pybind11::literals;

    inline Code operator ""_python(const char* source, size_t size) {
        return Code::compile(std::string_view(source, size));
    }

}


//////////////////////////////
////    STATIC MEMBERS    ////
//////////////////////////////


// TODO: Decimal type?


/* Every Python type has a static `Type` member that gives access to the Python type
object associated with instances of that class. */
inline const Type Type::type {};
inline const Type Object::type = reinterpret_borrow<Type>((PyObject*) &PyBaseObject_Type);
inline const Type impl::FunctionTag::type = reinterpret_borrow<Type>((PyObject*) &PyFunction_Type);
// inline const Type ClassMethod::type = reinterpret_borrow<Type>((PyObject*) &PyClassMethodDescr_Type);
// inline const Type StaticMethod::type = reinterpret_borrow<Type>((PyObject*) &PyStaticMethod_Type);
// inline const Type Property::type = reinterpret_borrow<Type>((PyObject*) &PyProperty_Type);
inline const Type NoneType::type = reinterpret_borrow<Type>((PyObject*) Py_TYPE(Py_None));
inline const Type NotImplementedType::type = reinterpret_borrow<Type>((PyObject*) Py_TYPE(Py_NotImplemented));
inline const Type EllipsisType::type = reinterpret_borrow<Type>((PyObject*) &PyEllipsis_Type);
inline const Type Module::type = reinterpret_borrow<Type>((PyObject*) &PyModule_Type);
inline const Type Bool::type = reinterpret_borrow<Type>((PyObject*) &PyBool_Type);
inline const Type Int::type = reinterpret_borrow<Type>((PyObject*) &PyLong_Type);
inline const Type Float::type = reinterpret_borrow<Type>((PyObject*) &PyFloat_Type);
inline const Type Complex::type = reinterpret_borrow<Type>((PyObject*) &PyComplex_Type);
inline const Type Slice::type = reinterpret_borrow<Type>((PyObject*) &PySlice_Type);
inline const Type Range::type = reinterpret_borrow<Type>((PyObject*) &PyRange_Type);
inline const Type impl::ListTag::type = reinterpret_borrow<Type>((PyObject*) &PyList_Type);
inline const Type impl::TupleTag::type = reinterpret_borrow<Type>((PyObject*) &PyTuple_Type);
inline const Type impl::SetTag::type = reinterpret_borrow<Type>((PyObject*) &PySet_Type);
inline const Type impl::FrozenSetTag::type = reinterpret_borrow<Type>((PyObject*) &PyFrozenSet_Type);
inline const Type impl::DictTag::type = reinterpret_borrow<Type>((PyObject*) &PyDict_Type);
inline const Type impl::KeyTag::type = reinterpret_borrow<Type>((PyObject*) &PyDictKeys_Type);
inline const Type impl::ValueTag::type = reinterpret_borrow<Type>((PyObject*) &PyDictValues_Type);
inline const Type impl::ItemTag::type = reinterpret_borrow<Type>((PyObject*) &PyDictItems_Type);
inline const Type impl::MappingProxyTag::type = reinterpret_borrow<Type>((PyObject*) &PyDictProxy_Type);
inline const Type Str::type = reinterpret_borrow<Type>((PyObject*) &PyUnicode_Type);
inline const Type Bytes::type = reinterpret_borrow<Type>((PyObject*) &PyBytes_Type);
inline const Type ByteArray::type = reinterpret_borrow<Type>((PyObject*) &PyByteArray_Type);
inline const Type Code::type = reinterpret_borrow<Type>((PyObject*) &PyCode_Type);
inline const Type Frame::type = reinterpret_borrow<Type>((PyObject*) &PyFrame_Type);
// inline const Type Timedelta::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyDelta_Type->ptr());
//     } else {
//         return Type();
//     }
// }();
// inline const Type Timezone::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyTZInfo_Type->ptr());
//     } else {
//         return Type();
//     }
// }();
// inline const Type Date::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyDate_Type->ptr());
//     } else {
//         return Type();
//     }
// }();
// inline const Type Time::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyTime_Type->ptr());
//     } else {
//         return Type();
//     }
// }();
// inline const Type Datetime::type = [] {
//     if (impl::DATETIME_IMPORTED) {
//         return reinterpret_borrow<Type>(impl::PyDateTime_Type->ptr());
//     } else {
//         return Type();
//     }
// }();


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


namespace impl {

    template <typename Obj, typename T>
    struct visit_helper {
        static constexpr bool value = false;
    };

    template <typename Obj, typename R, typename T, typename... A>
    struct visit_helper<Obj, R(*)(T, A...)> {
        using Arg = std::decay_t<T>;
        static_assert(
            std::derived_from<Arg, Obj>,
            "visitor argument must derive from the visited object"
        );
        static constexpr bool value = true;
    };

    template <typename Obj, typename R, typename C, typename T, typename... A>
    struct visit_helper<Obj, R(C::*)(T, A...)> : visit_helper<Obj, R(*)(T, A...)> {};
    template <typename Obj, typename R, typename C, typename T, typename... A>
    struct visit_helper<Obj, R(C::*)(T, A...) const> : visit_helper<Obj, R(*)(T, A...)> {};
    template <typename Obj, typename R, typename C, typename T, typename... A>
    struct visit_helper<Obj, R(C::*)(T, A...) volatile> : visit_helper<Obj, R(*)(T, A...)> {};
    template <typename Obj, typename R, typename C, typename T, typename... A>
    struct visit_helper<Obj, R(C::*)(T, A...) const volatile> : visit_helper<Obj, R(*)(T, A...)> {};
    template <typename Obj, has_call_operator T>
    struct visit_helper<Obj, T> : visit_helper<Obj, decltype(&T::operator())> {};

    template <typename T, typename Obj>
    concept visitable = visit_helper<std::decay_t<Obj>, std::decay_t<T>>::value;

    template <typename Obj, typename Func, typename... Rest>
    auto visit_recursive(Obj&& obj, Func&& func, Rest&&... rest) {
        using Arg = visit_helper<std::decay_t<Obj>, std::decay_t<Func>>::Arg;

        if (isinstance<Arg>(obj)) {
            return std::invoke(func, reinterpret_borrow<Arg>(obj.ptr()));

        } else {
            if constexpr (sizeof...(Rest) > 0) {
                return visit_recursive(
                    std::forward<Obj>(obj),
                    std::forward<Rest>(rest)...
                );

            } else {
                std::string message = (
                    "no suitable function found for object of type '" +
                    std::string(Type(obj).name()) + "'"
                );
                throw TypeError(message);
            }
        }
    }

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
        template <typename T = Func> requires (cpp_like<T>)
        constexpr operator bool() const {
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
            if constexpr(std::same_as<Return, NoReturn>) {
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
        template <typename T = Func> requires (cpp_like<T>)
        constexpr operator bool() const {
            return is_callable_any<Func>;
        }

        /* Implicitly convert the tag to a runtime bool. */
        template <typename T = Func> requires (python_like<T>)
        operator bool() const {
            return PyCallable_Check(this->func.ptr());
        }

    };

}


/* Issue a Python assertion from C++. */
inline void assert_(bool condition, std::string message = "") {
    if (!condition) {
        throw AssertionError(message);
    }
}


/* An equivalent for std::visit, which applies one of a selection of functions
depending on the runtime type of a generic Python object.

This is roughly equivalent to std::visit, but with a few key differences:

    1.  Each function must accept at least one argument, which must be derived from
        `py::Object`.  They are free to accept additional arguments as long as those
        arguments have default values.
    2.  Each function is checked in order, which invokes a runtime type check against
        the type of the first argument.  The first function that matches the observed
        type of the object is called, and all other functions are ignored.
    3.  The generic `py::Object` type acts as a catch-all, since its isinstance() method
        always returns true as long as the object is not null.  Including a function
        that accepts `py::Object` can therefore act as a default case, if no previous
        function matches.
    4.  If no function is found that matches the object's type, a `TypeError` is thrown
        with a message indicating the observed type of the object.
    5.  Overload sets are not supported, as the first argument to an overloaded
        function is not well-defined.  List the functions in order instead.

Here's an example:

    py::Object x = "abc";
    int choice = py::visit(x,
        [](const py::Int& x) {
            return 1;
        },
        [](const py::Str& x) {
            return 2;  // will be chosen
        },
        [](const py::Object& x) {  // default case
            return 3;
        }
    );
*/
template <typename Obj, impl::visitable<Obj>... Funcs>
    requires (std::derived_from<std::decay_t<Obj>, Object>)
auto visit(Obj&& obj, Funcs&&... funcs) {
    static_assert(
        sizeof...(Funcs) > 0,
        "visit() requires at least one function to dispatch to"
    );
    return impl::visit_recursive(
        std::forward<Obj>(obj),
        std::forward<Funcs>(funcs)...
    );
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
template <typename... Args, impl::is_callable_any Func>
[[nodiscard]] constexpr auto callable(const Func& func) {
    // If no template arguments are given, default to wildcard matching
    if constexpr (sizeof...(Args) == 0) {
        return impl::CallTraits<Func, void>{func};

    // If void is given as a single argument, reinterpret as an empty argument list
    } else if constexpr (
        sizeof...(Args) == 1 &&
        std::same_as<std::tuple_element_t<0, std::tuple<Args...>>, void>
    ) {
        return impl::CallTraits<Func>{func};

    // Otherwise, pass the arguments through to the strict matching overload
    } else {
        return impl::CallTraits<Func, Args...>{func};
    }
}


// not implemented
// help() - not applicable
// input() - Use std::cin for now.  May be implemented in the future.
// open() - planned for a future release along with pathlib support


/* Equivalent to Python `all(args...)` */
template <typename First = void, typename... Rest> [[deprecated]]
bool all(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::all().  Use std::all_of() or its std::ranges "
        "equivalent with a boolean predicate instead."
    );
    return false;
}


/* Equivalent to Python `any(args...)` */
template <typename First = void, typename... Rest> [[deprecated]]
bool any(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::any().  Use std::any_of() or its std::ranges "
        "equivalent with a boolean predicate instead."
    );
    return false;
}


/* Equivalent to Python `enumerate(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object enumerate(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::enumerate().  Pipe the container with "
        "std::views::enumerate() or use a simple loop counter instead."
    );
    return {};
}


/* Equivalent to Python `filter(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object filter(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::filter().  Pipe the container with "
        "std::views::filter() instead."
    );
    return {};
}


/* Equivalent to Python `iter(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object iter(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::iter().  Use begin() and end() directly."
    );
    return {};
}


/* Equivalent to Python `iter(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object reversed(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::reversed().  Use rbegin() and rend() directly."
    );
    return {};
}


/* Equivalent to Python `map(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object map(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::map().  Pipe the container with "
        "std::views::transform() instead."
    );
    return {};
}


/* Equivalent to Python `max(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object max(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::max().  Use std::max(), std::max_element(), "
        "or their std::ranges equivalents instead."
    );
    return {};
}


/* Equivalent to Python `min(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object min(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::min().  Use std::min(), std::min_element(), "
        "or their std::ranges equivalents instead."
    );
    return {};
}


/* Equivalent to Python `next(args...)`. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object next(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::next().  Use the iterator's ++ increment "
        "operator instead, and compare against an end iterator to check for "
        "completion."
    );
    return {};
}


/* Equivalent to Python `sum(obj)`, but also works on C++ containers. */
template <typename First = void, typename... Rest> [[deprecated]]
py::Object sum(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::sum().  Use std::accumulate() or an ordinary "
        "loop variable instead."
    );
    return {};
}


template <typename First = void, typename... Rest> [[deprecated]]
py::Object zip(First&& first, Rest&&... rest) {
    static_assert(
        std::is_void_v<First>,
        "Bertrand does not implement py::zip().  If you are using C++23 or later, use "
        "std::views::zip() instead.  Otherwise, implement a coupled iterator class or "
        "keep the iterators separate during the loop body."
    );
    return {};
}


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
[[nodiscard]] inline Dict<Str, Object> builtins() {
    Interpreter::init();
    PyObject* result = PyEval_GetBuiltins();
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Dict<Str, Object>>(result);
}


/* Equivalent to Python `globals()`. */
[[nodiscard]] inline Dict<Str, Object> globals() {
    Interpreter::init();
    PyObject* result = PyEval_GetGlobals();
    if (result == nullptr) {
        throw RuntimeError("cannot get globals - no frame is currently executing");
    }
    return reinterpret_borrow<Dict<Str, Object>>(result);
}


/* Equivalent to Python `locals()`. */
[[nodiscard]] inline Dict<Str, Object> locals() {
    Interpreter::init();
    PyObject* result = PyEval_GetLocals();
    if (result == nullptr) {
        throw RuntimeError("cannot get locals - no frame is currently executing");
    }
    return reinterpret_borrow<Dict<Str, Object>>(result);
}


/* Equivalent to Python `aiter(obj)`.  Only works on asynchronous Python iterators. */
[[nodiscard]] inline Object aiter(const Object& obj) {
    static const Str s_aiter = "aiter";
    return builtins()[s_aiter](obj);
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


/* Equivalent to Python `ascii(obj)`.  Like `repr()`, but returns an ASCII-encoded
string. */
[[nodiscard]] inline Str ascii(const Handle& obj) {
    PyObject* result = PyObject_ASCII(obj.ptr());
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(result);
}


/* Equivalent to Python `bin(obj)`.  Converts an integer or other object implementing
__index__() into a binary string representation. */
[[nodiscard]] inline Str bin(const Handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 2);
    if (string == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `chr(obj)`.  Converts an integer or other object implementing
__index__() into a unicode character. */
[[nodiscard]] inline Str chr(const Handle& obj) {
    PyObject* string = PyUnicode_FromFormat("%llc", obj.cast<long long>());
    if (string == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `delattr(obj, name)`. */
inline void delattr(const Object& obj, const Str& name) {
    if (PyObject_DelAttr(obj.ptr(), name.ptr()) < 0) {
        Exception::from_python();
    }
}


/* Equivalent to Python `dir()` with no arguments.  Returns a list of names in the
current local scope. */
[[nodiscard]] inline List<Str> dir() {
    Interpreter::init();
    PyObject* result = PyObject_Dir(nullptr);
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<List<Str>>(result);
}


/* Equivalent to Python `dir(obj)`. */
[[nodiscard]] inline List<Str> dir(const Handle& obj) {
    if (obj.ptr() == nullptr) {
        throw TypeError("cannot call dir() on a null object");
    }
    PyObject* result = PyObject_Dir(obj.ptr());
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<List<Str>>(result);
}


/* Equivalent to Python `eval()` with a string expression. */
inline Object eval(const Str& expr) {
    try {
        return pybind11::eval(expr, globals(), locals());
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `eval()` with a string expression and global variables. */
inline Object eval(const Str& source, Dict<Str, Object>& globals) {
    try {
        return pybind11::eval(source, globals, locals());
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `eval()` with a string expression and global/local variables. */
inline Object eval(
    const Str& source,
    Dict<Str, Object>& globals,
    Dict<Str, Object>& locals
) {
    try {
        return pybind11::eval(source, globals, locals);
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `exec()` with a string expression. */
inline void exec(const Str& source) {
    try {
        pybind11::exec(source, globals(), locals());
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `exec()` with a string expression and global variables. */
inline void exec(const Str& source, Dict<Str, Object>& globals) {
    try {
        pybind11::exec(source, globals, locals());
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `exec()` with a string expression and global/local variables. */
inline void exec(
    const Str& source,
    Dict<Str, Object>& globals,
    Dict<Str, Object>& locals
) {
    try {
        pybind11::exec(source, globals, locals);
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `exec()` with a precompiled code object. */
inline void exec(const Code& code) {
    code(globals() | locals());
}


/* Equivalent to Python `getattr(obj, name)` with a dynamic attribute name. */
[[nodiscard]] inline Object getattr(const Handle& obj, const Str& name) {
    PyObject* result = PyObject_GetAttr(obj.ptr(), name.ptr());
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `getattr(obj, name, default)` with a dynamic attribute name and
default value. */
[[nodiscard]] inline Object getattr(
    const Handle& obj,
    const Str& name,
    const Object& default_value
) {
    PyObject* result = PyObject_GetAttr(obj.ptr(), name.ptr());
    if (result == nullptr) {
        PyErr_Clear();
        return default_value;
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `hasattr(obj, name)` with a dynamic attribute name. */
[[nodiscard]] inline bool hasattr(const Handle& obj, const Str& name) {
    return PyObject_HasAttr(obj.ptr(), name.ptr());
}


/* Equivalent to Python `hex(obj)`.  Converts an integer or other object implementing
__index__() into a hexadecimal string representation. */
[[nodiscard]] inline Str hex(const Int& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 16);
    if (string == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `id(obj)`, but also works with C++ values.  Casts the object's
memory address to a void pointer. */
template <typename T>
[[nodiscard]] const void* id(const T& obj) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<void*>(obj);

    } else if constexpr (impl::python_like<T>) {
        return reinterpret_cast<void*>(obj.ptr());

    } else {
        return reinterpret_cast<void*>(&obj);
    }
}


/* Equivalent to Python `oct(obj)`.  Converts an integer or other object implementing
__index__() into an octal string representation. */
[[nodiscard]] inline Str oct(const Handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 8);
    if (string == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `ord(obj)`.  Converts a unicode character into an integer
representation. */
[[nodiscard]] inline Int ord(const Handle& obj) {
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


/* Equivalent to Python `setattr(obj, name, value)` with a dynamic attribute name. */
inline void setattr(const Handle& obj, const Str& name, const Object& value) {
    if (PyObject_SetAttr(obj.ptr(), name.ptr(), value.ptr()) < 0) {
        Exception::from_python();
    }
}


// TODO: sorted() for C++ types.  Still returns a List, but has to reimplement logic
// for key and reverse arguments.  std::ranges::sort() is a better alternative for C++
// types.
// -> sorted() will have to preserve type information for CTAD containers, or be
// deleted outright.


// /* Equivalent to Python `sorted(obj)`. */
// [[nodiscard]] inline List sorted(const Handle& obj) {
//     static const Str s_sorted = "sorted";
//     return builtins()[s_sorted](obj);
// }


// /* Equivalent to Python `sorted(obj, key=key, reverse=reverse)`. */
// [[nodiscard]] inline List sorted(const Handle& obj, const Function& key, bool reverse = false) {
//     static const Str s_sorted = "sorted";
//     return builtins()[s_sorted](
//         obj,
//         py::arg("key") = key,
//         py::arg("reverse") = reverse
//     );
// }


/* Equivalent to Python `vars()`. */
[[nodiscard]] inline Dict<Str, Object> vars() {
    return locals();
}


/* Equivalent to Python `vars(object)`. */
[[nodiscard]] inline Dict<Str, Object> vars(const Object& object) {
    static const Str lookup = "__dict__";
    return reinterpret_steal<Dict<Str, Object>>(
        getattr(object, lookup).release()
    );
}


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


// TODO: ensure that placing set_pyerr() here doesn't cause any problems.

#ifdef BERTRAND_NO_TRACEBACK

    template <typename CRTP, std::derived_from<Exception> Base>
    void __exception__::set_pyerr() const override {
        PyErr_SetString(ptr(Type<CRTP>()), Base::message().c_str());
    }

#else

    template <typename CRTP, std::derived_from<Exception> Base>
    void __exception__::set_pyerr() const override {
        Base::traceback.restore(ptr(Type<CRTP>()), Base::message().c_str());
    }

#endif


template <typename Func, typename... Defaults>
Module& Module::def(const Str& name, const Str& doc, Func&& body, Defaults&&... defaults) {
    Function f(
        name,
        doc,
        std::forward<Func>(body),
        std::forward<Defaults>(defaults)...
    );
    if (PyModule_AddObjectRef(this->ptr(), PyUnicode_AsUTF8(name.ptr()), f.ptr())) {
        Exception::from_python();
    }
    return *this;
}


inline Module Module::def_submodule(const Str& name, const Str& doc = "") {
    const char* this_name = PyModule_GetName(m_ptr);
    if (this_name == nullptr) {
        Exception::from_python();
    }
    std::string full_name = std::string(this_name) + "." + name;
    Handle submodule = PyImport_AddModule(full_name.c_str());
    if (!submodule) {
        Exception::from_python();
    }
    Module result = reinterpret_borrow<Module>(submodule);
    try {
        if (doc && pybind11::options::show_user_defined_docstrings()) {
            setattr<"__doc__">(result, doc);
        }
        setattr(*this, name, result);
        return result;
    } catch (...) {
        Exception::from_pybind11();
    }
}


template <typename Derived, impl::is_generic CppType>
inline PyObject* impl::Binding<Derived, CppType>::__class_getitem__(
    Derived* self,
    PyObject* key
) {
    if (PyTuple_Check(key)) {
        Py_INCREF(key);
    } else {
        PyObject* tuple = PyTuple_Pack(1, key);
        if (tuple == nullptr) {
            return nullptr;
        }
        key = tuple;
    }
    try {
        py::Tuple tuple = reinterpret_steal<py::Tuple<Object>>(key);
        auto it = template_instantiations.find(tuple);
        if (it == template_instantiations.end()) {
            // TODO: reconstruct the full __class_getitem__ call
            throw py::TypeError(
                "class template has not been instantiated: " +
                repr(key)
            );
        }
        return Py_NewRef(ptr(it->second));

    } catch (...) {
        Exception::to_python();
        return nullptr;
    }
}


template <typename Return, typename... Target>
[[nodiscard]] inline std::optional<std::string> Function<Return(Target...)>::filename() const {
    std::optional<Code> code = this->code();
    if (code.has_value()) {
        return code->filename;
    }
    return std::nullopt;
}


template <typename Return, typename... Target>
[[nodiscard]] inline std::optional<size_t> Function<Return(Target...)>::lineno() const {
    std::optional<Code> code = this->code();
    if (code.has_value()) {
        return code->line_number;
    }
    return std::nullopt;
}


template <typename Return, typename... Target>
[[nodiscard]] inline std::optional<Module> Function<Return(Target...)>::module_() const {
    PyObject* result = PyFunction_GetModule(unwrap_method());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            PyErr_Clear();
        }
        return std::nullopt;
    }
    return reinterpret_borrow<Module>(result);
}


template <typename Return, typename... Target>
[[nodiscard]] inline std::optional<Code> Function<Return(Target...)>::code() const {
    PyObject* result = PyFunction_GetCode(unwrap_method());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            PyErr_Clear();
        }
        return std::nullopt;
    }
    return reinterpret_borrow<Code>(result);
}


template <typename Return, typename... Target>
[[nodiscard]] inline std::optional<Dict<Str, Object>> Function<Return(Target...)>::globals() const {
    PyObject* result = PyFunction_GetGlobals(unwrap_method());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            PyErr_Clear();
        }
        return std::nullopt;
    }
    return reinterpret_borrow<Dict<Str, Object>>(result);
}


// template <typename Return, typename... Target>
// [[nodiscard]] MappingProxy<Dict<Str, Object>> Function<Return(Target...)>::defaults() const {

// }


// template <typename Return, typename... Target>
// template <typename... Values> requires (sizeof...(Values) > 0)  // TODO: complete this
// void Function<Return(Target...)>::defaults(Values&&... values) {

// }


// template <typename Return, typename... Target>
// [[nodiscard]] MappingProxy<Dict<Str, Object>> Function<Return(Target...)>::annotations() const {

// }


// template <typename Return, typename... Target>
// template <typename... Annotations> requires (sizeof...(Annotations) > 0)  // TODO: complete this
// void Function<Return(Target...)>::annotations(Annotations&&... values) {

// }


template <typename Return, typename... Target>
[[nodiscard]] inline std::optional<Tuple<Object>> Function<Return(Target...)>::closure() const {
    PyObject* result = PyFunction_GetClosure(unwrap_method());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            PyErr_Clear();
        }
        return std::nullopt;
    }
    return reinterpret_borrow<Tuple<Object>>(result);
}


template <typename Return, typename... Target>
inline void Function<Return(Target...)>::closure(std::optional<Tuple<Object>> closure) {
    PyObject* item = closure.has_value() ? closure->ptr() : Py_None;
    if (PyFunction_SetClosure(unwrap_method(), item)) {
        Exception::from_python();
    }
}


#if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

    /* Get the type's qualified name. */
    [[nodiscard]] inline Str Type::qualname() const {
        PyObject* result = PyType_GetQualName(self());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Str>(result);
    }

#endif


//////////////////////////////////////
////    BERTRAND.PYTHON MODULE    ////
//////////////////////////////////////


template <>
class Module<"bertrand.python"> : public Object, public impl::ModuleTag {
public:

    /* NOTE: the bertrand.python module uses per-module state to store global
    configuration in a way that allows per-interpreter state.  This includes:
    
        - The exception translation map, which allows Python exceptions to be caught
          and rethrown in C++ using Exception::from_python().
        - The type source translation map, which allows recognized Python type objects
          to be mapped to the fully-qualified names of their corresponding C++ types.
          This is used to account for Python-style type annotations when generating
          C++ bindings for Python modules.

    These maps are populated as bindings are exported, meaning that they always reflect
    the current interpreter state.  They will be updated whenever a Bertrand-enabled
    extension module is loaded or unloaded. */
    struct __python__ : ModuleTag::def<__python__, "bertrand.python"> {
        using Base = ModuleTag::def<__python__, "bertrand.python">;
        using TypeMap = Dict<Type<>, Str>;
        using ExceptionMap = std::unordered_map<
            PyTypeObject*,
            std::function<void(PyObject* /* exc_value */)>
        >;

        TypeMap type_map;
        ExceptionMap exception_map;

        static Module<"bertrand.python"> __export__(Bindings bindings) {
            bindings.var<"type_map">(&__python__::type_map);

            auto result = bindings.finalize();

            __python__* contents = reinterpret_cast<__python__*>(ptr(result));
            new (&contents->type_map) TypeMap();
            new (&contents->exception_map) ExceptionMap();

            return result;
        }

        static int __traverse__(__python__* self, visitproc visit, void* arg) {
            Py_VISIT(ptr(self->type_map));
            return Base::__traverse__(self, visit, arg);
        }

        static int __clear__(__python__* self) {
            Py_XDECREF(release(self->type_map));
            return Base::__clear__(self);
        }

    };

    Module(Handle h, borrowed_t t) : Object(h, t) {}
    Module(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Module>::template enable<Args...>)
    Module(Args&&... args) : Object(
        implicit_ctor<Module>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Module>::template enable<Args...>)
    explicit Module(Args&&... args) : Object(
        explicit_ctor<Module>{},
        std::forward<Args>(args)...
    ) {}

};


template <typename CRTP, StaticStr ModName>
template <typename Cls>
void impl::ModuleTag::def<CRTP, ModName>::Bindings::register_type(
    Module<ModName>& mod,
    PyTypeObject* type
) {
    using Mod = Module<"bertrand.python">::__python__;
    Mod* contents;
    if constexpr (ModName == "bertrand.python") {
        contents = reinterpret_cast<Mod*>(ptr(mod));
    } else {
        contents = reinterpret_cast<Mod*>(ptr(Module<"bertrand.python">()));
    }
    Type<> key = reinterpret_borrow<Type<>>(reinterpret_cast<PyObject*>(type));
    contents->type_map[key] = impl::demangle(typeid(Cls).name());
}


/// TODO: Note that I need to export all of the standard exception types to
/// bertrand.python in order to populate the exception map.


template <typename CRTP, StaticStr ModName>
void impl::ModuleTag::def<CRTP, ModName>::Bindings::register_exception(
    Module<ModName>& mod,
    PyTypeObject* type,
    std::function<void(PyObject*)> callback
) {
    using Mod = Module<"bertrand.python">::__python__;
    if constexpr (ModName == "bertrand.python") {
        reinterpret_cast<Mod*>(ptr(mod))->exception_map[type] = callback;
    } else {
        Mod* contents = reinterpret_cast<Mod*>(ptr(Module<"bertrand.python">()));
        contents->exception_map[type] = callback;
    }
}


[[noreturn, clang::noinline]] void Interface<Exception>::from_python() {
    PyThreadState* thread = PyThreadState_Get();
    PyObject* value = thread->current_exception;
    if (value == nullptr) {
        throw std::logic_error(
            "could not convert Python exception into a C++ exception - "
            "exception is not set."
        );
    }
    thread->current_exception = nullptr;

    // append C++ frames to the head (least recent end) of the existing traceback
    // if directed.
    #ifndef BERTRAND_NO_TRACEBACK
        try {
            PyTracebackObject* front = impl::build_traceback(
                cpptrace::generate_trace(1),
                PyException_GetTraceback(value)
            )
            if (front != nullptr) {
                PyException_SetTraceback(value, front);
                Py_DECREF(front);
            }
        } catch (...) {
            // if cpptrace somehow fails, ignore it and continue
        }
    #endif

    // search the global exception map for the exception type, and invoke its callback,
    // which will throw the appropriate C++ exception type
    using Mod = Module<"bertrand.python">::__python__;
    Module<"bertrand.python"> python;
    Mod* mod = reinterpret_cast<Mod*>(ptr(python));
    auto it = mod->exception_map.find(Py_TYPE(value));
    if (it != mod->exception_map.end()) {
        it->second(value);
    }

    // otherwise, throw as a generic C++ exception
    throw reinterpret_steal<Exception>(value);
}


[[noreturn, clang::noinline]] void Interface<Type<Exception>>::from_python() {
    PyThreadState* thread = PyThreadState_Get();
    PyObject* value = thread->current_exception;
    if (value == nullptr) {
        throw std::logic_error(
            "could not convert Python exception into a C++ exception - "
            "exception is not set."
        );
    }
    thread->current_exception = nullptr;
    #ifndef BERTRAND_NO_TRACEBACK
        try {
            PyTracebackObject* front = impl::build_traceback(
                cpptrace::generate_trace(1),
                PyException_GetTraceback(value)
            )
            if (front != nullptr) {
                PyException_SetTraceback(value, front);
                Py_DECREF(front);
            }
        } catch (...) {}
    #endif
    using Mod = Module<"bertrand.python">::__python__;
    Module<"bertrand.python"> python;
    Mod* mod = reinterpret_cast<Mod*>(ptr(python));
    auto it = mod->exception_map.find(Py_TYPE(value));
    if (it != mod->exception_map.end()) {
        it->second(value);
    }
    throw reinterpret_steal<Exception>(value);
}


}  // namespace py


// auto Regex::Match::group(const py::args& args) const
//     -> std::vector<std::optional<std::string>>
// {
//     py::Tuple tuple = args;
//     std::vector<std::optional<std::string>> result;
//     result.reserve(len(tuple));

//     for (const auto& arg : tuple) {
//         if (py::isinstance<py::Str>(arg)) {
//             result.push_back(group(static_cast<std::string>(arg)));
//         } else if (py::isinstance<py::Int>(arg)) {
//             result.push_back(group(static_cast<size_t>(arg)));
//         } else {
//             throw py::TypeError(
//                 "group() expects an integer or string argument"
//             );
//         }
//     }

//     return result;
// }


#undef PYBIND11_DETAILED_ERROR_MESSAGES
