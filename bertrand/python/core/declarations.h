#ifndef BERTRAND_PYTHON_CORE_DECLARATIONS_H
#define BERTRAND_PYTHON_CORE_DECLARATIONS_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <chrono>
#include <complex>
#include <concepts>
#include <deque>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <ostream>
#include <ranges>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// required for demangling
#if defined(__GNUC__) || defined(__clang__)
    #include <cxxabi.h>
    #include <cstdlib>
#elif defined(_MSC_VER)
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib, "dbghelp.lib")
#endif

#define Py_BUILD_CORE

#include <Python.h>
#include <frameobject.h>
#include <internal/pycore_frame.h>  // required to assign to frame->f_lineno
#include <internal/pycore_moduleobject.h>  // required to create module subclasses

#undef Py_BUILD_CORE

#include <cpptrace/cpptrace.hpp>

#include <bertrand/static_str.h>


namespace py {
using bertrand::StaticStr;


namespace impl {
    struct BertrandTag {};
    struct UnionTag : BertrandTag {};
    struct IterTag : BertrandTag {};
    struct FunctionTag : BertrandTag {};
    struct TypeTag : BertrandTag {};
    struct ModuleTag : BertrandTag {};
    struct TupleTag : BertrandTag {};
    struct ListTag : BertrandTag{};
    struct SetTag : BertrandTag {};
    struct FrozenSetTag : BertrandTag {};
    struct KeyTag : BertrandTag {};
    struct ValueTag : BertrandTag {};
    struct ItemTag : BertrandTag {};
    struct DictTag : BertrandTag {};
    struct MappingProxyTag : BertrandTag {};

    /* Demangle a C++ type name using the compiler's intrinsics. */
    static constexpr std::string demangle(const char* name) {
        #if defined(__GNUC__) || defined(__clang__)
            int status = 0;
            std::unique_ptr<char, void(*)(void*)> res {
                abi::__cxa_demangle(
                    name,
                    nullptr,
                    nullptr,
                    &status
                ),
                std::free
            };
            return (status == 0) ? res.get() : name;
        #elif defined(_MSC_VER)
            char undecorated_name[1024];
            if (UnDecorateSymbolName(
                name,
                undecorated_name,
                sizeof(undecorated_name),
                UNDNAME_COMPLETE
            )) {
                return std::string(undecorated_name);
            } else {
                return name;
            }
        #else
            return name; // fallback: no demangling
        #endif
    }

    template <size_t I>
    static void unpack_arg() {
        static_assert(false, "index out of range for parameter pack");
    }

    template <size_t I, typename T, typename... Ts>
    static decltype(auto) unpack_arg(T&& curr, Ts&&... next) {
        if constexpr (I == 0) {
            return std::forward<T>(curr);
        } else {
            return unpack_arg<I - 1>(std::forward<Ts>(next)...);
        }
    }

    template <size_t I, typename... Ts>
    struct _unpack_type;
    template <size_t I, typename T, typename... Ts>
    struct _unpack_type<I, T, Ts...> {
        template <size_t J>
        struct helper { using type = _unpack_type<J - 1, Ts...>::type;
        };
        template <size_t J> requires (J == 0)
        struct helper<J> { using type = T; };
        using type = helper<I>::type;
    };
    template <size_t I, typename... Ts>
    using unpack_type = _unpack_type<I, Ts...>::type;

    template <typename Search, size_t I, typename... Ts>
    static constexpr size_t _index_of = 0;
    template <typename Search, size_t I, typename T, typename... Ts>
    static constexpr size_t _index_of<Search, I, T, Ts...> =
        std::same_as<Search, T> ? 0 : _index_of<Search, I + 1, Ts...> + 1;
    template <typename Search, typename... Ts>
    static constexpr size_t index_of = _index_of<Search, 0, Ts...>;

    /* Merge several hashes into a single value.  Based on `boost::hash_combine()`:
    https://www.boost.org/doc/libs/1_86_0/libs/container_hash/doc/html/hash.html#notes_hash_combine */
    template <std::convertible_to<size_t>... Hashes>
    size_t hash_combine(size_t first, Hashes... rest) noexcept {
        if constexpr (sizeof(size_t) == 4) {
            constexpr auto mix = [](size_t& seed, size_t value) {
                seed += 0x9e3779b9 + value;
                seed ^= seed >> 16;
                seed *= 0x21f0aaad;
                seed ^= seed >> 15;
                seed *= 0x735a2d97;
                seed ^= seed >> 15;
            };
            (mix(first, rest), ...);
        } else {
            constexpr auto mix = [](size_t& seed, size_t value) {
                seed += 0x9e3779b9 + value;
                seed ^= seed >> 32;
                seed *= 0xe9846af9b1a615d;
                seed ^= seed >> 32;
                seed *= 0xe9846af9b1a615d;
                seed ^= seed >> 28;
            };
            (mix(first, rest), ...);
        }
        return first;
    }

    /* A static RAII guard that initializes the Python interpreter the first time a Python
    object is created and finalizes it when the program exits. */
    struct Interpreter : impl::BertrandTag {

        /* Ensure that the interpreter is active within the given context.  This is
        called internally whenever a Python object is created from pure C++ inputs, and is
        not called in any other context in order to avoid unnecessary overhead.  It must be
        implemented as a function in order to avoid C++'s static initialization order
        fiasco. */
        static const Interpreter& init() {
            static Interpreter instance{};
            return instance;
        }

        Interpreter(const Interpreter&) = delete;
        Interpreter(Interpreter&&) = delete;

    private:

        Interpreter() {
            if (!Py_IsInitialized()) {
                Py_Initialize();
            }
        }

        ~Interpreter() {
            if (Py_IsInitialized()) {
                Py_Finalize();
            }
        }
    };

