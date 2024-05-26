#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

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
#include "bertrand/static_str.h"


namespace bertrand {
namespace py {


///////////////////////////////////////
////    INHERITED FROM PYBIND11    ////
///////////////////////////////////////


/* Pybind11 documentation:
*     https://pybind11.readthedocs.io/en/stable/
*/

// TODO: account for all relevant binding functions inherited from pybind11

// binding functions
using pybind11::init;
using pybind11::init_alias;
using pybind11::implicitly_convertible;
using pybind11::args_are_all_keyword_or_ds;  // TODO: superceded by new call syntax
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
using pybind11::arg;  // TODO: superceded by Arg<>
using pybind11::arg_v;  // TODO: superceded by Arg<>
using pybind11::kw_only;  // TODO: superceded by Arg<>
using pybind11::pos_only;  // TODO: superceded by Arg<>


////////////////////////////////////
////    ARGUMENT ANNOTATIONS    ////
////////////////////////////////////


namespace impl {
    struct ArgTag {};
}


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a py::Function. */
template <StaticStr Name, typename T>
class Arg : public impl::ArgTag {

    template <bool positional, bool keyword>
    struct Optional : public impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = positional;
        static constexpr bool is_keyword = keyword;
        static constexpr bool is_optional = true;
        static constexpr bool is_variadic = false;

        T value;

        template <std::convertible_to<T> V>
        Optional(V&& value) : value(std::forward<V>(value)) {}
        Optional(const Arg& other) : value(other.value) {}
        Optional(Arg&& other) : value(std::move(other.value)) {}

        operator std::remove_reference_t<T>&() & { return value; }
        operator std::remove_reference_t<T>&&() && { return std::move(value); }
        operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return value;
        }
    };

    template <bool optional>
    struct Positional : public impl::ArgTag {
        using type = T;
        using opt = Optional<true, false>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        T value;

        template <std::convertible_to<T> V>
        Positional(V&& value) : value(std::forward<V>(value)) {}
        Positional(const Arg& other) : value(other.m_value) {}
        Positional(Arg&& other) : value(std::move(other.m_value)) {}

