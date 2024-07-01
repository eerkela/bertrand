#ifndef BERTRAND_PYTHON_COMMON_DECLARATIONS_H
#define BERTRAND_PYTHON_COMMON_DECLARATIONS_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <chrono>
#include <complex>
#include <deque>
#include <initializer_list>
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
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Python.h>
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/functional.h>
#include <pybind11/iostream.h>
// #include <pybind11/numpy.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <bertrand/common.h>
#include <bertrand/static_str.h>


namespace bertrand {
namespace py {


namespace impl {
    /* Identifies all types that are part of the bertrand library. */
    struct BertrandTag {};
}


/* A static RAII guard that automatically initializes the Python interpreter the first
time a Python object is created and finalizes it when the program exits. */
struct Interpreter : public impl::BertrandTag {

    /* Ensure that the interpreter is active within the given context.  This is
    called internally whenever a Python object is created from pure C++ inputs, and is
    not called in any other context in order to avoid unnecessary overhead.  It must be
    implemented as a function in order to avoid C++'s static initialization order
    fiasco.*/
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


///////////////////////////////////////
////    INHERITED FROM PYBIND11    ////
///////////////////////////////////////


/* Pybind11 documentation:
*     https://pybind11.readthedocs.io/en/stable/
*/


// binding functions
using pybind11::init;  // TODO: can be automatically inferred via AST parsing
using pybind11::init_alias;  // TODO: not needed
using pybind11::implicitly_convertible;  // Do not use
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
using pybind11::overload_cast;
using pybind11::const_;
using pybind11::args;  // TODO: superceded by Arg<>
using pybind11::kwargs;  // TODO: superceded by Arg<>
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


////////////////////////////////////
////    ARGUMENT ANNOTATIONS    ////
////////////////////////////////////


// TODO: variadic arguments currently don't work with references.  Need to use a
// std::reference_wrapper<> for these cases.

// TODO: need to implement unpacking proxies for the dereference operator.  Single
// unpacking requires the object to be iterable, and double unpacking requires it to
// be a mapping.  In fact, it needs to be specifically indexable with a string.


namespace impl {
    struct ArgTag : public BertrandTag {};
}


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a py::Function. */
template <StaticStr Name, typename T>
class Arg : public impl::ArgTag {

    template <bool positional, bool keyword>
    class Optional : public impl::ArgTag {
        T m_value;

    public:
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = positional;
        static constexpr bool is_keyword = keyword;
        static constexpr bool is_optional = true;
        static constexpr bool is_variadic = false;

        template <std::convertible_to<T> V>
        Optional(V&& value) : m_value(std::forward<V>(value)) {}
        Optional(const Arg& other) : m_value(other.m_value) {}
        Optional(Arg&& other) : m_value(std::move(other.m_value)) {}

        [[nodiscard]] std::remove_reference_t<T>& value() & { return m_value; }
        [[nodiscard]] std::remove_reference_t<T>&& value() && { return std::move(m_value); }
        [[nodiscard]] const std::remove_const_t<std::remove_reference_t<T>>& value() const & {
            return m_value;
        }

        [[nodiscard]] operator std::remove_reference_t<T>&() & { return m_value; }
        [[nodiscard]] operator std::remove_reference_t<T>&&() && { return std::move(m_value); }
        [[nodiscard]] operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return m_value;
        }
    };

    template <bool optional>
    class Positional : public impl::ArgTag {
        T m_value;

    public:
        using type = T;
        using opt = Optional<true, false>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        template <std::convertible_to<T> V>
        Positional(V&& value) : m_value(std::forward<V>(value)) {}
        Positional(const Arg& other) : m_value(other.m_value) {}
        Positional(Arg&& other) : m_value(std::move(other.m_value)) {}

        [[nodiscard]] std::remove_reference_t<T>& value() & { return m_value; }
        [[nodiscard]] std::remove_reference_t<T>&& value() && { return std::move(m_value); }
        [[nodiscard]] const std::remove_const_t<std::remove_reference_t<T>>& value() const & {
            return m_value;
        }

        [[nodiscard]] operator std::remove_reference_t<T>&() & { return m_value; }
        [[nodiscard]] operator std::remove_reference_t<T>&&() && { return std::move(m_value); }
        [[nodiscard]] operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return m_value;
        }
    };