    struct Sentinel {};

}


/// TODO: really, what I should do is remove as many of the following forward
/// declarations as possible, so that I don't restrict the template signatures and
/// can get good error messages from C++20 concepts


struct Object;



// template <typename Begin = Object, typename End = void, typename Container = void>
// struct Iterator;
// template <StaticStr Name, typename T>
// struct Arg;
// template <typename F> requires (impl::Signature<F>::enable)
// struct Function;
template <typename T = Object>
struct Type;
struct BertrandMeta;
template <StaticStr Name>
struct Module;
struct NoneType;
struct NotImplementedType;
struct EllipsisType;
struct Slice;
struct Bool;
struct Int;
struct Float;
struct Complex;
struct Str;
struct Bytes;
struct ByteArray;
struct Date;
struct Time;
struct Datetime;
struct Timedelta;
struct Timezone;
struct Range;
template <typename Val = Object>
struct List;
template <typename Val = Object>
struct Tuple;
template <typename Key = Object>
struct Set;
template <typename Key = Object>
struct FrozenSet;
template <typename Key = Object, typename Val = Object>
struct Dict;
template <typename Map>
struct KeyView;
template <typename Map>
struct ValueView;
template <typename Map>
struct ItemView;
template <typename Map>
struct MappingProxy;


/* Base class for disabled control structures. */
struct Disable : impl::BertrandTag {
    static constexpr bool enable = false;
};


/* Base class for enabled control structures.  Encodes the return type as a template
parameter. */
template <typename T>
struct Returns : impl::BertrandTag {
    static constexpr bool enable = true;
    using type = T;
};


/* Customizes the way C++ templates are exposed to Python.  The closest Python
analogue to this is the `__class_getitem__` method of a custom type, which in
Bertrand's case allows navigation of the C++ template hierarchy from Python, by
subscripting a generic type.  Such a subscription directly searches a Python dictionary
held in the type's metaclass, whose keys are populated by this control struct when the
type is imported.

This control struct is disabled by default, and must be explicitly specialized for any
type that implements template parameters.  All specializations MUST implement a custom
call operator that takes no arguments, and produces a key to be inserted into the
template dictionary.  A key consisting of multiple, comma-separated parts can be
encoded as a tuple, which will be accessed idiomatically from Python when the
multidimensional subscript operator is used.  The only restriction on the contents of
the returned key is that each element must be hashable, enabling the use of non-type
template parameters, such as integers or strings, which will be modeled identically on
the Python side. */
template <typename Self>
struct __template__                                         : Disable {};


/* Enables the `py::isinstance<...>()` operator for any subclass of `py::Object`, which
must return a boolean.  Returns false by default unless this class is explicitly
specialized.  Specializations of this class may implement a custom call operator with
any of the following forms:

1.  __isinstance__<T, Base>{}(T&& obj) -> bool
2.  __isinstance__<T, Base>{}(T&& obj, Base&& cls) -> bool

The first corresponds to a `py::isinstance<Base>(obj)` call, which is evaluated at
compile time if possible.  The second matches a `py::isinstance(obj, cls)` call, which
is evaluated at runtime and only enabled if the equivalent Python call would be
well-formed (i.e. `cls` is a type-like object or a union of types for which
`issubclass()` returns a valid result, etc.).  The first form is preferred in almost
all cases, while the second form is generally only used when dealing with dynamic
types. */
template <typename Derived, typename Base>
struct __isinstance__                                       : Disable {};


/* Enables the `py::issubclass<...>()` operator for any subclass of `py::Object`, which
must return a boolean.  Returns false by default unless this class is explicitly
specialized.  Specializations of this class may implement a custom call operator with
any of the following forms:

1.  __issubclass__<T, Base>{}() -> bool
2.  __issubclass__<T, Base>{}(T&& obj) -> bool
3.  __issubclass__<T, Base>{}(T&& obj, Base&& cls) -> bool

Which correspond to the following:

1.  `py::issubclass<Derived, Base>()`, which is always evaluated at compile time.
2.  `py::issubclass<Base>(obj)`, which is evaluated at compile time if possible.
3.  `py::issubclass(obj, Base)`, which is evaluated at runtime and only enabled if the
    equivalent Python call would be well-formed (i.e. `obj` is a type-like object or a
    dynamic object which can be narrowed to a single type, etc.).

The first form is preferred when dealing with pure static types, since it has no
runtime overhead, and essentially devolves into a `std::derived_from<>` check.  The
second form is used when the base type is known at compile time, but the derived type
is not, and is always valid.  If no custom logic is implemented, it will decay into
the first form using the derived object's C++ type.  The third form is used when both
types are dynamic, similar to the two-argument form of `py::isinstance()`. */
template <typename Derived, typename Base>
struct __issubclass__                                       : Disable {};


/* Enables the `py::getattr<"name">()` helper for any `py::Object` subclass, and
assigns a corresponding return type.  Disabled by default unless this class is
explicitly specialized for a given attribute name.  Specializations of this class may
implement a custom call operator to replace the default behavior, which delegates to a
normal dotted attribute lookup at the Python level.   */
template <typename Self, StaticStr Name>
struct __getattr__                                          : Disable {};


/* Enables the `py::setattr<"name">()` helper for any `py::Object` subclass, which must
return void.  Disabled by default unless this class is explicitly specialized for a
given attribute name.  Specializations of this class may implement a custom call
operator to replace the default behavior, which delegates to a normal dotted attribute
assignment at the Python level. */
template <typename Self, StaticStr Name, typename Value>
struct __setattr__                                          : Disable {};


/* Enables the `py::delattr<"name">()` helper for any `py::Object` subclass, which must
return void.  Disabled by default unless this class is explicitly specialized for a
given attribute name.  Specializations of this class may implement a custom call
operator to replace the default behavior, which delegates to a normal dotted attribute
deletion at the Python level. */
template <typename Self, StaticStr Name>
struct __delattr__                                          : Disable {};


/* Enables the C++ initializer list constructor for any `py::Object` subclass, which
must return the type that the `std::initializer_list<>` should be templated on when
constructing instances of this class.  Note that this is NOT the class itself, nor is
it the full `std::initializer_list<>` specialization as it would ordinarily be given.
This is due to restrictions in the C++ API around `std::initializer_list` in general.

The initializer list constructor is disabled by default unless this class is explicitly
specialized to return a particular element type.  Specializations of this class MUST
implement a custom call operator to define the constructor logic, which should take a
`const std::initializer_list<>&` as an argument and return an instance of the given
class.  This is what allows direct initialization of Python container types, analogous
to Python's built-in `[]`, `()`, and `{}` container syntax. */
template <typename Self>
struct __initializer__                                      : Disable {};


/* Enables the explicit C++ constructor for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__init__">` or
`__getattr__<Self, "__new__">` in that order, which must return member functions,
possibly with Python-style argument annotations.  Specializations of this class MUST
implement a custom call operator to define the constructor logic.

A special case exists for the default constructor for a given type, which accepts no
arguments.  Such constructors will be demoted to implicit constructors, rather than
requiring an explicit call. */
template <typename Self, typename... Args>
struct __init__ {
    template <StaticStr name>
    struct ctor {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct ctor<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<Args...>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = ctor<"__init__">::enable || ctor<"__new__">::enable;
    using type = std::conditional_t<
        ctor<"__init__">::enable,
        typename ctor<"__init__">::type,
        std::conditional_t<
            ctor<"__new__">::enable,
            typename ctor<"__new__">::type,
            void
        >
    >;
};


/* Enables implicit conversions between any `py::Object` subclass and an arbitrary
type.  This class handles both conversion to and from Python, as well as conversions
between Python types at the same time.  Specializations of this class MUST implement a
custom call operator which takes an instance of `From` and returns an instance of `To`
(both of which can have arbitrary cvref-qualifiers), with the following rules:

1.  If `From` is a C++ type and `To` is a Python type, then `__cast__<From, To>` will
    enable an implicit constructor on `To` that accepts a `From` object with the
    given qualifiers.
2.  If `From` is a Python type and `To` is a C++ type, then `__cast__<From, To>` will
    enable an implicit conversion operator on `From` that returns a `To` object with
    the given qualifiers.
3.  If both `From` and `To` are Python types, then `__cast__<From, To>` will enable
    an implicit conversion operator similar to (2), but can interact with the CPython
    API to ensure dynamic type safety.
4.  If only `From` is supplied, then the return type must be a `py::Object` subclass
    and `__cast__<From>` will mark it as being convertible to Python.  In this case,
    the default behavior is to call the return type's constructor with the given
    `From` argument, which will apply the correct conversion logic according to the
    previous rules.  The user does not (and should not) need to implement a custom call
    operator in this case.
 */
template <typename From, typename To = void>
struct __cast__                                             : Disable {};


/* Enables explicit conversions between any `py::Object` subclass and an arbitrary
type.  This class corresponds to the `static_cast<To>()` operator in C++, which is
similar to, but more restrictive than the ordinary `__cast__` control struct.
Specializations of this class MUST implement a custom call operator which takes an
instance of `From` and returns an instance of `To` (both of which can have arbitrary
cvref-qualifiers), with the following rules:

1.  If `From` is a C++ type and `To` is a Python type, then `__explicit_cast__<From,
    To>` will enable an explicit constructor on `To` that accepts a `From` object with
    the given qualifiers.  Such a constructor will be also called when performing a
    functional-style cast in C++ (e.g. `To(from)`).
2.  If `From` is a Python type and `To` is a C++ type, then `__explicit_cast__<From,
    To>` will enable an explicit conversion operator on `From` that returns a `To`
    object with the given qualifiers.

Note that normal `__cast__` specializations will always take precedence over explicit
casts, so this control struct is only used when no implicit conversion would match, and
the user explicitly specifies the cast. */
template <typename From, typename To>
struct __explicit_cast__                                    : Disable {};


/* Customizes the `py::repr()` output for an arbitrary type.  Note that `py::repr()` is
always enabled by default; specializing this struct merely changes the output.  The
default specialization delegates to Python by introspecting
`__getattr__<Self, "__repr__">` if possible, which must return a member function,
possibly with Python-style argument annotations.  If no such attribute exists, then
the operator will fall back to either C++ stream insertion via the `<<` operator or
`std::to_string()` for primitive types.  If none of the above are available, then
`repr()` will return a string containing the demangled typeid. */
template <typename Self>
struct __repr__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        static constexpr bool _enable = false;
        template <std::derived_from<impl::FunctionTag> T>
            requires (T::has_self && T::template bind<>)
        static constexpr bool _enable<T> =
            std::convertible_to<typename T::Return, std::string>;
        static constexpr bool enable = _enable<typename __getattr__<Self, name>::type>;
    };
    static constexpr bool enable = infer<"__repr__">::enable;
    using type = std::string;
};


/* Enables the C++ call operator for any `py::Object` subclass, and assigns a
corresponding return type.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__call__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self, typename... Args>
struct __call__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<Args...>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__call__">::enable;
    using type = infer<"__call__">::type;
};


/* Enables the C++ subscript operator for any `py::Object` subclass, and assigns a
corresponding return type.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__getitem__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self, typename... Key>
struct __getitem__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<Key...>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__getitem__">::enable;
    using type = infer<"__getitem__">::type;
};


/* Enables the C++ subscript assignment operator for any `py::Object` subclass, which
must return void.  The default specialization delegates to Python by introspecting
`__getattr__<Self, "__setitem__">`, which must return a member function, possibly with
Python-style argument annotations.  Specializations of this class may implement a
custom call operator to replace the default behavior. */
template <typename Self, typename Value, typename... Key>
struct __setitem__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<Value, Key...>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__setitem__">::enable;
    using type = infer<"__setitem__">::type;
};