        operator std::remove_reference_t<T>&() & { return value; }
        operator std::remove_reference_t<T>&&() && { return std::move(value); }
        operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return value;
        }
    };

    template <bool optional>
    struct Keyword : public impl::ArgTag {
        using type = T;
        using opt = Optional<false, true>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        T value;

        template <std::convertible_to<T> V>
        Keyword(V&& value) : value(std::forward<V>(value)) {}
        Keyword(const Arg& other) : value(other.m_value) {}
        Keyword(Arg&& other) : value(std::move(other.m_value)) {}

        operator std::remove_reference_t<T>&() & { return value; }
        operator std::remove_reference_t<T>&&() && { return std::move(value); }
        operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return value;
        }
    };

    struct Args : public impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        std::vector<T> value;

        Args() = default;
        Args(const std::vector<T>& value) : value(value) {}
        Args(std::vector<T>&& value) : value(std::move(value)) {}
        template <std::convertible_to<T> V>
        Args(const std::vector<V>& value) {
            this->value.reserve(value.size());
            for (const auto& item : value) {
                this->value.push_back(item);
            }
        }
        Args(const Args& other) : value(other.value) {}
        Args(Args&& other) : value(std::move(other.value)) {}

        operator std::vector<T>&() & { return value; }
        operator std::vector<T>&&() && { return std::move(value); }
        operator const std::vector<T>&() const & { return value; }

        auto begin() const { return value.begin(); }
        auto cbegin() const { return value.cbegin(); }
        auto end() const { return value.end(); }
        auto cend() const { return value.cend(); }
        auto rbegin() const { return value.rbegin(); }
        auto crbegin() const { return value.crbegin(); }
        auto rend() const { return value.rend(); }
        auto crend() const { return value.crend(); }
        constexpr auto size() const { return value.size(); }
        constexpr auto empty() const { return value.empty(); }
        constexpr auto data() const { return value.data(); }
        constexpr decltype(auto) front() const { return value.front(); }
        constexpr decltype(auto) back() const { return value.back(); }
        constexpr decltype(auto) operator[](size_t index) const { return value.at(index); } 
    };

    struct Kwargs : public impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        std::unordered_map<std::string, T> value;

        Kwargs();
        Kwargs(const std::unordered_map<std::string, T>& value) : value(value) {}
        Kwargs(std::unordered_map<std::string, T>&& value) : value(std::move(value)) {}
        template <std::convertible_to<T> V>
        Kwargs(const std::unordered_map<std::string, V>& value) {
            this->value.reserve(value.size());
            for (const auto& [k, v] : value) {
                this->value.emplace(k, v);
            }
        }
        Kwargs(const Kwargs& other) : value(other.value) {}
        Kwargs(Kwargs&& other) : value(std::move(other.value)) {}

        operator std::unordered_map<std::string, T>&() & { return value; }
        operator std::unordered_map<std::string, T>&&() && { return std::move(value); }
        operator const std::unordered_map<std::string, T>&() const & { return value; }

        auto begin() const { return value.begin(); }
        auto cbegin() const { return value.cbegin(); }
        auto end() const { return value.end(); }
        auto cend() const { return value.cend(); }
        constexpr auto size() const { return value.size(); }
        constexpr bool empty() const { return value.empty(); }
        constexpr bool contains(const std::string& key) const { return value.contains(key); }
        constexpr auto count(const std::string& key) const { return value.count(key); }
        decltype(auto) find(const std::string& key) const { return value.find(key); }
        decltype(auto) operator[](const std::string& key) const { return value.at(key); }
    };

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

    T value;

    template <std::convertible_to<T> V>
    Arg(V&& value) : value(std::forward<V>(value)) {}
    Arg(const Arg& other) : value(other.value) {}
    Arg(Arg&& other) : value(std::move(other.value)) {}

    operator std::remove_reference_t<T>&() & { return value; }
    operator std::remove_reference_t<T>&&() && { return std::move(value); }
    operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
        return value;
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

}


/* Compile-time factory for `UnboundArgument` tags. */
template <StaticStr name>
static constexpr impl::UnboundArg<name> arg_ {};


///////////////////////////////
////    PRIMITIVE TYPES    ////
///////////////////////////////


template <typename... Args>
using Class = pybind11::class_<Args...>;
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
    struct InitializerTag {};  // TODO: delete?
    struct ProxyTag {};
    struct FunctionTag { static const Type type; };
    struct TupleTag { static const Type type; };
    struct ListTag { static const Type type; };
    struct SetTag { static const Type type; };
    struct FrozenSetTag { static const Type type; };
    struct KeyTag { static const Type type; };
    struct ValueTag { static const Type type; };
    struct ItemTag { static const Type type; };
    struct DictTag { static const Type type; };
    struct MappingProxyTag { static const Type type; };
}


///////////////////////////////
////    CONTROL STRUCTS    ////
///////////////////////////////


/* Base class for enabled control structures.  Encodes the return type as a template
parameter. */
template <typename T>
struct Returns {
    static constexpr bool enable = true;
    using Return = T;
};


