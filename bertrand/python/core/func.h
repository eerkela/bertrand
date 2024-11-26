#ifndef BERTRAND_PYTHON_CORE_FUNC_H
#define BERTRAND_PYTHON_CORE_FUNC_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "arg.h"
#include "ops.h"
#include "access.h"
#include "iter.h"


namespace py {


/* Introspect an annotated C++ function signature to extract compile-time type
information about its parameters and allow a matching function to be called safely
from both languages with the same, Python-style syntax.  Also defines supporting
data structures to allow for dynamic function overloading and first-class partial
binding. */
template <typename T>
struct Signature {
    static constexpr bool enable = false;
};


namespace impl {

    /* Validate a C++ string that represents an argument name, throwing an error if it
    does not conform to Python naming conventions. */
    inline std::string_view get_parameter_name(std::string_view str) {
        std::string_view sub = str.substr(
            str.starts_with("*") +
            str.starts_with("**")
        );
        if (sub.empty()) {
            throw TypeError("argument name cannot be empty");
        } else if (std::isdigit(sub.front())) {
            throw TypeError(
                "argument name cannot start with a number: '" +
                std::string(sub) + "'"
            );
        }
        for (const char c : sub) {
            if (std::isalnum(c) || c == '_') {
                continue;
            }
            throw TypeError(
                "argument name must only contain alphanumerics and underscores: '" +
                std::string(sub) + "'"
            );
        }
        return str;
    }

    /* Validate a Python string that represents an argument name, throwing an error if
    it does not conform to Python naming conventions, and otherwise returning the name
    as a C++ string_view. */
    inline std::string_view get_parameter_name(PyObject* str) {
        Py_ssize_t len;
        const char* data = PyUnicode_AsUTF8AndSize(str, &len);
        if (data == nullptr) {
            Exception::from_python();
        }
        return get_parameter_name({data, static_cast<size_t>(len)});
    }

    /* A simple representation of a single parameter in a function signature or call
    site, for use when searching for overloads. */
    struct Param {
        /// TODO: in order to properly support variadic kwargs from C++ (which are
        /// represented using KeywordPacks), these need to own the underlying string
        /// data.  Alternatively, I can try to figure out how to store string_views
        /// in the keyword pack.
        std::string_view name;
        Object value;  // may be a type or instance
        ArgKind kind;

        constexpr bool posonly() const noexcept { return kind.posonly(); }
        constexpr bool pos() const noexcept { return kind.pos(); }
        constexpr bool args() const noexcept { return kind.args(); }
        constexpr bool kwonly() const noexcept { return kind.kwonly(); }
        constexpr bool kw() const noexcept { return kind.kw(); }
        constexpr bool kwargs() const noexcept { return kind.kwargs(); }
        constexpr bool opt() const noexcept { return kind.opt(); }
        constexpr bool variadic() const noexcept { return kind.variadic(); }

        /* Compute a hash of this parameter's name, type, and kind, using the given
        FNV-1a hash seed and prime. */
        size_t hash(size_t seed, size_t prime) const noexcept {
            return hash_combine(
                fnv1a(name.data(), seed, prime),
                PyType_Check(ptr(value)) ?
                    reinterpret_cast<size_t>(ptr(value)) :
                    reinterpret_cast<size_t>(Py_TYPE(ptr(value))),
                static_cast<size_t>(kind)
            );
        }
    };

    /* A read-only container of `Param` objects that also holds a combined hash
    suitable for cache optimization when searching a function's overload trie.  The
    underlying container type is flexible, and will generally be either a `std::array`
    (if the number of arguments is known ahead of time) or a `std::vector` (if they
    must be dynamic), but any container that supports read-only iteration, item access,
    and `size()` queries is technically supported. */
    template <yields<const Param&> T>
        requires (has_size<T> && lookup_yields<T, const Param&, size_t>)
    struct Params {
        T value;
        size_t hash = 0;

        const Param& operator[](size_t i) const noexcept { return value[i]; }
        size_t size() const noexcept { return std::ranges::size(value); }
        bool empty() const noexcept { return std::ranges::empty(value); }
        auto begin() const noexcept { return std::ranges::begin(value); }
        auto cbegin() const noexcept { return std::ranges::cbegin(value); }
        auto end() const noexcept { return std::ranges::end(value); }
        auto cend() const noexcept { return std::ranges::cend(value); }
    };

    /// TODO:
    /// Signature<F>::Defaults{values...};
    /// Signature<F>::Overloads{};
    /// Signature<F>::Vectorcall{array, nargsf, kwnames}(args...);
    /// Signature<F>::Partial{args...};  <- partially binds the function
    /// Signature<F>{parts...}(args...);  <- initializes the signature in canonical form?
    ///     ->  Signature will hold the canonical partial within it, then directly
    ///         initialize it and delegate to its call operator like normal.
    /// Signature<F>::Bind<...> <- aliases to Signature<F>::parts::bind<...>
    ///     ->   promotes convenience and readability in error messages.

    template <typename T>
    constexpr bool _canonical_function_type = false;
    template <typename R, typename... Args>
    constexpr bool _canonical_function_type<R(Args...)> = true;

    template <typename F>
    concept canonical_function_type = _canonical_function_type<F>;

    struct SignatureTag : BertrandTag {};
    struct defTag {};

    template <typename R>
    struct SignatureBase : SignatureTag {
        using Return = R;
    };

}


/* The canonical form of `py::Signature`, which encapsulates all of the internal
call machinery, most of which is evaluated at compile time.  All other
specializations should redirect to this form in order to avoid reimplementing the
nuts and bolts of the function system. */
template <typename Return, typename... Args>
struct Signature<Return(Args...)> : impl::SignatureBase<Return> {
    static constexpr bool enable = true;
    using type = Return(Args...);

    struct Partial;
    Partial parts;

    constexpr Signature(const Partial& other) : parts(other.parts) {}
    constexpr Signature(Partial&& other) : parts(std::move(other.parts)) {}

private:
    template <typename T>
    using ArgTraits = impl::ArgTraits<T>;

    template <typename T>
    using Params = impl::Params<T>;
    using Param = impl::Param;

    /// TODO: these helpers shouldn't count partial arguments?  That means they'll
    /// always take them into account, and there won't be any issues with Partial
    /// or Defaults subsignatures because they'll never contain partial args.

    template <typename...>
    static constexpr size_t _n_posonly = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _n_posonly<T, Ts...> =
        _n_posonly<Ts...> + ArgTraits<T>::posonly();

    template <typename...>
    static constexpr size_t _n_pos = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _n_pos<T, Ts...> =
        _n_pos<Ts...> + ArgTraits<T>::pos();

    template <typename...>
    static constexpr size_t _n_kw = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _n_kw<T, Ts...> =
        _n_kw<Ts...> + ArgTraits<T>::kw();

    template <typename...>
    static constexpr size_t _n_kwonly = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _n_kwonly<T, Ts...> =
        _n_kwonly<Ts...> + ArgTraits<T>::kwonly();

    template <StaticStr, typename...>
    static constexpr size_t _idx = 0;
    template <StaticStr Name, typename T, typename... Ts>
    static constexpr size_t _idx<Name, T, Ts...> =
        ArgTraits<T>::name == Name ? 0 : _idx<Name, Ts...> + 1;

    template <typename...>
    static constexpr size_t _posonly_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _posonly_idx<T, Ts...> =
        ArgTraits<T>::posonly() ? 0 : _posonly_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _pos_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _pos_idx<T, Ts...> =
        ArgTraits<T>::pos() ? 0 : _pos_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _args_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _args_idx<T, Ts...> =
        ArgTraits<T>::args() ? 0 : _args_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _kw_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _kw_idx<T, Ts...> =
        ArgTraits<T>::kw() ? 0 : _kw_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _kwonly_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _kwonly_idx<T, Ts...> =
        ArgTraits<T>::kwonly() ? 0 : _kwonly_idx<Ts...> + 1;

    template <typename...>
    static constexpr size_t _kwargs_idx = 0;
    template <typename T, typename... Ts>
    static constexpr size_t _kwargs_idx<T, Ts...> =
        ArgTraits<T>::kwargs() ? 0 : _kwargs_idx<Ts...> + 1;

    template <size_t I, StaticStr Name, typename T>
    struct Element {
        static constexpr size_t index = I;
        static constexpr StaticStr name = Name;
        using type = T;
        std::remove_cvref_t<type> value;

        constexpr impl::remove_rvalue<type> get(this auto&& self) {
            return std::forward<decltype(self)>(self).value;
        }
    };

    template <typename out, typename...>
    struct _unbind { using type = out; };
    template <typename R, typename... out, typename A, typename... As>
    struct _unbind<Signature<R(out...)>, A, As...> {
        template <typename>
        struct filter { using type = Signature<R(out...)>; };
        template <typename T> requires (ArgTraits<T>::bound())
        struct filter<T> {
            using type = Signature<R(out..., typename ArgTraits<T>::unbind)>;
        };
        using type = _unbind<filter<A>, As...>::type;
    };

    template <size_t I, size_t K>
    static constexpr bool _in_partial = false;
    template <size_t I, size_t K> requires (K < Partial::n)
    static constexpr bool _in_partial<I, K> =
        I == Partial::template rfind<K> || _in_partial<I, K + 1>;
    template <size_t I>
    static constexpr bool in_partial = _in_partial<I, 0>;

    template <size_t I, typename T> requires (I < Signature::n)
    static constexpr auto to_arg(T&& value) -> impl::unpack_type<I, Args...> {
        if constexpr (impl::is_arg<impl::unpack_type<I, Args...>>) {
            return {std::forward<T>(value)};
        } else {
            return std::forward<T>(value);
        }
    };

    template <size_t I>
    static Param _key(size_t& hash) {
        Param param = {
            .name = ArgTraits<at<I>>::name,
            .value = Overloads::positional_table[I].type(),
            .kind = ArgTraits<at<I>>::kind
        };
        hash = impl::hash_combine(hash, param.hash(
            Overloads::seed,
            Overloads::prime
        ));
        return param;
    }

public:
    static constexpr size_t n                   = sizeof...(Args);
    static constexpr size_t n_posonly           = _n_posonly<Args...>;
    static constexpr size_t n_pos               = _n_pos<Args...>;
    static constexpr size_t n_kw                = _n_kw<Args...>;
    static constexpr size_t n_kwonly            = _n_kwonly<Args...>;

    /// TODO: has<> may need to restrict itself to keyword arguments only, not
    /// named positional-only arguments.  Either that or I just need to be
    /// really careful when validating functions

    template <StaticStr Name>
    static constexpr bool has                   = n > _idx<Name, Args...>;
    static constexpr bool has_posonly           = n_posonly > 0;
    static constexpr bool has_pos               = n_pos > 0;
    static constexpr bool has_kw                = n_kw > 0;
    static constexpr bool has_kwonly            = n_kwonly > 0;
    static constexpr bool has_args              = n > _args_idx<Args...>;
    static constexpr bool has_kwargs            = n > _kwargs_idx<Args...>;

    template <StaticStr Name> requires (has<Name>)
    static constexpr size_t idx                 = _idx<Name, Args...>;
    static constexpr size_t posonly_idx         = _posonly_idx<Args...>;
    static constexpr size_t pos_idx             = _pos_idx<Args...>;
    static constexpr size_t kw_idx              = _kw_idx<Args...>;
    static constexpr size_t kwonly_idx          = _kwonly_idx<Args...>;
    static constexpr size_t args_idx            = _args_idx<Args...>;
    static constexpr size_t kwargs_idx          = _kwargs_idx<Args...>;

    template <size_t I> requires (I < n)
    using at = impl::unpack_type<I, Args...>;

    template <typename R>
    using with_return = Signature<R(Args...)>;

    template <typename... As>
    using with_args = Signature<Return(As...)>;

    template <typename Func>
    static constexpr bool invocable = std::is_invocable_r_v<Return, Func, Args...>;

    /* Holds a series of template constraints that can be used to validate function
    signatures according to Python calling conventions. */
    template <std::derived_from<impl::SignatureTag> Source>
    struct Check {
    private:
        template <size_t>
        static constexpr bool _args_are_python = true;
        template <size_t I> requires (I < Source::n)
        static constexpr bool _args_are_python<I> = [] {
            return impl::inherits<
                typename ArgTraits<typename Source::template at<I>>::type,
                Object
            > && _args_are_python<I + 1>;
        }();

        template <size_t>
        static constexpr bool _no_qualified_args = true;
        template <size_t I> requires (I < Source::n)
        static constexpr bool _no_qualified_args<I> = [] {
            using T = ArgTraits<typename Source::template at<I>>::type;
            return !(
                std::is_reference_v<T> ||
                std::is_const_v<std::remove_reference_t<T>> ||
                std::is_volatile_v<std::remove_reference_t<T>>
            ) && _no_qualified_args<I + 1>;
        }();

        template <size_t>
        static constexpr bool _no_qualified_arg_annotations = true;
        template <size_t I> requires (I < Source::n)
        static constexpr bool _no_qualified_arg_annotations<I> = [] {
            using T = Source::template at<I>;
            return !(impl::is_arg<T> && (
                std::is_reference_v<T> ||
                std::is_const_v<std::remove_reference_t<T>> ||
                std::is_volatile_v<std::remove_reference_t<T>>
            )) && _no_qualified_arg_annotations<I + 1>;
        }();

        template <size_t>
        static constexpr bool _proper_argument_order = true;
        template <size_t I> requires (I < Source::n)
        static constexpr bool _proper_argument_order<I> = [] {
            using T = Source::template at<I>;
            return !((
                ArgTraits<T>::posonly() &&
                (I > std::min({
                    Source::args_idx,
                    Source::kw_idx,
                    Source::kwargs_idx
                })) ||
                (!ArgTraits<T>::opt() && I > Source::Defaults::pos_idx)
            ) || (
                ArgTraits<T>::pos() && (
                    (I > std::min({
                        Source::args_idx,
                        Source::kwonly_idx,
                        Source::kwargs_idx
                    })) ||
                    (!ArgTraits<T>::opt() && I > Source::Defaults::pos_idx)
                )
            ) || (
                ArgTraits<T>::args() && (I > std::min(
                    Source::kwonly_idx,
                    Source::kwargs_idx
                ))
            ) || (
                ArgTraits<T>::kwonly() && (I > Source::kwargs_idx)
            )) && _proper_argument_order<I + 1>;
        }();

        template <size_t>
        static constexpr bool _no_duplicate_args = true;
        template <size_t I> requires (I < Source::n)
        static constexpr bool _no_duplicate_args<I> = [] {
            using T = Source::template at<I>;
            return !((
                ArgTraits<T>::name != "" &&
                I != Source::template idx<ArgTraits<T>::name>
            ) || (
                ArgTraits<T>::args() &&
                I != Source::args_idx
            ) || (
                ArgTraits<T>::kwargs() &&
                I != Source::kwargs_idx
            )) && _no_duplicate_args<I + 1>;
        }();

        template <size_t, size_t>
        static constexpr bool _no_extra_positional_args = true;
        template <size_t I, size_t J>
            requires (J < std::min({
                Source::args_idx,
                Source::kw_idx,
                Source::kwargs_idx
            }))
        static constexpr bool _no_extra_positional_args<I, J> = [] {
            return
                I < std::min(Signature::kwonly_idx, Signature::kwargs_idx) &&
                _no_extra_positional_args<
                    I + 1,
                    J + !in_partial<I>
                >;
        }();

        template <size_t>
        static constexpr bool _no_extra_keyword_args = true;
        template <size_t J> requires (J < Source::kwargs_idx)
        static constexpr bool _no_extra_keyword_args<J> = [] {
            using T = Source::template at<J>;
            return
                Signature::has<ArgTraits<T>::name> &&
                _no_extra_keyword_args<J + 1>;
        }();

        template <size_t, size_t>
        static constexpr bool _no_conflicting_values = true;
        template <size_t I, size_t J> requires (I < Signature::n && J < Source::n)
        static constexpr bool _no_conflicting_values<I, J> = [] {
            using T = Signature::at<I>;
            using U = Source::template at<J>;

            constexpr bool kw_conflicts_with_partial =
                ArgTraits<U>::kw() &&
                Partial::template has<ArgTraits<U>::name>;

            constexpr bool kw_conflicts_with_positional =
                !in_partial<I> && !ArgTraits<T>::name.empty() && (
                    ArgTraits<T>::posonly() ||
                    J < std::min(Source::kw_idx, Source::kwargs_idx)
                ) && Source::template has<ArgTraits<T>::name>;

            return
                !kw_conflicts_with_partial &&
                !kw_conflicts_with_positional &&
                _no_conflicting_values<
                    J == Source::args_idx ? std::min({
                        Signature::args_idx + 1,
                        Signature::kwonly_idx,
                        Signature::kwargs_idx
                    }) : I + 1,
                    I == Signature::args_idx ? std::min({
                        Source::kw_idx,
                        Source::kwargs_idx
                    }) : J + !in_partial<I>
                >;
        }();

        template <size_t, size_t>
        static constexpr bool _satisfies_required_args = true;
        template <size_t I, size_t J> requires (I < Signature::n)
        static constexpr bool _satisfies_required_args<I, J> = [] {
            return (
                in_partial<I> ||
                ArgTraits<Signature::at<I>>::opt() ||
                ArgTraits<Signature::at<I>>::variadic() ||
                (
                    ArgTraits<Signature::at<I>>::pos() &&
                        J < std::min(Source::kw_idx, Source::kwargs_idx)
                ) || (
                    ArgTraits<Signature::at<I>>::kw() &&
                        Source::template has<ArgTraits<Signature::at<I>>::name>
                )
            ) && _satisfies_required_args<
                J == Source::args_idx ?
                    std::min(Signature::kwonly_idx, Signature::kwargs_idx) :
                    I + 1,
                I == Signature::args_idx ?
                    std::min(Source::kw_idx, Source::kwargs_idx) :
                    J + !in_partial<I>
            >;
        }();

        template <size_t, size_t>
        static constexpr bool _can_convert = true;
        template <size_t I, size_t J> requires (I < Signature::n && J < Source::n)
        static constexpr bool _can_convert<I, J> = [] {
            if constexpr (ArgTraits<Signature::at<I>>::args()) {
                constexpr size_t source_kw =
                    std::min(Source::kw_idx, Source::kwargs_idx);
                return
                    []<size_t... Js>(std::index_sequence<Js...>) {
                        return (std::convertible_to<
                            typename ArgTraits<typename Source::template at<J + Js>>::type,
                            typename ArgTraits<Signature::at<I>>::type
                        > && ...);
                    }(std::make_index_sequence<J < source_kw ? source_kw - J : 0>{}) &&
                    _can_convert<I + 1, source_kw>;

            } else if constexpr (ArgTraits<Signature::at<I>>::kwargs()) {
                return
                    []<size_t... Js>(std::index_sequence<Js...>) {
                        return ((
                            Signature::has<ArgTraits<
                                typename Source::template at<Source::kw_idx + Js>
                            >::name> || std::convertible_to<
                                typename ArgTraits<
                                    typename Source::template at<Source::kw_idx + Js>
                                >::type,
                                typename ArgTraits<Signature::at<I>>::type
                            >
                        ) && ...);
                    }(std::make_index_sequence<Source::n - Source::kw_idx>{}) &&
                    _can_convert<I + 1, J>;

            } else if constexpr (in_partial<I>) {
                return _can_convert<I + 1, J>;

            } else if constexpr (ArgTraits<typename Source::template at<J>>::posonly()) {
                return std::convertible_to<
                    typename ArgTraits<typename Source::template at<J>>::type,
                    typename ArgTraits<Signature::at<I>>::type
                > && _can_convert<I + 1, J + 1>;

            } else if constexpr (ArgTraits<typename Source::template at<J>>::kw()) {
                constexpr StaticStr name = ArgTraits<typename Source::template at<J>>::name;
                if constexpr (Signature::has<name>) {
                    constexpr size_t idx = Signature::idx<name>;
                    if constexpr (!std::convertible_to<
                        typename ArgTraits<typename Source::template at<J>>::type,
                        typename ArgTraits<Signature::at<idx>>::type
                    >) {
                        return false;
                    };
                }
                return _can_convert<I + 1, J + 1>;

            } else if constexpr (ArgTraits<typename Source::template at<J>>::args()) {
                constexpr size_t target_kw =
                    std::min(Signature::kwonly_idx, Signature::kwargs_idx);
                return
                    []<size_t... Is>(std::index_sequence<Is...>) {
                        return (
                            (
                                in_partial<I + Is> || std::convertible_to<
                                    typename ArgTraits<
                                        typename Source::template at<J>
                                    >::type,
                                    typename ArgTraits<Signature::at<I + Is>>::type
                                >
                            ) && ...
                        );
                    }(std::make_index_sequence<I < target_kw ? target_kw - I : 0>{}) &&
                    _can_convert<target_kw, J + 1>;

            } else if constexpr (ArgTraits<typename Source::template at<J>>::kwargs()) {
                constexpr size_t transition = std::min({
                    Source::args_idx,
                    Source::kwonly_idx,
                    Source::kwargs_idx
                });
                constexpr size_t target_kw = Source::has_args ?
                    Signature::kwonly_idx :
                    []<size_t... Ks>(std::index_sequence<Ks...>) {
                        return std::max(
                            Signature::kw_idx,
                            Source::n_posonly + (0 + ... + (
                                std::tuple_element_t<
                                    Ks,
                                    typename Partial::Tuple
                                >::target_idx < transition
                            ))
                        );
                    }(std::make_index_sequence<Partial::n>{});
                return
                    []<size_t... Is>(std::index_sequence<Is...>) {
                        return ((
                            in_partial<target_kw + Is> || Source::template has<
                                ArgTraits<Signature::at<target_kw + Is>>::name
                            > || std::convertible_to<
                                typename ArgTraits<typename Source::template at<J>>::type,
                                typename ArgTraits<Signature::at<target_kw + Is>>::type
                            >
                        ) && ...);
                    }(std::make_index_sequence<Signature::n - target_kw>{}) &&
                    _can_convert<I, J + 1>;

            } else {
                static_assert(false);
                return false;
            }
        }();

        template <size_t I, size_t>
        static constexpr bool _viable_overload =
            I == Signature::n ||
            (I == Signature::args_idx && Signature::args_idx == Signature::n - 1) ||
            (I == Signature::kwargs_idx && Signature::kwargs_idx == Signature::n - 1);
        template <size_t I, size_t J> requires (I < Signature::n && J < Source::n)
        static constexpr bool _viable_overload<I, J> = [] {
            using T = Signature::at<I>;
            using U = Source::template at<J>;
            if constexpr (ArgTraits<T>::posonly()) {
                return
                    ArgTraits<U>::posonly() &&
                    !(ArgTraits<T>::opt() && !ArgTraits<U>::opt()) &&
                    (ArgTraits<T>::name == ArgTraits<U>::name) &&
                    issubclass<
                        typename ArgTraits<U>::type,
                        typename ArgTraits<T>::type
                    >() &&
                    _viable_overload<I + 1, J + 1>;

            } else if constexpr (ArgTraits<T>::pos()) {
                return
                    (ArgTraits<U>::pos() && ArgTraits<U>::kw()) &&
                    !(ArgTraits<T>::opt() && !ArgTraits<U>::opt()) &&
                    (ArgTraits<T>::name == ArgTraits<U>::name) &&
                    issubclass<
                        typename ArgTraits<U>::type,
                        typename ArgTraits<T>::type
                    >() &&
                    _viable_overload<I + 1, J + 1>;

            } else if constexpr (ArgTraits<T>::kw()) {
                return
                    (ArgTraits<U>::kw() && ArgTraits<U>::pos()) &&
                    !(ArgTraits<T>::opt() && !ArgTraits<U>::opt()) &&
                    (ArgTraits<T>::name == ArgTraits<U>::name) &&
                    issubclass<
                        typename ArgTraits<U>::type,
                        typename ArgTraits<T>::type
                    >() &&
                    _viable_overload<I + 1, J + 1>;

            } else if constexpr (ArgTraits<T>::args()) {
                if constexpr (ArgTraits<U>::pos() || ArgTraits<U>::args()) {
                    if constexpr (!issubclass<
                        typename ArgTraits<U>::type,
                        typename ArgTraits<T>::type
                    >()) {
                        return false;
                    }
                    return _viable_overload<I, J + 1>;
                }
                return _viable_overload<I + 1, J + 1>;

            } else if constexpr (ArgTraits<T>::kwargs()) {
                if constexpr (ArgTraits<U>::kw() || ArgTraits<U>::kwargs()) {
                    if constexpr (!issubclass<
                        typename ArgTraits<U>::type,
                        typename ArgTraits<T>::type
                    >()) {
                        return false;
                    }
                    return _viable_overload<I, J + 1>;
                }
                return _viable_overload<I + 1, J + 1>;

            } else {
                static_assert(false, "unrecognized parameter type");
                return false;
            }
        }();

    public:
        static constexpr bool args_fit_within_bitset =
            Source::n <= 64;

        static constexpr bool return_is_python =
            impl::inherits<typename Source::Return, Object>;

        static constexpr bool args_are_python =
            _args_are_python<0>;

        static constexpr bool no_qualified_return = !(
            std::is_reference_v<typename Source::Return> ||
            std::is_const_v<std::remove_reference_t<typename Source::Return>> ||
            std::is_volatile_v<std::remove_reference_t<typename Source::Return>>
        );

        static constexpr bool no_qualified_args =
            _no_qualified_args<0>;

        static constexpr bool no_qualified_arg_annotations =
            _no_qualified_arg_annotations<0>;

        static constexpr bool proper_argument_order =
            _proper_argument_order<0>;

        static constexpr bool no_duplicate_args =
            _no_duplicate_args<0>;

        static constexpr bool no_extra_positional_args =
            Signature::has_args || !Source::has_posonly ||
            _no_extra_positional_args<0, 0>;

        static constexpr bool no_extra_keyword_args =
            Signature::has_kwargs || _no_extra_keyword_args<Source::kw_idx>;

        static constexpr bool no_conflicting_values =
            _no_conflicting_values<0, 0>;

        static constexpr bool satisfies_required_args =
            _satisfies_required_args<0, 0>;

        static constexpr bool can_convert =
            _can_convert<0, 0>;

        static constexpr bool viable_overload =
            _viable_overload<0, 0>; 
    };

    static constexpr bool args_fit_within_bitset =
        Check<Signature>::args_fit_within_bitset;

    static constexpr bool return_is_python =
        Check<Signature>::return_is_python;

    static constexpr bool args_are_python =
        Check<Signature>::args_are_python;

    static constexpr bool no_qualified_return =
        Check<Signature>::no_qualified_return;

    static constexpr bool no_qualified_args =
        Check<Signature>::no_qualified_args;

    static constexpr bool no_qualified_arg_annotations =
        Check<Signature>::no_qualified_arg_annotations;

    static constexpr bool proper_argument_order =
        Check<Signature>::proper_argument_order;

    static constexpr bool no_duplicate_args =
        Check<Signature>::no_duplicate_args;

    /* A tuple holding a default value for every argument in the enclosing
    parameter list that is marked as optional.  One of these must be provided
    whenever a C++ function is invoked, and constructing one requires that the
    initializers match a sub-signature consisting only of the optional args as
    keyword-only parameters for clarity.  The result may be empty if there are no
    optional arguments in the enclosing signature, in which case the constructor
    will be optimized out. */
    struct Defaults {
    protected:
        template <typename...>
        static constexpr size_t _n_posonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_posonly<T, Ts...> =
            _n_posonly<Ts...> + (ArgTraits<T>::posonly() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_pos = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_pos<T, Ts...> =
            _n_pos<Ts...> + (ArgTraits<T>::pos() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_kw = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kw<T, Ts...> =
            _n_kw<Ts...> + (ArgTraits<T>::kw() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_kwonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kwonly<T, Ts...> =
            _n_kwonly<Ts...> + (ArgTraits<T>::kwonly() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _posonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _posonly_idx<T, Ts...> =
            ArgTraits<T>::posonly() && ArgTraits<T>::opt() ?
                0 : _posonly_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _pos_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _pos_idx<T, Ts...> =
            ArgTraits<T>::pos() && ArgTraits<T>::opt() ?
                0 : _pos_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kw_idx<T, Ts...> =
            ArgTraits<T>::kw() && ArgTraits<T>::opt() ?
                0 : _kw_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwonly_idx<T, Ts...> =
            ArgTraits<T>::kwonly() && ArgTraits<T>::opt() ?
                0 : _kwonly_idx<Ts...> + 1;

        template <size_t, typename>
        static constexpr size_t _find = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _find<I, std::tuple<T, Ts...>> =
            (I == T::index) ? 0 : 1 + _find<I, std::tuple<Ts...>>;

        /* Build a sub-signature holding only the arguments marked as optional from
        the enclosing signature.  This will be a specialization of the enclosing
        class, which is used to bind arguments to this class's constructor using
        the same semantics as the function's call operator. */
        template <typename out, typename...>
        struct extract { using type = out; };
        template <typename... out, typename A, typename... As>
        struct extract<Signature<Defaults(out...)>, A, As...> {
            template <typename>
            struct sub_signature { using type = Signature<Defaults(out...)>; };
            template <typename T> requires (ArgTraits<T>::opt())
            struct sub_signature<T> {
                template <typename D>
                struct to_default { using type = D; };
                template <typename D> requires (ArgTraits<D>::opt())
                struct to_default<D> {
                    using type = Arg<
                        ArgTraits<D>::name,
                        typename ArgTraits<D>::type
                    >::kw;
                };
                using type = Signature<Defaults(out..., typename to_default<T>::type)>;
            };
            using type = extract<typename sub_signature<A>::type, As...>::type;
        };
        using Inner = extract<Signature<Defaults()>, Args...>::type;

        /* Build a std::tuple of Elements instances to hold the default values
        themselves. */
        template <typename out, size_t, typename...>
        struct collect { using type = out; };
        template <typename... out, size_t I, typename A, typename... As>
        struct collect<std::tuple<out...>, I, A, As...> {
            template <typename>
            struct tuple { using type = std::tuple<out...>; };
            template <typename T> requires (ArgTraits<T>::opt())
            struct tuple<T> {
                using type = std::tuple<
                    out...,
                    Element<
                        I,
                        ArgTraits<T>::name,
                        typename ArgTraits<T>::type
                    >
                >;
            };
            using type = collect<typename tuple<A>::type, I + 1, As...>::type;
        };

        using Tuple = collect<std::tuple<>, 0, Args...>::type;
        Tuple values;

        template <size_t J, typename... As>
        static constexpr decltype(auto) build(As&&... args) {
            using T = std::tuple_element_t<J, Tuple>;
            constexpr size_t idx = Signature<void(As...)>::template idx<T::name>;
            return impl::unpack_arg<idx>(std::forward<As>(args)...);
        }

    public:
        static constexpr size_t n               = Inner::n;
        static constexpr size_t n_posonly       = _n_posonly<Args...>;
        static constexpr size_t n_pos           = _n_pos<Args...>;
        static constexpr size_t n_kw            = _n_kw<Args...>;
        static constexpr size_t n_kwonly        = _n_kwonly<Args...>;

        template <StaticStr Name>
        static constexpr bool has               = Inner::template has<Name>;
        static constexpr bool has_posonly       = n_posonly > 0;
        static constexpr bool has_pos           = n_pos > 0;
        static constexpr bool has_kw            = n_kw > 0;
        static constexpr bool has_kwonly        = n_kwonly > 0;

        template <StaticStr Name> requires (has<Name>)
        static constexpr size_t idx             = Inner::template idx<Name>;
        static constexpr size_t posonly_idx     = _posonly_idx<Args...>;
        static constexpr size_t pos_idx         = _pos_idx<Args...>;
        static constexpr size_t kw_idx          = _kw_idx<Args...>;
        static constexpr size_t kwonly_idx      = _kwonly_idx<Args...>;

        template <size_t I> requires (I < n)
        using at = Inner::template at<I>;

        /* Given an index into the enclosing signature, find the corresponding index
        in the defaults tuple if that index is marked as optional. */
        template <size_t I> requires (ArgTraits<typename Signature::at<I>>::opt())
        static constexpr size_t find = _find<I, Tuple>;

        /* Given an index into the defaults tuple, find the corresponding index in
        the enclosing parameter list. */
        template <size_t J> requires (J < n)
        static constexpr size_t rfind = std::tuple_element<J, Tuple>::type::index;

        /* Bind an argument list to the default values to enable the constructor. */
        template <typename... As>
        using Bind = Inner::template Bind<As...>;

        /* Provides access to the template constraints for the constructor
        sub-signature. */
        template <typename Source>
        using Check = Inner::template Check<Source>;

        template <typename... As>
            requires (
                !(impl::arg_pack<As> || ...) &&
                !(impl::kwarg_pack<As> || ...) &&
                Check<Signature<Defaults(As...)>>::proper_argument_order &&
                Check<Signature<Defaults(As...)>>::no_qualified_arg_annotations &&
                Check<Signature<Defaults(As...)>>::no_duplicate_args &&
                Check<Signature<Defaults(As...)>>::no_conflicting_values &&
                Check<Signature<Defaults(As...)>>::no_extra_positional_args &&
                Check<Signature<Defaults(As...)>>::no_extra_keyword_args &&
                Check<Signature<Defaults(As...)>>::satisfies_required_args &&
                Check<Signature<Defaults(As...)>>::can_convert
            )
        constexpr Defaults(As&&... args) : values(
            []<size_t... Js>(std::index_sequence<Js...>, auto&&... args) -> Tuple {
                return {{build<Js>(std::forward<decltype(args)>(args)...)}...};
            }(std::index_sequence_for<As...>{}, std::forward<As>(args)...)
        ) {}

        /* Get the default value at index I of the tuple.  Use find<> to correlate
        an index from the enclosing signature if needed.  If the defaults container
        is used as an lvalue, then this will either directly reference the internal
        value if the corresponding argument expects an lvalue, or a copy if it
        expects an unqualified or rvalue type.  If the defaults container is given
        as an rvalue instead, then the copy will be optimized to a move. */
        template <size_t J> requires (J < n)
        constexpr decltype(auto) get(this auto&& self) {
            return std::get<J>(std::forward<decltype(self)>(self).values).get();
        }

        /* Get the default value associated with the named argument, if it is
        marked as optional.  If the defaults container is used as an lvalue, then
        this will either directly reference the internal value if the corresponding
        argument expects an lvalue, or a copy if it expects an unqualified or
        rvalue type.  If the defaults container is given as an rvalue instead, then
        the copy will be optimized to a move. */
        template <StaticStr Name> requires (has<Name>)
        constexpr decltype(auto) get(this auto&& self) {
            return std::get<idx<Name>>(std::forward<decltype(self)>(self).values).get();
        }
    };

    /* A tuple holding a partial value for every bound argument in the enclosing
    parameter list.  One of these must be provided whenever a C++ function is
    invoked, and constructing one requires that the initializers match a
    sub-signature consisting only of the bound args as positional-only and
    keyword-only parameters for clarity.  The result may be empty if there are no
    bound arguments in the enclosing signature, in which case the constructor will
    be optimized out. */
    struct Partial {
    private:
        /// TODO: count/index helpers just like Defaults?

        /* Build a sub-signature holding only the bound arguments from the
        enclosing signature.  This will be a specialization of the enclosing class,
        which is used to bind arguments to this class's constructor using the same
        semantics as the function's call operator. */
        template <typename out, typename...>
        struct extract { using type = out; };
        template <typename... out, typename A, typename... As>
        struct extract<Signature<Partial(out...)>, A, As...> {
            template <typename>
            struct sub_signature { using type = Signature<Partial(out...)>; };
            template <typename T> requires (ArgTraits<T>::bound())
            struct sub_signature<T> {
                template <typename>
                struct extend;
                template <typename... Ps>
                struct extend<pack<Ps...>> {
                    template <typename P>
                    struct to_partial { using type = P; };
                    template <typename P> requires (ArgTraits<P>::kw())
                    struct to_partial<P> {
                        using type = Arg<
                            ArgTraits<P>::name,
                            typename ArgTraits<P>::type
                        >::kw;
                    };
                    using type = Signature<Partial(out..., typename to_partial<Ps>::type...)>;
                };
                using type = extend<typename ArgTraits<T>::bound_to>::type;
            };
            using type = extract<
                typename sub_signature<A>::type,
                As...
            >::type;
        };
        using Inner = extract<Signature<Partial()>, Args...>::type;

        /* Build a std::tuple of Elements that hold the bound values in a way that
        can be cross-referenced with the target signature. */
        template <typename out, size_t, typename...>
        struct collect { using type = out; };
        template <typename... out, size_t I, typename A, typename... As>
        struct collect<std::tuple<out...>, I, A, As...> {
            template <typename>
            struct tuple { using type = std::tuple<out...>; };
            template <typename T> requires (ArgTraits<T>::bound())
            struct tuple<T> {
                template <typename>
                struct extend;
                template <typename... Ps>
                struct extend<pack<Ps...>> {
                    using type = std::tuple<
                        out...,
                        Element<
                            I,
                            ArgTraits<Ps>::name,
                            typename ArgTraits<Ps>::type
                        >...
                    >;
                };
                using type = extend<typename ArgTraits<T>::bound_to>::type;
            };
            using type = collect<typename tuple<A>::type, I + 1, As...>::type;
        };

        using Tuple = collect<std::tuple<>, 0, Args...>::type;
        Tuple values;

        template <size_t K, typename... As>
        static constexpr decltype(auto) build(As&&... args) {
            using T = std::tuple_element_t<K, Tuple>;
            if constexpr (T::name.empty()) {
                return impl::unpack_arg<K>(std::forward<As>(args)...);
            } else {
                constexpr size_t idx = Signature<void(As...)>::template idx<T::name>;
                return impl::unpack_arg<idx>(std::forward<As>(args)...);
            }
        }

    public:
        static constexpr size_t n               = Inner::n;
        static constexpr size_t n_posonly       = Inner::n_posonly;
        static constexpr size_t n_pos           = Inner::n_pos;
        static constexpr size_t n_kw            = Inner::n_kw;
        static constexpr size_t n_kwonly        = Inner::n_kwonly;

        template <StaticStr Name>
        static constexpr bool has               = Inner::template has<Name>;
        static constexpr bool has_posonly       = Inner::has_posonly;
        static constexpr bool has_pos           = Inner::has_pos;
        static constexpr bool has_kw            = Inner::has_kw;
        static constexpr bool has_kwonly        = Inner::has_kwonly;

        template <StaticStr Name> requires (has<Name>)
        static constexpr size_t idx             = Inner::template idx<Name>;
        static constexpr size_t posonly_idx     = Inner::posonly_idx;
        static constexpr size_t pos_idx         = Inner::pos_idx;
        static constexpr size_t kw_idx          = Inner::kw_idx;
        static constexpr size_t kwonly_idx      = Inner::kwonly_idx;

        template <size_t K> requires (K < n)
        using at = Inner::template at<K>;

        /* Get the recorded name of the bound argument at index K of the partial
        tuple. */
        template <size_t K> requires (K < n)
        static constexpr StaticStr name = std::tuple_element_t<K, Tuple>::name;

        /* Given an index into the partial tuple, find the corresponding index in
        the enclosing parameter list. */
        template <size_t K> requires (K < n)
        static constexpr size_t rfind = std::tuple_element_t<K, Tuple>::index;

        /* Bind an argument list to the partial values to enable the constructor. */
        template <typename... As>
        using Bind = Inner::template Bind<As...>;

        /* Provides access to the template constraints for the constructor
        sub-signature. */
        template <typename Source>
        using Check = Inner::template Check<Source>;

        template <typename... As>
            requires (
                !(impl::arg_pack<As> || ...) &&
                !(impl::kwarg_pack<As> || ...) &&
                Check<Signature<Partial(As...)>>::proper_argument_order &&
                Check<Signature<Partial(As...)>>::no_qualified_arg_annotations &&
                Check<Signature<Partial(As...)>>::no_duplicate_args &&
                Check<Signature<Partial(As...)>>::no_conflicting_values &&
                Check<Signature<Partial(As...)>>::no_extra_positional_args &&
                Check<Signature<Partial(As...)>>::no_extra_keyword_args &&
                Check<Signature<Partial(As...)>>::satisfies_required_args &&
                Check<Signature<Partial(As...)>>::can_convert
            )
        constexpr Partial(As&&... args) : values(
            []<size_t... Ks>(std::index_sequence<Ks...>, auto&&... args) -> Tuple {
                return {{build<Ks>(std::forward<decltype(args)>(args)...)}...};
            }(std::index_sequence_for<As...>{}, std::forward<As>(args)...)
        ) {}

        /* Get the bound value at index K of the tuple.  If the partials are
        forwarded as an lvalue, then this will either directly reference the
        internal value if the corresponding argument expects an lvalue, or a copy
        if it expects an unqualified or rvalue type.  If the partials are given as
        an rvalue instead, then the copy will instead be optimized to a move. */
        template <size_t K> requires (K < n)
        constexpr decltype(auto) get(this auto&& self) {
            return std::get<K>(std::forward<decltype(self)>(self).values).get();
        }

        /* Get the bound value associated with the named argument, if it was given
        as a keyword argument.  If the partials are forwarded as an lvalue, then
        this will either directly reference the internal value if the corresponding
        argument expects an lvalue, or a copy if it expects an unqualified or rvalue
        type.  If the partials are given as an rvalue instead, then the copy will be
        optimized to a move. */
        template <StaticStr Name> requires (has<Name>)
        constexpr decltype(auto) get(this auto&& self) {
            return std::get<idx<Name>>(std::forward<decltype(self)>(self).values).get();
        }
    };

    /* Bind a C++ argument list to the enclosing signature, inserting default
    values and partial arguments where necessary to satisfy the signature.  This
    helper enables and implements the signature's call operator as a 3-way merge
    between the partial arguments, default values, and given source arguments.
    Additionally, the bound arguments can be saved and encoded into a partial
    signature in a chainable fashion, using the same infrastructure to simulate a
    normal function call at every step.  Any existing partial arguments will be
    folded into the resulting signature, facilitating higher-order function
    composition (currying, etc.) that can be computed entirely at compile time. */
    template <typename... Values>
        requires (
            Check<Signature<Return(Values...)>>::proper_argument_order &&
            Check<Signature<Return(Values...)>>::no_qualified_arg_annotations &&
            Check<Signature<Return(Values...)>>::no_duplicate_args &&
            Check<Signature<Return(Values...)>>::no_extra_positional_args &&
            Check<Signature<Return(Values...)>>::no_extra_keyword_args &&
            Check<Signature<Return(Values...)>>::no_conflicting_values &&
            Check<Signature<Return(Values...)>>::can_convert
        )
    struct Bind {
    private:
        using Source = Signature<Return(Values...)>;

        /* Source positional packs must be converted to this type, which
        encloses a pair of iterators over the positional arguments.  The
        iterators are consumed as arguments are extracted from the pack,
        allowing for efficient validation by simply checking whether all
        elements were consumed, and listing those that weren't. */
        template <typename Pack>
        struct PositionalPack {
            std::ranges::iterator_t<const Pack&> begin;
            std::ranges::sentinel_t<const Pack&> end;
            size_t size;

            PositionalPack(const Pack& pack) :
                begin(std::ranges::begin(pack)),
                end(std::ranges::end(pack)),
                size(std::ranges::size(pack))
            {}

            void validate() {
                if constexpr (!Signature::has_args) {
                    if (begin != end) {
                        std::string message =
                            "too many arguments in positional parameter pack: ['" +
                            repr(*begin);
                        while (++begin != end) {
                            message += "', '" + repr(*begin);
                        }
                        message += "']";
                        throw TypeError(message);
                    }
                }
            }

            bool has_value() const { return begin != end; }
            decltype(auto) value() {
                decltype(auto) result = *begin;
                ++begin;
                return result;
            }
        };

        /* Source keyword packs must be converted to this type, which encloses
        a temporary map of keyword names to their corresponding values.  The
        `.extract()` method is used to destructively search the map and fill
        in corresponding values, allowing for efficient validation by simply
        checking whether the map is empty, and listing any remaining contents
        if not. */
        template <typename Pack>
        struct KeywordPack {
            using Map = std::unordered_map<
                std::string,
                typename Pack::mapped_type
            >;
            Map map;

            KeywordPack(const Pack& pack) :
                map([](const Pack& pack) {
                    Map map;
                    map.reserve(pack.size());
                    for (auto&& [key, value] : pack) {
                        auto [it, inserted] = map.emplace(
                            std::forward<decltype(key)>(key),
                            std::forward<decltype(value)>(value)
                        );
                        if (!inserted) {
                            throw TypeError(
                                "duplicate keyword argument: '" + it->first + "'"
                            );
                        }
                    }
                    return map;
                }(pack))
            {}

            void validate() {
                if constexpr (!Signature::has_kwargs) {
                    if (!map.empty()) {
                        auto it = map.begin();
                        auto end = map.end();
                        std::string message =
                            "unexpected keyword arguments: ['" + it->first;
                        while (++it != end) {
                            message += "', '" + it->first;
                        }
                        message += "']";
                        throw TypeError(message);
                    }
                }
            }

            auto size() const { return map.size(); }
            template <typename T>
            auto extract(T&& key) { return map.extract(std::forward<T>(key)); }
            auto begin() { return map.begin(); }
            auto end() { return map.end(); }
        };

        template <typename Pack>
        PositionalPack(const Pack&) -> PositionalPack<Pack>;
        template <typename Pack>
        KeywordPack(const Pack&) -> KeywordPack<Pack>;

        template <typename... A>
        static constexpr bool pos_pack_idx = 0;
        template <typename T, typename... As>
        static constexpr bool pos_pack_idx<PositionalPack<T>, As...> = 0;
        template <typename A, typename... As>
        static constexpr bool pos_pack_idx<A, As...> = pos_pack_idx<As...> + 1;

        template <typename... A>
        static constexpr bool kw_pack_idx = 0;
        template <typename T, typename... As>
        static constexpr bool kw_pack_idx<KeywordPack<T>, As...> = 0;
        template <typename A, typename... As>
        static constexpr bool kw_pack_idx<A, As...> = kw_pack_idx<As...> + 1;

        template <size_t I, size_t J, size_t K>
        struct call {  // terminal case
            template <typename... As>
            struct signature {
                using Source = Signature<Return(As...)>;

                template <typename out, size_t, size_t>
                struct bind { using type = out; };
                template <typename... out, size_t I2, size_t J2>
                    requires (I2 < Signature::n)
                struct bind<Return(out...), I2, J2> {
                    template <typename>
                    struct append;

                    template <typename T> requires (ArgTraits<T>::posonly())
                    struct append<T> {
                        template <size_t J3>
                        struct do_append {
                            using type = bind<Return(out..., T), I2 + 1, J3>::type;
                        };
                        template <size_t J3> requires (J3 < Source::kw_idx)
                        struct do_append<J3> {
                            using S = Source::template at<J>;
                            using bound = ArgTraits<T>::template bind<S>::type;
                            using type = bind<Return(out..., bound), I2 + 1, J3 + 1>::type;
                        };
                        using type = do_append<J2>::type;
                    };

                    template <typename T> requires (ArgTraits<T>::pos() && ArgTraits<T>::kw())
                    struct append<T> {
                        template <size_t J3>
                        struct do_append {
                            using type = bind<Return(out..., T), I2 + 1, J3>::type;
                        };
                        template <size_t J3> requires (J3 < Source::kw_idx)
                        struct do_append<J3> {
                            using S = Source::template at<J>;
                            using bound = ArgTraits<T>::template bind<S>::type;
                            using type = bind<Return(out..., bound), I2 + 1, J3 + 1>::type;
                        };
                        template <size_t J3> requires (
                            !(J3 < Source::kw_idx) &&
                            Source::template has<ArgTraits<T>::name>
                        )
                        struct do_append<J3> {
                            static constexpr StaticStr name = ArgTraits<T>::name;
                            static constexpr size_t idx = Source::template idx<name>;
                            using S = Source::template at<idx>;
                            using bound = ArgTraits<T>::template bind<S>::type;
                            using type = bind<Return(out..., bound), I2 + 1, J3>::type;
                        };
                        using type = do_append<J2>::type;
                    };

                    template <typename T> requires (ArgTraits<T>::kwonly())
                    struct append<T> {
                        template <size_t J3>
                        struct do_append {
                            using type = bind<Return(out..., T), I2 + 1, J3>::type;
                        };
                        template <size_t J3>
                            requires (Source::template has<ArgTraits<T>::name>)
                        struct do_append<J3> {
                            static constexpr StaticStr name = ArgTraits<T>::name;
                            static constexpr size_t idx = Source::template idx<name>;
                            using S = Source::template at<idx>;
                            using bound = ArgTraits<T>::template bind<S>::type;
                            using type = bind<Return(out..., bound), I2 + 1, J3>::type;
                        };
                        using type = do_append<J2>::type;
                    };

                    template <typename T> requires (ArgTraits<T>::args())
                    struct append<T> {
                        /// TODO: consume all remaining positional arguments
                    };

                    template <typename T> requires (ArgTraits<T>::kwargs())
                    struct append<T> {
                        /// TODO: bind any keywords that are not present in the
                        /// target signature
                    };

                    using type = append<
                        typename ArgTraits<Signature::at<I2>>::unbind
                    >::type;
                };

                using type = bind<Return(), I, J>::type;
            };

            template <typename P, typename... As>
            static auto bind(
                P&& parts,
                As&&... args
            ) -> signature<As...>::type {
                return {std::forward<As>(args)...};
            }

            /* Invoking a C++ function involves a 3-way merge of the partial
            arguments, source arguments, and default values, in that order of
            precedence.  By the end, the parameters are guaranteed to exactly
            match the enclosing signature, such that it can be passed to a
            matching function with the intended semantics.  This is done by
            inserting, removing, and reordering parameters from the argument
            list at compile time using index sequences and fold expressions,
            which can be inlined into the final call. */
            struct cpp {
                template <typename P, typename D, typename F, typename... A>
                static constexpr std::invoke_result_t<F, Args...> operator()(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    // validate and remove positional parameter packs
                    if constexpr (pos_pack_idx<A...> < sizeof...(A)) {
                        constexpr size_t idx = pos_pack_idx<A...>;
                        return []<size_t... Prev, size_t... Next>(
                            std::index_sequence<Prev...>,
                            std::index_sequence<Next...>,
                            auto&& parts,
                            auto&& defaults,
                            auto&& func,
                            auto&&... args
                        ) {
                            auto& pack = impl::unpack_arg<idx>(
                                std::forward<decltype(args)>(args)...
                            );
                            pack.validate();
                            return typename call<I, J, K>::cpp{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                std::forward<decltype(func)>(func),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                impl::unpack_arg<idx + 1 + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...
                            );
                        }(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );

                    // validate and remove keyword parameter packs
                    } else if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                        constexpr size_t idx = kw_pack_idx<A...>;
                        return []<size_t... Prev, size_t... Next>(
                            std::index_sequence<Prev...>,
                            std::index_sequence<Next...>,
                            auto&& parts,
                            auto&& defaults,
                            auto&& func,
                            auto&&... args
                        ) {
                            auto& pack = impl::unpack_arg<idx>(
                                std::forward<decltype(args)>(args)...
                            );
                            pack.validate();
                            return typename call<I, J, K>::cpp{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                std::forward<decltype(func)>(func),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                impl::unpack_arg<idx + 1 + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...
                            );
                        }(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );

                    // call the function with the final argument list
                    } else {
                        return std::forward<F>(func)(std::forward<A>(args)...);
                    }
                }
            };

            /* Invoking a Python function involves populating an argument
            array according to a C++ parameter list.  Such an array can then be
            used to efficiently call a Python function using the vectorcall
            protocol, which is the fastest way to call a Python
            function from C++.  Here's the basic layout:

                                        ( kwnames tuple )
                    -------------------------------------
                    | x | p | p | p |...| k | k | k |...|
                    -------------------------------------
                        ^             ^
                        |             nargs ends here
                        *args starts here

            Where 'x' is an optional first element that can be temporarily
            written to in order to efficiently forward the `self` argument
            for bound methods, etc.  The presence of this argument is
            determined by the PY_VECTORCALL_ARGUMENTS_OFFSET flag, which is
            encoded in nargs.  You can check for its presence by bitwise
            AND-ing against nargs, and the true number of arguments must be
            extracted using `PyVectorcall_NARGS(nargs)` to account for this.

            If PY_VECTORCALL_ARGUMENTS_OFFSET is set and 'x' is written to,
            then it must always be reset to its original value before the
            function returns.  This allows for nested forwarding/scoping
            using the same argument list, with no extra allocations.  We always
            enable it here, since it's a free optimization for downstream code
            that makes use of it. */
            struct python {
                static constexpr size_t n_partial_keywords = 0;

                template <typename P, typename... A>
                static Params<std::vector<Param>> key(
                    std::vector<Param>& out,
                    size_t hash,
                    P&& parts,
                    A&&... args
                ) {
                    return {
                        .value = std::move(out),
                        .hash = hash
                    };
                }

                template <typename P, typename... A>
                static Object operator()(
                    P&& parts,
                    PyObject** array,
                    size_t idx,
                    PyObject* kwnames,
                    size_t kw_idx,
                    PyObject* func,
                    A&&... args
                ) {
                    try {
                        if constexpr (pos_pack_idx<A...> < sizeof...(A)) {
                            auto& pack = impl::unpack_arg<pos_pack_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            pack.validate();
                        }
                        if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                            auto& pack = impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            pack.validate();
                        }
                    } catch(...) {
                        for (size_t i = 0; i < idx; ++i) {
                            Py_DECREF(array[i]);
                        }
                        throw;
                    }
                    PyObject* result = PyObject_Vectorcall(
                        func,
                        array - 1,  // account for vectorcall offset
                        (idx - kw_idx) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    );
                    for (size_t i = 0; i < idx; ++i) {
                        Py_DECREF(array[i]);
                    }
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return reinterpret_steal<Object>(result);
                }
            };
        };
        template <size_t I, size_t J, size_t K>
            requires (
                I < Signature::n &&
                (K < Partial::n && Partial::template rfind<K> == I)
            )
        struct call<I, J, K> {  // insert partial argument(s)
            template <size_t K2>
            static constexpr size_t consecutive = 0;
            template <size_t K2>
                requires (K2 < Partial::n && Partial::template rfind<K2> == I)
            static constexpr size_t consecutive<K2> = consecutive<K2 + 1> + 1;

            template <typename P, typename... A>
            static auto bind(
                P&& parts,
                A&&... args
            ) {
                using T = Signature::at<I>;
                if constexpr (ArgTraits<T>::args()) {
                    constexpr size_t transition = Signature<Return(A...)>::kw_idx;
                    return []<size_t... Prev, size_t... Ks, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Ks...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&&... args
                    ) {
                        return call<
                            I + 1,
                            transition + consecutive<K>,
                            K + consecutive<K>
                        >::bind(
                            std::forward<decltype(parts)>(parts),
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)
                            )...,
                            std::forward<decltype(parts)>(
                                parts
                            ).template get<K + Ks>()...,
                            impl::unpack_arg<transition + Next>(
                                std::forward<decltype(args)>(args)
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consecutive<K>>{},
                        std::make_index_sequence<sizeof...(A) - transition>{},
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return []<size_t... Prev, size_t... Ks, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Ks...>,
                        auto&& parts,
                        auto&&... args
                    ) {
                        return call<
                            I + 1,
                            sizeof...(A) + consecutive<K>,
                            K + consecutive<K>
                        >::bind(
                            std::forward<decltype(parts)>(parts),
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)
                            )...,
                            arg<Partial::template name<K + Ks>> =
                                std::forward<decltype(parts)>(
                                    parts
                                ).template get<K + Ks>()...,
                            impl::unpack_arg<J + Next>(
                                std::forward<decltype(args)>(args)
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<consecutive<K>>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );

                } else {
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&&... args
                    ) {
                        using bound = std::tuple_element_t<K, Tuple>;
                        using orig = unpack_type<
                            bound::partial_idx,
                            Parts...
                        >;
                        // demote keywords in the original partial into
                        // positional arguments in the new partial if the next
                        // source arg is positional and the target arg can be
                        // both positional or keyword
                        if constexpr (!ArgTraits<orig>::kw() && !(
                            (J < Signature<Return(A...)>::kw_idx) &&
                            (ArgTraits<T>::pos() && ArgTraits<T>::kw())
                        )) {
                            return call<I + 1, J + 1, K + 1>::bind(
                                std::forward<decltype(parts)>(parts),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)
                                )...,
                                arg<ArgTraits<orig>::name> =
                                    std::forward<decltype(parts)>(
                                        parts
                                    ).template get<K>(),
                                impl::unpack_arg<J + Next>(
                                    std::forward<decltype(args)>(args)
                                )...
                            );
                        } else {
                            return call<I + 1, J + 1, K + 1>::bind(
                                std::forward<decltype(parts)>(parts),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)
                                )...,
                                std::forward<decltype(parts)>(
                                    parts
                                ).template get<K>(),
                                impl::unpack_arg<J + Next>(
                                    std::forward<decltype(args)>(args)
                                )...
                            );
                        }
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                }
            }

            struct cpp {
                template <typename P, typename D, typename F, typename... A>
                static constexpr std::invoke_result_t<F, Args...> operator()(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    using T = Signature::at<I>;

                    if constexpr (ArgTraits<T>::args()) {
                        constexpr size_t transition = std::min(
                            Signature<Return(A...)>::kw_idx,
                            kw_pack_idx<A...>
                        );
                        return []<size_t... Prev, size_t... Next>(
                            std::index_sequence<Prev...>,
                            std::index_sequence<Next...>,
                            auto&& parts,
                            auto&& defaults,
                            auto&& func,
                            auto&&... args
                        ) {
                            return typename call<
                                I + 1,
                                J + 1,
                                K + consecutive<K>
                            >::cpp{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                std::forward<decltype(func)>(func),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                to_arg<I>(variadic_positional(
                                    std::forward<decltype(parts)>(parts),
                                    std::forward<decltype(args)>(args)...
                                )),
                                impl::unpack_arg<transition + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...
                            );
                        }(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - transition>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );

                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return []<size_t... Prev>(
                            std::index_sequence<Prev...>,
                            auto&& parts,
                            auto&& defaults,
                            auto&& func,
                            auto&&... args
                        ) {
                            return typename call<
                                I + 1,
                                J + 1,
                                K + consecutive<K>
                            >::cpp{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                std::forward<decltype(func)>(func),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                to_arg<I>(variadic_keywords(
                                    std::forward<decltype(parts)>(parts),
                                    std::forward<decltype(args)>(args)...
                                ))
                            );
                        }(
                            std::make_index_sequence<J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );

                    } else {
                        return []<size_t... Prev, size_t... Next>(
                            std::index_sequence<Prev...>,
                            std::index_sequence<Next...>,
                            auto&& parts,
                            auto&& defaults,
                            auto&& func,
                            auto&&... args
                        ) {
                            return typename call<I + 1, J + 1, K + 1>::cpp{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                std::forward<decltype(func)>(func),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                to_arg<I>(std::forward<decltype(parts)>(
                                    parts
                                ).template get<K>()),
                                impl::unpack_arg<J + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...
                            );
                        }(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );
                    }
                }

            private:

                template <size_t J2, typename T, typename... A>
                static void _variadic_positional(
                    std::vector<T>& out,
                    A&&... args
                ) {
                    if constexpr (J2 == pos_pack_idx<A...>) {
                        auto& pack = impl::unpack_arg<J2>(std::forward<A>(args)...);
                        out.insert(out.end(), pack.begin(), pack.end());
                    } else {
                        out.emplace_back(impl::unpack_arg<J2>(
                            std::forward<A>(args)...
                        ));
                    }
                }

                template <typename P, typename... A>
                static auto variadic_positional(P&& parts, A&&... args) {
                    using T = Signature::at<I>;
                    constexpr size_t transition = std::min(
                        Signature<Return(A...)>::kw_idx,
                        kw_pack_idx<A...>
                    );
                    constexpr size_t diff = J < transition ? transition - J : 0;

                    // allocate variadic positional array
                    using vec = std::vector<typename ArgTraits<T>::type>;
                    vec out;
                    if constexpr (diff && pos_pack_idx<A...> < sizeof...(A)) {
                        out.reserve(
                            consecutive<K> +
                            (diff - 1) +
                            impl::unpack_arg<pos_pack_idx<A...>>(
                                std::forward<A>(args)...
                            ).size()
                        );
                    } else {
                        out.reserve(consecutive<K> + diff);
                    }

                    // consume partial args
                    []<size_t... Ks>(
                        std::index_sequence<Ks...>,
                        vec& out,
                        auto&& parts
                    ) {
                        (out.emplace_back(std::forward<decltype(parts)>(
                            parts
                        ).template get<K + Ks>()), ...);
                    }(
                        std::make_index_sequence<consecutive<K>>{},
                        out,
                        std::forward<P>(parts)
                    );

                    // consume source args + parameter packs
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        vec& out,
                        auto&&... args
                    ) {
                        (_variadic_positional<J + Js>(
                            out,
                            std::forward<decltype(args)>(args)...
                        ), ...);
                    }(
                        std::make_index_sequence<diff>{},
                        out,
                        std::forward<A>(args)...
                    );
                    return out;
                }

                template <size_t J2, typename T, typename... A>
                static void _variadic_keywords(
                    std::unordered_map<std::string, T>& out,
                    A&&... args
                ) {
                    if constexpr (J2 == kw_pack_idx<A...>) {
                        auto& pack = impl::unpack_arg<J2>(std::forward<A>(args)...);
                        auto it = pack.begin();
                        auto end = pack.end();
                        while (it != end) {
                            // postfix ++ required to increment before invalidation
                            auto node = pack.extract(it++);
                            auto rc = out.insert(node);
                            if (!rc.inserted) {
                                throw TypeError(
                                    "duplicate value for parameter '" +
                                    node.key() + "'"
                                );
                            }
                        }
                    } else {
                        out.emplace(
                            ArgTraits<impl::unpack_type<J2, A...>>::name,
                            impl::unpack_arg<J2>(std::forward<A>(args)...)
                        );
                    }
                }

                template <typename P, typename... A>
                static auto variadic_keywords(P&& parts, A&&... args) {
                    using T = Signature::at<I>;
                    constexpr size_t diff = Source::n - J;

                    // allocate variadic keyword map
                    using map = std::unordered_map<
                        std::string,
                        typename ArgTraits<T>::type
                    >;
                    map out;
                    if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                        out.reserve(
                            consecutive<K> +
                            (diff - 1) +
                            impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            ).size()
                        );
                    } else {
                        out.reserve(consecutive<K> + diff);
                    }

                    // consume partial kwargs
                    []<size_t... Ks>(
                        std::index_sequence<Ks...>,
                        map& out,
                        auto&& parts
                    ) {
                        (out.emplace(
                            Partial::template name<K + Ks>,
                            std::forward<decltype(parts)>(
                                parts
                            ).template get<K + Ks>()
                        ), ...);
                    }(
                        std::make_index_sequence<consecutive<K>>{},
                        out,
                        std::forward<P>(parts)
                    );

                    // consume source kwargs + parameter packs
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        map& out,
                        auto&&... args
                    ) {
                        (_variadic_keywords<J + Js>(
                            out,
                            std::forward<decltype(args)>(args)...
                        ), ...);
                    }(
                        std::make_index_sequence<diff>{},
                        out,
                        std::forward<A>(args)...
                    );
                    return out;
                }
            };

            struct python {
                static constexpr size_t n_partial_keywords = [] {
                    constexpr size_t next = call<
                        I + 1,
                        J,
                        K + consecutive<K>
                    >::python::n_partial_keywords;
                    if constexpr (
                        ArgTraits<Signature::at<I>>::kwonly() ||
                        ArgTraits<Signature::at<I>>::kwargs()
                    ) {
                        return next + consecutive<K>;
                    } else if constexpr (ArgTraits<Signature::at<I>>::kw()) {
                        return next + (J >= Source::kw_idx);
                    } else {
                        return next;
                    }
                }();

                template <typename P, typename... A>
                static Params<std::vector<Param>> key(
                    std::vector<Param>& out,
                    size_t hash,
                    P&& parts,
                    A&&... args
                ) {
                    using T = Signature::at<I>;
                    if constexpr (
                        ArgTraits<T>::kwargs() ||
                        ArgTraits<T>::kwonly() ||
                        (
                            ArgTraits<T>::kw() &&
                            pos_pack_idx<A...> >= sizeof...(A) &&
                            J >= std::min(
                                Signature<Return(A...)>::kw_idx,
                                kw_pack_idx<A...>
                            )
                        )
                    ) {
                        out.emplace_back(
                            Partial::template name<K>,
                            to_python(
                                std::forward<P>(parts).template get<K>()
                            ),
                            impl::ArgKind::KW
                        );
                    } else {
                        out.emplace_back(
                            "",
                            to_python(
                                std::forward<P>(parts).template get<K>()
                            ),
                            impl::ArgKind::POS
                        );
                    }
                    return call<
                        I + !ArgTraits<T>::variadic(),
                        J,
                        K + 1
                    >::python::key(
                        out,
                        impl::hash_combine(
                            hash,
                            out.back().hash(
                                Overloads::seed,
                                Overloads::prime
                            )),
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                }

                template <typename P, typename... A>
                static Object operator()(
                    P&& parts,
                    PyObject** array,
                    size_t idx,
                    PyObject* kwnames,
                    size_t kw_idx,
                    PyObject* func,
                    A&&... args
                ) {
                    using T = Signature::at<I>;
                    try {
                        array[idx] = release(to_python(
                            std::forward<P>(parts).template get<K>()
                        ));
                        ++idx;
                        if constexpr (
                            ArgTraits<T>::kwargs() ||
                            ArgTraits<T>::kwonly() ||
                            (
                                ArgTraits<T>::kw() &&
                                pos_pack_idx<A...> >= sizeof...(A) &&
                                J >= std::min(
                                    Signature<Return(A...)>::kw_idx,
                                    kw_pack_idx<A...>
                                )
                            )
                        ) {
                            PyTuple_SET_ITEM(
                                kwnames,
                                kw_idx,
                                release(
                                    template_string<Partial::template name<K>>()
                                )
                            );
                            ++kw_idx;
                        }
                    } catch (...) {
                        for (size_t i = 0; i < idx; ++i) {
                            Py_DECREF(array[i]);
                        }
                        throw;
                    }
                    return typename call<
                        I + !ArgTraits<T>::variadic(),
                        J,
                        K + 1
                    >::python{}(
                        std::forward<P>(parts),
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        func,
                        std::forward<A>(args)...
                    );
                }
            };
        };
        template <size_t I, size_t J, size_t K>
            requires (
                I < Signature::n &&
                !(K < Partial::n && Partial::template rfind<K> == I)
            )
        struct call<I, J, K> {  // forward source argument(s) or default value
            template <typename... A>
            static constexpr void assert_no_keyword_conflict(A&&... args) {
                constexpr StaticStr name = ArgTraits<Signature::at<I>>::name;
                if constexpr (!name.empty() && kw_pack_idx<A...> < sizeof...(A)) {
                    auto& pack = impl::unpack_arg<kw_pack_idx<A...>>(
                        std::forward<A>(args)...
                    );
                    auto node = pack.extract(name);
                    if (node) {
                        throw TypeError(
                            "conflicting value for parameter '" + name +
                            "' at index " + std::to_string(I)
                        );
                    }
                }
            }

            template <typename P, typename... A>
            static auto bind(
                P&& parts,
                A&&... args
            ) {
                using T = Signature::at<I>;
                if constexpr (ArgTraits<T>::args()) {
                    /// TODO: implement

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    /// TODO: implement

                } else {
                    return call<I + 1, J + 1, K>::bind(
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                }
            }

            struct cpp {
                template <typename P, typename D, typename F, typename... A>
                static constexpr std::invoke_result_t<F, Args...> operator()(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    using T = Signature::at<I>;
                    constexpr StaticStr name = ArgTraits<T>::name;
                    constexpr size_t pos_range = std::min({
                        pos_pack_idx<A...>,
                        Signature<Return(A...)>::kw_idx,
                        kw_pack_idx<A...>
                    });

                    // positional-only
                    if constexpr (ArgTraits<T>::posonly()) {
                        assert_no_keyword_conflict(std::forward<A>(args)...);
                        if constexpr (J < pos_range) {
                            return typename call<I + 1, J + 1, K>::cpp{}(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (J == pos_pack_idx<A...>) {
                            auto& pack = impl::unpack_arg<J>(std::forward<A>(args)...);
                            if (pack.has_value()) {
                                return insert_from_pos_pack(
                                    std::forward<P>(parts),
                                    std::forward<D>(defaults),
                                    std::forward<F>(func),
                                    std::forward<A>(args)...
                                );
                            } else {
                                return remove(
                                    std::forward<P>(parts),
                                    std::forward<D>(defaults),
                                    std::forward<F>(func),
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return insert_default(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (name.empty()) {
                            throw TypeError(
                                "no match for positional-only parameter at "
                                "index " + std::to_string(I)
                            );
                        } else {
                            throw TypeError(
                                "no match for positional-only parameter '" +
                                name + "' at index " + std::to_string(I)
                            );
                        }

                    // positional-or-keyword
                    } else if constexpr (ArgTraits<T>::pos()) {
                        if constexpr (J < pos_range) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            return typename call<I + 1, J + 1, K>::cpp{}(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (J == pos_pack_idx<A...>) {
                            auto& pack = impl::unpack_arg<J>(std::forward<A>(args)...);
                            if (pack.has_value()) {
                                assert_no_keyword_conflict(std::forward<A>(args)...);
                                return insert_from_pos_pack(
                                    std::forward<P>(parts),
                                    std::forward<D>(defaults),
                                    std::forward<F>(func),
                                    std::forward<A>(args)...
                                );
                            } else {
                                return remove(
                                    std::forward<P>(parts),
                                    std::forward<D>(defaults),
                                    std::forward<F>(func),
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (Signature<Return(A...)>::template has<name>) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            return reorder(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                            auto& pack = impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            auto node = pack.extract(name);
                            if (node) {
                                return insert_from_kw_pack(
                                    node,
                                    std::forward<P>(parts),
                                    std::forward<D>(defaults),
                                    std::forward<F>(func),
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return insert_default(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                        throw TypeError(
                            "no match for parameter '" + name + "' at index " +
                            std::to_string(I)
                        );

                    // keyword-only
                    } else if constexpr (ArgTraits<T>::kw()) {
                        if constexpr (Signature<Return(A...)>::template has<name>) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            return reorder(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                            auto& pack = impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            auto node = pack.extract(name);
                            if (node) {
                                return insert_from_kw_pack(
                                    node,
                                    std::forward<P>(parts),
                                    std::forward<D>(defaults),
                                    std::forward<F>(func),
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return insert_default(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                std::forward<A>(args)...
                            );
                        }
                        throw TypeError(
                            "no match for keyword-only parameter '" + name +
                            "' at index " + std::to_string(I)
                        );

                    // variadic positional args
                    } else if constexpr (ArgTraits<T>::args()) {
                        constexpr size_t transition = std::min(
                            Signature<Return(A...)>::kw_idx,
                            kw_pack_idx<A...>
                        );
                        constexpr size_t idx = std::max(J, transition);
                        return []<size_t... Prev, size_t... Next>(
                            std::index_sequence<Prev...>,
                            std::index_sequence<Next...>,
                            auto&& parts,
                            auto&& defaults,
                            auto&& func,
                            auto&&... args
                        ) {
                            return typename call<I + 1, transition, K>::cpp{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                std::forward<decltype(func)>(func),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                to_arg<I>(variadic_positional(
                                    std::forward<decltype(args)>(args)...
                                )),
                                impl::unpack_arg<idx + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...
                            );
                        }(
                            std::make_index_sequence<J>{},
                            std::make_index_sequence<sizeof...(A) - idx>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );

                    // variadic keyword args
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return []<size_t... Prev>(
                            std::index_sequence<Prev...>,
                            auto&& parts,
                            auto&& defaults,
                            auto&& func,
                            auto&&... args
                        ) {
                            return typename call<I + 1, J + 1, K>::cpp{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                std::forward<decltype(func)>(func),
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                to_arg<I>(variadic_keyword(
                                    std::forward<decltype(args)>(args)...
                                ))
                            );
                        }(
                            std::make_index_sequence<J>{},
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            std::forward<A>(args)...
                        );

                    } else {
                        static_assert(false, "invalid argument kind");
                        std::unreachable();
                    }
                }

            private:

                template <typename P, typename D, typename F, typename... A>
                static constexpr decltype(auto) reorder(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    constexpr StaticStr name = ArgTraits<Signature::at<I>>::name;
                    constexpr size_t idx = Signature<Return(A...)>::template idx<name>;
                    return []<size_t... Prev, size_t... Next, size_t... Last>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        std::index_sequence<Last...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        return typename call<I + 1, J + 1, K>::cpp{}(
                            std::forward<decltype(parts)>(parts),
                            std::forward<decltype(defaults)>(defaults),
                            std::forward<decltype(func)>(func),
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            to_arg<I>(impl::unpack_arg<idx>(
                                std::forward<decltype(args)>(args)...
                            )),
                            impl::unpack_arg<J + Next>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            impl::unpack_arg<idx + 1 + Last>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<idx - J>{},
                        std::make_index_sequence<sizeof...(A) - (idx + 1)>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                }

                template <typename P, typename D, typename F, typename... A>
                static constexpr decltype(auto) insert_default(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        return typename call<I + 1, J + 1, K>::cpp{}(
                            std::forward<decltype(parts)>(parts),
                            std::forward<decltype(defaults)>(defaults),
                            std::forward<decltype(func)>(func),
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            to_arg<I>(std::forward<decltype(defaults)>(
                                defaults
                            ).template get<Defaults::template find<I>>()),
                            impl::unpack_arg<J + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                }

                template <typename P, typename D, typename F, typename... A>
                static constexpr decltype(auto) insert_from_pos_pack(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        auto& pack = impl::unpack_arg<pos_pack_idx<A...>>(
                            std::forward<A>(args)...
                        );
                        return typename call<I + 1, J + 1, K>::cpp{}(
                            std::forward<decltype(parts)>(parts),
                            std::forward<decltype(defaults)>(defaults),
                            std::forward<decltype(func)>(func),
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            to_arg<I>(pack.value()),
                            impl::unpack_arg<J + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                }

                template <typename P, typename D, typename F, typename... A>
                static constexpr decltype(auto) insert_from_kw_pack(
                    auto&& node,
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    return []<size_t... Prev, size_t... Next>(
                        auto&& node,
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        if constexpr (std::is_lvalue_reference_v<
                            typename ArgTraits<Signature::at<I>>::type
                        >) {
                            return typename call<I + 1, J + 1, K>::cpp{}(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                impl::unpack_arg<Prev>(std::forward<A>(args)...)...,
                                to_arg<I>(node.mapped()),
                                impl::unpack_arg<J + Next>(std::forward<A>(args)...)...
                            );
                        } else {
                            return typename call<I + 1, J + 1, K>::cpp{}(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                impl::unpack_arg<Prev>(std::forward<A>(args)...)...,
                                to_arg<I>(std::move(node.mapped())),
                                impl::unpack_arg<J + Next>(std::forward<A>(args)...)...
                            );
                        }
                    }(
                        std::forward<decltype(node)>(node),
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - J>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                }

                template <typename P, typename D, typename F, typename... A>
                static constexpr decltype(auto) remove(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    A&&... args
                ) {
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        auto&& defaults,
                        auto&& func,
                        auto&&... args
                    ) {
                        return typename call<I, J, K>::cpp{}(
                            std::forward<decltype(parts)>(parts),
                            std::forward<decltype(defaults)>(defaults),
                            std::forward<decltype(func)>(func),
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            impl::unpack_arg<J + 1 + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                }

                template <size_t J2, typename T, typename... A>
                static void _variadic_positional(
                    std::vector<T>& out,
                    A&&... args
                ) {
                    if constexpr (J2 == pos_pack_idx<A...>) {
                        auto& pack = impl::unpack_arg<J2>(std::forward<A>(args)...);
                        out.insert(out.end(), pack.begin(), pack.end());
                    } else {
                        out.emplace_back(impl::unpack_arg<J2>(
                            std::forward<A>(args)...
                        ));
                    }
                }

                template <typename... A>
                static auto variadic_positional(A&&... args) {
                    using T = Signature::at<I>;
                    constexpr size_t transition = std::min(
                        Signature<Return(A...)>::kw_idx,
                        kw_pack_idx<A...>
                    );
                    constexpr size_t diff = J < transition ? transition - J : 0;

                    // allocate variadic positional array
                    std::vector<typename ArgTraits<T>::type> out;
                    if constexpr (diff) {
                        if constexpr (pos_pack_idx<A...> < sizeof...(A)) {
                            out.reserve(
                                (diff - 1) +
                                impl::unpack_arg<pos_pack_idx<A...>>(
                                    std::forward<A>(args)...
                                ).size()
                            );
                        } else {
                            out.reserve(diff);
                        }
                    }

                    // consume source args + parameter packs
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        std::vector<typename ArgTraits<T>::type>& out,
                        auto&&... args
                    ) {
                        (_variadic_positional<J + Js>(
                            out,
                            std::forward<decltype(args)>(args)...
                        ), ...);
                    }(
                        std::make_index_sequence<diff>{},
                        out,
                        std::forward<A>(args)...
                    );
                    return out;
                }

                template <size_t J2, typename T, typename... A>
                static void _variadic_keywords(
                    std::unordered_map<std::string, T>& out,
                    A&&... args
                ) {
                    if constexpr (J2 == kw_pack_idx<A...>) {
                        auto& pack = impl::unpack_arg<J2>(std::forward<A>(args)...);
                        auto it = pack.begin();
                        auto end = pack.end();
                        while (it != end) {
                            // postfix ++ required to increment before invalidation
                            auto node = pack.extract(it++);
                            auto rc = out.insert(node);
                            if (!rc.inserted) {
                                throw TypeError(
                                    "duplicate value for parameter '" +
                                    node.key() + "'"
                                );
                            }
                        }
                    } else {
                        out.emplace(
                            ArgTraits<impl::unpack_type<J2, A...>>::name,
                            impl::unpack_arg<J2>(std::forward<A>(args)...)
                        );
                    }
                }

                template <typename... A>
                static auto variadic_keywords(A&&... args) {
                    using T = Signature::at<I>;
                    constexpr size_t diff = Source::n - J;

                    // allocate variadic keyword map
                    using map = std::unordered_map<
                        std::string,
                        typename ArgTraits<T>::type
                    >;
                    map out;
                    if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                        out.reserve(
                            (diff - 1) +
                            impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            ).size()
                        );
                    } else {
                        out.reserve(diff);
                    }

                    // consume source kwargs + parameter packs
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        map& out,
                        auto&&... args
                    ) {
                        (_variadic_keywords<J + Js>(
                            out,
                            std::forward<decltype(args)>(args)...
                        ), ...);
                    }(
                        std::make_index_sequence<diff>{},
                        out,
                        std::forward<A>(args)...
                    );
                    return out;
                }
            };

            struct python {
                static constexpr size_t n_partial_keywords = [] {
                    /// NOTE: this always overestimates J with respect to
                    /// optional arguments, but it doesn't matter because all
                    /// we care about is the location of J relative to the last
                    /// positional argument in the source signature, and no
                    /// missing arguments can appear before that.  Thus, any
                    /// subsequent positional-or-keyword arguments with partial
                    /// values will be promoted to keywords in order to allow
                    /// Python to insert the correct defaults, without needing
                    /// to manually specify them here.
                    if constexpr (J == Source::args_idx) {
                        return call<
                            std::min(
                                Signature::kwonly_idx,
                                Signature::kwargs_idx
                            ),
                            J + 1,
                            K
                        >::python::n_partial_keywords;
                    } else {
                        return call<
                            I + 1,
                            J + 1,
                            K
                        >::python::n_partial_keywords;
                    }
                }();

                template <typename P, typename... A>
                static Params<std::vector<Param>> key(
                    std::vector<Param>& out,
                    size_t hash,
                    P&& parts,
                    A&&... args
                ) {
                    using T = Signature::at<I>;
                    constexpr StaticStr name = ArgTraits<T>::name;
                    constexpr size_t pos_range = std::min({
                        pos_pack_idx<A...>,
                        Signature<Return(A...)>::kw_idx,
                        kw_pack_idx<A...>
                    });

                    // positional-only
                    if constexpr (ArgTraits<T>::posonly()) {
                        assert_no_keyword_conflict(std::forward<A>(args)...);
                        if constexpr (J < pos_range) {
                            out.emplace_back(
                                "",
                                to_python(
                                    impl::unpack_arg<J>(std::forward<A>(args)...)
                                ),
                                impl::ArgKind::POS
                            );
                            return call<I + 1, J + 1, K>::python::key(
                                out,
                                impl::hash_combine(
                                    hash,
                                    out.back().hash(
                                        Overloads::seed,
                                        Overloads::prime)
                                ),
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (J == pos_pack_idx<A...>) {
                            auto& pack = impl::unpack_arg<J>(std::forward<A>(args)...);
                            if (pack.has_value()) {
                                out.emplace_back(
                                    "",
                                    to_python(pack.value()),
                                    impl::ArgKind::POS
                                );
                                return call<I + 1, J, K>::python::key(
                                    out,
                                    impl::hash_combine(
                                        hash,
                                        out.back().hash(
                                            Overloads::seed,
                                            Overloads::prime
                                        )
                                    ),
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            } else {
                                return key_remove(
                                    out,
                                    hash,
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::python::key(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (name.empty()) {
                            throw TypeError(
                                "no match for positional-only parameter at "
                                "index " + std::to_string(I)
                            );
                        } else {
                            throw TypeError(
                                "no match for positional-only parameter '" +
                                name + "' at index " + std::to_string(I)
                            );
                        }

                    // positional-or-keyword
                    } else if constexpr (ArgTraits<T>::pos()) {
                        if constexpr (J < pos_range) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            out.emplace_back(
                                "",
                                to_python(
                                    impl::unpack_arg<J>(std::forward<A>(args)...)
                                ),
                                impl::ArgKind::POS
                            );
                            return call<I + 1, J, K>::python::key(
                                out,
                                impl::hash_combine(
                                    hash,
                                    out.back().hash(
                                        Overloads::seed,
                                        Overloads::prime
                                    )
                                ),
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (J == pos_pack_idx<A...>) {
                            auto& pack = impl::unpack_arg<J>(std::forward<A>(args)...);
                            if (pack.has_value()) {
                                out.emplace_back(
                                    "",
                                    to_python(pack.value()),
                                    impl::ArgKind::POS
                                );
                                return call<I + 1, J, K>::python::key(
                                    out,
                                    impl::hash_combine(
                                        hash,
                                        out.back().hash(
                                            Overloads::seed,
                                            Overloads::prime
                                        )
                                    ),
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            } else {
                                return key_remove(
                                    out,
                                    hash,
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (Signature<Return(A...)>::template hash<name>) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            return key_reorder(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                            auto& pack = impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            auto node = pack.extract(name);
                            if (node) {
                                out.emplace_back(
                                    ArgTraits<T>::name,
                                    to_python(std::move(node.mapped())),
                                    impl::ArgKind::KW
                                );
                                return call<I + 1, J, K>::python::key(
                                    out,
                                    impl::hash_combine(
                                        hash,
                                        out.back().hash(
                                            Overloads::seed,
                                            Overloads::prime
                                        )
                                    ),
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::python::key(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                        throw TypeError(
                            "no match for parameter '" + name + "' at index " +
                            std::to_string(I)
                        );

                    // keyword-only
                    } else if constexpr (ArgTraits<T>::kw()) {
                        if constexpr (Signature<Return(A...)>::template has<name>) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            return key_reorder(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                            auto& pack = impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            auto node = pack.extract(name);
                            if (node) {
                                out.emplace_back(
                                    ArgTraits<T>::name,
                                    to_python(std::move(node.mapped())),
                                    impl::ArgKind::KW
                                );
                                return call<I + 1, J, K>::python::key(
                                    out,
                                    impl::hash_combine(
                                        hash,
                                        out.back().hash(
                                            Overloads::seed,
                                            Overloads::prime
                                        )
                                    ),
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::python::key(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                        throw TypeError(
                            "no match for keyword-only parameter '" + name +
                            "' at index " + std::to_string(I)
                        );

                    // variadic positional
                    } else if constexpr (ArgTraits<T>::args()) {
                        constexpr size_t transition = std::min(
                            Signature<Return(A...)>::kw_idx,
                            kw_pack_idx<A...>
                        );
                        return []<size_t... Js>(
                            std::index_sequence<Js...>,
                            std::vector<Param>& out,
                            size_t hash,
                            auto&& parts,
                            auto&&... args
                        ) {
                            (key_variadic_positional<J + Js>(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            ), ...);
                            return call<
                                I + 1,
                                std::max(J, transition),
                                K
                            >::python::key(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }(
                            std::make_index_sequence<
                                J < transition ? transition - J : 0
                            >{},
                            out,
                            hash,
                            std::forward<P>(parts),
                            std::forward<A>(args)...
                        );

                    // variadic keyword
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return []<size_t... Js>(
                            std::index_sequence<Js...>,
                            std::vector<Param>& out,
                            size_t hash,
                            auto&& parts,
                            auto&&... args
                        ) {
                            (key_variadic_keywords<J + Js>(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            ), ...);
                            return call<
                                I + 1,
                                J + 1,
                                K
                            >::python::key(
                                out,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }(
                            std::make_index_sequence<sizeof...(A) - J>{},
                            out,
                            hash,
                            std::forward<P>(parts),
                            std::forward<A>(args)...
                        );

                    } else {
                        static_assert(false, "invalid argument kind");
                        std::unreachable();
                    }
                }

                template <typename P, typename... A>
                static Object operator()(
                    P&& parts,
                    PyObject** array,
                    size_t idx,
                    PyObject* kwnames,
                    size_t kw_idx,
                    PyObject* func,
                    A&&... args
                ) {
                    using T = Signature::at<I>;
                    constexpr StaticStr name = ArgTraits<T>::name;
                    constexpr size_t pos_range = std::min({
                        pos_pack_idx<A...>,
                        Signature<Return(A...)>::kw_idx,
                        kw_pack_idx<A...>
                    });

                    // positional-only
                    if constexpr (ArgTraits<T>::posonly()) {
                        assert_no_keyword_conflict(std::forward<A>(args)...);
                        if constexpr (J < pos_range) {
                            call_insert(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                std::forward<P>(parts).template get<K>()
                            );
                            return typename call<I + 1, J + 1, K>::python{}(
                                std::forward<P>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (J == pos_pack_idx<A...>) {
                            auto& pack = impl::unpack_arg<J>(std::forward<A>(args)...);
                            if (pack.has_value()) {
                                call_insert(
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    pack.value()
                                );
                                return typename call<I + 1, J, K>::python{}(
                                    std::forward<P>(parts),
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    func,
                                    std::forward<A>(args)...
                                );
                            } else {
                                return call_remove(
                                    std::forward<P>(parts),
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    func,
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return typename call<I + 1, J, K>::python{}(
                                std::forward<P>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (name.empty()) {
                            throw TypeError(
                                "no match for positional-only parameter at "
                                "index " + std::to_string(I)
                            );
                        } else {
                            throw TypeError(
                                "no match for positional-only parameter '" +
                                name + "' at index " + std::to_string(I)
                            );
                        }

                    // positional-or-keyword
                    } else if constexpr (ArgTraits<T>::pos()) {
                        if constexpr (J < pos_range) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            call_insert(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                std::forward<P>(parts).template get<K>()
                            );
                            return typename call<I + 1, J + 1, K>::python{}(
                                std::forward<P>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (J == pos_pack_idx<A...>) {
                            auto& pack = impl::unpack_arg<J>(std::forward<A>(args)...);
                            if (pack.has_value()) {
                                assert_no_keyword_conflict(std::forward<A>(args)...);
                                call_insert(
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    pack.value()
                                );
                                return typename call<I + 1, J, K>::python{}(
                                    std::forward<P>(parts),
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    func,
                                    std::forward<A>(args)...
                                );
                            } else {
                                return call_remove(
                                    std::forward<P>(parts),
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    func,
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (Signature<Return(A...)>::template has<name>) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            return call_reorder(
                                std::forward<P>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                            auto& pack = impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            auto node = pack.extract(name);
                            if (node) {
                                call_insert(
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    std::move(node.mapped())
                                );
                                return typename call<I + 1, J, K>::python{}(
                                    std::forward<P>(parts),
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    func,
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return typename call<I + 1, J, K>::python{}(
                                std::forward<P>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }
                        throw TypeError(
                            "no match for parameter '" + name + "' at index " +
                            std::to_string(I)
                        );

                    // keyword-only
                    } else if constexpr (ArgTraits<T>::kw()) {
                        if constexpr (Signature<Return(A...)>::template has<name>) {
                            assert_no_keyword_conflict(std::forward<A>(args)...);
                            return call_reorder(
                                std::forward<P>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }
                        if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                            auto& pack = impl::unpack_arg<kw_pack_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            auto node = pack.extract(name);
                            if (node) {
                                call_insert(
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    std::move(node.mapped())
                                );
                                return typename call<I + 1, J, K>::python{}(
                                    std::forward<P>(parts),
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    func,
                                    std::forward<A>(args)...
                                );
                            }
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return typename call<I + 1, J, K>::python{}(
                                std::forward<P>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }
                        throw TypeError(
                            "no match for keyword-only parameter '" + name +
                            "' at index " + std::to_string(I)
                        );

                    // variadic positional args
                    } else if constexpr (ArgTraits<T>::args()) {
                        constexpr size_t transition = std::min(
                            Signature<Return(A...)>::kw_idx,
                            kw_pack_idx<A...>
                        );
                        return []<size_t...  Js>(
                            std::index_sequence<Js...>,
                            auto&& parts,
                            PyObject** array,
                            size_t idx,
                            PyObject* kwnames,
                            size_t kw_idx,
                            PyObject* func,
                            auto&&... args
                        ) {
                            (call_variadic_positional<J + Js>(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                std::forward<decltype(args)>(args)...
                            ), ...);
                            return typename call<
                                I + 1,
                                std::max(J, transition),
                                K
                            >::python{}(
                                std::forward<decltype(parts)>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }(
                            std::make_index_sequence<
                                J < transition ? transition - J : 0
                            >{},
                            std::forward<P>(parts),
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            func,
                            std::forward<A>(args)...
                        );

                    // variadic keyword args
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return []<size_t... Js>(
                            std::index_sequence<Js...>,
                            auto&& parts,
                            PyObject** array,
                            size_t idx,
                            PyObject* kwnames,
                            size_t kw_idx,
                            PyObject* func,
                            auto&&... args
                        ) {
                            (call_variadic_keywords<J + Js>(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                std::forward<decltype(args)>(args)...
                            ), ...);
                            return typename call<
                                I + 1,
                                J + 1,
                                K
                            >::python{}(
                                std::forward<decltype(parts)>(parts),
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                func,
                                std::forward<A>(args)...
                            );
                        }(
                            std::make_index_sequence<sizeof...(A) - J>{},
                            std::forward<P>(parts),
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            func,
                            std::forward<A>(args)...
                        );

                    } else {
                        static_assert(false, "invalid argument kind");
                        std::unreachable();
                    }
                }

            private:

                template <typename... A>
                static void call_insert(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    auto&& value
                ) {
                    using T = Signature::at<I>;
                    try {
                        array[idx] = release(to_python(
                            std::forward<decltype(value)>(value)
                        ));
                        ++idx;
                        if constexpr (
                            ArgTraits<T>::kwonly() ||
                            ArgTraits<T>::kwargs() ||
                            (ArgTraits<T>::kw() && J >= std::min(
                                Signature<Return(A...)>::kw_idx,
                                kw_pack_idx<A...>
                            ))
                        ) {
                            PyTuple_SET_ITEM(
                                kwnames,
                                kw_idx,
                                release(impl::template_string<ArgTraits<T>::name>())
                            );
                            ++kw_idx;
                        }
                    } catch (...) {
                        for (size_t i = 0; i < idx; ++i) {
                            Py_DECREF(array[i]);
                        }
                        throw;
                    }
                }

                template <typename... A>
                static void call_insert(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    Object name,
                    auto&& value
                ) {
                    using T = Signature::at<I>;
                    try {
                        array[idx] = release(to_python(
                            std::forward<decltype(value)>(value)
                        ));
                        ++idx;
                        if constexpr (
                            ArgTraits<T>::kwonly() ||
                            ArgTraits<T>::kwargs() ||
                            (ArgTraits<T>::kw() && J >= std::min(
                                Signature<Return(A...)>::kw_idx,
                                kw_pack_idx<A...>
                            ))
                        ) {
                            PyTuple_SET_ITEM(
                                kwnames,
                                kw_idx,
                                release(name)
                            );
                            ++kw_idx;
                        }
                    } catch (...) {
                        for (size_t i = 0; i < idx; ++i) {
                            Py_DECREF(array[i]);
                        }
                        throw;
                    }
                }

                template <typename P, typename... A>
                static Object call_reorder(
                    P&& parts,
                    PyObject** array,
                    size_t idx,
                    PyObject* kwnames,
                    size_t kw_idx,
                    PyObject* func,
                    A&&... args
                ) {
                    constexpr StaticStr name = ArgTraits<Signature::at<I>>::name;
                    constexpr size_t kw = Signature<Return(A...)>::template idx<name>;
                    return []<size_t... Prev, size_t... Next, size_t... Last>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        std::index_sequence<Last...>,
                        auto&& parts,
                        PyObject** array,
                        size_t idx,
                        PyObject* kwnames,
                        size_t kw_idx,
                        PyObject* func,
                        auto&&... args
                    ) {
                        call_insert(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            impl::unpack_arg<kw>(
                                std::forward<decltype(args)>(args)...
                            )
                        );
                        return typename call<I + 1, J + 1, K>::python{}(
                            std::forward<decltype(parts)>(parts),
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            func,
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            to_arg<I>(impl::unpack_arg<kw>(
                                std::forward<decltype(args)>(args)...
                            )),
                            impl::unpack_arg<J + Next>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            impl::unpack_arg<kw + 1 + Last>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<kw - J>{},
                        std::make_index_sequence<sizeof...(A) - (kw + 1)>{},
                        std::forward<P>(parts),
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        func,
                        std::forward<A>(args)...
                    );
                }

                template <typename P, typename... A>
                static Params<std::vector<Param>> key_reorder(
                    std::vector<Param>& out,
                    size_t hash,
                    P&& parts,
                    A&&... args
                ) {
                    constexpr StaticStr name = ArgTraits<Signature::at<I>>::name;
                    constexpr size_t kw = Signature<Return(A...)>::template idx<name>;
                    return []<size_t... Prev, size_t... Next, size_t... Last>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        std::index_sequence<Last...>,
                        std::vector<Param>& out,
                        size_t hash,
                        auto&& parts,
                        auto&&... args
                    ) {
                        out.emplace_back(
                            ArgTraits<Signature::at<I>>::name,
                            to_python(impl::unpack_arg<kw>(
                                std::forward<decltype(args)>(args)...
                            )),
                            impl::ArgKind::KW
                        );
                        return call<I + 1, J + 1, K>::python::key(
                            out,
                            impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            ),
                            std::forward<decltype(parts)>(parts),
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            to_arg<I>(impl::unpack_arg<kw>(
                                std::forward<decltype(args)>(args)...
                            )),
                            impl::unpack_arg<J + Next>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            impl::unpack_arg<kw + 1 + Last>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<kw - J>{},
                        std::make_index_sequence<sizeof...(A) - (kw + 1)>{},
                        out,
                        hash,
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                }

                template <typename P, typename... A>
                static Object call_remove(
                    P&& parts,
                    PyObject** array,
                    size_t idx,
                    PyObject* kwnames,
                    size_t kw_idx,
                    PyObject* func,
                    A&&... args
                ) {
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        auto&& parts,
                        PyObject** array,
                        size_t idx,
                        PyObject* kwnames,
                        size_t kw_idx,
                        PyObject* func,
                        auto&&... args
                    ) {
                        return typename call<I, J, K>::python{}(
                            std::forward<decltype(parts)>(parts),
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            func,
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            impl::unpack_arg<J + 1 + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                        std::forward<P>(parts),
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        func,
                        std::forward<A>(args)...
                    );
                }

                template <typename P, typename... A>
                static Params<std::vector<Param>> key_remove(
                    std::vector<Param>& out,
                    size_t hash,
                    P&& parts,
                    A&&... args
                ) {
                    return []<size_t... Prev, size_t... Next>(
                        std::index_sequence<Prev...>,
                        std::index_sequence<Next...>,
                        std::vector<Param>& out,
                        size_t hash,
                        auto&& parts,
                        auto&&... args
                    ) {
                        return call<I, J, K>::python::key(
                            out,
                            hash,
                            std::forward<decltype(parts)>(parts),
                            impl::unpack_arg<Prev>(
                                std::forward<decltype(args)>(args)...
                            )...,
                            impl::unpack_arg<J + 1 + Next>(
                                std::forward<decltype(args)>(args)...
                            )...
                        );
                    }(
                        std::make_index_sequence<J>{},
                        std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                        out,
                        hash,
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                }

                template <size_t J2, typename... A>
                static void call_variadic_positional(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    A&&... args
                ) {
                    if constexpr (J2 == pos_pack_idx<A...>) {
                        auto& pack = impl::unpack_arg<J2>(std::forward<A>(args)...);
                        for (auto& value : pack) {
                            call_insert(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                impl::template_string<"">(),
                                value
                            );
                        }
                    } else {
                        call_insert(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            impl::template_string<"">(),
                            impl::unpack_arg<J2>(std::forward<A>(args)...)
                        );
                    }
                }

                template <size_t J2, typename... A>
                static void key_variadic_positional(
                    std::vector<Param>& out,
                    size_t& hash,
                    A&&... args
                ) {
                    if constexpr (J2 == pos_pack_idx<A...>) {
                        auto& pack = impl::unpack_arg<J2>(std::forward<A>(args)...);
                        for (auto& value : pack) {
                            out.emplace_back(
                                "",
                                to_python(value),
                                impl::ArgKind::POS
                            );
                            hash = impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            );
                        }
                    } else {
                        out.emplace_back(
                            "",
                            to_python(
                                impl::unpack_arg<J2>(std::forward<A>(args)...)
                            ),
                            impl::ArgKind::POS
                        );
                        hash = impl::hash_combine(
                            hash,
                            out.back().hash(
                                Overloads::seed,
                                Overloads::prime
                            )
                        );
                    }
                }

                template <size_t J2, typename... A>
                static void call_variadic_keywords(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    A&&... args
                ) {
                    if constexpr (J2 == kw_pack_idx<A...>) {
                        auto& pack = impl::unpack_arg<J2>(std::forward<A>(args)...);
                        auto it = pack.begin();
                        auto end = pack.end();
                        while (it != end) {
                            // postfix ++ required to increment before invalidation
                            auto node = pack.extract(it++);
                            PyObject* name = PyUnicode_FromStringAndSize(
                                node.key().data(),
                                node.key().size()
                            );
                            if (name == nullptr) {
                                for (size_t i = 0; i < idx; ++i) {
                                    Py_DECREF(array[i]);
                                }
                                Exception::from_python();
                            }
                            call_insert(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                reinterpret_steal<Object>(name),
                                std::move(node.mapped())
                            );
                        }
                    } else {
                        call_insert(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            impl::template_string<
                                ArgTraits<impl::unpack_type<J2, Args...>>::name
                            >(),
                            impl::unpack_arg<J2>(std::forward<A>(args)...)
                        );
                    }
                }

                template <size_t J2, typename... A>
                static void key_variadic_keywords(
                    std::vector<Param>& out,
                    size_t& hash,
                    A&&... args
                ) {
                    if constexpr (J2 == kw_pack_idx<A...>) {
                        auto& pack = impl::unpack_arg<J2>(std::forward<A>(args)...);
                        auto it = pack.begin();
                        auto end = pack.end();
                        while (it != end) {
                            // postfix ++ required to increment before invalidation
                            auto node = pack.extract(it++);
                            out.emplace_back(
                                /// TODO: this forces the parameter list to hold
                                /// a string rather than a string_view?
                                std::move(node.key()),
                                to_python(std::move(node.mapped())),
                                impl::ArgKind::KW
                            );
                            hash = impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            );
                        }
                    } else {
                        out.emplace_back(
                            ArgTraits<Signature::at<I>>::name,
                            to_python(
                                impl::unpack_arg<J2>(std::forward<A>(args)...)
                            ),
                            impl::ArgKind::KW
                        );
                        hash = impl::hash_combine(
                            hash,
                            out.back().hash(
                                Overloads::seed,
                                Overloads::prime
                            )
                        );
                    }
                }
            };
        };

        template <bool, bool>
        struct get_signature {
            using type = decltype(call<0, 0, 0>::bind(
                std::declval<Partial>(),
                std::declval<Values...>()
            ));
        };
        template <bool has_args, bool has_kwargs> requires (has_args || has_kwargs)
        struct get_signature<has_args, has_kwargs> {
            static_assert(
                !has_args,
                "positional packs cannot be bound as partial arguments"
            );
            static_assert(
                !has_kwargs,
                "keyword packs cannot be bound as partial arguments"
            );
            using type = void;
        };

    public:
        using signature = get_signature<Source::has_args, Source::has_kwargs>::type;

        static constexpr size_t n               = sizeof...(Values);
        static constexpr size_t n_pos           = Source::n_pos;
        static constexpr size_t n_kw            = Source::n_kw;

        template <StaticStr Name>
        static constexpr bool has               = Source::template has<Name>;
        static constexpr bool has_pos           = Source::has_pos;
        static constexpr bool has_args          = Source::has_args;
        static constexpr bool has_kw            = Source::has_kw;
        static constexpr bool has_kwargs        = Source::has_kwargs;

        template <StaticStr Name> requires (has<Name>)
        static constexpr size_t idx             = Source::template idx<Name>;
        static constexpr size_t args_idx        = Source::args_idx;
        static constexpr size_t kw_idx          = Source::kw_idx;
        static constexpr size_t kwargs_idx      = Source::kwargs_idx;

        template <size_t I> requires (I < n)
        using at = Source::template at<I>;

        static constexpr bool satisfies_required_args =
            Check<Source>::satisfies_required_args;

        /* Produce an overload key from the bound C++ arguments, which can be
        used to search the overload trie and invoke a resulting function. */
        template <impl::inherits<Partial> P>
        static Params<std::vector<Param>> key(
            P&& parts,
            Values... values
        ) {
            std::vector<Param> out;
            if constexpr (Source::has_args && Source::has_kwargs) {
                out.reserve(
                    Partial::n +
                    (Source::n - 2) +
                    impl::unpack_arg<Source::args_idx>(
                        std::forward<Values>(values)...
                    ).size() +
                    impl::unpack_arg<Source::kwargs_idx>(
                        std::forward<Values>(values)...
                    ).size()
                );
            } else if constexpr (Source::has_args) {
                out.reserve(
                    Partial::n +
                    (Source::n - 1) +
                    impl::unpack_arg<Source::args_idx>(
                        std::forward<Values>(values)...
                    ).size()
                );
            } else if constexpr (Source::has_kwargs) {
                out.reserve(
                    Partial::n +
                    (Source::n - 1) +
                    impl::unpack_arg<Source::kwargs_idx>(
                        std::forward<Values>(values)...
                    ).size()
                );
            } else {
                out.reserve(
                    Partial::n +
                    Source::n
                );
            }
            return call<0, 0, 0>::python::key(
                out,
                0,
                std::forward<P>(parts),
                std::forward<Values>(values)...
            );
        }

        template <impl::inherits<Partial> P>
        static constexpr auto bind(P&& parts, Values... args) {
            return call<0, 0, 0>::bind(
                std::forward<P>(parts),
                std::forward<Values>(args)...
            );
        }

        /* Invoke a C++ function from C++ using Python-style arguments. */
        template <impl::inherits<Partial> P, impl::inherits<Defaults> D, typename F>
            requires (
                std::is_invocable_r_v<Return, F, Args...> &&
                satisfies_required_args
            )
        static constexpr Return operator()(
            P&& parts,
            D&& defaults,
            F&& func,
            Values... args
        ) {
            /// NOTE: source positional and keyword packs must be converted
            /// into PositionalPack and KeywordPack helpers, which are
            /// destructively iterated over within the call algorithm and
            /// validated empty just before calling the target function,
            /// wherein they are omitted.
            if constexpr (Source::has_args && Source::has_kwargs) {
                return []<size_t... Prev, size_t... Next>(
                    std::index_sequence<Prev...>,
                    std::index_sequence<Next...>,
                    auto&& parts,
                    auto&& defaults,
                    auto&& func,
                    auto&&... args
                ) {
                    return typename call<0, 0, 0>::cpp{}(
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(defaults)>(defaults),
                        std::forward<decltype(func)>(func),
                        impl::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        PositionalPack(impl::unpack_arg<Source::args_idx>(
                            std::forward<decltype(args)>(args)...
                        )),
                        impl::unpack_arg<Source::args_idx + 1 + Next>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        KeywordPack(impl::unpack_arg<Source::kwargs_idx>(
                            std::forward<decltype(args)>(args)...
                        ))
                    );
                }(
                    std::make_index_sequence<Source::args_idx>{},
                    std::make_index_sequence<
                        Source::kwargs_idx - (Source::args_idx + 1)
                    >{},
                    std::forward<P>(parts),
                    std::forward<D>(defaults),
                    std::forward<F>(func),
                    std::forward<Values>(args)...
                );

            } else if constexpr (Source::has_args) {
                return []<size_t... Prev, size_t... Next>(
                    std::index_sequence<Prev...>,
                    std::index_sequence<Next...>,
                    auto&& parts,
                    auto&& defaults,
                    auto&& func,
                    auto&&... args
                ) {
                    return typename call<0, 0, 0>::cpp{}(
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(defaults)>(defaults),
                        std::forward<decltype(func)>(func),
                        impl::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        PositionalPack(impl::unpack_arg<Source::args_idx>(
                            std::forward<decltype(args)>(args)...
                        )),
                        impl::unpack_arg<Source::args_idx + 1 + Next>(
                            std::forward<decltype(args)>(args)...
                        )...
                    );
                }(
                    std::make_index_sequence<Source::args_idx>{},
                    std::make_index_sequence<Source::n - (Source::args_idx + 1)>{},
                    std::forward<P>(parts),
                    std::forward<D>(defaults),
                    std::forward<F>(func),
                    std::forward<Values>(args)...
                );

            } else if constexpr (Source::has_kwargs) {
                return []<size_t... Prev>(
                    std::index_sequence<Prev...>,
                    auto&& parts,
                    auto&& defaults,
                    auto&& func,
                    auto&&... args
                ) {
                    return typename call<0, 0, 0>::cpp{}(
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(defaults)>(defaults),
                        std::forward<decltype(func)>(func),
                        impl::unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)...
                        )...,
                        KeywordPack(impl::unpack_arg<Source::kwargs_idx>(
                            std::forward<decltype(args)>(args)...
                        ))
                    );
                }(
                    std::make_index_sequence<Source::kwargs_idx>{},
                    std::forward<P>(parts),
                    std::forward<D>(defaults),
                    std::forward<F>(func),
                    std::forward<Values>(args)...
                );

            } else {
                return typename call<0, 0, 0>::cpp{}(
                    std::forward<P>(parts),
                    std::forward<D>(defaults),
                    std::forward<F>(func),
                    std::forward<Values>(args)...
                );
            }
        }

        /* Invoke a Python function from C++ using Python-style arguments.  This
        will always return a new reference to a raw Python object, or throw a
        runtime error if the arguments are malformed in some way. */
        template <impl::inherits<Partial> P>
            requires (
                std::convertible_to<Object, Return> &&
                satisfies_required_args
            )
        static Return operator()(
            P&& parts,
            PyObject* func,
            Values... args
        ) {
            constexpr auto heap_array = [](size_t size) {
                PyObject** array = new PyObject*[size + 1];
                if (array == nullptr) {
                    throw MemoryError();
                }
                array[0] = nullptr;
                return array;
            };

            /// NOTE: source positional and keyword packs must be converted
            /// into PositionalPack and KeywordPack helpers, which are
            /// destructively iterated over within the call algorithm and
            /// validated empty just before calling the target function.  In
            /// the Python case, this may require an additional heap allocation
            /// for the vectorcall array, which can be optimized to a stack
            /// allocation if the exact number of arguments is known at
            /// compile time (i.e. there are no positional or keyword unpacking
            /// operators in the argument list).
            if constexpr (Source::has_args && Source::has_kwargs) {
                return []<size_t... Prev, size_t... Next>(
                    std::index_sequence<Prev...>,
                    std::index_sequence<Next...>,
                    auto&& parts,
                    PyObject* func,
                    auto&&... args
                ) {
                    size_t size =
                        Partial::n +
                        (Source::n - 2) +
                        impl::unpack_arg<Source::args_idx>(
                            std::forward<decltype(args)>(args)...
                        ).size() +
                        impl::unpack_arg<Source::kwargs_idx>(
                            std::forward<decltype(args)>(args)...
                        ).size();
                    PyObject** array = ++heap_array(size);
                    try {
                        size_t kw_size =
                            call<0, 0, 0>::python::n_partial_keywords +
                            Source::n_kw +
                            impl::unpack_arg<Source::kwargs_idx>(
                                std::forward<decltype(args)>(args)...
                            ).size();
                        if (kw_size) {
                            PyObject* kwnames = PyTuple_New(kw_size);
                            try {
                                Object out = typename call<0, 0, 0>::python{}(
                                    std::forward<decltype(parts)>(parts),
                                    array,
                                    0,
                                    kwnames,
                                    0,
                                    func,
                                    impl::unpack_arg<Prev>(
                                        std::forward<decltype(args)>(args)...
                                    )...,
                                    PositionalPack(impl::unpack_arg<Source::args_idx>(
                                        std::forward<decltype(args)>(args)...
                                    )),
                                    impl::unpack_arg<Source::args_idx + 1 + Next>(
                                        std::forward<decltype(args)>(args)...
                                    )...,
                                    KeywordPack(impl::unpack_arg<Source::kwargs_idx>(
                                        std::forward<decltype(args)>(args)...
                                    ))
                                );
                                Py_DECREF(kwnames);
                                delete[] --array;
                                return out;
                            } catch (...) {
                                Py_DECREF(kwnames);
                                throw;
                            }
                        } else {
                            Object out = typename call<0, 0, 0>::python{}(
                                std::forward<decltype(parts)>(parts),
                                array,
                                0,
                                nullptr,
                                0,
                                func,
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                PositionalPack(impl::unpack_arg<Source::args_idx>(
                                    std::forward<decltype(args)>(args)...
                                )),
                                impl::unpack_arg<Source::args_idx + 1 + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                KeywordPack(impl::unpack_arg<Source::kwargs_idx>(
                                    std::forward<decltype(args)>(args)...
                                ))
                            );
                            delete[] --array;
                            return out;
                        }
                    } catch (...) {
                        delete[] --array;
                        throw;
                    }
                }(
                    std::make_index_sequence<Source::args_idx>{},
                    std::make_index_sequence<
                        Source::kwargs_idx - (Source::args_idx + 1)
                    >{},
                    std::forward<P>(parts),
                    func,
                    std::forward<Values>(args)...
                );

            } else if constexpr (Source::has_args) {
                return []<size_t... Prev, size_t... Next>(
                    std::index_sequence<Prev...>,
                    std::index_sequence<Next...>,
                    auto&& parts,
                    PyObject* func,
                    auto&&... args
                ) {
                    size_t size =
                        Partial::n +
                        (Source::n - 1) +
                        impl::unpack_arg<Source::args_idx>(
                            std::forward<decltype(args)>(args)...
                        ).size();
                    PyObject** array = ++heap_array(size);
                    try {
                        constexpr size_t kw_size =
                            call<0, 0, 0>::python::n_partial_keywords +
                            Source::n_kw;
                        if constexpr (kw_size) {
                            PyObject* kwnames = PyTuple_New(kw_size);
                            try {
                                Object out = typename call<0, 0, 0>::python{}(
                                    std::forward<decltype(parts)>(parts),
                                    array,
                                    0,
                                    kwnames,
                                    0,
                                    func,
                                    impl::unpack_arg<Prev>(
                                        std::forward<decltype(args)>(args)...
                                    )...,
                                    PositionalPack(impl::unpack_arg<Source::args_idx>(
                                        std::forward<decltype(args)>(args)...
                                    )),
                                    impl::unpack_arg<Source::args_idx + 1 + Next>(
                                        std::forward<decltype(args)>(args)...
                                    )...
                                );
                                Py_DECREF(kwnames);
                                delete[] --array;
                                return out;
                            } catch (...) {
                                Py_DECREF(kwnames);
                                throw;
                            }
                        } else {
                            Object out = typename call<0, 0, 0>::python{}(
                                std::forward<decltype(parts)>(parts),
                                array,
                                0,
                                nullptr,
                                0,
                                func,
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                PositionalPack(impl::unpack_arg<Source::args_idx>(
                                    std::forward<decltype(args)>(args)...
                                )),
                                impl::unpack_arg<Source::args_idx + 1 + Next>(
                                    std::forward<decltype(args)>(args)...
                                )...
                            );
                            delete[] --array;
                            return out;
                        }
                    } catch (...) {
                        delete[] --array;
                        throw;
                    }
                }(
                    std::make_index_sequence<Source::args_idx>{},
                    std::make_index_sequence<Source::n - (Source::args_idx + 1)>{},
                    std::forward<P>(parts),
                    func,
                    std::forward<Values>(args)...
                );

            } else if constexpr (Source::has_kwargs) {
                return []<size_t... Prev>(
                    std::index_sequence<Prev...>,
                    auto&& parts,
                    PyObject* func,
                    auto&&... args
                ) {
                    size_t size =
                        Partial::n +
                        (Source::n - 1) +
                        impl::unpack_arg<Source::kwargs_idx>(
                            std::forward<decltype(args)>(args)...
                        ).size();
                    PyObject** array = ++heap_array(size);
                    try {
                        size_t kw_size =
                            call<0, 0, 0>::python::n_partial_keywords +
                            Source::n_kw +
                            impl::unpack_arg<Source::kwargs_idx>(
                                std::forward<decltype(args)>(args)...
                            ).size();
                        if (kw_size) {
                            PyObject* kwnames = PyTuple_New(kw_size);
                            try {
                                Object out = typename call<0, 0, 0>::python{}(
                                    std::forward<decltype(parts)>(parts),
                                    array,
                                    0,
                                    kwnames,
                                    0,
                                    func,
                                    impl::unpack_arg<Prev>(
                                        std::forward<decltype(args)>(args)...
                                    )...,
                                    KeywordPack(impl::unpack_arg<Source::kwargs_idx>(
                                        std::forward<decltype(args)>(args)...
                                    ))
                                );
                                Py_DECREF(kwnames);
                                delete[] --array;
                                return out;
                            } catch (...) {
                                Py_DECREF(kwnames);
                                throw;
                            }
                        } else {
                            Object out = typename call<0, 0, 0>::python{}(
                                std::forward<decltype(parts)>(parts),
                                array,
                                0,
                                nullptr,
                                0,
                                func,
                                impl::unpack_arg<Prev>(
                                    std::forward<decltype(args)>(args)...
                                )...,
                                KeywordPack(impl::unpack_arg<Source::kwargs_idx>(
                                    std::forward<decltype(args)>(args)...
                                ))
                            );
                            delete[] --array;
                            return out;
                        }
                    } catch (...) {
                        delete[] --array;
                        throw;
                    }
                }(
                    std::make_index_sequence<Source::kwargs_idx>{},
                    std::forward<P>(parts),
                    func,
                    std::forward<Values>(args)...
                );

            } else {
                constexpr size_t size = Partial::n + Source::n;
                PyObject* array[size + 1];
                array[0] = nullptr;
                ++array;
                constexpr size_t kw_size =
                    call<0, 0, 0>::python::n_partial_keywords + Source::n_kw;
                if constexpr (kw_size) {
                    PyObject* kwnames = PyTuple_New(kw_size);
                    try {
                        Object out = typename call<0, 0, 0>::python{}(
                            std::forward<decltype(parts)>(parts),
                            array,
                            0,
                            kwnames,
                            0,
                            func,
                            std::forward<decltype(args)>(args)...
                        );
                        Py_DECREF(kwnames);
                        return out;
                    } catch (...) {
                        Py_DECREF(kwnames);
                        throw;
                    }
                } else {
                    return typename call<0, 0, 0>::python{}(
                        std::forward<decltype(parts)>(parts),
                        array,
                        0,
                        nullptr,
                        0,
                        func,
                        std::forward<decltype(args)>(args)...
                    );
                }
            }
        }
    };

    /* Produce a partial signature from the given arguments.  This method is
    chainable; the arguments will be interpreted as if they were passed to the
    signature's call operator, and any existing partials will be preserved. */
    template <typename... As>
        requires (
            (!impl::arg_pack<As> && ...) &&
            (!impl::kwarg_pack<As> && ...) &&
            Check<Signature<Return(As...)>>::proper_argument_order &&
            Check<Signature<Return(As...)>>::no_qualified_arg_annotations &&
            Check<Signature<Return(As...)>>::no_duplicate_args &&
            Check<Signature<Return(As...)>>::no_extra_positional_args &&
            Check<Signature<Return(As...)>>::no_extra_keyword_args &&
            Check<Signature<Return(As...)>>::no_conflicting_values &&
            Check<Signature<Return(As...)>>::can_convert
        )
    auto bind(this auto&& self, As&&... args) -> Bind<As...>::signature {
        return Bind<As...>::bind(
            std::forward<decltype(self)>(self).parts,
            std::forward<As>(args)...
        );
    }

    /* Unbinding a signature strips any partial arguments that have been encoded
    thus far and returns a new signature without them. */
    using Unbind = _unbind<Signature<Return()>, Args...>::type;

    /* Clear any partial arguments that have been accumulated thus far, returning
    a new signature without any bound arguments. */
    Unbind unbind() const {
        return {};
    }

    /* Invoke a C++ function that matches the enclosing signature. */
    template <impl::inherits<Defaults> D, typename F, typename... As>
        requires (
            std::is_invocable_r_v<Return, F, Args...> &&
            Check<Signature<Return(As...)>>::proper_argument_order &&
            Check<Signature<Return(As...)>>::no_qualified_arg_annotations &&
            Check<Signature<Return(As...)>>::no_duplicate_args &&
            Check<Signature<Return(As...)>>::no_conflicting_values &&
            Check<Signature<Return(As...)>>::no_extra_positional_args &&
            Check<Signature<Return(As...)>>::no_extra_keyword_args &&
            Check<Signature<Return(As...)>>::satisfies_required_args &&
            Check<Signature<Return(As...)>>::can_convert
        )
    Return operator()(this auto&& self, D&& defaults, F&& func, As&&... args) {
        return Bind<As...>{}(
            std::forward<decltype(self)>(self).parts,
            std::forward<D>(defaults),
            std::forward<F>(func),
            std::forward<As>(args)...
        );
    }

    /* Invoke a Python function that matches the enclosing signature. */
    template <typename... As>
        requires (
            std::convertible_to<Object, Return> &&
            Check<Signature<Return(As...)>>::proper_argument_order &&
            Check<Signature<Return(As...)>>::no_qualified_arg_annotations &&
            Check<Signature<Return(As...)>>::no_duplicate_args &&
            Check<Signature<Return(As...)>>::no_conflicting_values &&
            Check<Signature<Return(As...)>>::no_extra_positional_args &&
            Check<Signature<Return(As...)>>::no_extra_keyword_args &&
            Check<Signature<Return(As...)>>::satisfies_required_args &&
            Check<Signature<Return(As...)>>::can_convert
        )
    Return operator()(this auto&& self, PyObject* func, As&&... args) {
        return Bind<As...>{}(
            std::forward<decltype(self)>(self).parts,
            func,
            std::forward<As>(args)...
        );
    }

    /* Bind a Python vectorcall array to the enclosing signature and implement
    the translation logic necessary to invoke a matching C++ function.  This
    is essentially the inverse of the Bind<>::call::python algorithm, and uses
    many of the same techniques from Bind<>::call::cpp to build up the C++
    argument list in a way that avoids any intermediate data structures.  The
    arguments are simply interpreted as dynamic `Object` types and implicitly
    converted to the expected argument type using the same infrastructure as
    ordinary bertrand conversions.

    This is by far the most efficient way to invoke a C++ function from Python,
    as it generally involves only a single `isinstance()` check per argument,
    and a possible conversion to an equivalent C++ type.  Both are done using
    the same infrastructure as implicit conversions from the dynamic `Object`
    type, which delegates to the `__isinstance__` control struct, meaning any
    changes to the C++ conversion logic will be reflected here as well.
    Generally speaking, built-in types use this mechanism to optimize the check
    to specialized C API endpoints where possible, which improves performance
    even further beyond a standard `isinstance()` call.

    In some cases, an additional conversion may be required to handle Python
    types that lack sufficient type information for the check, such as standard
    Python containers that can contain any type.  In those cases, the type
    check may be applied elementwise to the contents of the container, which
    can be expensive if the container is large.  This can be avoided by using
    Bertrand types as inputs to the function, in which case the check is always
    O(1) in time, due to the 1:1 equivalence between Bertrand wrappers and
    their C++ counterparts, and corresponding type safety guarantees. */
    struct Vectorcall {
    protected:
        /* The kwnames tuple must be converted into a temporary map that can
        be destructively searched over the course of the call algorithm.  If
        any arguments remain by the time the underlying function is called,
        then they are considered extras. */
        struct Kwargs {
            using Map = std::unordered_map<std::string_view, PyObject*>;
            Map map;

            Kwargs(
                PyObject* const* array,
                size_t nargs,
                PyObject* kwnames,
                size_t kwcount
            ) :
                map([](
                    PyObject* const* array,
                    size_t nargs,
                    PyObject* kwnames,
                    size_t kwcount
                ) {
                    Map map;
                    map.reserve(kwcount);
                    for (size_t i = 0; i < kwcount; ++i) {
                        Py_ssize_t len;
                        const char* name = PyUnicode_AsUTF8AndSize(
                            PyTuple_GET_ITEM(kwnames, i),
                            &len
                        );
                        if (name == nullptr) {
                            Exception::from_python();
                        }
                        map.emplace(
                            std::string_view{name, static_cast<size_t>(len)},
                            array[nargs + i]
                        );
                    }
                    return map;
                }(array, nargs, kwnames, kwcount))
            {}

            void validate() {
                if constexpr (!Signature::has_kwargs) {
                    if (!map.empty()) {
                        auto it = map.begin();
                        auto end = map.end();
                        std::string message =
                            "unexpected keyword arguments: ['" +
                            std::string(it->first);
                        while (++it != end) {
                            message += "', '" + std::string(it->first);
                        }
                        message += "']";
                        throw TypeError(message);
                    }
                }
            }

            auto size() const { return map.size(); }
            template <typename T>
            auto extract(T&& key) { return map.extract(std::forward<T>(key)); }
            auto begin() { return map.begin(); }
            auto end() { return map.end(); }
        };

        /* Invoking a C++ function from Python involves translating a
        vectorcall array and kwnames tuple into a valid C++ parameter list that
        exactly matches the enclosing signature.  This is yet another 3-way
        merge between partial arguments, converted vectorcall arguments, and
        default values, in that order of precedence, and is essentially the
        inverse of the Bind<>::call<>::python algorithm.  It uses techniques
        from Bind<>::call<>::cpp to build up the C++ argument list via index
        sequences and fold expressions, which are inlined into the final call. */
        template <size_t I, size_t K>
        struct call {  // terminal case
            template <typename P>
            static Params<std::vector<Param>> key(
                std::vector<Param>& out,
                size_t hash,
                Kwargs& kwargs,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                P&& parts
            ) {
                validate_positional(array, idx, nargs);
                kwargs.validate();
                return {
                    .value = std::move(out),
                    .hash = hash
                };
            }

            template <typename P>
            static Params<std::vector<Param>> key(
                std::vector<Param>& out,
                size_t hash,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                P&& parts
            ) {
                validate_positional(array, idx, nargs);
                return {
                    .value = std::move(out),
                    .hash = hash
                };
            }

            template <typename P, typename D, typename F, typename... A>
            static std::invoke_result_t<F, Args...> operator()(
                P&& parts,
                D&& defaults,
                Kwargs& kwargs,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                F&& func,
                A&&... args
            ) {
                validate_positional(array, idx, nargs);
                kwargs.validate();
                return std::forward<F>(func)(std::forward<A>(args)...);
            }

            template <typename P, typename D, typename F, typename... A>
            static std::invoke_result_t<F, Args...> operator()(
                P&& parts,
                D&& defaults,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                F&& func,
                A&&... args
            ) {
                validate_positional(array, idx, nargs);
                return std::forward<F>(func)(std::forward<A>(args)...);
            }

        private:

            static void validate_positional(
                PyObject* const* array,
                size_t idx,
                size_t nargs
            ) {
                if constexpr (!Signature::has_args) {
                    if (idx < nargs) {
                        std::string message =
                            "unexpected positional arguments: [";
                        PyObject* str = PyObject_Repr(array[idx]);
                        if (str == nullptr) {
                            Exception::from_python();
                        }
                        Py_ssize_t len;
                        const char* name = PyUnicode_AsUTF8AndSize(
                            str,
                            &len
                        );
                        if (name == nullptr) {
                            Exception::from_python();
                        }
                        message += std::string(name, len);
                        while (++idx < nargs) {
                            str = PyObject_Repr(array[idx]);
                            if (str == nullptr) {
                                Exception::from_python();
                            }
                            name = PyUnicode_AsUTF8AndSize(
                                str,
                                &len
                            );
                            if (name == nullptr) {
                                Exception::from_python();
                            }
                            message += ", " + std::string(name, len);
                        }
                        message += "]";
                        throw TypeError(message);
                    }
                }
            }
        };
        template <size_t I, size_t K>
            requires (
                I < Signature::n &&
                (K < Partial::n && Partial::template rfind<K> == I)
            )
        struct call<I, K> {  // insert partial argument(s)
            template <size_t K2>
            static constexpr size_t consecutive = 0;
            template <size_t K2>
                requires (K2 < Partial::n && Partial::template rfind<K2> == I)
            static constexpr size_t consecutive<K2> = consecutive<K2 + 1> + 1;

            template <typename P>
            static Params<std::vector<Param>> key(
                std::vector<Param>& out,
                size_t hash,
                Kwargs& kwargs,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                P&& parts
            ) {
                using T = Signature::at<I>;
                if (
                    ArgTraits<T>::kwargs() ||
                    ArgTraits<T>::kwonly() ||
                    (ArgTraits<T>::kw() && idx >= nargs)
                ) {
                    out.emplace_back(
                        Partial::template name<K>,
                        to_python(
                            std::forward<P>(parts).template get<K>()
                        ),
                        impl::ArgKind::KW
                    );
                } else {
                    out.emplace_back(
                        "",
                        to_python(
                            std::forward<P>(parts).template get<K>()
                        ),
                        impl::ArgKind::POS
                    );
                }
                return call<
                    I + !ArgTraits<T>::variadic(),
                    K + 1
                >::python::key(
                    out,
                    impl::hash_combine(hash, out.back().hash(
                        Overloads::seed,
                        Overloads::prime
                    )),
                    kwargs,
                    array,
                    idx,
                    nargs,
                    std::forward<P>(parts)
                );
            }

            template <typename P>
            static Params<std::vector<Param>> key(
                std::vector<Param>& out,
                size_t hash,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                P&& parts
            ) {
                using T = Signature::at<I>;
                if (
                    ArgTraits<T>::kwargs() ||
                    ArgTraits<T>::kwonly() ||
                    (ArgTraits<T>::kw() && idx >= nargs)
                ) {
                    out.emplace_back(
                        Partial::template name<K>,
                        to_python(
                            std::forward<P>(parts).template get<K>()
                        ),
                        impl::ArgKind::KW
                    );
                } else {
                    out.emplace_back(
                        "",
                        to_python(
                            std::forward<P>(parts).template get<K>()
                        ),
                        impl::ArgKind::POS
                    );
                }
                return call<
                    I + !ArgTraits<T>::variadic(),
                    K + 1
                >::python::key(
                    out,
                    impl::hash_combine(hash, out.back().hash(
                        Overloads::seed,
                        Overloads::prime
                    )),
                    array,
                    idx,
                    nargs,
                    std::forward<P>(parts)
                );
            }

            template <typename P, typename D, typename F, typename... A>
            static std::invoke_result_t<F, Args...> operator()(
                P&& parts,
                D&& defaults,
                Kwargs& kwargs,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                F&& func,
                A&&... args
            ) {
                using T = Signature::at<I>;
                if constexpr (ArgTraits<T>::args() || ArgTraits<T>::kwargs()) {
                    return []<size_t... Ks>(
                        std::index_sequence<Ks...>,
                        auto&& parts,
                        auto&& defaults,
                        Kwargs& kwargs,
                        PyObject* const* array,
                        size_t idx,
                        size_t nargs,
                        auto&& func,
                        auto&&... args
                    ) {
                        if constexpr (ArgTraits<T>::args()) {
                            return call<I + 1, K + consecutive<K>>{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                kwargs,
                                array,
                                idx,
                                nargs,
                                std::forward<decltype(func)>(func),
                                std::forward<decltype(args)>(args)...,
                                to_arg<I>(variadic_positional(
                                    std::forward<decltype(parts)>(parts),
                                    array,
                                    idx,
                                    nargs
                                ))
                            );
                        } else if constexpr (ArgTraits<T>::kwargs()) {
                            return call<I + 1, K + consecutive<K>>{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                kwargs,
                                array,
                                idx,
                                nargs,
                                std::forward<decltype(func)>(func),
                                std::forward<decltype(args)>(args)...,
                                to_arg<I>(variadic_keywords(
                                    std::forward<decltype(parts)>(parts),
                                    kwargs
                                ))
                            );
                        }
                    }(
                        std::make_index_sequence<consecutive<K>>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        kwargs,
                        array,
                        idx,
                        nargs,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else {
                    return call<I + 1, K + 1>{}(
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        kwargs,
                        array,
                        idx,
                        nargs,
                        std::forward<F>(func),
                        std::forward<A>(args)...,
                        to_arg<I>(
                            std::forward<P>(parts).template get<K>()
                        )
                    );
                }
            }

            template <typename P, typename D, typename F, typename... A>
            static std::invoke_result_t<F, Args...> operator()(
                P&& parts,
                D&& defaults,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                F&& func,
                A&&... args
            ) {
                using T = Signature::at<I>;
                if constexpr (ArgTraits<T>::args() || ArgTraits<T>::kwargs()) {
                    return []<size_t... Ks>(
                        std::index_sequence<Ks...>,
                        auto&& parts,
                        auto&& defaults,
                        PyObject* const* array,
                        size_t idx,
                        size_t nargs,
                        auto&& func,
                        auto&&... args
                    ) {
                        if constexpr (ArgTraits<T>::args()) {
                            return call<I + 1, K + consecutive<K>>{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                array,
                                idx,
                                nargs,
                                std::forward<decltype(func)>(func),
                                std::forward<decltype(args)>(args)...,
                                to_arg<I>(variadic_positional(
                                    std::forward<decltype(parts)>(parts),
                                    array,
                                    idx,
                                    nargs
                                ))
                            );
                        } else if constexpr (ArgTraits<T>::kwargs()) {
                            return call<I + 1, K + consecutive<K>>{}(
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(defaults)>(defaults),
                                array,
                                idx,
                                nargs,
                                std::forward<decltype(func)>(func),
                                std::forward<decltype(args)>(args)...
                            );
                        }
                    }(
                        std::make_index_sequence<consecutive<K>>{},
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        array,
                        idx,
                        nargs,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else {
                    return call<I + 1, K + 1>{}(
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        array,
                        idx,
                        nargs,
                        std::forward<F>(func),
                        std::forward<A>(args)...,
                        to_arg<I>(
                            std::forward<P>(parts).template get<K>()
                        )
                    );
                }
            }

        private:

            template <typename P, typename... A>
            static auto variadic_positional(
                P&& parts,
                PyObject* const* array,
                size_t idx,
                size_t nargs
            ) {
                using T = Signature::at<I>;

                // allocate variadic positional array
                using vec = std::vector<typename ArgTraits<T>::type>;
                vec out;
                size_t diff = nargs > idx ? nargs - idx : 0;
                out.reserve(consecutive<K> + diff);

                // consume partial args
                []<size_t... Ks>(
                    std::index_sequence<Ks...>,
                    vec& out,
                    auto&& parts
                ) {
                    (out.emplace_back(std::forward<decltype(
                        parts
                    )>(parts).template get<K + Ks>()), ...);
                }(
                    std::make_index_sequence<consecutive<K>>{},
                    out,
                    std::forward<P>(parts)
                );

                // consume vectorcall args
                for (size_t i = idx; idx < nargs; ++i) {
                    out.emplace_back(
                        reinterpret_borrow<Object>(array[i])
                    );
                }
                return out;
            }

            template <typename P>
            static auto variadic_keywords(
                P&& parts,
                Kwargs& kwargs
            ) {
                using T = Signature::at<I>;

                // allocate variadic keyword map
                using map = std::unordered_map<
                    std::string,
                    typename ArgTraits<T>::type
                >;
                map out;
                out.reserve(consecutive<K> + kwargs.size());

                // consume partial kwargs
                []<size_t... Ks>(
                    std::index_sequence<Ks...>,
                    map& out,
                    auto&& parts
                ) {
                    (out.emplace(
                        Partial::template name<K + Ks>,
                        std::forward<decltype(parts)>(
                            parts
                        ).template get<K + Ks>()
                    ), ...);
                }(
                    std::make_index_sequence<consecutive<K>>{},
                    out,
                    std::forward<P>(parts)
                );

                // consume vectorcall kwargs
                for (auto& [key, value] : kwargs) {
                    out.emplace(
                        key,
                        reinterpret_borrow<Object>(value)
                    );
                }
                return out;
            }
        };
        template <size_t I, size_t K>
            requires (
                I < Signature::n &&
                !(K < Partial::n && Partial::template rfind<K> == I)
            )
        struct call<I, K> {  // insert Python argument(s) or default value
            template <typename P>
            static Params<std::vector<Param>> key(
                std::vector<Param>& out,
                size_t hash,
                Kwargs& kwargs,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                P&& parts
            ) {
                using T = Signature::at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                // positional-only
                if constexpr (ArgTraits<T>::posonly()) {
                    if (idx < nargs) {
                        out.emplace_back(
                            "",
                            reinterpret_borrow<Object>(array[idx]),
                            impl::ArgKind::POS
                        );
                        return call<I + 1, K>::python::key(
                            out,
                            impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            ),
                            kwargs,
                            array,
                            ++idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>::python::key(
                            out,
                            hash,
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    if constexpr (name.empty()) {
                        throw TypeError(
                            "no match for positional-only parameter at "
                            "index " + std::to_string(I)
                        );
                    } else {
                        throw TypeError(
                            "no match for positional-only parameter '" +
                            name + "' at index " + std::to_string(I)
                        );
                    }

                // positional-or-keyword
                } else if constexpr (ArgTraits<T>::pos()) {
                    if (idx < nargs) {
                        out.emplace_back(
                            "",
                            reinterpret_borrow<Object>(array[idx]),
                            impl::ArgKind::POS
                        );
                        return call<I + 1, K>::python::key(
                            out,
                            impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            ),
                            kwargs,
                            array,
                            ++idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    auto node = kwargs.extract(name);
                    if (node) {
                        out.emplace_back(
                            node.key(),
                            reinterpret_borrow<Object>(node.mapped()),
                            impl::ArgKind::KW
                        );
                        return call<I + 1, K>::python::key(
                            out,
                            impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            ),
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>::python::key(
                            out,
                            hash,
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    throw TypeError(
                        "no match for parameter '" + name + "' at index " +
                        std::to_string(I)
                    );

                // keyword-only
                } else if constexpr (ArgTraits<T>::kw()) {
                    auto node = kwargs.extract(name);
                    if (node) {
                        out.emplace_back(
                            std::move(node.key()),
                            reinterpret_borrow<Object>(node.mapped()),
                            impl::ArgKind::KW
                        );
                        return call<I + 1, K>::python::key(
                            out,
                            impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            ),
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>::python::key(
                            out,
                            hash,
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    throw TypeError(
                        "no match for parameter '" + name + "' at index " +
                        std::to_string(I)
                    );

                // keyword-or-positional
                } else if constexpr (ArgTraits<T>::args()) {
                    while (idx < nargs) {
                        out.emplace_back(
                            "",
                            reinterpret_borrow<Object>(array[idx++]),
                            impl::ArgKind::POS
                        );
                        hash = impl::hash_combine(
                            hash,
                            out.back().hash(
                                Overloads::seed,
                                Overloads::prime
                            )
                        );
                    }
                    return call<I + 1, K>::python::key(
                        out,
                        hash,
                        kwargs,
                        array,
                        idx,
                        nargs,
                        std::forward<P>(parts)
                    );

                // variadic positional
                } else if constexpr (ArgTraits<T>::kwargs()) {
                    auto it = kwargs.begin();
                    auto end = kwargs.end();
                    while (it != end) {
                        // postfix ++ required to increment before invalidation
                        auto node = kwargs.extract(it++);
                        out.emplace_back(
                            std::move(node.key()),
                            reinterpret_borrow<Object>(node.mapped())
                        );
                        hash = impl::hash_combine(
                            hash,
                            out.back().hash(
                                Overloads::seed,
                                Overloads::prime
                            )
                        );
                    }
                    return call<I + 1, K>::python::key(
                        out,
                        hash,
                        kwargs,
                        array,
                        idx,
                        nargs,
                        std::forward<P>(parts)
                    );

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

            template <typename P>
            static Params<std::vector<Param>> key(
                std::vector<Param>& out,
                size_t hash,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                P&& parts
            ) {
                using T = Signature::at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                // positional-only
                if constexpr (ArgTraits<T>::posonly()) {
                    if (idx < nargs) {
                        out.emplace_back(
                            "",
                            reinterpret_borrow<Object>(array[idx]),
                            impl::ArgKind::POS
                        );
                        return call<I + 1, K>::python::key(
                            out,
                            impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            ),
                            array,
                            ++idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>::python::key(
                            out,
                            hash,
                            array,
                            idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    if constexpr (name.empty()) {
                        throw TypeError(
                            "no match for positional-only parameter at "
                            "index " + std::to_string(I)
                        );
                    } else {
                        throw TypeError(
                            "no match for positional-only parameter '" +
                            name + "' at index " + std::to_string(I)
                        );
                    }

                // positional-or-keyword
                } else if constexpr (ArgTraits<T>::pos()) {
                    if (idx < nargs) {
                        out.emplace_back(
                            "",
                            reinterpret_borrow<Object>(array[idx]),
                            impl::ArgKind::POS
                        );
                        return call<I + 1, K>::python::key(
                            out,
                            impl::hash_combine(
                                hash,
                                out.back().hash(
                                    Overloads::seed,
                                    Overloads::prime
                                )
                            ),
                            array,
                            ++idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>::python::key(
                            out,
                            hash,
                            array,
                            idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    throw TypeError(
                        "no match for parameter '" + name + "' at index " +
                        std::to_string(I)
                    );

                // keyword-only
                } else if constexpr (ArgTraits<T>::kw()) {
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>::python::key(
                            out,
                            hash,
                            array,
                            idx,
                            nargs,
                            std::forward<P>(parts)
                        );
                    }
                    throw TypeError(
                        "no match for parameter '" + name + "' at index " +
                        std::to_string(I)
                    );

                // keyword-or-positional
                } else if constexpr (ArgTraits<T>::args()) {
                    while (idx < nargs) {
                        out.emplace_back(
                            "",
                            reinterpret_borrow<Object>(array[idx++]),
                            impl::ArgKind::POS
                        );
                        hash = impl::hash_combine(
                            hash,
                            out.back().hash(
                                Overloads::seed,
                                Overloads::prime
                            )
                        );
                    }
                    return call<I + 1, K>::python::key(
                        out,
                        hash,
                        array,
                        idx,
                        nargs,
                        std::forward<P>(parts)
                    );

                // variadic positional
                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return call<I + 1, K>::python::key(
                        out,
                        hash,
                        array,
                        idx,
                        nargs,
                        std::forward<P>(parts)
                    );

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

            template <typename P, typename D, typename F, typename... A>
            static std::invoke_result_t<F, Args...> operator()(
                P&& parts,
                D&& defaults,
                Kwargs& kwargs,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                F&& func,
                A&&... args
            ) {
                using T = Signature::at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                // positional-only
                if constexpr (ArgTraits<T>::posonly()) {
                    if (idx < nargs) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            kwargs,
                            array,
                            ++idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(reinterpret_borrow<Object>(
                                array[idx - 1]
                            ))
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(
                                std::forward<D>(defaults).template get<I>()
                            )
                        );
                    }
                    if constexpr (name.empty()) {
                        throw TypeError(
                            "no match for positional-only parameter at "
                            "index " + std::to_string(I)
                        );
                    } else {
                        throw TypeError(
                            "no match for positional-only parameter '" +
                            name + "' at index " + std::to_string(I)
                        );
                    }

                // positional-or-keyword
                } else if constexpr (ArgTraits<T>::pos()) {
                    if (idx < nargs) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            kwargs,
                            array,
                            ++idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(reinterpret_borrow<Object>(
                                array[idx - 1]
                            ))
                        );
                    }
                    auto node = kwargs.extract(name);
                    if (node) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(reinterpret_borrow<Object>(
                                node.mapped()
                            ))
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(
                                std::forward<D>(defaults).template get<I>()
                            )
                        );
                    }
                    throw TypeError(
                        "no match for parameter '" + name + "' at index " +
                        std::to_string(I)
                    );

                // keyword-only
                } else if constexpr (ArgTraits<T>::kw()) {
                    auto node = kwargs.extract(name);
                    if (node) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(reinterpret_borrow<Object>(
                                node.mapped()
                            ))
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            kwargs,
                            array,
                            idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(
                                std::forward<D>(defaults).template get<I>()
                            )
                        );
                    }
                    throw TypeError(
                        "no match for parameter '" + name + "' at index " +
                        std::to_string(I)
                    );

                // keyword-or-positional
                } else if constexpr (ArgTraits<T>::args()) {
                    return call<I + 1, K>{}(
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        kwargs,
                        array,
                        idx,
                        nargs,
                        std::forward<F>(func),
                        std::forward<A>(args)...,
                        to_arg<I>(variadic_positional(
                            std::forward<P>(parts),
                            array,
                            idx,
                            nargs
                        ))
                    );

                // variadic positional
                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return call<I + 1, K>{}(
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        kwargs,
                        array,
                        idx,
                        nargs,
                        std::forward<F>(func),
                        std::forward<A>(args)...,
                        to_arg<I>(variadic_keywords(
                            std::forward<P>(parts),
                            kwargs
                        ))
                    );

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

            template <typename P, typename D, typename F, typename... A>
            static std::invoke_result_t<F, Args...> operator()(
                P&& parts,
                D&& defaults,
                PyObject* const* array,
                size_t idx,
                size_t nargs,
                F&& func,
                A&&... args
            ) {
                using T = Signature::at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                // positional-only
                if constexpr (ArgTraits<T>::posonly()) {
                    if (idx < nargs) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            array,
                            ++idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(reinterpret_borrow<Object>(
                                array[idx - 1]
                            ))
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            array,
                            idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(
                                std::forward<D>(defaults).template get<I>()
                            )
                        );
                    }
                    if constexpr (name.empty()) {
                        throw TypeError(
                            "no match for positional-only parameter at "
                            "index " + std::to_string(I)
                        );
                    } else {
                        throw TypeError(
                            "no match for positional-only parameter '" +
                            name + "' at index " + std::to_string(I)
                        );
                    }

                // positional-or-keyword
                } else if constexpr (ArgTraits<T>::pos()) {
                    if (idx < nargs) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            array,
                            ++idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(reinterpret_borrow<Object>(
                                array[idx - 1]
                            ))
                        );
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            array,
                            idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(
                                std::forward<D>(defaults).template get<I>()
                            )
                        );
                    }
                    throw TypeError(
                        "no match for parameter '" + name + "' at index " +
                        std::to_string(I)
                    );

                // keyword-only
                } else if constexpr (ArgTraits<T>::kw()) {
                    if constexpr (ArgTraits<T>::opt()) {
                        return call<I + 1, K>{}(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            array,
                            idx,
                            nargs,
                            std::forward<F>(func),
                            std::forward<A>(args)...,
                            to_arg<I>(
                                std::forward<D>(defaults).template get<I>()
                            )
                        );
                    }
                    throw TypeError(
                        "no match for parameter '" + name + "' at index " +
                        std::to_string(I)
                    );

                // keyword-or-positional
                } else if constexpr (ArgTraits<T>::args()) {
                    return call<I + 1, K>{}(
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        array,
                        idx,
                        nargs,
                        std::forward<F>(func),
                        std::forward<A>(args)...,
                        to_arg<I>(variadic_positional(
                            std::forward<P>(parts),
                            array,
                            idx,
                            nargs
                        ))
                    );

                // variadic positional
                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return call<I + 1, K>{}(
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        array,
                        idx,
                        nargs,
                        std::forward<F>(func),
                        std::forward<A>(args)...,
                        to_arg<I>(std::unordered_map<
                            std::string,
                            typename ArgTraits<T>::type
                        >{})
                    );

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

        private:

            template <typename P, typename... A>
            static auto variadic_positional(
                P&& parts,
                PyObject* const* array,
                size_t idx,
                size_t nargs
            ) {
                using T = Signature::at<I>;

                // allocate variadic positional array
                using vec = std::vector<typename ArgTraits<T>::type>;
                vec out;
                out.reserve(nargs > idx ? nargs - idx : 0);

                // consume vectorcall args
                for (size_t i = idx; idx < nargs; ++i) {
                    out.emplace_back(
                        reinterpret_borrow<Object>(array[i])
                    );
                }
                return out;
            }

            template <typename P>
            static auto variadic_keywords(
                P&& parts,
                Kwargs& kwargs
            ) {
                using T = Signature::at<I>;

                // allocate variadic keyword map
                using map = std::unordered_map<
                    std::string,
                    typename ArgTraits<T>::type
                >;
                map out;
                out.reserve(kwargs.size());

                // consume vectorcall kwargs
                auto it = kwargs.begin();
                auto end = kwargs.end();
                while (it != end) {
                    // postfix ++ required to increment before invalidation
                    auto node = kwargs.extract(it++);
                    out.emplace_back(
                        std::move(node.key()),
                        reinterpret_borrow<Object>(node.mapped())
                    );
                }
                return out;
            }
        };

    public:
        PyObject* const* args;
        size_t nargs;
        size_t flags;
        PyObject* kwnames;
        size_t kwcount;
        std::vector<PyObject*> converted;

        Vectorcall(PyObject* const* args, size_t nargsf, PyObject* kwnames) :
            args(args),
            kwnames(kwnames),
            kwcount(kwnames ? PyTuple_GET_SIZE(kwnames) : 0),
            nargs(PyVectorcall_NARGS(nargsf)),
            flags(nargsf & PY_VECTORCALL_ARGUMENTS_OFFSET)
        {}

        Vectorcall(const Vectorcall& other) :
            args(other.args),
            nargs(other.nargs),
            flags(other.flags),
            kwnames(other.kwnames),
            kwcount(other.kwcount),
            converted(other.converted)
        {
            for (PyObject* obj : converted) {
                Py_INCREF(obj);
            }
        }

        Vectorcall(Vectorcall&& other) :
            args(other.args),
            nargs(other.nargs),
            flags(other.flags),
            kwnames(other.kwnames),
            kwcount(other.kwcount),
            converted(std::move(other.converted))
        {
            other.args = nullptr;
            other.kwnames = nullptr;
            other.kwcount = 0;
            other.nargs = 0;
            other.flags = 0;
        }

        Vectorcall& operator=(const Vectorcall& other) {
            if (this != &other) {
                for (PyObject* obj : converted) {
                    Py_DECREF(obj);
                }
                args = other.args;
                nargs = other.nargs;
                flags = other.flags;
                kwnames = other.kwnames;
                kwcount = other.kwcount;
                converted = other.converted;
                for (PyObject* obj : converted) {
                    Py_INCREF(obj);
                }
            }
            return *this;
        }

        Vectorcall& operator=(Vectorcall&& other) {
            if (this != &other) {
                for (PyObject* obj : converted) {
                    Py_DECREF(obj);
                }
                args = other.args;
                nargs = other.nargs;
                flags = other.flags;
                kwnames = other.kwnames;
                kwcount = other.kwcount;
                converted = std::move(other.converted);
                other.args = nullptr;
                other.kwnames = nullptr;
                other.kwcount = 0;
                other.nargs = 0;
                other.flags = 0;
            }
            return *this;
        }

        ~Vectorcall() noexcept {
            for (PyObject* obj : converted) {
                Py_DECREF(obj);
            }
        }

        /* Produce an overload key from the Python arguments, which can be used to
        search the overload trie and invoke a resulting function. */
        template <impl::inherits<Partial> P>
        Params<std::vector<Param>> key(P&& parts) {
            /// NOTE: in order to provide consistent hashes with full type
            /// information, each argument must be converted into a Bertrand
            /// type before the key is generated.  This is the only way to
            /// properly differentiate between ambiguous Python types (like
            /// generic containers) and avoid hash ambiguities.
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(impl::template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            size_t size = nargs + kwcount;
            converted.reserve(Partial::n + size);
            for (size_t i = 0; i < size; ++i) {
                PyObject* obj = PyObject_CallOneArg(
                    ptr(bertrand),
                    args[i]
                );
                if (obj == nullptr) {
                    for (size_t j = 0; j < i; ++j) {
                        Py_DECREF(converted[j]);
                    }
                    converted.clear();
                    Exception::from_python();
                }
                converted.emplace_back(obj);
            }

            std::vector<Param> out;
            out.reserve(Partial::n + size);
            if (kwnames) {
                Kwargs kwargs {converted.data(), nargs, kwnames, kwcount};
                return call<0, 0>::key(
                    out,
                    0,
                    kwargs,
                    converted.data(),
                    0,
                    nargs,
                    std::forward<P>(parts)
                );
            } else {
                return call<0, 0>::key(
                    out,
                    0,
                    converted.data(),
                    0,
                    nargs,
                    std::forward<P>(parts)
                );
            }
        }

        /* Invoke a C++ function from Python using Python-style arguments. */
        template <impl::inherits<Partial> P, impl::inherits<Defaults> D, typename F>
            requires (std::is_invocable_r_v<Return, F, Args...>)
        Return operator()(P&& parts, D&& defaults, F&& func) const {
            if (kwnames) {
                Kwargs kwargs {args, nargs, kwnames, kwcount};
                return call<0, 0>{}(
                    std::forward<P>(parts),
                    std::forward<D>(defaults),
                    kwargs,
                    converted.empty() ? args : converted.data(),
                    0,
                    nargs,
                    std::forward<F>(func)
                );
            } else {
                return call<0, 0>{}(
                    std::forward<P>(parts),
                    std::forward<D>(defaults),
                    converted.empty() ? args : converted.data(),
                    0,
                    nargs,
                    std::forward<F>(func)
                );
            }
        }
    };

    /* Interpret a Python vectorcall array in order to invoke a C++ function that
    matches the enclosing signature. */
    template <impl::inherits<Defaults> D, typename F>
        requires (std::is_invocable_r_v<Return, F, Args...>)
    Return vectorcall(
        this auto&& self,
        D&& defaults,
        F&& func,
        PyObject* const* args,
        size_t nargsf,
        PyObject* kwnames
    ) {
        return Vectorcall{args, nargsf, kwnames}(
            std::forward<decltype(self)>(self).parts,
            std::forward<D>(defaults),
            std::forward<F>(func)
        );
    }

    /// TODO: partial arguments will have to be provided to the Overload trie
    /// iterators, such that they can be automatically inserted when traversing
    /// the trie, and only matching functions will be returned.  This might mess
    /// with caching, since in practice, we would always need to include the
    /// partial arguments in the key in order to make the hash stable and
    /// unambiguous.
    /// -> That actually may not require any changes, since basically I just have
    /// to properly insert the partial arguments when building the key, which is
    /// not always simple, but is at least centralized in the Partial<> class.
    /// -> Actually yes it does, because the partial key isn't fully formed.

    /* A Trie-based data structure containing a pool of dynamic overloads for a
    `py::Function` object, which will be dispatched to when the function is called
    from either Python or C++.  This uses a standardized key() format to allow for
    efficient caching. */
    struct Overloads {
    private:
        template <size_t I>
        friend Param Signature::_key(size_t& hash);

        static constexpr size_t keyword_table_size = impl::next_power_of_two(2 * n_kw);
        static constexpr size_t keyword_modulus(size_t hash) {
            return hash & (keyword_table_size - 1);
        }

        /* Check to see if the candidate seed and prime produce any collisions for the
        target keyword arguments. */
        template <typename...>
        struct collisions {
            static constexpr bool operator()(size_t, size_t) {
                return false;
            }
        };
        template <typename T, typename... Ts>
        struct collisions<T, Ts...> {
            template <typename...>
            struct scan {
                static constexpr bool operator()(size_t, size_t, size_t) {
                    return false;
                }
            };
            template <typename U, typename... Us>
            struct scan<U, Us...> {
                static constexpr bool operator()(size_t idx, size_t seed, size_t prime) {
                    if constexpr (ArgTraits<U>::kw()) {
                        size_t hash = impl::fnv1a(
                            ArgTraits<U>::name,
                            seed,
                            prime
                        );
                        return
                            (keyword_modulus(hash) == idx) ||
                            scan<Us...>{}(idx, seed, prime);
                    } else {
                        return scan<Us...>{}(idx, seed, prime);
                    }
                }
            };

            static constexpr bool operator()(size_t seed, size_t prime) {
                if constexpr (ArgTraits<T>::kw()) {
                    size_t hash = impl::fnv1a(
                        ArgTraits<T>::name,
                        seed,
                        prime
                    );
                    return scan<Ts...>{}(
                        keyword_modulus(hash),
                        seed,
                        prime
                    ) || collisions<Ts...>{}(seed, prime);
                } else {
                    return collisions<Ts...>{}(seed, prime);
                }
            }
        };

        /* Find an FNV-1a seed and prime that produces perfect hashes with respect to
        the keyword table size. */
        static constexpr auto hash_components = [] -> std::tuple<size_t, size_t, bool> {
            constexpr size_t recursion_limit = impl::fnv1a_seed + 100'000;
            size_t seed = impl::fnv1a_seed;
            size_t prime = impl::fnv1a_prime;
            size_t i = 0;
            while (collisions<Args...>{}(seed, prime)) {
                if (++seed > recursion_limit) {
                    if (++i == 10) {
                        return {0, 0, false};
                    }
                    seed = impl::fnv1a_seed;
                    prime = impl::fnv1a_fallback_primes[i];
                }
            }
            return {seed, prime, true};
        }();
        static_assert(
            std::get<2>(hash_components),
            "error: unable to find a perfect hash seed after 10^6 iterations.  "
            "Consider increasing the recursion limit or reviewing the keyword "
            "argument names for potential issues.\n"
        );

        template <size_t I>
        static constexpr uint64_t _required = [] {
            return
                ArgTraits<impl::unpack_type<I, Args...>>::opt() ||
                ArgTraits<impl::unpack_type<I, Args...>>::variadic() ?
                    0ULL : 1ULL << I;
        }();

    public:
        /* A seed for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t seed = std::get<0>(hash_components);

        /* A prime for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t prime = std::get<1>(hash_components);

        /* Hash a byte string according to the FNV-1a algorithm using the seed and
        prime that were found at compile time to perfectly hash the keyword
        arguments. */
        static constexpr size_t hash(const char* str) noexcept {
            return impl::fnv1a(str, seed, prime);
        }
        static constexpr size_t hash(std::string_view str) noexcept {
            return impl::fnv1a(str.data(), seed, prime);
        }
        static constexpr size_t hash(const std::string& str) noexcept {
            return impl::fnv1a(str.data(), seed, prime);
        }

        /* A single entry in a callback table, storing the argument name (which may be
        empty), a one-hot encoded bitmask specifying this argument's position, a
        function that can be used to validate the argument, and a lazy function that
        can be used to retrieve its corresponding Python type. */
        struct Callback {
            std::string_view name;
            uint64_t mask = 0;
            bool(*isinstance)(const Object&) = nullptr;
            bool(*issubclass)(const Object&) = nullptr;
            Object(*type)() = nullptr;
            [[nodiscard]] explicit constexpr operator bool() const noexcept {
                return isinstance != nullptr;
            }
        };

        /* A bitmask with a 1 in the position of all of the required arguments in the
        parameter list.

        Each callback stores a one-hot encoded mask that is joined into a single
        bitmask as each argument is processed.  The resulting mask can then be compared
        to this constant to determine if all required arguments have been provided.  If
        that comparison evaluates to false, then further bitwise inspection can be done
        to determine exactly which arguments were missing, as well as their names.

        Note that this mask effectively limits the number of arguments that a function
        can accept to 64, which is a reasonable limit for most functions.  The
        performance benefits justify the limitation, and if you need more than 64
        arguments, you should probably be using a different design pattern anyways. */
        static constexpr uint64_t required =
            []<size_t... Is>(std::index_sequence<Is...>) {
                return (0 | ... | _required<Is>);
            }(std::make_index_sequence<n>{});

    private:
        static constexpr Callback null_check;

        template <size_t I>
        static consteval Callback populate_positional_table() {
            using T = at<I>;
            return {
                .name = ArgTraits<T>::name,
                .mask = ArgTraits<T>::variadic() ? 0ULL : 1ULL << I,
                .isinstance = [](const Object& value) -> bool {
                    using U = ArgTraits<T>::type;
                    if constexpr (impl::has_python<U>) {
                        return isinstance<std::remove_cvref_t<impl::python_type<U>>>(value);
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " + type_name<U>
                        );
                    }
                },
                .issubclass = [](const Object& type) -> bool {
                    using U = ArgTraits<T>::type;
                    if constexpr (impl::has_python<U>) {
                        return issubclass<std::remove_cvref_t<impl::python_type<U>>>(type);
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " + type_name<U>
                        );
                    }
                },
                .type = []() -> Object {
                    using U = ArgTraits<T>::type;
                    if constexpr (impl::has_python<U>) {
                        return Type<std::remove_cvref_t<impl::python_type<U>>>();
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " + type_name<U>
                        );
                    }
                }
            };
        }

        static constexpr auto positional_table =
            []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array<Callback, n>{populate_positional_table<Is>()...};
            }(std::make_index_sequence<n>{});

        template <size_t I>
        static constexpr void populate_keyword_table(
            std::array<Callback, keyword_table_size>& table,
            size_t seed,
            size_t prime
        ) {
            using T = at<I>;
            if constexpr (ArgTraits<T>::kw()) {
                table[keyword_modulus(hash(ArgTraits<T>::name.data()))] = {
                    .name = ArgTraits<T>::name,
                    .mask = ArgTraits<T>::variadic() ? 0ULL : 1ULL << I,
                    .isinstance = [](const Object& value) -> bool {
                        using U = ArgTraits<T>::type;
                        if constexpr (impl::has_python<U>) {
                            return isinstance<std::remove_cvref_t<impl::python_type<U>>>(value);
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " + type_name<U>
                            );
                        }
                    },
                    .issubclass = [](const Object& type) -> bool {
                        using U = ArgTraits<T>::type;
                        if constexpr (impl::has_python<U>) {
                            return issubclass<std::remove_cvref_t<impl::python_type<U>>>(type);
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " + type_name<U>
                            );
                        }
                    },
                    .type = []() -> Object {
                        using U = ArgTraits<T>::type;
                        if constexpr (impl::has_python<U>) {
                            return Type<std::remove_cvref_t<impl::python_type<U>>>();
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " + type_name<U>
                            );
                        }
                    }
                };
            }
        }

        static constexpr auto keyword_table =
            []<size_t... Is>(std::index_sequence<Is...>, size_t seed, size_t prime) {
                std::array<Callback, keyword_table_size> table;
                (populate_keyword_table<Is>(table, seed, prime), ...);
                return table;
            }(std::make_index_sequence<n>{}, seed, prime);

        struct BoundView;

        struct instance {
            static bool operator()(PyObject* obj, PyObject* cls) {
                int rc = PyObject_IsInstance(obj, cls);
                if (rc < 0) {
                    Exception::from_python();
                }
                return rc;
            }
        };

        struct subclass {
            static bool operator()(PyObject* obj, PyObject* cls) {
                int rc = PyObject_IsSubclass(obj, cls);
                if (rc < 0) {
                    Exception::from_python();
                }
                return rc;
            }
        };

        template <typename T>
        static constexpr bool valid_check =
            std::same_as<T, instance> || std::same_as<T, subclass>;

    public:
        struct Metadata;
        struct Edge;
        struct Edges;
        struct Node;

        /* Look up a positional argument, returning a callback object that can be used
        to efficiently validate it.  If the index does not correspond to a recognized
        positional argument, a null callback will be returned that evaluates to false
        under boolean logic.  If the parameter list accepts variadic positional
        arguments, then the variadic argument's callback will be returned instead. */
        static constexpr const Callback& check(size_t i) noexcept {
            if constexpr (has_args) {
                return i < args_idx ? positional_table[i] : positional_table[args_idx];
            } else if constexpr (has_kwonly) {
                return i < kwonly_idx ? positional_table[i] : null_check;
            } else {
                return i < kwargs_idx ? positional_table[i] : null_check;
            }
        }

        /* Look up a keyword argument, returning a callback object that can be used to
        efficiently validate it.  If the argument name is not recognized, a null
        callback will be returned that evaluates to false under boolean logic.  If the
        parameter list accepts variadic keyword arguments, then the variadic argument's
        callback will be returned instead. */
        static constexpr const Callback& check(std::string_view name) noexcept {
            const Callback& callback = keyword_table[
                keyword_modulus(hash(name.data()))
            ];
            if (callback.name == name) {
                return callback;
            } else {
                if constexpr (has_kwargs) {
                    return keyword_table[kwargs_idx];
                } else {
                    return null_check;
                }
            }
        }

        /* An encoded representation of a function that has been inserted into the
        overload trie, which includes the function itself, a hash of the key that
        it was inserted under, a bitmask of the required arguments that must be
        satisfied to invoke the function, and a canonical path of edges starting
        from the root node that leads to the terminal function.

        These are stored in an associative set rather than a hash set in order to
        ensure address stability over the lifetime of the trie, so that it doesn't
        need to manage any memory itself. */
        struct Metadata {
            size_t hash;
            uint64_t required;
            Object func;
            std::vector<Edge> path;
            friend bool operator<(const Metadata& lhs, const Metadata& rhs) {
                return lhs.hash < rhs.hash;
            }
            friend bool operator<(const Metadata& lhs, size_t rhs) {
                return lhs.hash < rhs;
            }
            friend bool operator<(size_t lhs, const Metadata& rhs) {
                return lhs < rhs.hash;
            }
        };

        /* A single link between two nodes in the trie, which describes how to
        traverse from one to the other.  Multiple edges may share the same target
        node, and a unique edge will be created for each parameter in a key when it
        is inserted, such that the original key can be unambiguously identified
        from a simple search of the trie structure. */
        struct Edge {
            size_t hash;
            uint64_t mask;
            std::string name;
            Object type;
            impl::ArgKind kind;
            std::shared_ptr<Node> node;
        };

        /* A sorted collection of outgoing edges linking a node to its descendants.
        Edges are topologically sorted by their expected type, with subclasses
        coming before their parent classes. */
        struct Edges {
        private:
            friend BoundView;

            /* `issubclass()` checks are used to sort the edge map, with ties
            being broken by address. */
            struct TopoSort {
                static bool operator()(PyObject* lhs, PyObject* rhs) {
                    int rc = PyObject_IsSubclass(lhs, rhs);
                    if (rc < 0) {
                        Exception::from_python();
                    }
                    return rc || lhs < rhs;
                }
            };

            /* Edges are stored indirectly to simplify memory management, and are
            sorted based on kind, with required arguments coming before optional,
            which come before variadic, with ties broken by hash.  Each one refers
            to the contents of a `Metadata::path` sequence, which is guaranteed to
            have a stable address for the lifetime of the overload. */
            struct EdgePtr {
                Edge* edge;
                EdgePtr(const Edge* edge = nullptr) : edge(edge) {}
                operator const Edge*() const { return edge; }
                const Edge& operator*() const { return *edge; }
                const Edge* operator->() const { return edge; }
                friend bool operator<(const EdgePtr& lhs, const EdgePtr& rhs) {
                    return
                        lhs.edge->kind < rhs.edge->kind ||
                        lhs.edge->hash < rhs.edge->hash;
                }
                friend bool operator<(const EdgePtr& lhs, size_t rhs) {
                    return lhs.edge->hash < rhs;
                }
                friend bool operator<(size_t lhs, const EdgePtr& rhs) {
                    return lhs < rhs.edge->hash;
                }
            };

            /* Edge pointers are stored in another associative set to achieve
            the nested sorting.  By definition, each edge within the set points
            to the same destination node. */
            struct EdgeKinds {
                using Set = std::set<const EdgePtr, std::less<>>;
                std::shared_ptr<Node> node;
                Set set;
            };

            /* The types stored in the edge map are also borrowed references to a
            `Metadata::path` sequence to simplify memory management. */
            using Map = std::map<PyObject*, EdgeKinds, TopoSort>;
            Map map;

            /* A range adaptor that only yields edges matching a particular key,
            identified by its hash. */
            template <typename do_check> requires (valid_check<do_check>)
            struct HashView {
                const Edges& self;
                Object value;
                size_t hash;

                struct Iterator {
                    using iterator_category = std::input_iterator_tag;
                    using difference_type = std::ptrdiff_t;
                    using value_type = const Edge*;
                    using pointer = value_type*;
                    using reference = value_type&;

                    Map::iterator it;
                    Map::iterator end;
                    Object value;
                    size_t hash;
                    const Edge* curr;

                    Iterator(
                        Map::iterator&& it,
                        Map::iterator&& end,
                        const Object& value,
                        size_t hash
                    ) : it(std::move(it)), end(std::move(end)), value(value),
                        hash(hash), curr(nullptr)
                    {
                        while (this->it != this->end) {
                            if (do_check{}(ptr(value), this->it->first)) {
                                auto lookup = this->it->second.set.find(hash);
                                if (lookup != this->it->second.set.end()) {
                                    curr = *lookup;
                                    break;
                                }
                            }
                            ++it;
                        }
                    }

                    Iterator& operator++() {
                        ++it;
                        while (it != end) {
                            if (do_check{}(ptr(value), it->first)) {
                                auto lookup = it->second.set.find(hash);
                                if (lookup != it->second.set.end()) {
                                    curr = *lookup;
                                    break;
                                }
                            }
                            ++it;
                        }
                        return *this;
                    }

                    const Edge* operator*() const {
                        return curr;
                    }

                    friend bool operator==(
                        const Iterator& iter,
                        const impl::Sentinel& sentinel
                    ) {
                        return iter.it == iter.end;
                    }

                    friend bool operator==(
                        const impl::Sentinel& sentinel,
                        const Iterator& iter
                    ) {
                        return iter.it == iter.end;
                    }

                    friend bool operator!=(
                        const Iterator& iter,
                        const impl::Sentinel& sentinel
                    ) {
                        return iter.it != iter.end;
                    }

                    friend bool operator!=(
                        const impl::Sentinel& sentinel,
                        const Iterator& iter
                    ) {
                        return iter.it != iter.end;
                    }
                };

                Iterator begin() const {
                    return {self.begin(), self.end(), value, hash};
                }

                impl::Sentinel end() const {
                    return {};
                }
            };

            /* A range adaptor that yields edges in order, regardless of key. */
            template <typename do_check> requires (valid_check<do_check>)
            struct OrderedView {
                const Edges& self;
                Object value;

                struct Iterator {
                    using iterator_category = std::input_iterator_tag;
                    using difference_type = std::ptrdiff_t;
                    using value_type = const Edge*;
                    using pointer = value_type*;
                    using reference = value_type&;

                    Map::iterator it;
                    Map::iterator end;
                    EdgeKinds::Set::iterator edge_it;
                    EdgeKinds::Set::iterator edge_end;
                    Object value;

                    Iterator(
                        Map::iterator&& it,
                        Map::iterator&& end,
                        const Object& value
                    ) : it(std::move(it)), end(std::move(end)), value(value)
                    {
                        while (this->it != this->end) {
                            if (do_check{}(ptr(value), this->it->first)) {
                                edge_it = this->it->second.set.begin();
                                edge_end = this->it->second.set.end();
                                break;
                            }
                            ++it;
                        }
                    }

                    Iterator& operator++() {
                        ++edge_it;
                        if (edge_it == edge_end) {
                            ++it;
                            while (it != end) {
                                if (do_check{}(ptr(value), it->first)) {
                                    edge_it = it->second.set.begin();
                                    edge_end = it->second.set.end();
                                    break;
                                }
                                ++it;
                            }
                        }
                        return *this;
                    }

                    const Edge* operator*() const {
                        return *edge_it;
                    }

                    friend bool operator==(
                        const Iterator& iter,
                        const impl::Sentinel& sentinel
                    ) {
                        return iter.it == iter.end;
                    }

                    friend bool operator==(
                        const impl::Sentinel& sentinel,
                        const Iterator& iter
                    ) {
                        return iter.it == iter.end;
                    }

                    friend bool operator!=(
                        const Iterator& iter,
                        const impl::Sentinel& sentinel
                    ) {
                        return iter.it != iter.end;
                    }

                    friend bool operator!=(
                        const impl::Sentinel& sentinel,
                        const Iterator& iter
                    ) {
                        return iter.it != iter.end;
                    }

                };

                Iterator begin() const {
                    return {self.begin(), self.end(), value};
                }

                impl::Sentinel end() const {
                    return {};
                }
            };

        public:
            auto size() const { return map.size(); }
            auto empty() const { return map.empty(); }
            auto begin() const { return map.begin(); }
            auto cbegin() const { return map.cbegin(); }
            auto end() const { return map.end(); }
            auto cend() const { return map.cend(); }

            /* Insert an edge into this map and initialize its node pointer.
            Returns true if the insertion resulted in the creation of a new node,
            or false if the edge references an existing node. */
            [[maybe_unused]] bool insert(Edge& edge) {
                auto [outer, inserted] = map.try_emplace(
                    ptr(edge.type),
                    EdgeKinds{}
                );
                auto [_, success] = outer->second.set.emplace(&edge);
                if (!success) {
                    if (inserted) {
                        map.erase(outer);
                    }
                    throw TypeError(
                        "overload trie already contains an edge for type: " +
                        repr(edge.type)
                    );
                }
                if (inserted) {
                    outer->second.node = std::make_shared<Node>();
                }
                edge.node = outer->second.node;
                return inserted;
            }

            /* Insert an edge into this map using an explicit node pointer.
            Returns true if the insertion created a new table in the map, or false
            if it was added to an existing one.  Does NOT initialize the edge's
            node pointer, and a false return value does NOT guarantee that the
            existing table references the same node. */
            [[maybe_unused]] bool insert(Edge& edge, std::shared_ptr<Node> node) {
                auto [outer, inserted] = map.try_emplace(
                    ptr(edge.type),
                    EdgeKinds{node}
                );
                auto [_, success] = outer->second.set.emplace(&edge);
                if (!success) {
                    if (inserted) {
                        map.erase(outer);
                    }
                    throw TypeError(
                        "overload trie already contains an edge for type: " +
                        repr(edge.type)
                    );
                }
                return inserted;
            }

            /* Remove any outgoing edges that match the given hash. */
            void remove(size_t hash) noexcept {
                std::vector<PyObject*> dead;
                for (auto& [type, table] : map) {
                    table.set.erase(hash);
                    if (table.set.empty()) {
                        dead.emplace_back(type);
                    }
                }
                for (PyObject* type : dead) {
                    map.erase(type);
                }
            }

            /* Return a range adaptor that iterates over the topologically-sorted
            types and yields individual edges for those that match against an
            observed object.  If multiple edges exist for a given object, then the
            range will yield them in order based on kind, with required arguments
            coming before optional, which come before variadic.  There is no
            guarantee that the edges come from a single key, just that they match
            the observed object. */
            template <typename do_check> requires (valid_check<do_check>)
            OrderedView<do_check> match(const Object& value) const {
                return {*this, value};
            }

            /* Return a range adaptor that iterates over the topologically-sorted
            types, and yields individual edges for those that match against an
            observed object and originate from the specified key, identified by its
            unique hash.  Rather than matching all possible edges, this view will
            limit its search to the specified key, tracing checking edges that are
            contained within it. */
            template <typename do_check> requires (valid_check<do_check>)
            HashView<do_check> match(const Object& value, size_t hash) const {
                return {*this, value, hash};
            }
        };

        /* A single node in the overload trie, which holds the topologically-sorted
        edge maps necessary for traversal, insertion, and deletion of candidate
        functions, as well as a (possibly null) terminal function to call if this
        node is the last in a given argument list. */
        struct Node {
            PyObject* func = nullptr;
            Edges positional;
            std::unordered_map<std::string_view, Edges> keyword;

            /// NOTE: A special empty string will be used to represent variadic
            // keyword arguments, which can match any unrecognized names.

            /* Recursively search for a matching function in this node's sub-trie.
            Returns a borrowed reference to a terminal function in the case of a
            match, or null if no match is found, which causes the algorithm to
            backtrack one level and continue searching.

            This method is only called after the first argument has been processed,
            which means the hash will remain stable over the course of the search.
            The mask, however, is a mutable out parameter that will be updated with
            all the edges that were followed to get here, so that the result can be
            easily compared to the required bitmask of the candidate hash, and
            keyword argument order can be normalized. */
            template <typename do_check, typename Container>
                requires (valid_check<do_check>)
            [[nodiscard]] PyObject* search(
                const Params<Container>& key,
                size_t idx,
                size_t hash,
                uint64_t& mask
            ) const {
                if (idx >= key.size()) {
                    return func;
                }
                const Param& param = key[idx];

                // positional arguments have empty names
                if (param.name.empty()) {
                    for (const Edge* edge : positional.template match<do_check>(
                        param.value,
                        hash
                    )) {
                        size_t i = idx + 1;
                        if constexpr (Signature::has_args) {
                            if (edge->kind.variadic()) {
                                const Param* curr;
                                while (
                                    i < key.size() &&
                                    (curr = &key[i])->pos() &&
                                    do_check{}(curr->value, ptr(edge->type))
                                ) {
                                    ++i;
                                }
                                if (i < key.size() && curr->pos()) {
                                    continue;  // failed type check
                                }
                            }
                        }
                        uint64_t temp_mask = mask | edge->mask;
                        PyObject* result = edge->node->template search<do_check>(
                            key,
                            i,
                            hash,
                            temp_mask
                        );
                        if (result) {
                            mask = temp_mask;
                            return result;
                        }
                    }

                // keyword argument names must be looked up in the keyword map.  If
                // the keyword name is not recognized, check for a variadic keyword
                // argument under an empty string, and continue with that.
                } else {
                    auto it = keyword.find(param.name);
                    if (
                        it != keyword.end() ||
                        (it = keyword.find("")) != keyword.end()
                    ) {
                        for (const Edge* edge : it->second.template match<do_check>(
                            param.value,
                            hash
                        )) {
                            uint64_t temp_mask = mask | edge->mask;
                            PyObject* result = edge->node->template search<do_check>(
                                key,
                                idx + 1,
                                hash,
                                temp_mask
                            );
                            if (result) {
                                // Keyword arguments can be given in any order, so
                                // the return value may not always reflect the
                                // deepest node.  To fix this, we compare the
                                // incoming mask to the outgoing mask, and
                                // substitute the result if this node comes later
                                // in the original argument list.
                                if (mask > edge->mask) {
                                    result = func;
                                }
                                mask = temp_mask;
                                return result;
                            }
                        }
                    }
                }

                // return nullptr to backtrack
                return nullptr;
            }

            /* Remove all outgoing edges that match a particular hash. */
            void remove(size_t hash) {
                positional.remove(hash);

                std::vector<std::string_view> dead_kw;
                for (auto& [name, edges] : keyword) {
                    edges.remove(hash);
                    if (edges.empty()) {
                        dead_kw.emplace_back(name);
                    }
                }
                for (std::string_view name : dead_kw) {
                    keyword.erase(name);
                }
            }

            /* Check to see if this node has any outgoing edges. */
            bool empty() const {
                return positional.empty() && keyword.empty();
            }
        };

        std::shared_ptr<Node> root;
        std::set<const Metadata, std::less<>> data;
        mutable std::unordered_map<size_t, PyObject*> cache;

        /* Clear the overload trie, removing all tracked functions. */
        void clear() {
            cache.clear();
            root.reset();
            data.clear();
        }

        /* Manually reset the function's overload cache, forcing paths to be
        recalculated on subsequent calls. */
        void flush() {
            cache.clear();
        }

        /// TODO: these return Objects, not PyObject* pointers.  Rather than
        /// nullptr, it returns None to refer to the base overload.

        /* Search the overload trie for a matching signature, as if calling the
        function.  An `isinstance()` check is performed on each parameter when
        searching the trie.

        This will recursively backtrack until a matching node is found or the trie
        is exhausted, returning nullptr on a failed search.  The results will be
        cached for subsequent invocations.  An error will be thrown if the key does
        not fully satisfy the enclosing parameter list.  Note that variadic
        parameter packs must be expanded prior to calling this function.

        The call operator for `py::Function<>` will delegate to this method after
        constructing a key from the input arguments, in order to resolve dynamic
        overloads.  If it returns null, then the fallback implementation will be
        used instead (which is stored within the function itself).

        Returns a borrowed reference to the terminal function if a match is
        found within the trie, or null otherwise. */
        template <typename Container>
        [[nodiscard]] PyObject* search_instance(const Params<Container>& key) const {
            auto it = cache.find(key.hash);
            if (it != cache.end()) {
                return it->second;
            }
            assert_valid_args<instance>(key);
            size_t hash;
            PyObject* result = recursive_search<instance>(key, hash);
            cache[key.hash] = result;
            return result;
        }

        /* Equivalent to `search_instance()`, except that the key is assumed to
        contain Python type objects rather than instances, and the trie will be
        searched by applying `issubclass()` rather than `isinstance()`.  This is
        used by the `py::Function<>` index operator to allow navigation of the trie
        without concrete input arguments. */
        template <typename Container>
        [[nodiscard]] PyObject* search_subclass(const Params<Container>& key) const {
            auto it = cache.find(key.hash);
            if (it != cache.end()) {
                return it->second;
            }
            assert_valid_args<subclass>(key);
            size_t hash;
            PyObject* result = recursive_search<subclass>(key, hash);
            cache[key.hash] = result;
            return result;
        }

        /* Search the overload trie for a matching signature, as if calling the
        function, but suppressing any errors caused by the signature not satisfying
        the enclosing parameter list.  An `isinstance()` check is performed on each
        parameter when searching the trie.

        This is equivalent to calling `search_instance()` in a try/catch, but
        without any error handling overhead.  Errors are converted into null
        optionals, separate from the null status of the wrapped pointer, which
        retains the same semantics as `search_instance()`.

        This is used by the `.resolve()` method of `py::Function<>`, which
        simulates a call without actually invoking the function, and instead
        returns the overload that would be called if the function were to be
        invoked with the given arguments.

        Returns a borrowed reference to the terminal function if a match is
        found within the trie, or null otherwise. */
        template <typename Container>
        [[nodiscard]] std::optional<PyObject*> get_instance(
            const Params<Container>& key
        ) const {
            auto it = cache.find(key.hash);
            if (it != cache.end()) {
                return it->second;
            }
            if (!check_valid_args<instance>(key)) {
                return std::nullopt;
            }
            size_t hash;
            PyObject* result = recursive_search<instance>(key, hash);
            cache[key.hash] = result;
            return result;
        }

        /* Equivalent to `get_instance()`, except that the key is assumed to
        contain Python type objects rather than instances, and the trie will be
        searched by applying `issubclass()` rather than `isinstance()`.  This is
        used by the `py::Function<>` index operator to allow navigation of the trie
        without concrete input arguments. */
        template <typename Container>
        [[nodiscard]] std::optional<PyObject*> get_subclass(
            const Params<Container>& key
        ) const {
            auto it = cache.find(key.hash);
            if (it != cache.end()) {
                return it->second;
            }
            if (!check_valid_args<subclass>(key)) {
                return std::nullopt;
            }
            size_t hash;
            PyObject* result = recursive_search<subclass>(key, hash);
            cache[key.hash] = result;
            return result;
        }

        /* Filter the overload trie for a given first positional argument, which
        represents an implicit `self` parameter for a bound member function.
        Returns a range adaptor that extracts only the matching functions from the
        metadata set, with extra information encoding their full path through the
        overload trie. */
        [[nodiscard]] BoundView match(const Object& value) const {
            return {*this, value};
        }

        /* Insert a function into the overload trie, throwing a TypeError if it
        does not conform to the enclosing parameter list or if it conflicts with
        another node in the trie.  The key must contain type objects drawn from the
        signature of the inserted function, and `issubclass()` checks will be
        applied to topologically sort the arguments upon insertion.  The function
        can be any callable object as long as it conforms to the given signature. */
        template <typename Container>
        void insert(const Params<Container>& key, const Object& func) {
            // assert the key minimally satisfies the enclosing parameter list
            []<size_t... Is>(
                std::index_sequence<Is...>,
                const Params<Container>& key
            ) {
                size_t idx = 0;
                (assert_viable_overload<Is>(key, idx), ...);
            }(std::make_index_sequence<Signature::n>{}, key);

            // construct the root node if it doesn't already exist
            if (root == nullptr) {
                root = std::make_shared<Node>();
            }

            // if the key is empty, then the root node is the terminal node
            if (key.empty()) {
                if (root->func) {
                    throw TypeError("overload already exists");
                }
                root->func = ptr(func);
                data.emplace(key.hash, 0, func, {});
                cache.clear();
                return;
            }

            // insert an edge linking each parameter in the key
            std::vector<Edge> path;
            path.reserve(key.size());
            Node* curr = root.get();
            int first_keyword = -1;
            int last_required = 0;
            uint64_t required = 0;
            for (int i = 0, end = key.size(); i < end; ++i) {
                try {
                    const Param& param = key[i];
                    path.emplace_back(
                        key.hash,
                        1ULL << i,
                        param.name,
                        param.value,
                        param.kind,
                        nullptr
                    );
                    if (param.posonly()) {
                        curr->positional.insert(path.back());
                        if (!param.opt()) {
                            ++first_keyword;
                            last_required = i;
                            required |= 1ULL << i;
                        }
                    } else if (param.pos()) {
                        curr->positional.insert(path.back());
                        auto [it, _] = curr->keyword.try_emplace(param.name, Edges{});
                        it->second.insert(path.back(), path.back().node);
                        if (!param.opt()) {
                            last_required = i;
                            required |= 1ULL << i;
                        }
                    } else if (param.kw()) {
                        auto [it, _] = curr->keyword.try_emplace(param.name, Edges{});
                        it->second.insert(path.back());
                        if (!param.opt()) {
                            last_required = i;
                            required |= 1ULL << i;
                        }
                    } else if (param.args()) {
                        curr->positional.insert(path.back());
                    } else if (param.kwargs()) {
                        auto [it, _] = curr->keyword.try_emplace("", Edges{});
                        it->second.insert(path.back());
                    } else {
                        throw ValueError("invalid argument kind");
                    }
                    curr = path.back().node.get();

                } catch (...) {
                    curr = root.get();
                    for (int j = 0; j < i; ++j) {
                        const Edge& edge = path[j];
                        curr->remove(edge.hash);
                        curr = edge.node.get();
                    }
                    if (root->empty()) {
                        root.reset();
                    }
                    throw;
                }
            }

            // backfill the terminal functions and full keyword maps for each node
            try {
                std::string_view name;
                int start = key.size() - 1;
                for (int i = start; i > first_keyword; --i) {
                    Edge& edge = path[i];
                    if (i >= last_required) {
                        if (edge.node->func) {
                            throw TypeError("overload already exists");
                        }
                        edge.node->func = ptr(func);
                    }
                    for (int j = first_keyword; j < key.size(); ++j) {
                        Edge& kw = path[j];
                        if (
                            kw.posonly() ||
                            kw.args() ||
                            kw.name == edge.name ||  // incoming edge
                            (i < start && kw.name == name)  // outgoing edge
                        ) {
                            continue;
                        }
                        auto& [it, _] = edge.node->keyword.try_emplace(
                            kw.name,
                            Edges{}
                        );
                        it->second.insert(kw, kw.node);
                    }
                    name = edge.name;
                }

                // extend backfill to the root node
                if (!required) {
                    if (root->func) {
                        throw TypeError("overload already exists");
                    }
                    root->func = ptr(func);
                }
                bool extend_keywords = true;
                for (Edge& edge : path) {
                    if (!edge.posonly()) {
                        break;
                    } else if (!edge.opt()) {
                        extend_keywords = false;
                        break;
                    }
                }
                if (extend_keywords) {
                    for (int j = first_keyword; j < key.size(); ++j) {
                        Edge& kw = path[j];
                        if (kw.posonly() || kw.args()) {
                            continue;
                        }
                        auto& [it, _] = root->keyword.try_emplace(
                            kw.name,
                            Edges{}
                        );
                        it->second.insert(kw, kw.node);
                    }
                }

            } catch (...) {
                Node* curr = root.get();
                for (int i = 0, end = key.size(); i < end; ++i) {
                    const Edge& edge = path[i];
                    curr->remove(edge.hash);
                    if (i >= last_required) {
                        edge.node->func = nullptr;
                    }
                    curr = edge.node.get();
                }
                if (root->empty()) {
                    root.reset();
                }
                throw;
            }

            // track the function and required arguments for the inserted key
            data.emplace(key.hash, required, func, std::move(path));
            cache.clear();
        }

        /* Remove a function from the overload trie and prune any dead-ends that
        lead to it. */
        void remove(const Object& func) {
            for (const Metadata& metadata : data) {
                if (metadata.func.is(func)) {
                    Node* curr = root.get();
                    for (const Edge& edge : metadata.path) {
                        curr->remove(metadata.hash);
                        if (edge.node->func == ptr(func)) {
                            edge.node->func = nullptr;
                        }
                        curr = edge.node.get();
                    }
                    if (root->func == ptr(func)) {
                        root->func = nullptr;
                    }
                    data.erase(metadata.hash);
                    if (data.empty()) {
                        root.reset();
                    }
                    return;
                }
            }
            throw KeyError(repr(func));
        }

    private:

        /* A range adaptor that iterates over the space of overloads that follow a
        given `self` argument, which is used to prune the trie.  When a bound
        method is created, it will use one of these views to correctly forward the
        overload interface. */
        struct BoundView {
            const Overloads& self;
            Object value;

            struct Iterator {
                using iterator_category = std::input_iterator_tag;
                using difference_type = std::ptrdiff_t;
                using value_type = const Metadata;
                using pointer = value_type*;
                using reference = value_type&;

                const Overloads& self;
                const Metadata* curr;
                Edges::OrderedView view;
                std::ranges::iterator_t<typename Edges::OrderedView> it;
                std::ranges::sentinel_t<typename Edges::OrderedView> end;
                std::unordered_set<size_t> visited;

                Iterator(const Overloads& self, const Object& value) :
                    self(self),
                    curr(nullptr),
                    view(self.root->positional.template match<instance>(value)),
                    it(std::ranges::begin(this->view)),
                    end(std::ranges::end(this->view))
                {
                    if (it != end) {
                        curr = self.data.find((*it)->hash);
                        visited.emplace(curr->hash);
                    }
                }

                Iterator& operator++() {
                    while (++it != end) {
                        const Edge* edge = *it;
                        auto lookup = visited.find(edge->hash);
                        if (lookup == visited.end()) {
                            visited.emplace(edge->hash);
                            curr = &*(self.data.find(edge->hash));
                            return *this;
                        }
                    }
                    return *this;
                }

                const Metadata& operator*() const {
                    return *curr;
                }

                friend bool operator==(
                    const Iterator& iter,
                    const impl::Sentinel& sentinel
                ) {
                    return iter.it == iter.end;
                }

                friend bool operator==(
                    const impl::Sentinel& sentinel,
                    const Iterator& iter
                ) {
                    return iter.it == iter.end;
                }

                friend bool operator!=(
                    const Iterator& iter,
                    const impl::Sentinel& sentinel
                ) {
                    return iter.it != iter.end;
                }

                friend bool operator!=(
                    const impl::Sentinel& sentinel,
                    const Iterator& iter
                ) {
                    return iter.it != iter.end;
                }
            };

            Iterator begin() const {
                return {self, value};
            }

            impl::Sentinel end() const {
                return {};
            }
        };

        template <typename do_check, typename Container> requires (valid_check<do_check>)
        static void assert_valid_args(const Params<Container>& key) {
            uint64_t mask = 0;
            for (size_t i = 0, n = key.size(); i < n; ++i) {
                const Param& param = key[i];
                if (param.name.empty()) {
                    const Callback& check = Signature::check(i);
                    if (!check) {
                        throw TypeError(
                            "received unexpected positional argument at index " +
                            std::to_string(i)
                        );
                    }
                    if constexpr (std::same_as<do_check, instance>) {
                        if (!check.isinstance(param.value)) {
                            throw TypeError(
                                "expected positional argument at index " +
                                std::to_string(i) + " to be a subclass of '" +
                                repr(check.type()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                    } else {
                        if (!check.issubclass(param.value)) {
                            throw TypeError(
                                "expected positional argument at index " +
                                std::to_string(i) + " to be a subclass of '" +
                                repr(check.type()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                    }
                    mask |= check.mask;
                } else {
                    const Callback& check = Signature::check(param.name);
                    if (!check) {
                        throw TypeError(
                            "received unexpected keyword argument: '" +
                            std::string(param.name) + "'"
                        );
                    }
                    if (mask & check.mask) {
                        throw TypeError(
                            "received multiple values for argument '" +
                            std::string(param.name) + "'"
                        );
                    }
                    if constexpr (std::same_as<do_check, instance>) {
                        if (!check.isinstance(param.value)) {
                            throw TypeError(
                                "expected argument '" + std::string(param.name) +
                                "' to be a subclass of '" +
                                repr(check.type()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                    } else {
                        if (!check.issubclass(param.value)) {
                            throw TypeError(
                                "expected argument '" + std::string(param.name) +
                                "' to be a subclass of '" +
                                repr(check.type()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                    }
                    mask |= check.mask;
                }
            }
            if ((mask & Signature::required) != Signature::required) {
                uint64_t missing = Signature::required & ~(mask & Signature::required);
                std::string msg = "missing required arguments: [";
                size_t i = 0;
                while (i < n) {
                    if (missing & (1ULL << i)) {
                        const Callback& check = positional_table[i];
                        if (check.name.empty()) {
                            msg += "<parameter " + std::to_string(i) + ">";
                        } else {
                            msg += "'" + std::string(check.name) + "'";
                        }
                        ++i;
                        break;
                    }
                    ++i;
                }
                while (i < n) {
                    if (missing & (1ULL << i)) {
                        const Callback& check = positional_table[i];
                        if (check.name.empty()) {
                            msg += ", <parameter " + std::to_string(i) + ">";
                        } else {
                            msg += ", '" + std::string(check.name) + "'";
                        }
                    }
                    ++i;
                }
                msg += "]";
                throw TypeError(msg);
            }
        }

        template <typename do_check, typename Container> requires (valid_check<do_check>)
        static bool check_valid_args(const Params<Container>& key) {
            uint64_t mask = 0;
            for (size_t i = 0, n = key.size(); i < n; ++i) {
                const Param& param = key[i];
                if (param.name.empty()) {
                    const Callback& check = Signature::check(i);
                    if constexpr (std::same_as<do_check, instance>) {
                        if (!check || !check.isinstance(param.value)) {
                            return false;
                        }
                    } else {
                        if (!check || !check.issubclass(param.value)) {
                            return false;
                        }
                    }
                    mask |= check.mask;
                } else {
                    const Callback& check = Signature::check(param.name);
                    if constexpr (std::same_as<do_check, instance>) {
                        if (
                            !check ||
                            (mask & check.mask) ||
                            !check.isinstance(param.value)
                        ) {
                            return false;
                        }
                    } else {
                        if (
                            !check ||
                            (mask & check.mask) ||
                            !check.issubclass(param.value)
                        ) {
                            return false;
                        }
                    }
                    mask |= check.mask;
                }
            }
            if ((mask & required) != required) {
                return false;
            }
            return true;
        }

        template <typename do_check, typename Container> requires (valid_check<do_check>)
        PyObject* recursive_search(
            const Params<Container>& key,
            size_t& hash
        ) const {
            // account for empty root node and/or key
            if (!root) {
                return nullptr;
            } else if (key.empty()) {
                return root->func;  // may be null
            }

            // The hash is ambiguous for the first argument, so we need to test all
            // edges in order to find a matching key. Otherwise, we already know
            // which key we're tracing, so we can restrict our search to exact
            // matches.  This maintains consistency in the final bitmasks, since
            // each recursive call will only search along a single path after the
            // first edge has been identified.
            const Param& param = key[0];

            // positional arguments have empty names
            if (param.name.empty()) {
                for (const Edge* edge : root->positional.template match<do_check>(
                    param.value
                )) {
                    size_t i = 1;
                    size_t candidate = edge->hash;
                    uint64_t mask = edge->mask;
                    if constexpr (Signature::has_args) {
                        if (edge->kind.variadic()) {
                            const Param* curr;
                            while (
                                i < key.size() &&
                                (curr = &key[i])->pos() &&
                                do_check{}(curr->value, ptr(edge->type))
                            ) {
                                ++i;
                            }
                            if (i < key.size() && curr->pos()) {
                                continue;  // failed type check on positional arg
                            }
                        }
                    }
                    PyObject* result = edge->node->template search<do_check>(
                        key,
                        i,
                        candidate,
                        mask
                    );
                    if (result) {
                        const Metadata& metadata = *(data.find(candidate));
                        if ((mask & metadata.required) == metadata.required) {
                            hash = candidate;
                            return result;
                        }
                    }
                }

            // keyword argument names must be looked up in the keyword map.  If
            // the keyword name is not recognized, check for a variadic keyword
            // argument under an empty string, and continue with that.
            } else {
                auto it = root->keyword.find(param.name);
                if (
                    it != root->keyword.end() ||
                    (it = root->keyword.find("")) != root->keyword.end()
                ) {
                    for (const Edge* edge : it->second.template match<do_check>(
                        param.value
                    )) {
                        size_t candidate = edge->hash;
                        uint64_t mask = edge->mask;
                        PyObject* result = edge->node->template search<do_check>(
                            key,
                            1,
                            candidate,
                            mask
                        );
                        if (result) {
                            const Metadata& metadata = *(data.find(candidate));
                            if ((mask & metadata.required) == metadata.required) {
                                hash = candidate;
                                return result;
                            }
                        }
                    }
                }
            }

            // if all matching edges have been exhausted, then there is no match
            return nullptr;
        }

        template <size_t I, typename Container>
        static void assert_viable_overload(
            const Params<Container>& key,
            size_t& idx
        ) {
            using T = at<I>;
            using Expected = std::remove_cvref_t<impl::python_type<
                typename ArgTraits<at<I>>::type
            >>;
            constexpr auto description = [](const Param& param) {
                if (param.kwonly()) {
                    return "keyword-only";
                } else if (param.kw()) {
                    return "positional-or-keyword";
                } else if (param.pos()) {
                    return "positional";
                } else if (param.args()) {
                    return "variadic positional";
                } else if (param.kwargs()) {
                    return "variadic keyword";
                } else {
                    return "<unknown>";
                }
            };

            if constexpr (ArgTraits<T>::posonly()) {
                if (idx >= key.size()) {
                    if (ArgTraits<T>::name.empty()) {
                        throw TypeError(
                            "missing positional-only argument at index " +
                            std::to_string(idx)
                        );
                    } else {
                        throw TypeError(
                            "missing positional-only argument '" +
                            ArgTraits<T>::name + "' at index " +
                            std::to_string(idx)
                        );
                    }
                }
                const Param& param = key[idx];
                if (!param.posonly()) {
                    if (ArgTraits<T>::name.empty()) {
                        throw TypeError(
                            "expected positional-only argument at index " +
                            std::to_string(idx) + ", not " + description(param)
                        );
                    } else {
                        throw TypeError(
                            "expected argument '" + ArgTraits<T>::name +
                            "' at index " + std::to_string(idx) +
                            " to be positional-only, not " + description(param)
                        );
                    }
                }
                if (!ArgTraits<T>::name.empty() && param.name != ArgTraits<T>::name) {
                    throw TypeError(
                        "expected argument '" + ArgTraits<T>::name +
                        "' at index " + std::to_string(idx) + ", not '" +
                        std::string(param.name) + "'"
                    );
                }
                if (!ArgTraits<T>::opt() && param.opt()) {
                    if (ArgTraits<T>::name.empty()) {
                        throw TypeError(
                            "required positional-only argument at index " +
                            std::to_string(idx) + " must not have a default "
                            "value"
                        );
                    } else {
                        throw TypeError(
                            "required positional-only argument '" +
                            ArgTraits<T>::name + "' at index " +
                            std::to_string(idx) + " must not have a default "
                            "value"
                        );
                    }
                }
                if (!issubclass<Expected>(param.value)) {
                    if (ArgTraits<T>::name.empty()) {
                        throw TypeError(
                            "expected positional-only argument at index " +
                            std::to_string(idx) + " to be a subclass of '" +
                            repr(Type<Expected>()) + "', not: '" +
                            repr(param.value) + "'"
                        );
                    } else {
                        throw TypeError(
                            "expected positional-only argument '" +
                            ArgTraits<T>::name + "' at index " +
                            std::to_string(idx) + " to be a subclass of '" +
                            repr(Type<Expected>()) + "', not: '" +
                            repr(param.value) + "'"
                        );
                    }
                }
                ++idx;

            } else if constexpr (ArgTraits<T>::pos()) {
                if (idx >= key.size()) {
                    throw TypeError(
                        "missing positional-or-keyword argument '" +
                        ArgTraits<T>::name + "' at index " +
                        std::to_string(idx)
                    );
                }
                const Param& param = key[idx];
                if (!param.pos() || !param.kw()) {
                    throw TypeError(
                        "expected argument '" + ArgTraits<T>::name +
                        "' at index " + std::to_string(idx) +
                        " to be positional-or-keyword, not " + description(param)
                    );
                }
                if (param.name != ArgTraits<T>::name) {
                    throw TypeError(
                        "expected positional-or-keyword argument '" +
                        ArgTraits<T>::name + "' at index " +
                        std::to_string(idx) + ", not '" +
                        std::string(param.name) + "'"
                    );
                }
                if (!ArgTraits<T>::opt() && param.opt()) {
                    throw TypeError(
                        "required positional-or-keyword argument '" +
                        ArgTraits<T>::name + "' at index " +
                        std::to_string(idx) + " must not have a default value"
                    );
                }
                if (!issubclass<Expected>(param.value)) {
                    throw TypeError(
                        "expected positional-or-keyword argument '" +
                        ArgTraits<T>::name + "' at index " +
                        std::to_string(idx) + " to be a subclass of '" +
                        repr(Type<Expected>()) + "', not: '" +
                        repr(param.value) + "'"
                    );
                }
                ++idx;

            } else if constexpr (ArgTraits<T>::kw()) {
                if (idx >= key.size()) {
                    throw TypeError(
                        "missing keyword-only argument '" + ArgTraits<T>::name +
                        "' at index " + std::to_string(idx)
                    );
                }
                const Param& param = key[idx];
                if (!param.kwonly()) {
                    throw TypeError(
                        "expected argument '" + ArgTraits<T>::name +
                        "' at index " + std::to_string(idx) +
                        " to be keyword-only, not " + description(param)
                    );
                }
                if (param.name != ArgTraits<T>::name) {
                    throw TypeError(
                        "expected keyword-only argument '" + ArgTraits<T>::name +
                        "' at index " + std::to_string(idx) + ", not '" +
                        std::string(param.name) + "'"
                    );
                }
                if (!ArgTraits<T>::opt() && param.opt()) {
                    throw TypeError(
                        "required keyword-only argument '" + ArgTraits<T>::name +
                        "' at index " + std::to_string(idx) + " must not have a "
                        "default value"
                    );
                }
                if (!issubclass<Expected>(param.value)) {
                    throw TypeError(
                        "expected keyword-only argument '" + ArgTraits<T>::name +
                        "' at index " + std::to_string(idx) +
                        " to be a subclass of '" +
                        repr(Type<Expected>()) + "', not: '" +
                        repr(param.value) + "'"
                    );
                }
                ++idx;

            } else if constexpr (ArgTraits<T>::args()) {
                while (idx < key.size()) {
                    const Param& param = key[idx];
                    if (!(param.pos() || param.args())) {
                        break;
                    }
                    if (!issubclass<Expected>(param.value)) {
                        if (param.name.empty()) {
                            throw TypeError(
                                "expected variadic positional argument at index " +
                                std::to_string(idx) + " to be a subclass of '" +
                                repr(Type<Expected>()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        } else {
                            throw TypeError(
                                "expected variadic positional argument '" +
                                std::string(param.name) + "' at index " +
                                std::to_string(idx) + " to be a subclass of '" +
                                repr(Type<Expected>()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                    }
                    ++idx;
                }

            } else if constexpr (ArgTraits<T>::kwargs()) {
                while (idx < key.size()) {
                    const Param& param = key[idx];
                    if (!(param.kw() || param.kwargs())) {
                        break;
                    }
                    if (!issubclass<Expected>(param.value)) {
                        throw TypeError(
                            "expected variadic keyword argument '" +
                            std::string(param.name) + "' at index " +
                            std::to_string(idx) + " to be a subclass of '" +
                            repr(Type<Expected>()) + "', not: '" +
                            repr(param.value) + "'"
                        );
                    }
                    ++idx;
                }

            } else {
                static_assert(false, "invalid argument kind");
            }
        }
    };

    /* Produce an overload key that matches the enclosing parameter list. */
    static Params<std::array<Param, n>> key() {
        size_t hash = 0;
        return {
            .value = []<size_t... Is>(std::index_sequence<Is...>, size_t& hash) {
                return std::array<Param, n>{_key<Is>(hash)...};
            }(std::make_index_sequence<n>{}, hash),
            .hash = hash
        };
    }

    /* Produce a Python `inspect.Signature` object that matches this signature,
    allowing a matching function to be seamlessly introspected from Python. */
    static Object to_python() {
        /// TODO: return an inspect.Signature object matching this signature.
    }

    /// TODO: is capture even necessary?
    /// -> It's used when converting a Python function into a py::Function<> object,
    /// so probably?
    /// -> Does it need to unbind any partial arguments?

    /* Capture a Python function object and generate a `std::function` that matches the
    enclosing signature.  All arguments will be forwarded to the Python object when the
    function is called, according to the logic set out in Bind<...>. */
    static std::function<typename Unbind::type> capture(const Object& obj) {
        /// TODO: this function type needs to be generated behind the scenes, possibly
        /// as a part of _unbind<...>.  The Args... that are referenced here are not
        /// unbound.
        /// -> I also might need some safeguards in the call logic to ensure that
        /// whatever function is passed into the call operator cannot include any
        /// bound partial arguments?

        struct Func {
            Object obj;

            /// TODO: figure out how to do this with the partial stuff?
            Return operator()(Args... args) const {
                PyObject* result = Bind<Args...>{}(
                    ptr(obj),
                    std::forward<Args>(args)...
                );
                if constexpr (std::is_void_v<Return>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            }
        };
        return Func{obj};
    }
};


/// NOTE: py::Signature<> contains all of the logic necessary to introspect and
/// invoke functions from both languages with the same consistent call semantics.
/// By default, it is enabled for all trivially-introspectable function types,
/// meaning that the underlying function does not accept template parameters or
/// participate in an overload set.  However, it is still possible to support these
/// cases by specializing py::Signature<> for the desired function types, and then
/// redirecting to a canonical signature via inheritance.  Doing so will allow the
/// non-trivial function to be used as the initializer for a `py::def` statement, and
/// possibly also `py::Function` if the normalized signature meets the requirements.
template <typename R, typename... A>
struct Signature<R(A...) noexcept> : Signature<R(A...)> {};
template <typename R, typename... A>
struct Signature<R(*)(A...)> : Signature<R(A...)> {};
template <typename R, typename... A>
struct Signature<R(*)(A...) noexcept> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...)> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) &> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) noexcept> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) & noexcept> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) const> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) const &> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) const noexcept> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) const & noexcept> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) volatile> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) volatile &> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) volatile noexcept> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) volatile & noexcept> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) const volatile> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) const volatile &> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) const volatile noexcept> : Signature<R(A...)> {};
template <typename R, typename C, typename... A>
struct Signature<R(C::*)(A...) const volatile & noexcept> : Signature<R(A...)> {};
template <impl::has_call_operator T>
struct Signature<T> : Signature<decltype(&std::remove_reference_t<T>::operator())> {};


/* A template constraint that controls whether the `py::call()` operator is enabled
for a given C++ function and argument list. */
template <typename F, typename... Args>
concept callable =
    Signature<F>::enable &&
    Signature<F>::args_fit_within_bitset &&
    Signature<F>::proper_argument_order &&
    Signature<F>::no_qualified_arg_annotations &&
    Signature<F>::no_duplicate_args &&
    Signature<F>::template Bind<Args...>::proper_argument_order &&
    Signature<F>::template Bind<Args...>::no_qualified_arg_annotations &&
    Signature<F>::template Bind<Args...>::no_duplicate_args &&
    Signature<F>::template Bind<Args...>::no_conflicting_values &&
    Signature<F>::template Bind<Args...>::no_extra_positional_args &&
    Signature<F>::template Bind<Args...>::no_extra_keyword_args &&
    Signature<F>::template Bind<Args...>::satisfies_required_args &&
    Signature<F>::template Bind<Args...>::can_convert;


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename F, typename... Args>
    requires (
        callable<F, Args...> &&
        Signature<F>::Partial::n == 0 &&
        Signature<F>::Defaults::n == 0
    )
constexpr decltype(auto) call(F&& func, Args&&... args) {
    return Signature<F>{}(
        typename Signature<F>::Defaults{},
        std::forward<F>(func),
        std::forward<Args>(args)...
    );
}


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename F, typename... Args>
    requires (
        callable<F, Args...> &&
        Signature<F>::Partial::n == 0
    )
constexpr decltype(auto) call(
    const typename Signature<F>::Defaults& defaults,
    F&& func,
    Args&&... args
) {
    return Signature<F>{}(
        defaults,
        std::forward<F>(func),
        std::forward<Args>(args)...
    );
}


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename F, typename... Args>
    requires (
        callable<F, Args...> &&
        Signature<F>::Partial::n == 0
    )
constexpr decltype(auto) call(
    typename Signature<F>::Defaults&& defaults,
    F&& func,
    Args&&... args
) {
    return Signature<F>{}(
        std::move(defaults),
        std::forward<F>(func),
        std::forward<Args>(args)...
    );
}


/* A template constraint that controls whether the `py::partial()` operator is enabled
for a given C++ function and argument list. */
template <typename F, typename... Args>
concept partially_callable =
    Signature<F>::enable &&
    !(impl::arg_pack<Args> || ...) &&
    !(impl::kwarg_pack<Args> || ...) &&
    Signature<F>::args_fit_within_bitset &&
    Signature<F>::proper_argument_order &&
    Signature<F>::no_qualified_arg_annotations &&
    Signature<F>::no_duplicate_args &&
    Signature<F>::template Bind<Args...>::proper_argument_order &&
    Signature<F>::template Bind<Args...>::no_qualified_arg_annotations &&
    Signature<F>::template Bind<Args...>::no_duplicate_args &&
    Signature<F>::template Bind<Args...>::no_conflicting_values &&
    Signature<F>::template Bind<Args...>::no_extra_positional_args &&
    Signature<F>::template Bind<Args...>::no_extra_keyword_args &&
    Signature<F>::template Bind<Args...>::can_convert;


/// TODO: I can force the partial function to accept only Arg<> annotations, and then
/// encode the partial arguments directly into the signature.  CTAD can then deduce
/// the partial arguments from the signature and generate a new signature using
/// Arg<"name", type>::bind<type> for each partial argument.  The same strategy could
/// be applied to Function<> in order to represent bound methods and partials
/// uniformly.
/// -> If I do that, the partial would have to store the function as a `std::function`
/// wrapper, so as to erase its type.  In fact, I might be able to have the Function<>
/// type directly store a partial<> object, which would unify some of the interface.
/// The C++ call operator could then be implemented as a call directly to the partial
/// object, after checking for overloads, etc.
/// -> Maybe I keep partial the way it is for maximum efficiency, and then just template
/// it on the `std::function` type when I need to store it in a Function<> object.
/// That should offer the best of both worlds.


/* Construct a partial function object that captures a C++ function and a subset of its
arguments, which can be used to invoke the function later with the remaining arguments.
Arguments and default values are given in the same style as `call()`, and will be
stored internally within the partial object, forcing a copy in the case of lvalue
inputs.  When the partial is called, an additional copy may be made if the function
expects a temporary or rvalue reference, so as not to modify the stored arguments.  If
the partial is called as an rvalue (by moving it, for example), then the second copy
can be avoided, and the stored arguments will be moved directly into the function call.

Note that the function signature cannot contain any template parameters (including auto
arguments), as the function signature must be known unambiguously at compile time to
implement the required matching.

The returned partial is a thin proxy that only implements the call operator and a
handful of introspection methods.  It also allows transparent access to the decorated
function via the `*` and `->` operators. */
template <typename Func, typename... Args>
    requires (
        partially_callable<Func, Args...> &&
        Signature<Func>::Partial::n == 0
    )
struct def : impl::defTag {
    using partial = Signature<Func>::template Partial<Args...>;
    using signature = partial::signature;

    typename Signature<Func>::Defaults defaults;
    std::remove_cvref_t<Func> func;
    partial parts;

    /// TODO: this class should also expose `.bind()` and `.unbind()`, as well as the
    /// `>>` operator for chaining.  Also, all of this logic needs to be updated so
    /// that these things have a usable public interface.

    static constexpr size_t n = sizeof...(Args);
    /// TODO: other introspection fields forwarded from Arguments<>.  These should
    /// probably be similar to the introspection fields in `py::Function<>`, except
    /// that they should account for the bound partial arguments.  `n` would be
    /// reduced by the number of arguments in the partial, and the other fields
    /// will be adjusted accordingly.

    template <typename... Values>
    using Bind = partial::template Bind<Values...>;
    /// TODO: ideally, this Bind<> struct would expose a call operator that does all
    /// the necessary argument manipulation, so it can be used symmetrically to the
    /// other Bind<> implementations.  It might also allow the creation of an overload
    /// key, which completes the interface.

    template <impl::is<Func> F> requires (Signature<F>::Defaults::n == 0)
    explicit constexpr def(F&& func, Args... args) :
        defaults(),
        func(std::forward<F>(func)),
        parts(std::forward<Args>(args)...)
    {}

    explicit constexpr def(
        const typename Signature<Func>::Defaults& defaults,
        Func func,
        Args... args
    ) :
        defaults(defaults),
        func(std::forward<Func>(func)),
        parts(std::forward<Args>(args)...)
    {}

    explicit constexpr def(
        typename Signature<Func>::Defaults&& defaults,
        Func func,
        Args... args
    ) :
        defaults(std::move(defaults)),
        func(std::forward<Func>(func)),
        parts(std::forward<Args>(args)...)
    {}

    [[nodiscard]] std::remove_cvref_t<Func>& operator*() {
        return func;
    }

    [[nodiscard]] constexpr const std::remove_cvref_t<Func>& operator*() const {
        return func;
    }

    [[nodiscard]] std::remove_cvref_t<Func>* operator->() {
        return &func;
    }

    [[nodiscard]] constexpr const std::remove_cvref_t<Func>* operator->() const {
        return &func;
    }

    template <size_t I> requires (I < n)
    [[nodiscard]] constexpr decltype(auto) get() const {
        return parts.template get<I>();
    }

    template <size_t I> requires (I < n)
    [[nodiscard]] decltype(auto) get() && {
        return std::move(parts).template get<I>();
    }

    template <StaticStr name> requires (partial::template has<name>)
    [[nodiscard]] constexpr decltype(auto) get() const {
        return parts.template get<name>();
    }

    template <StaticStr name> requires (partial::template has<name>)
    [[nodiscard]] decltype(auto) get() && {
        return std::move(parts).template get<name>();
    }

    template <typename... Values>
        requires (
            Bind<Values...>::proper_argument_order &&
            Bind<Values...>::no_qualified_arg_annotations &&
            Bind<Values...>::no_duplicate_args &&
            Bind<Values...>::no_extra_positional_args &&
            Bind<Values...>::no_extra_keyword_args &&
            Bind<Values...>::no_conflicting_values &&
            Bind<Values...>::satisfies_required_args &&
            Bind<Values...>::can_convert
        )
    constexpr decltype(auto) operator()(this auto&& self, Values&&... values) {
        return std::forward<decltype(self)>(self).parts(
            std::forward<decltype(self)>(self).defaults,
            std::forward<decltype(self)>(self).func,
            std::forward<Values>(values)...
        );
    }
};


/// TODO: first deduction guide is unnecessary, or I need to add more to the last 2.
/// One or the other.
template <typename F>
    requires (
        partially_callable<F> &&
        Signature<F>::Partial::n == 0 &&
        Signature<F>::Defaults::n == 0
    )
explicit def(F) -> def<F>;
template <typename F, typename... A>
    requires (
        partially_callable<F, A...> &&
        Signature<F>::Partial::n == 0 &&
        Signature<F>::Defaults::n == 0
    )
explicit def(F, A&&...) -> def<F, A...>;
template <typename F, typename... A>
    requires (
        partially_callable<F, A...> &&
        Signature<F>::Partial::n == 0
    )
explicit def(typename Signature<F>::Defaults&&, F, A&&...) -> def<F, A...>;
template <typename F, typename... A>
    requires (
        partially_callable<F, A...> &&
        Signature<F>::Partial::n == 0
    )
explicit def(const typename Signature<F>::Defaults&, F, A&&...) -> def<F, A...>;


template <impl::inherits<impl::defTag> T>
struct Signature<T> : std::remove_reference_t<T>::signature {};


template <typename Self, typename... Args>
    requires (
        __call__<Self, Args...>::enable &&
        std::convertible_to<typename __call__<Self, Args...>::type, Object> && (
            std::is_invocable_r_v<
                typename __call__<Self, Args...>::type,
                __call__<Self, Args...>,
                Self,
                Args...
            > || (
                !std::is_invocable_v<__call__<Self, Args...>, Self, Args...> &&
                impl::has_cpp<Self> &&
                std::is_invocable_r_v<
                    typename __call__<Self, Args...>::type,
                    impl::cpp_type<Self>,
                    Args...
                >
            ) || (
                !std::is_invocable_v<__call__<Self, Args...>, Self, Args...> &&
                !impl::has_cpp<Self> &&
                std::derived_from<typename __call__<Self, Args...>::type, Object> &&
                __getattr__<Self, "__call__">::enable &&
                impl::inherits<typename __getattr__<Self, "__call__">::type, impl::FunctionTag>
            )
        )
    )
decltype(auto) Object::operator()(this Self&& self, Args&&... args) {
    if constexpr (std::is_invocable_v<__call__<Self, Args...>, Self, Args...>) {
        return __call__<Self, Args...>{}(
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );

    } else if constexpr (impl::has_cpp<Self>) {
        return from_python(std::forward<Self>(self))(
            std::forward<Args>(args)...
        );
    } else {
        return getattr<"__call__">(std::forward<Self>(self))(
            std::forward<Args>(args)...
        );
    }
}


////////////////////////
////    FUNCTION    ////
////////////////////////


template <typename F = Object(Arg<"*args", Object>, Arg<"**kwargs", Object>)>
    requires (
        impl::canonical_function_type<F> &&
        Signature<F>::args_fit_within_bitset &&
        Signature<F>::no_qualified_args &&
        Signature<F>::no_qualified_return &&
        Signature<F>::proper_argument_order &&
        Signature<F>::no_duplicate_args &&
        Signature<F>::args_are_python &&
        Signature<F>::return_is_python
    )
struct Function;


/// TODO: CTAD guides take in a function annotated with Arg<>, which has no bound
/// arguments, as well as a list of partial arguments.  It then extends the annotation
/// for each argument, synthesizing a new signature that binds the partial arguments
/// and discards any defaults that might be present for those arguments.  The
/// synthesized signature is what is then used to construct and call the Function<>
/// object.


template <typename F, typename... Partial>
    requires (partially_callable<F, Partial...> && Defaults<F>::n == 0)
Function(F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial> requires (partially_callable<F, Partial...>)
Function(Defaults<F>, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial>
    requires (partially_callable<F, Partial...> && Defaults<F>::n == 0)
Function(std::string, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial> requires (partially_callable<F, Partial...>)
Function(std::string, Defaults<F>, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial>
    requires (partially_callable<F, Partial...> && Defaults<F>::n == 0)
Function(std::string, std::string, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial> requires (partially_callable<F, Partial...>)
Function(std::string, std::string, Defaults<F>, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;


namespace impl {

    /* decorators with and without arguments:

    def name(_func=None, *, key1=value1, key2=value2, ...):
        def decorator_name(func):
            ...  # Create and return a wrapper function.
            return func

        if _func is None:
            return decorator_name
        else:
            return decorator_name(_func)

    */
    /// TODO: ^ that is really hard to do from C++, particularly as it relates to
    /// function capture.

    /* Inspect an annotated Python function and extract its inline type hints so that
    they can be translated into a corresponding parameter list. */
    struct Inspect {
    private:

        static Object import_typing() {
            PyObject* typing = PyImport_Import(ptr(template_string<"typing">()));
            if (typing == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(typing);
        }

        static Object import_types() {
            PyObject* types = PyImport_Import(ptr(template_string<"types">()));
            if (types == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(types);
        }

        Object get_signature() const {
            // signature = inspect.signature(func)
            // hints = typing.get_type_hints(func)
            // signature = signature.replace(
            //      return_annotation=hints.get("return", inspect.Parameter.empty),
            //      parameters=[
            //         p if p.annotation is inspect.Parameter.empty else
            //         p.replace(annotation=hints[p.name])
            //         for p in signature.parameters.values()
            //     ]
            // )
            Object signature = getattr<"signature">(inspect)(func);
            Object hints = getattr<"get_type_hints">(typing)(
                func,
                arg<"include_extras"> = reinterpret_borrow<Object>(Py_True)
            );
            Object empty = getattr<"empty">(getattr<"Parameter">(inspect));
            Object parameters = getattr<"values">(
                getattr<"parameters">(signature)
            )();
            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            Object new_params = reinterpret_steal<Object>(PyList_New(len));
            Py_ssize_t idx = 0;
            for (Object param : parameters) {
                Object annotation = getattr<"annotation">(param);
                if (!annotation.is(empty)) {
                    annotation = reinterpret_steal<Object>(PyDict_GetItemWithError(
                        ptr(hints),
                        ptr(getattr<"name">(param))
                    ));
                    if (annotation.is(nullptr)) {
                        if (PyErr_Occurred()) {
                            Exception::from_python();
                        } else {
                            throw KeyError(
                                "no type hint for parameter: " + repr(param)
                            );
                        }
                    }
                    param = getattr<"replace">(param)(
                        arg<"annotation"> = annotation
                    );
                }
                // steals a reference
                PyList_SET_ITEM(ptr(new_params), idx++, release(param));
            }
            Object return_annotation = reinterpret_steal<Object>(PyDict_GetItem(
                ptr(hints),
                ptr(template_string<"return">())
            ));
            if (return_annotation.is(nullptr)) {
                return_annotation = empty;
            }
            return getattr<"replace">(signature)(
                arg<"return_annotation"> = return_annotation,
                arg<"parameters"> = new_params
            );
        }

        Object get_parameters() const {
            Object values = getattr<"values">(
                getattr<"parameters">(signature)
            )();
            Object result = reinterpret_steal<Object>(
                PySequence_Tuple(ptr(values))
            );
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }

        static Object to_union(std::set<Object>& keys, const Object& Union) {
            Object key = reinterpret_steal<Object>(PyTuple_New(keys.size()));
            if (key.is(nullptr)) {
                Exception::from_python();
            }
            size_t i = 0;
            for (const Object& type : keys) {
                PyTuple_SET_ITEM(ptr(key), i++, Py_NewRef(ptr(type)));
            }
            Object specialization = reinterpret_steal<Object>(PyObject_GetItem(
                ptr(Union),
                ptr(key)
            ));
            if (specialization.is(nullptr)) {
                Exception::from_python();
            }
            return specialization;
        }

    public:
        Object bertrand = [] {
            PyObject* bertrand = PyImport_Import(
                ptr(template_string<"bertrand">())
            );
            if (bertrand == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(bertrand);
        }();
        Object inspect = [] {
            PyObject* inspect = PyImport_Import(ptr(template_string<"inspect">()));
            if (inspect == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(inspect);
        }();
        Object typing = import_typing();

        Object func;
        Object signature;
        Object parameters;
        size_t seed;
        size_t prime;

        Inspect(
            const Object& func,
            size_t seed,
            size_t prime
        ) : func(func),
            signature(get_signature()),
            parameters(get_parameters()),
            seed(seed),
            prime(prime)
        {}

        Inspect(
            Object&& func,
            size_t seed,
            size_t prime
        ) : func(std::move(func)),
            signature(get_signature()),
            parameters(get_parameters()),
            seed(seed),
            prime(prime)
        {}

        Inspect(const Inspect& other) = delete;
        Inspect(Inspect&& other) = delete;
        Inspect& operator=(const Inspect& other) = delete;
        Inspect& operator=(Inspect&& other) noexcept = delete;

        /* Get the `inspect.Parameter` object at a particular index of the introspected
        function signature. */
        Object at(size_t i) const {
            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            } else if (i >= len) {
                throw IndexError("index out of range");
            }
            return reinterpret_borrow<Object>(
                PyTuple_GET_ITEM(ptr(parameters), i)
            );
        }

        /* A callback function to use when parsing inline type hints within a Python
        function declaration. */
        struct Callback {
            std::string id;
            std::function<bool(Object, std::set<Object>&)> func;
            bool operator()(const Object& hint, std::set<Object>& out) const {
                return func(hint, out);
            }
        };

        /* Initiate a search of the callback map in order to parse a Python-style type
        hint.  The search stops at the first callback that returns true, otherwise the
        hint is interpreted as either a single type if it is a Python class, or a
        generic `object` type otherwise. */
        static void parse(Object hint, std::set<Object>& out) {
            for (const Callback& cb : callbacks) {
                if (cb(hint, out)) {
                    return;
                }
            }

            // Annotated types are unwrapped and reprocessed if not handled by a callback
            Object typing = import_typing();
            Object origin = getattr<"get_origin">(typing)(hint);
            if (origin.is(getattr<"Annotated">(typing))) {
                parse(reinterpret_borrow<Object>(PyTuple_GET_ITEM(
                    ptr(getattr<"get_args">(typing)(hint)),
                    0
                )), out);
                return;
            }

            // unrecognized hints are assumed to implement `issubclass()`
            out.emplace(std::move(hint));
        }

        /* In order to provide custom handlers for Python type hints, each annotation
        will be passed through a series of callbacks that convert it into a flat list
        of Python types, which will be used to generate the final overload keys.

        Each callback is tested in order and expected to return true if it can handle
        the hint, in which case the search terminates and the final state of the `out`
        vector will be pushed into the set of possible overload keys.  If no callback
        can handle a given hint, then it is interpreted as a single type if it is a
        Python class, or as a generic `object` type otherwise, which is equivalent to
        Python's `typing.Any`.  Some type hints, such as `Union` and `Optional`, will
        recursively search the callback map in order to split the hint into its
        constituent types, which will be registered as unique overloads.

        Note that `inspect.get_type_hints(include_extras=True)` is used to extract the
        type hints from the function signature, meaning that stringized annotations and
        forward references will be normalized before any callbacks are invoked.  The
        `include_extras` flag is used to ensure that `typing.Annotated` hints are
        preserved, so that they can be interpreted by the callback map if necessary.
        The default behavior in this case is to simply extract the underlying type,
        but custom callbacks can be added to interpret these annotations as needed. */
        inline static std::vector<Callback> callbacks {
            /// NOTE: Callbacks are linearly searched, so more common constructs should
            /// be generally placed at the front of the list for performance reasons.
            {
                /// TODO: handling GenericAlias types is going to be fairly complicated, 
                /// and will require interactions with the global type map, and thus a
                /// forward declaration here.
                "types.GenericAlias",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object types = import_types();
                    int rc = PyObject_IsInstance(
                        ptr(hint),
                        ptr(getattr<"GenericAlias">(types))
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
                        Object typing = import_typing();
                        Object origin = getattr<"get_origin">(typing)(hint);
                        /// TODO: search in type map or fall back to Object
                        Object args = getattr<"get_args">(typing)(hint);
                        /// TODO: parametrize the bertrand type with the same args.  If
                        /// this causes a template error, then fall back to its default
                        /// specialization (i.e. list[Object]).
                        throw NotImplementedError(
                            "generic type subscription is not yet implemented"
                        );
                        return true;
                    }
                    return false;
                }
            },
            {
                "types.UnionType",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object types = import_types();
                    int rc = PyObject_IsInstance(
                        ptr(hint),
                        ptr(getattr<"UnionType">(types))
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
                        Object args = getattr<"get_args">(types)(hint);
                        Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                        for (Py_ssize_t i = 0; i < len; ++i) {
                            parse(reinterpret_borrow<Object>(
                                PyTuple_GET_ITEM(ptr(args), i)
                            ), out);
                        }
                        return true;
                    }
                    return false;
                }
            },
            {
                /// NOTE: when `typing.get_origin()` is called on a `typing.Optional`,
                /// it returns `typing.Union`, meaning that this handler will also
                /// implicitly cover `Optional` annotations for free.
                "typing.Union",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = getattr<"get_origin">(typing)(hint);
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Union">(typing))) {
                        Object args = getattr<"get_args">(typing)(hint);
                        Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                        for (Py_ssize_t i = 0; i < len; ++i) {
                            parse(reinterpret_borrow<Object>(
                                PyTuple_GET_ITEM(ptr(args), i)
                            ), out);
                        }
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.Any",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = getattr<"get_origin">(typing)(hint);
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Any">(typing))) {
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyBaseObject_Type)
                        ));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.TypeAliasType",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    int rc = PyObject_IsInstance(
                        ptr(hint),
                        ptr(getattr<"TypeAliasType">(typing))
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
                        parse(getattr<"__value__">(hint), out);
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.Literal",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = getattr<"get_origin">(typing)(hint);
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Literal">(typing))) {
                        Object args = getattr<"get_args">(typing)(hint);
                        if (args.is(nullptr)) {
                            Exception::from_python();
                        }
                        Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                        for (Py_ssize_t i = 0; i < len; ++i) {
                            out.emplace(reinterpret_borrow<Object>(
                                reinterpret_cast<PyObject*>(Py_TYPE(
                                    PyTuple_GET_ITEM(ptr(args), i)
                                ))
                            ));
                        }
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.LiteralString",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    if (hint.is(getattr<"LiteralString">(typing))) {
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyUnicode_Type)
                        ));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.AnyStr",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    if (hint.is(getattr<"AnyStr">(typing))) {
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyUnicode_Type)
                        ));
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyBytes_Type)
                        ));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.NoReturn",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    if (
                        hint.is(getattr<"NoReturn">(typing)) ||
                        hint.is(getattr<"Never">(typing))
                    ) {
                        /// NOTE: this handler models NoReturn/Never by not pushing a
                        /// type to the `out` set, giving an empty return type.
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.TypeGuard",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = getattr<"get_origin">(typing)(hint);
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"TypeGuard">(typing))) {
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyBool_Type)
                        ));
                        return true;
                    }
                    return false;
                }
            }
        };

        /* Get the return type of the function, using the same callback handlers as
        the parameters.  May return a specialization of `Union` if multiple return
        types are valid, or `NoneType` for void and noreturn functions.  The result is
        always assumed to implement python-style `isinstance()` and `issubclass()`
        checks. */
        const Object& returns() const {
            if (!_returns.is(nullptr)) {
                return _returns;
            }
            std::set<Object> keys;
            Object hint = getattr<"return_annotation">(signature);
            if (hint.is(getattr<"empty">(signature))) {
                keys.insert(reinterpret_borrow<Object>(
                    reinterpret_cast<PyObject*>(&PyBaseObject_Type)
                ));
            } else {
                parse(hint, keys);
            }
            if (keys.empty()) {
                _returns = reinterpret_borrow<Object>(
                    reinterpret_cast<PyObject*>(Py_TYPE(Py_None))
                );
            } else if (keys.size() == 1) {
                _returns = std::move(*keys.begin());
            } else {
                _returns = to_union(
                    keys,
                    getattr<"Union">(bertrand)
                );
            }
            return _returns;
        }

        /* Convert the introspected signature into a lightweight C++ template key,
        suitable for insertion into a function's overload trie. */
        const Params<std::vector<Param>>& key() const {
            if (key_initialized) {
                return _key;
            }

            Object Parameter = getattr<"Parameter">(inspect);
            Object empty = getattr<"empty">(Parameter);
            Object POSITIONAL_ONLY = getattr<"POSITIONAL_ONLY">(Parameter);
            Object POSITIONAL_OR_KEYWORD = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            Object VAR_POSITIONAL = getattr<"VAR_POSITIONAL">(Parameter);
            Object KEYWORD_ONLY = getattr<"KEYWORD_ONLY">(Parameter);
            Object VAR_KEYWORD = getattr<"VAR_KEYWORD">(Parameter);

            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            _key.value.reserve(len);
            for (Object param : parameters) {
                std::string_view name = get_parameter_name(
                    ptr(getattr<"name">(param)
                ));

                ArgKind category;
                Object kind = getattr<"kind">(param);
                if (kind.is(POSITIONAL_ONLY)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::POS :
                        ArgKind::POS | ArgKind::OPT;
                } else if (kind.is(POSITIONAL_OR_KEYWORD)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::POS | ArgKind::KW :
                        ArgKind::POS | ArgKind::KW | ArgKind::OPT;
                } else if (kind.is(KEYWORD_ONLY)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::KW :
                        ArgKind::KW | ArgKind::OPT;
                } else if (kind.is(VAR_POSITIONAL)) {
                    category = ArgKind::POS | ArgKind::VARIADIC;
                } else if (kind.is(VAR_KEYWORD)) {
                    category = ArgKind::KW | ArgKind::VARIADIC;
                } else {
                    throw TypeError("unrecognized parameter kind: " + repr(kind));
                }

                std::set<Object> types;
                Object hint = getattr<"annotation">(param);
                if (hint.is(empty)) {
                    types.emplace(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyBaseObject_Type)
                    ));
                } else {
                    parse(hint, types);
                }
                if (types.empty()) {
                    throw TypeError(
                        "invalid type hint for parameter '" + std::string(name) +
                        "': " + repr(getattr<"annotation">(param))
                    );
                } else if (types.size() == 1) {
                    _key.value.emplace_back(name, std::move(*types.begin()), category);
                    _key.hash = hash_combine(
                        _key.hash,
                        _key.value.back().hash(seed, prime)
                    );
                } else {
                    _key.value.emplace_back(
                        name,
                        to_union(types, getattr<"Union">(bertrand)),
                        category
                    );
                    _key.hash = hash_combine(
                        _key.hash,
                        _key.value.back().hash(seed, prime)
                    );
                }
            }
            key_initialized = true;
            return _key;
        }

        /* Convert the inspected signature into a valid template key for the
        `bertrand.Function` class on the Python side. */
        const Object& template_key() const {
            if (!_template_key.is(nullptr)) {
                return _template_key;
            }

            Object Parameter = getattr<"Parameter">(inspect);
            Object empty = getattr<"empty">(Parameter);
            Object POSITIONAL_ONLY = getattr<"POSITIONAL_ONLY">(Parameter);
            Object POSITIONAL_OR_KEYWORD = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            Object VAR_POSITIONAL = getattr<"VAR_POSITIONAL">(Parameter);
            Object KEYWORD_ONLY = getattr<"KEYWORD_ONLY">(Parameter);

            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            Object result = reinterpret_steal<Object>(PyTuple_New(len + 1));
            if (result.is(nullptr)) {
                Exception::from_python();
            }

            // first element lists type of bound `self` argument and return type as a
            // slice
            Object returns = this->returns();
            if (returns.is(reinterpret_cast<PyObject*>(Py_TYPE(Py_None)))) {
                returns = None;
            }
            Object cls = getattr<"__self__">(func, None);
            if (PyType_Check(ptr(cls))) {
                PyObject* slice = PySlice_New(
                    ptr(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyType_Type)
                    )[cls]),
                    Py_None,
                    ptr(returns)
                );
                if (slice == nullptr) {
                    Exception::from_python();
                }
                PyTuple_SET_ITEM(ptr(result), 0, slice);  // steals a reference
            } else {
                PyObject* slice = PySlice_New(
                    reinterpret_cast<PyObject*>(Py_TYPE(ptr(cls))),
                    Py_None,
                    ptr(returns)
                );
                if (slice == nullptr) {
                    Exception::from_python();
                }
                PyTuple_SET_ITEM(ptr(result), 0, slice);  // steals a reference
            }

            /// remaining elements are parameters, with slices, '/', '*', etc.
            const Params<std::vector<Param>>& key = this->key();
            Py_ssize_t offset = 1;
            Py_ssize_t posonly_idx = std::numeric_limits<Py_ssize_t>::max();
            Py_ssize_t kwonly_idx = std::numeric_limits<Py_ssize_t>::max();
            for (Py_ssize_t i = 0; i < len; ++i) {
                const Param& param = key[i];
                if (param.posonly()) {
                    posonly_idx = i;
                    if (!param.opt()) {
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            Py_NewRef(ptr(param.value))
                        );
                    } else {
                        PyObject* slice = PySlice_New(
                            ptr(param.value),
                            Py_Ellipsis,
                            Py_None
                        );
                        if (slice == nullptr) {
                            Exception::from_python();
                        }
                        PyTuple_SET_ITEM(ptr(result), i + offset, slice);
                    }
                } else {
                    // insert '/' delimiter if there are any posonly arguments
                    if (i > posonly_idx) {
                        PyObject* grow;
                        if (_PyTuple_Resize(&grow, len + offset + 1) < 0) {
                            Exception::from_python();
                        }
                        result = reinterpret_steal<Object>(grow);
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            release(template_string<"/">())
                        );
                        ++offset;

                    // insert '*' delimiter if there are any kwonly arguments
                    } else if (
                        param.kwonly() &&
                        kwonly_idx == std::numeric_limits<Py_ssize_t>::max()
                    ) {
                        kwonly_idx = i;
                        PyObject* grow;
                        if (_PyTuple_Resize(&grow, len + offset + 1) < 0) {
                            Exception::from_python();
                        }
                        result = reinterpret_steal<Object>(grow);
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            release(template_string<"*">())
                        );
                        ++offset;
                    }

                    // insert parameter identifier
                    Object name = reinterpret_steal<Object>(
                        PyUnicode_FromStringAndSize(
                            param.name.data(),
                            param.name.size()
                        )
                    );
                    if (name.is(nullptr)) {
                        Exception::from_python();
                    }
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        ptr(param.value),
                        param.opt() ? Py_Ellipsis : Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(ptr(result), i + offset, slice);
                }
            }
            _template_key = result;
            return _template_key;
        }

    private:
        mutable bool key_initialized = false;
        mutable Params<std::vector<Param>> _key = {std::vector<Param>{}, 0};
        mutable Object _returns = reinterpret_steal<Object>(nullptr);
        mutable Object _template_key = reinterpret_steal<Object>(nullptr);
    };

    /* A descriptor proxy for an unbound Bertrand function, which enables the
    `func.method` access specifier.  Unlike the others, this descriptor is never
    attached to a type, it merely forwards the underlying function to match Python's
    PyFunctionObject semantics, and leverage optimizations in the type flags, etc. */
    struct Method : PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A descriptor that binds a Bertrand function as an instance method of a Python
class.

Notes
-----
The `func.method` accessor is actually a property that returns an unbound
instance of this type.  That instance then implements a call operator, which
allows it to be used as a decorator that self-attaches the descriptor to a
Python class.

This architecture allows the unbound descriptor to implement the `&` and `|`
operators, which allow for extremely simple structural types in Python:

```
@bertrand
def func(x: foo | (bar.method & baz.property) | qux.staticmethod) -> int:
    ...
```

This syntax is not available in C++, which requires the use of explicit
`Union<...>` and `Intersection<...>` types instead.

Note that unlike the other descriptors, this one is not actually attached to
the decorated type.  Instead, it is used to expose the structural operators for
consistency with the rest of the function interface, and will attach the
underlying function (rather than this descriptor) when invoked.  This allows
for optimizations in the underlying CPython API, and conforms to Python's
ordinary function semantics.

Examples
--------
This descriptor is primarily used via the `@func.method` decorator of a
Bertrand function, which automatically binds the function to the decorated
type.

>>> import bertrand
>>> @bertrand
... def foo(self, x: int) -> int:
...     return x + 1
...
>>> @foo.method
... class Bar:
...     pass
...
>>> Bar().foo(1)
2

It is also possible to create a Bertrand method in-place by explicitly calling
the `@bertrand` decorator on a standard method declaration, just like you would
for a non-member Bertrand function.

>>> class Baz:
...     @bertrand
...     def foo(self, x: int) -> int:
...         return x + 1
...
>>> Baz().foo(1)
2

Both syntaxes achieve the same effect, but the first allows the function to
be defined separately from the class, enables UFCS, and allows for easy
structural typing and function overloading.  It is thus the preferred way of
defining methods in Bertrand.

Additionally, the result of the `bertrand.method` property can be used in
`isinstance()` and `issubclass()` checks in order to enforce the structural
types created by the `&` and `|` operators.

>>> @bertrand
... def foo(cls, x: int) -> int:
...     return x + 1
...
>>> @foo.classmethod
... class Bar:
...     pass
...
>>> isinstance(Bar(), foo.method)  # Bar() implements foo as an instance method
True
>>> issubclass(Bar, foo.method)  # Bar implements foo as an instance method
True

This works by checking whether the operand has an attribute `foo`, which is a
callable with the same signature as the free-standing function.  Note that
this does not strictly require the use of `@foo.method`, although that is by
far the easiest way to guarantee that this check always succeeds.  Technically,
any type for which `obj.foo(...)` is well-formed will pass the check,
regardless of how that method is exposed, making this a true structural type
check.)doc";

        static PyTypeObject __type__;

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object func;

        explicit Method(const Object& func) : func(func) {}
        explicit Method(Object&& func) : func(std::move(func)) {}

        static void __dealloc__(Method* self) noexcept {
            self->~Method();
        }

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                Method* self = reinterpret_cast<Method*>(
                    type->tp_alloc(type, 0)
                );
                if (self == nullptr) {
                    return nullptr;
                }
                try {
                    new (self) Method(None);
                } catch (...) {
                    Py_DECREF(self);
                    throw;
                }
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            Method* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                const char* kwlist[] = {nullptr};
                PyObject* func;
                if (PyArg_ParseTupleAndKeywords(
                    args,
                    kwargs,
                    "O:method",
                    const_cast<char**>(kwlist),
                    &func
                )) {
                    return -1;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object wrapped = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(func)
                );
                getattr<"bind_partial">(
                    getattr<"__signature__">(wrapped)
                )(None);
                self->func = wrapped;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __wrapped__(Method* self, void*) noexcept {
            return Py_NewRef(ptr(self->func));
        }

        static PyObject* __call__(
            Method* self,
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                /// TODO: accept a single, optional positional-only argument plus
                /// optional keyword-only args.  Then, within the body of this
                /// function, create another function that takes a single argument,
                /// which is the actual decorator itself.  This gets complicated, but
                /// is necessary to allow easy use from Python itself.

                /// TODO: maybe I return a PyFunction here?  I can maybe use the Code
                /// constructor to create the internal function?  That gets a little
                /// spicy, but maybe I can use py::Function<> itself instead?  That
                /// would allow me to use a capturing lambda here, which is much
                /// closer to the Python syntax.  That would require a forward
                /// declaration here, though.  And/or the CTAD constructors would
                /// need to be moved up above this point.



                if (kwnames) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "method() does not accept keyword arguments"
                    );
                    return nullptr;
                }
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "method() requires exactly one positional argument"
                    );
                    return nullptr;
                }
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    ptr(self->func),
                    cls,
                    self
                };
                return PyObject_VectorcallMethod(
                    ptr(template_string<"_bind_method">()),
                    forward,
                    3,
                    nullptr
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get__(
            Method* self,
            PyObject* obj,
            PyObject* type
        ) noexcept { 
            PyTypeObject* cls = Py_TYPE(ptr(self->func));
            return cls->tp_descr_get(ptr(self->func), obj, type);
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<Method*>(lhs)->func),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<Method*>(rhs)->func)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<Method*>(lhs)->func),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<Method*>(rhs)->func)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(Method* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(obj, ptr(self->func));
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(Method* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(cls, ptr(self->func));
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(Method* self) noexcept {
            try {
                std::string str = "<method(" + repr(self->func) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__)
        };

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__wrapped__",
                reinterpret_cast<getter>(&__wrapped__),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    PyTypeObject Method::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(Method).name(),
        .tp_basicsize = sizeof(Method),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&Method::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&Method::__repr__),
        .tp_as_number = &Method::number,
        .tp_call = PyVectorcall_Call,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(Method::__doc__),
        .tp_methods = Method::methods,
        .tp_getset = Method::getset,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&Method::__get__),
        .tp_init = reinterpret_cast<initproc>(&Method::__init__),
        .tp_new = reinterpret_cast<newfunc>(&Method::__new__),
        .tp_vectorcall_offset = offsetof(Method, __vectorcall__)
    };

    /* A `@classmethod` descriptor for a Bertrand function type, which references an
    unbound function and produces bound equivalents that pass the enclosing type as the
    first argument when accessed. */
    struct ClassMethod : PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A descriptor that binds a Bertrand function as a class method of a Python
class.

Notes
-----
The `func.classmethod` accessor is actually a property that returns an unbound
instance of this type.  That instance then implements a call operator, which
allows it to be used as a decorator that self-attaches the descriptor to a
Python class.

This architecture allows the unbound descriptor to implement the `&` and `|`
operators, which allow for extremely simple structural types in Python:

```
@bertrand
def func(x: foo | (bar.classmethod & baz.property) | qux.staticmethod) -> int:
    ...
```

This syntax is not available in C++, which requires the use of explicit
`Union<...>` and `Intersection<...>` types instead.

Examples
--------
This descriptor is primarily used via the `@func.classmethod` decorator of a
Bertrand function, which automatically binds the function to the decorated
type.

>>> import bertrand
>>> @bertrand
... def foo(cls, x: int) -> int:
...     return x + 1
...
>>> @foo.classmethod
... class Bar:
...     pass
...
>>> Bar.foo(1)
2

It is also possible to create a classmethod in-place by explicitly calling
`@bertrand.classmethod` within a class definition, just like the normal
Python `@classmethod` decorator.

>>> class Baz:
...     @bertrand.classmethod
...     def foo(cls, x: int) -> int:
...         return x + 1
...
>>> Baz.foo(1)
2

Both syntaxes achieve the same effect, but the first allows the function to
be defined separately from the class, enables UFCS, and allows for easy
structural typing and function overloading.  It is thus the preferred way of
defining class methods in Bertrand.

Additionally, the result of the `bertrand.classmethod` property can be used
in `isinstance()` and `issubclass()` checks in order to enforce the structural
types created by the `&` and `|` operators.

>>> @bertrand
... def foo(cls, x: int) -> int:
...     return x + 1
...
>>> @foo.classmethod
... class Bar:
...     pass
...
>>> isinstance(Bar(), foo.classmethod)  # Bar() implements foo as a classmethod
True
>>> issubclass(Bar, foo.classmethod)  # Bar implements foo as a classmethod
True

This works by checking whether the operand has an attribute `foo`, which is a
callable with the same signature as the free-standing function.  Note that
this does not strictly require the use of `@foo.classmethod`, although that is
by far the easiest way to guarantee that this check always succeeds.
Technically, any type for which `obj.foo(...)` is well-formed will pass the
check, regardless of how that method is exposed, making this a true structural
type check.)doc";

        static PyTypeObject __type__;

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object func;
        Object member_type;

        explicit ClassMethod(const Object& func) : func(func) {}
        explicit ClassMethod(Object&& func) : func(std::move(func)) {}

        static void __dealloc__(ClassMethod* self) noexcept {
            self->~ClassMethod();
        }

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                ClassMethod* self = reinterpret_cast<ClassMethod*>(
                    type->tp_alloc(type, 0)
                );
                if (self == nullptr) {
                    return nullptr;
                }
                try {
                    new (self) ClassMethod(None);
                } catch (...) {
                    Py_DECREF(self);
                    throw;
                }
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            ClassMethod* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                const char* kwlist[] = {nullptr};
                PyObject* func;
                if (PyArg_ParseTupleAndKeywords(
                    args,
                    kwargs,
                    "O:classmethod",
                    const_cast<char**>(kwlist),
                    &func
                )) {
                    return -1;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object wrapped = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(func)
                );
                getattr<"bind_partial">(
                    getattr<"__signature__">(wrapped)
                )(None);
                self->func = wrapped;
                self->member_type = None;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __wrapped__(ClassMethod* self, void*) noexcept {
            return Py_NewRef(ptr(self->func));
        }

        static PyObject* __call__(
            ClassMethod* self,
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if (kwnames) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "classmethod() does not accept keyword arguments"
                    );
                    return nullptr;
                }
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "classmethod() requires exactly one positional argument"
                    );
                    return nullptr;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    ptr(self->func),
                    cls,
                    self
                };
                PyObject* result = PyObject_VectorcallMethod(
                    ptr(template_string<"_bind_classmethod">()),
                    forward,
                    3,
                    nullptr
                );
                if (result == nullptr) {
                    return nullptr;
                }
                try {
                    self->member_type = self->member_function_type(
                        bertrand,
                        reinterpret_borrow<Object>(cls)
                    );
                } catch (...) {
                    Py_DECREF(result);
                    throw;
                }
                return result;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get__(
            ClassMethod* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            PyObject* cls = type == Py_None ?
                reinterpret_cast<PyObject*>(Py_TYPE(obj)) :
                type;
            if (self->member_type.is(None)) {
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                self->member_type = self->member_function_type(
                    bertrand,
                    reinterpret_borrow<Object>(cls)
                );
            }
            PyObject* const args[] = {
                ptr(self->member_type),
                ptr(self->func),
                cls,
            };
            return PyObject_VectorcallMethod(
                ptr(template_string<"_capture">()),
                args,
                3,
                nullptr
            );
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<ClassMethod*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<ClassMethod*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<ClassMethod*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<ClassMethod*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(ClassMethod* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(
                    obj,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(ClassMethod* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(
                    cls,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(ClassMethod* self) noexcept {
            try {
                std::string str = "<classmethod(" + repr(self->func) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        Object member_function_type(const Object& bertrand, const Object& cls) const {
            Object key = getattr<"__template_key__">(func);
            Py_ssize_t len = PyTuple_GET_SIZE(ptr(key));
            Object new_key = reinterpret_steal<Object>(PyTuple_New(len - 1));
            if (new_key.is(nullptr)) {
                Exception::from_python();
            }
            Object rtype = reinterpret_steal<Object>(PySlice_New(
                ptr(reinterpret_borrow<Object>(
                    reinterpret_cast<PyObject*>(&PyType_Type)
                )[cls]),
                Py_None,
                reinterpret_cast<PySliceObject*>(
                    PyTuple_GET_ITEM(ptr(key), 0)
                )->step
            ));
            if (rtype.is(nullptr)) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(new_key), 0, release(rtype));
            for (Py_ssize_t i = 2; i < len; ++i) {
                PyTuple_SET_ITEM(
                    ptr(new_key),
                    i - 1,
                    Py_NewRef(PyTuple_GET_ITEM(ptr(key), i))
                );
            }
            Object specialization = reinterpret_borrow<Object>(
                reinterpret_cast<PyObject*>(Py_TYPE(ptr(func)))
            )[new_key];
            return getattr<"Function">(bertrand)[specialization];
        }

        Object structural_type() const {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object self_type = getattr<"_self_type">(func);
            if (self_type.is(None)) {
                throw TypeError("function must accept at least one positional argument");
            }
            Object specialization = member_function_type(bertrand, self_type);
            Object result = reinterpret_steal<Object>(PySlice_New(
                ptr(getattr<"__name__">(func)),
                ptr(specialization),
                Py_None
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return getattr<"Intersection">(bertrand)[result];
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__wrapped__",
                reinterpret_cast<getter>(&__wrapped__),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    PyTypeObject ClassMethod::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(ClassMethod).name(),
        .tp_basicsize = sizeof(ClassMethod),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&ClassMethod::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&ClassMethod::__repr__),
        .tp_as_number = &ClassMethod::number,
        .tp_call = PyVectorcall_Call,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(ClassMethod::__doc__),
        .tp_methods = ClassMethod::methods,
        .tp_getset = ClassMethod::getset,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&ClassMethod::__get__),
        .tp_init = reinterpret_cast<initproc>(&ClassMethod::__init__),
        .tp_new = reinterpret_cast<newfunc>(&ClassMethod::__new__),
        .tp_vectorcall_offset = offsetof(ClassMethod, __vectorcall__)
    };

    /* A `@staticmethod` descriptor for a C++ function type, which references an
    unbound function and directly forwards it when accessed. */
    struct StaticMethod : PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A descriptor that binds a Bertrand function as a static method of a Python
class.

Notes
-----
The `func.staticmethod` accessor is actually a property that returns an unbound
instance of this type.  That instance then implements a call operator, which
allows it to be used as a decorator that self-attaches the descriptor to a
Python class.

This architecture allows the unbound descriptor to implement the `&` and `|`
operators, which allow for extremely simple structural types in Python:

```
@bertrand
def func(x: foo | (bar.classmethod & baz.property) | qux.staticmethod) -> int:
    ...
```

This syntax is not available in C++, which requires the use of explicit
`Union<...>` and `Intersection<...>` types instead.

Examples
--------
This descriptor is primarily used via the `@func.staticmethod` decorator of a
Bertrand function, which automatically binds the function to the decorated
type.

>>> import bertrand
>>> @bertrand
... def foo(x: int) -> int:
...     return x + 1
...
>>> @foo.staticmethod
... class Bar:
...     pass
...
>>> Bar.foo(1)
2

It is also possible to create a staticmethod in-place by explicitly calling
`@bertrand.staticmethod` within a class definition, just like the normal
Python `@staticmethod` decorator.

>>> class Baz:
...     @bertrand.staticmethod
...     def foo(x: int) -> int:
...         return x + 1
...
>>> Baz.foo(1)
2

Both syntaxes achieve the same effect, but the first allows the function to
be defined separately from the class, enables UFCS, and allows for easy
structural typing and function overloading.  It is thus the preferred way of
defining static methods in Bertrand.

Additionally, the result of the `bertrand.staticmethod` property can be used
in `isinstance()` and `issubclass()` checks in order to enforce the structural
types created by the `&` and `|` operators.

>>> @bertrand
... def foo(x: int) -> int:
...     return x + 1
...
>>> @foo.staticmethod
... class Bar:
...     pass
...
>>> isinstance(Bar(), foo.staticmethod)  # Bar() implements foo as a staticmethod
True
>>> issubclass(Bar, foo.staticmethod)  # Bar implements foo as a staticmethod
True

This works by checking whether the operand has an attribute `foo`, which is a
callable with the same signature as the free-standing function.  Note that
this does not strictly require the use of `@foo.staticmethod`, although that is
by far the easiest way to guarantee that this check always succeeds.
Technically, any type for which `obj.foo(...)` is well-formed will pass the
check, regardless of how that method is exposed, making this a true structural
type check.)doc";

        static PyTypeObject __type__;

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object func;

        explicit StaticMethod(const Object& func) : func(func) {}
        explicit StaticMethod(Object&& func) : func(std::move(func)) {}

        static void __dealloc__(StaticMethod* self) noexcept {
            self->~StaticMethod();
        }

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                StaticMethod* self = reinterpret_cast<StaticMethod*>(
                    type->tp_alloc(type, 0)
                );
                if (self == nullptr) {
                    return nullptr;
                }
                try {
                    new (self) StaticMethod(None);
                } catch (...) {
                    Py_DECREF(self);
                    throw;
                }
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            StaticMethod* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                const char* kwlist[] = {nullptr};
                PyObject* func;
                if (PyArg_ParseTupleAndKeywords(
                    args,
                    kwargs,
                    "O:staticmethod",
                    const_cast<char**>(kwlist),
                    &func
                )) {
                    return -1;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                self->func = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(func)
                );
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __wrapped__(StaticMethod* self, void*) noexcept {
            return Py_NewRef(ptr(self->func));
        }

        static PyObject* __call__(
            StaticMethod* self,
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if (kwnames) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "staticmethod() does not accept keyword arguments"
                    );
                    return nullptr;
                }
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "staticmethod() requires exactly one positional argument"
                    );
                    return nullptr;
                }
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    ptr(self->func),
                    cls,
                    self
                };
                return PyObject_VectorcallMethod(
                    ptr(template_string<"_bind_staticmethod">()),
                    forward,
                    3,
                    nullptr
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get__(
            StaticMethod* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            return Py_NewRef(ptr(self->func));
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<StaticMethod*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<StaticMethod*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<StaticMethod*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<StaticMethod*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(StaticMethod* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(
                    obj,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(StaticMethod* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(
                    cls,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(StaticMethod* self) noexcept {
            try {
                std::string str = "<staticmethod(" + repr(self->func) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        Object structural_type() const {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object result = reinterpret_steal<Object>(PySlice_New(
                ptr(getattr<"__name__">(func)),
                reinterpret_cast<PyObject*>(Py_TYPE(ptr(func))),
                Py_None
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return getattr<"Intersection">(bertrand)[result];
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__wrapped__",
                reinterpret_cast<getter>(&StaticMethod::__wrapped__),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    PyTypeObject StaticMethod::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(StaticMethod).name(),
        .tp_basicsize = sizeof(StaticMethod),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&StaticMethod::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&StaticMethod::__repr__),
        .tp_as_number = &StaticMethod::number,
        .tp_call = PyVectorcall_Call,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(StaticMethod::__doc__),
        .tp_getset = StaticMethod::getset,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&StaticMethod::__get__),
        .tp_init = reinterpret_cast<initproc>(&StaticMethod::__init__),
        .tp_new = reinterpret_cast<newfunc>(&StaticMethod::__new__),
        .tp_vectorcall_offset = offsetof(StaticMethod, __vectorcall__)
    };

    /* A `@property` descriptor for a C++ function type that accepts a single
    compatible argument, which will be used as the getter for the property.  Setters
    and deleters can also be registered with the same `self` parameter.  The setter can
    accept any type for the assigned value, allowing overloads. */
    struct Property : PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A descriptor that binds a Bertrand function as a property getter of a
Python class.

Notes
-----
The `func.property` accessor is actually a property that returns an unbound
instance of this type.  That instance then implements a call operator, which
allows it to be used as a decorator that self-attaches the descriptor to a
Python class.

This architecture allows the unbound descriptor to implement the `&` and `|`
operators, which allow for extremely simple structural types in Python:

```
@bertrand
def func(x: foo | (bar.classmethod & baz.property) | qux.staticmethod) -> int:
    ...
```

This syntax is not available in C++, which requires the use of explicit
`Union<...>` and `Intersection<...>` types instead.

Examples
--------
This descriptor is primarily used via the `@func.property` decorator of a
Bertrand function, which automatically binds the function to the decorated
type.

>>> import bertrand
>>> @bertrand
... def foo(self) -> int:
...     return 2
...
>>> @foo.property
... class Bar:
...     pass
...
>>> Bar().foo
2

It is also possible to create a property in-place by explicitly calling
`@bertrand.property` within a class definition, just like the normal Python
`@property` decorator.

>>> class Baz:
...     @bertrand.property
...     def foo(self) -> int:
...         return 2
...
>>> Baz().foo
2

Both syntaxes achieve the same effect, but the first allows the function to
be defined separately from the class, enables UFCS, and allows for easy
structural typing and function overloading.  It is thus the preferred way of
defining properties in Bertrand.

Additionally, the result of the `bertrand.property` property can be used in
`isinstance()` and `issubclass()` checks in order to enforce the structural
types created by the `&` and `|` operators.

>>> @bertrand
... def foo(self) -> int:
...     return 2
...
>>> @foo.property
... class Bar:
...     pass
...
>>> isinstance(Bar(), foo.property)  # Bar() has an attribute 'foo' with the same return type 
True
>>> issubclass(Bar, foo.property)  # Bar has an attribute 'foo' with the same return type
True

Unlike the `classmethod` and `staticmethod` descriptors, the `property`
descriptor does not require that the resulting attribute is callable, just that
it has the same type as the return type of the free-standing function.  It
effectively devolves into a structural check against a simple type, in this
case equivalent to:

>>> isinstance(Bar(), bertrand.Intersection["foo": int])
True
>>> issubclass(Bar, bertrand.Intersection["foo": int])
True

Technically, any type for which `obj.foo` is well-formed and returns an integer
will pass the check, regardless of how it is exposed, making this a true
structural type check.)doc";

        static PyTypeObject __type__;

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object fget;
        Object fset;
        Object fdel;
        Object doc;

        explicit Property(
            const Object& fget,
            const Object& fset = None,
            const Object& fdel = None,
            const Object& doc = None
        ) : fget(fget), fset(fset), fdel(fdel), doc(doc)
        {}

        static void __dealloc__(Property* self) noexcept {
            self->~Property();
        }

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                Property* self = reinterpret_cast<Property*>(
                    type->tp_alloc(type, 0)
                );
                if (self == nullptr) {
                    return nullptr;
                }
                try {
                    new (self) Property(None);
                } catch (...) {
                    Py_DECREF(self);
                    throw;
                }
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            Property* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object Function = getattr<"Function">(bertrand);
                PyObject* fget = nullptr;
                PyObject* fset = nullptr;
                PyObject* fdel = nullptr;
                PyObject* doc = nullptr;
                const char* const kwnames[] {
                    "fget",
                    "fset",
                    "fdel",
                    "doc",
                    nullptr
                };
                PyArg_ParseTupleAndKeywords(
                    args,
                    kwargs,
                    "O|OOU:property",
                    const_cast<char**>(kwnames),  // necessary for Python API
                    &fget,
                    &fset,
                    &fdel,
                    &doc
                );
                Object getter = Function(reinterpret_borrow<Object>(fget));
                Object self_type = getattr<"_self_type">(getter);
                if (self_type.is(None)) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "getter must accept exactly one positional argument"
                    );
                    return -1;
                }
                Object setter = reinterpret_borrow<Object>(fset);
                if (fset) {
                    setter = Function(setter);
                    getattr<"bind">(getattr<"__signature__">(setter))(None, None);
                    int rc = PyObject_IsSubclass(
                        ptr(self_type),
                        ptr(getattr<"_self_type">(setter))
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() setter must accept the same type as "
                            "the getter"
                        );
                        return -1;
                    }
                }
                Object deleter = reinterpret_borrow<Object>(fdel);
                if (fdel) {
                    deleter = Function(deleter);
                    getattr<"bind">(getattr<"__signature__">(getter))(None);
                    int rc = PyObject_IsSubclass(
                        ptr(self_type),
                        ptr(getattr<"_self_type">(deleter))
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() deleter must accept the same type as "
                            "the getter"
                        );
                        return -1;
                    }
                }
                self->fget = getter;
                self->fset = setter;
                self->fdel = deleter;
                self->doc = reinterpret_borrow<Object>(doc);
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __wrapped__(Property* self, void*) noexcept {
            return Py_NewRef(ptr(self->fget));
        }

        static PyObject* get_fget(Property* self, void*) noexcept {
            return Py_NewRef(ptr(self->fget));
        }

        static int set_fget(Property* self, PyObject* value, void*) noexcept {
            try {
                if (!value) {
                    self->fget = None;
                    return 0;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object func = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(value)
                );
                Object self_type = getattr<"_self_type">(func);
                if (self_type.is(None)) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "getter must accept exactly one positional argument"
                    );
                    return -1;
                }
                if (!self->fset.is(None)) {
                    int rc = PyObject_IsSubclass(
                        ptr(self_type),
                        ptr(getattr<"_self_type">(self->fset))
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() getter must accept the same type as "
                            "the setter"
                        );
                        return -1;
                    }
                }
                if (!self->fdel.is(None)) {
                    int rc = PyObject_IsSubclass(
                        ptr(self_type),
                        ptr(getattr<"_self_type">(self->fdel))
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() getter must accept the same type as "
                            "the deleter"
                        );
                        return -1;
                    }
                }
                self->fget = func;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* get_fset(Property* self, void*) noexcept {
            return Py_NewRef(ptr(self->fset));
        }

        static int set_fset(Property* self, PyObject* value, void*) noexcept {
            try {
                if (!value) {
                    self->fset = None;
                    return 0;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object func = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(value)
                );
                Object self_type = getattr<"_self_type">(func);
                if (self_type.is(None)) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "setter must accept exactly one positional argument"
                    );
                    return -1;
                }
                if (!self->fget.is(None)) {
                    int rc = PyObject_IsSubclass(
                        ptr(getattr<"_self_type">(self->fget)),
                        ptr(self_type)
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() setter must accept the same type as "
                            "the getter"
                        );
                        return -1;
                    }
                }
                self->fset = func;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* get_fdel(Property* self, void*) noexcept {
            return Py_NewRef(ptr(self->fdel));
        }

        static int set_fdel(Property* self, PyObject* value, void*) noexcept {
            try {
                if (!value) {
                    self->fdel = None;
                    return 0;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object func = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(value)
                );
                Object self_type = getattr<"_self_type">(func);
                if (self_type.is(None)) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "deleter must accept exactly one positional argument"
                    );
                    return -1;
                }
                if (!self->fget.is(None)) {
                    int rc = PyObject_IsSubclass(
                        ptr(getattr<"_self_type">(self->fget)),
                        ptr(self_type)
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() deleter must accept the same type as "
                            "the getter"
                        );
                        return -1;
                    }
                }
                self->fdel = func;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* getter(Property* self, PyObject* func) noexcept {
            if (set_fget(self, func, nullptr)) {
                return nullptr;
            }
            return Py_NewRef(ptr(self->fget));
        }

        static PyObject* setter(Property* self, PyObject* func) noexcept {
            if (set_fset(self, func, nullptr)) {
                return nullptr;
            }
            return Py_NewRef(ptr(self->fset));
        }

        static PyObject* deleter(Property* self, PyObject* func) noexcept {
            if (set_fdel(self, func, nullptr)) {
                return nullptr;
            }
            return Py_NewRef(ptr(self->fdel));
        }

        /// TODO: Property::__call__() should also accept optional setter/deleter/
        /// docstring as keyword-only arguments, so that you can use
        /// `@func.property(setter=fset, deleter=fdel, doc="docstring")`.

        /// TODO: in fact, each of the previous descriptors' call operators may want
        /// to accept an optional docstring.

        static PyObject* __call__(
            Property* self,
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if (kwnames) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "property() does not accept keyword arguments"
                    );
                    return nullptr;
                }
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "property() requires exactly one positional argument"
                    );
                    return nullptr;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                /// TODO: _bind_property() may need to check the self argument of
                /// multiple functions simultaneously.
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    ptr(self->fget),
                    cls,
                    self
                };
                return PyObject_VectorcallMethod(
                    ptr(template_string<"_bind_property">()),
                    forward,
                    3,
                    nullptr
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get__(
            Property* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            return PyObject_CallOneArg(ptr(self->fget), obj);
        }

        static PyObject* __set__(
            Property* self,
            PyObject* obj,
            PyObject* value
        ) noexcept {
            try {
                if (value) {
                    if (self->fset.is(None)) {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "property '%U' of '%R' object has no setter",
                            ptr(getattr<"__name__">(self->fget)),
                            reinterpret_cast<PyObject*>(Py_TYPE(obj))
                        );
                        return nullptr;
                    }
                    PyObject* const args[] = {obj, value};
                    return PyObject_Vectorcall(
                        ptr(self->fset),
                        args,
                        2,
                        nullptr
                    );
                }

                if (self->fdel.is(None)) {
                    PyErr_Format(
                        PyExc_AttributeError,
                        "property '%U' of '%R' object has no deleter",
                        ptr(getattr<"__name__">(self->fget)),
                        reinterpret_cast<PyObject*>(Py_TYPE(obj))
                    );
                    return nullptr;
                }
                return PyObject_CallOneArg(ptr(self->fdel), obj);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<Property*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<Property*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<Property*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<Property*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(Property* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(
                    obj,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(Property* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(
                    cls,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(Property* self) noexcept {
            try {
                std::string str = "<property(" + repr(self->fget) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get_doc__(Property* self, void*) noexcept {
            if (!self->doc.is(None)) {
                return Py_NewRef(ptr(self->doc));
            }
            return release(getattr<"__doc__">(self->fget));
        }

    private:

        Object structural_type() const {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object rtype = getattr<"_return_type">(fget);
            if (rtype.is(None)) {
                throw TypeError("getter must not return void");
            }
            Object result = reinterpret_steal<Object>(PySlice_New(
                ptr(getattr<"__name__">(fget)),
                ptr(rtype),
                Py_None
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return getattr<"Intersection">(bertrand)[result];
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

        /// TODO: document these?

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {
                "getter",
                reinterpret_cast<PyCFunction>(&getter),
                METH_O,
                nullptr
            },
            {
                "setter",
                reinterpret_cast<PyCFunction>(&setter),
                METH_O,
                nullptr
            },
            {
                "deleter",
                reinterpret_cast<PyCFunction>(&deleter),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__wrapped__",
                reinterpret_cast<::getter>(&__wrapped__),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "fget",
                reinterpret_cast<::getter>(&get_fget),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "fset",
                reinterpret_cast<::getter>(&get_fset),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "fdel",
                reinterpret_cast<::getter>(&get_fdel),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "__doc__",
                reinterpret_cast<::getter>(&__get_doc__),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    PyTypeObject Property::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(Property).name(),
        .tp_basicsize = sizeof(Property),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&Property::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&Property::__repr__),
        .tp_as_number = &Property::number,
        .tp_call = PyVectorcall_Call,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(Property::__doc__),
        .tp_methods = Property::methods,
        .tp_getset = Property::getset,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&Property::__get__),
        .tp_descr_set = reinterpret_cast<descrsetfunc>(&Property::__set__),
        .tp_init = reinterpret_cast<initproc>(&Property::__init__),
        .tp_new = reinterpret_cast<newfunc>(&Property::__new__),
        .tp_vectorcall_offset = offsetof(Property, __vectorcall__),
    };

    // /* The Python `bertrand.Function[]` template interface type, which holds all
    // instantiations, each of which inherit from this class, and allows for CTAD-like
    // construction via the `__new__()` operator.  Has no interface otherwise, requiring
    // the user to manually instantiate it as if it were a C++ template. */
    // struct FunctionTemplates : PyObject {

    //     /// TODO: this HAS to be a heap type because it is an instance of the metaclass,
    //     /// and therefore always has mutable state.
    //     /// -> Maybe when writing bindings, this is just given as a function to the
    //     /// binding generator, and it would be responsible for implementing the
    //     /// template interface's CTAD constructor, and it would use Python-style
    //     /// argument annotations just like any other function.

    //     /// TODO: Okay, the way to do this is to have the bindings automatically
    //     /// populate tp_new with an overloadable function, and then the user can
    //     /// register overloads directly from Python.  The function you supply to the
    //     /// binding helper would be inserted as the base case, which defaults to
    //     /// raising a TypeError if the user tries to instantiate the template.  If
    //     /// that is the case, then I might be able to automatically register overloads
    //     /// as each type is instantiated, in a way that doesn't cause errors if the
    //     /// overload conflicts with an existing one.
    //     /// -> If I implement that using argument annotations, then this gets
    //     /// substantially simpler as well, since I don't need to extract the arguments
    //     /// manually.

    //     /// TODO: remember to set tp_vectorcall to this method, so I don't need to
    //     /// implement real __new__/__init__ constructors.
    //     static PyObject* __new__(
    //         FunctionTemplates* self,
    //         PyObject* const* args,
    //         size_t nargsf,
    //         PyObject* kwnames
    //     ) {
    //         try {
    //             size_t nargs = PyVectorcall_NARGS(nargsf);
    //             size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
    //             if (nargs != 1) {
    //                 throw TypeError(
    //                     "expected a single, positional-only argument, but "
    //                     "received " + std::to_string(nargs)
    //                 );
    //             }
    //             PyObject* func = args[0];
    //             Object name = reinterpret_steal<Object>(nullptr);
    //             Object doc = reinterpret_steal<Object>(nullptr);
    //             if (kwcount) {
    //                 for (size_t i = 0; i < kwcount; ++i) {
    //                     PyObject* key = PyTuple_GET_ITEM(kwnames, i);
    //                     int is_name = PyObject_RichCompareBool(
    //                         key,
    //                         ptr(template_string<"name">()),
    //                         Py_EQ
    //                     );
    //                     if (is_name < 0) {
    //                         Exception::from_python();
    //                     } else if (is_name) {
    //                         name = reinterpret_borrow<Object>(args[nargs + i]);
    //                         if (!PyUnicode_Check(ptr(name))) {
    //                             throw TypeError(
    //                                 "expected 'name' to be a string, but received " +
    //                                 repr(name)
    //                             );
    //                         }
    //                     }
    //                     int is_doc = PyObject_RichCompareBool(
    //                         key,
    //                         ptr(template_string<"doc">()),
    //                         Py_EQ
    //                     );
    //                     if (is_doc < 0) {
    //                         Exception::from_python();
    //                     } else if (is_doc) {
    //                         doc = reinterpret_borrow<Object>(args[nargs + i]);
    //                         if (!PyUnicode_Check(ptr(doc))) {
    //                             throw TypeError(
    //                                 "expected 'doc' to be a string, but received " +
    //                                 repr(doc)
    //                             );
    //                         }
    //                     }
    //                     if (!is_name && !is_doc) {
    //                         throw TypeError(
    //                             "unexpected keyword argument '" +
    //                             repr(reinterpret_borrow<Object>(key)) + "'"
    //                         );
    //                     }
    //                 }
    //             }

    //             // inspect the input function and subscript the template interface to
    //             // get the correct specialization
    //             impl::Inspect signature = {
    //                 func,
    //                 impl::fnv1a_seed,
    //                 impl::fnv1a_prime
    //             };
    //             Object specialization = reinterpret_steal<Object>(
    //                 PyObject_GetItem(
    //                     self,
    //                     ptr(signature.template_key())
    //                 )
    //             );
    //             if (specialization.is(nullptr)) {
    //                 Exception::from_python();
    //             }

    //             // if the parameter list contains unions, then we need to default-
    //             // initialize the specialization and then register separate overloads
    //             // for each path through the parameter list.  Note that if the function
    //             // is the only argument and already exactly matches the deduced type,
    //             // then we can just return it directly to avoid unnecessary nesting.
    //             Object result = reinterpret_steal<Object>(nullptr);
    //             if (signature.size() > 1) {
    //                 if (!kwcount) {
    //                     if (specialization.is(
    //                         reinterpret_cast<PyObject*>(Py_TYPE(func))
    //                     )) {
    //                         return release(specialization);
    //                     }
    //                     result = reinterpret_steal<Object>(PyObject_CallNoArgs(
    //                         ptr(specialization)
    //                     ));
    //                 } else if (name.is(nullptr)) {
    //                     PyObject* args[] = {
    //                         nullptr,
    //                         ptr(doc),
    //                     };
    //                     result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                         ptr(specialization),
    //                         args,
    //                         kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                         kwnames
    //                     ));
    //                 } else if (doc.is(nullptr)) {
    //                     PyObject* args[] = {
    //                         nullptr,
    //                         ptr(name),
    //                     };
    //                     result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                         ptr(specialization),
    //                         args,
    //                         kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                         kwnames
    //                     ));
    //                 } else {
    //                     PyObject* args[] = {
    //                         nullptr,
    //                         ptr(name),
    //                         ptr(doc),
    //                     };
    //                     result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                         ptr(specialization),
    //                         args,
    //                         kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                         kwnames
    //                     ));
    //                 }
    //                 if (result.is(nullptr)) {
    //                     Exception::from_python();
    //                 }
    //                 Object rc = reinterpret_steal<Object>(PyObject_CallMethodOneArg(
    //                     ptr(result),
    //                     ptr(impl::template_string<"overload">()),
    //                     func
    //                 ));
    //                 if (rc.is(nullptr)) {
    //                     Exception::from_python();
    //                 }
    //                 return release(result);
    //             }

    //             // otherwise, we can initialize the specialization directly, which
    //             // captures the function and uses it as the base case
    //             if (!kwcount) {
    //                 if (specialization.is(
    //                     reinterpret_cast<PyObject*>(Py_TYPE(func))
    //                 )) {
    //                     return release(specialization);
    //                 }
    //                 result = reinterpret_steal<Object>(PyObject_CallOneArg(
    //                     ptr(specialization),
    //                     func
    //                 ));
    //             } else if (name.is(nullptr)) {
    //                 PyObject* args[] = {
    //                     nullptr,
    //                     func,
    //                     ptr(doc),
    //                 };
    //                 result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                     ptr(specialization),
    //                     args,
    //                     kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                     kwnames
    //                 ));
    //             } else if (doc.is(nullptr)) {
    //                 PyObject* args[] = {
    //                     nullptr,
    //                     func,
    //                     ptr(name),
    //                 };
    //                 result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                     ptr(specialization),
    //                     args,
    //                     kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                     kwnames
    //                 ));
    //             } else {
    //                 PyObject* args[] = {
    //                     nullptr,
    //                     func,
    //                     ptr(name),
    //                     ptr(doc),
    //                 };
    //                 result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                     ptr(specialization),
    //                     args,
    //                     kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                     kwnames
    //                 ));
    //             }
    //             if (result.is(nullptr)) {
    //                 Exception::from_python();
    //             }
    //             return release(result);

    //         } catch (...) {
    //             Exception::to_python();
    //             return nullptr;
    //         }
    //     }

    // };

}  // namespace impl


template <typename F>
struct Interface<Function<F>> : impl::FunctionTag {

    /* The normalized function pointer type for this specialization. */
    using Signature = impl::Signature<F>::type;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = impl::Signature<F>::Self;

    /* A tuple holding the function's default values, which are inferred from the input
    signature. */
    using Defaults = impl::Signature<F>::Defaults;

    /* A trie-based data structure describing dynamic overloads for a function
    object. */
    using Overloads = impl::Signature<F>::Overloads;

    /* The function's return type. */
    using Return = impl::Signature<F>::Return;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R> requires (std::convertible_to<R, Object>)
    using with_return =
        Function<typename impl::Signature<F>::template with_return<R>::type>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
        requires (
            std::convertible_to<C, Object> &&
            impl::Signature<F>::template can_make_member<C>
        )
    using with_self =
        Function<typename impl::Signature<F>::template with_self<C>::type>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
        requires (
            sizeof...(A) <= (64 - impl::Signature<F>::has_self) &&
            impl::Arguments<A...>::args_are_convertible_to_python &&
            impl::Arguments<A...>::proper_argument_order &&
            impl::Arguments<A...>::no_duplicate_args &&
            impl::Arguments<A...>::no_qualified_arg_annotations
        )
    using with_args =
        Function<typename impl::Signature<F>::template with_args<A...>::type>;

    /* Check whether a target function can be registered as a valid overload of this
    function type.  Such a function must minimally account for all the arguments in
    this function signature (which may be bound to subclasses), and list a return
    type that can be converted to this function's return type.  If the function accepts
    variadic positional or keyword arguments, then overloads may include any number of
    additional parameters in their stead, as long as all of those parameters are
    convertible to the variadic type. */
    template <typename Func>
    static constexpr bool compatible = false;

    template <typename Func>
        requires (impl::Signature<std::remove_cvref_t<Func>>::enable)
    static constexpr bool compatible<Func> =
        []<size_t... Is>(std::index_sequence<Is...>) {
            return impl::Signature<F>::template compatible<
                typename impl::Signature<std::remove_cvref_t<Func>>::Return,
                typename impl::Signature<std::remove_cvref_t<Func>>::template at<Is>...
            >;
        }(std::make_index_sequence<impl::Signature<std::remove_cvref_t<Func>>::n>{});

    template <typename Func>
        requires (
            !impl::Signature<std::remove_cvref_t<Func>>::enable &&
            impl::inherits<Func, impl::FunctionTag>
        )
    static constexpr bool compatible<Func> = compatible<
        typename std::remove_reference_t<Func>::Signature
    >;

    template <typename Func>
        requires (
            !impl::Signature<Func>::enable &&
            !impl::inherits<Func, impl::FunctionTag> &&
            impl::has_call_operator<Func>
        )
    static constexpr bool compatible<Func> = 
        impl::Signature<decltype(&std::remove_reference_t<Func>::operator())>::enable &&
        compatible<
            typename impl::Signature<decltype(&std::remove_reference_t<Func>::operator())>::
            template with_self<void>::type
        >;

    /* Check whether this function type can be used to invoke an external C++ function.
    This is identical to a `std::is_invocable_r_v<Func, ...>` check against this
    function's return and argument types.  Note that member functions expect a `self`
    parameter to be listed first, following Python style. */
    template <typename Func>
    static constexpr bool invocable = impl::Signature<F>::template invocable<Func>;

    /* Check whether the function can be called with the given arguments, after
    accounting for optional/variadic/keyword arguments, etc. */
    template <typename... Args>
    static constexpr bool bind = impl::Signature<F>::template Bind<Args...>::enable;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = impl::Signature<F>::n;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = impl::Signature<F>::n_posonly;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = impl::Signature<F>::n_pos;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = impl::Signature<F>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = impl::Signature<F>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = impl::Signature<F>::n_opt;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = impl::Signature<F>::n_opt_posonly;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = impl::Signature<F>::n_opt_pos;

    /* The total number of optional keyword arguments that the function accepts,
    counting both keyword-only and positional-or-keyword arguments, but not
    positional-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_kw = impl::Signature<F>::n_opt_kw;

    /* The total number of optional keyword-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_kwonly = impl::Signature<F>::n_opt_kwonly;

    /* Check if the named argument is present in the function signature. */
    template <StaticStr Name>
    static constexpr bool has = impl::Signature<F>::template has<Name>;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = impl::Signature<F>::has_posonly;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = impl::Signature<F>::has_pos;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = impl::Signature<F>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = impl::Signature<F>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = impl::Signature<F>::has_opt;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = impl::Signature<F>::has_opt_posonly;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = impl::Signature<F>::has_opt_pos;

    /* Check if the function accepts at least one optional keyword argument.  This will
    match either positional-or-keyword or keyword-only arguments. */
    static constexpr bool has_opt_kw = impl::Signature<F>::has_opt_kw;

    /* Check if the function accepts at least one optional keyword-only argument. */
    static constexpr bool has_opt_kwonly = impl::Signature<F>::has_opt_kwonly;

    /* Check if the function has a `self` parameter, indicating that it can be called
    as a member function. */
    static constexpr bool has_self = impl::Signature<F>::has_self;

    /* Check if the function accepts variadic positional arguments. */
    static constexpr bool has_args = impl::Signature<F>::has_args;

    /* Check if the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = impl::Signature<F>::has_kwargs;

    /* Find the index of the named argument, if it is present. */
    template <StaticStr Name> requires (has<Name>)
    static constexpr size_t idx = impl::Signature<F>::template idx<Name>;

    /* Find the index of the first keyword argument that appears in the function
    signature.  This will match either a positional-or-keyword argument or a
    keyword-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t kw_idx = impl::Signature<F>::kw_index;

    /* Find the index of the first keyword-only argument that appears in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwonly_idx = impl::Signature<F>::kw_only_index;

    /* Find the index of the first optional argument in the function signature.  If no
    such argument is present, this will return `n`. */
    static constexpr size_t opt_idx = impl::Signature<F>::opt_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = impl::Signature<F>::opt_posonly_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = impl::Signature<F>::opt_pos_index;

    /* Find the index of the first optional keyword argument in the function signature.
    This will match either a positional-or-keyword argument or a keyword-only argument.
    If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kw_idx = impl::Signature<F>::opt_kw_index;

    /* Find the index of the first optional keyword-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kwonly_idx = impl::Signature<F>::opt_kwonly_index;

    /* Find the index of the variadic positional arguments in the function signature,
    if they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t args_idx = impl::Signature<F>::args_index;

    /* Find the index of the variadic keyword arguments in the function signature, if
    they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwargs_idx = impl::Signature<F>::kwargs_index;

    /* Get the (annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = impl::Signature<F>::template at<I>;

    /* A bitmask of all the required arguments needed to call this function.  This is
    used during argument validation to quickly determine if the parameter list is
    satisfied when keyword are provided out of order, etc. */
    static constexpr uint64_t required = impl::Signature<F>::required;

    /* An FNV-1a seed that was found to perfectly hash the function's keyword argument
    names. */
    static constexpr size_t seed = impl::Signature<F>::seed;

    /* The FNV-1a prime number that was found to perfectly hash the function's keyword
    argument names. */
    static constexpr size_t prime = impl::Signature<F>::prime;

    /* Hash a string according to the seed and prime that were found at compile time to
    perfectly hash this function's keyword arguments. */
    [[nodiscard]] static constexpr size_t hash(const char* str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(std::string_view str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(const std::string& str) noexcept {
        return impl::Signature<F>::hash(str);
    }

    /* Register an overload for this function from C++. */
    template <typename Self, typename Func>
        requires (
            !std::is_const_v<std::remove_reference_t<Self>> &&
            compatible<Func>
        )
    void overload(this Self&& self, const Function<Func>& func);

    /// TODO: key() should return the function's overload key as a tuple of slices,
    /// for inspection from Python.

    /* Attach the function as a bound method of a Python type. */
    template <typename T>
    void method(this const auto& self, Type<T>& type);

    template <typename T>
    void classmethod(this const auto& self, Type<T>& type);

    template <typename T>
    void staticmethod(this const auto& self, Type<T>& type);

    template <typename T>
    void property(
        this const auto& self,
        Type<T>& type,
        /* setter */,
        /* deleter */
    );

    /// TODO: when getting and setting these properties, do I need to use Attr
    /// proxies for consistency?

    __declspec(property(get=_get_name, put=_set_name)) std::string __name__;
    [[nodiscard]] std::string _get_name(this const auto& self);
    void _set_name(this auto& self, const std::string& name);

    __declspec(property(get=_get_doc, put=_set_doc)) std::string __doc__;
    [[nodiscard]] std::string _get_doc(this const auto& self);
    void _set_doc(this auto& self, const std::string& doc);

    /// TODO: __defaults__ should return a std::tuple of default values, as they are
    /// given in the signature.

    __declspec(property(get=_get_defaults, put=_set_defaults))
        std::optional<Tuple<Object>> __defaults__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_defaults(this const auto& self);
    void _set_defaults(this auto& self, const Tuple<Object>& defaults);

    /// TODO: This should return a std::tuple of Python type annotations for each
    /// argument.

    __declspec(property(get=_get_annotations, put=_set_annotations))
        std::optional<Dict<Str, Object>> __annotations__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_annotations(this const auto& self);
    void _set_annotations(this auto& self, const Dict<Str, Object>& annotations);

    /// TODO: __signature__, which returns a proper Python `inspect.Signature` object.

};


template <typename F>
struct Interface<Type<Function<F>>> {

    /* The normalized function pointer type for this specialization. */
    using Signature = Interface<Function<F>>::Signature;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = Interface<Function<F>>::Self;

    /* A tuple holding the function's default values, which are inferred from the input
    signature and stored as a `std::tuple`. */
    using Defaults = Interface<Function<F>>::Defaults;

    /* A trie-based data structure describing dynamic overloads for a function
    object. */
    using Overloads = Interface<Function<F>>::Overloads;

    /* The function's return type. */
    using Return = Interface<Function<F>>::Return;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R> requires (std::convertible_to<R, Object>)
    using with_return = Interface<Function<F>>::template with_return<R>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
        requires (
            std::convertible_to<C, Object> &&
            impl::Signature<F>::template can_make_member<C>
        )
    using with_self = Interface<Function<F>>::template with_self<C>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
        requires (
            sizeof...(A) <= (64 - impl::Signature<F>::has_self) &&
            impl::Arguments<A...>::args_are_convertible_to_python &&
            impl::Arguments<A...>::proper_argument_order &&
            impl::Arguments<A...>::no_duplicate_args &&
            impl::Arguments<A...>::no_qualified_arg_annotations
        )
    using with_args = Interface<Function<F>>::template with_args<A...>;

    /* Check whether a target function can be registered as a valid overload of this
    function type.  Such a function must minimally account for all the arguments in
    this function signature (which may be bound to subclasses), and list a return
    type that can be converted to this function's return type.  If the function accepts
    variadic positional or keyword arguments, then overloads may include any number of
    additional parameters in their stead, as long as all of those parameters are
    convertible to the variadic type. */
    template <typename Func>
    static constexpr bool compatible = Interface<Function<F>>::template compatible<Func>;

    /* Check whether this function type can be used to invoke an external C++ function.
    This is identical to a `std::is_invocable_r_v<Func, ...>` check against this
    function's return and argument types.  Note that member functions expect a `self`
    parameter to be listed first, following Python style. */
    template <typename Func>
    static constexpr bool invocable = Interface<Function<F>>::template invocable<Func>;

    /* Check whether the function can be called with the given arguments, after
    accounting for optional/variadic/keyword arguments, etc. */
    template <typename... Args>
    static constexpr bool bind = Interface<Function<F>>::template bind<Args...>;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = Interface<Function<F>>::n;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = Interface<Function<F>>::n_posonly;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = Interface<Function<F>>::n_pos;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = Interface<Function<F>>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = Interface<Function<F>>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = Interface<Function<F>>::n_opt;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = Interface<Function<F>>::n_opt_posonly;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = Interface<Function<F>>::n_opt_pos;

    /* The total number of optional keyword arguments that the function accepts,
    counting both keyword-only and positional-or-keyword arguments, but not
    positional-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_kw = Interface<Function<F>>::n_opt_kw;

    /* The total number of optional keyword-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_kwonly = Interface<Function<F>>::n_opt_kwonly;

    /* Check if the named argument is present in the function signature. */
    template <StaticStr Name>
    static constexpr bool has = Interface<Function<F>>::template has<Name>;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = Interface<Function<F>>::has_posonly;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = Interface<Function<F>>::has_pos;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = Interface<Function<F>>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = Interface<Function<F>>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = Interface<Function<F>>::has_opt;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = Interface<Function<F>>::has_opt_posonly;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = Interface<Function<F>>::has_opt_pos;

    /* Check if the function accepts at least one optional keyword argument.  This will
    match either positional-or-keyword or keyword-only arguments. */
    static constexpr bool has_opt_kw = Interface<Function<F>>::has_opt_kw;

    /* Check if the function accepts at least one optional keyword-only argument. */
    static constexpr bool has_opt_kwonly = Interface<Function<F>>::has_opt_kwonly;

    /* Check if the function has a `self` parameter, indicating that it can be called
    as a member function. */
    static constexpr bool has_self = Interface<Function<F>>::has_self;

    /* Check if the function accepts variadic positional arguments. */
    static constexpr bool has_args = Interface<Function<F>>::has_args;

    /* Check if the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = Interface<Function<F>>::has_kwargs;

    /* Find the index of the named argument, if it is present. */
    template <StaticStr Name> requires (has<Name>)
    static constexpr size_t idx = Interface<Function<F>>::template idx<Name>;

    /* Find the index of the first keyword argument that appears in the function
    signature.  This will match either a positional-or-keyword argument or a
    keyword-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t kw_idx = Interface<Function<F>>::kw_index;

    /* Find the index of the first keyword-only argument that appears in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwonly_idx = Interface<Function<F>>::kw_only_index;

    /* Find the index of the first optional argument in the function signature.  If no
    such argument is present, this will return `n`. */
    static constexpr size_t opt_idx = Interface<Function<F>>::opt_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = Interface<Function<F>>::opt_posonly_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = Interface<Function<F>>::opt_pos_index;

    /* Find the index of the first optional keyword argument in the function signature.
    This will match either a positional-or-keyword argument or a keyword-only argument.
    If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kw_idx = Interface<Function<F>>::opt_kw_index;

    /* Find the index of the first optional keyword-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kwonly_idx = Interface<Function<F>>::opt_kwonly_index;

    /* Find the index of the variadic positional arguments in the function signature,
    if they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t args_idx = Interface<Function<F>>::args_index;

    /* Find the index of the variadic keyword arguments in the function signature, if
    they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwargs_idx = Interface<Function<F>>::kwargs_index;

    /* Get the (possibly annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = Interface<Function<F>>::template at<I>;

    /* A bitmask of all the required arguments needed to call this function.  This is
    used during argument validation to quickly determine if the parameter list is
    satisfied when keyword are provided out of order, etc. */
    static constexpr uint64_t required = Interface<Function<F>>::required;

    /* An FNV-1a seed that was found to perfectly hash the function's keyword argument
    names. */
    static constexpr size_t seed = Interface<Function<F>>::seed;

    /* The FNV-1a prime number that was found to perfectly hash the function's keyword
    argument names. */
    static constexpr size_t prime = Interface<Function<F>>::prime;

    /* Hash a string according to the seed and prime that were found at compile time to
    perfectly hash this function's keyword arguments. */
    [[nodiscard]] static constexpr size_t hash(const char* str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(std::string_view str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(const std::string& str) noexcept {
        return impl::Signature<F>::hash(str);
    }

    /* Register an overload for this function. */
    template <impl::inherits<Interface<Function<F>>> Self, typename Func>
        requires (!std::is_const_v<std::remove_reference_t<Self>> && compatible<Func>)
    void overload(Self&& self, const Function<Func>& func) {
        std::forward<Self>(self).overload(func);
    }

    /* Attach the function as a bound method of a Python type. */
    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void method(const Self& self, Type<T>& type) {
        std::forward<Self>(self).method(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void classmethod(const Self& self, Type<T>& type) {
        std::forward<Self>(self).classmethod(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void staticmethod(const Self& self, Type<T>& type) {
        std::forward<Self>(self).staticmethod(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void property(const Self& self, Type<T>& type, /* setter */, /* deleter */) {
        std::forward<Self>(self).property(type);
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __name__(const Self& self) {
        return self.__name__;
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __doc__(const Self& self) {
        return self.__doc__;
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __defaults__(const Self& self);

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __annotations__(const Self& self);

};


/* A universal function wrapper that can represent either a Python function exposed to
C++, or a C++ function exposed to Python with equivalent semantics.  Supports keyword,
optional, and variadic arguments through the `py::Arg` annotation.

Notes
-----
When constructed with a C++ function, this class will create a Python object that
encapsulates the function and allows it to be called from Python.  The Python wrapper
has a unique type for each template signature, which allows Bertrand to enforce strong
type safety and provide accurate error messages if a signature mismatch is detected.
It also allows Bertrand to directly unpack the underlying function from the Python
object, bypassing the Python interpreter and demoting the call to pure C++ where
possible.  If the function accepts `py::Arg` annotations in its signature, then these
will be extracted using template metaprogramming and observed when the function is
called in either language.

When constructed with a Python function, this class will store the function directly
and allow it to be called from C++ with the same semantics as the Python interpreter.
The `inspect` module is used to extract parameter names, categories, and default
values, as well as type annotations if they are present, all of which will be checked
against the expected signature and result in errors if they do not match.  `py::Arg`
annotations can be used to provide keyword, optional, and variadic arguments according
to the templated signature, and the function will be called directly using the
vectorcall protocol, which is the most efficient way to call a Python function from
C++.  

Container unpacking via the `*` and `**` operators is also supported, although it must
be explicitly enabled for C++ containers by overriding the dereference operator (which
is done automatically for iterable Python objects), and is limited in some respects
compared to Python:

    1.  The unpacked container must be the last argument in its respective category
        (positional or keyword), and there can only be at most one of each at the call
        site.  These are not reflected in ordinary Python, but are necessary to ensure
        that compile-time argument matching is unambiguous.
    2.  The container's value type must be convertible to each of the argument types
        that follow it in the function signature, or else a compile error will be
        raised.
    3.  If double unpacking is performed, then the container must yield key-value pairs
        where the key is implicitly convertible to a string, and the value is
        convertible to the corresponding argument type.  If this is not the case, a
        compile error will be raised.
    4.  If the container does not contain enough elements to satisfy the remaining
        arguments, or it contains too many, a runtime error will be raised when the
        function is called.  Since it is impossible to know the size of the container
        at compile time, this cannot be done statically.

Examples
--------
Consider the following function:

    int subtract(int x, int y) {
        return x - y;
    }

We can directly wrap this as a `py::Function` if we want, which does not alter the
calling convention or signature in any way:

    py::Function func("subtract", "a simple example function", subtract);
    func(1, 2);  // returns -1

If this function is exported to Python, its call signature will remain unchanged,
meaning that both arguments must be supplied as positional-only arguments, and no
default values will be considered.

    >>> func(1, 2)  # ok, returns -1
    >>> func(1)  # error: missing required positional argument
    >>> func(1, y = 2)  # error: unexpected keyword argument

We can add parameter names and default values by annotating the C++ function (or a
wrapper around it) with `py::Arg` tags.  For instance:

    py::Function func(
        "subtract",
        "a simple example function",
        [](py::Arg<"x", int> x, py::Arg<"y", int>::opt y) {
            return subtract(x.value, y.value);
        },
        py::arg<"y"> = 2
    );

Note that the annotations store their values in an explicit `value` member, which uses
aggregate initialization to extend the lifetime of temporaries.  The annotations can
thus store references with the same semantics as an ordinary function call, as if the
annotations were not present.  For instance, this:

    py::Function func(
        "subtract",
        "a simple example function",
        [](py::Arg<"x", const int&> x, py::Arg<"y", const int&>::opt y) {
            return subtract(x.value, y.value);
        },
        py::arg<"y"> = 2
    );

is equivalent to the previous example in every way, but with the added benefit that the
`x` and `y` arguments will not be copied unnecessarily according to C++ value
semantics.

With this in place, we can now do the following:

    func(1);
    func(1, 2);
    func(1, py::arg<"y"> = 2);

    // or, equivalently:
    static constexpr auto x = py::arg<"x">;
    static constexpr auto y = py::arg<"y">;
    func(x = 1);
    func(x = 1, y = 2);
    func(y = 2, x = 1);  // keyword arguments can have arbitrary order

All of which will return the same result as before.  The function can also be passed to
Python and called similarly:

    >>> func(1)
    >>> func(1, 2)
    >>> func(1, y = 2)
    >>> func(x = 1)
    >>> func(x = 1, y = 2)
    >>> func(y = 2, x = 1)

What's more, all of the logic necessary to handle these cases is resolved statically at
compile time, meaning that there is no runtime cost for using these annotations, and no
additional code is generated for the function itself.  When it is called from C++, all
we have to do is inspect the provided arguments and match them against the underlying
signature, generating a compile time index sequence that can be used to reorder the
arguments and insert default values where needed.  In fact, each of the above
invocations will be transformed into the same underlying function call, with virtually
the same performance characteristics as raw C++ (disregarding any extra indirection
caused by the `std::function` wrapper).

Additionally, since all arguments are evaluated purely at compile time, we can enforce
strong type safety guarantees on the function signature and disallow invalid calls
using template constraints.  This means that proper call syntax is automatically
enforced throughout the codebase, in a way that allows static analyzers to give proper
syntax highlighting and LSP support. */
template <typename F>
    requires (
        impl::canonical_function_type<F> &&
        Signature<F>::args_fit_within_bitset &&
        Signature<F>::no_qualified_args &&
        Signature<F>::no_qualified_return &&
        Signature<F>::proper_argument_order &&
        Signature<F>::no_duplicate_args &&
        Signature<F>::args_are_python &&
        Signature<F>::return_is_python
    )
struct Function : Object, Interface<Function<F>> {
private:

    /* Non-member function type. */
    template <typename Sig>
    struct PyFunction : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A wrapper around a C++ or Python function, which allows it to be used from
both languages.

Notes
-----
This type is not directly instantiable from Python.  Instead, it can only be
accessed through the `bertrand.Function` template interface, which can be
navigated by subscripting the interface according to a possible function
signature.

Examples
--------
>>> from bertrand import Function
>>> Function[::int, "x": int, "y": int]
<class 'py::Function<py::Int(*)(py::Arg<"x", py::Int>, py::Arg<"y", py::Int>)>'>
>>> Function[::None, "*objects": object, "sep": str: ..., "end": str: ..., "file": object: ..., "flush": bool: ...]
<class 'py::Function<void(*)(py::Arg<"objects", py::Object>::args, py::Arg<"sep", py::Str>::opt, py::Arg<"end", py::Str>::opt, py::Arg<"file", py::Object>::opt, py::Arg<"flush", py::Bool>::opt)>'>
>>> Function[list[object]::None, "*", "key": object: ..., "reverse": bool: ...]
<class 'py::Function<void(py::List<py::Object>::*)(py::Arg<"key", py::Object>::kw::opt, py::Arg<"reverse", py::Bool>::kw::opt)>'>
>>> Function[type[bytes]::bytes, "string": str, "/"]
<class 'py::Function<py::Bytes(Type<py::Bytes>::*)(py::Arg<"string", py::Str>::pos)>'>

Each of these accessors will resolve to a unique Python type that wraps a
specific C++ function signature.

The 2nd example shows the template signature of the built-in `print()`
function, which returns void and accepts variadic positional arguments of any
type, followed by keyword arguments of various types, all of which are optional
(indicated by the trailing `...` syntax).

The 3rd example represents a bound member function corresponding to the
built-in `list.sort()` method, which accepts two optional keyword-only
arguments, where the list can contain any type.  The `*` delimiter works
just like a standard Python function declaration in this case, with equivalent
semantics.  The type of the bound `self` parameter is given on the left side of
the `list[object]::None` return type, which can be thought of similar to a C++
`::` scope accessor.  The type on the right side is the method's normal return
type, which in this case is `None`.

The 4th example represents a class method corresponding to the built-in
`bytes.fromhex()` method, which accepts a single, required, positional-only
argument of type `str`.  The `/` delimiter is used to indicate positional-only
arguments similar to `*`.  The type of the `self` parameter in this case is
given as a subscription of `type[]`, which indicates that the bound `self`
parameter is a type object, and thus the method is a class method.)doc";

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object pyfunc = None;
        Object pysignature = None;
        /// TODO: cache the member function type for structural |, &, isinstance(), and issubclass()
        Object member_type = None;
        Object name = None;
        Object docstring = None;
        Sig::Defaults defaults;
        std::function<typename Sig::to_value::type> func;
        Sig::Overloads overloads;

        /* Exposes a C++ function to Python */
        explicit PyFunction(
            Object&& name,
            Object&& docstring,
            Sig::Defaults&& defaults,
            std::function<typename Sig::to_value::type>&& func
        ) : defaults(std::move(defaults)), func(std::move(func)),
            name(std::move(name)), docstring(std::move(docstring))
        {}

        /* Exposes a Python function to C++ by generating a capturing lambda wrapper,
        after a quick signature validation.  The function must exactly match the
        enclosing signature, including argument names, types, and
        posonly/kwonly/optional/variadic qualifiers. */
        explicit PyFunction(
            PyObject* pyfunc,
            PyObject* name = nullptr,
            PyObject* docstring = nullptr,
            impl::Inspect* signature = nullptr
        ) :
            pyfunc(pyfunc),
            defaults([](PyObject* pyfunc, impl::Inspect* signature) {
                if (signature) {
                    return validate_signature(pyfunc, *signature);
                } else {
                    impl::Inspect signature = {pyfunc, Sig::seed, Sig::prime};
                    return validate_signature(pyfunc, signature);
                }
            }(pyfunc, signature)),
            func(Sig::capture(pyfunc))
        {
            this->name = name ? Py_NewRef(name) : PyObject_GetAttr(
                name,
                ptr(impl::template_string<"__name__">())
            );
            if (this->name == nullptr) {
                Exception::from_python();
            }
            this->docstring = docstring ? Py_NewRef(docstring) : PyObject_GetAttr(
                docstring,
                ptr(impl::template_string<"__doc__">())
            );
            if (this->docstring == nullptr) {
                Py_DECREF(this->name);
                Exception::from_python();
            }
            Py_INCREF(this->pyfunc);
        }

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings);
        static Type<Function> __import__();

        static PyObject* __new__(
            PyTypeObject* cls,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            PyFunction* self = reinterpret_cast<PyFunction*>(cls->tp_alloc(cls, 0));
            if (self == nullptr) {
                return nullptr;
            }
            self->__vectorcall__ = reinterpret_cast<vectorcallfunc>(__call__);
            new (&self->pyfunc) Object(None);
            new (&self->pysignature) Object(None);
            new (&self->member_function_type) Object(None);
            new (&self->name) Object(None);
            new (&self->docstring) Object(None);
            new (&self->defaults) Sig::Defaults();
            new (&self->func) std::function<typename Sig::to_value::type>();
            new (&self->overloads) Sig::Overloads();
            return reinterpret_cast<PyObject*>(self);
        }

        static int __init__(
            PyFunction* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            /// TODO: if no positional arguments are provided, generate a default base
            /// function that immediately raises a TypeError.  In this case, the name
            /// and docstring can be passed in as keyword arguments, otherwise they
            /// are inferred from the function itself.
            /// -> Actually what I should do is allow the keyword arguments to be
            /// supplied at all times, in order to allow for binding lambdas and other
            /// function objects in Python.

            try {
                size_t nargs = PyTuple_GET_SIZE(args);
                if (nargs > 1) {
                    throw TypeError(
                        "expected at most one positional argument, but received " +
                        std::to_string(nargs)
                    );
                }
                Object name = reinterpret_steal<Object>(nullptr);
                Object doc = reinterpret_steal<Object>(nullptr);
                if (kwargs) {
                    name = reinterpret_steal<Object>(PyDict_GetItem(
                        kwargs,
                        ptr(impl::template_string<"name">())
                    ));
                    if (!name.is(nullptr) && !PyUnicode_Check(ptr(name))) {
                        throw TypeError(
                            "expected 'name' to be a string, not: " + repr(name)
                        );
                    }
                    doc = reinterpret_steal<Object>(PyDict_GetItem(
                        kwargs,
                        ptr(impl::template_string<"doc">())
                    ));
                    if (!doc.is(nullptr) && !PyUnicode_Check(ptr(doc))) {
                        throw TypeError(
                            "expected 'doc' to be a string, not: " + repr(doc)
                        );
                    }
                    Py_ssize_t observed = name.is(nullptr) + doc.is(nullptr);
                    if (observed != PyDict_Size(kwargs)) {
                        throw TypeError(
                            "received unexpected keyword argument(s): " +
                            repr(reinterpret_borrow<Object>(kwargs))
                        );
                    }
                }

                if (nargs == 0) {
                    /// TODO: generate a default base function that raises a TypeError
                    /// when called, and forward to first constructor.
                }


                PyObject* func = PyTuple_GET_ITEM(args, 0);
                impl::Inspect signature = {func, Sig::seed, Sig::prime};

                // remember the original signature for the benefit of static analyzers,
                // documentation purposes, etc.
                new (self) PyFunction(
                    func,
                    nullptr,  /// TODO: name and docstring passed into constructor as kwargs
                    nullptr,
                    &signature
                );
                self->pysignature = release(signature.signature);
                PyObject_GC_Track(self);
                return 0;

            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /// TODO: implement a private _call() method that avoids conversions and
        /// directly invokes the function with the preconverted vectorcall arguments.
        /// That would make the overload system signficantly faster, since it avoids
        /// extra heap allocations and overload checks.

        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                /// convert the vectorcall arguments into bertrand types
                typename Sig::Vectorcall vectorcall {args, nargsf, kwnames};

                // check for overloads and forward if one is found
                if (self->overloads.root) {
                    PyObject* overload = self->overloads.search_instance(
                        vectorcall.key()
                    );
                    if (overload) {
                        return PyObject_Vectorcall(
                            overload,
                            vectorcall.args(),
                            vectorcall.nargsf(),
                            vectorcall.kwnames()
                        );
                    }
                }

                // if this function wraps a captured Python function, then we can
                // immediately forward to it as an optimization
                if (!self->pyfunc.is(None)) {
                    return PyObject_Vectorcall(
                        ptr(self->pyfunc),
                        vectorcall.args(),
                        vectorcall.nargsf(),
                        vectorcall.kwnames()
                    );
                }

                // otherwise, we fall back to the base C++ implementation, which
                // translates the arguments according to the template signature
                return release(to_python(
                    vectorcall(self->defaults, self->func)
                ));

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Bind a set of arguments to this function, producing a partial function that
        injects them 
         */
        static PyObject* bind(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            /// TODO: get the types of all the arguments, confirm that they match the
            /// enclosing signature, and then produce a corresponding function type,
            /// which will probably involve a private constructor call.  I might be
            /// able to determine the type ahead of time, and then call its Python-level
            /// constructor to do the validation + error handling.
        }

        /* Simulate a function call, returning the overload that would be chosen if
        the function were to be called with the given arguments, or None if they are
        malformed. */
        static PyObject* resolve(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                typename Sig::Vectorcall vectorcall {args, nargsf, kwnames};
                std::optional<PyObject*> func =
                    self->overloads.get_instance(vectorcall.key());
                PyObject* value = func.value_or(Py_None);
                return Py_NewRef(value ? value : self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Register an overload from Python.  Accepts only a single argument, which
        must be a function or other callable object that can be passed to the
        `inspect.signature()` factory function.  That includes user-defined types with
        overloaded call operators, as long as the operator is properly annotated
        according to Python style, or the object provides a `__signature__` property
        that returns a valid `inspect.Signature` object.  This method can be used as a
        decorator from Python. */
        static PyObject* overload(PyFunction* self, PyObject* func) noexcept {
            try {
                Object obj = reinterpret_borrow<Object>(func);
                impl::Inspect signature(obj, Sig::seed, Sig::prime);
                if (!issubclass<typename Sig::Return>(signature.returns())) {
                    std::string message =
                        "overload return type '" + repr(signature.returns()) +
                        "' is not a subclass of " +
                        repr(Type<typename Sig::Return>());
                    PyErr_SetString(PyExc_TypeError, message.c_str());
                    return nullptr;
                }
                self->overloads.insert(signature.key(), obj);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Remove an overload from this function.  Throws a KeyError if the function
        is not found. */
        static PyObject* remove(PyFunction* self, PyObject* func) noexcept {
            try {
                self->overloads.remove(reinterpret_borrow<Object>(func));
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Manually clear the function's overload trie from Python. */
        static PyObject* clear(PyFunction* self) noexcept {
            try {
                self->overloads.clear();
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Manually clear the function's overload cache from Python. */
        static PyObject* flush(PyFunction* self) noexcept {
            try {
                self->overloads.flush();
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __getitem__(PyFunction* self, PyObject* specifier) noexcept {
            try {
                if (PyTuple_Check(specifier)) {
                    Py_INCREF(specifier);
                } else {
                    specifier = PyTuple_Pack(1, specifier);
                    if (specifier == nullptr) {
                        return nullptr;
                    }
                }
                auto key = subscript_key(
                    reinterpret_borrow<Object>(specifier)
                );
                std::optional<PyObject*> func = self->overloads.get_subclass(key);
                PyObject* value = func.value_or(Py_None);
                return Py_NewRef(value ? value : self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __delitem__(
            PyFunction* self,
            PyObject* specifier,
            PyObject* value
        ) noexcept {
            try {
                if (value) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "functions do not support item assignment: use "
                        "`@func.overload` to register an overload instead"
                    );
                    return -1;
                }
                if (PyTuple_Check(specifier)) {
                    Py_INCREF(specifier);
                } else {
                    specifier = PyTuple_Pack(1, specifier);
                    if (specifier == nullptr) {
                        return -1;
                    }
                }
                auto key = subscript_key(
                    reinterpret_borrow<Object>(specifier)
                );
                Object func = reinterpret_borrow<Object>(
                    self->overloads.search_subclass(key)
                );
                if (func.is(nullptr)) {
                    PyErr_SetString(
                        PyExc_ValueError,
                        "cannot delete a function's base overload"
                    );
                    return -1;
                }
                self->overloads.remove(func);
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static int __bool__(PyFunction* self) noexcept {
            /// NOTE: `bool()` typically forwards to `len()`, which would cause
            /// functions to erroneously evaluate to false in some circumstances.
            return true;
        }

        static Py_ssize_t __len__(PyFunction* self) noexcept {
            return self->overloads.data.size();
        }

        static PyObject* __iter__(PyFunction* self) noexcept {
            try {
                return release(Iterator(
                    self->overloads.data | std::views::transform(
                        [](const Sig::Overloads::Metadata& data) -> Object {
                            return data.func;
                        }
                    )
                ));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __contains__(PyFunction* self, PyObject* func) noexcept {
            try {
                for (const auto& data : self->overloads.data) {
                    if (ptr(data.func) == func) {
                        return 1;
                    }
                }
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /* Attach a function to a type as an instance method descriptor.  Accepts the
        type to attach to, which can be provided by calling this method as a decorator
        from Python. */
        static PyObject* method(PyFunction* self, void*) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "method() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    impl::Method* descr = reinterpret_cast<impl::Method*>(
                        impl::Method::__type__.tp_alloc(
                            &impl::Method::__type__,
                            0
                        )
                    );
                    if (descr == nullptr) {
                        return nullptr;
                    }
                    try {
                        new (descr) impl::Method(reinterpret_borrow<Object>(self));
                    } catch (...) {
                        Py_DECREF(descr);
                        throw;
                    }
                    return descr;
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Attach a function to a type as a class method descriptor.  Accepts the type
        to attach to, which can be provided by calling this method as a decorator from
        Python. */
        static PyObject* classmethod(PyFunction* self, void*) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "classmethod() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    impl::ClassMethod* descr = reinterpret_cast<impl::ClassMethod*>(
                        impl::ClassMethod::__type__.tp_alloc(
                            &impl::ClassMethod::__type__,
                            0
                        )
                    );
                    if (descr == nullptr) {
                        return nullptr;
                    }
                    try {
                        new (descr) impl::ClassMethod(reinterpret_borrow<Object>(self));
                    } catch (...) {
                        Py_DECREF(descr);
                        throw;
                    }
                    return descr;
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Attach a function to a type as a static method descriptor.  Accepts the type
        to attach to, which can be provided by calling this method as a decorator from
        Python. */
        static PyObject* staticmethod(PyFunction* self, void*) noexcept {
            try {
                impl::StaticMethod* descr = reinterpret_cast<impl::StaticMethod*>(
                    impl::StaticMethod::__type__.tp_alloc(&impl::StaticMethod::__type__, 0)
                );
                if (descr == nullptr) {
                    return nullptr;
                }
                try {
                    new (descr) impl::StaticMethod(reinterpret_borrow<Object>(self));
                } catch (...) {
                    Py_DECREF(descr);
                    throw;
                }
                return descr;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /// TODO: .property needs to be converted into a getset descriptor that
        /// returns an unbound descriptor object.  The special binding logic is thus
        /// implemented in the descriptor's call operator.

        /* Attach a function to a type as a getset descriptor.  Accepts a type object
        to attach to, which can be provided by calling this method as a decorator from
        Python, as well as two keyword-only arguments for an optional setter and
        deleter.  The same getter/setter fields are available from the descriptor
        itself via traditional Python `@Type.property.setter` and
        `@Type.property.deleter` decorators. */
        static PyObject* property(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "property() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    using T = impl::ArgTraits<typename Sig::template at<0>>::type;
                    size_t nargs = PyVectorcall_NARGS(nargsf);
                    PyObject* cls;
                    if (nargs == 0) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "%U.property() requires a type object as the sole "
                            "positional argument",
                            self->name
                        );
                        return nullptr;
                    } else if (nargs == 1) {
                        cls = args[0];
                    } else {
                        PyErr_Format(
                            PyExc_TypeError,
                            "%U.property() takes exactly one positional "
                            "argument",
                            self->name
                        );
                        return nullptr;
                    }
                    if (!PyType_Check(cls)) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "expected a type object, not: %R",
                            cls
                        );
                        return nullptr;
                    }
                    if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "class must be a must be a subclass of %R",
                            ptr(Type<T>())
                        );
                        return nullptr;
                    }

                    PyObject* fset = nullptr;
                    PyObject* fdel = nullptr;
                    if (kwnames) {
                        Py_ssize_t kwcount = PyTuple_GET_SIZE(kwnames);
                        if (kwcount > 2) {
                            PyErr_SetString(
                                PyExc_TypeError,
                                "property() takes at most 2 keyword arguments"
                            );
                            return nullptr;
                        } else if (kwcount > 1) {
                            PyObject* key = PyTuple_GET_ITEM(kwnames, 0);
                            int rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"setter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fset = args[1];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"deleter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fdel = args[1];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                            key = PyTuple_GET_ITEM(kwnames, 1);
                            rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"deleter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fdel = args[2];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"setter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fset = args[2];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                        } else if (kwcount > 0) {
                            PyObject* key = PyTuple_GET_ITEM(kwnames, 0);
                            int rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"setter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fset = args[1];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"deleter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fdel = args[1];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                        }
                    }
                    /// TODO: validate fset and fdel are callable with the expected
                    /// signatures -> This can be done with the Inspect() helper, which
                    /// will extract all overload keys from the function.  I just have
                    /// to confirm that at least one path through the overload trie
                    /// matches the expected signature.

                    if (PyObject_HasAttr(cls, self->name)) {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "attribute '%U' already exists on type '%R'",
                            self->name,
                            cls
                        );
                        return nullptr;
                    }
                    using Property = impl::Property;
                    Property* descr = reinterpret_cast<Property*>(
                        Property::__type__.tp_alloc(&Property::__type__, 0)
                    );
                    if (descr == nullptr) {
                        return nullptr;
                    }
                    try {
                        new (descr) Property(cls, self, fset, fdel);
                    } catch (...) {
                        Py_DECREF(descr);
                        Exception::to_python();
                        return nullptr;
                    }
                    int rc = PyObject_SetAttr(cls, self->name, descr);
                    Py_DECREF(descr);
                    if (rc) {
                        return nullptr;
                    }
                    return Py_NewRef(cls);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Implement the descriptor protocol to generate bound member functions.  Note
        that due to the Py_TPFLAGS_METHOD_DESCRIPTOR flag, this will not be called when
        invoking the function as a method during normal use.  It's only used when the
        method is accessed via the `.` operator and not immediately called. */
        static PyObject* __get__(
            PyFunction* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            try {
                PyObject* cls = reinterpret_cast<PyObject*>(Py_TYPE(self));

                // get the current function's template key and allocate a copy
                Object unbound_key = reinterpret_steal<Object>(PyObject_GetAttr(
                    cls,
                    ptr(impl::template_string<"__template__">())
                ));
                if (unbound_key.is(nullptr)) {
                    return nullptr;
                }
                Py_ssize_t len = PyTuple_GET_SIZE(ptr(unbound_key));
                Object bound_key = reinterpret_steal<Object>(
                    PyTuple_New(len - 1)
                );
                if (bound_key.is(nullptr)) {
                    return nullptr;
                }

                // the first element encodes the unbound function's return type.  All
                // we need to do is replace the first index of the slice with the new
                // type and exclude the first argument from the unbound key
                Object slice = reinterpret_steal<Object>(PySlice_New(
                    type == Py_None ?
                        reinterpret_cast<PyObject*>(Py_TYPE(obj)) : type,
                    Py_None,
                    reinterpret_cast<PySliceObject*>(
                        PyTuple_GET_ITEM(ptr(unbound_key), 0)
                    )->step
                ));
                if (slice.is(nullptr)) {
                    return nullptr;
                }
                PyTuple_SET_ITEM(ptr(bound_key), 0, release(slice));
                for (size_t i = 2; i < len; ++i) {  // skip return type and first arg
                    PyTuple_SET_ITEM(
                        ptr(bound_key),
                        i - 1,
                        Py_NewRef(PyTuple_GET_ITEM(ptr(unbound_key), i))
                    );
                }

                // once the new key is built, we can index the unbound function type to
                // get the corresponding Python class for the bound function
                Object bound_type = reinterpret_steal<Object>(PyObject_GetItem(
                    cls,
                    ptr(bound_key)
                ));
                if (bound_type.is(nullptr)) {
                    return nullptr;
                }
                PyObject* args[] = {ptr(bound_type), self, obj};
                return PyObject_VectorcallMethod(
                    ptr(impl::template_string<"_capture">()),
                    args,
                    3,
                    nullptr
                );

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<Function>()))
                )) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<__python__*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<Function>()))
                )) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<__python__*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(PyFunction* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(
                    obj,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(PyFunction* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(
                    cls,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __name__(PyFunction* self, void*) noexcept {
            return Py_NewRef(ptr(self->name));
        }

        static PyObject* __signature__(PyFunction* self, void*) noexcept {
            if (!self->pysignature.is(None)) {
                return Py_NewRef(ptr(self->pysignature));
            }

            try {
                Object inspect = reinterpret_steal<Object>(PyImport_Import(
                    ptr(impl::template_string<"inspect">())
                ));
                if (inspect.is(nullptr)) {
                    return nullptr;
                }

                // if this function captures a Python function, forward to it
                if (!(self->pyfunc.is(None))) {
                    return PyObject_CallOneArg(
                        ptr(getattr<"signature">(inspect)),
                        ptr(self->pyfunc)
                    );
                }

                // otherwise, we need to build a signature object ourselves
                Object Signature = getattr<"Signature">(inspect);
                Object Parameter = getattr<"Parameter">(inspect);

                // build the parameter annotations
                Object tuple = reinterpret_steal<Object>(PyTuple_New(Sig::n));
                if (tuple.is(nullptr)) {
                    return nullptr;
                }
                []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyObject* tuple,
                    PyFunction* self,
                    const Object& Parameter
                ) {
                    (PyTuple_SET_ITEM(  // steals a reference
                        tuple,
                        Is,
                        release(build_parameter<Is>(self, Parameter))
                    ), ...);
                }(
                    std::make_index_sequence<Sig::n>{},
                    ptr(tuple),
                    self,
                    Parameter
                );

                // get the return annotation
                Type<typename Sig::Return> return_type;

                // create the signature object
                return release(Signature(tuple, arg<"return_annotation"_> = return_type));

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(PyFunction* self) noexcept {
            try {
                std::string str = "<" + type_name<Function<F>> + " at " +
                    std::to_string(reinterpret_cast<size_t>(self)) + ">";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /* Implements the Python constructor without any safety checks. */
        /// TODO: not sure if this is strictly necessary?  I think it's called from bound
        /// methods to accelerate them?

        static PyObject* _self_type(PyFunction* self, void*) noexcept {
            if constexpr (Sig::n == 0 || !(Sig::has_pos || Sig::has_args)) {
                Py_RETURN_NONE;
            } else {
                using T = impl::ArgTraits<typename Sig::template at<0>>::type;
                return release(Type<T>());
            }
        }

        static PyObject* _return_type(PyFunction* self, void*) noexcept {
            if constexpr (std::is_void_v<typename Sig::Return>) {
                Py_RETURN_NONE;
            } else {
                return release(Type<typename Sig::Return>());
            }
        }

        static PyObject* _bind_method(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf
        ) noexcept {
            using T = impl::ArgTraits<typename Sig::template at<0>>::type;
            size_t nargs = PyVectorcall_NARGS(nargsf);
            if (nargs != 2) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_method() requires exactly two positional arguments"
                );
                return nullptr;
            }
            PyObject* cls = args[0];
            if (!PyType_Check(cls)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "method() requires a type object"
                );
                return nullptr;
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class must be a must be a subclass of %R",
                    ptr(Type<T>())
                );
                return nullptr;
            }
            if (PyObject_HasAttr(cls, self->name)) {
                PyErr_Format(
                    PyExc_AttributeError,
                    "attribute '%U' already exists on type '%R'",
                    self->name,
                    cls
                );
                return nullptr;
            }
            PyObject* descr = args[1];
            if (!PyType_IsSubtype(Py_TYPE(descr), &impl::Method::__type__)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_method() requires a Bertrand method descriptor as "
                    "the second argument"
                );
                return nullptr;
            }
            int rc = PyObject_SetAttr(cls, self->name, descr);
            if (rc) {
                return nullptr;
            }
            return Py_NewRef(cls);
        }

        static PyObject* _bind_classmethod(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf
        ) noexcept {
            using T = impl::ArgTraits<typename Sig::template at<0>>::type;
            size_t nargs = PyVectorcall_NARGS(nargsf);
            if (nargs != 2) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_classmethod() requires exactly two positional arguments"
                );
                return nullptr;
            }
            PyObject* cls = args[0];
            if (!PyType_Check(cls)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "classmethod() requires a type object"
                );
                return nullptr;
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class must be a must be a subclass of %R",
                    ptr(Type<T>())
                );
                return nullptr;
            }
            if (PyObject_HasAttr(cls, self->name)) {
                PyErr_Format(
                    PyExc_AttributeError,
                    "attribute '%U' already exists on type '%R'",
                    self->name,
                    cls
                );
                return nullptr;
            }
            PyObject* descr = args[1];
            if (!PyType_IsSubtype(Py_TYPE(descr), &impl::ClassMethod::__type__)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_classmethod() requires a Bertrand classmethod "
                    "descriptor as the second argument"
                );
                return nullptr;
            }
            int rc = PyObject_SetAttr(cls, self->name, descr);
            if (rc) {
                return nullptr;
            }
            return Py_NewRef(cls);
        }

        static PyObject* _bind_staticmethod(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf
        ) noexcept {
            size_t nargs = PyVectorcall_NARGS(nargsf);
            if (nargs != 2) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_staticmethod() requires exactly two positional "
                    "arguments"
                );
                return nullptr;
            }
            PyObject* cls = args[0];
            if (!PyType_Check(cls)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "staticmethod() requires a type object"
                );
                return nullptr;
            }
            if (PyObject_HasAttr(cls, self->name)) {
                PyErr_Format(
                    PyExc_AttributeError,
                    "attribute '%U' already exists on type '%R'",
                    self->name,
                    cls
                );
                return nullptr;
            }
            PyObject* descr = args[1];
            if (!PyType_IsSubtype(Py_TYPE(descr), &impl::StaticMethod::__type__)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_staticmethod() requires a Bertrand classmethod "
                    "descriptor as the second argument"
                );
                return nullptr;
            }
            int rc = PyObject_SetAttr(cls, self->name, descr);
            if (rc) {
                return nullptr;
            }
            return Py_NewRef(cls);
        }

        /// TODO: bind_property?

        static PyObject* _subtrie_len(PyFunction* self, PyObject* value) noexcept {
            try {
                size_t len = 0;
                for (const typename Sig::Overloads::Metadata& data :
                    self->overloads.match(value)
                ) {
                    ++len;
                }
                return PyLong_FromSize_t(len);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* _subtrie_iter(PyFunction* self, PyObject* value) noexcept {
            try {
                return release(Iterator(
                    self->overloads.match(value) | std::views::transform(
                        [](const typename Sig::Overloads::Metadata& data) -> Object {
                            return data.func;
                        }
                    )
                ));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* _subtrie_contains(
            PyFunction* self,
            PyObject* const* args,
            Py_ssize_t nargsf
        ) noexcept {
            try {
                for (const typename Sig::Overloads::Metadata& data :
                    self->overloads.match(args[0])
                ) {
                    if (data.func == args[1]) {
                        Py_RETURN_TRUE;
                    }
                }
                Py_RETURN_FALSE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /// TODO: all this constructor crap also has to be reflected for bound methods.

        static PyObject* validate_signature(PyObject* func, const impl::Inspect& signature) {
            // ensure at least one possible return type exactly matches the
            // expected template signature
            Object rtype = std::is_void_v<typename Sig::Return> ?
                reinterpret_borrow<Object>(Py_None) :
                Object(Type<typename Sig::Return>());
            bool match = false;
            for (PyObject* returns : signature.returns()) {
                if (rtype.is(returns)) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                throw TypeError(
                    "base function must return " + repr(rtype) + ", not: '" +
                    repr(reinterpret_borrow<Object>(signature.returns()[0])) +
                    "'"
                );
            }

            // ensure at least one complete parameter list exactly matches the
            // expected template signature
            constexpr auto validate = []<size_t... Is>(
                std::index_sequence<Is...>,
                impl::Inspect& signature,
                const auto& key
            ) {
                return (validate_parameter<Is>(key[Is]) && ...);
            };
            match = false;
            for (const auto& key : signature) {
                if (
                    key.size() == Sig::n &&
                    validate(std::make_index_sequence<Sig::n>{}, signature, key)
                ) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                throw TypeError(
                    /// TODO: improve this error message by printing out the
                    /// expected signature.  Maybe I can just get the repr of the
                    /// current function type?
                    "no match for parameter list"
                );
            }

            // extract default values from the signature
            return []<size_t... Js>(std::index_sequence<Js...>, impl::Inspect& sig) {
                return typename Sig::Defaults{extract_default<Js>(sig)...};
            }(std::make_index_sequence<Sig::n_opt>{}, signature);
        }

        template <size_t I>
        static bool validate_parameter(const Param& param) {
            using T = Sig::template at<I>;
            return (
                param.name == impl::ArgTraits<T>::name &&
                param.kind == impl::ArgTraits<T>::kind &&
                param.value == ptr(Type<typename impl::ArgTraits<T>::type>())
            );
        }

        template <size_t J>
        static Object extract_default(impl::Inspect& signature) {
            Object default_value = getattr<"default">(
                signature.at(Sig::Defaults::template rfind<J>)
            );
            if (default_value.is(getattr<"empty">(signature.signature))) {
                throw TypeError(
                    "missing default value for parameter '" +
                    impl::ArgTraits<typename Sig::Defaults::template at<J>>::name + "'"
                );
            }
            return default_value;
        }

        static Params<std::vector<Param>> subscript_key(
            const Object& specifier
        ) {
            size_t hash = 0;
            Py_ssize_t size = PyTuple_GET_SIZE(ptr(specifier));
            std::vector<Param> key;
            key.reserve(size);

            std::unordered_set<std::string_view> names;
            Py_ssize_t kw_idx = std::numeric_limits<Py_ssize_t>::max();
            for (Py_ssize_t i = 0; i < size; ++i) {
                PyObject* item = PyTuple_GET_ITEM(ptr(specifier), i);

                // slices represent keyword arguments
                if (PySlice_Check(item)) {
                    PySliceObject* slice = reinterpret_cast<PySliceObject*>(item);
                    if (!PyUnicode_Check(slice->start)) {
                        throw TypeError(
                            "expected a keyword argument name as first "
                            "element of slice, not " + repr(
                                reinterpret_borrow<Object>(slice->start)
                            )
                        );
                    }
                    std::string_view name = impl::get_parameter_name(slice->start);
                    if (names.contains(name)) {
                        throw TypeError(
                            "duplicate keyword argument: " + std::string(name)
                        );
                    }
                    if (!PyType_Check(slice->stop)) {
                        throw TypeError(
                            "expected a type as second element of slice, not " +
                            repr(reinterpret_borrow<Object>(slice->stop))
                        );
                    }
                    if (slice->step != Py_None) {
                        throw TypeError(
                            "keyword argument cannot have a third slice element: " +
                            repr(reinterpret_borrow<Object>(slice->step))
                        );
                    }
                    key.emplace_back(
                        name,
                        reinterpret_borrow<Object>(slice->stop),
                        impl::ArgKind::KW
                    );
                    hash = impl::hash_combine(
                        hash,
                        key.back().hash(Sig::seed, Sig::prime)
                    );
                    kw_idx = i;
                    names.insert(name);

                // all other objects are positional arguments
                } else {
                    if (i > kw_idx) {
                        throw TypeError(
                            "positional argument follows keyword argument"
                        );
                    }
                    if (!PyType_Check(item)) {
                        throw TypeError(
                            "expected a type object, not " +
                            repr(reinterpret_borrow<Object>(item))
                        );
                    }
                    key.emplace_back(
                        "",
                        reinterpret_borrow<Object>(item),
                        impl::ArgKind::POS
                    );
                    hash = impl::hash_combine(
                        hash,
                        key.back().hash(Sig::seed, Sig::prime)
                    );
                }
            }

            return {std::move(key), hash};
        }

        Object structural_type() const {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(impl::template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object cls = reinterpret_steal<Object>(_self_type(*this, nullptr));
            if (cls.is(None)) {
                throw TypeError("function must accept at least one positional argument");
            }
            Object key = getattr<"__template_key__">(cls);
            Py_ssize_t len = PyTuple_GET_SIZE(ptr(key));
            Object new_key = reinterpret_steal<Object>(PyTuple_New(len - 1));
            if (new_key.is(nullptr)) {
                Exception::from_python();
            }
            Object rtype = reinterpret_steal<Object>(PySlice_New(
                ptr(cls),
                Py_None,
                reinterpret_cast<PySliceObject*>(
                    PyTuple_GET_ITEM(ptr(key), 0)
                )->step
            ));
            if (rtype.is(nullptr)) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(new_key), 0, release(rtype));
            for (Py_ssize_t i = 2; i < len; ++i) {
                PyTuple_SET_ITEM(
                    ptr(new_key),
                    i - 1,
                    Py_NewRef(PyTuple_GET_ITEM(ptr(key), i))
                );
            }
            Object specialization = reinterpret_borrow<Object>(
                reinterpret_cast<PyObject*>(Py_Type(ptr(func)))
            )[new_key];
            Object result = reinterpret_steal<Object>(PySlice_New(
                ptr(name),
                ptr(specialization),
                Py_None
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return getattr<"Intersection">(bertrand)[result];
        }

        template <size_t I>
        static Object build_parameter(PyFunction* self, const Object& Parameter) {
            using T = Sig::template at<I>;
            using Traits = impl::ArgTraits<T>;

            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    Traits::name,
                    Traits::name.size()
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }

            Object kind;
            if constexpr (Traits::kwonly()) {
                kind = getattr<"KEYWORD_ONLY">(Parameter);
            } else if constexpr (Traits::kw()) {
                kind = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            } else if constexpr (Traits::pos()) {
                kind = getattr<"POSITIONAL_ONLY">(Parameter);
            } else if constexpr (Traits::args()) {
                kind = getattr<"VAR_POSITIONAL">(Parameter);
            } else if constexpr (Traits::kwargs()) {
                kind = getattr<"VAR_KEYWORD">(Parameter);
            } else {
                throw TypeError("unrecognized argument kind");
            }

            Object default_value = self->defaults.template get<I>();
            Type<typename Traits::type> annotation;

            PyObject* args[] = {
                nullptr,
                ptr(name),
                ptr(kind),
                ptr(default_value),
                ptr(annotation),
            };
            Object kwnames = reinterpret_steal<Object>(
                PyTuple_Pack(4,
                    ptr(impl::template_string<"name">()),
                    ptr(impl::template_string<"kind">()),
                    ptr(impl::template_string<"default">()),
                    ptr(impl::template_string<"annotation">())
                )
            );
            Object result = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(Parameter),
                args + 1,
                0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                ptr(kwnames)
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }

        inline static PyNumberMethods number = {
            .nb_bool = reinterpret_cast<inquiry>(&__bool__),
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "overload",
                reinterpret_cast<PyCFunction>(&overload),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "clear",
                reinterpret_cast<PyCFunction>(&clear),
                METH_NOARGS,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "flush",
                reinterpret_cast<PyCFunction>(&flush),
                METH_NOARGS,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "method",
                reinterpret_cast<PyCFunction>(&method),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "_bind_method",
                reinterpret_cast<PyCFunction>(&_bind_method),
                METH_FASTCALL,
                nullptr
            },
            {
                "_bind_classmethod",
                reinterpret_cast<PyCFunction>(&_bind_classmethod),
                METH_FASTCALL,
                nullptr
            },
            {
                "_bind_staticmethod",
                reinterpret_cast<PyCFunction>(&_bind_staticmethod),
                METH_FASTCALL,
                nullptr
            },
            {
                "_subtrie_len",
                reinterpret_cast<PyCFunction>(&_subtrie_len),
                METH_O,
                nullptr
            },
            {
                "_subtrie_iter",
                reinterpret_cast<PyCFunction>(&_subtrie_iter),
                METH_O,
                nullptr
            },
            {
                "_subtrie_contains",
                reinterpret_cast<PyCFunction>(&_subtrie_contains),
                METH_FASTCALL,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "method",
                reinterpret_cast<getter>(&method),
                nullptr,
                PyDoc_STR(
R"doc()doc"
                ),
                nullptr
            },
            {
                "classmethod",
                reinterpret_cast<getter>(&classmethod),
                nullptr,
                PyDoc_STR(
R"doc(Returns a classmethod descriptor for this function.

Returns
-------
classmethod
    A classmethod descriptor that binds the function to a type.

Raises
------
TypeError
    If the function does not accept at least one positional argument which can
    be interpreted as a type.

Notes
-----
The returned descriptor implements a call operator that attaches it to a type,
enabling this property to be called like a normal method/decorator.  The
unbound descriptor provides a convenient place to implement the `&` and `|`
operators for structural typing.)doc"
                ),
                nullptr
            },
            {
                "staticmethod",
                reinterpret_cast<getter>(&staticmethod),
                nullptr,
                PyDoc_STR(
R"doc(Returns a staticmethod descriptor for this function.

Returns
-------
staticmethod
    A staticmethod descriptor that binds the function to a type.

Notes
-----
The returned descriptor implements a call operator that attaches it to a type,
enabling this property to be called like a normal method/decorator.  The
unbound descriptor provides a convenient place to implement the `&` and `|`
operators for structural typing.)doc"
                ),
                nullptr
            },
            {
                "property",
                reinterpret_cast<getter>(&property),
                nullptr,
                PyDoc_STR(
R"doc(Returns a property descriptor that uses this function as a getter.

Returns
-------
property
    A property descriptor that binds the function to a type.

Raises
------
TypeError
    If the function does not accept exactly one positional argument which can
    be bound to the given type.

Notes
-----
The returned descriptor implements a call operator that attaches it to a type,
enabling this property to be called like a normal method/decorator.  The
unbound descriptor provides a convenient place to implement the `&` and `|`
operators for structural typing.)doc"
                ),
                nullptr
            },
            {
                "__signature__",
                reinterpret_cast<getter>(&__signature__),
                nullptr,
                PyDoc_STR(
R"doc(A property that produces an accurate `inspect.Signature` object when a
C++ function is introspected from Python.

Returns
-------
inspect.Signature
    A signature object that describes the function's expected arguments and
    return value.

Notes
-----
Providing this descriptor allows the `inspect` module to be used on C++
functions as if they were implemented in Python itself, reflecting the signature
of their underlying `py::Function` representation.)doc"
                ),
                nullptr
            },
            {
                "_self_type",
                reinterpret_cast<getter>(&_self_type),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "_return_type",
                reinterpret_cast<getter>(&_return_type),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    /* Bound member function type.  Must be constructed with a corresponding `self`
    parameter, which will be inserted as the first argument to a call according to
    Python style. */
    template <typename Sig> requires (Sig::has_self)
    struct PyFunction<Sig> : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A bound member function descriptor.

Notes
-----
This type is equivalent to Python's internal `types.MethodType`, which
describes the return value of a method descriptor when accessed from an
instance of an enclosing class.  The only difference is that this type is
implemented in C++, and thus has a unique instantiation for each signature.

Additionally, it must be noted that instances of this type must be constructed
with an appropriate `self` parameter, which is inserted as the first argument
to the underlying C++/Python function when called, according to Python style.
As such, it is not possible for an instance of this type to represent an
unbound function object; those are always represented as a non-member function
type instead.  By templating `py::Function<...>` on a member function pointer,
you are directly indicating the presence of the bound `self` parameter, in a
way that encodes this information into the type systems of both languages
simultaneously.

In essence, all this type does is hold a reference to both an equivalent
non-member function, as well as a reference to the `self` object that the
function is bound to.  All operations will be simply forwarded to the
underlying non-member function, including overloads, introspection, and so on,
but with the `self` argument already accounted for.

Examples
--------
>>> from bertrand import Function
>>> Function[::int, "x": int, "y": int]
<class 'py::Function<py::Int(*)(py::Arg<"x", py::Int>, py::Arg<"y", py::Int>)>'>
>>> Function[::None, "*objects": object, "sep": str: ..., "end": str: ..., "file": object: ..., "flush": bool: ...]
<class 'py::Function<void(*)(py::Arg<"objects", py::Object>::args, py::Arg<"sep", py::Str>::opt, py::Arg<"end", py::Str>::opt, py::Arg<"file", py::Object>::opt, py::Arg<"flush", py::Bool>::opt)>'>
>>> Function[list[object]::None, "*", "key": object: ..., "reverse": bool: ...]
<class 'py::Function<void(py::List<py::Object>::*)(py::Arg<"key", py::Object>::kw::opt, py::Arg<"reverse", py::Bool>::kw::opt)>'>
>>> Function[type[bytes]::bytes, "string": str, "/"]
<class 'py::Function<py::Bytes(Type<py::Bytes>::*)(py::Arg<"string", py::Str>::pos)>'>

Each of these accessors will resolve to a unique Python type that wraps a
specific C++ function signature.

The 2nd example shows the template signature of the built-in `print()`
function, which returns void and accepts variadic positional arguments of any
type, followed by keyword arguments of various types, all of which are optional
(indicated by the trailing `...` syntax).

The 3rd example represents a bound member function corresponding to the
built-in `list.sort()` method, which accepts two optional keyword-only
arguments, where the list can contain any type.  The `*` delimiter works
just like a standard Python function declaration in this case, with equivalent
semantics.  The type of the bound `self` parameter is given on the left side of
the `list[object]::None` return type, which can be thought of similar to a C++
`::` scope accessor.  The type on the right side is the method's normal return
type, which in this case is `None`.

The 4th example represents a class method corresponding to the built-in
`bytes.fromhex()` method, which accepts a single, required, positional-only
argument of type `str`.  The `/` delimiter is used to indicate positional-only
arguments similar to `*`.  The type of the `self` parameter in this case is
given as a subscription of `type[]`, which indicates that the bound `self`
parameter is a type object, and thus the method is a class method.)doc";

        vectorcallfunc call = reinterpret_cast<vectorcallfunc>(__call__);
        PyObject* __wrapped__;
        PyObject* __self__;

        explicit PyFunction(PyObject* __wrapped__, PyObject* __self__) noexcept :
            __wrapped__(Py_NewRef(__wrapped__)), __self__(Py_NewRef(__self__))
        {}

        ~PyFunction() noexcept {
            Py_XDECREF(__wrapped__);
            Py_XDECREF(__self__);
        }

        static void __dealloc__(PyFunction* self) noexcept {
            PyObject_GC_UnTrack(self);
            self->~PyFunction();
            Py_TYPE(self)->tp_free(self);
        }

        static PyObject* __new__(
            PyTypeObject* cls,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            try {
                PyFunction* self = reinterpret_cast<PyFunction*>(cls->tp_alloc(cls, 0));
                if (self == nullptr) {
                    return nullptr;
                }
                self->call = reinterpret_cast<vectorcallfunc>(__call__);
                self->__wrapped__ = nullptr;
                self->__self__ = nullptr;
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            PyFunction* self,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            try {
                size_t nargs = PyTuple_GET_SIZE(args);
                if (nargs != 2 || kwds != nullptr) {
                    PyErr_Format(
                        PyExc_TypeError,
                        "expected exactly 2 positional-only arguments, but "
                        "received %zd",
                        nargs
                    );
                    return -1;
                }
                PyObject* func = PyTuple_GET_ITEM(args, 0);
                impl::Inspect signature = {func, Sig::seed, Sig::prime};

                /// TODO: do everything from the unbound constructor, but also ensure
                /// that the self argument matches the expected type.
                /// -> NOTE: this must assert that the function being passed in has a
                /// `__self__` attribute that matches the expected type, which is true
                /// for both Python bound methods and my own bound methods.

            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /// TODO: I'll need a Python-level __init__/__new__ method that
        /// constructs a new instance of this type, which will be called
        /// when the descriptor is accessed.

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings);
        static Type<Function> __import__();

        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                /// NOTE: Python includes an optimization of the vectorcall protocol
                /// for bound functions that can temporarily forward the correct `self`
                /// argument without reallocating the underlying array, which we can
                /// take advantage of if possible.
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargsf & PY_VECTORCALL_ARGUMENTS_OFFSET) {
                    PyObject** arr = const_cast<PyObject**>(args) - 1;
                    PyObject* temp = arr[0];
                    arr[0] = self->__self__;
                    PyObject* result = PyObject_Vectorcall(
                        self->__wrapped__,
                        arr,
                        nargs + 1,
                        kwnames
                    );
                    arr[0] = temp;
                    return result;
                }

                /// otherwise, we have to heap allocate a new array and copy the arguments
                size_t n = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0);
                PyObject** arr = new PyObject*[n + 1];
                arr[0] = self->__self__;
                for (size_t i = 0; i < n; ++i) {
                    arr[i + 1] = args[i];
                }
                PyObject* result = PyObject_Vectorcall(
                    self->__wrapped__,
                    arr,
                    nargs + 1,
                    kwnames
                );
                delete[] arr;
                return result;

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static Py_ssize_t __len__(PyFunction* self) noexcept {
            PyObject* result = PyObject_CallMethodOneArg(
                self->__wrapped__,
                ptr(impl::template_string<"_subtrie_len">()),
                self->__self__
            );
            if (result == nullptr) {
                return -1;
            }
            Py_ssize_t len = PyLong_AsSsize_t(result);
            Py_DECREF(result);
            return len;
        }

        /* Subscripting a bound method will forward to the unbound method, prepending
        the key with the `self` argument. */
        static PyObject* __getitem__(
            PyFunction* self,
            PyObject* specifier
        ) noexcept {
            if (PyTuple_Check(specifier)) {
                Py_ssize_t len = PyTuple_GET_SIZE(specifier);
                PyObject* tuple = PyTuple_New(len + 1);
                if (tuple == nullptr) {
                    return nullptr;
                }
                PyTuple_SET_ITEM(tuple, 0, Py_NewRef(self->__self__));
                for (Py_ssize_t i = 0; i < len; ++i) {
                    PyTuple_SET_ITEM(
                        tuple,
                        i + 1,
                        Py_NewRef(PyTuple_GET_ITEM(specifier, i))
                    );
                }
                specifier = tuple;
            } else {
                specifier = PyTuple_Pack(2, self->__self__, specifier);
                if (specifier == nullptr) {
                    return nullptr;
                }
            }
            PyObject* result = PyObject_GetItem(self->__wrapped__, specifier);
            Py_DECREF(specifier);
            return result;
        }

        /* Deleting an overload from a bound method will forward the deletion to the
        unbound method, prepending the key with the `self` argument. */
        static int __delitem__(
            PyFunction* self,
            PyObject* specifier,
            PyObject* value
        ) noexcept {
            if (value) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "functions do not support item assignment: use "
                    "`@func.overload` to register an overload instead"
                );
                return -1;
            }
            if (PyTuple_Check(specifier)) {
                Py_ssize_t len = PyTuple_GET_SIZE(specifier);
                PyObject* tuple = PyTuple_New(len + 1);
                if (tuple == nullptr) {
                    return -1;
                }
                PyTuple_SET_ITEM(tuple, 0, Py_NewRef(self->__self__));
                for (Py_ssize_t i = 0; i < len; ++i) {
                    PyTuple_SET_ITEM(
                        tuple,
                        i + 1,
                        Py_NewRef(PyTuple_GET_ITEM(specifier, i))
                    );
                }
                specifier = tuple;
            } else {
                specifier = PyTuple_Pack(2, self->__self__, specifier);
                if (specifier == nullptr) {
                    return -1;
                }
            }
            int result = PyObject_DelItem(self->__wrapped__, specifier);
            Py_DECREF(specifier);
            return result;
        }

        static int __contains__(PyFunction* self, PyObject* func) noexcept {
            PyObject* args[] = {
                self->__wrapped__,
                self->__self__,
                func
            };
            PyObject* result = PyObject_VectorcallMethod(
                ptr(impl::template_string<"_subtrie_contains">()),
                args,
                3 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                nullptr
            );
            if (result == nullptr) {
                return -1;
            }
            int contains = PyObject_IsTrue(result);
            Py_DECREF(result);
            return contains;
        }

        static PyObject* __iter__(PyFunction* self) noexcept {
            return PyObject_CallMethodOneArg(
                self->__wrapped__,
                ptr(impl::template_string<"_subtrie_iter">()),
                self->__self__
            );
        }

        static PyObject* __signature__(PyFunction* self, void*) noexcept {
            try {
                Object inspect = reinterpret_steal<Object>(PyImport_Import(
                    ptr(impl::template_string<"inspect">())
                ));
                if (inspect.is(nullptr)) {
                    return nullptr;
                }
                Object signature = PyObject_CallOneArg(
                    ptr(getattr<"signature">(inspect)),
                    self->__wrapped__
                );
                if (signature.is(nullptr)) {
                    return nullptr;
                }
                Object values = getattr<"values">(
                    getattr<"parameters">(signature)
                );
                size_t size = len(values);
                Object parameters = reinterpret_steal<Object>(
                    PyTuple_New(size - 1)
                );
                if (parameters.is(nullptr)) {
                    return nullptr;
                }
                auto it = begin(values);
                auto stop = end(values);
                ++it;
                for (size_t i = 0; it != stop; ++it, ++i) {
                    PyTuple_SET_ITEM(
                        ptr(parameters),
                        i,
                        Py_NewRef(ptr(*it))
                    );
                }
                PyObject* args[] = {nullptr, ptr(parameters)};
                Object kwnames = reinterpret_steal<Object>(
                    PyTuple_Pack(1, ptr(impl::template_string<"parameters">()))
                );
                return PyObject_Vectorcall(
                    ptr(getattr<"replace">(signature)),
                    args + 1,
                    0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    ptr(kwnames)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Default `repr()` reflects Python conventions for bound methods. */
        static PyObject* __repr__(PyFunction* self) noexcept {
            try {
                std::string str =
                    "<bound method " +
                    demangle(Py_TYPE(self->__self__)->tp_name) + ".";
                Py_ssize_t len;
                const char* name = PyUnicode_AsUTF8AndSize(
                    self->__wrapped__->name,
                    &len
                );
                if (name == nullptr) {
                    return nullptr;
                }
                str += std::string(name, len) + " of ";
                str += repr(reinterpret_borrow<Object>(self->__self__)) + ">";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /* A private, class-level constructor called internally by the descriptor
        protocol to avoid any superfluous argument validation when binding methods. */
        static PyObject* _capture(
            PyTypeObject* cls,
            PyObject* const* args,
            Py_ssize_t nargsf
        ) noexcept {
            PyObject* result = cls->tp_alloc(cls, 0);
            if (result == nullptr) {
                return nullptr;
            }
            try {
                new (result) PyFunction(args[0], args[1]);
            } catch (...) {
                Py_DECREF(result);
                Exception::to_python();
                return nullptr;
            }
            PyObject_GC_Track(result);
            return result;
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&impl::FuncIntersect::__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&impl::FuncUnion::__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "_capture",
                reinterpret_cast<PyCFunction>(&_capture),
                METH_CLASS | METH_FASTCALL,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__signature__",
                reinterpret_cast<getter>(&__signature__),
                nullptr,
                PyDoc_STR(
R"doc(A property that produces an accurate `inspect.Signature` object when a
C++ function is introspected from Python.

Notes
-----
Providing this descriptor allows the `inspect` module to be used on C++
functions as if they were implemented in Python itself, reflecting the signature
of their underlying `py::Function` representation.)doc"
                ),
                nullptr
            },
            {nullptr}
        };

    };

public:
    using __python__ = PyFunction<impl::Signature<F>>;

    Function(PyObject* p, borrowed_t t) : Object(p, t) {}
    Function(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Function> requires (__initializer__<T>::enable)
    Function(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

    template <typename... A> requires (implicit_ctor<Function>::template enable<A...>)
    Function(A&&... args) : Object(
        implicit_ctor<Function>{},
        std::forward<A>(args)...
    ) {}

    template <typename... A> requires (explicit_ctor<Function>::template enable<A...>)
    explicit Function(A&&... args) : Object(
        explicit_ctor<Function>{},
        std::forward<A>(args)...
    ) {}

};


/// TODO: I would also need some way to disambiguate static functions from member
/// functions when doing CTAD.  This is probably accomplished by providing an extra
/// argument to the constructor which holds the `self` value, and is implicitly
/// convertible to the function's first parameter type.  In that case, the CTAD
/// guide would always deduce to a member function over a static function.  If the
/// extra argument is given and is not convertible to the first parameter type, then
/// we issue a compile error, and if the extra argument is not given at all, then we
/// interpret it as a static function.
/// -> This can be done by specializing the CTAD guides such that if exactly one
/// partial argument is given, the function type deduces to a member function?
/// -> Actually, with the partial binding apparatus, it *might* be possible to
/// eliminate member function types entirely, although I'm not sure if that's
/// appropriate everywhere.  If possible, though, then it would cut down on the
/// number of types I need to generate, bring the template signature into line with
/// `std::function`, and potentially simplify the implementation.

/// TODO: alternatively, I could generalize the member function syntax to account for
/// all pre-bound partial arguments.  So:

/*
    Function<Int(*)(Arg<"x", Int>, Arg<"y", Int>::opt)> func(
        "subtract",
        "a static function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        }
    );

    Function<Int(Int::*)(Arg<"x", Int>, Arg<"y", Int>::opt)> func(
        "subtract",
        "a member function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        },
        1
    );

    Function<Int(pack<Int, Int>::*)(Arg<"x", Int>, Arg<"y", Int>::opt)> func(
        "subtract",
        "a simple example function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        },
        1,
        2
    );
*/

/// That ensures that no information is lost, and is fully generalizable, but it does
/// restrict conversions a bit.

/// TODO: an alternative is to use the partial mechanism to remove arguments from the
/// signature, rather than further encoding them.

/*
    Function<Int(*)(Arg<"x", Int>, Arg<"y", Int>::opt)> func(
        "subtract",
        "a static function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        }
    );

    Function<Int(*)(Arg<"y", Int>::opt)> func(
        "subtract",
        "a member function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        },
        1
    );

    Function<Int(*)()> func(
        "subtract",
        "a simple example function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        },
        1,
        2
    );
*/

/// That's probably better overall, and means the template signature always reflects
/// the actual function signature when the function is called, which is a plus.  It
/// also means I can potentially remove the function pointer type?

/// -> I can't do that because the internal `std::function` has to retain all of the
/// type information for how the function is called, so no arguments can be removed.
/// Instead, I need to go with either the first syntax or introduce a Bound<>
/// annotation that indicates that a parameter has already been bound to a given
/// argument.  Maybe that's another extension of `Arg<>`?




template <impl::inherits<impl::FunctionTag> F>
struct __template__<F> {
    using Func = std::remove_reference_t<F>;

    /* Functions use a special template syntax in Python to reflect C++ signatures as
     * symmetrically as as possible.  Here's an example:
     *
     *      Function[::int, "x": int, "y": int: ...]
     *
     * This describes a function which returns an integer and accepts two integer
     * arguments, `x` and `y`, the second of which is optional (indicated by ellipsis
     * following the type).  The first element describes the return type, as well as
     * the type of a possible `self` argument for member functions, with the following
     * syntax:
     *
     *      Function[Foo::int, "x": int, "y": int: ...]
     *
     * This describes the same function as before, but bound to class `Foo` as an
     * instance method.  Class methods are described by binding to `type[Foo]` instead,
     * and static methods use the same syntax as regular functions.  If the return
     * type is void, it can be replaced with `None`, which is the default for an empty
     * slice:
     *
     *      Function[::, "name": str]
     *
     * It is also possible to omit an argument name, in which case the argument will
     * be anonymous and positional-only:
     *
     *      Function[::int, int, int: ...]
     *
     * Trailing `...` syntax can still be used to mark an optional positional-only
     * argument.  Alternatively, a `"/"` delimiter can be used according to Python
     * syntax, in order to explicitly name positional-only arguments:
     *
     *      Function[::int, "x": int, "/", "y": int: ...]
     *
     * In this case, the `x` argument is positional-only, while `y` can be passed as
     * either a positional or keyword argument.  A `"*"` delimiter can be used to
     * separate positional-or-keyword arguments from keyword-only arguments:
     *
     *      Function[::int, "x": int, "*", "y": int: ...]
     *
     * Lastly, prepending `*` or `**` to an argument name will mark it as a variadic
     * positional or keyword argument, respectively:
     *
     *      Function[::int, "*args": int, "**kwargs": str]
     *
     * Such arguments cannot have default values.
     */

    template <size_t I, size_t PosOnly, size_t KwOnly>
    static void populate(PyObject* tuple, size_t& offset) {
        using T = Func::template at<I>;
        Type<typename impl::ArgTraits<T>::type> type;

        /// NOTE: `/` and `*` argument delimiters must be inserted where necessary to
        /// model positional-only and keyword-only arguments correctly in Python.
        if constexpr (
            (I == PosOnly) ||
            ((I == Func::n - 1) && impl::ArgTraits<T>::posonly())
        ) {
            PyObject* str = PyUnicode_FromStringAndSize("/", 1);
            if (str == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, str);
            ++offset;

        } else if constexpr (I == KwOnly) {
            PyObject* str = PyUnicode_FromStringAndSize("*", 1);
            if (str == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, str);
            ++offset;
        }

        if constexpr (impl::ArgTraits<T>::posonly()) {
            if constexpr (impl::ArgTraits<T>::name.empty()) {
                if constexpr (impl::ArgTraits<T>::opt()) {
                    PyObject* slice = PySlice_New(
                        Type<typename impl::ArgTraits<T>::type>(),
                        Py_Ellipsis,
                        Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                } else {
                    PyTuple_SET_ITEM(tuple, I + offset, ptr(type));
                }
            } else {
                Object name = reinterpret_steal<Object>(
                    PyUnicode_FromStringAndSize(
                        impl::ArgTraits<T>::name,
                        impl::ArgTraits<T>::name.size()
                    )
                );
                if (name.is(nullptr)) {
                    Exception::from_python();
                }
                if constexpr (impl::ArgTraits<T>::opt()) {
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        ptr(type),
                        Py_Ellipsis
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                } else {
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        ptr(type),
                        Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                }
            }

        } else if constexpr (impl::ArgTraits<T>::kw()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size()
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                impl::ArgTraits<T>::opt() ? Py_Ellipsis : Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else if constexpr (impl::ArgTraits<T>::args()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    "*" + impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size() + 1
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else if constexpr (impl::ArgTraits<T>::kwargs()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    "**" + impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size() + 2
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else {
            static_assert(false, "unrecognized argument kind");
        }
    }

    static Object operator()() {
        Object result = reinterpret_steal<Object>(
            PyTuple_New(Func::n + 1 + Func::has_posonly + Func::has_kwonly)
        );
        if (result.is(nullptr)) {
            Exception::from_python();
        }

        Object rtype = std::is_void_v<typename Func::Return> ?
            Object(None) :
            Object(Type<typename impl::ArgTraits<typename Func::Return>::type>());
        if constexpr (Func::has_self) {
            Object slice = reinterpret_steal<Object>(PySlice_New(
                Type<typename impl::ArgTraits<typename Func::Self>::type>(),
                Py_None,
                ptr(rtype)
            ));
            if (slice.is(nullptr)) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(result), 0, release(slice));
        } else {
            Object slice = reinterpret_steal<Object>(PySlice_New(
                Py_None,
                Py_None,
                ptr(rtype)
            ));
            if (slice.is(nullptr)) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(result), 0, release(slice));
        }

        constexpr size_t transition = Func::has_posonly ? 
            std::min({Func::args_idx, Func::kw_idx, Func::kwargs_idx}) :
            Func::n;

        []<size_t... Is>(
            std::index_sequence<Is...>,
            PyObject* list
        ) {
            size_t offset = 1;
            (populate<Is, transition, Func::kwonly_idx>(list, offset), ...);
        }(std::make_index_sequence<Func::n>{}, ptr(result));
        return result;
    }
};







template <typename Return, typename... Target, typename Func, typename... Values>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            "",
            "",
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};


template <
    std::convertible_to<std::string> Name,
    typename Return,
    typename... Target,
    typename Func,
    typename... Values
>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Name, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Name&& name, Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            std::forward(name),
            "",
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};


template <
    std::convertible_to<std::string> Name,
    std::convertible_to<std::string> Doc,
    typename Return,
    typename... Target,
    typename Func,
    typename... Values
>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Name, Doc, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Name&& name, Doc&& doc, Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            std::forward(name),
            std::forward<Doc>(doc),
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};





/// TODO: class methods can be indicated by a member method of Type<T>.  That
/// would allow this mechanism to scale arbitrarily.


// TODO: constructor should fail if the function type is a subclass of my root
// function type, but not a subclass of this specific function type.  This
// indicates a type mismatch in the function signature, which is a violation of
// static type safety.  I can then print a helpful error message with the demangled
// function types which can show their differences.
// -> This can be implemented in the actual call operator itself, but it would be
// better to implement it on the constructor side.  Perhaps including it in the
// isinstance()/issubclass() checks would be sufficient, since those are used
// during implicit conversion anyways.


/// TODO: all of these should be moved to their respective methods:
/// -   assert_matches() is needed in isinstance() + issubclass() to ensure
///     strict type safety.  Maybe also in the constructor, which can be
///     avoided using CTAD.
/// -   assert_satisfies() is needed in .overload()


struct TODO2 {

    template <size_t I, typename Container>
    static bool _matches(const Params<Container>& key) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;
        if (I < key.size()) {
            const Param& param = key[I];
            if constexpr (ArgTraits<at<I>>::kwonly()) {
                return (
                    (param.kwonly() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::kw()) {
                return (
                    (param.kw() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::pos()) {
                return (
                    (param.posonly() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::args()) {
                return (
                    param.args() &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::kwargs()) {
                return (
                    param.kwargs() &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else {
                static_assert(false, "unrecognized parameter kind");
            }
        }
        return false;
    }

    template <size_t I, typename Container>
    static void _assert_matches(const Params<Container>& key) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        constexpr auto description = [](const Param& param) {
            if (param.kwonly()) {
                return "keyword-only";
            } else if (param.kw()) {
                return "positional-or-keyword";
            } else if (param.pos()) {
                return "positional";
            } else if (param.args()) {
                return "variadic positional";
            } else if (param.kwargs()) {
                return "variadic keyword";
            } else {
                return "<unknown>";
            }
        };

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing keyword-only argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + std::to_string(I) 
                );
            }
            const Param& param = key[I];
            if (!param.kwonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be keyword-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' at index " + std::to_string(I) + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                        "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                        "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing positional-or-keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
                );
            }
            const Param& param = key[I];
            if (!param.kw()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-or-keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected positional-or-keyword argument '" +
                        ArgTraits<at<I>>::name + "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected positional-or-keyword argument '" +
                        ArgTraits<at<I>>::name + "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing positional-only argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
                );
            }
            const Param& param = key[I];
            if (!param.posonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-only argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected positional-only argument '" +
                        ArgTraits<at<I>>::name + "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected positional-only argument '" +
                        ArgTraits<at<I>>::name + "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected positional-only argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing variadic positional argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
                );
            }
            const Param& param = key[I];
            if (!param.args()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be variadic positional, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected variadic positional argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected variadic positional argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing variadic keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
                );
            }
            const Param& param = key[I];
            if (!param.kwargs()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be variadic keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected variadic keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected variadic keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else {
            static_assert(false, "unrecognized parameter type");
        }
    }

    template <size_t I, typename... Ts>
    static constexpr bool _satisfies() { return true; };
    template <size_t I, typename T, typename... Ts>
    static constexpr bool _satisfies() {
        if constexpr (ArgTraits<at<I>>::kwonly()) {
            return (
                (
                    ArgTraits<T>::kwonly() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            return (
                (
                    ArgTraits<T>::kw() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            return (
                (
                    ArgTraits<T>::pos() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if constexpr ((ArgTraits<T>::pos() || ArgTraits<T>::args())) {
                if constexpr (
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            }
            return satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if constexpr (ArgTraits<T>::kw()) {
                if constexpr (
                    !has<ArgTraits<T>::name> &&
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            } else if constexpr (ArgTraits<T>::kwargs()) {
                if constexpr (
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            }
            return satisfies<I + 1, Ts...>;

        } else {
            static_assert(false, "unrecognized parameter type");
        }

        return false;
    }

    template <size_t I, typename Container>
    static bool _satisfies(const Params<Container>& key, size_t& idx) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        /// NOTE: if the original argument in the enclosing signature is required,
        /// then the new argument cannot be optional.  Otherwise, it can be either
        /// required or optional.

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.kwonly() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(param.value)))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.kw() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(param.value)))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.pos() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(param.value)))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->pos()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        return false;
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return true;
                    }
                    param = &key[idx];            
                }
                if (param->args()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        return false;
                    }
                    ++idx;
                    return true;
                }
            }
            return true;

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->kw()) {
                    if (
                        /// TODO: check to see if the argument is present
                        // !callback(param->name) &&
                        !issubclass<T>(reinterpret_borrow<Object>(param->value))
                    ) {
                        return false;
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return true;
                    }
                    param = &key[idx];
                }
                if (param->kwargs()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        return false;
                    }
                    ++idx;
                    return true;
                }
            }
            return true;

        } else {
            static_assert(false, "unrecognized parameter type");
        }
        return false;
    }

    template <size_t I, typename Container>
    static void _assert_satisfies(const Params<Container>& key, size_t& idx) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        constexpr auto description = [](const Param& param) {
            if (param.kwonly()) {
                return "keyword-only";
            } else if (param.kw()) {
                return "positional-or-keyword";
            } else if (param.pos()) {
                return "positional";
            } else if (param.args()) {
                return "variadic positional";
            } else if (param.kwargs()) {
                return "variadic keyword";
            } else {
                return "<unknown>";
            }
        };

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing keyword-only argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.kwonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be keyword-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' at index " + std::to_string(idx) + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(param.value))) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(Type<T>()) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing positional-or-keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.kw()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-or-keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(idx) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(param.value))) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(Type<T>()) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing positional argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.pos()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional argument '" + ArgTraits<at<I>>::name +
                    "' at index " + std::to_string(idx) + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required positional argument '" + ArgTraits<at<I>>::name +
                    "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(param.value))) {
                throw TypeError(
                    "expected positional argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(Type<T>()) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->pos() && idx < key.size()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        throw TypeError(
                            "expected positional argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" +
                            repr(reinterpret_borrow<Object>(param->value)) + "'"
                        );
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return;
                    }
                    param = &key[idx];
                }
                if (param->args()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        throw TypeError(
                            "expected variadic positional argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" +
                            repr(reinterpret_borrow<Object>(param->value)) + "'"
                        );
                    }
                    ++idx;
                }
            }

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->kw() && idx < key.size()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        throw TypeError(
                            "expected keyword argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" +
                            repr(reinterpret_borrow<Object>(param->value)) + "'"
                        );
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return;
                    }
                    param = &key[idx];
                }
                if (param->kwargs()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        throw TypeError(
                            "expected variadic keyword argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" +
                            repr(reinterpret_borrow<Object>(param->value)) + "'"
                        );
                    }
                    ++idx;
                }
            }

        } else {
            static_assert(false, "unrecognized parameter type");
        }
    }

    /* Check to see if a compile-time function signature exactly matches the
    enclosing parameter list. */
    template <typename... Params>
    static constexpr bool matches() {
        return (std::same_as<Params, Args> && ...);
    }

    /* Check to see if a dynamic function signature exactly matches the enclosing
    parameter list. */
    template <typename Container>
    static bool matches(const Params<Container>& key) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key
        ) {
            return key.size() == n && (_matches<Is>(key) && ...);
        }(std::make_index_sequence<n>{}, key);
    }

    /* Validate a dynamic function signature, raising an error if it does not
    exactly match the enclosing parameter list. */
    template <typename Container>
    static void assert_matches(const Params<Container>& key) {
        []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key
        ) {
            if (key.size() != n) {
                throw TypeError(
                    "expected " + std::to_string(n) + " arguments, got " +
                    std::to_string(key.size())
                );
            }
            (_assert_matches<Is>(key), ...);
        }(std::make_index_sequence<n>{}, key);
    }

    /* Check to see if a compile-time function signature can be bound to the
    enclosing parameter list, meaning that it could be registered as a viable
    overload. */
    template <typename... Params>
    static constexpr bool satisfies() {
        return _satisfies<0, Params...>();
    }

    /* Check to see if a dynamic function signature can be bound to the enclosing
    parameter list, meaning that it could be registered as a viable overload. */
    template <typename Container>
    static bool satisfies(const Params<Container>& key) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key,
            size_t idx
        ) {
            return key.size() == n && (_satisfies<Is>(key, idx) && ...);
        }(std::make_index_sequence<n>{}, key, 0);
    }

    /* Validate a Python function signature, raising an error if it cannot be
    bound to the enclosing parameter list. */
    template <typename Container>
    static void assert_satisfies(const Params<Container>& key) {
        []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key,
            size_t idx
        ) {
            if (key.size() != n) {
                throw TypeError(
                    "expected " + std::to_string(n) + " arguments, got " +
                    std::to_string(key.size())
                );
            }
            (_assert_satisfies<Is>(key, idx), ...);
        }(std::make_index_sequence<n>{}, key, 0);
    }

};




template <typename T, typename R, typename... A>
struct __isinstance__<T, Function<R(A...)>>                 : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if (impl::cpp_like<T>) {
            return issubclass<T, Function<R(A...)>>();

        } else if constexpr (issubclass<T, Function<R(A...)>>()) {
            return ptr(obj) != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            return ptr(obj) != nullptr && (
                PyFunction_Check(ptr(obj)) ||
                PyMethod_Check(ptr(obj)) ||
                PyCFunction_Check(ptr(obj))
            );
        } else {
            return false;
        }
    }
};


// TODO: if default specialization is given, type checks should be fully generic, right?
// issubclass<T, Function<>>() should check impl::is_callable_any<T>;

    // template <typename T>
    // concept is_callable_any = 
    //     std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> ||
    //     std::is_member_function_pointer_v<std::decay_t<T>> ||
    //     has_call_operator<T>;


template <typename T, typename R, typename... A>
struct __issubclass__<T, Function<R(A...)>>                 : Returns<bool> {
    static constexpr bool operator()() {
        return std::is_invocable_r_v<R, T, A...>;
    }
    static constexpr bool operator()(const T&) {
        // TODO: this is going to have to be radically rethought.
        // Maybe I just forward to an issubclass() check against the type object?
        // In fact, this could maybe be standard operating procedure for all types.
        // 
        return PyType_IsSubtype(
            reinterpret_cast<PyTypeObject*>(ptr(Type<T>())),
            reinterpret_cast<PyTypeObject*>(ptr(Type<Function<R(A...)>>()))
        );
    }
};


/* Call the function with the given arguments.  If the wrapped function is of the
coupled Python type, then this will be translated into a raw C++ call, bypassing
Python entirely. */
template <impl::inherits<impl::FunctionTag> Self, typename... Args>
    requires (std::remove_reference_t<Self>::bind<Args...>)
struct __call__<Self, Args...> : Returns<typename std::remove_reference_t<Self>::Return> {
    using Func = std::remove_reference_t<Self>;
    static Func::Return operator()(Self&& self, Args&&... args) {
        if (!self->overloads.data.empty()) {
            /// TODO: generate an overload key from the C++ arguments
            /// -> This can be implemented in Arguments<...>::Bind<...>::key()
            PyObject* overload = self->overloads.search(/* overload key */);
            if (overload) {
                return Func::call(overload, std::forward<Args>(args)...);
            }
        }
        return Func::call(self->defaults, self->func, std::forward<Args>(args)...);
    }
};


/// TODO: __getitem__, __contains__, __iter__, __len__, __bool__


template <typename F>
template <typename Self, typename Func>
    requires (
        !std::is_const_v<std::remove_reference_t<Self>> &&
        compatible<Func>
    )
void Interface<Function<F>>::overload(this Self&& self, const Function<Func>& func) {
    /// TODO: C++ side of function overloading
}


template <typename F>
template <typename T>
void Interface<Function<F>>::method(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::classmethod(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::staticmethod(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::property(
    this const auto& self,
    Type<T>& type,
    /* setter */,
    /* deleter */
) {
    /// TODO: C++ side of method binding
}


namespace impl {

    /* A convenience function that calls a named method of a Python object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <StaticStr Name, typename Self, typename... Args>
        requires (
            __getattr__<std::decay_t<Self>, Name>::enable &&
            std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
            __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>
        )
    decltype(auto) call_method(Self&& self, Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, Name>::type;
        Object meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(self),
            ptr(template_string<Name>())
        ));
        if (meth.is(nullptr)) {
            Exception::from_python();
        }
        try {
            return Func::template invoke<typename Func::ReturnType>(
                meth,
                std::forward<Args>(args)...
            );
        } catch (...) {
            throw;
        }
    }

    /* A convenience function that calls a named method of a Python type object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <typename Self, StaticStr Name, typename... Args>
        requires (
            __getattr__<std::decay_t<Self>, Name>::enable &&
            std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
            __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>
        )
    decltype(auto) call_static(Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, Name>::type;
        Object meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(Self::type),
            ptr(template_string<Name>())
        ));
        if (meth.is(nullptr)) {
            Exception::from_python();
        }
        try {
            return Func::template invoke<typename Func::ReturnType>(
                meth,
                std::forward<Args>(args)...
            );
        } catch (...) {
            throw;
        }
    }

    /// NOTE: the type returned by `std::mem_fn()` is implementation-defined, so we
    /// have to do some template magic to trick the compiler into deducing the correct
    /// type during template specializations.

    template <typename T>
    struct respecialize { static constexpr bool enable = false; };
    template <template <typename...> typename T, typename... Ts>
    struct respecialize<T<Ts...>> {
        static constexpr bool enable = true;
        template <typename... New>
        using type = T<New...>;
    };
    template <typename Sig>
    using std_mem_fn_type = respecialize<
        decltype(std::mem_fn(std::declval<void(Object::*)()>()))
    >::template type<Sig>;

};


#define NON_MEMBER_FUNC(IN, OUT) \
    template <typename R, typename... A> \
    struct __cast__<IN> : Returns<Function<OUT>> {};

#define MEMBER_FUNC(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __cast__<IN> : Returns<Function<OUT>> {};

#define STD_MEM_FN(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __cast__<impl::std_mem_fn_type<IN>> : Returns<Function<OUT>> {};


NON_MEMBER_FUNC(R(A...), R(*)(A...))
NON_MEMBER_FUNC(R(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>&&, R(*)(A...))
MEMBER_FUNC(R(C::*)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)


#undef NON_MEMBER_FUNC
#undef MEMBER_FUNC
#undef STD_MEM_FN


}  // namespace py


#endif