/* Enables the C++ subscript deletion operator for any `py::Object` subclass, which
must return void.  The default specialization delegates to Python by introspecting
`__getattr__<Self, "__delitem__">`, which must return a member function, possibly with
Python-style argument annotations.  Specializations of this class may implement a
custom call operator to replace the default behavior. */
template <typename Self, typename... Key>
struct __delitem__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<Key...>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__delitem__">::enable;
    using type = infer<"__delitem__">::type;
};


/* Enables the C++ size operator for any `py::Object` subclass, which must return
`size_t` for consistency with the C++ API.  The default specialization delegates to
Python by introspecting `__getattr__<Self, "__len__">`, which must return a member
function, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename Self>
struct __len__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__len__">::enable;
    using type = infer<"__len__">::type;
};


/* Enables the C++ iteration operators for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__iter__">`,
which must return a member function, possibly with Python-style argument annotations.
Custom specializations of this struct are expected to implement the iteration protocol
directly inline, as if they were implementing a C++ `begin` iterator class, which will
always be initialized with the `Self` argument and nothing else.  The end iterator is
always given as `py::impl::Sentinel`, which is an empty struct against which the
`__iter__` specialization must be comparable. */
template <typename Self>
struct __iter__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> requires (
            T::has_self && T::template bind<> &&
            std::derived_from<typename T::Return, impl::IterTag>
        )
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = T::Return::value_type;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__iter__">::enable;
    using type = infer<"__iter__">::type;
};


/* Enables the C++ reverse iteration operators for any `py::Object` subclass.  The
default specialization delegates to Python by introspecting
`__getattr__<Self, "__reversed__">`, which must return a member function, possibly with
Python-style argument annotations.  Custom specializations of this struct are expected
to implement the iteration protocol directly inline, as if they were implementing a C++
`std::views` adaptor, which will always be initialized with the `Self` argument and
nothing else.  From there, it can implement its own nested iterator types, which must
be returned from a member `begin()` and `end()` method within the `__iter__`
specialization itself. */
template <typename Self>
struct __reversed__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> requires (
            T::has_self && T::template bind<> &&
            std::derived_from<typename T::Return, impl::IterTag>
        )
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = T::Return::value_type;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__reversed__">::enable;
    using type = infer<"__reversed__">::type;
};


/* Enables the C++ `.in()` method for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__contains__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior.  */
template <typename Self, typename Key>
struct __contains__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<Key>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__contains__">::enable;
    using type = infer<"__contains__">::type;
};


/* Enables `std::hash<>` for any `py::Object` subclass, which must return `size_t` for
consistency with the C++ API.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__hash__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self>
struct __hash__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__hash__">::enable;
    using type = infer<"__hash__">::type;
};


/* Enables `std::abs()` for any `py::Object` subclass.  The default specialization
delegates to Python by introspecting `__getattr__<Self, "__abs__">`, which must return
a member function, possibly with Python-style argument annotations.  Specializations of
this class may implement a custom call operator to replace the default behavior. */
template <typename Self>
struct __abs__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__abs__">::enable;
    using type = infer<"__abs__">::type;
};


/* Enables the C++ unary `~` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__invert__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __invert__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__invert__">::enable;
    using type = infer<"__invert__">::type;
};


/* Enables the C++ unary `+` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__pos__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __pos__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__pos__">::enable;
    using type = infer<"__pos__">::type;
};


/* Enables the C++ unary `-` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__neg__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __neg__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__neg__">::enable;
    using type = infer<"__neg__">::type;
};


/* Enables the C++ prefix `++` operator for any `py::Object` subclass.  Defaults to
disabled, since there is no equivalent Python operator.  Specializations of this class
may implement a custom call operator to implement this operator.

Note that postfix `++` is not supported for Python objects, since it would not respect
C++ copy semantics. */
template <typename Self>
struct __increment__                                        : Disable {};


/* Enables the C++ prefix `--` operator for any `py::Object` subclass.  Defaults to
disabled, since there is no equivalent Python operator.  Specializations of this class
may implement a custom call operator to implement this operator.

Note that postfix `--` is not supported for Python objects, since it would not respect
C++ copy semantics. */
template <typename Self>
struct __decrement__                                        : Disable {};


/* Enables the C++ binary `<` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__lt__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __lt__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__lt__">::enable;
    using type = infer<"__lt__">::type;
};


/* Enables the C++ binary `<=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__le__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __le__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__le__">::enable;
    using type = infer<"__le__">::type;
};


/* Enables the C++ binary `==` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__eq__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __eq__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__eq__">::enable;
    using type = infer<"__eq__">::type;
};


/* Enables the C++ binary `!=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ne__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ne__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ne__">::enable;
    using type = infer<"__ne__">::type;
};


/* Enables the C++ binary `>=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ge__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ge__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ge__">::enable;
    using type = infer<"__ge__">::type;
};


/* Enables the C++ binary `>` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__gt__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __gt__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__gt__">::enable;
    using type = infer<"__gt__">::type;
};


/* Enables the C++ binary `+` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__add__">`
or `__getattr__<R, "__radd__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __add__ {
    template <StaticStr name>
    struct add {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct add<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct radd {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct radd<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable = add<"__add__">::enable || radd<"__radd__">::enable;
    using type = std::conditional_t<
        add<"__add__">::enable,
        typename add<"__add__">::type,
        std::conditional_t<
            radd<"__radd__">::enable,
            typename radd<"__radd__">::type,
            void
        >
    >;
};


/* Enables the C++ `+=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__iadd__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __iadd__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__iadd__">::enable;
    using type = infer<"__iadd__">::type;
};


/* Enables the C++ binary `-` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__sub__">`
or `__getattr__<R, "__rsub__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __sub__ {
    template <StaticStr name>
    struct sub {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct sub<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rsub {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rsub<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable = sub<"__sub__">::enable || rsub<"__rsub__">::enable;
    using type = std::conditional_t<
        sub<"__sub__">::enable,
        typename sub<"__sub__">::type,
        std::conditional_t<
            rsub<"__rsub__">::enable,
            typename rsub<"__rsub__">::type,
            void
        >
    >;
};


/* Enables the C++ `-=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__isub__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __isub__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__isub__">::enable;
    using type = infer<"__isub__">::type;
};


/* Enables the C++ binary `*` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__mul__">`
or `__getattr__<R, "__rmul__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __mul__ {
    template <StaticStr name>
    struct mul {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct mul<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rmul {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rmul<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable = mul<"__mul__">::enable || rmul<"__rmul__">::enable;
    using type = std::conditional_t<
        mul<"__mul__">::enable,
        typename mul<"__mul__">::type,
        std::conditional_t<
            rmul<"__rmul__">::enable,
            typename rmul<"__rmul__">::type,
            void
        >
    >;
};


/* Enables the C++ `*=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__imul__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __imul__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__imul__">::enable;
    using type = infer<"__imul__">::type;
};


/* Enables the C++ binary `/` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either
`__getattr__<L, "__truediv__">` or `__getattr__<R, "__rtruediv__">` in that order, both
of which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __truediv__ {
    template <StaticStr name>
    struct truediv {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct truediv<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rtruediv {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rtruediv<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        truediv<"__truediv__">::enable || rtruediv<"__rtruediv__">::enable;
    using type = std::conditional_t<
        truediv<"__truediv__">::enable,
        typename truediv<"__truediv__">::type,
        std::conditional_t<
            rtruediv<"__rtruediv__">::enable,
            typename rtruediv<"__rtruediv__">::type,
            void
        >
    >;
};


/* Enables the C++ `/=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__itruediv__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __itruediv__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__itruediv__">::enable;
    using type = infer<"__itruediv__">::type;
};


/* Implements the Python `//` operator logic in C++ for any `py::Object` subclass,
which has no corresponding C++ operator.  The default specialization delegates to
Python by introspecting either `__getattr__<L, "__floordiv__">` or
`__getattr__<R, "__rfloordiv__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior.

This is used internally to implement `py::div()`, `py::mod()`, `py::divmod()`, and
`py::round()`, which have a wide variety of fully customizable rounding strategies
based on this operator. */
template <typename L, typename R>
struct __floordiv__ {
    template <StaticStr name>
    struct floordiv {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct floordiv<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rfloordiv {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rfloordiv<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        floordiv<"__floordiv__">::enable || rfloordiv<"__rfloordiv__">::enable;
    using type = std::conditional_t<
        floordiv<"__floordiv__">::enable,
        typename floordiv<"__floordiv__">::type,
        std::conditional_t<
            rfloordiv<"__rfloordiv__">::enable,
            typename rfloordiv<"__rfloordiv__">::type,
            void
        >
    >;
};


/* Implements the Python `//=` operator logic in C++ for any `py::Object` subclass,
which has no corresponding C++ operator.  The default specialization delegates to
Python by introspecting `__getattr__<L, "__ifloordiv__">`, which must return a member
function, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior.

This is used internally to implement `py::div()`, `py::mod()`, `py::divmod()`, and
`py::round()`, which have a wide variety of fully customizable rounding strategies
based on this operator. */
template <typename L, typename R>
struct __ifloordiv__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ifloordiv__">::enable;
    using type = infer<"__ifloordiv__">::type;
};


/* Enables the C++ binary `%` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__mod__">`
or `__getattr__<R, "__rmod__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __mod__ {
    template <StaticStr name>
    struct mod {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct mod<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rmod {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rmod<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable = mod<"__mod__">::enable || rmod<"__rmod__">::enable;
    using type = std::conditional_t<
        mod<"__mod__">::enable,
        typename mod<"__mod__">::type,
        std::conditional_t<
            rmod<"__rmod__">::enable,
            typename rmod<"__rmod__">::type,
            void
        >
    >;
};


/* Enables the C++ `%=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__imod__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __imod__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__imod__">::enable;
    using type = infer<"__imod__">::type;
};


/// TODO: __pow__ needs to accept an optional third argument that defaults to void in
/// order to support the ternary form of the pow() function.  This is not currently


/* Implements the Python `pow()` operator logic in C++ for any `py::Object` subclass,
which has no corresponding C++ operator.  The default specialization delegates to
Python by introspecting either `__getattr__<L, "__pow__">` or
`__getattr__<R, "__rpow__">` in that order, both of which must return member functions,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior.

This is used internally to implement `py::pow()`. */
template <typename Base, typename Exp, typename Mod = void>
struct __pow__ {
    template <StaticStr name>
    struct pow {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Base, name>::enable)
    struct pow<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            /// TODO: this needs to be updated to support the ternary form of pow()
            static constexpr bool enable = T::has_self && T::template bind<Exp>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Base, name>::type>::enable;
        using type = inspect<typename __getattr__<Base, name>::type>::type;
    };
    static constexpr bool enable = pow<"__pow__">::enable || pow<"__rpow__">::enable;
    using type = std::conditional_t<
        pow<"__pow__">::enable,
        typename pow<"__pow__">::type,
        std::conditional_t<
            pow<"__rpow__">::enable,
            typename pow<"__rpow__">::type,
            void
        >
    >;
};