    template <bool optional>
    class Keyword : public impl::ArgTag {
        T m_value;

    public:
        using type = T;
        using opt = Optional<false, true>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        template <std::convertible_to<T> V>
        Keyword(V&& value) : m_value(std::forward<V>(value)) {}
        Keyword(const Arg& other) : m_value(other.m_value) {}
        Keyword(Arg&& other) : m_value(std::move(other.m_value)) {}

        [[nodiscard]] std::remove_reference_t<T>& value() & { return m_value; }
        [[nodiscard]] std::remove_reference_t<T>&& value() && { return std::move(m_value); }
        [[nodiscard]] const std::remove_const_t<std::remove_reference_t<T>>& value() const & {
            return m_value;
        }

        [[nodiscard]] operator std::remove_reference_t<T>&() & { return m_value; }
        [[nodiscard]] operator std::remove_reference_t<T>&&() && { return std::move(m_value); }
        [[nodiscard]] operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return m_value;
        }
    };

    class Args : public impl::ArgTag {
        std::vector<T> m_value;

    public:
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        Args() = default;
        Args(const std::vector<T>& value) : m_value(value) {}
        Args(std::vector<T>&& value) : m_value(std::move(value)) {}
        template <std::convertible_to<T> V>
        Args(const std::vector<V>& value) {
            m_value.reserve(value.size());
            for (const auto& item : value) {
                m_value.push_back(item);
            }
        }
        Args(const Args& other) : m_value(other.m_value) {}
        Args(Args&& other) : m_value(std::move(other.m_value)) {}

        [[nodiscard]] std::vector<T>& value() & { return m_value; }
        [[nodiscard]] std::vector<T>&& value() && { return std::move(m_value); }
        [[nodiscard]] const std::vector<T>& value() const & { return m_value; }

        [[nodiscard]] operator std::vector<T>&() & { return m_value; }
        [[nodiscard]] operator std::vector<T>&&() && { return std::move(m_value); }
        [[nodiscard]] operator const std::vector<T>&() const & { return m_value; }

        [[nodiscard]] auto begin() const { return m_value.begin(); }
        [[nodiscard]] auto cbegin() const { return m_value.cbegin(); }
        [[nodiscard]] auto end() const { return m_value.end(); }
        [[nodiscard]] auto cend() const { return m_value.cend(); }
        [[nodiscard]] auto rbegin() const { return m_value.rbegin(); }
        [[nodiscard]] auto crbegin() const { return m_value.crbegin(); }
        [[nodiscard]] auto rend() const { return m_value.rend(); }
        [[nodiscard]] auto crend() const { return m_value.crend(); }
        [[nodiscard]] constexpr auto size() const { return m_value.size(); }
        [[nodiscard]] constexpr auto empty() const { return m_value.empty(); }
        [[nodiscard]] constexpr auto data() const { return m_value.data(); }
        [[nodiscard]] constexpr decltype(auto) front() const { return m_value.front(); }
        [[nodiscard]] constexpr decltype(auto) back() const { return m_value.back(); }
        [[nodiscard]] constexpr decltype(auto) operator[](size_t index) const {
            return m_value.at(index);
        } 
    };

    class Kwargs : public impl::ArgTag {
        std::unordered_map<std::string, T> m_value;

    public:
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        Kwargs();
        Kwargs(const std::unordered_map<std::string, T>& value) : m_value(value) {}
        Kwargs(std::unordered_map<std::string, T>&& value) : m_value(std::move(value)) {}
        template <std::convertible_to<T> V>
        Kwargs(const std::unordered_map<std::string, V>& value) {
            m_value.reserve(value.size());
            for (const auto& [k, v] : value) {
                m_value.emplace(k, v);
            }
        }
        Kwargs(const Kwargs& other) : m_value(other.m_value) {}
        Kwargs(Kwargs&& other) : m_value(std::move(other.m_value)) {}

        [[nodiscard]] std::unordered_map<std::string, T>& value() & {
            return m_value;
        }
        [[nodiscard]] std::unordered_map<std::string, T>&& value() && {
            return std::move(m_value);
        }
        [[nodiscard]] const std::unordered_map<std::string, T>& value() const & {
            return m_value;
        }

