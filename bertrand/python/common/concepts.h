#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_CONCEPTS_H
#define BERTRAND_PYTHON_COMMON_CONCEPTS_H

#include "declarations.h"


namespace bertrand {
namespace py {
namespace impl {

    /* Helper function triggers implicit conversion operators and/or implicit
    constructors, but not explicit ones.  In contrast, static_cast<>() will trigger
    explicit constructors on the target type, which can give unexpected results and
    violate strict type safety. */
    template <typename U>
    static decltype(auto) implicit_cast(U&& value) {
        return std::forward<U>(value);
    }

    template <typename From, typename To>
    concept has_conversion_operator = requires(const From& from) {
        from.operator To();
    };

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(const From& from) {
        static_cast<To>(from);
    };

    template <typename T>
    constexpr bool is_initializer_list = false;
    template <typename T>
    constexpr bool is_initializer_list<std::initializer_list<T>> = true;
    template <typename T>
    concept initializer_like = is_initializer_list<std::remove_cvref_t<T>>;

    template <typename T>
    concept is_iterable = requires(T t) {
        { std::begin(t) } -> std::input_or_output_iterator;
        { std::end(t) } -> std::input_or_output_iterator;
    };

    template <typename T>
    using dereference_type = decltype(*std::begin(std::declval<T>()));

    template <typename T>
    concept is_reverse_iterable = requires(const T& t) {
        { std::rbegin(t) } -> std::input_or_output_iterator;
        { std::rend(t) } -> std::input_or_output_iterator;
    };

    template <typename T>
    using reverse_dereference_type = decltype(*std::rbegin(std::declval<T>()));

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