template <typename T>
struct __as_object__                        : Returns<Object> {};  // enabled by default
template <typename Self, typename T>
struct __implicit_cast__                    { static constexpr bool enable = false; };
template <typename Self, typename T>
struct __explicit_cast__                    : Returns<T> {  // enabled by default
    static T operator()(const Self& self);  // falls back to pybind11::cast
};
template <typename Self, typename... Args>
struct __call__                             { static constexpr bool enable = false; };
template <typename Self, StaticStr Name>
struct __getattr__                          { static constexpr bool enable = false; };
template <typename Self, StaticStr Name, typename Value>
struct __setattr__                          { static constexpr bool enable = false; };
template <typename Self, StaticStr Name>
struct __delattr__                          { static constexpr bool enable = false; };
template <typename Self, typename Key>
struct __getitem__                          { static constexpr bool enable = false; };
template <typename Self, typename Key, typename Value>
struct __setitem__                          { static constexpr bool enable = false; };
template <typename Self, typename Key>
struct __delitem__                          { static constexpr bool enable = false; };
template <typename Self>
struct __len__                              { static constexpr bool enable = false; };
template <typename Self>
struct __iter__                             { static constexpr bool enable = false; };
template <typename Self>
struct __reversed__                         { static constexpr bool enable = false; };
template <typename Self, typename Key>
struct __contains__                         { static constexpr bool enable = false; };
template <typename Self>
struct __hash__                             { static constexpr bool enable = false; };
template <typename Self>
struct __abs__                              { static constexpr bool enable = false; };
template <typename Self>
struct __invert__                           { static constexpr bool enable = false; };
template <typename Self>
struct __pos__                              { static constexpr bool enable = false; };
template <typename Self>
struct __neg__                              { static constexpr bool enable = false; };
template <typename Self>
struct __increment__                        { static constexpr bool enable = false; };
template <typename Self>
struct __decrement__                        { static constexpr bool enable = false; };
template <typename L, typename R>
struct __lt__                               { static constexpr bool enable = false; };
template <typename L, typename R>
struct __le__                               { static constexpr bool enable = false; };
template <typename L, typename R>
struct __eq__                               { static constexpr bool enable = false; };
template <typename L, typename R>
struct __ne__                               { static constexpr bool enable = false; };
template <typename L, typename R>
struct __ge__                               { static constexpr bool enable = false; };
template <typename L, typename R>
struct __gt__                               { static constexpr bool enable = false; };
template <typename L, typename R>
struct __add__                              { static constexpr bool enable = false; };
template <typename L, typename R>
struct __iadd__                             { static constexpr bool enable = false; };
template <typename L, typename R>
struct __sub__                              { static constexpr bool enable = false; };
template <typename L, typename R>
struct __isub__                             { static constexpr bool enable = false; };
template <typename L, typename R>
struct __mul__                              { static constexpr bool enable = false; };
template <typename L, typename R>
struct __imul__                             { static constexpr bool enable = false; };
template <typename L, typename R>
struct __truediv__                          { static constexpr bool enable = false; };
template <typename L, typename R>
struct __itruediv__                         { static constexpr bool enable = false; };
template <typename L, typename R>
struct __floordiv__                         { static constexpr bool enable = false; };
template <typename L, typename R>
struct __ifloordiv__                        { static constexpr bool enable = false; };
template <typename L, typename R>
struct __mod__                              { static constexpr bool enable = false; };
template <typename L, typename R>
struct __imod__                             { static constexpr bool enable = false; };
template <typename L, typename R>
struct __pow__                              { static constexpr bool enable = false; };
template <typename L, typename R>
struct __ipow__                             { static constexpr bool enable = false; };
template <typename L, typename R>
struct __lshift__                           { static constexpr bool enable = false; };
template <typename L, typename R>
struct __ilshift__                          { static constexpr bool enable = false; };
template <typename L, typename R>
struct __rshift__                           { static constexpr bool enable = false; };
template <typename L, typename R>
struct __irshift__                          { static constexpr bool enable = false; };
template <typename L, typename R>
struct __and__                              { static constexpr bool enable = false; };
template <typename L, typename R>
struct __iand__                             { static constexpr bool enable = false; };
template <typename L, typename R>
struct __or__                               { static constexpr bool enable = false; };
template <typename L, typename R>
struct __ior__                              { static constexpr bool enable = false; };
template <typename L, typename R>
struct __xor__                              { static constexpr bool enable = false; };
template <typename L, typename R>
struct __ixor__                             { static constexpr bool enable = false; };


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
    struct TemplateString {
        static PyObject* ptr;  // NOTE: string will be garbage collected at shutdown
    };

    template <StaticStr name>
    inline PyObject* TemplateString<name>::ptr = PyUnicode_FromStringAndSize(
        name,
        name.size()
    );

    template <typename T>
    using as_object_t = __as_object__<std::remove_cvref_t<T>>::Return;

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

    // TODO: remove SliceInitializer or make it into a std::variant?
    struct SliceInitializer;

    template <typename T>
    constexpr bool is_initializer_list = false;
    template <typename T>
    constexpr bool is_initializer_list<std::initializer_list<T>> = true;

    template <typename T>
    using dereference_type = decltype(*std::begin(std::declval<T>()));

    template <typename T>
    using reverse_dereference_type = decltype(*std::rbegin(std::declval<T>()));

    template <typename T, typename Key>
    using lookup_type = decltype(std::declval<T>()[std::declval<Key>()]);

    template <typename T, typename... Ts>
    using first = T;

}