        [[nodiscard]] operator std::unordered_map<std::string, T>&() & {
            return m_value;
        }
        [[nodiscard]] operator std::unordered_map<std::string, T>&&() && {
            return std::move(m_value);
        }
        [[nodiscard]] operator const std::unordered_map<std::string, T>&() const & {
            return m_value;
        }

        [[nodiscard]] auto begin() const { return m_value.begin(); }
        [[nodiscard]] auto cbegin() const { return m_value.cbegin(); }
        [[nodiscard]] auto end() const { return m_value.end(); }
        [[nodiscard]] auto cend() const { return m_value.cend(); }
        [[nodiscard]] constexpr auto size() const { return m_value.size(); }
        [[nodiscard]] constexpr bool empty() const { return m_value.empty(); }
        [[nodiscard]] constexpr bool contains(const std::string& key) const {
            return m_value.contains(key);
        }
        [[nodiscard]] constexpr auto count(const std::string& key) const {
            return m_value.count(key);
        }
        [[nodiscard]] decltype(auto) find(const std::string& key) const {
            return m_value.find(key);
        }
        [[nodiscard]] decltype(auto) operator[](const std::string& key) const {
            return m_value.at(key);
        }
    };

    T m_value;

public:
    static_assert(Name != "", "Argument name cannot be an empty string.");

    using type = T;
    using pos = Positional<false>;
    using kw = Keyword<false>;
    using opt = Optional<true, true>;
    using args = Args;
    using kwargs = Kwargs;
    static constexpr StaticStr name = Name;
    static constexpr bool is_positional = true;
    static constexpr bool is_keyword = true;
    static constexpr bool is_optional = false;
    static constexpr bool is_variadic = false;

    template <std::convertible_to<T> V>
    Arg(V&& value) : m_value(std::forward<V>(value)) {}
    Arg(const Arg& other) : m_value(other.m_value) {}
    Arg(Arg&& other) : m_value(std::move(other.m_value)) {}

    [[nodiscard]] std::remove_reference_t<T>& value() & { return m_value; }
    [[nodiscard]] std::remove_reference_t<T>&& value() && { return std::move(m_value); }
    [[nodiscard]] const std::remove_const_t<std::remove_reference_t<T>>& value() const & {
        return m_value;
    }

    [[nodiscard]] operator std::remove_reference_t<T>&() & { return m_value; }
    [[nodiscard]] operator std::remove_reference_t<T>&&() && { return std::move(m_value); }
    [[nodiscard]] operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
        return m_value;
    }
};


namespace impl {

    /* A compile-time tag that allows for the familiar `py::arg<"name"> = value`
    syntax.  The `py::arg<"name">` part resolves to an instance of this class, and the
    argument becomes bound when the `=` operator is applied to it. */
    template <StaticStr name>
    struct UnboundArg {
        template <typename T>
        constexpr Arg<name, T> operator=(T&& value) const {
            return {std::forward<T>(value)};
        }
    };

    /* A special proxy for an iterable container that unpacks it into a sequence of
    positional arguments that can be used to invoke a py::Function. */
    template <typename Sequence>
    struct UnpackPositional : public ArgTag {
        Sequence& container;

        // TODO: set flags so that this is recognized as an ::args type

        UnpackPositional(Sequence& container) : container(container) {}

        auto size() const { return std::size(container); }
        auto begin() { return std::begin(container); }
        auto begin() const { return std::begin(container); }
        auto cbegin() const { return std::cbegin(container); }
        auto end() { return std::end(container); }
        auto end() const { return std::end(container); }
        auto cend() const { return std::cend(container); }

    };

    /* A special proxy for a mapping that unpacks it into a sequence of keyword
    arguments that can be used to invoke a py::Function. */
    template <typename Mapping>
    struct UnpackKeyword : public ArgTag {
        Mapping& container;

        // TODO: set flags so that this is recognized as a ::kwargs type

        UnpackKeyword(Mapping& container) : container(container) {}

        auto size() const { return std::size(container); }
        auto begin() { return std::begin(container); }
        auto begin() const { return std::begin(container); }
        auto cbegin() const { return std::cbegin(container); }
        auto end() { return std::end(container); }
        auto end() const { return std::end(container); }
        auto cend() const { return std::cend(container); }