/* Implements the Python `**=` operator logic in C++ for any `py::Object` subclass,
which has no corresponding C++ operator.  The default specialization delegates to
Python by introspecting `__getattr__<L, "__ipow__">`, which must return a member
function, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior.

This is used internally to implement `py::ipow()`. */
template <typename Base, typename Exp, typename Mod = void>
struct __ipow__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Base, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            /// TODO: same as for ternary form of pow()
            static constexpr bool enable = T::has_self && T::template bind<Exp>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Base, name>::type>::enable;
        using type = inspect<typename __getattr__<Base, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ipow__">::enable;
    using type = infer<"__ipow__">::type;
};


/* Enables the C++ binary `<<` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__lshift__">`
or `__getattr__<R, "__rlshift__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __lshift__ {
    template <StaticStr name>
    struct lshift {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct lshift<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rlshift {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rlshift<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        lshift<"__lshift__">::enable || rlshift<"__rlshift__">::enable;
    using type = std::conditional_t<
        lshift<"__lshift__">::enable,
        typename lshift<"__lshift__">::type,
        std::conditional_t<
            rlshift<"__rlshift__">::enable,
            typename rlshift<"__rlshift__">::type,
            void
        >
    >;
};


/* Enables the C++ `<<=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ilshift__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ilshift__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ilshift__">::enable;
    using type = infer<"__ilshift__">::type;
};


/* Enables the C++ binary `>>` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__rshift__">`
or `__getattr__<R, "__rrshift__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __rshift__ {
    template <StaticStr name>
    struct rshift {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct rshift<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rrshift {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rrshift<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        rshift<"__rshift__">::enable || rrshift<"__rrshift__">::enable;
    using type = std::conditional_t<
        rshift<"__rshift__">::enable,
        typename rshift<"__rshift__">::type,
        std::conditional_t<
            rrshift<"__rrshift__">::enable,
            typename rrshift<"__rrshift__">::type,
            void
        >
    >;
};


/* Enables the C++ `>>=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__irshift__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __irshift__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__irshift__">::enable;
    using type = infer<"__irshift__">::type;
};


/* Enables the C++ binary `&` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__and__">`
or `__getattr__<R, "__rand__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __and__ {
    template <StaticStr name>
    struct _and {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct _and<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rand {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rand<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable = _and<"__and__">::enable || rand<"__rand__">::enable;
    using type = std::conditional_t<
        _and<"__and__">::enable,
        typename _and<"__and__">::type,
        std::conditional_t<
            rand<"__rand__">::enable,
            typename rand<"__rand__">::type,
            void
        >
    >;
};


/* Enables the C++ `&=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__iand__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __iand__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__iand__">::enable;
    using type = infer<"__iand__">::type;
};


/* Enables the C++ binary `|` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__or__">`
or `__getattr__<R, "__ror__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __or__ {
    template <StaticStr name>
    struct _or {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct _or<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct ror {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct ror<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable = _or<"__or__">::enable || ror<"__ror__">::enable;
    using type = std::conditional_t<
        _or<"__or__">::enable,
        typename _or<"__or__">::type,
        std::conditional_t<
            ror<"__ror__">::enable,
            typename ror<"__ror__">::type,
            void
        >
    >;
};


/* Enables the C++ `|=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ior__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ior__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ior__">::enable;
    using type = infer<"__ior__">::type;
};


/* Enables the C++ binary `^` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__xor__">`
or `__getattr__<R, "__rxor__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __xor__ {
    template <StaticStr name>
    struct _xor {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct _xor<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <StaticStr name>
    struct rxor {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
    struct rxor<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T>
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<L>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable = _xor<"__xor__">::enable || rxor<"__rxor__">::enable;
    using type = std::conditional_t<
        _xor<"__xor__">::enable,
        typename _xor<"__xor__">::type,
        std::conditional_t<
            rxor<"__rxor__">::enable,
            typename rxor<"__rxor__">::type,
            void
        >
    >;
};


/* Enables the C++ `^=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ixor__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ixor__ {
    template <StaticStr name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <std::derived_from<impl::FunctionTag> T> 
        struct inspect<T> {
            static constexpr bool enable = T::has_self && T::template bind<R>;
            using type = T::Return;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ixor__">::enable;
    using type = infer<"__ixor__">::type;
};


/* A Python interface mixin which can be used to reflect multiple inheritance within
the Object hierarchy.

When mixed with an Object base class, this class allows its interface to be separated
from the underlying PyObject* pointer, meaning several interfaces can be mixed together
without affecting the object's binary layout.  Each interface MUST use an explicit
auto this parameter to access the PyObject* pointer, which eliminates the need for
additional casting and ensures that lazy evaluation of the pointer works correctly for
Item and Attr proxies.  The interface can then further cast the pointer to a specific
PyObject* type if necessary to access internal fields of the Python representation.

This class must be specialized for all types that wish to support multiple inheritance.
Doing so is rather tricky due to the circular dependency between the Object and its
Interface, so here's a simple example to illustrate how it's done:

    // forward declare the Object wrapper
    struct Wrapper;

    // define the wrapper's interface, mixing in the interfaces of its base classes
    template <>
    struct Interface<Wrapper> : Interface<Base1>, Interface<Base2>, ... {
        void foo();  // forward declarations for interface methods
        int bar() const;
        static std::string baz();
    };

    // define the wrapper, mixing the interface with Object
    struct Wrapper : Object, Interface<Wrapper> {
        struct __python__ : def<__python__, Wrapper, SomeCppObj> {
            static Type __export__(Bindings bindings) {
                // export a C++ object's interface to Python.  The base classes
                // reflect in Python the interface inheritance we defined here.
                return bindings.template finalize<Base1, Base2, ...>();
            }
        };

        // Alternatively, if the Wrapper represents a pure Python class:
        struct __python__ : def<__python__, Wrapper>, PyObject {  // inherit from a PyObject type
            // __export__() is not necessary in this case
            static Type __import__() {
                // get a reference to the external Python class, perhaps by importing
                // a module and getting a reference to the class object
            }
        };

        // You can also define a pure Python type directly inline:
        struct __python__ : def<__python__, Wrapper>, PyObject {  // inherit from a PyObject type
            std::unordered_map<std::string, int> foo;
            std::vector<Object> bar;
            // ... C++ fields are held within the object's Python representation

            static Type __export__(Bindings bindings) {
                // export the type's interface to Python
                return bindings.template finalize<Base1, Base2, ...>();
            }
        };

        // define standard Object constructors
        Wrapper(PyObject* h, borrowed_t t) : Object(h, t) {}
        Wrapper(PyObject* h, stolen_t t) : Object(h, t) {}

        template <typename... Args> requires (implicit_ctor<Wrapper>::enable<Args...>)
        Wrapper(Args&&... args) : Object(
            implicit_ctor<Wrapper>{},
            std::forward<Args>(args)...
        ) {}

        template <typename... Args> requires (explicit_ctor<Wrapper>::enable<Args...>)
        explicit Wrapper(Args&&... args) : Object(
            explicit_ctor<Wrapper>{},
            std::forward<Args>(args)...
        ) {}
    };

    // define the type's interface so that it mimics Python's MRO
    template <>
    struct Interface<Type<Wrapper>> : Interface<Type<Base1>>, Interface<Type<Base2>>, ... {
        static void foo(auto& self) {  // non-static methods gain an auto self parameter
            self.foo();
        }
        static int bar(const auto& self) {
            return self.bar();
        }
        static std::string baz() {  // static methods stay the same
            return Wrapper::baz();
        }
    };

    // specialize the necessary control structures
    template <>
    struct __getattr__<Wrapper, "foo"> : Returns<Function<void()>> {};
    template <>
    struct __getattr__<Wrapper, "bar"> : Returns<Function<int()>> {};
    template <>
    struct __getattr__<Wrapper, "baz"> : Returns<Function<std::string()>> {};
    template <>
    struct __getattr__<Type<Wrapper>, "foo"> : Returns<Function<void(Wrapper&)>> {};
    template <>
    struct __getattr__<Type<Wrapper>, "bar"> : Returns<Function<int(const Wrapper&)>> {};
    template <>
    struct __getattr__<Type<Wrapper>, "baz"> : Returns<Function<std::string()>> {};
    // ... for all supported C++ operators

    // implement the interface methods
    void Interface<Wrapper>::foo() {
        print("Hello, world!");
    }
    int Interface<Wrapper>::bar() const {
        return 42;
    }
    std::string Interface<Wrapper>::baz() {
        return "static methods work too!";
    }
    void Interface<Type<Wrapper>>::foo(auto& self) {
        self.foo();
    }
    int Interface<Type<Wrapper>>::bar(const auto& self) {
        return self.bar();
    }
    std::string Interface<Type<Wrapper>>::baz() {
        return Wrapper::baz();
    }

This pattern is fairly rigid, as the forward declarations are necessary to prevent
circular dependencies from causing compilation errors.  It also encourages the same
interface to be defined for both the Object and its Type, as well as its Python
representation, so that they can be treated symmetrically across all languages. 
However, the upside is that once it has been set up, this block of code is fully
self-contained, ensures that both the Python and C++ interfaces are kept in sync, and
can represent complex inheritance hierarchies with ease.  By inheriting from
interfaces, the C++ Object types can directly mirror any Python class hierarchy, even
accounting for multiple inheritance.  In fact, with a few `using` declarations to
resolve conflicts, the Object and its Type can even model Python-style MRO, or expose
multiple overloads at the same time. */
template <typename T>
struct Interface;