////////////////////////
////    CONCEPTS    ////
////////////////////////


namespace impl {

    template <typename From, typename To>
    concept has_conversion_operator = requires(const From& from) {
        from.operator To();
    };

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(const From& from) {
        static_cast<To>(from);
    };

    template <typename T>
    concept initializer_like = is_initializer_list<std::remove_cvref_t<T>>;

    template <typename T>
    concept is_iterable = requires(T t) {
        { std::begin(t) } -> std::input_or_output_iterator;
        { std::end(t) } -> std::input_or_output_iterator;
    };

    template <typename T>
    concept is_reverse_iterable = requires(const T& t) {
        { std::rbegin(t) } -> std::input_or_output_iterator;
        { std::rend(t) } -> std::input_or_output_iterator;
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
    concept has_size = requires(const T& t) {
        { std::size(t) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept sequence_like = is_iterable<T> && has_size<T> && requires(const T& t) {
        { t[0] };
    };

    template <typename T>
    concept has_empty = requires(const T& t) {
        { t.empty() } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept has_reserve = requires(T& t, size_t n) {
        { t.reserve(n) } -> std::same_as<void>;
    };

    template <typename T, typename Key>
    concept has_contains = requires(const T& t, const Key& key) {
        { t.contains(key) } -> std::convertible_to<bool>;
    };

    template <typename T, typename Key>
    concept supports_lookup = requires(T t, const Key& key) {
        { t[key] };
    } && !std::integral<T> && !std::is_pointer_v<T>;

    template <typename T, typename Key, typename Value>
    concept lookup_yields = requires(T t, const Key& key, const Value& value) {
        { t[key] } -> std::convertible_to<Value>;
    };

    template <typename T>
    concept pair_like = requires(const T& t) {
        { t.first } -> std::same_as<typename T::first_type>;
        { t.second } -> std::same_as<typename T::second_type>;
    };

    template <typename T>
    concept complex_like = requires(const T& t) {
        { t.real() } -> std::convertible_to<double>;
        { t.imag() } -> std::convertible_to<double>;
    };

    template <typename T>
    concept string_literal = requires(const T& t) {
        { []<size_t N>(const char(&)[N]){}(t) } -> std::same_as<void>;
    };

    // NOTE: decay is necessary to treat `const char[N]` like `const char*`
    template <typename T>
    concept is_hashable = requires(T&& t) {
        { std::hash<std::decay_t<T>>{}(std::forward<T>(t)) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_abs = requires(const T& t) {
        { std::abs(t) };
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
    concept has_call_operator = requires { &std::decay_t<T>::operator(); };

    template <typename T>
    concept is_callable_any = 
        std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> ||
        std::is_member_function_pointer_v<std::decay_t<T>> ||
        has_call_operator<T>;

    template <typename T, typename... Ts>
    concept homogenous = (std::same_as<T, Ts> && ...);

    template <typename T>
    concept proxy_like = std::derived_from<std::remove_cvref_t<T>, ProxyTag>;

    template <typename T>
    concept not_proxy_like = !proxy_like<T>;

    template <typename T>
    concept pybind11_like = pybind11::detail::is_pyobject<std::remove_cvref_t<T>>::value;

    template <typename T>
    concept python_like = (
        pybind11_like<std::remove_cvref_t<T>> ||
        std::derived_from<std::remove_cvref_t<T>, Object>
    );

    template <typename T>
    concept is_object_exact = (
        std::same_as<std::remove_cvref_t<T>, Object> ||
        std::same_as<std::remove_cvref_t<T>, pybind11::object>
    );

    template <typename... Ts>
    concept any_are_python_like = (std::derived_from<std::remove_cvref_t<Ts>, Object> || ...);

    template <typename T>
    concept cpp_like = !python_like<T>;

    template <typename L, typename R>
    struct lt_comparable {
        static constexpr bool value = requires(const L& a, const R& b) {
            { a < b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct le_comparable {
        static constexpr bool value = requires(const L& a, const R& b) {
            { a <= b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct eq_comparable {
        static constexpr bool value = requires(const L& a, const R& b) {
            { a == b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ne_comparable {
        static constexpr bool value = requires(const L& a, const R& b) {
            { a != b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ge_comparable {
        static constexpr bool value = requires(const L& a, const R& b) {
            { a >= b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct gt_comparable {
        static constexpr bool value = requires(const L& a, const R& b) {
            { a > b } -> std::convertible_to<bool>;
        };
    };

    template <typename T>
    concept none_like = std::derived_from<as_object_t<T>, NoneType>;

    template <typename T>
    concept ellipsis_like = std::derived_from<as_object_t<T>, EllipsisType>;

    template <typename T>
    concept slice_like = std::derived_from<as_object_t<T>, Slice>;

    template <typename T>
    concept module_like = std::derived_from<as_object_t<T>, Module>;

    // TODO: if I swap this to std::derived_from, I get compile errors
    template <typename T>
    concept bool_like = std::same_as<as_object_t<T>, Bool>;

    // TODO: if I swap this to std::derived_from, I get compile errors
    template <typename T>
    concept int_like = std::same_as<as_object_t<T>, Int>;

    // TODO: if I swap this to std::derived_from, I get compile errors
    template <typename T>
    concept float_like = std::same_as<as_object_t<T>, Float>;

    template <typename T>
    concept str_like = std::derived_from<as_object_t<T>, Str>;

    template <typename T>
    concept bytes_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::derived_from<as_object_t<T>, Bytes>
    );

    template <typename T>
    concept bytearray_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::same_as<std::remove_cvref_t<T>, void*> ||
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

    template <typename Derived, typename Base>
    concept typecheck = Base::template check<Derived>();

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
    struct Broadcast {
        template <typename T>
        struct deref { using type = T; };
        template <is_iterable T>
        struct deref<T> {
            using type = std::conditional_t<
                pybind11_like<T>, Object, dereference_type<T>
            >;
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
    struct Broadcast<Condition, std::pair<T1, T2>, std::pair<T3, T4>> {
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
    struct Broadcast<Condition, L, std::pair<T1, T2>> {
        static constexpr bool value =
            Broadcast<Condition, L, T1>::value && Broadcast<Condition, L, T2>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename T1,
        typename T2,
        typename R
    >
    struct Broadcast<Condition, std::pair<T1, T2>, R> {
        static constexpr bool value =
            Broadcast<Condition, T1, R>::value && Broadcast<Condition, T2, R>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename... Ts1,
        typename... Ts2
    >
    struct Broadcast<Condition, std::tuple<Ts1...>, std::tuple<Ts2...>> {
        static constexpr bool value =
            (Broadcast<Condition, Ts1, std::tuple<Ts2...>>::value && ...);
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename... Ts
    >
    struct Broadcast<Condition, L, std::tuple<Ts...>> {
        static constexpr bool value =
            (Broadcast<Condition, L, Ts>::value && ...);
    };

    template <
        template <typename, typename> typename Condition,
        typename... Ts,
        typename R
    >
    struct Broadcast<Condition, std::tuple<Ts...>, R> {
        static constexpr bool value =
            (Broadcast<Condition, Ts, R>::value && ...);
    };

}


/////////////////////////
////    FUNCTIONS    ////
/////////////////////////


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj);
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj);


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_DECLARATIONS_H