        // TODO: index by string

    };

}


/* Compile-time factory for `UnboundArgument` tags. */
template <StaticStr name>
static constexpr impl::UnboundArg<name> arg {};


///////////////////////////////
////    PRIMITIVE TYPES    ////
///////////////////////////////


template <typename... Args>
using Class = pybind11::class_<Args...>;  // TODO: these don't inherit from BertrandTag, but should
using Handle = pybind11::handle;
using WeakRef = pybind11::weakref;
using Capsule = pybind11::capsule;
using Buffer = pybind11::buffer;  // TODO: delete this and force users to use memoryview instead
using MemoryView = pybind11::memoryview;  // TODO: place in buffer.h along with memoryview
class Object;
template <typename>
class Function;
// class ClassMethod;  // TODO: template on function type
// class StaticMethod;  // TODO: template on function type
// class Property;  // NOTE: no need to template because getters/setters/deleters have consistent signatures
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
template <typename Val = Object>
class List;
template <typename Val = Object>
class Tuple;
template <typename Key = Object>
class Set;
template <typename Key = Object>
class FrozenSet;
template <typename Key = Object, typename Val = Object>
class Dict;
template <typename Map>
class KeyView;
template <typename Map>
class ValueView;
template <typename Map>
class ItemView;
template <typename Map>
class MappingProxy;
class Str;
class Bytes;
class ByteArray;
class Type;
class Super;
class Code;
class Frame;
class Timedelta;
class Timezone;
class Date;
class Time;
class Datetime;


namespace impl {
    struct ProxyTag : public BertrandTag {};
    struct FunctionTag : public BertrandTag { static const Type type; };
    struct TupleTag : public BertrandTag { static const Type type; };
    struct ListTag : public BertrandTag{ static const Type type; };
    struct SetTag : public BertrandTag { static const Type type; };
    struct FrozenSetTag : public BertrandTag { static const Type type; };
    struct KeyTag : public BertrandTag { static const Type type; };
    struct ValueTag : public BertrandTag { static const Type type; };
    struct ItemTag : public BertrandTag { static const Type type; };
    struct DictTag : public BertrandTag { static const Type type; };
    struct MappingProxyTag : public BertrandTag { static const Type type; };

    struct StructTag : TupleTag {};
}


///////////////////////////////
////    CONTROL STRUCTS    ////
///////////////////////////////


/* Base class for disabled control structures. */
struct Disable : public impl::BertrandTag {
    static constexpr bool enable = false;
};


/* Base class for enabled control structures.  Encodes the return type as a template
parameter. */
template <typename T>
struct Returns : public impl::BertrandTag {
    static constexpr bool enable = true;
    using type = T;
};