namespace impl {

    /* Trigger implicit conversion operators and/or implicit constructors, but not
    explicit ones.  In contrast, static_cast<>() will trigger explicit constructors on
    the target type, which can give unexpected results and violate type safety. */
    template <typename U>
    decltype(auto) implicit_cast(U&& value) {
        return std::forward<U>(value);
    }

    template <typename L, typename R>
    concept is = std::same_as<std::remove_cvref_t<L>, std::remove_cvref_t<R>>;

    template <typename L, typename R>
    concept inherits = std::derived_from<std::remove_cvref_t<L>, std::remove_cvref_t<R>>;

    template <typename T>
    concept is_const = std::is_const_v<std::remove_reference_t<T>>;

    template <typename T>
    concept is_volatile = std::is_volatile_v<std::remove_reference_t<T>>;

    template <typename T>
    concept bertrand = std::derived_from<std::remove_cvref_t<T>, BertrandTag>;

    template <typename T>
    concept python = std::derived_from<std::remove_cvref_t<T>, Object>;

    template <typename T>
    concept cpp = !python<T>;

    template <typename T>
    concept dynamic = is<T, Object>;

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(From from) {
        { static_cast<To>(from) } -> std::same_as<To>;
    };

    template <typename... Ts>
    constexpr bool types_are_unique = true;
    template <typename T, typename... Ts>
    constexpr bool types_are_unique<T, Ts...> =
        !(std::same_as<T, Ts> || ...) && types_are_unique<Ts...>;

    template <typename T, typename Self>
    struct _qualify { using type = T; };
    template <typename T, typename Self>
    struct _qualify<T, const Self> { using type = const T; };
    template <typename T, typename Self>
    struct _qualify<T, volatile Self> { using type = volatile T; };
    template <typename T, typename Self>
    struct _qualify<T, const volatile Self> { using type = const volatile T; };
    template <typename T, typename Self>
    struct _qualify<T, Self&> { using type = T&; };
    template <typename T, typename Self>
    struct _qualify<T, const Self&> { using type = const T&; };
    template <typename T, typename Self>
    struct _qualify<T, volatile Self&> { using type = volatile T&; };
    template <typename T, typename Self>
    struct _qualify<T, const volatile Self&> { using type = const volatile T&; };
    template <typename T, typename Self>
    struct _qualify<T, Self&&> { using type = T&&; };
    template <typename T, typename Self>
    struct _qualify<T, const Self&&> { using type = const T&&; };
    template <typename T, typename Self>
    struct _qualify<T, volatile Self&&> { using type = volatile T&&; };
    template <typename T, typename Self>
    struct _qualify<T, const volatile Self&&> { using type = const volatile T&&; };
    template <typename Self>
    struct _qualify<void, Self> { using type = void; };
    template <typename Self>
    struct _qualify<void, const Self> { using type = void; };
    template <typename Self>
    struct _qualify<void, volatile Self> { using type = void; };
    template <typename Self>
    struct _qualify<void, const volatile Self> { using type = void; };
    template <typename Self>
    struct _qualify<void, Self&> { using type = void; };
    template <typename Self>
    struct _qualify<void, const Self&> { using type = void; };
    template <typename Self>
    struct _qualify<void, volatile Self&> { using type = void; };
    template <typename Self>
    struct _qualify<void, const volatile Self&> { using type = void; };
    template <typename Self>
    struct _qualify<void, Self&&> { using type = void; };
    template <typename Self>
    struct _qualify<void, const Self&&> { using type = void; };
    template <typename Self>
    struct _qualify<void, volatile Self&&> { using type = void; };
    template <typename Self>
    struct _qualify<void, const volatile Self&&> { using type = void; };
    template <typename T, typename Self>
    using qualify = _qualify<T, Self>::type;
    template <typename T, typename Self>
    using qualify_lvalue = std::add_lvalue_reference_t<qualify<T, Self>>;
    template <typename T, typename Self>
    using qualify_pointer = std::add_pointer_t<std::remove_reference_t<qualify<T, Self>>>;

    template <typename T, typename = void>
    constexpr bool has_interface_helper = false;
    template <typename T>
    constexpr bool has_interface_helper<T, std::void_t<Interface<T>>> = true;
    template <typename T>
    concept has_interface = has_interface_helper<std::remove_cvref_t<T>>;

    template <typename T, typename = void>
    constexpr bool has_type_helper = false;
    template <typename T>
    constexpr bool has_type_helper<T, std::void_t<Type<T>>> = true;
    template <typename T>
    concept has_type = has_type_helper<std::remove_cvref_t<T>>;

