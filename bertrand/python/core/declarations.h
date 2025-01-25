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

#define Py_BUILD_CORE

#include <Python.h>
#include <frameobject.h>
#include <internal/pycore_frame.h>  // required to assign to frame->f_lineno
#include <internal/pycore_moduleobject.h>  // required to create module subclasses

#undef Py_BUILD_CORE

#include <cpptrace/cpptrace.hpp>

#include <bertrand/common.h>
#include <bertrand/bitset.h>
#include <bertrand/except.h>
#include <bertrand/static_str.h>
#include <bertrand/static_map.h>
#include <bertrand/func.h>


namespace bertrand {


namespace impl {
    /// TODO: convert tags to snake_case
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

    struct empty_interface : BertrandTag {};

    /* Library constructor runs BEFORE a `main()` entry point and ensures the Python
    interpreter is available for any static/global constructors, without explicit
    initialization. */
    __attribute__((constructor(101)))
    static void initialize_python() {
        if (!Py_IsInitialized()) {
            Py_Initialize();
        }
    }

    /* Library destructor runs AFTER a `main()` entry point and ensures that the Python
    interpreter is available for any static/global destructors, without explicit
    finalization. */
    __attribute__((destructor(65535)))
    static void finalize_python() {
        if (Py_IsInitialized()) {
            Py_Finalize();
        }
    }

    /* A static RAII guard that initializes the Python interpreter the first time a Python
    object is created and finalizes it when the program exits. */
    struct Interpreter : impl::BertrandTag {
        /* Ensure that the interpreter is active within the given context.  This is
        called internally whenever a Python object is created from pure C++ inputs, and
        is not called in any other context in order to avoid unnecessary overhead. */
        [[gnu::always_inline]] static void init() noexcept {
            /// TODO: at the moment, this does nothing, relying on the global
            /// constructor above to initialize the interpreter.  This is as yet
            /// untested, however, so if it doesn't work, I can uncomment this code.
            // if (!Py_IsInitialized()) {
            //     Py_Initialize();
            // }
        }

        Interpreter(const Interpreter&) = delete;
        Interpreter(Interpreter&&) = delete;

        ~Interpreter() noexcept {
            /// TODO: similar to init(), this does nothing, relying on the global
            /// destructor to finalize the interpreter.  If that doesn't work, I can
            /// uncomment this code.
            // if (Py_IsInitialized()) {
            //     Py_Finalize();
            // }
        }

    private:
        static Interpreter self;

        Interpreter() = default;
    };

    inline Interpreter Interpreter::self {};

}


///////////////////////////////
////    CONTROL STRUCTS    ////
///////////////////////////////


struct Object;
struct Int;


namespace meta {

    namespace detail {
        template <typename T>
        inline constexpr bool builtin_type = false;
        template <>
        inline constexpr bool builtin_type<Object> = true;
        template <>
        inline constexpr bool builtin_type<Int> = true;
    }

    template <typename T>
    concept python =
        detail::builtin_type<std::remove_cvref_t<T>> ||
        std::derived_from<std::remove_cvref_t<T>, Object>;