template <typename T>
struct __as_object__                                        : Returns<Object> {};
template <typename Derived, typename Base>
struct __isinstance__                                       : Disable {};
template <typename Derived, typename Base>
struct __issubclass__                                       : Disable {};
template <typename Self, typename... Args>
struct __init__                                             : Disable {};
template <typename Self, typename... Args>
struct __explicit_init__                                    : Disable {};
template <typename From, typename To>
struct __cast__                                             : Disable {};
template <typename From, typename To>
struct __explicit_cast__                                    : Disable {};
template <typename Self, typename... Args>
struct __call__                                             : Disable {};
template <typename Self, StaticStr Name>
struct __getattr__                                          : Disable {};
template <typename Self, StaticStr Name, typename Value>
struct __setattr__                                          : Disable {};
template <typename Self, StaticStr Name>
struct __delattr__                                          : Disable {};
template <typename Self, typename Key>
struct __getitem__                                          : Disable {};
template <typename Self, typename Key, typename Value>
struct __setitem__                                          : Disable {};
template <typename Self, typename Key>
struct __delitem__                                          : Disable {};
template <typename Self>
struct __len__                                              : Disable {};
template <typename Self>
struct __iter__                                             : Disable {};
template <typename Self>
struct __reversed__                                         : Disable {};
template <typename Self, typename Key>
struct __contains__                                         : Disable {};
template <typename Self>
struct __hash__                                             : Disable {};
template <typename Self>
struct __abs__                                              : Disable {};
template <typename Self>
struct __invert__                                           : Disable {};
template <typename Self>
struct __pos__                                              : Disable {};
template <typename Self>
struct __neg__                                              : Disable {};
template <typename Self>
struct __increment__                                        : Disable {};
template <typename Self>
struct __decrement__                                        : Disable {};
template <typename L, typename R>
struct __lt__                                               : Disable {};
template <typename L, typename R>
struct __le__                                               : Disable {};
template <typename L, typename R>
struct __eq__                                               : Disable {};
template <typename L, typename R>
struct __ne__                                               : Disable {};
template <typename L, typename R>
struct __ge__                                               : Disable {};
template <typename L, typename R>
struct __gt__                                               : Disable {};
template <typename L, typename R>
struct __add__                                              : Disable {};
template <typename L, typename R>
struct __iadd__                                             : Disable {};
template <typename L, typename R>
struct __sub__                                              : Disable {};
template <typename L, typename R>
struct __isub__                                             : Disable {};
template <typename L, typename R>
struct __mul__                                              : Disable {};
template <typename L, typename R>
struct __imul__                                             : Disable {};
template <typename L, typename R>
struct __truediv__                                          : Disable {};
template <typename L, typename R>
struct __itruediv__                                         : Disable {};
template <typename L, typename R>
struct __floordiv__                                         : Disable {};
template <typename L, typename R>
struct __ifloordiv__                                        : Disable {};
template <typename L, typename R>
struct __mod__                                              : Disable {};
template <typename L, typename R>
struct __imod__                                             : Disable {};
template <typename L, typename R>
struct __pow__                                              : Disable {};
template <typename L, typename R>
struct __ipow__                                             : Disable {};
template <typename L, typename R>
struct __lshift__                                           : Disable {};
template <typename L, typename R>
struct __ilshift__                                          : Disable {};
template <typename L, typename R>
struct __rshift__                                           : Disable {};
template <typename L, typename R>
struct __irshift__                                          : Disable {};
template <typename L, typename R>
struct __and__                                              : Disable {};
template <typename L, typename R>
struct __iand__                                             : Disable {};
template <typename L, typename R>
struct __or__                                               : Disable {};
template <typename L, typename R>
struct __ior__                                              : Disable {};
template <typename L, typename R>
struct __xor__                                              : Disable {};
template <typename L, typename R>
struct __ixor__                                             : Disable {};


/////////////////////////
////    UTILITIES    ////
/////////////////////////


namespace impl {

    /* Helper function triggers implicit conversion operators and/or implicit
    constructors, but not explicit ones.  In contrast, static_cast<>() will trigger
    explicit constructors on the target type, which can give unexpected results and
    violate strict type safety. */
    template <typename U>
    static decltype(auto) implicit_cast(U&& value) {
        return std::forward<U>(value);
    }

    /* Convenience class that stores a static Python string for use during attr lookups.
    Separating this into its own class ensures that only one string is allocated per
    attribute name, even if that attribute name is repeated across multiple contexts. */
    template <StaticStr name>
    struct TemplateString : public BertrandTag {
        inline static PyObject* ptr = (Interpreter::init(), PyUnicode_FromStringAndSize(
            name,
            name.size()
        ));  // NOTE: string will be garbage collected at shutdown
    };

    template <typename T>
    using as_object_t = __as_object__<std::remove_cvref_t<T>>::type;

    template <typename Obj, typename Key> requires (__getitem__<Obj, Key>::enable)
    class Item;
    template <typename Obj, StaticStr name> requires (__getattr__<Obj, name>::enable)
    class Attr;
    template <typename Policy>
    class Iterator;
    template <typename Policy>
    class ReverseIterator;
    template <typename Deref>
    class GenericIter;

    struct SliceInitializer;

    template <typename T>
    using iter_type = decltype(*std::begin(std::declval<T>()));

    template <typename T>
    using reverse_iter_type = decltype(*std::rbegin(std::declval<T>()));

    template <typename T, typename Key>
    using lookup_type = decltype(std::declval<T>()[std::declval<Key>()]);

}


////////////////////////
////    CONCEPTS    ////
////////////////////////


namespace impl {