    template <typename T, typename = void>
    struct python_helper {
        static constexpr bool enable = __cast__<T>::enable;
        template <typename U>
        struct helper { using type = void; };
        template <typename U> requires (__cast__<U>::enable)
        struct helper<U> { using type = __cast__<U>::type; };
        using type = helper<T>::type;
    };
    template <typename T>
    struct python_helper<T, std::void_t<
        typename std::remove_cvref_t<T>::__python__::__object__>
    > {
        static constexpr bool enable = std::same_as<
            std::remove_cvref_t<T>,
            typename std::remove_cvref_t<T>::__python__::__object__
        >;
        using type = T;
    };
    template <typename T>
    concept has_python = python_helper<T>::enable;
    template <typename T>
    using python_type = python_helper<T>::type;

    template <typename T, typename = void>
    constexpr bool has_export_helper = false;
    template <typename T>
    constexpr bool has_export_helper<T, std::void_t<decltype(&T::__python__::__export__)>> =
        std::is_function_v<decltype(T::__python__::__export__)> &&
        std::is_invocable_v<decltype(T::__python__::__export__)>;
    template <typename T>
    concept has_export =
        python<T> && has_python<T> && has_export_helper<std::remove_cvref_t<T>>;

    template <typename T, typename = void>
    constexpr bool has_import_helper = false;
    template <typename T>
    constexpr bool has_import_helper<T, std::void_t<decltype(&T::__python__::__import__)>> =
        std::is_function_v<decltype(T::__python__::__import__)> &&
        std::is_invocable_v<decltype(T::__python__::__import__)>;
    template <typename T>
    concept has_import =
        python<T> && has_python<T> && has_import_helper<std::remove_cvref_t<T>>;

    template <typename T, typename = void>
    struct cpp_helper {
        static constexpr bool enable = cpp<T>;
        using type = T;
    };
    template <typename T>
    struct cpp_helper<T, std::void_t<typename std::remove_cvref_t<T>::__python__::__cpp__>> {
        static constexpr bool enable = requires() {
            { &std::remove_cvref_t<T>::__python__::m_cpp };
        };
        using type = std::remove_cvref_t<T>::__python__::__cpp__;
    };
    template <typename T>
    concept has_cpp = cpp_helper<T>::enable;
    template <typename T> requires (cpp_helper<T>::enable)
    using cpp_type = cpp_helper<T>::type;

    template <typename Self, StaticStr Name>
        requires (__getattr__<Self, Name>::enable)
    struct Attr;
    template <typename T>
    struct attr_helper { static constexpr bool enable = false; };
    template <typename Self, StaticStr Name>
    struct attr_helper<Attr<Self, Name>> {
        static constexpr bool enable = true;
        using type = __getattr__<Self, Name>::type;
    };
    template <typename T>
    concept is_attr = attr_helper<std::remove_cvref_t<T>>::enable;
    template <is_attr T>
    using attr_type = attr_helper<std::remove_cvref_t<T>>::type;

    template <typename T, StaticStr Name, typename... Args>
    concept attr_is_callable_with =
        __getattr__<T, Name>::enable &&
        std::is_invocable_v<typename __getattr__<T, Name>::type, Args...>;

    template <typename Container, typename... Key>
        requires (__getitem__<Container, Key...>::enable)
    struct Item;
    template <typename T>
    struct item_helper { static constexpr bool enable = false; };
    template <typename Container, typename... Key>
    struct item_helper<Item<Container, Key...>> {
        static constexpr bool enable = true;
        using type = __getitem__<Container, Key...>::type;
    };
    template <typename T>
    concept is_item = item_helper<std::remove_cvref_t<T>>::enable;
    template <is_item T>
    using item_type = item_helper<std::remove_cvref_t<T>>::type;

    template <typename T>
    concept lazily_evaluated = is_attr<T> || is_item<T>;

    template <typename T>
    struct lazy_type_helper {};
    template <is_attr T>
    struct lazy_type_helper<T> { using type = attr_type<T>; };
    template <is_item T>
    struct lazy_type_helper<T> { using type = item_type<T>; };
    template <lazily_evaluated T>
    using lazy_type = lazy_type_helper<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct optional_helper { static constexpr bool value = false; };
    template <typename T>
    struct optional_helper<std::optional<T>> {
        static constexpr bool value = true;
        using type = T;
    };
    template <typename T>
    concept is_optional = optional_helper<std::remove_cvref_t<T>>::value;
    template <is_optional T>
    using optional_type = optional_helper<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct variant_helper { static constexpr bool value = false; };
    template <typename... Ts>
    struct variant_helper<std::variant<Ts...>> {
        static constexpr bool value = true;
        using types = std::tuple<Ts...>;
    };
    template <typename T>
    concept is_variant = variant_helper<std::remove_cvref_t<T>>::value;
    template <is_variant T>
    using variant_types = typename variant_helper<std::remove_cvref_t<T>>::types;

    template <typename T>
    struct ptr_helper { static constexpr bool value = false; };
    template <typename T>
    struct ptr_helper<T*> {
        static constexpr bool value = true;
        using type = T;
    };
    template <typename T>
    concept is_ptr = ptr_helper<std::remove_cvref_t<T>>::value;
    template <is_ptr T>
    using ptr_type = ptr_helper<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct shared_ptr_helper { static constexpr bool enable = false; };
    template <typename T>
    struct shared_ptr_helper<std::shared_ptr<T>> {
        static constexpr bool enable = true;
        using type = T;
    };
    template <typename T>
    concept is_shared_ptr = shared_ptr_helper<std::remove_cvref_t<T>>::enable;
    template <is_shared_ptr T>
    using shared_ptr_type = shared_ptr_helper<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct unique_ptr_helper { static constexpr bool enable = false; };
    template <typename T>
    struct unique_ptr_helper<std::unique_ptr<T>> {
        static constexpr bool enable = true;
        using type = T;
    };
    template <typename T>
    concept is_unique_ptr = unique_ptr_helper<std::remove_cvref_t<T>>::enable;
    template <is_unique_ptr T>
    using unique_ptr_type = unique_ptr_helper<std::remove_cvref_t<T>>::type;

    template <typename T>
    concept string_literal = requires(T t) {
        { []<size_t N>(const char(&)[N]){}(t) };
    };

    template <typename T>
    concept iterable = requires(T& t) {
        { std::ranges::begin(t) } -> std::input_or_output_iterator;
        { std::ranges::end(t) } -> std::sentinel_for<decltype(std::ranges::begin(t))>;
    };

    template <iterable T>
    using iter_type = decltype(*std::ranges::begin(
        std::declval<std::add_lvalue_reference_t<T>>()
    ));

    template <typename T, typename Value>
    concept yields = iterable<T> && std::convertible_to<iter_type<T>, Value>;

    template <typename T>
    concept reverse_iterable = requires(T& t) {
        { std::ranges::rbegin(t) } -> std::input_or_output_iterator;
        { std::ranges::rend(t) } -> std::sentinel_for<decltype(std::ranges::rbegin(t))>;
    };

    template <reverse_iterable T>
    using reverse_iter_type = decltype(*std::ranges::rbegin(
        std::declval<std::add_lvalue_reference_t<T>>()
    ));

    template <typename T, typename Value>
    concept yields_reverse =
        reverse_iterable<T> && std::convertible_to<reverse_iter_type<T>, Value>;