    template <typename T, typename Key>
    using lookup_type = decltype(std::declval<T>()[std::declval<Key>()]);

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
    using first = T;

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

}  // namespace impl


/* Base class for enabled control structures.  Encodes the return type as a template
parameter. */
template <typename T>
struct Returns {
    static constexpr bool enable = true;
    using Return = T;
};


/* Control structure maps C++ types to their Bertrand equivalents, for use in CTAD and
implicit conversions.  This is enabled by default, falling back to Object if no
specialization could be found. */
template <typename T>
struct __as_object__ : Returns<Object> {};


template <typename T>
using as_object_t = __as_object__<std::remove_cvref_t<T>>::Return;


// TODO: use as_object in operator overloads/function signature conversions.


/* Convert an arbitrary C++ value to an equivalent Python object if it isn't one
already. */
template <typename T> requires (__as_object__<std::remove_cvref_t<T>>::enable)
auto as_object(T&& value) -> __as_object__<std::remove_cvref_t<T>>::Return {
    return std::forward<T>(value);
}


template <std::derived_from<Object> T>
struct __as_object__<T> : Returns<T> {};

// template <typename R, typename... A>
// struct __as_object__<R(A...)> : Returns<Function<R(A...)>> {};
// template <typename R, typename... A>
// struct __as_object__<R(*)(A...)> : Returns<Function<R(A...)>> {};
// template <typename R, typename C, typename... A>
// struct __as_object__<R(C::*)(A...)> : Returns<Function<R(A...)>> {};
// template <typename R, typename C, typename... A>
// struct __as_object__<R(C::*)(A...) noexcept> : Returns<Function<R(A...)>> {};
// template <typename R, typename C, typename... A>
// struct __as_object__<R(C::*)(A...) const> : Returns<Function<R(A...)>> {};
// template <typename R, typename C, typename... A>
// struct __as_object__<R(C::*)(A...) const noexcept> : Returns<Function<R(A...)>> {};
// template <typename R, typename C, typename... A>
// struct __as_object__<R(C::*)(A...) volatile> : Returns<Function<R(A...)>> {};
// template <typename R, typename C, typename... A>
// struct __as_object__<R(C::*)(A...) volatile noexcept> : Returns<Function<R(A...)>> {};
// template <typename R, typename C, typename... A>
// struct __as_object__<R(C::*)(A...) const volatile> : Returns<Function<R(A...)>> {};
// template <typename R, typename C, typename... A>
// struct __as_object__<R(C::*)(A...) const volatile noexcept> : Returns<Function<R(A...)>> {};
// template <typename R, typename... A>
// struct __as_object__<std::function<R(A...)>> : Returns<Function<R(A...)>> {};
// template <std::derived_from<pybind11::function> T>
// struct __as_object__<T> : Returns<Function<Object(Args<Object>, Kwargs<Object>)>> {};

template <>
struct __as_object__<std::nullptr_t> : Returns<NoneType> {};
template <>
struct __as_object__<std::nullopt_t> : Returns<NoneType> {};
template <std::derived_from<pybind11::none> T>
struct __as_object__<T> : Returns<NoneType> {};

template <std::derived_from<pybind11::ellipsis> T>
struct __as_object__<T> : Returns<EllipsisType> {};

template <std::derived_from<pybind11::slice> T>
struct __as_object__<T> : Returns<Slice> {};

template <std::derived_from<pybind11::module_> T>
struct __as_object__<T> : Returns<Module> {};

template <>
struct __as_object__<bool> : Returns<Bool> {};
template <std::derived_from<pybind11::bool_> T>
struct __as_object__<T> : Returns<Bool> {};

template <std::integral T> requires (!std::same_as<bool, T>)
struct __as_object__<T> : Returns<Int> {};
template <std::derived_from<pybind11::int_> T>
struct __as_object__<T> : Returns<Int> {};

template <std::floating_point T>
struct __as_object__<T> : Returns<Float> {};
template <std::derived_from<pybind11::float_> T>
struct __as_object__<T> : Returns<Float> {};

template <impl::complex_like T> requires (!std::derived_from<T, Object>)
struct __as_object__<T> : Returns<Complex> {};

template <>
struct __as_object__<const char*> : Returns<Str> {};
template <size_t N>
struct __as_object__<const char(&)[N]> : Returns<Str> {};
template <std::derived_from<std::string> T>
struct __as_object__<T> : Returns<Str> {};
template <std::derived_from<std::string_view> T>
struct __as_object__<T> : Returns<Str> {};
template <std::derived_from<pybind11::str> T>
struct __as_object__<T> : Returns<Str> {};

template <>
struct __as_object__<void*> : Returns<Bytes> {};
template <std::derived_from<pybind11::bytes> T>
struct __as_object__<T> : Returns<Bytes> {};

template <std::derived_from<pybind11::bytearray> T>
struct __as_object__<T> : Returns<ByteArray> {};

template <typename... Args>
struct __as_object__<std::chrono::duration<Args...>> : Returns<Timedelta> {};

// TODO: std::time_t?

template <typename... Args>
struct __as_object__<std::chrono::time_point<Args...>> : Returns<Datetime> {};

template <typename First, typename Second>
struct __as_object__<std::pair<First, Second>> : Returns<Tuple<Object>> {};  // TODO: should return Struct?
template <typename... Args>
struct __as_object__<std::tuple<Args...>> : Returns<Tuple<Object>> {};  // TODO: should return Struct?
template <typename T, size_t N>
struct __as_object__<std::array<T, N>> : Returns<Tuple<as_object_t<T>>> {};
template <std::derived_from<pybind11::tuple> T>
struct __as_object__<T> : Returns<Tuple<Object>> {};

template <typename T, typename... Args>
struct __as_object__<std::vector<T, Args...>> : Returns<List<as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::deque<T, Args...>> : Returns<List<as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::list<T, Args...>> : Returns<List<as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::forward_list<T, Args...>> : Returns<List<as_object_t<T>>> {};
template <std::derived_from<pybind11::list> T>
struct __as_object__<T> : Returns<List<Object>> {};

template <typename T, typename... Args>
struct __as_object__<std::unordered_set<T, Args...>> : Returns<Set<as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::set<T, Args...>> : Returns<Set<as_object_t<T>>> {};
template <std::derived_from<pybind11::set> T>
struct __as_object__<T> : Returns<Set<Object>> {};

template <std::derived_from<pybind11::frozenset> T>
struct __as_object__<T> : Returns<FrozenSet<Object>> {};

template <typename K, typename V, typename... Args>
struct __as_object__<std::unordered_map<K, V, Args...>> : Returns<Dict<as_object_t<K>, as_object_t<V>>> {};
template <typename K, typename V, typename... Args>
struct __as_object__<std::map<K, V, Args...>> : Returns<Dict<as_object_t<K>, as_object_t<V>>> {};
template <std::derived_from<pybind11::dict> T>
struct __as_object__<T> : Returns<Dict<Object, Object>> {};

template <std::derived_from<pybind11::type> T>
struct __as_object__<T> : Returns<Type> {};


namespace impl {

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

}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_CONCEPTS_H