    template <typename From, typename To>
    concept has_conversion_operator = requires(From&& from) {
        from.operator To();
    };

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(From&& from) {
        static_cast<To>(std::forward<From>(from));
    };

    // TODO: note that is_iterable now checks for an ADL begin method, rather than
    // specifically std::begin()/std::end().  This subtly changes their behavior.

    template <typename T>
    concept is_iterable = requires(T&& t) {
        { begin(std::forward<T>(t)) } -> std::input_or_output_iterator;
        { end(std::forward<T>(t)) } -> std::input_or_output_iterator;
    };

    template <typename T>
    concept is_reverse_iterable = requires(T&& t) {
        { rbegin(std::forward<T>(t)) } -> std::input_or_output_iterator;
        { rend(std::forward<T>(t)) } -> std::input_or_output_iterator;
    };

    template <typename T>
    concept iterator_like = requires(T&& it, T&& end) {
        { *std::forward<T>(it) } -> std::convertible_to<typename std::decay_t<T>::value_type>;
        { ++std::forward<T>(it) } -> std::same_as<std::remove_reference_t<T>&>;
        { std::forward<T>(it)++ } -> std::same_as<std::remove_reference_t<T>>;
        { std::forward<T>(it) == std::forward<T>(end) } -> std::convertible_to<bool>;
        { std::forward<T>(it) != std::forward<T>(end) } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept has_size = requires(T&& t) {
        { std::size(std::forward<T>(t)) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept sequence_like = is_iterable<T> && has_size<T> && requires(T&& t) {
        { std::forward<T>(t)[0] } -> std::convertible_to<iter_type<T>>;
    };

    template <typename T>
    concept mapping_like = requires(T&& t) {
        typename std::decay_t<T>::key_type;
        typename std::decay_t<T>::mapped_type;
        { std::forward<T>(t)[std::declval<typename std::decay_t<T>::key_type>()] } ->
            std::convertible_to<typename std::decay_t<T>::mapped_type>;
    };

    template <typename T>
    concept has_empty = requires(T&& t) {
        { std::forward<T>(t).empty() } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept has_reserve = requires(T&& t, size_t n) {
        { std::forward<T>(t).reserve(n) } -> std::same_as<void>;
    };

    template <typename T, typename Key>
    concept has_contains = requires(T&& t, Key&& key) {
        { std::forward<T>(t).contains(std::forward<Key>(key)) } -> std::convertible_to<bool>;
    };

    template <typename T, typename Key>
    concept supports_lookup = !std::is_pointer_v<T> && !std::integral<std::decay_t<T>> &&
        requires(T&& t, Key&& key) {
            { std::forward<T>(t)[std::forward<Key>(key)] };
        };

    template <typename T, typename Key, typename Value>
    concept lookup_yields = requires(T&& t, Key&& key) {
        { std::forward<T>(t)[std::forward<Key>(key)] } -> std::convertible_to<Value>;
    };

    template <typename T>
    concept pair_like = requires(T&& t) {
        { std::forward<T>(t).first } -> std::same_as<typename std::decay_t<T>::first_type>;
        { std::forward<T>(t).second } -> std::same_as<typename std::decay_t<T>::second_type>;
    };

    template <typename T>
    concept complex_like = requires(T&& t) {
        { std::forward<T>(t).real() } -> std::convertible_to<double>;
        { std::forward<T>(t).imag() } -> std::convertible_to<double>;
    };

    template <typename T>
    concept string_literal = requires(T&& t) {
        { []<size_t N>(const char(&)[N]){}(std::forward<T>(t)) };
    };

    // NOTE: decay is necessary to treat `const char[N]` like `const char*`
    template <typename T>
    concept is_hashable = requires(T&& t) {
        { std::hash<std::decay_t<T>>{}(std::forward<T>(t)) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_abs = requires(T&& t) {
        { std::abs(std::forward<T>(t)) };
    };

    template <typename T>
    concept has_to_string = requires(T&& t) {
        { std::to_string(std::forward<T>(t)) } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_stream_insertion = requires(std::ostream& os, T&& t) {
        { os << std::forward<T>(t) } -> std::convertible_to<std::ostream&>;
    };

    template <typename T>
    concept has_call_operator = requires { &std::decay_t<T>::operator(); };

    template <typename T>
    concept is_callable_any = 
        std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> ||
        std::is_member_function_pointer_v<std::decay_t<T>> ||
        has_call_operator<T>;



    // TODO: bertrand_like, pybind11_like are mutually orthogonal, and pybind11_like
    // matches all types exposed by pybind11.



    template <typename T>
    concept bertrand_like = std::derived_from<std::decay_t<T>, BertrandTag>;

    template <typename T>
    concept proxy_like = std::derived_from<std::decay_t<T>, ProxyTag>;

    template <typename T>
    concept not_proxy_like = !proxy_like<T>;

    template <typename T>
    concept pybind11_like = pybind11::detail::is_pyobject<std::decay_t<T>>::value;

    template <typename T>
    concept python_like = (
        pybind11_like<std::decay_t<T>> ||
        std::derived_from<std::decay_t<T>, Object>
    );

    template <typename T>
    concept is_object_exact = (
        std::same_as<std::decay_t<T>, Object> ||
        std::same_as<std::decay_t<T>, pybind11::object>
    );

    template <typename... Ts>
    concept any_are_python_like = (std::derived_from<std::decay_t<Ts>, Object> || ...);

    template <typename T>
    concept cpp_like = !python_like<T>;

    template <typename L, typename R>
    struct lt_comparable : public BertrandTag {
        static constexpr bool value = requires(L&& a, R&& b) {
            { std::forward<L>(a) < std::forward<R>(b) } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct le_comparable : public BertrandTag {
        static constexpr bool value = requires(L&& a, R&& b) {
            { std::forward<L>(a) <= std::forward<R>(b) } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct eq_comparable : public BertrandTag {
        static constexpr bool value = requires(L&& a, R&& b) {
            { std::forward<L>(a) == std::forward<R>(b) } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ne_comparable : public BertrandTag {
        static constexpr bool value = requires(L&& a, R&& b) {
            { std::forward<L>(a) != std::forward<R>(b) } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ge_comparable : public BertrandTag {
        static constexpr bool value = requires(L&& a, R&& b) {
            { std::forward<L>(a) >= std::forward<R>(b) } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct gt_comparable : public BertrandTag {
        static constexpr bool value = requires(L&& a, R&& b) {
            { std::forward<L>(a) > std::forward<R>(b) } -> std::convertible_to<bool>;
        };
    };

    template <typename T>
    concept none_like = std::derived_from<as_object_t<T>, NoneType>;

    template <typename T>
    concept notimplemented_like = std::derived_from<as_object_t<T>, NotImplementedType>;

    template <typename T>
    concept ellipsis_like = std::derived_from<as_object_t<T>, EllipsisType>;

    template <typename T>
    concept slice_like = std::derived_from<as_object_t<T>, Slice>;

    template <typename T>
    concept module_like = std::derived_from<as_object_t<T>, Module>;

    template <typename T>
    concept bool_like = std::derived_from<as_object_t<T>, Bool>;

    template <typename T>
    concept int_like = std::derived_from<as_object_t<T>, Int>;

    template <typename T>
    concept float_like = std::derived_from<as_object_t<T>, Float>;

    template <typename T>
    concept str_like = std::derived_from<as_object_t<T>, Str>;

    template <typename T>
    concept bytes_like = (
        string_literal<T> ||
        std::same_as<std::decay_t<T>, void*> ||
        std::derived_from<as_object_t<T>, Bytes>
    );

    template <typename T>
    concept bytearray_like = (
        string_literal<T> ||
        std::same_as<std::decay_t<T>, void*> ||
        std::derived_from<as_object_t<T>, ByteArray>
    );

    template <typename T>
    concept anybytes_like = bytes_like<T> || bytearray_like<T>;

    template <typename T>
    concept timedelta_like = std::derived_from<as_object_t<T>, Timedelta>;

    template <typename T>
    concept timezone_like = std::derived_from<as_object_t<T>, Timezone>;

    template <typename T>
    concept date_like = std::derived_from<as_object_t<T>, Date>;

    template <typename T>
    concept time_like = std::derived_from<as_object_t<T>, Time>;

    template <typename T>
    concept datetime_like = std::derived_from<as_object_t<T>, Datetime>;

    template <typename T>
    concept range_like = std::derived_from<as_object_t<T>, Range>;

    template <typename T>
    concept tuple_like = std::derived_from<as_object_t<T>, TupleTag>;

    template <typename T>
    concept list_like = std::derived_from<as_object_t<T>, ListTag>;

    template <typename T>
    concept set_like = std::derived_from<as_object_t<T>, SetTag>;

    template <typename T>
    concept frozenset_like = std::derived_from<as_object_t<T>, FrozenSetTag>;

    template <typename T>
    concept anyset_like = set_like<T> || frozenset_like<T>;

    template <typename T>
    concept dict_like = std::derived_from<as_object_t<T>, DictTag>;

    template <typename T>
    concept mappingproxy_like = std::derived_from<as_object_t<T>, MappingProxyTag>;

    template <typename T>
    concept anydict_like = dict_like<T> || mappingproxy_like<T>;

    template <typename T>
    concept type_like = std::derived_from<as_object_t<T>, Type>;

    /* NOTE: some binary operators (such as lexicographic comparisons) accept generic
     * containers, which may be combined with containers of different types.  In these
     * cases, the operator should be enabled if and only if it is also supported by the
     * respective element types.  This sounds simple, but is complicated by the
     * implementation of std::pair and std::tuple, which may contain heterogenous types.
     *
     * The Broadcast<> struct helps by recursively applying a scalar constraint over
     * the values of a generic container type, with specializations to account for
     * std::pair and std::tuple.  A generic specialization is provided for all types
     * that implement a nested `value_type` alias, as well as pybind11 types, which are
     * assumed to contain generic objects.  Note that the condition must be a type
     * trait (not a concept) in order to be valid as a template template parameter.
     */

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename R
    >
    struct Broadcast : public BertrandTag {
        template <typename T>
        struct deref { using type = T; };
        template <is_iterable T>
        struct deref<T> {
            using type = std::conditional_t<pybind11_like<T>, Object, iter_type<T>>;
        };

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
    struct Broadcast<Condition, std::pair<T1, T2>, std::pair<T3, T4>> : public BertrandTag {
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
    struct Broadcast<Condition, L, std::pair<T1, T2>> : public BertrandTag {
        static constexpr bool value =
            Broadcast<Condition, L, T1>::value && Broadcast<Condition, L, T2>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename T1,
        typename T2,
        typename R
    >
    struct Broadcast<Condition, std::pair<T1, T2>, R> : public BertrandTag {
        static constexpr bool value =
            Broadcast<Condition, T1, R>::value && Broadcast<Condition, T2, R>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename... Ts1,
        typename... Ts2
    >
    struct Broadcast<Condition, std::tuple<Ts1...>, std::tuple<Ts2...>> : public BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, Ts1, std::tuple<Ts2...>>::value && ...);
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename... Ts
    >
    struct Broadcast<Condition, L, std::tuple<Ts...>> : public BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, L, Ts>::value && ...);
    };

    template <
        template <typename, typename> typename Condition,
        typename... Ts,
        typename R
    >
    struct Broadcast<Condition, std::tuple<Ts...>, R> : public BertrandTag {
        static constexpr bool value =
            (Broadcast<Condition, Ts, R>::value && ...);
    };

}


/////////////////////////
////    UTILITIES    ////
/////////////////////////


namespace impl {

    template <typename T>
    struct unwrap_proxy_helper : public BertrandTag { using type = T; };
    template <proxy_like T>
    struct unwrap_proxy_helper<T> : public BertrandTag { using type = typename T::type; };
    template <typename T>
    using unwrap_proxy = typename unwrap_proxy_helper<T>::type;

}


/////////////////////////
////    FUNCTIONS    ////
/////////////////////////


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<pybind11::object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj) {
    return pybind11::reinterpret_borrow<T>(obj);
}


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj);


/* Steal a reference to a raw Python handle. */
template <std::derived_from<pybind11::object> T>
[[nodiscard]] T reinterpret_steal(Handle obj) {
    return pybind11::reinterpret_steal<T>(obj);
}


/* Steal a reference to a raw Python handle. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj);


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_DECLARATIONS_H
