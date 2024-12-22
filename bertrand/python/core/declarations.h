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
#include <bertrand/static_str.h>
#include <bertrand/arg.h>


namespace py {
using bertrand::Arg;
using bertrand::ArgTraits;
using bertrand::arg;
using bertrand::args;
using bertrand::StaticStr;
using bertrand::StaticMap;
using bertrand::type_name;
using bertrand::demangle;


#ifdef NDEBUG
    constexpr bool DEBUG = false;
#else
    constexpr bool DEBUG = true;
#endif


namespace impl {
    using bertrand::Sentinel;
    using bertrand::index_of;
    using bertrand::unpack_type;
    using bertrand::unpack_arg;
    using bertrand::fnv1a_seed;
    using bertrand::fnv1a_prime;
    using bertrand::fnv1a;
    using bertrand::FNV1a;
    using bertrand::hash_combine;
    using bertrand::qualify;
    using bertrand::qualify_lvalue;
    using bertrand::qualify_rvalue;
    using bertrand::qualify_pointer;
    using bertrand::remove_lvalue;
    using bertrand::remove_rvalue;
    using bertrand::implicit_cast;
    using bertrand::is;
    using bertrand::inherits;
    using bertrand::is_const;
    using bertrand::is_volatile;
    using bertrand::is_lvalue;
    using bertrand::is_rvalue;
    using bertrand::is_ptr;
    using bertrand::types_are_unique;
    using bertrand::explicitly_convertible_to;
    using bertrand::static_str;
    using bertrand::string_literal;
    using bertrand::is_optional;
    using bertrand::optional_type;
    using bertrand::is_variant;
    using bertrand::variant_types;
    using bertrand::is_shared_ptr;
    using bertrand::shared_ptr_type;
    using bertrand::is_unique_ptr;
    using bertrand::unique_ptr_type;
    using bertrand::iterable;
    using bertrand::iter_type;
    using bertrand::yields;
    using bertrand::reverse_iterable;
    using bertrand::reverse_iter_type;
    using bertrand::yields_reverse;
    using bertrand::has_size;
    using bertrand::has_empty;
    using bertrand::sequence_like;
    using bertrand::mapping_like;
    using bertrand::supports_lookup;
    using bertrand::lookup_type;
    using bertrand::lookup_yields;
    using bertrand::supports_item_assignment;
    using bertrand::pair_like;
    using bertrand::pair_like_with;
    using bertrand::yields_pairs;
    using bertrand::yields_pairs_with;
    using bertrand::has_to_string;
    using bertrand::has_stream_insertion;
    using bertrand::has_call_operator;
    using bertrand::complex_like;
    using bertrand::has_reserve;
    using bertrand::has_contains;
    using bertrand::has_keys;
    using bertrand::has_values;
    using bertrand::has_items;
    using bertrand::has_operator_bool;
    using bertrand::hashable;
    using bertrand::has_abs;
    using bertrand::abs_type;
    using bertrand::abs_returns;
    using bertrand::has_invert;
    using bertrand::invert_type;
    using bertrand::invert_returns;
    using bertrand::has_pos;
    using bertrand::pos_type;
    using bertrand::pos_returns;
    using bertrand::has_neg;
    using bertrand::neg_type;
    using bertrand::neg_returns;
    using bertrand::has_preincrement;
    using bertrand::preincrement_type;
    using bertrand::preincrement_returns;
    using bertrand::has_postincrement;
    using bertrand::postincrement_type;
    using bertrand::postincrement_returns;
    using bertrand::has_predecrement;
    using bertrand::predecrement_type;
    using bertrand::predecrement_returns;
    using bertrand::has_postdecrement;
    using bertrand::postdecrement_type;
    using bertrand::postdecrement_returns;
    using bertrand::has_lt;
    using bertrand::lt_type;
    using bertrand::lt_returns;
    using bertrand::has_le;
    using bertrand::le_type;
    using bertrand::le_returns;
    using bertrand::has_eq;
    using bertrand::eq_type;
    using bertrand::eq_returns;
    using bertrand::has_ne;
    using bertrand::ne_type;
    using bertrand::ne_returns;
    using bertrand::has_ge;
    using bertrand::ge_type;
    using bertrand::ge_returns;
    using bertrand::has_gt;
    using bertrand::gt_type;
    using bertrand::gt_returns;
    using bertrand::has_add;
    using bertrand::add_type;
    using bertrand::add_returns;
    using bertrand::has_iadd;
    using bertrand::iadd_type;
    using bertrand::iadd_returns;
    using bertrand::has_sub;
    using bertrand::sub_type;
    using bertrand::sub_returns;
    using bertrand::has_isub;
    using bertrand::isub_type;
    using bertrand::isub_returns;
    using bertrand::has_mul;
    using bertrand::mul_type;
    using bertrand::mul_returns;
    using bertrand::has_imul;
    using bertrand::imul_type;
    using bertrand::imul_returns;
    using bertrand::has_truediv;
    using bertrand::truediv_type;
    using bertrand::truediv_returns;
    using bertrand::has_itruediv;
    using bertrand::itruediv_type;
    using bertrand::itruediv_returns;
    using bertrand::has_mod;
    using bertrand::mod_type;
    using bertrand::mod_returns;
    using bertrand::has_imod;
    using bertrand::imod_type;
    using bertrand::imod_returns;
    using bertrand::has_pow;
    using bertrand::pow_type;
    using bertrand::pow_returns;
    using bertrand::has_lshift;
    using bertrand::lshift_type;
    using bertrand::lshift_returns;
    using bertrand::has_ilshift;
    using bertrand::ilshift_type;
    using bertrand::ilshift_returns;
    using bertrand::has_rshift;
    using bertrand::rshift_type;
    using bertrand::rshift_returns;
    using bertrand::has_irshift;
    using bertrand::irshift_type;
    using bertrand::irshift_returns;
    using bertrand::has_and;
    using bertrand::and_type;
    using bertrand::and_returns;
    using bertrand::has_iand;
    using bertrand::iand_type;
    using bertrand::iand_returns;
    using bertrand::has_or;
    using bertrand::or_type;
    using bertrand::or_returns;
    using bertrand::has_ior;
    using bertrand::ior_type;
    using bertrand::ior_returns;
    using bertrand::has_xor;
    using bertrand::xor_type;
    using bertrand::xor_returns;
    using bertrand::has_ixor;
    using bertrand::ixor_type;
    using bertrand::ixor_returns;
    using bertrand::ArgPack;
    using bertrand::KwargPack;
    using bertrand::ArgKind;
    using bertrand::arg_name;
    using bertrand::variadic_args_name;
    using bertrand::variadic_kwargs_name;
    using bertrand::arg_pack;
    using bertrand::kwarg_pack;
    using bertrand::is_arg;
    using bertrand::impl::args_tag;
    using bertrand::is_args;
    using bertrand::impl::chain_tag;
    using bertrand::chain;

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

}


/* A python-style `assert` statement in C++, which is optimized away if built with
`-DNDEBUG` (i.e. release mode).  The only difference between this and the built-in
`assert()` macro is that this raises a `py::AssertionError` with a coherent
traceback for cross-language support. */
template <size_t N>
[[gnu::always_inline]] void assert_(bool, const char(&)[N]);


///////////////////////////////
////    CONTROL STRUCTS    ////
///////////////////////////////


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

