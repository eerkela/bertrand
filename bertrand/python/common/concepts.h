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

    namespace categories {

        struct Base {
            static constexpr bool bool_like = false;
            static constexpr bool int_like = false;
            static constexpr bool float_like = false;
            static constexpr bool complex_like = false;
            static constexpr bool str_like = false;
            static constexpr bool timedelta_like = false;
            static constexpr bool timezone_like = false;
            static constexpr bool date_like = false;
            static constexpr bool time_like = false;
            static constexpr bool datetime_like = false;
            static constexpr bool tuple_like = false;
            static constexpr bool list_like = false;
            static constexpr bool set_like = false;
            static constexpr bool dict_like = false;
        };

        template <typename T>
        class Traits : public Base {};

        template <typename T>
        struct Traits<std::complex<T>> : public Base {
            static constexpr bool complex_like = true;
        };

        template <typename... Args>
        struct Traits<std::chrono::duration<Args...>> : public Base {
            static constexpr bool timedelta_like = true;
        };

        template <typename... Args>
        struct Traits<std::chrono::time_point<Args...>> : public Base {
            static constexpr bool time_like = true;
        };

        // TODO: std::time_t?

        template <typename... Args>
        struct Traits<std::pair<Args...>> : public Base {
            static constexpr bool tuple_like = true;
        };

        template <typename... Args>
        struct Traits<std::tuple<Args...>> : public Base {
            static constexpr bool tuple_like = true;
        };

        template <typename T, size_t N>
        struct Traits<std::array<T, N>> : public Base {
            static constexpr bool list_like = true;
        };

        template <typename... Args>
        struct Traits<std::vector<Args...>> : public Base {
            static constexpr bool list_like = true;
        };

        template <typename... Args>
        struct Traits<std::deque<Args...>> : public Base {
            static constexpr bool list_like = true;
        };

        template <typename... Args>
        struct Traits<std::list<Args...>> : public Base {
            static constexpr bool list_like = true;
        };

        template <typename... Args>
        struct Traits<std::forward_list<Args...>> : public Base {
            static constexpr bool list_like = true;
        };

        template <typename... Args>
        struct Traits<std::set<Args...>> : public Base {
            static constexpr bool set_like = true;
        };

        template <typename... Args>
        struct Traits<std::unordered_set<Args...>> : public Base {
            static constexpr bool set_like = true;
        };

        template <typename... Args>
        struct Traits<std::map<Args...>> : public Base {
            static constexpr bool dict_like = true;
        };

        template <typename... Args>
        struct Traits<std::unordered_map<Args...>> : public Base {
            static constexpr bool dict_like = true;
        };

    }

    // TODO: maybe all concepts are also exposed as traits in a nested namespace?

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
    concept none_like = (
        std::same_as<std::remove_cvref_t<T>, std::nullptr_t> ||
        std::derived_from<std::remove_cvref_t<T>, NoneType> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::none>
    );

    template <typename T>
    concept slice_like = (
        std::derived_from<std::remove_cvref_t<T>, Slice> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::slice>
    );

    template <typename T>
    concept module_like = (
        std::derived_from<std::remove_cvref_t<T>, Module> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::module>
    );

    template <typename T>
    concept bool_like = (
        std::same_as<std::remove_cvref_t<T>, bool> ||
        std::derived_from<std::remove_cvref_t<T>, Bool> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::bool_>
    );

    template <typename T>
    concept int_like = (
        std::derived_from<std::remove_cvref_t<T>, Int> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::int_> ||
        (
            std::integral<std::remove_cvref_t<T>> &&
            !std::same_as<std::remove_cvref_t<T>, bool>
        )
    );

    template <typename T>
    concept float_like = (
        std::floating_point<std::remove_cvref_t<T>> ||
        std::derived_from<std::remove_cvref_t<T>, Float> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::float_>
    );

    template <typename T>
    concept complex_like = requires(const T& t) {
        { t.real() } -> std::convertible_to<double>;
        { t.imag() } -> std::convertible_to<double>;
    };

    template <typename T>
    concept string_literal = requires(const T& t) {
        { []<size_t N>(const char(&)[N]){}(t) } -> std::same_as<void>;
    };

    template <typename T>
    concept str_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::same_as<std::remove_cvref_t<T>, const char*> ||
        std::same_as<std::remove_cvref_t<T>, std::string> ||
        std::same_as<std::remove_cvref_t<T>, std::string_view> ||
        std::derived_from<std::remove_cvref_t<T>, Str> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::str>
    );

    template <typename T>
    concept bytes_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::same_as<std::remove_cvref_t<T>, void*> ||
        std::derived_from<std::remove_cvref_t<T>, Bytes> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::bytes>
    );

    template <typename T>
    concept bytearray_like = (
        string_literal<std::remove_cvref_t<T>> ||
        std::same_as<std::remove_cvref_t<T>, void*> ||
        std::derived_from<std::remove_cvref_t<T>, ByteArray> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::bytearray>
    );

    template <typename T>
    concept anybytes_like = bytes_like<T> || bytearray_like<T>;

    template <typename T>
    concept timedelta_like = (
        categories::Traits<std::remove_cvref_t<T>>::timedelta_like ||
        std::derived_from<std::remove_cvref_t<T>, Timedelta>
    );

    template <typename T>
    concept timezone_like = (
        categories::Traits<std::remove_cvref_t<T>>::timezone_like ||
        std::derived_from<std::remove_cvref_t<T>, Timezone>
    );

    template <typename T>
    concept date_like = (
        categories::Traits<std::remove_cvref_t<T>>::date_like ||
        std::derived_from<std::remove_cvref_t<T>, Date>
    );

    template <typename T>
    concept time_like = (
        categories::Traits<std::remove_cvref_t<T>>::time_like ||
        std::derived_from<std::remove_cvref_t<T>, Time>
    );

    template <typename T>
    concept datetime_like = (
        categories::Traits<std::remove_cvref_t<T>>::datetime_like ||
        std::derived_from<std::remove_cvref_t<T>, Datetime>
    );

    template <typename T>
    concept range_like = (
        std::derived_from<std::remove_cvref_t<T>, Range>
    );

    template <typename T>
    concept tuple_like = (
        categories::Traits<std::remove_cvref_t<T>>::tuple_like ||
        std::derived_from<std::remove_cvref_t<T>, TupleTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::tuple>
    );

    template <typename T>
    concept list_like = (
        categories::Traits<std::remove_cvref_t<T>>::list_like ||
        std::derived_from<std::remove_cvref_t<T>, ListTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::list>
    );

    template <typename T>
    concept set_like = (
        categories::Traits<std::remove_cvref_t<T>>::set_like ||
        std::derived_from<std::remove_cvref_t<T>, SetTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::set>
    );

    template <typename T>
    concept frozenset_like = (
        categories::Traits<std::remove_cvref_t<T>>::set_like ||
        std::derived_from<std::remove_cvref_t<T>, FrozenSetTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::frozenset>
    );

    template <typename T>
    concept anyset_like = set_like<T> || frozenset_like<T>;

    template <typename T>
    concept dict_like = (
        categories::Traits<std::remove_cvref_t<T>>::dict_like ||
        std::derived_from<std::remove_cvref_t<T>, DictTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::dict>
    );

    template <typename T>
    concept mappingproxy_like = (
        categories::Traits<std::remove_cvref_t<T>>::dict_like ||
        std::derived_from<std::remove_cvref_t<T>, MappingProxyTag>
    );

    template <typename T>
    concept anydict_like = dict_like<T> || mappingproxy_like<T>;

    template <typename T>
    concept type_like = (
        std::derived_from<std::remove_cvref_t<T>, Type> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::type>
    );

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
    concept has_value_type = requires {
        typename T::value_type;
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
        static constexpr bool value = Condition<L, R>::value;
    };

    template <
        template <typename, typename> typename Condition,
        has_value_type L,
        has_value_type R
    >
    struct Broadcast<Condition, L, R> {
        static constexpr bool value =
            Broadcast<Condition, typename L::value_type, typename R::value_type>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        has_value_type R
    >
    struct Broadcast<Condition, L, R> {
        static constexpr bool value =
            Broadcast<Condition, L, typename R::value_type>::value;
    };

    template <
        template <typename, typename> typename Condition,
        has_value_type L,
        typename R
    >
    struct Broadcast<Condition, L, R> {
        static constexpr bool value =
            Broadcast<Condition, typename L::value_type, R>::value;
    };

    template <
        template <typename, typename> typename Condition,
        pybind11_like L,
        pybind11_like R
    >
    struct Broadcast<Condition, L, R> {
        static constexpr bool value = Broadcast<Condition, Object, Object>::value;
    };

    template <
        template <typename, typename> typename Condition,
        typename L,
        pybind11_like R
    > requires (!has_value_type<L>)
    struct Broadcast<Condition, L, R> {
        static constexpr bool value = Broadcast<Condition, L, Object>::value;
    };

    template <
        template <typename, typename> typename Condition,
        pybind11_like L,
        typename R
    > requires (!has_value_type<R>)
    struct Broadcast<Condition, L, R> {
        static constexpr bool value = Broadcast<Condition, Object, R>::value;
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
}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_CONCEPTS_H
