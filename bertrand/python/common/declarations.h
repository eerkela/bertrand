#ifndef BERTRAND_PYTHON_COMMON_DECLARATIONS_H
#define BERTRAND_PYTHON_COMMON_DECLARATIONS_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <chrono>
#include <complex>
#include <deque>
#include <fstream>
#include <functional>
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
#include <frameobject.h>
#include <internal/pycore_frame.h>  // required to assign to frame->f_lineno

#include <cpptrace/cpptrace.hpp>

#include <bertrand/common.h>
#include <bertrand/static_str.h>


namespace py {
using bertrand::StaticStr;


namespace impl {
    struct BertrandTag {};
    struct ArgTag : public BertrandTag {};
    struct ProxyTag : public BertrandTag {};
}


/* A static RAII guard that initializes the Python interpreter the first time a Python
object is created and finalizes it when the program exits. */
struct Interpreter : public impl::BertrandTag {

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


class Handle;
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
}


/* Base class for enabled control structures.  Encodes the return type as a template
parameter. */
template <typename T>
struct Returns : public impl::BertrandTag {
    static constexpr bool enable = true;
    using type = T;
};


/* Base class for disabled control structures. */
struct Disable : public impl::BertrandTag {
    static constexpr bool enable = false;
};


template <typename T>
struct __as_object__                                        : Disable {};
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


namespace impl {

    /* Trigger implicit conversion operators and/or implicit constructors, but not
    explicit ones.  In contrast, static_cast<>() will trigger explicit constructors on
    the target type, which can give unexpected results and violate type safety. */
    template <typename U>
    decltype(auto) implicit_cast(U&& value) {
        return std::forward<U>(value);
    }

    /* A convenience class that stores a static Python string for use during attr
    lookups.  Using this class ensures that only one string is allocated per attribute
    name, even if that name is repeated across multiple contexts. */
    template <StaticStr name>
    struct TemplateString : public BertrandTag {
        inline static PyObject* ptr = (Interpreter::init(), PyUnicode_FromStringAndSize(
            name,
            name.size()
        ));  // NOTE: string will be garbage collected at shutdown
    };

    template <typename Obj, typename Key> requires (__getitem__<Obj, Key>::enable)
    class Item;
    template <typename Policy>
    class Iterator;
    template <typename Policy>
    class ReverseIterator;
    template <typename Deref>
    class GenericIter;

    struct SliceInitializer;

    template <typename T>
    using iter_type = decltype(*std::ranges::begin(std::declval<T>()));

    template <typename T>
    using reverse_iter_type = decltype(*std::ranges::rbegin(std::declval<T>()));

    template <typename T, typename Key>
    using lookup_type = decltype(std::declval<T>()[std::declval<Key>()]);

    template <typename From, typename To>
    concept has_conversion_operator = requires(From&& from) {
        from.operator To();
    };

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(From&& from) {
        static_cast<To>(std::forward<From>(from));
    };

    template <typename T>
    concept is_iterable = requires(T&& t) {
        { std::ranges::begin(std::forward<T>(t)) } -> std::input_or_output_iterator;
        { std::ranges::end(std::forward<T>(t)) } -> std::input_or_output_iterator;
    };

    template <typename T, typename Value>
    concept yields = is_iterable<T> && std::convertible_to<iter_type<T>, Value>;

    template <typename T>
    concept is_reverse_iterable = requires(T&& t) {
        { std::ranges::rbegin(std::forward<T>(t)) } -> std::input_or_output_iterator;
        { std::ranges::rend(std::forward<T>(t)) } -> std::input_or_output_iterator;
    };

    template <typename T, typename Value>
    concept yields_reverse =
        is_reverse_iterable<T> && std::convertible_to<reverse_iter_type<T>, Value>;

    template <typename T, typename U>
    concept has_static_begin = requires(T&& t, U&& u) {
        { std::forward<T>(t).begin(std::forward<U>(u)) } -> std::input_or_output_iterator;
    };

    template <typename T, typename U>
    concept has_static_end = requires(T&& t, U&& u) {
        { std::forward<T>(t).end(std::forward<U>(u)) } -> std::input_or_output_iterator;
    };

    template <typename T, typename U>
    concept has_static_rbegin = requires(T&& t, U&& u) {
        { std::forward<T>(t).rbegin(std::forward<U>(u)) } -> std::input_or_output_iterator;
    };