    template <typename T>
    concept cpp = !python<T>;

}


/* Base class for disabled control structures. */
struct disable : impl::BertrandTag {
    static constexpr bool enable = false;
};


/* Base class for enabled control structures.  Encodes the return type as a template
parameter. */
template <typename T>
struct returns : impl::BertrandTag {
    static constexpr bool enable = true;
    using type = T;
};


/// TODO: maybe all default operator specializations also account for wrapped C++ types,
/// and are enabled if the underlying type supports the operation.


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
struct __template__                                         : disable {};


/// NOTE: __repr__ is contained in static_str.h in order to allow pure C++ usage.


/* Enables the `bertrand::getattr<"name">()` helper for any `bertrand::Object`
subclass, and assigns a corresponding return type.  Disabled by default unless this
class is explicitly specialized for a given attribute name.  Specializations of this
class may implement a custom call operator to replace the default behavior, which
delegates to a normal dotted attribute lookup at the Python level.   */
template <typename Self, static_str Name>
struct __getattr__                                          : disable {};


/* Enables the `bertrand::setattr<"name">()` helper for any `bertrand::Object`
subclass, which must return void.  Disabled by default unless this class is explicitly
specialized for a given attribute name.  Specializations of this class may implement a
custom call operator to replace the default behavior, which delegates to a normal
dotted attribute assignment at the Python level. */
template <typename Self, static_str Name, typename Value>
struct __setattr__                                          : disable {};


/* Enables the `bertrand::delattr<"name">()` helper for any `bertrand::Object`
subclass, which must return void.  Disabled by default unless this class is explicitly
specialized for a given attribute name.  Specializations of this class may implement a
custom call operator to replace the default behavior, which delegates to a normal
dotted attribute deletion at the Python level. */
template <typename Self, static_str Name>
struct __delattr__                                          : disable {};


/* Enables the C++ initializer list constructor for any `bertrand::Object` subclass,
which must return the type that the `std::initializer_list<>` should be templated on
when constructing instances of this class.  Note that this is NOT the class itself, nor
is it the full `std::initializer_list<>` specialization as it would ordinarily be
given.  This is due to restrictions in the C++ API around `std::initializer_list` in
general.

The initializer list constructor is disabled by default unless this class is explicitly
specialized to return a particular element type.  Specializations of this class MUST
implement a custom call operator to define the constructor logic, which should take a
`const std::initializer_list<>&` as an argument and return an instance of the given
class.  This is what allows direct initialization of Python container types, analogous
to Python's built-in `[]`, `()`, and `{}` container syntax. */
template <typename Self>
struct __initializer__                                      : disable {};


/* Enables implicit conversions between any `bertrand::Object` subclass and an
arbitrary type.  This class handles both conversion to and from Python, as well as
conversions between Python types at the same time.  Specializations of this class MUST
implement a custom call operator which takes an instance of `From` and returns an
instance of `To` (both of which can have arbitrary cvref-qualifiers), with the
following rules:

1.  If `From` is a C++ type and `To` is a Python type, then `__cast__<From, To>` will
    enable an implicit constructor on `To` that accepts a `From` object with the
    given qualifiers.
2.  If `From` is a Python type and `To` is a C++ type, then `__cast__<From, To>` will
    enable an implicit conversion operator on `From` that returns a `To` object with
    the given qualifiers.
3.  If both `From` and `To` are Python types, then `__cast__<From, To>` will enable
    an implicit conversion operator similar to (2), but can interact with the CPython
    API to ensure dynamic type safety.
4.  If only `From` is supplied, then the return type must be a `bertrand::Object`
    subclass and `__cast__<From>` will mark it as being convertible to Python.  In this
    case, the default behavior is to call the return type's constructor with the given
    `From` argument, which will apply the correct conversion logic according to the
    previous rules.  The user does not (and should not) need to implement a custom call
    operator in this case.
 */
template <typename From, typename To = void>
struct __cast__                                             : disable {};


/* Enables the explicit C++ constructor for any `bertrand::Object` subclass.  The
default specialization delegates to Python by introspecting
`__getattr__<Self, "__init__">` or `__getattr__<Self, "__new__">` in that order, which
must return member functions, possibly with Python-style argument annotations.
Specializations of this class MUST implement a custom call operator to define the
constructor logic.

A special case exists for the default constructor for a given type, which accepts no
arguments.  Such constructors will be demoted to implicit constructors, rather than
requiring an explicit call. */
template <typename Self, typename... Args>
struct __init__ {
    template <static_str>
    struct ctor {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct ctor<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (
                !meta::is<T, Object> &&
                std::is_invocable_r_v<Object, T, Args...>
            )
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, Args...>;
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


/* Enables the `bertrand::issubclass<...>()` operator for any subclass of
`bertrand::Object`, which must return a boolean.  This operator has 3 forms:

1.  `bertrand::issubclass<Derived, Base>()`, which is always enabled, and applies a C++
    `std::derived_from<>` check to the given types by default.  Users can provide an
    `__issubclass__<Derived, Base>{}() -> bool` operator to customize this behavior,
    but the result should always be a compile-time constant, and should check against
    `bertrand::interface<Base>` in order to account for multiple inheritance.
2.  `bertrand::issubclass<Base>(derived)`, which is only enabled if `derived` is a
    type-like object, and has no default behavior.  Users can provide an
    `__issubclass__<Derived, Base>{}(Derived&& obj) -> bool` operator to customize this
    if necessary, though the internal overloads are generally sufficient.
3.  `bertrand::issubclass(derived, base)`, which is only enabled if the control
    structure implements an
    `__issubclass__<Derived, Base>{}(Derived&& obj, Base&& cls) -> bool` operator.  By
    default, Bertrand will attempt to detect a suitable `__subclasscheck__(derived)`
    method on the base object by introspecting `__getattr__<Base, "__subclasscheck__">`.
    If such a method is found, then this form will be enabled, and the call will be
    delegated to the Python side.  Users can provide an
    `__issubclass__<Derived, Base>{}(Derived&& obj, Base&& base) -> bool` operator to
    customize this behavior.
 */
template <typename Derived, typename Base>
struct __issubclass__ {
    template <static_str>
    struct infer { static constexpr bool enable = false; };
    template <static_str name> requires (__getattr__<Base, name>::enable)
    struct infer<name> {
        static constexpr bool enable =
            std::is_invocable_r_v<bool, typename __getattr__<Base, name>::type, Derived>;
    };
    static constexpr bool enable = meta::python<Derived> && meta::python<Base>;
    using type = bool;
    template <typename T = infer<"__subclasscheck__">> requires (T::enable)
    static bool operator()(Derived obj, Base base);
};


/* Enables the `bertrand::isinstance<...>()` operator for any subclass of
`bertrand::Object`, which must return a boolean.  This operator has 2 forms:

1.  `bertrand::isinstance<Base>(derived)`, which is always enabled, and checks whether
    the type of `derived` inherits from `Base`.  This devolves to a
    `bertrand::issubclass<Derived, Base>()` check by default, which determines the
    inheritance relationship at compile time.  Users can provide an
    `__isinstance__<Derived, Base>{}(Derived&& obj) -> bool` operator to customize this
    behavior.
2.  `bertrand::isinstance(derived, base)`, which is only enabled if the control
    structure implements an
    `__isinstance__<Derived, Base>{}(Derived&& obj, Base&& cls) -> bool` operator.  By
    default, Bertrand will attempt to detect a suitable `__instancecheck__(derived)`
    method on the base object by introspecting `__getattr__<Base, "__instancecheck__">`.
    If such a method is found, then this form will be enabled, and the call will be
    delegated to the Python side.  Users can provide an
    `__isinstance__<Derived, Base>{}(Derived&& obj, Base&& base) -> bool` operator to
    customize this behavior.
 */
template <typename Derived, typename Base>
struct __isinstance__ {
    template <static_str>
    struct infer { static constexpr bool enable = false; };
    template <static_str name> requires (__getattr__<Base, name>::enable)
    struct infer<name> {
        static constexpr bool enable = 
            std::is_invocable_r_v<bool, typename __getattr__<Base, name>::type, Derived>;
    };
    static constexpr bool enable = meta::python<Derived> && meta::python<Base>;
    using type = bool;
    template <typename T = infer<"__instancecheck__">> requires (T::enable)
    static bool operator()(Derived obj, Base base);
};


/* Enables the C++ call operator for any `bertrand::Object` subclass, and assigns a
corresponding return type.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__call__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self, typename... Args>
struct __call__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, Args...>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, Args...>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__call__">::enable;
    using type = infer<"__call__">::type;
};


/* Enables the C++ subscript operator for any `bertrand::Object` subclass, and assigns
a corresponding return type.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__getitem__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self, typename... Key>
struct __getitem__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, Key...>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, Key...>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__getitem__">::enable;
    using type = infer<"__getitem__">::type;
};


/* Enables the C++ subscript assignment operator for any `bertrand::Object` subclass,
which must return void.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__setitem__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self, typename Value, typename... Key>
struct __setitem__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<void, T, Value, Key...>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, Value, Key...>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__setitem__">::enable;
    using type = infer<"__setitem__">::type;
};


/* Enables the C++ subscript deletion operator for any `bertrand::Object` subclass,
which must return void.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__delitem__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self, typename... Key>
struct __delitem__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<void, T, Key...>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, Key...>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__delitem__">::enable;
    using type = infer<"__delitem__">::type;
};


/* Enables the C++ size operator for any `bertrand::Object` subclass, which must return
`size_t` for consistency with the C++ API.  The default specialization delegates to
Python by introspecting `__getattr__<Self, "__len__">`, which must return a member
function, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename Self>
struct __len__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<size_t, T>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__len__">::enable;
    using type = infer<"__len__">::type;
};


/* Enables the C++ iteration operators for any `bertrand::Object` subclass.  The
default specialization delegates to Python by introspecting
`__getattr__<Self, "__iter__">`, which must return a member function, possibly with
Python-style argument annotations.  Custom specializations of this struct are expected
to implement the iteration protocol directly inline, as if they were implementing a C++
`begin` iterator class, which will always be initialized with the `Self` argument and
nothing else.  The end iterator is always given as `bertrand::sentinel`, which is an
empty struct against which the `__iter__` specialization must be comparable. */
template <typename Self>
struct __iter__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_v<T> && meta::iterable<std::invoke_result_t<T>>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = meta::iter_type<std::invoke_result_t<T>>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__iter__">::enable;
    using type = infer<"__iter__">::type;
};


/* Enables the C++ reverse iteration operators for any `bertrand::Object` subclass.
The default specialization delegates to Python by introspecting
`__getattr__<Self, "__reversed__">`, which must return a member function, possibly with
Python-style argument annotations.  Custom specializations of this struct are expected
to implement the iteration protocol directly inline, as if they were implementing a C++
`std::views` adaptor, which will always be initialized with the `Self` argument and
nothing else.  From there, it can implement its own nested iterator types, which must
be returned from a member `begin()` and `end()` method within the `__iter__`
specialization itself. */
template <typename Self>
struct __reversed__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_v<T> && meta::iterable<std::invoke_result_t<T>>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = meta::iter_type<std::invoke_result_t<T>>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__reversed__">::enable;
    using type = infer<"__reversed__">::type;
};


/* Enables the C++ `.in()` method for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__contains__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior.  */
template <typename Self, typename Key>
struct __contains__ {
    template <static_str>
    static constexpr bool _enable = false;
    template <static_str name> requires (__getattr__<Self, name>::enable)
    static constexpr bool _enable<name> =
        std::is_invocable_r_v<bool, typename __getattr__<Self, name>::type, Key>;
    static constexpr bool enable = _enable<"__contains__">;
    using type = bool;
};


/* Enables `std::hash<>` for any `bertrand::Object` subclass, which must return
`size_t` for consistency with C++.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__hash__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self>
struct __hash__ {
    template <static_str>
    static constexpr bool _enable = false;
    template <static_str name> requires (__getattr__<Self, name>::enable)
    static constexpr bool _enable<name> =
        std::is_invocable_r_v<size_t, typename __getattr__<Self, name>::type>;
    static constexpr bool enable = _enable<"__hash__">;
    using type = size_t;
};


/* Enables `std::abs()` for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__abs__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __abs__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__abs__">::enable;
    using type = infer<"__abs__">::type;
};


/* Enables the C++ unary `~` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__invert__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __invert__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__invert__">::enable;
    using type = infer<"__invert__">::type;
};


/* Enables the C++ unary `+` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__pos__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __pos__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__pos__">::enable;
    using type = infer<"__pos__">::type;
};


/* Enables the C++ unary `-` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__neg__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __neg__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Self, name>::type>::enable;
        using type = inspect<typename __getattr__<Self, name>::type>::type;
    };
    static constexpr bool enable = infer<"__neg__">::enable;
    using type = infer<"__neg__">::type;
};


/* Enables the C++ binary `<` operator for any `bertrand::Object` subclass.  The
default specialization delegates to Python by introspecting `__getattr__<L, "__lt__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __lt__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__lt__">::enable;
    using type = infer<"__lt__">::type;
};


/* Enables the C++ binary `<=` operator for any `bertrand::Object` subclass.  The
default specialization delegates to Python by introspecting `__getattr__<L, "__le__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __le__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__le__">::enable;
    using type = infer<"__le__">::type;
};


/* Enables the C++ binary `==` operator for any `bertrand::Object` subclass.  The
default specialization delegates to Python by introspecting `__getattr__<L, "__eq__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __eq__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__eq__">::enable;
    using type = infer<"__eq__">::type;
};


/* Enables the C++ binary `!=` operator for any `bertrand::Object` subclass.  The
default specialization delegates to Python by introspecting `__getattr__<L, "__ne__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ne__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ne__">::enable;
    using type = infer<"__ne__">::type;
};


/* Enables the C++ binary `>=` operator for any `bertrand::Object` subclass.  The
default specialization delegates to Python by introspecting `__getattr__<L, "__ge__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ge__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ge__">::enable;
    using type = infer<"__ge__">::type;
};


/* Enables the C++ binary `>` operator for any `bertrand::Object` subclass.  The
default specialization delegates to Python by introspecting `__getattr__<L, "__gt__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __gt__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__gt__">::enable;
    using type = infer<"__gt__">::type;
};


/* Enables the C++ binary `+` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__add__">` or `__getattr__<R, "__radd__">` in that order, both of
which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __add__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__add__">::enable || reverse<"__radd__">::enable;
    using type = std::conditional_t<
        forward<"__add__">::enable,
        typename forward<"__add__">::type,
        std::conditional_t<
            reverse<"__radd__">::enable,
            typename reverse<"__radd__">::type,
            void
        >
    >;
};


/* Enables the C++ `+=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__iadd__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __iadd__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__iadd__">::enable;
    using type = infer<"__iadd__">::type;
};


/* Enables the C++ prefix `++` operator for any `bertrand::Object` subclass.  Enabled
by default if inplace addition with `Int` is enabled for the given type.
Specializations of this class may implement a custom call operator to replace the
default behavior.

Note that postfix `++` is not supported for Python objects, since it would not respect
C++ copy semantics. */
template <typename Self>
struct __increment__ {
    template <typename>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <typename S> requires (__iadd__<S, Int>::enable)
    struct infer<S> {
        static constexpr bool enable = true;
        using type = __iadd__<S, Int>::enable::type;
    };
    static constexpr bool enable = infer<Self>::enable;
    using type = infer<Self>::type;
};


/* Enables the C++ binary `-` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__sub__">` or `__getattr__<R, "__rsub__">` in that order, both of
which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __sub__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__sub__">::enable || reverse<"__rsub__">::enable;
    using type = std::conditional_t<
        forward<"__sub__">::enable,
        typename forward<"__sub__">::type,
        std::conditional_t<
            reverse<"__rsub__">::enable,
            typename reverse<"__rsub__">::type,
            void
        >
    >;
};


/* Enables the C++ `-=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__isub__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __isub__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__isub__">::enable;
    using type = infer<"__isub__">::type;
};


/* Enables the C++ prefix `--` operator for any `bertrand::Object` subclass.  Enabled
by default if inplace subtraction with `Int` is enabled for the given type.
Specializations of this class may implement a custom call operator to implement this
operator.

Note that postfix `--` is not supported for Python objects, since it would not respect
C++ copy semantics. */
template <typename Self>
struct __decrement__ {
    template <typename>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <typename S> requires (__isub__<S, Int>::enable)
    struct infer<S> {
        static constexpr bool enable = true;
        using type = __isub__<S, Int>::enable::type;
    };
    static constexpr bool enable = infer<Self>::enable;
    using type = infer<Self>::type;
};


/* Enables the C++ binary `*` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__mul__">` or `__getattr__<R, "__rmul__">` in that order, both of
which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __mul__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__mul__">::enable || reverse<"__rmul__">::enable;
    using type = std::conditional_t<
        forward<"__mul__">::enable,
        typename forward<"__mul__">::type,
        std::conditional_t<
            reverse<"__rmul__">::enable,
            typename reverse<"__rmul__">::type,
            void
        >
    >;
};


/* Enables the C++ `*=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__imul__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __imul__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__imul__">::enable;
    using type = infer<"__imul__">::type;
};


/* Enables the C++ binary `/` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__truediv__">` or `__getattr__<R, "__rtruediv__">` in that order, both
of which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __truediv__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__truediv__">::enable || reverse<"__rtruediv__">::enable;
    using type = std::conditional_t<
        forward<"__truediv__">::enable,
        typename forward<"__truediv__">::type,
        std::conditional_t<
            reverse<"__rtruediv__">::enable,
            typename reverse<"__rtruediv__">::type,
            void
        >
    >;
};


/* Enables the C++ `/=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__itruediv__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __itruediv__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__itruediv__">::enable;
    using type = infer<"__itruediv__">::type;
};


/* Implements the Python `//` operator logic in C++ for any `bertrand::Object`
subclass, which has no corresponding C++ operator.  The default specialization
delegates to Python by introspecting either `__getattr__<L, "__floordiv__">` or
`__getattr__<R, "__rfloordiv__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior.

This is used internally to implement `bertrand::div()`, `bertrand::mod()`,
`bertrand::divmod()`, and `bertrand::round()`, which have a wide variety of fully
customizable rounding strategies based on this operator. */
template <typename L, typename R>
struct __floordiv__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__floordiv__">::enable || reverse<"__rfloordiv__">::enable;
    using type = std::conditional_t<
        forward<"__floordiv__">::enable,
        typename forward<"__floordiv__">::type,
        std::conditional_t<
            reverse<"__rfloordiv__">::enable,
            typename reverse<"__rfloordiv__">::type,
            void
        >
    >;
};


