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

#include <bertrand/common.h>
#include <bertrand/static_str.h>


namespace py {
using bertrand::StaticStr;


namespace impl {
    struct BertrandTag {};
    struct TypeTag;
    struct IterTag : public BertrandTag {};
    struct ArgTag : public BertrandTag {};
    struct FunctionTag : public BertrandTag {};
    struct ModuleTag;
    struct TupleTag : public BertrandTag {};
    struct ListTag : public BertrandTag{};
    struct SetTag : public BertrandTag {};
    struct FrozenSetTag : public BertrandTag {};
    struct KeyTag : public BertrandTag {};
    struct ValueTag : public BertrandTag {};
    struct ItemTag : public BertrandTag {};
    struct DictTag : public BertrandTag {};
    struct MappingProxyTag : public BertrandTag {};

    static std::string demangle(const char* name) {
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

    enum class Origin {
        PYTHON,
        CPP
    };

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
class Type;
class BertrandMeta;
template <std::derived_from<Object> Return>
class Iterator;
template <StaticStr Name, typename T>
class Arg;
template <typename>
class Function;
template <StaticStr Name>
class Module;
class NoneType;
class NotImplementedType;
class EllipsisType;
class Slice;
class Code;
class Frame;
class Bool;
class Int;
class Float;
class Complex;
class Str;
class Bytes;
class ByteArray;
class Date;
class Time;
class Datetime;
class Timedelta;
class Timezone;
class Range;
template <std::derived_from<Object> Val = Object>
class List;
template <std::derived_from<Object> Val = Object>
class Tuple;
template <std::derived_from<Object> Key = Object>
class Set;
template <std::derived_from<Object> Key = Object>
class FrozenSet;
template <std::derived_from<Object> Key = Object, std::derived_from<Object> Val = Object>
class Dict;
template <std::derived_from<Object> Map>
class KeyView;
template <std::derived_from<Object> Map>
class ValueView;
template <std::derived_from<Object> Map>
class ItemView;
template <std::derived_from<Object> Map>
class MappingProxy;


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

template <std::derived_from<Object> T>
struct __as_object__<T>                                     : Returns<T> {};


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
    struct SliceInitializer;

    template <typename T>
    using iter_type = decltype(*std::ranges::begin(std::declval<T>()));

    template <typename T>
    using reverse_iter_type = decltype(*std::ranges::rbegin(std::declval<T>()));

    template <typename T, typename Key>
    using lookup_type = decltype(std::declval<T>()[std::declval<Key>()]);

    template <typename T>
    constexpr bool is_generic_helper = false;
    template <template <typename...> typename T, typename... Ts>
    constexpr bool is_generic_helper<T<Ts...>> = true;

    template <typename T>
    concept is_generic = is_generic_helper<T>;

    template <typename T, typename = void>
    constexpr bool is_module_helper = false;
    template <typename T>
    constexpr bool is_module_helper<T, std::void_t<typename T::__module__>> = true;

    // TODO: is_module should just check for ModuleTag as a base, since all modules now
    // must inherit from it to gain access to __module__ in the first place.

    template <typename T>
    concept is_module =
        is_module_helper<T> && std::derived_from<typename T::__module__, ModuleTag>;

    template <typename T, StaticStr Name, typename... Args>
    concept attr_is_callable_with =
        __getattr__<T, Name>::enable &&
        __getattr__<T, Name>::type::template invocable<Args...>;

    template <typename From, typename To>
    concept has_conversion_operator = requires(From&& from) {
        from.operator To();
    };

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(From from) {
        static_cast<To>(from);
    };

    template <typename T>
    concept is_iterable = requires(T t) {
        { std::ranges::begin(t) } -> std::input_or_output_iterator;
        { std::ranges::end(t) } -> std::input_or_output_iterator;
    };

    template <typename T, typename Value>
    concept yields = is_iterable<T> && std::convertible_to<iter_type<T>, Value>;

    template <typename T>
    concept is_reverse_iterable = requires(T t) {
        { std::ranges::rbegin(t) } -> std::input_or_output_iterator;
        { std::ranges::rend(t) } -> std::input_or_output_iterator;
    };

    template <typename T, typename Value>
    concept yields_reverse =
        is_reverse_iterable<T> && std::convertible_to<reverse_iter_type<T>, Value>;

    template <typename T>
    concept iterator_like = requires(T begin, T end) {
        { *begin } -> std::convertible_to<typename std::decay_t<T>::value_type>;
        { ++begin } -> std::same_as<std::remove_reference_t<T>&>;
        { begin++ } -> std::same_as<std::remove_reference_t<T>>;
        { begin == end } -> std::convertible_to<bool>;
        { begin != end } -> std::convertible_to<bool>;
    };

    template <typename T>
    static constexpr bool is_optional_helper = false;
    template <typename T>
    static constexpr bool is_optional_helper<std::optional<T>> = true;
    template <typename T>
    static constexpr bool is_optional = is_optional_helper<std::decay_t<T>>;

    template <typename T>
    concept has_size = requires(T t) {
        { std::size(t) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept sequence_like = is_iterable<T> && has_size<T> && requires(T t) {
        { t[0] } -> std::convertible_to<iter_type<T>>;
    };

    template <typename T>
    concept mapping_like = requires(T t) {
        typename std::decay_t<T>::key_type;
        typename std::decay_t<T>::mapped_type;
        { t[std::declval<typename std::decay_t<T>::key_type>()] } ->
            std::convertible_to<typename std::decay_t<T>::mapped_type>;
    };

    template <typename T, typename Key>
    concept supports_lookup = !std::is_pointer_v<T> && !std::integral<std::decay_t<T>> &&
        requires(T t, Key key) {
            { t[key] };
        };

    template <typename T, typename Key, typename Value>
    concept lookup_yields = supports_lookup<T, Key> && requires(T t, Key key) {
        { t[key] } -> std::convertible_to<Value>;
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
    concept yields_pairs = is_iterable<T> && pair_like<iter_type<T>>;

    template <typename T, typename First, typename Second>
    concept yields_pairs_with = is_iterable<T> && pair_like_with<iter_type<T>, First, Second>;

    template <typename T>
    concept hashable = requires(T t) {
        { std::hash<std::decay_t<T>>{}(t) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_abs = requires(T t) {
        { std::abs(t) };
    };

    template <typename T, typename Return>
    concept abs_returns = requires(T t) {
        { std::abs(t) } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_invert = requires(T t) {
        { ~t };
    };

    template <typename T, typename Return>
    concept invert_returns = requires(T t) {
        { ~t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_pos = requires(T t) {
        { +t };
    };

    template <typename T, typename Return>
    concept pos_returns = requires(T t) {
        { +t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_neg = requires(T t) {
        { -t };
    };

    template <typename T, typename Return>
    concept neg_returns = requires(T t) {
        { -t } -> std::convertible_to<Return>;
    };

    template <typename T>
    concept has_preincrement = requires(T t) {
        { ++t } -> std::convertible_to<T&>;
    };

    template <typename T>
    concept has_postincrement = requires(T t) {
        { t++ } -> std::convertible_to<T>;
    };

    template <typename T>
    concept has_predecrement = requires(T t) {
        { --t } -> std::convertible_to<T&>;
    };

    template <typename T>
    concept has_postdecrement = requires(T t) {
        { t-- } -> std::convertible_to<T>;
    };

    template <typename L, typename R>
    concept has_lt = requires(L l, R r) {
        { l < r };
    };

    template <typename L, typename R, typename Return>
    concept lt_returns = requires(L l, R r) {
        { l < r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_le = requires(L l, R r) {
        { l <= r };
    };

    template <typename L, typename R, typename Return>
    concept le_returns = requires(L l, R r) {
        { l <= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_eq = requires(L l, R r) {
        { l == r };
    };

    template <typename L, typename R, typename Return>
    concept eq_returns = requires(L l, R r) {
        { l == r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ne = requires(L l, R r) {
        { l != r };
    };

    template <typename L, typename R, typename Return>
    concept ne_returns = requires(L l, R r) {
        { l != r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ge = requires(L l, R r) {
        { l >= r };
    };

    template <typename L, typename R, typename Return>
    concept ge_returns = requires(L l, R r) {
        { l >= r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_gt = requires(L l, R r) {
        { l > r };
    };

    template <typename L, typename R, typename Return>
    concept gt_returns = requires(L l, R r) {
        { l > r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_add = requires(L l, R r) {
        { l + r };
    };

    template <typename L, typename R, typename Return>
    concept add_returns = requires(L l, R r) {
        { l + r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_iadd = requires(L& l, R r) {
        { l += r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_sub = requires(L l, R r) {
        { l - r };
    };

    template <typename L, typename R, typename Return>
    concept sub_returns = requires(L l, R r) {
        { l - r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_isub = requires(L& l, R r) {
        { l -= r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_mul = requires(L l, R r) {
        { l * r };
    };

    template <typename L, typename R, typename Return>
    concept mul_returns = requires(L l, R r) {
        { l * r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_imul = requires(L& l, R r) {
        { l *= r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_truediv = requires(L l, R r) {
        { l / r };
    };

    template <typename L, typename R, typename Return>
    concept truediv_returns = requires(L l, R r) {
        { l / r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_itruediv = requires(L& l, R r) {
        { l /= r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_mod = requires(L l, R r) {
        { l % r };
    };

    template <typename L, typename R, typename Return>
    concept mod_returns = requires(L l, R r) {
        { l % r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_imod = requires(L& l, R r) {
        { l %= r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_pow = requires(L l, R r) {
        { std::pow(l, r) };
    };

    template <typename L, typename R, typename Return>
    concept pow_returns = requires(L l, R r) {
        { std::pow(l, r) } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_lshift = requires(L l, R r) {
        { l << r };
    };

    template <typename L, typename R, typename Return>
    concept lshift_returns = requires(L l, R r) {
        { l << r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ilshift = requires(L& l, R r) {
        { l <<= r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_rshift = requires(L l, R r) {
        { l >> r };
    };

    template <typename L, typename R, typename Return>
    concept rhsift_returns = requires(L l, R r) {
        { l >> r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_irshift = requires(L& l, R r) {
        { l >>= r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_and = requires(L l, R r) {
        { l & r };
    };

    template <typename L, typename R, typename Return>
    concept and_returns = requires(L l, R r) {
        { l & r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_iand = requires(L& l, R r) {
        { l &= r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_or = requires(L l, R r) {
        { l | r };
    };

    template <typename L, typename R, typename Return>
    concept or_returns = requires(L l, R r) {
        { l | r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ior = requires(L& l, R r) {
        { l |= r } -> std::convertible_to<L&>;
    };

    template <typename L, typename R>
    concept has_xor = requires(L l, R r) {
        { l ^ r };
    };

    template <typename L, typename R, typename Return>
    concept xor_returns = requires(L l, R r) {
        { l ^ r } -> std::convertible_to<Return>;
    };

    template <typename L, typename R>
    concept has_ixor = requires(L& l, R r) {
        { l ^= r } -> std::convertible_to<L&>;
    };

    template <typename T>
    concept has_concat = requires(const T& lhs, const T& rhs) {
        { lhs + rhs } -> std::convertible_to<T>;
    };

    template <typename T>
    concept has_inplace_concat = requires(T& lhs, const T& rhs) {
        { lhs += rhs } -> std::convertible_to<T&>;
    };

    template <typename T>
    concept has_repeat = requires(const T& lhs, size_t rhs) {
        { lhs * rhs } -> std::convertible_to<T>;
    };

    template <typename T>
    concept has_inplace_repeat = requires(T& lhs, size_t rhs) {
        { lhs *= rhs } -> std::convertible_to<T&>;
    };

    template <typename T>
    concept has_to_string = requires(T t) {
        { std::to_string(t) } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_stream_insertion = requires(std::ostream& os, T t) {
        { os << t } -> std::convertible_to<std::ostream&>;
    };

    template <typename T>
    concept has_call_operator = requires { &std::decay_t<T>::operator(); };

    template <typename T>
    concept is_callable_any = 
        std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> ||
        std::is_member_function_pointer_v<std::decay_t<T>> ||
        has_call_operator<T>;

    template <typename T>
    concept string_literal = requires(T t) {
        { []<size_t N>(const char(&)[N]){}(t) };
    };

    template <typename T>
    concept complex_like = requires(T t) {
        { t.real() } -> std::convertible_to<double>;
        { t.imag() } -> std::convertible_to<double>;
    };

    template <typename T>
    concept has_empty = requires(T t) {
        { t.empty() } -> std::convertible_to<bool>;
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
        { t.keys() } -> is_iterable;
        { t.keys() } -> yields<typename std::decay_t<T>::key_type>;
    };

    template <typename T>
    concept has_values = requires(T t) {
        { t.values() } -> is_iterable;
        { t.values() } -> yields<typename std::decay_t<T>::mapped_type>;
    };

    template <typename T>
    concept has_items = requires(T t) {
        { t.items() } -> is_iterable;
        { t.items() } -> yields_pairs_with<
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

    // TODO: is_object_exact may not be necessary

    template <typename T>
    concept is_object_exact = std::same_as<std::decay_t<T>, Object>;  // TODO: rename to dynamic_type and include Handle?

    template <typename T>
    concept originates_from_python =
        std::derived_from<std::remove_cvref_t<T>, Object> &&
        Type<std::remove_cvref_t<T>>::__python__::__origin__ == Origin::PYTHON;

    template <typename T>
    concept cpp_like = !python_like<T>;

    template <typename T>
    concept originates_from_cpp =
        std::derived_from<std::remove_cvref_t<T>, Object> &&
        Type<std::remove_cvref_t<T>>::__python__::__origin__ == Origin::CPP;

    template <typename T>
    concept cpp_or_originates_from_cpp = cpp_like<T> || originates_from_cpp<T>;

    template <cpp_or_originates_from_cpp T>
    using cpp_type = std::conditional_t<
        originates_from_cpp<T>,
        typename Type<std::remove_cvref_t<T>>::__python__::t_cpp,
        T
    >;

    template <typename T>
    concept type_like =
        __as_object__<std::remove_cvref_t<T>>::enable &&
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, TypeTag>;

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
        std::derived_from<typename __as_object__<std::remove_cvref_t<T>>::type, ModuleTag>;

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
        static constexpr bool value = requires(L a, R b) {
            { a < b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct le_comparable : public BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a <= b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct eq_comparable : public BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a == b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ne_comparable : public BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a != b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct ge_comparable : public BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a >= b } -> std::convertible_to<bool>;
        };
    };

    template <typename L, typename R>
    struct gt_comparable : public BertrandTag {
        static constexpr bool value = requires(L a, R b) {
            { a > b } -> std::convertible_to<bool>;
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

}


/* Retrieve the pointer backing a Python object. */
[[nodiscard]] inline PyObject* ptr(Handle obj);


/* Cause a Python object to relinquish ownership over its backing pointer, and then
return the raw pointer. */
[[nodiscard]] inline PyObject* release(Handle obj);


/* Steal a reference to a raw Python handle. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj);


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj);


/* Wrap a non-owning, mutable reference to a C++ object into a `py::Object` proxy that
exposes it to Python.  Note that this only works if a corresponding `py::Object`
subclass exists, which was declared using the `__python__` CRTP helper, and whose C++
type exactly matches the argument.

WARNING: This function is unsafe and should be used with caution.  It is the caller's
responsibility to make sure that the underlying object outlives the wrapper, otherwise
undefined behavior will occur.  It is mostly intended for internal use in order to
expose shared state to Python, for instance to model exported global variables. */
template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::originates_from_cpp<typename __as_object__<T>::type> &&
        std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
    )
[[nodiscard]] auto wrap(T& obj) -> __as_object__<T>::type;


/* Wrap a non-owning, immutable reference to a C++ object into a `py::Object` proxy
that exposes it to Python.  Note that this only works if a corresponding `py::Object`
subclass exists, which was declared using the `__python__` CRTP helper, and whose C++
type exactly matches the argument.

WARNING: This function is unsafe and should be used with caution.  It is the caller's
responsibility to make sure that the underlying object outlives the wrapper, otherwise
undefined behavior will occur.  It is mostly intended for internal use in order to
expose shared state to Python, for instance to model exported global variables. */
template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::originates_from_cpp<typename __as_object__<T>::type> &&
        std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
    )
[[nodiscard]] auto wrap(const T& obj) -> __as_object__<T>::type;


/* Retrieve a reference to the internal C++ object that backs a `py::Object` wrapper.
Note that this only works if the wrapper was declared using the `__python__` CRTP
helper.  If the wrapper does not own the backing object, this method will follow the
pointer to resolve the reference. */
template <typename T> requires (impl::cpp_or_originates_from_cpp<T>)
[[nodiscard]] auto& unwrap(T& obj);


/* Retrieve a reference to the internal C++ object that backs a `py::Object` wrapper.
Note that this only works if the wrapper was declared using the `__python__` CRTP
helper.  If the wrapper does not own the backing object, this method will follow the
pointer to resolve the reference. */
template <typename T> requires (impl::cpp_or_originates_from_cpp<T>)
[[nodiscard]] const auto& unwrap(const T& obj);


}  // namespace py


#endif