    template <typename T, typename U>
    concept has_static_rend = requires(T&& t, U&& u) {
        { std::forward<T>(t).rend(std::forward<U>(u)) } -> std::input_or_output_iterator;
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

    template <typename T, typename Key>
    concept supports_lookup = !std::is_pointer_v<T> && !std::integral<std::decay_t<T>> &&
        requires(T&& t, Key&& key) {
            { std::forward<T>(t)[std::forward<Key>(key)] };
        };

    template <typename T, typename Key, typename Value>
    concept lookup_yields = supports_lookup<T, Key> && requires(T&& t, Key&& key) {
        { std::forward<T>(t)[std::forward<Key>(key)] } -> std::convertible_to<Value>;
    };

    template <typename T>
    concept pair_like = std::tuple_size<T>::value == 2 && requires(T&& t) {
        { std::get<0>(std::forward<T>(t)) };
        { std::get<1>(std::forward<T>(t)) };
    };

    template <typename T, typename First, typename Second>
    concept pair_like_with = pair_like<T> && requires(T&& t) {
        { std::get<0>(std::forward<T>(t)) } -> std::convertible_to<First>;
        { std::get<1>(std::forward<T>(t)) } -> std::convertible_to<Second>;
    };

    template <typename T>
    concept yields_pairs = is_iterable<T> && pair_like<iter_type<T>>;

    template <typename T, typename First, typename Second>
    concept yields_pairs_with = is_iterable<T> && pair_like_with<iter_type<T>, First, Second>;

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

    template <typename T>
    concept hashable = requires(T&& t) {
        { std::hash<std::decay_t<T>>{}(std::forward<T>(t)) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept string_literal = requires(T&& t) {
        { []<size_t N>(const char(&)[N]){}(std::forward<T>(t)) };
    };

    template <typename T>
    concept complex_like = requires(T&& t) {
        { std::forward<T>(t).real() } -> std::convertible_to<double>;
        { std::forward<T>(t).imag() } -> std::convertible_to<double>;
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

    template <typename T>
    concept has_keys = requires(T&& t) {
        { std::forward<T>(t).keys() } -> is_iterable;
        { std::forward<T>(t).keys() } -> yields<typename std::decay_t<T>::key_type>;
    };

    template <typename T>
    concept has_values = requires(T&& t) {
        { std::forward<T>(t).values() } -> is_iterable;
        { std::forward<T>(t).values() } -> yields<typename std::decay_t<T>::mapped_type>;
    };

    template <typename T>
    concept has_items = requires(T&& t) {
        { std::forward<T>(t).items() } -> is_iterable;
        { std::forward<T>(t).items() } -> yields_pairs_with<
            typename std::decay_t<T>::key_type,
            typename std::decay_t<T>::mapped_type
        >;
    };

    template <typename T>
    concept bertrand_like = std::derived_from<std::decay_t<T>, BertrandTag>;

    template <typename T>
    concept python_like = std::derived_from<std::decay_t<T>, Object>;

    template <typename... Ts>
    concept any_are_python_like = (python_like<Ts> || ...);

    template <typename T>
    concept is_object_exact = std::same_as<std::decay_t<T>, Object>;

    template <typename T>
    concept proxy_like = std::derived_from<std::decay_t<T>, ProxyTag>;

    template <typename T>
    concept not_proxy_like = !proxy_like<T>;

    template <typename T>
    concept cpp_like = !python_like<T>;

    template <typename T>
    concept none_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, NoneType>;

    template <typename T>
    concept notimplemented_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, NotImplementedType>;

    template <typename T>
    concept ellipsis_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, EllipsisType>;

    template <typename T>
    concept slice_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Slice>;

    template <typename T>
    concept module_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Module>;

    template <typename T>
    concept bool_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Bool>;

    template <typename T>
    concept int_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Int>;

    template <typename T>
    concept float_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Float>;

    template <typename T>
    concept str_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Str>;

    template <typename T>
    concept bytes_like = (
        string_literal<T> ||
        std::same_as<std::decay_t<T>, void*> || (
            __as_object__<std::remove_cvref_t<T>>::enable &&
            std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Bytes>
        )
    );

    template <typename T>
    concept bytearray_like = (
        string_literal<T> ||
        std::same_as<std::decay_t<T>, void*> || (
            __as_object__<std::remove_cvref_t<T>>::enable &&
            std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, ByteArray>
        )
    );

    template <typename T>
    concept anybytes_like = bytes_like<T> || bytearray_like<T>;

    template <typename T>
    concept timedelta_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Timedelta>;

    template <typename T>
    concept timezone_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Timezone>;

    template <typename T>
    concept date_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Date>;

    template <typename T>
    concept time_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Time>;

    template <typename T>
    concept datetime_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Datetime>;

    template <typename T>
    concept range_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Range>;

    template <typename T>
    concept tuple_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, TupleTag>;

    template <typename T>
    concept list_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, ListTag>;

    template <typename T>
    concept set_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, SetTag>;

    template <typename T>
    concept frozenset_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, FrozenSetTag>;

    template <typename T>
    concept anyset_like = set_like<T> || frozenset_like<T>;

    template <typename T>
    concept dict_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, DictTag>;

    template <typename T>
    concept mappingproxy_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, MappingProxyTag>;

    template <typename T>
    concept anydict_like = dict_like<T> || mappingproxy_like<T>;

    template <typename T>
    concept type_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, Type>;

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

    template <
        template <typename, typename> typename Condition,
        typename L,
        typename R
    >
    struct Broadcast : public BertrandTag {
        template <typename T>
        struct deref { using type = T; };
        template <is_iterable T>
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

    template <typename T>
    struct unwrap_proxy_helper : public BertrandTag { using type = T; };
    template <proxy_like T>
    struct unwrap_proxy_helper<T> : public BertrandTag { using type = typename T::type; };
    template <typename T>
    using unwrap_proxy = typename unwrap_proxy_helper<T>::type;

}


// TODO: Arg and its non-variadic subclasses have to use aggregate initialization to
// extend the lifetime of the temporary to match the lifetime of the Arg object.  This
// requires modifying Function to use aggregate initialization when invoking the inner
// function, so that the outside behavior is unaffected.
// -> Args can't use constructors, since those violate lifetime rules.


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
    };

    struct Args : public impl::ArgTag {
        using type = std::conditional_t<
            std::is_rvalue_reference_v<T>,
            std::remove_reference_t<T>,
            std::conditional_t<
                std::is_lvalue_reference_v<T>,
                std::reference_wrapper<T>,
                T
            >
        >;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        std::vector<type> value;

        Args() = default;
        Args(const std::vector<type>& val) : value(val) {}
        Args(std::vector<type>&& val) : value(std::move(val)) {}
        template <std::convertible_to<T> V>
        Args(const std::vector<V>& val) {
            value.reserve(val.size());
            for (const auto& item : val) {
                value.push_back(item);
            }
        }
        Args(const Args& other) : value(other.value) {}
        Args(Args&& other) : value(std::move(other.value)) {}

        [[nodiscard]] auto begin() const { return value.begin(); }
        [[nodiscard]] auto cbegin() const { return value.cbegin(); }
        [[nodiscard]] auto end() const { return value.end(); }
        [[nodiscard]] auto cend() const { return value.cend(); }
        [[nodiscard]] auto rbegin() const { return value.rbegin(); }
        [[nodiscard]] auto crbegin() const { return value.crbegin(); }
        [[nodiscard]] auto rend() const { return value.rend(); }
        [[nodiscard]] auto crend() const { return value.crend(); }
        [[nodiscard]] constexpr auto size() const { return value.size(); }
        [[nodiscard]] constexpr auto empty() const { return value.empty(); }
        [[nodiscard]] constexpr auto data() const { return value.data(); }
        [[nodiscard]] constexpr decltype(auto) front() const { return value.front(); }
        [[nodiscard]] constexpr decltype(auto) back() const { return value.back(); }
        [[nodiscard]] constexpr decltype(auto) operator[](size_t index) const {
            return value.at(index);
        }
    };

    struct Kwargs : public impl::ArgTag {
        using type = std::conditional_t<
            std::is_rvalue_reference_v<T>,
            std::remove_reference_t<T>,
            std::conditional_t<
                std::is_lvalue_reference_v<T>,
                std::reference_wrapper<T>,
                T
            >
        >;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        std::unordered_map<std::string, T> value;

        Kwargs() = default;
        Kwargs(const std::unordered_map<std::string, type>& val) : value(val) {}
        Kwargs(std::unordered_map<std::string, type>&& val) : value(std::move(val)) {}
        template <std::convertible_to<T> V>
        Kwargs(const std::unordered_map<std::string, V>& val) {
            value.reserve(val.size());
            for (const auto& [k, v] : val) {
                value.emplace(k, v);
            }
        }
        Kwargs(const Kwargs& other) : value(other.value) {}
        Kwargs(Kwargs&& other) : value(std::move(other.value)) {}

        [[nodiscard]] auto begin() const { return value.begin(); }
        [[nodiscard]] auto cbegin() const { return value.cbegin(); }
        [[nodiscard]] auto end() const { return value.end(); }
        [[nodiscard]] auto cend() const { return value.cend(); }
        [[nodiscard]] constexpr auto size() const { return value.size(); }
        [[nodiscard]] constexpr bool empty() const { return value.empty(); }
        [[nodiscard]] constexpr bool contains(const std::string& key) const {
            return value.contains(key);
        }
        [[nodiscard]] constexpr auto count(const std::string& key) const {
            return value.count(key);
        }
        [[nodiscard]] decltype(auto) find(const std::string& key) const {
            return value.find(key);
        }
        [[nodiscard]] decltype(auto) operator[](const std::string& key) const {
            return value.at(key);
        }
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
};


namespace impl {

    template <StaticStr name>
    struct UnboundArg {
        template <typename T>
        constexpr auto operator=(T&& value) const {
            return Arg<name, T>{std::forward<T>(value)};
        }
    };

}


/* A compile-time factory for binding keyword arguments with Python syntax.  A
constexpr instance of this class can be used to provide even more Pythonic syntax:

    constexpr auto x = py::arg<"x">;
    my_func(x = 42);
*/
template <StaticStr name>
constexpr impl::UnboundArg<name> arg {};


/////////////////////////
////    FUNCTIONS    ////
/////////////////////////


/* Get the underlying pointer from a Python object. */
[[nodiscard]] PyObject* ptr(Handle obj);


/* Cause a Python object to release ownership over its underlying pointer. */
[[nodiscard]] PyObject* release(Handle obj);


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj);


/* Steal a reference to a raw Python handle. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj);


}  // namespace py


#endif