/* Implements the Python `//=` operator logic in C++ for any `bertrand::Object`
subclass, which has no corresponding C++ operator.  The default specialization
delegates to Python by introspecting `__getattr__<L, "__ifloordiv__">`, which must
return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior.

This is used internally to implement `bertrand::div()`, `bertrand::mod()`,
`bertrand::divmod()`, and `bertrand::round()`, which have a wide variety of fully
customizable rounding strategies based on this operator. */
template <typename L, typename R>
struct __ifloordiv__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ifloordiv__">::enable;
    using type = infer<"__ifloordiv__">::type;
};


/* Enables the C++ binary `%` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__mod__">` or `__getattr__<R, "__rmod__">` in that order, both of
which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __mod__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__mod__">::enable || reverse<"__rmod__">::enable;
    using type = std::conditional_t<
        forward<"__mod__">::enable,
        typename forward<"__mod__">::type,
        std::conditional_t<
            reverse<"__rmod__">::enable,
            typename reverse<"__rmod__">::type,
            void
        >
    >;
};


/* Enables the C++ `%=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__imod__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __imod__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__imod__">::enable;
    using type = infer<"__imod__">::type;
};


/* Implements the Python `pow()` operator logic in C++ for any `bertrand::Object`
subclass, which has no corresponding C++ operator.  The default specialization
delegates to Python by introspecting either `__getattr__<L, "__pow__">` or
`__getattr__<R, "__rpow__">` in that order, both of which must return member functions,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior.

This is used internally to implement `bertrand::pow()`. */
template <typename Base, typename Exp, typename Mod = void>
struct __pow__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Base, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, Exp>)
        struct inspect<T> {
            /// TODO: this needs to be updated to support the ternary form of pow()
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, Exp>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Base, name>::type>::enable;
        using type = inspect<typename __getattr__<Base, name>::type>::type;
    };
    /// TODO: reverse<>
    static constexpr bool enable =
        forward<"__pow__">::enable || forward<"__rpow__">::enable;
    using type = std::conditional_t<
        forward<"__pow__">::enable,
        typename forward<"__pow__">::type,
        std::conditional_t<
            forward<"__rpow__">::enable,
            typename forward<"__rpow__">::type,
            void
        >
    >;
};