    template <typename T>
    concept bertrand = std::derived_from<std::remove_cvref_t<T>, BertrandTag>;

    template <typename T>
    concept python = std::derived_from<std::remove_cvref_t<T>, Object>;

    template <typename T>
    concept cpp = !python<T>;

}


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
struct __template__                                         : Disable {};


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


/// TODO: see if I can eliminate the __explicit_cast__ control struct.  It's
/// unnecessarily confusing, and can interfere with expected C++ behavior
/// (i.e. static_cast<> not doing any work at runtime).


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
    template <StaticStr>
    struct ctor {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct ctor<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (
                !impl::is<T, Object> &&
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
    template <StaticStr>
    static constexpr bool _enable = false;
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    static constexpr bool _enable<name> =
        std::is_invocable_r_v<std::string, typename __getattr__<Self, name>::type>;
    static constexpr bool enable = _enable<"__repr__">;
    using type = std::string;
};


/* Enables the `py::issubclass<...>()` operator for any subclass of `py::Object`, which
must return a boolean.  This operator has 3 forms:

1.  `py::issubclass<Derived, Base>()`, which is always enabled, and applies a C++
    `std::derived_from<>` check to the given types by default.  Users can provide an
    `__issubclass__<Derived, Base>{}() -> bool` operator to customize this behavior,
    but the result should always be a compile-time constant, and should check against
    `py::Interface<Base>` in order to account for multiple inheritance.
2.  `py::issubclass<Base>(derived)`, which is only enabled if `derived` is a type-like
    object, and has no default behavior.  Users can provide an
    `__issubclass__<Derived, Base>{}(Derived&& obj) -> bool` operator to customize this
    if necessary, though the internal overloads are generally sufficient.
3.  `py::issubclass(derived, base)`, which is only enabled if the control structure
    implements an `__issubclass__<Derived, Base>{}(Derived&& obj, Base&& cls) -> bool`
    operator.  By default, Bertrand will attempt to detect a suitable
    `__subclasscheck__(derived)` method on the base object by introspecting
    `__getattr__<Base, "__subclasscheck__">`.  If such a method is found, then this
    form will be enabled, and the call will be delegated to the Python side.  Users can
    provide an `__issubclass__<Derived, Base>{}(Derived&& obj, Base&& base) -> bool`
    operator to customize this behavior.
 */
template <typename Derived, typename Base>
struct __issubclass__ {
    template <StaticStr>
    struct infer { static constexpr bool enable = false; };
    template <StaticStr name> requires (__getattr__<Base, name>::enable)
    struct infer<name> {
        static constexpr bool enable =
            std::is_invocable_r_v<bool, typename __getattr__<Base, name>::type, Derived>;
    };
    static constexpr bool enable = impl::python<Derived> && impl::python<Base>;
    using type = bool;
    template <typename T = infer<"__subclasscheck__">> requires (T::enable)
    static bool operator()(Derived obj, Base base);
};


/* Enables the `py::isinstance<...>()` operator for any subclass of `py::Object`, which
must return a boolean.  This operator has 2 forms:

1.  `py::isinstance<Base>(derived)`, which is always enabled, and checks whether the
    type of `derived` inherits from `Base`.  This devolves to a
    `py::issubclass<Derived, Base>()` check by default, which determines the
    inheritance relationship at compile time.  Users can provide an
    `__isinstance__<Derived, Base>{}(Derived&& obj) -> bool` operator to customize this
    behavior.
2.  `py::isinstance(derived, base)`, which is only enabled if the control structure
    implements an `__isinstance__<Derived, Base>{}(Derived&& obj, Base&& cls) -> bool`
    operator.  By default, Bertrand will attempt to detect a suitable
    `__instancecheck__(derived)` method on the base object by introspecting
    `__getattr__<Base, "__instancecheck__">`.  If such a method is found, then this
    form will be enabled, and the call will be delegated to the Python side.  Users can
    provide an `__isinstance__<Derived, Base>{}(Derived&& obj, Base&& base) -> bool`
    operator to customize this behavior.
 */
template <typename Derived, typename Base>
struct __isinstance__ {
    template <StaticStr>
    struct infer { static constexpr bool enable = false; };
    template <StaticStr name> requires (__getattr__<Base, name>::enable)
    struct infer<name> {
        static constexpr bool enable = 
            std::is_invocable_r_v<bool, typename __getattr__<Base, name>::type, Derived>;
    };
    static constexpr bool enable = impl::python<Derived> && impl::python<Base>;
    using type = bool;
    template <typename T = infer<"__instancecheck__">> requires (T::enable)
    static bool operator()(Derived obj, Base base);
};


/* Enables the C++ call operator for any `py::Object` subclass, and assigns a
corresponding return type.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__call__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self, typename... Args>
struct __call__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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


/* Enables the C++ subscript operator for any `py::Object` subclass, and assigns a
corresponding return type.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__getitem__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self, typename... Key>
struct __getitem__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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


/* Enables the C++ subscript assignment operator for any `py::Object` subclass, which
must return void.  The default specialization delegates to Python by introspecting
`__getattr__<Self, "__setitem__">`, which must return a member function, possibly with
Python-style argument annotations.  Specializations of this class may implement a
custom call operator to replace the default behavior. */
template <typename Self, typename Value, typename... Key>
struct __setitem__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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


/* Enables the C++ subscript deletion operator for any `py::Object` subclass, which
must return void.  The default specialization delegates to Python by introspecting
`__getattr__<Self, "__delitem__">`, which must return a member function, possibly with
Python-style argument annotations.  Specializations of this class may implement a
custom call operator to replace the default behavior. */
template <typename Self, typename... Key>
struct __delitem__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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


/* Enables the C++ size operator for any `py::Object` subclass, which must return
`size_t` for consistency with the C++ API.  The default specialization delegates to
Python by introspecting `__getattr__<Self, "__len__">`, which must return a member
function, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename Self>
struct __len__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_v<T> && impl::iterable<std::invoke_result_t<T>>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = impl::iter_type<std::invoke_result_t<T>>;
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
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T>
            requires (std::is_invocable_v<T> && impl::iterable<std::invoke_result_t<T>>)
        struct inspect<T> {
            static constexpr bool enable = true;
            using type = impl::iter_type<std::invoke_result_t<T>>;
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
    template <StaticStr>
    static constexpr bool _enable = false;
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    static constexpr bool _enable<name> =
        std::is_invocable_r_v<bool, typename __getattr__<Self, name>::type, Key>;
    static constexpr bool enable = _enable<"__contains__">;
    using type = bool;
};


/* Enables `std::hash<>` for any `py::Object` subclass, which must return `size_t` for
consistency with the C++ API.  The default specialization delegates to Python by
introspecting `__getattr__<Self, "__hash__">`, which must return a member function,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior. */
template <typename Self>
struct __hash__ {
    template <StaticStr>
    static constexpr bool _enable = false;
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
    static constexpr bool _enable<name> =
        std::is_invocable_r_v<size_t, typename __getattr__<Self, name>::type>;
    static constexpr bool enable = _enable<"__hash__">;
    using type = size_t;
};


/* Enables `std::abs()` for any `py::Object` subclass.  The default specialization
delegates to Python by introspecting `__getattr__<Self, "__abs__">`, which must return
a member function, possibly with Python-style argument annotations.  Specializations of
this class may implement a custom call operator to replace the default behavior. */
template <typename Self>
struct __abs__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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


/* Enables the C++ unary `~` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__invert__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __invert__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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


/* Enables the C++ unary `+` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__pos__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __pos__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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


/* Enables the C++ unary `-` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<Self, "__neg__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename Self>
struct __neg__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Self, name>::enable)
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


/* Enables the C++ binary `<` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__lt__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __lt__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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


/* Enables the C++ binary `<=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__le__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __le__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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


/* Enables the C++ binary `==` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__eq__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __eq__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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


/* Enables the C++ binary `!=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ne__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ne__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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


/* Enables the C++ binary `>=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ge__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ge__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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


/* Enables the C++ binary `>` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__gt__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __gt__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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


/* Enables the C++ binary `+` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__add__">`
or `__getattr__<R, "__radd__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __add__ {
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `+=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__iadd__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __iadd__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    static constexpr bool enable = infer<"__iadd__">::enable;
    using type = infer<"__iadd__">::type;
};


/* Enables the C++ prefix `++` operator for any `py::Object` subclass.  Enabled by
default if inplace addition with `Int` is enabled for the given type.  Specializations
of this class may implement a custom call operator to replace the default behavior.

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


/* Enables the C++ binary `-` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__sub__">`
or `__getattr__<R, "__rsub__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __sub__ {
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `-=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__isub__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __isub__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    static constexpr bool enable = infer<"__isub__">::enable;
    using type = infer<"__isub__">::type;
};


/* Enables the C++ prefix `--` operator for any `py::Object` subclass.  Enabled by
default if inplace subtraction with `Int` is enabled for the given type.
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


/* Enables the C++ binary `*` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__mul__">`
or `__getattr__<R, "__rmul__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __mul__ {
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `*=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__imul__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __imul__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `/=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__itruediv__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __itruediv__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `%=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__imod__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __imod__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    static constexpr bool enable = infer<"__imod__">::enable;
    using type = infer<"__imod__">::type;
};


/* Implements the Python `pow()` operator logic in C++ for any `py::Object` subclass,
which has no corresponding C++ operator.  The default specialization delegates to
Python by introspecting either `__getattr__<L, "__pow__">` or
`__getattr__<R, "__rpow__">` in that order, both of which must return member functions,
possibly with Python-style argument annotations.  Specializations of this class may
implement a custom call operator to replace the default behavior.

This is used internally to implement `py::pow()`. */
template <typename Base, typename Exp, typename Mod = void>
struct __pow__ {
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Base, name>::enable)
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


/* Implements the Python `**=` operator logic in C++ for any `py::Object` subclass,
which has no corresponding C++ operator.  The default specialization delegates to
Python by introspecting `__getattr__<L, "__ipow__">`, which must return a member
function, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior.

This is used internally to implement `py::ipow()`. */
template <typename Base, typename Exp, typename Mod = void>
struct __ipow__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<Base, name>::enable)
    struct infer<name> {
        template <typename>
        struct inspect {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename T> requires (std::is_invocable_r_v<Object, T, Exp>) 
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


/* Enables the C++ binary `<<` operator for any `py::Object` subclass.  the default
specialization delegates to Python by introspecting either `__getattr__<L, "__lshift__">`
or `__getattr__<R, "__rlshift__">` in that order, both of which must return member
functions, possibly with Python-style argument annotations.  Specializations of this
class may implement a custom call operator to replace the default behavior. */
template <typename L, typename R>
struct __lshift__ {
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `<<=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ilshift__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ilshift__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `>>=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__irshift__">`,
which must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __irshift__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `&=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__iand__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __iand__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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


/* Enables the C++ `|=` operator for any `py::Object` subclass.  The default
specialization delegates to Python by introspecting `__getattr__<L, "__ior__">`, which
must return a member function, possibly with Python-style argument annotations.
Specializations of this class may implement a custom call operator to replace the
default behavior. */
template <typename L, typename R>
struct __ior__ {
    template <StaticStr>
    struct infer {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct forward {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<L, name>::enable)
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
    template <StaticStr>
    struct reverse {
        static constexpr bool enable = false;
        using type = void;
    };
    template <StaticStr name> requires (__getattr__<R, name>::enable)
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
        template <typename T> requires (std::is_invocable_r_v<Object, T, R>) 
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


namespace impl {

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
        requires (
            __getattr__<Self, Name>::enable &&
            std::derived_from<typename __getattr__<Self, Name>::type, Object> && (
                !std::is_invocable_v<__getattr__<Self, Name>, Self> ||
                std::is_invocable_r_v<
                    typename __getattr__<Self, Name>::type,
                    __getattr__<Self, Name>,
                    Self
                >
            )
        )
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

    template <typename Self, typename... Key>
        requires (
            __getitem__<Self, Key...>::enable &&
            std::convertible_to<typename __getitem__<Self, Key...>::type, Object> && (
                std::is_invocable_r_v<
                    typename __getitem__<Self, Key...>::type,
                    __getitem__<Self, Key...>,
                    Self,
                    Key...
                > || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    has_cpp<Self> &&
                    lookup_yields<
                        cpp_type<Self>&,
                        typename __getitem__<Self, Key...>::type,
                        Key...
                    >
                ) || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    !has_cpp<Self> &&
                    std::derived_from<typename __getitem__<Self, Key...>::type, Object>
                )
            )
        )
    struct Item;
    template <typename T>
    struct item_helper { static constexpr bool enable = false; };
    template <typename Self, typename... Key>
    struct item_helper<Item<Self, Key...>> {
        static constexpr bool enable = true;
        using type = __getitem__<Self, Key...>::type;
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

    /// TODO: eventually I should reconsider the following concepts and potentially
    /// standardize them in some way.  Ideally, I can fully remove them and replace
    /// the logic with converts_to.

    template <typename T, typename Base>
    concept converts_to = __cast__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __cast__<std::remove_cvref_t<T>>::type, Base>;

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
