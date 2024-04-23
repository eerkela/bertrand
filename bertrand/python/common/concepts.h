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
            static constexpr bool boollike = false;
            static constexpr bool intlike = false;
            static constexpr bool floatlike = false;
            static constexpr bool complexlike = false;
            static constexpr bool strlike = false;
            static constexpr bool timedeltalike = false;
            static constexpr bool timezonelike = false;
            static constexpr bool datelike = false;
            static constexpr bool timelike = false;
            static constexpr bool datetimelike = false;
            static constexpr bool tuplelike = false;
            static constexpr bool listlike = false;
            static constexpr bool setlike = false;
            static constexpr bool dictlike = false;
        };

        template <typename T>
        class Traits : public Base {};

        template <typename T>
        struct Traits<std::complex<T>> : public Base {
            static constexpr bool complexlike = true;
        };

        template <typename... Args>
        struct Traits<std::chrono::duration<Args...>> : public Base {
            static constexpr bool timedeltalike = true;
        };

        template <typename... Args>
        struct Traits<std::chrono::time_point<Args...>> : public Base {
            static constexpr bool timelike = true;
        };

        // TODO: std::time_t?

        template <typename... Args>
        struct Traits<std::pair<Args...>> : public Base {
            static constexpr bool tuplelike = true;
        };

        template <typename... Args>
        struct Traits<std::tuple<Args...>> : public Base {
            static constexpr bool tuplelike = true;
        };

        template <typename T, size_t N>
        struct Traits<std::array<T, N>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::vector<Args...>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::deque<Args...>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::list<Args...>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::forward_list<Args...>> : public Base {
            static constexpr bool listlike = true;
        };

        template <typename... Args>
        struct Traits<std::set<Args...>> : public Base {
            static constexpr bool setlike = true;
        };

        template <typename... Args>
        struct Traits<std::unordered_set<Args...>> : public Base {
            static constexpr bool setlike = true;
        };

        template <typename... Args>
        struct Traits<std::map<Args...>> : public Base {
            static constexpr bool dictlike = true;
        };

        template <typename... Args>
        struct Traits<std::unordered_map<Args...>> : public Base {
            static constexpr bool dictlike = true;
        };

    }

    template <typename T>
    concept python_like = (
        std::derived_from<std::remove_cvref_t<T>, pybind11::object> ||
        std::derived_from<std::remove_cvref_t<T>, Object>
    );

    template <typename T>
    concept proxy_like = std::derived_from<std::remove_cvref_t<T>, ProxyTag>;

    template <typename T>
    concept initializer_like = std::derived_from<std::remove_cvref_t<T>, InitializerTag>;

    template <typename T>
    concept accessor_like = requires(const T& t) {
        { []<typename Policy>(const detail::accessor<Policy>){}(t) } -> std::same_as<void>;
    };

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
        categories::Traits<std::remove_cvref_t<T>>::timedeltalike ||
        std::derived_from<std::remove_cvref_t<T>, Timedelta>
    );

    template <typename T>
    concept timezone_like = (
        categories::Traits<std::remove_cvref_t<T>>::timezonelike ||
        std::derived_from<std::remove_cvref_t<T>, Timezone>
    );

    template <typename T>
    concept date_like = (
        categories::Traits<std::remove_cvref_t<T>>::datelike ||
        std::derived_from<std::remove_cvref_t<T>, Date>
    );

    template <typename T>
    concept time_like = (
        categories::Traits<std::remove_cvref_t<T>>::timelike ||
        std::derived_from<std::remove_cvref_t<T>, Time>
    );

    template <typename T>
    concept datetime_like = (
        categories::Traits<std::remove_cvref_t<T>>::datetimelike ||
        std::derived_from<std::remove_cvref_t<T>, Datetime>
    );

    template <typename T>
    concept range_like = (
        std::derived_from<std::remove_cvref_t<T>, Range>
    );

    template <typename T>
    concept tuple_like = (
        categories::Traits<std::remove_cvref_t<T>>::tuplelike ||
        std::derived_from<std::remove_cvref_t<T>, TupleTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::tuple>
    );

    template <typename T>
    concept list_like = (
        categories::Traits<std::remove_cvref_t<T>>::listlike ||
        std::derived_from<std::remove_cvref_t<T>, ListTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::list>
    );

    template <typename T>
    concept set_like = (
        categories::Traits<std::remove_cvref_t<T>>::setlike ||
        std::derived_from<std::remove_cvref_t<T>, SetTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::set>
    );

    template <typename T>
    concept frozenset_like = (
        categories::Traits<std::remove_cvref_t<T>>::setlike ||
        std::derived_from<std::remove_cvref_t<T>, FrozenSetTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::frozenset>
    );

    template <typename T>
    concept anyset_like = set_like<T> || frozenset_like<T>;

    template <typename T>
    concept dict_like = (
        categories::Traits<std::remove_cvref_t<T>>::dictlike ||
        std::derived_from<std::remove_cvref_t<T>, DictTag> ||
        std::derived_from<std::remove_cvref_t<T>, pybind11::dict>
    );

    template <typename T>
    concept mappingproxy_like = (
        categories::Traits<std::remove_cvref_t<T>>::dictlike ||
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

    // NOTE: decay is necessary to treat `const char[N]` like `const char*`
    template <typename T>
    concept is_hashable = requires(T&& t) {
        { std::hash<std::decay_t<T>>{}(std::forward<T>(t)) } -> std::convertible_to<size_t>;
    };

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
    concept has_to_string = requires(const T& t) {
        { std::to_string(t) } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept has_stream_insertion = requires(std::ostream& os, const T& t) {
        { os << t } -> std::convertible_to<std::ostream&>;
    };

    template <typename T>
    concept pybind11_iterable = requires(const T& t) {
        { pybind11::iter(t) } -> std::convertible_to<pybind11::iterator>;
    };

    template <typename T>
    concept has_call_operator = requires { &std::decay_t<T>::operator(); };

    /* SFINAE condition is used to recognize callable C++ types without regard to their
    argument signatures. */
    template <typename T>
    concept is_callable_any = 
        std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> ||
        std::is_member_function_pointer_v<std::decay_t<T>> ||
        has_call_operator<T>;


}  // namespace impl
}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_CONCEPTS_H