/* Implements the Python `**=` operator logic in C++ for any `bertrand::Object`
subclass, which has no corresponding C++ operator.  The default specialization
delegates to Python by introspecting `__getattr__<L, "__ipow__">`, which must return a
member function, possibly with Python-style argument annotations.  Specializations of
this class may implement a custom call operator to replace the default behavior.

This is used internally to implement `bertrand::ipow()`. */
template <typename Base, typename Exp, typename Mod = void>
struct __ipow__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<Base, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<Base>, T, Exp>) 
        struct inspect<T> {
            /// TODO: same as for ternary form of pow()
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, Exp>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<Base, name>::type>::enable;
        using type = inspect<typename __getattr__<Base, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ipow__">::enable;
    using type = infer<"__ipow__">::type;
};


/* Enables the C++ binary `<<` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__lshift__">` or `__getattr__<R, "__rlshift__">` in that order, both
of which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __lshift__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__lshift__">::enable || reverse<"__rlshift__">::enable;
    using type = std::conditional_t<
        forward<"__lshift__">::enable,
        typename forward<"__lshift__">::type,
        std::conditional_t<
            reverse<"__rlshift__">::enable,
            typename reverse<"__rlshift__">::type,
            void
        >
    >;
};


/* Enables the C++ `<<=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ilshift__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ilshift__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ilshift__">::enable;
    using type = infer<"__ilshift__">::type;
};


/* Enables the C++ binary `>>` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__rshift__">` or `__getattr__<R, "__rrshift__">` in that order, both
of which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __rshift__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__rshift__">::enable || reverse<"__rrshift__">::enable;
    using type = std::conditional_t<
        forward<"__rshift__">::enable,
        typename forward<"__rshift__">::type,
        std::conditional_t<
            reverse<"__rrshift__">::enable,
            typename reverse<"__rrshift__">::type,
            void
        >
    >;
};


/* Enables the C++ `>>=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__irshift__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __irshift__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__irshift__">::enable;
    using type = infer<"__irshift__">::type;
};


/* Enables the C++ binary `&` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__and__">` or `__getattr__<R, "__rand__">` in that order, both of
which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __and__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__and__">::enable || reverse<"__rand__">::enable;
    using type = std::conditional_t<
        forward<"__and__">::enable,
        typename forward<"__and__">::type,
        std::conditional_t<
            reverse<"__rand__">::enable,
            typename reverse<"__rand__">::type,
            void
        >
    >;
};


/* Enables the C++ `&=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__iand__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __iand__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__iand__">::enable;
    using type = infer<"__iand__">::type;
};


/* Enables the C++ binary `|` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__or__">` or `__getattr__<R, "__ror__">` in that order, both of which
must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __or__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__or__">::enable || reverse<"__ror__">::enable;
    using type = std::conditional_t<
        forward<"__or__">::enable,
        typename forward<"__or__">::type,
        std::conditional_t<
            reverse<"__ror__">::enable,
            typename reverse<"__ror__">::type,
            void
        >
    >;
};


/* Enables the C++ `|=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ior__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ior__ {
    template <static_str>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ior__">::enable;
    using type = infer<"__ior__">::type;
};


/* Enables the C++ binary `^` operator for any `bertrand::Object` subclass.  the
default specialization delegates to Python by introspecting either
`__getattr__<L, "__xor__">` or `__getattr__<R, "__rxor__">` in that order, both of
which must return member functions, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __xor__ {
    template <static_str>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct forward<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    template <static_str>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<R, name>::enable)
    struct reverse<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, L>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, L>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<R, name>::type>::enable;
        using type = inspect<typename __getattr__<R, name>::type>::type;
    };
    static constexpr bool enable =
        forward<"__xor__">::enable || reverse<"__rxor__">::enable;
    using type = std::conditional_t<
        forward<"__xor__">::enable,
        typename forward<"__xor__">::type,
        std::conditional_t<
            reverse<"__rxor__">::enable,
            typename reverse<"__rxor__">::type,
            void
        >
    >;
};


/* Enables the C++ `^=` operator for any `bertrand::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ixor__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ixor__ {
    template <static_str name>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <static_str name> requires (__getattr__<L, name>::enable)
    struct infer<name> {
        template <typename T>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_r_v<std::remove_cvref_t<L>, T, R>) 
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = std::invoke_result_t<T, R>;
        };
        static constexpr bool enable =
            inspect<typename __getattr__<L, name>::type>::enable;
        using type = inspect<typename __getattr__<L, name>::type>::type;
    };
    static constexpr bool enable = infer<"__ixor__">::enable;
    using type = infer<"__ixor__">::type;
};


//////////////////////////////
////    BUILT-IN TYPES    ////
//////////////////////////////


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
interface, so here's a simple example to illustrate how it's done:

    // forward declare the Object wrapper
    struct Wrapper;

    // define the wrapper's interface, mixing in the interfaces of its base classes
    template <>
    struct interface<Wrapper> : interface<Base1>, interface<Base2>, ... {
        void foo();  // forward declarations for interface methods
        int bar() const;
        static std::string baz();
    };

    // define the wrapper, mixing the interface with Object
    struct Wrapper : Object, interface<Wrapper> {
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
    struct interface<Type<Wrapper>> : interface<Type<Base1>>, interface<Type<Base2>>, ... {
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
    struct __getattr__<Wrapper, "foo"> : returns<Function<void()>> {};
    template <>
    struct __getattr__<Wrapper, "bar"> : returns<Function<int()>> {};
    template <>
    struct __getattr__<Wrapper, "baz"> : returns<Function<std::string()>> {};
    template <>
    struct __getattr__<Type<Wrapper>, "foo"> : returns<Function<void(Wrapper&)>> {};
    template <>
    struct __getattr__<Type<Wrapper>, "bar"> : returns<Function<int(const Wrapper&)>> {};
    template <>
    struct __getattr__<Type<Wrapper>, "baz"> : returns<Function<std::string()>> {};
    // ... for all supported C++ operators

    // implement the interface methods
    void interface<Wrapper>::foo() {
        print("Hello, world!");
    }
    int interface<Wrapper>::bar() const {
        return 42;
    }
    std::string interface<Wrapper>::baz() {
        return "static methods work too!";
    }
    void interface<Type<Wrapper>>::foo(auto& self) {
        self.foo();
    }
    int interface<Type<Wrapper>>::bar(const auto& self) {
        return self.bar();
    }
    std::string interface<Type<Wrapper>>::baz() {
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
struct interface : impl::empty_interface {};


namespace meta {

    namespace detail {

        template <typename T, typename = void>
        struct python {
            static constexpr bool enable = __cast__<T>::enable;
            template <typename U>
            struct helper { using type = void; };
            template <typename U> requires (__cast__<U>::enable)
            struct helper<U> { using type = __cast__<U>::type; };
            using type = helper<T>::type;
        };
        template <typename T>
        struct python<T, std::void_t<
            typename std::remove_cvref_t<T>::__python__::__object__>
        > {
            static constexpr bool enable = std::same_as<
                std::remove_cvref_t<T>,
                typename std::remove_cvref_t<T>::__python__::__object__
            >;
            using type = T;
        };

        template <typename T>
        struct python_type { using type = python<T>::type; };
        template <meta::python T>
        struct python_type<T> { using type = T; };

    }  // namespace detail

    template <typename T>
    concept has_python = python<T> || detail::python<T>::enable;
    template <has_python T>
    using python_type = detail::python_type<T>::type;

}  // namespace meta


template <meta::py_function Func>
struct Function;
template <meta::has_python T = Object>
struct Type;
template <static_str Name> requires (meta::arg_name<Name>)
struct Module;
template <meta::python... Types>
    requires (
        sizeof...(Types) > 1 &&
        (!meta::is_qualified<Types> && ...) &&
        meta::types_are_unique<Types...>
    )
struct Union;
template <meta::arg... Attrs>
    requires (
        (!meta::is_qualified<Attrs> && ...) &&
        (!meta::is_qualified<typename meta::arg_traits<Attrs>::type> && ...) &&
        (meta::python<typename meta::arg_traits<Attrs>::type> && ...) &&
        meta::strings_are_unique<meta::arg_traits<Attrs>::name...> &&
        impl::minimal_perfect_hash<meta::arg_traits<Attrs>::name...>::exists
    )
struct Intersection;
struct NoneType;
struct NotImplementedType;
struct EllipsisType;
struct Slice;
struct Bool;
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
template <meta::python T = Object> requires (!meta::is_qualified<T>)
struct List;
template <meta::python T = Object> requires (!meta::is_qualified<T>)
struct Tuple;
template <meta::python T = Object> requires (!meta::is_qualified<T>)
struct Set;
template <meta::python T = Object> requires (!meta::is_qualified<T>)
struct FrozenSet;
template <meta::python K = Object, meta::python V = Object>
    requires (!meta::is_qualified<K>, !meta::is_qualified<V>)
struct Dict;
template <meta::python M> requires (!meta::is_qualified<M>)
struct KeyView;
template <meta::python M> requires (!meta::is_qualified<M>)
struct ValueView;
template <meta::python M> requires (!meta::is_qualified<M>)
struct ItemView;
template <meta::python M> requires (!meta::is_qualified<M>)
struct MappingProxy;


template <meta::python T>
    requires (!meta::is_qualified<T> && !std::same_as<T, NoneType>)
using Optional = Union<T, NoneType>;


namespace meta {

    namespace detail {

        template <typename T>
        inline constexpr bool builtin_type<Function<T>> = true;
        template <typename T>
        inline constexpr bool builtin_type<Type<T>> = true;
        template <bertrand::static_str Name>
        inline constexpr bool builtin_type<Module<Name>> = true;
        template <typename... Ts>
        inline constexpr bool builtin_type<Union<Ts...>> = true;
        template <typename... Ts>
        inline constexpr bool builtin_type<Intersection<Ts...>> = true;
        template<>
        inline constexpr bool builtin_type<NoneType> = true;
        template<>
        inline constexpr bool builtin_type<NotImplementedType> = true;
        template<>
        inline constexpr bool builtin_type<EllipsisType> = true;
        template<>
        inline constexpr bool builtin_type<Slice> = true;
        template<>
        inline constexpr bool builtin_type<Bool> = true;
        template<>
        inline constexpr bool builtin_type<Float> = true;
        template<>
        inline constexpr bool builtin_type<Complex> = true;
        template<>
        inline constexpr bool builtin_type<Str> = true;
        template<>
        inline constexpr bool builtin_type<Bytes> = true;
        template<>
        inline constexpr bool builtin_type<ByteArray> = true;
        template<>
        inline constexpr bool builtin_type<Date> = true;
        template<>
        inline constexpr bool builtin_type<Time> = true;
        template<>
        inline constexpr bool builtin_type<Datetime> = true;
        template<>
        inline constexpr bool builtin_type<Timedelta> = true;
        template<>
        inline constexpr bool builtin_type<Timezone> = true;
        template<>
        inline constexpr bool builtin_type<Range> = true;
        template <typename T>
        inline constexpr bool builtin_type<List<T>> = true;
        template <typename T>
        inline constexpr bool builtin_type<Tuple<T>> = true;
        template <typename T>
        inline constexpr bool builtin_type<Set<T>> = true;
        template <typename T>
        inline constexpr bool builtin_type<FrozenSet<T>> = true;
        template <typename K, typename V>
        inline constexpr bool builtin_type<Dict<K, V>> = true;
        template <typename M>
        inline constexpr bool builtin_type<KeyView<M>> = true;
        template <typename M>
        inline constexpr bool builtin_type<ValueView<M>> = true;
        template <typename M>
        inline constexpr bool builtin_type<ItemView<M>> = true;
        template <typename M>
        inline constexpr bool builtin_type<MappingProxy<M>> = true;

        template <typename T, typename = void>
        constexpr bool has_type = false;
        template <typename T>
        constexpr bool has_type<T, std::void_t<Type<T>>> = true;

        template <typename T, typename = void>
        constexpr bool has_export = false;
        template <typename T>
        constexpr bool has_export<T, std::void_t<decltype(&T::__python__::__export__)>> =
            std::is_function_v<decltype(T::__python__::__export__)> &&
            std::is_invocable_v<decltype(T::__python__::__export__)>;

        template <typename T, typename = void>
        constexpr bool has_import = false;
        template <typename T>
        constexpr bool has_import<T, std::void_t<decltype(&T::__python__::__import__)>> =
            std::is_function_v<decltype(T::__python__::__import__)> &&
            std::is_invocable_v<decltype(T::__python__::__import__)>;

        template <typename T, typename = void>
        struct cpp {
            static constexpr bool enable = meta::cpp<T>;
            using type = T;
        };
        template <typename T>
        struct cpp<T, std::void_t<typename std::remove_cvref_t<T>::__python__::__cpp__>> {
            static constexpr bool enable = requires() {
                { &std::remove_cvref_t<T>::__python__::m_cpp };
            };
            using type = std::remove_cvref_t<T>::__python__::__cpp__;
        };

    }  // namespace detail

    template <typename T>
    concept has_interface = !inherits<
        interface<std::remove_cvref_t<T>>,
        impl::empty_interface
    >;

    template <typename T>
    concept has_type = detail::has_type<std::remove_cvref_t<T>>;

    template <typename T>
    concept has_export =
        python<T> && has_python<T> && detail::has_export<std::remove_cvref_t<T>>;

    template <typename T>
    concept has_import =
        python<T> && has_python<T> && detail::has_import<std::remove_cvref_t<T>>;

    template <typename T>
    concept has_cpp = detail::cpp<T>::enable;
    template <has_cpp T>
    using cpp_type = detail::cpp<T>::type;

}  // namespace meta


namespace impl {

    template <typename Self, static_str Name>
        requires (
            __getattr__<Self, Name>::enable &&
            meta::python<typename __getattr__<Self, Name>::type> &&
            !meta::is_qualified<typename __getattr__<Self, Name>::type> && (
                !std::is_invocable_v<__getattr__<Self, Name>, Self> ||
                std::is_invocable_r_v<
                    typename __getattr__<Self, Name>::type,
                    __getattr__<Self, Name>,
                    Self
                >
            )
        )
    struct Attr;

    template <typename Self, typename... Key>
        requires (
            __getitem__<Self, Key...>::enable &&
            meta::python<typename __getitem__<Self, Key...>::type> &&
            !meta::is_qualified<typename __getitem__<Self, Key...>::type> && (
                std::is_invocable_r_v<
                    typename __getitem__<Self, Key...>::type,
                    __getitem__<Self, Key...>,
                    Self,
                    Key...
                > || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    meta::has_cpp<Self> &&
                    meta::lookup_yields<
                        meta::cpp_type<Self>&,
                        typename __getitem__<Self, Key...>::type,
                        Key...
                    >
                ) || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    !meta::has_cpp<Self>
                )
            )
        )
    struct Item;

}  // namespace impl


namespace meta {

    namespace detail {

        template <typename T>
        struct attr { static constexpr bool enable = false; };
        template <typename Self, bertrand::static_str Name>
        struct attr<impl::Attr<Self, Name>> {
            static constexpr bool enable = true;
            using type = __getattr__<Self, Name>::type;
        };

        template <typename T>
        struct item { static constexpr bool enable = false; };
        template <typename Self, typename... Key>
        struct item<impl::Item<Self, Key...>> {
            static constexpr bool enable = true;
            using type = __getitem__<Self, Key...>::type;
        };

        template <typename T>
        struct lazy_type {};
        template <typename T> requires (attr<T>::enable)
        struct lazy_type<T> { using type = attr<T>::type; };
        template <typename T> requires (item<T>::enable)
        struct lazy_type<T> { using type = item<T>::type; };

    }  // namespace detail

    template <typename T>
    concept is_attr = detail::attr<std::remove_cvref_t<T>>::enable;
    template <is_attr T>
    using attr_type = detail::attr<std::remove_cvref_t<T>>::type;

    template <typename T, bertrand::static_str Name, typename... Args>
    concept attr_is_callable_with =
        __getattr__<T, Name>::enable &&
        std::is_invocable_v<typename __getattr__<T, Name>::type, Args...>;

    template <typename T>
    concept is_item = detail::item<std::remove_cvref_t<T>>::enable;
    template <is_item T>
    using item_type = detail::item<std::remove_cvref_t<T>>::type;

    template <typename T>
    concept lazily_evaluated = is_attr<T> || is_item<T>;

    template <lazily_evaluated T>
    using lazy_type = detail::lazy_type<std::remove_cvref_t<T>>::type;

    template <typename T, typename Base>
    concept like =
        __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Base>;

    template <typename T>
    concept type_like = like<T, impl::TypeTag>;

    template <typename T>
    concept none_like = like<T, NoneType>;

    template <typename T>
    concept notimplemented_like = like<T, NotImplementedType>;

    template <typename T>
    concept ellipsis_like = like<T, EllipsisType>;

    template <typename T>
    concept module_like = like<T, impl::ModuleTag>;

    template <typename T>
    concept bool_like = like<T, Bool>;

    template <typename T>
    concept int_like = like<T, Int>;

    template <typename T>
    concept float_like = like<T, Float>;

    template <typename T>
    concept str_like = like<T, Str>;

    template <typename T>
    concept bytes_like =
        string_literal<T> || std::same_as<std::decay_t<T>, void*> || like<T, Bytes>;

    template <typename T>
    concept bytearray_like =
        string_literal<T> || std::same_as<std::decay_t<T>, void*> || like<T, ByteArray>;

    template <typename T>
    concept anybytes_like = bytes_like<T> || bytearray_like<T>;

    template <typename T>
    concept timedelta_like = like<T, Timedelta>;

    template <typename T>
    concept timezone_like = like<T, Timezone>;

    template <typename T>
    concept date_like = like<T, Date>;

    template <typename T>
    concept time_like = like<T, Time>;

    template <typename T>
    concept datetime_like = like<T, Datetime>;

    template <typename T>
    concept range_like = like<T, Range>;

    template <typename T>
    concept tuple_like = like<T, impl::TupleTag>;

    template <typename T>
    concept list_like = like<T, impl::ListTag>;

    template <typename T>
    concept set_like = like<T, impl::SetTag>;

    template <typename T>
    concept frozenset_like = like<T, impl::FrozenSetTag>;

    template <typename T>
    concept anyset_like = set_like<T> || frozenset_like<T>;

    template <typename T>
    concept dict_like = like<T, impl::DictTag>;

    template <typename T>
    concept mappingproxy_like = like<T, impl::MappingProxyTag>;

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
    struct lt_comparable : impl::BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a < b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct le_comparable : impl::BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a <= b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct eq_comparable : impl::BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a == b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ne_comparable : impl::BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a != b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ge_comparable : impl::BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a >= b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct gt_comparable : impl::BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a > b } -> std::convertible_to<bool>;
        };
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename R
    >
    struct Broadcast : impl::BertrandTag {
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
    struct Broadcast<Condition, std::pair<T1, T2>, std::pair<T3, T4>> : impl::BertrandTag {
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
    struct Broadcast<Condition, L, std::pair<T1, T2>> : impl::BertrandTag {
        static constexpr bool value =
            Broadcast<Condition, L, T1>::value && Broadcast<Condition, L, T2>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename T1,
        typename T2,
        typename R
    >
    struct Broadcast<Condition, std::pair<T1, T2>, R> : impl::BertrandTag {
        static constexpr bool value =
            Broadcast<Condition, T1, R>::value && Broadcast<Condition, T2, R>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename... Ts1,
        typename... Ts2
    >
    struct Broadcast<Condition, std::tuple<Ts1...>, std::tuple<Ts2...>> : impl::BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, Ts1, std::tuple<Ts2...>>::value && ...);
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename... Ts
    >
    struct Broadcast<Condition, L, std::tuple<Ts...>> : impl::BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, L, Ts>::value && ...);
    };

    template <
        template <typename, typename> typename Condition,
        typename... Ts,
        typename R
    >
    struct Broadcast<Condition, std::tuple<Ts...>, R> : impl::BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, Ts, R>::value && ...);
    };

}  // namespace meta


/* Allows anonymous access to a Python wrapper for a given C++ type, assuming it has
one.  The result always corresponds to the return type of the unary `__cast__` control
structure, and reflects the Python type that would be constructed if an instance of `T`
were converted to `Object`. */
template <meta::has_python T>
using obj = meta::python_type<T>;


}  // namespace bertrand


#endif
