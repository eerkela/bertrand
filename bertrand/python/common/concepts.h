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

    template <typename T>
    concept sequence_like = requires(const T& t) {
        { std::begin(t) } -> std::input_or_output_iterator;
        { std::end(t) } -> std::input_or_output_iterator;
        { std::size(t) } -> std::convertible_to<size_t>;
        { t[0] };
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
    struct is_initializer_list : std::false_type {};
    template <typename T>
    struct is_initializer_list<std::initializer_list<T>> : std::true_type {};

    template <typename T>
    concept initializer_like = is_initializer_list<std::remove_cvref_t<T>>::value;

    template <typename T>
    concept complex_like = requires(const T& t) {
        { t.real() } -> std::convertible_to<double>;
        { t.imag() } -> std::convertible_to<double>;
    };

    template <typename T>
    concept string_literal = requires(const T& t) {
        { []<size_t N>(const char(&)[N]){}(t) } -> std::same_as<void>;
    };

    template <typename From, typename To>
    concept explicitly_convertible_to = requires(const From& from) {
        static_cast<To>(from);
    };

    template <typename From, typename To>
    concept has_conversion_operator = requires(const From& from) {
        from.operator To();
    };

    template <typename T>
    concept has_abs = requires(const T& t) {
        { std::abs(t) };
    };

    template <typename T>
    concept has_size = requires(const T& t) {
        { std::size(t) } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept has_empty = requires(const T& t) {
        { t.empty() } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept has_reserve = requires(T& t, size_t n) {
        { t.reserve(n) } -> std::same_as<void>;
    };

    template <typename T>
    concept pair_like = requires(const T& t) {
        { t.first } -> std::same_as<typename T::first_type>;
        { t.second } -> std::same_as<typename T::second_type>;
    };

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

    // NOTE: decay is necessary to treat `const char[N]` like `const char*`
    template <typename T>
    concept is_hashable = requires(T&& t) {
        { std::hash<std::decay_t<T>>{}(std::forward<T>(t)) } -> std::convertible_to<size_t>;
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
struct __to_python__ : Returns<Object> {};

template <typename T>
using to_python = __to_python__<std::remove_cvref_t<T>>::Return;

template <>
struct __to_python__<std::nullptr_t> : Returns<NoneType> {};
template <>
struct __to_python__<std::nullopt_t> : Returns<NoneType> {};
template <std::derived_from<NoneType> T>
struct __to_python__<T> : Returns<NoneType> {};
template <std::derived_from<pybind11::none> T>
struct __to_python__<T> : Returns<NoneType> {};

template <std::derived_from<Slice> T>
struct __to_python__<T> : Returns<Slice> {};
template <std::derived_from<pybind11::slice> T>
struct __to_python__<T> : Returns<Slice> {};

template <std::derived_from<Module> T>
struct __to_python__<T> : Returns<Module> {};
template <std::derived_from<pybind11::module_> T>
struct __to_python__<T> : Returns<Module> {};

template <>
struct __to_python__<bool> : Returns<Bool> {};
template <std::derived_from<Bool> T>
struct __to_python__<T> : Returns<Bool> {};
template <std::derived_from<pybind11::bool_> T>
struct __to_python__<T> : Returns<Bool> {};

template <std::integral T> requires (!std::same_as<bool, T>)
struct __to_python__<T> : Returns<Int> {};
template <std::derived_from<Int> T>
struct __to_python__<T> : Returns<Int> {};
template <std::derived_from<pybind11::int_> T>
struct __to_python__<T> : Returns<Int> {};

template <std::floating_point T>
struct __to_python__<T> : Returns<Float> {};
template <std::derived_from<Float> T>
struct __to_python__<T> : Returns<Float> {};
template <std::derived_from<pybind11::float_> T>
struct __to_python__<T> : Returns<Float> {};

template <impl::complex_like T>
struct __to_python__<T> : Returns<Complex> {};

template <>
struct __to_python__<const char*> : Returns<Str> {};
template <size_t N>
struct __to_python__<const char(&)[N]> : Returns<Str> {};
template <std::derived_from<std::string> T>
struct __to_python__<T> : Returns<Str> {};
template <std::derived_from<std::string_view> T>
struct __to_python__<T> : Returns<Str> {};
template <std::derived_from<Str> T>
struct __to_python__<T> : Returns<Str> {};
template <std::derived_from<pybind11::str> T>
struct __to_python__<T> : Returns<Str> {};

template <>
struct __to_python__<void*> : Returns<Bytes> {};
template <std::derived_from<Bytes> T>
struct __to_python__<T> : Returns<Bytes> {};
template <std::derived_from<pybind11::bytes> T>
struct __to_python__<T> : Returns<Bytes> {};

template <std::derived_from<ByteArray> T>
struct __to_python__<T> : Returns<ByteArray> {};
template <std::derived_from<pybind11::bytearray> T>
struct __to_python__<T> : Returns<ByteArray> {};

template <typename... Args>
struct __to_python__<std::chrono::duration<Args...>> : Returns<Timedelta> {};
template <std::derived_from<Timedelta> T>
struct __to_python__<T> : Returns<Timedelta> {};

template <std::derived_from<Timezone> T>
struct __to_python__<T> : Returns<Timezone> {};

template <std::derived_from<Date> T>
struct __to_python__<T> : Returns<Date> {};

// TODO: std::time_t?
template <std::derived_from<Time> T>
struct __to_python__<T> : Returns<Time> {};

template <typename... Args>
struct __to_python__<std::chrono::time_point<Args...>> : Returns<Datetime> {};
template <std::derived_from<Datetime> T>
struct __to_python__<T> : Returns<Datetime> {};

template <std::derived_from<Range> T>
struct __to_python__<T> : Returns<Range> {};

template <typename First, typename Second>
struct __to_python__<std::pair<First, Second>> : Returns<Tuple<Object>> {};  // TODO: should return Struct?
template <typename... Args>
struct __to_python__<std::tuple<Args...>> : Returns<Tuple<Object>> {};  // TODO: should return Struct?
template <typename T, size_t N>
struct __to_python__<std::array<T, N>> : Returns<Tuple<to_python<T>>> {};
template <std::derived_from<impl::TupleTag> T>
struct __to_python__<T> : Returns<Tuple<typename T::value_type>> {};
template <std::derived_from<pybind11::tuple> T>
struct __to_python__<T> : Returns<Tuple<Object>> {};

template <typename T, typename... Args>
struct __to_python__<std::vector<T, Args...>> : Returns<List<to_python<T>>> {};
template <typename T, typename... Args>
struct __to_python__<std::deque<T, Args...>> : Returns<List<to_python<T>>> {};
template <typename T, typename... Args>
struct __to_python__<std::list<T, Args...>> : Returns<List<to_python<T>>> {};
template <typename T, typename... Args>
struct __to_python__<std::forward_list<T, Args...>> : Returns<List<to_python<T>>> {};
template <std::derived_from<impl::ListTag> T>
struct __to_python__<T> : Returns<List<typename T::value_type>> {};
template <std::derived_from<pybind11::list> T>
struct __to_python__<T> : Returns<List<Object>> {};

template <typename T, typename... Args>
struct __to_python__<std::unordered_set<T, Args...>> : Returns<Set<to_python<T>>> {};
template <typename T, typename... Args>
struct __to_python__<std::set<T, Args...>> : Returns<Set<to_python<T>>> {};
template <std::derived_from<impl::SetTag> T>
struct __to_python__<T> : Returns<Set<typename T::value_type>> {};
template <std::derived_from<pybind11::set> T>
struct __to_python__<T> : Returns<Set<Object>> {};

template <std::derived_from<impl::FrozenSetTag> T>
struct __to_python__<T> : Returns<FrozenSet<typename T::value_type>> {};
template <std::derived_from<pybind11::frozenset> T>
struct __to_python__<T> : Returns<FrozenSet<Object>> {};

template <typename K, typename V, typename... Args>
struct __to_python__<std::unordered_map<K, V, Args...>> : Returns<Dict<to_python<K>, to_python<V>>> {};
template <typename K, typename V, typename... Args>
struct __to_python__<std::map<K, V, Args...>> : Returns<Dict<to_python<K>, to_python<V>>> {};
template <std::derived_from<impl::DictTag> T>
struct __to_python__<T> : Returns<Dict<typename T::key_type, typename T::mapped_type>> {};
template <std::derived_from<pybind11::dict> T>
struct __to_python__<T> : Returns<Dict<Object, Object>> {};

template <std::derived_from<impl::MappingProxyTag> T>
struct __to_python__<T> : Returns<MappingProxy<typename T::mapping_type>> {};

template <std::derived_from<Type> T>
struct __to_python__<T> : Returns<Type> {};
template <std::derived_from<pybind11::type> T>
struct __to_python__<T> : Returns<Type> {};






namespace impl {


    template <typename T>
    concept none_like = std::same_as<to_python<T>, NoneType>;

    template <typename T>
    concept slice_like = std::same_as<to_python<T>, Slice>;

    template <typename T>
    concept module_like = std::same_as<to_python<T>, Module>;

    template <typename T>
    concept bool_like = std::same_as<to_python<T>, Bool>;

    template <typename T>
    concept int_like = std::same_as<to_python<T>, Int>;

    template <typename T>
    concept float_like = std::same_as<to_python<T>, Float>;

    template <typename T>
    concept str_like = std::same_as<to_python<T>, Str>;

    template <typename T>
    concept bytes_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::same_as<to_python<T>, Bytes>
    );

    template <typename T>
    concept bytearray_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::same_as<std::remove_cvref_t<T>, void*> ||
        std::same_as<to_python<T>, ByteArray>
    );

    template <typename T>
    concept anybytes_like = bytes_like<T> || bytearray_like<T>;

    template <typename T>
    concept timedelta_like = std::same_as<to_python<T>, Timedelta>;

    template <typename T>
    concept timezone_like = std::same_as<to_python<T>, Timezone>;

    template <typename T>
    concept date_like = std::same_as<to_python<T>, Date>;

    template <typename T>
    concept time_like = std::same_as<to_python<T>, Time>;

    template <typename T>
    concept datetime_like = std::same_as<to_python<T>, Datetime>;

    template <typename T>
    concept range_like = std::same_as<to_python<T>, Range>;

    template <typename T>
    concept tuple_like = std::derived_from<to_python<T>, TupleTag>;

    template <typename T>
    concept list_like = std::derived_from<to_python<T>, ListTag>;

    template <typename T>
    concept set_like = std::derived_from<to_python<T>, SetTag>;

    template <typename T>
    concept frozenset_like = std::derived_from<to_python<T>, FrozenSetTag>;

    template <typename T>
    concept anyset_like = set_like<T> || frozenset_like<T>;

    template <typename T>
    concept dict_like = std::derived_from<to_python<T>, DictTag>;

    template <typename T>
    concept mappingproxy_like = std::derived_from<to_python<T>, MappingProxyTag>;

    template <typename T>
    concept anydict_like = dict_like<T> || mappingproxy_like<T>;

    template <typename T>
    concept type_like = std::same_as<to_python<T>, Type>;


}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_CONCEPTS_H