    template <typename T>
    concept has_size = requires(T t) {
        { std::ranges::size(t) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_empty = requires(T t) {
        { std::ranges::empty(t) } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept sequence_like = iterable<T> && has_size<T> && requires(T t) {
        { t[0] } -> std::convertible_to<iter_type<T>>;
    };

    template <typename T>
    concept mapping_like = requires(T t) {
        typename std::remove_cvref_t<T>::key_type;
        typename std::remove_cvref_t<T>::mapped_type;
        { t[std::declval<typename std::remove_cvref_t<T>::key_type>()] } ->
            std::convertible_to<typename std::remove_cvref_t<T>::mapped_type>;
    };

    template <typename T, typename... Key>
    concept supports_lookup =
        !std::is_pointer_v<T> &&
        !std::integral<std::remove_cvref_t<T>> &&
        requires(T t, Key... key) {
            { t[key...] };
        };

    template <typename T, typename... Key> requires (supports_lookup<T, Key...>)
    using lookup_type = decltype(std::declval<T>()[std::declval<Key>()...]);

    template <typename T, typename Value, typename... Key>
    concept lookup_yields = supports_lookup<T, Key...> && requires(T t, Key... key) {
        { t[key...] } -> std::convertible_to<Value>;
    };

    template <typename T, typename Value, typename... Key>
    concept supports_item_assignment =
        !std::is_pointer_v<T> &&
        !std::integral<std::remove_cvref_t<T>> &&
        requires(T t, Key... key, Value value) {
            { t[key...] = value };
        };

    template <typename T>
    concept pair_like = std::tuple_size<T>::value == 2 && requires(T t) {
        { std::get<0>(t) };
        { std::get<1>(t) };
    };

    template <typename T, typename First, typename Second>
    concept pair_like_with = pair_like<T> && requires(T t) {
        { std::get<0>(t) } -> std::convertible_to<First>;
        { std::get<1>(t) } -> std::convertible_to<Second>;
    };

    template <typename T>
    concept yields_pairs = iterable<T> && pair_like<iter_type<T>>;

    template <typename T, typename First, typename Second>
    concept yields_pairs_with =
        iterable<T> && pair_like_with<iter_type<T>, First, Second>;

    template <typename T>
    concept has_to_string = requires(T t) {
        { std::to_string(t) } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_stream_insertion = requires(std::ostream& os, T t) {
        { os << t } -> std::convertible_to<std::ostream&>;
    };

    template <typename T>
    concept has_call_operator = requires() {
        { &std::remove_cvref_t<T>::operator() };
    };

    template <typename T>
    concept complex_like = requires(T t) {
        { t.real() } -> std::convertible_to<double>;
        { t.imag() } -> std::convertible_to<double>;
    };

    template <typename T>
    concept has_reserve = requires(T t, size_t n) {
        { t.reserve(n) } -> std::same_as<void>;
    };

    template <typename T, typename Key>
    concept has_contains = requires(T t, Key key) {
        { t.contains(key) } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept has_keys = requires(T t) {
        { t.keys() } -> iterable;
        { t.keys() } -> yields<typename std::remove_cvref_t<T>::key_type>;
    };

    template <typename T>
    concept has_values = requires(T t) {
        { t.values() } -> iterable;
        { t.values() } -> yields<typename std::remove_cvref_t<T>::mapped_type>;
    };

    template <typename T>
    concept has_items = requires(T t) {
        { t.items() } -> iterable;
        { t.items() } -> yields_pairs_with<
            typename std::remove_cvref_t<T>::key_type,
            typename std::remove_cvref_t<T>::mapped_type
        >;
    };

    template <typename T>
    concept has_operator_bool = requires(T t) {
        { !t } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept hashable = requires(T t) {
        { std::hash<std::decay_t<T>>{}(t) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_abs = requires(T t) {{ std::abs(t) };};
    template <has_abs T>
    using abs_type = decltype(std::abs(std::declval<T>()));
    template <typename T, typename Return>
    concept abs_returns = requires(T t) {
        { std::abs(t) } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_invert = requires(T t) {{ ~t };};
    template <has_invert T>
    using invert_type = decltype(~std::declval<T>());
    template <typename T, typename Return>
    concept invert_returns = requires(T t) {
        { ~t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_pos = requires(T t) {{ +t };};
    template <has_pos T>
    using pos_type = decltype(+std::declval<T>());
    template <typename T, typename Return>
    concept pos_returns = requires(T t) {
        { +t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_neg = requires(T t) {{ -t };};
    template <has_neg T>
    using neg_type = decltype(-std::declval<T>());
    template <typename T, typename Return>
    concept neg_returns = requires(T t) {
        { -t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_preincrement = requires(T t) {{ ++t };};
    template <has_preincrement T>
    using preincrement_type = decltype(++std::declval<T>());
    template <typename T, typename Return>
    concept preincrement_returns = requires(T t) {
        { ++t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_postincrement = requires(T t) {{ t++ };};
    template <has_postincrement T>
    using postincrement_type = decltype(std::declval<T>()++);
    template <typename T, typename Return>
    concept postincrement_returns = requires(T t) {
        { t++ } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_predecrement = requires(T t) {{ --t };};
    template <has_predecrement T>
    using predecrement_type = decltype(--std::declval<T>());
    template <typename T, typename Return>
    concept predecrement_returns = requires(T t) {
        { --t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_postdecrement = requires(T t) {{ t-- };};
    template <has_postdecrement T>
    using postdecrement_type = decltype(std::declval<T>()--);
    template <typename T, typename Return>
    concept postdecrement_returns = requires(T t) {
        { t-- } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_lt = requires(L l, R r) {{ l < r };};
    template <typename L, typename R> requires (has_lt<L, R>)
    using lt_type = decltype(std::declval<L>() < std::declval<R>());
    template <typename L, typename R, typename Return>
    concept lt_returns = requires(L l, R r) {
        { l < r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_le = requires(L l, R r) {{ l <= r };};
    template <typename L, typename R> requires (has_le<L, R>)
    using le_type = decltype(std::declval<L>() <= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept le_returns = requires(L l, R r) {
        { l <= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_eq = requires(L l, R r) {{ l == r };};
    template <typename L, typename R> requires (has_eq<L, R>)
    using eq_type = decltype(std::declval<L>() == std::declval<R>());
    template <typename L, typename R, typename Return>
    concept eq_returns = requires(L l, R r) {
        { l == r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ne = requires(L l, R r) {{ l != r };};
    template <typename L, typename R> requires (has_ne<L, R>)
    using ne_type = decltype(std::declval<L>() != std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ne_returns = requires(L l, R r) {
        { l != r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ge = requires(L l, R r) {{ l >= r };};
    template <typename L, typename R> requires (has_ge<L, R>)
    using ge_type = decltype(std::declval<L>() >= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ge_returns = requires(L l, R r) {
        { l >= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_gt = requires(L l, R r) {{ l > r };};
    template <typename L, typename R> requires (has_gt<L, R>)
    using gt_type = decltype(std::declval<L>() > std::declval<R>());
    template <typename L, typename R, typename Return>
    concept gt_returns = requires(L l, R r) {
        { l > r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_add = requires(L l, R r) {{ l + r };};
    template <typename L, typename R> requires (has_add<L, R>)
    using add_type = decltype(std::declval<L>() + std::declval<R>());
    template <typename L, typename R, typename Return>
    concept add_returns = requires(L l, R r) {
        { l + r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_iadd = requires(L& l, R r) {{ l += r };};
    template <typename L, typename R> requires (has_iadd<L, R>)
    using iadd_type = decltype(std::declval<L&>() += std::declval<R>());
    template <typename L, typename R, typename Return>
    concept iadd_returns = requires(L& l, R r) {
        { l += r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_sub = requires(L l, R r) {{ l - r };};
    template <typename L, typename R> requires (has_sub<L, R>)
    using sub_type = decltype(std::declval<L>() - std::declval<R>());
    template <typename L, typename R, typename Return>
    concept sub_returns = requires(L l, R r) {
        { l - r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_isub = requires(L& l, R r) {{ l -= r };};
    template <typename L, typename R> requires (has_isub<L, R>)
    using isub_type = decltype(std::declval<L&>() -= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept isub_returns = requires(L& l, R r) {
        { l -= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_mul = requires(L l, R r) {{ l * r };};
    template <typename L, typename R> requires (has_mul<L, R>)
    using mul_type = decltype(std::declval<L>() * std::declval<R>());
    template <typename L, typename R, typename Return>
    concept mul_returns = requires(L l, R r) {
        { l * r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_imul = requires(L& l, R r) {{ l *= r };};
    template <typename L, typename R> requires (has_imul<L, R>)
    using imul_type = decltype(std::declval<L&>() *= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept imul_returns = requires(L& l, R r) {
        { l *= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_truediv = requires(L l, R r) {{ l / r };};
    template <typename L, typename R> requires (has_truediv<L, R>)
    using truediv_type = decltype(std::declval<L>() / std::declval<R>());
    template <typename L, typename R, typename Return>
    concept truediv_returns = requires(L l, R r) {
        { l / r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_itruediv = requires(L& l, R r) {{ l /= r };};
    template <typename L, typename R> requires (has_itruediv<L, R>)
    using itruediv_type = decltype(std::declval<L&>() /= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept itruediv_returns = requires(L& l, R r) {
        { l /= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_mod = requires(L l, R r) {{ l % r };};
    template <typename L, typename R> requires (has_mod<L, R>)
    using mod_type = decltype(std::declval<L>() % std::declval<R>());
    template <typename L, typename R, typename Return>
    concept mod_returns = requires(L l, R r) {
        { l % r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_imod = requires(L& l, R r) {{ l %= r };};
    template <typename L, typename R> requires (has_imod<L, R>)
    using imod_type = decltype(std::declval<L&>() %= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept imod_returns = requires(L& l, R r) {
        { l %= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_pow = requires(L l, R r) {{ std::pow(l, r) };};
    template <typename L, typename R> requires (has_pow<L, R>)
    using pow_type = decltype(std::pow(std::declval<L>(), std::declval<R>()));
    template <typename L, typename R, typename Return>
    concept pow_returns = requires(L l, R r) {
        { std::pow(l, r) } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_lshift = requires(L l, R r) {{ l << r };};
    template <typename L, typename R> requires (has_lshift<L, R>)
    using lshift_type = decltype(std::declval<L>() << std::declval<R>());
    template <typename L, typename R, typename Return>
    concept lshift_returns = requires(L l, R r) {
        { l << r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ilshift = requires(L& l, R r) {{ l <<= r };};
    template <typename L, typename R> requires (has_ilshift<L, R>)
    using ilshift_type = decltype(std::declval<L&>() <<= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ilshift_returns = requires(L& l, R r) {
        { l <<= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_rshift = requires(L l, R r) {{ l >> r };};
    template <typename L, typename R> requires (has_rshift<L, R>)
    using rshift_type = decltype(std::declval<L>() >> std::declval<R>());
    template <typename L, typename R, typename Return>
    concept rshift_returns = requires(L l, R r) {
        { l >> r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_irshift = requires(L& l, R r) {{ l >>= r };};
    template <typename L, typename R> requires (has_irshift<L, R>)
    using irshift_type = decltype(std::declval<L&>() >>= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept irshift_returns = requires(L& l, R r) {
        { l >>= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_and = requires(L l, R r) {{ l & r };};
    template <typename L, typename R> requires (has_and<L, R>)
    using and_type = decltype(std::declval<L>() & std::declval<R>());
    template <typename L, typename R, typename Return>
    concept and_returns = requires(L l, R r) {
        { l & r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_iand = requires(L& l, R r) {{ l &= r };};
    template <typename L, typename R> requires (has_iand<L, R>)
    using iand_type = decltype(std::declval<L&>() &= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept iand_returns = requires(L& l, R r) {
        { l &= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_or = requires(L l, R r) {{ l | r };};
    template <typename L, typename R> requires (has_or<L, R>)
    using or_type = decltype(std::declval<L>() | std::declval<R>());
    template <typename L, typename R, typename Return>
    concept or_returns = requires(L l, R r) {
        { l | r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ior = requires(L& l, R r) {{ l |= r };};
    template <typename L, typename R> requires (has_ior<L, R>)
    using ior_type = decltype(std::declval<L&>() |= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ior_returns = requires(L& l, R r) {
        { l |= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_xor = requires(L l, R r) {{ l ^ r };};
    template <typename L, typename R> requires (has_xor<L, R>)
    using xor_type = decltype(std::declval<L>() ^ std::declval<R>());
    template <typename L, typename R, typename Return>
    concept xor_returns = requires(L l, R r) {
        { l ^ r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ixor = requires(L& l, R r) {{ l ^= r };};
    template <typename L, typename R> requires (has_ixor<L, R>)
    using ixor_type = decltype(std::declval<L&>() ^= std::declval<R>());
    template <typename L, typename R, typename Return>
    concept ixor_returns = requires(L& l, R r) {
        { l ^= r } -> std::convertible_to<Return>;
    };


    /// TODO: eventually I should reconsider the following concepts and potentially
    /// standardize them in some way.  Ideally, I can fully remove them and replace
    /// the logic with converts_to and converts_exact.

    template <typename T, typename Base>
    concept converts_to = __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Base>;

    template <typename T, typename Base>
    concept converts_exact = __cast__<std::remove_cvref_t<T>>::enable &&
        std::same_as<typename __cast__<std::remove_cvref_t<T>>::type, Base>;

    template <typename T>
    concept type_like = converts_to<T, TypeTag>;

    template <typename T>
    concept none_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, NoneType>;

    template <typename T>
    concept notimplemented_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, NotImplementedType>;

    template <typename T>
    concept ellipsis_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, EllipsisType>;

    template <typename T>
    concept module_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, ModuleTag>;

    template <typename T>
    concept bool_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Bool>;

    template <typename T>
    concept int_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Int>;

    template <typename T>
    concept float_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Float>;

    template <typename T>
    concept str_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Str>;

    template <typename T>
    concept bytes_like = (
        string_literal<T> ||
        std::same_as<std::decay_t<T>, void*> || (
            __cast__<std::remove_cvref_t<T>>::enable &&
            std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Bytes>
        )
    );

    template <typename T>
    concept bytearray_like = (
        string_literal<T> ||
        std::same_as<std::decay_t<T>, void*> || (
            __cast__<std::remove_cvref_t<T>>::enable &&
            std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, ByteArray>
        )
    );

    template <typename T>
    concept anybytes_like = bytes_like<T> || bytearray_like<T>;

    template <typename T>
    concept timedelta_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Timedelta>;

    template <typename T>
    concept timezone_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Timezone>;

    template <typename T>
    concept date_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Date>;

    template <typename T>
    concept time_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Time>;

    template <typename T>
    concept datetime_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Datetime>;

    template <typename T>
    concept range_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Range>;

    template <typename T>
    concept tuple_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, TupleTag>;

    template <typename T>
    concept list_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, ListTag>;

    template <typename T>
    concept set_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, SetTag>;

    template <typename T>
    concept frozenset_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, FrozenSetTag>;

    template <typename T>
    concept anyset_like = set_like<T> || frozenset_like<T>;

    template <typename T>
    concept dict_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, DictTag>;

    template <typename T>
    concept mappingproxy_like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, MappingProxyTag>;

    template <typename T>
    concept anydict_like = dict_like<T> || mappingproxy_like<T>;

    /* NOTE: some binary operators (such as lexicographic comparisons) accept generic
     * containers, which may be combined with containers of different types.  In these
     * cases, the operator should be enabled if and only if it is also supported by the
     * respective element types.  This sounds simple, but is complicated by the
     * implementation of std::pair and std::tuple, which may contain heterogenous types.
     *
     * The Broadcast<> struct helps by recursively applying a scalar constraint over
     * the values of a generic container type, with specializations to account for
     * std::pair and std::tuple.  A generic specialization is provided for all types
     * that implement a nested `value_type`.  Note that the condition must be a type
     * trait (not a concept) in order to be valid as a template template parameter.
     */

    template <typename L, typename R>
    struct lt_comparable : BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a < b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct le_comparable : BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a <= b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct eq_comparable : BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a == b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ne_comparable : BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a != b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ge_comparable : BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a >= b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct gt_comparable : BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a > b } -> std::convertible_to<bool>;
        };
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename R
    >
    struct Broadcast : BertrandTag {
        template <typename T>
        struct deref { using type = T; };
        template <iterable T>
        struct deref<T> { using type = iter_type<T>; };

        static constexpr bool value = Condition<
            typename deref<L>::type,
            typename deref<R>::type
        >::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename T1,
        typename T2,
        typename T3,
        typename T4
    >
    struct Broadcast<Condition, std::pair<T1, T2>, std::pair<T3, T4>> : BertrandTag {
        static constexpr bool value =
            Broadcast<Condition, T1, std::pair<T3, T4>>::value &&
            Broadcast<Condition, T2, std::pair<T3, T4>>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename T1,
        typename T2
    >
    struct Broadcast<Condition, L, std::pair<T1, T2>> : BertrandTag {
        static constexpr bool value =
            Broadcast<Condition, L, T1>::value && Broadcast<Condition, L, T2>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename T1,
        typename T2,
        typename R
    >
    struct Broadcast<Condition, std::pair<T1, T2>, R> : BertrandTag {
        static constexpr bool value =
            Broadcast<Condition, T1, R>::value && Broadcast<Condition, T2, R>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename... Ts1,
        typename... Ts2
    >
    struct Broadcast<Condition, std::tuple<Ts1...>, std::tuple<Ts2...>> : BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, Ts1, std::tuple<Ts2...>>::value && ...);
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename... Ts
    >
    struct Broadcast<Condition, L, std::tuple<Ts...>> : BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, L, Ts>::value && ...);
    };

    template <
        template <typename, typename> typename Condition,
        typename... Ts,
        typename R
    >
    struct Broadcast<Condition, std::tuple<Ts...>, R> : BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, Ts, R>::value && ...);
    };

}


/* Allows anonymous access to a Python wrapper for a given C++ type, assuming it has
one.  The result always corresponds to the return type of the unary `__cast__` control
structure, and reflects the Python type that would be constructed if an instance of `T`
were converted to `Object`. */
template <impl::has_python T>
using obj = impl::python_type<T>;


}  // namespace py


#endif
