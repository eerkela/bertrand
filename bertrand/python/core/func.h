#ifndef BERTRAND_PYTHON_CORE_FUNC_H
#define BERTRAND_PYTHON_CORE_FUNC_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"


namespace py {


/////////////////////////
////    ARGUMENTS    ////
/////////////////////////


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a py::Function.  Uses aggregate initialization to extend the lifetime of
temporaries. */
template <StaticStr Name, typename T>
struct Arg : impl::ArgTag {
private:

    template <bool positional, bool keyword>
    struct Optional : impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_pos = positional;
        static constexpr bool is_kw = keyword;
        static constexpr bool is_opt = true;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = false;

        T value;
    };

    template <bool optional>
    struct Positional : impl::ArgTag {
        using type = T;
        using opt = Optional<true, false>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_pos = true;
        static constexpr bool is_kw = false;
        static constexpr bool is_opt = optional;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = false;

        T value;
    };

    template <bool optional>
    struct Keyword : impl::ArgTag {
        using type = T;
        using opt = Optional<false, true>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_pos = false;
        static constexpr bool is_kw = true;
        static constexpr bool is_opt = optional;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = false;

        T value;
    };

    struct Args : impl::ArgTag {
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
        static constexpr bool is_pos = false;
        static constexpr bool is_kw = false;
        static constexpr bool is_opt = false;
        static constexpr bool is_args = true;
        static constexpr bool is_kwargs = false;

        std::vector<type> value;
    };

    struct Kwargs : impl::ArgTag {
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
        static constexpr bool is_pos = false;
        static constexpr bool is_kw = false;
        static constexpr bool is_opt = false;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = true;

        std::unordered_map<std::string, T> value;
    };

public:
    static_assert(Name != "", "Argument must have a name.");

    using type = T;
    using pos = Positional<false>;
    using kw = Keyword<false>;
    using opt = Optional<true, true>;
    using args = Args;
    using kwargs = Kwargs;

    static constexpr StaticStr name = Name;
    static constexpr bool is_pos = true;
    static constexpr bool is_kw = true;
    static constexpr bool is_opt = false;
    static constexpr bool is_args = false;
    static constexpr bool is_kwargs = false;

    T value;
};


namespace impl {

    template <StaticStr name>
    struct UnboundArg : BertrandTag {
        template <typename T>
        constexpr auto operator=(T&& value) const {
            return Arg<name, T>{std::forward<T>(value)};
        }
    };

    /* Represents a keyword parameter pack obtained by double-dereferencing a Python
    object. */
    template <impl::python_like T> requires (impl::mapping_like<T>)
    struct KwargPack : BertrandTag {
        using key_type = T::key_type;
        using mapped_type = T::mapped_type;

        T value;

    private:

        static constexpr bool can_iterate =
            impl::yields_pairs_with<T, key_type, mapped_type> ||
            impl::has_items<T> ||
            (impl::has_keys<T> && impl::has_values<T>) ||
            (impl::yields<T, key_type> && impl::lookup_yields<T, mapped_type, key_type>) ||
            (impl::has_keys<T> && impl::lookup_yields<T, mapped_type, key_type>);

        auto transform() const {
            if constexpr (impl::yields_pairs_with<T, key_type, mapped_type>) {
                return value;

            } else if constexpr (impl::has_items<T>) {
                return value.items();

            } else if constexpr (impl::has_keys<T> && impl::has_values<T>) {
                return std::ranges::views::zip(value.keys(), value.values());

            } else if constexpr (
                impl::yields<T, key_type> && impl::lookup_yields<T, mapped_type, key_type>
            ) {
                return std::ranges::views::transform(
                    value,
                    [&](const key_type& key) {
                        return std::make_pair(key, value[key]);
                    }
                );

            } else {
                return std::ranges::views::transform(
                    value.keys(),
                    [&](const key_type& key) {
                        return std::make_pair(key, value[key]);
                    }
                );
            }
        }

    public:

        template <typename U = T> requires (can_iterate)
        auto begin() const { return std::ranges::begin(transform()); }
        template <typename U = T> requires (can_iterate)
        auto cbegin() const { return begin(); }
        template <typename U = T> requires (can_iterate)
        auto end() const { return std::ranges::end(transform()); }
        template <typename U = T> requires (can_iterate)
        auto cend() const { return end(); }

    };

    /* Represents a positional parameter pack obtained by dereferencing a Python
    object. */
    template <impl::python_like T> requires (impl::iterable<T>)
    struct ArgPack : BertrandTag {
        T value;

        auto begin() const { return std::ranges::begin(value); }
        auto cbegin() const { return begin(); }
        auto end() const { return std::ranges::end(value); }
        auto cend() const { return end(); }

        template <typename U = T> requires (impl::mapping_like<U>)
        auto operator*() const {
            return KwargPack<T>{std::forward<T>(value)};
        }

    };

}


/* A compile-time factory for binding keyword arguments with Python syntax.  constexpr
instances of this class can be used to provide an even more Pythonic syntax:

    constexpr auto x = py::arg<"x">;
    my_func(x = 42);
*/
template <StaticStr name>
constexpr impl::UnboundArg<name> arg {};


/* Dereference operator is used to emulate Python container unpacking. */
template <impl::python_like Self> requires (impl::iterable<Self>)
[[nodiscard]] auto operator*(Self&& self) -> impl::ArgPack<Self> {
    return {std::forward<Self>(self)};
}


////////////////////////
////    FUNCTION    ////
////////////////////////


namespace impl {

    /* Inspect an unannotated C++ argument in a py::Function. */
    template <typename T>
    struct Param : BertrandTag {
        using type                          = T;
        static constexpr StaticStr name     = "";
        static constexpr bool opt           = false;
        static constexpr bool pos           = true;
        static constexpr bool posonly       = true;
        static constexpr bool kw            = false;
        static constexpr bool kwonly        = false;
        static constexpr bool args          = false;
        static constexpr bool kwargs        = false;
        using no_opt                        = T;
    };

    /* Inspect an argument annotation in a py::Function. */
    template <inherits<ArgTag> T>
    struct Param<T> : BertrandTag {
        using U = std::remove_reference_t<T>;
        using type                          = U::type;
        static constexpr StaticStr name     = U::name;
        static constexpr bool opt           = U::is_opt;
        static constexpr bool pos           = U::is_pos;
        static constexpr bool posonly       = U::is_pos && !U::is_kw;
        static constexpr bool kw            = U::is_kw;
        static constexpr bool kwonly        = U::is_kw && !U::is_pos;
        static constexpr bool args          = U::is_args;
        static constexpr bool kwargs        = U::is_kwargs;
        /// TODO: allow an alias to remove the optional qualifier from the argument
        /// annotation
        using no_opt                        = void;  // TODO: do some stuff
    };

    /* Analyze an annotated function signature by inspecting each argument and
    extracting metadata that allows call signatures to be resolved at compile time. */
    template <typename... Args>
    struct Parameters : BertrandTag {
    private:

        template <size_t I, typename... Ts>
        static constexpr size_t _n_pos = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_pos<I, T, Ts...> =
            _n_pos<I + Param<T>::pos, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t _n_posonly = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_posonly<I, T, Ts...> =
            _n_posonly<I + Param<T>::posonly, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t _n_kw = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_kw<I, T, Ts...> =
            _n_kw<I + Param<T>::kw, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t _n_kwonly = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_kwonly<I, T, Ts...> =
            _n_kwonly<I + Param<T>::kwonly, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t _n_opt = I;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_opt<I, T, Ts...> =
            _n_opt<I + Param<T>::opt, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t _n_opt_pos = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_opt_pos<I, T, Ts...> =
            _n_opt_pos<I + (Param<T>::pos && Param<T>::opt), Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t _n_opt_posonly = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_opt_posonly<I, T, Ts...> =
            _n_opt_posonly<I + (Param<T>::posonly && Param<T>::opt), Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t _n_opt_kw = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_opt_kw<I, T, Ts...> =
            _n_opt_kw<I + (Param<T>::kw && Param<T>::opt), Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t _n_opt_kwonly = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t _n_opt_kwonly<I, T, Ts...> =
            _n_opt_kwonly<I + (Param<T>::kwonly && Param<T>::opt), Ts...>;

        template <StaticStr Name, typename... Ts>
        static constexpr size_t _idx = 0;
        template <StaticStr Name, typename T, typename... Ts>
        static constexpr size_t _idx<Name, T, Ts...> =
            (Param<T>::name == Name) ? 0 : 1 + _idx<Name, Ts...>;

        template <typename... Ts>
        static constexpr size_t _kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kw_idx<T, Ts...> =
            Param<T>::kw ? 0 : 1 + _kw_idx<Ts...>;

        template <typename... Ts>
        static constexpr size_t _kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwonly_idx<T, Ts...> =
            Param<T>::kwonly ? 0 : 1 + _kwonly_idx<Ts...>;

        template <typename... Ts>
        static constexpr size_t _opt_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_idx<T, Ts...> =
            Param<T>::opt ? 0 : 1 + _opt_idx<Ts...>;

        template <typename... Ts>
        static constexpr size_t _opt_pos_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_pos_idx<T, Ts...> =
            Param<T>::pos && Param<T>::opt ? 0 : 1 + _opt_pos_idx<Ts...>;

        template <typename... Ts>
        static constexpr size_t _opt_posonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_posonly_idx<T, Ts...> =
            Param<T>::posonly && Param<T>::opt ? 0 : 1 + _opt_posonly_idx<Ts...>;

        template <typename... Ts>
        static constexpr size_t _opt_kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_kw_idx<T, Ts...> =
            Param<T>::kw && Param<T>::opt ? 0 : 1 + _opt_kw_idx<Ts...>;

        template <typename... Ts>
        static constexpr size_t _opt_kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_kwonly_idx<T, Ts...> =
            Param<T>::kwonly && Param<T>::opt ? 0 : 1 + _opt_kwonly_idx<Ts...>;

        template <typename... Ts>
        static constexpr size_t _args_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _args_idx<T, Ts...> =
            Param<T>::args ? 0 : 1 + _args_idx<Ts...>;

        template <typename... Ts>
        static constexpr size_t _kwargs_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwargs_idx<T, Ts...> =
            Param<T>::kwargs ? 0 : 1 + _kwargs_idx<Ts...>;

    public:
        static constexpr size_t n                   = sizeof...(Args);
        static constexpr size_t n_pos               = _n_pos<0, Args...>;
        static constexpr size_t n_posonly           = _n_posonly<0, Args...>;
        static constexpr size_t n_kw                = _n_kw<0, Args...>;
        static constexpr size_t n_kwonly            = _n_kwonly<0, Args...>;
        static constexpr size_t n_opt               = _n_opt<0, Args...>;
        static constexpr size_t n_opt_pos           = _n_opt_pos<0, Args...>;
        static constexpr size_t n_opt_posonly       = _n_opt_posonly<0, Args...>;
        static constexpr size_t n_opt_kw            = _n_opt_kw<0, Args...>;
        static constexpr size_t n_opt_kwonly        = _n_opt_kwonly<0, Args...>;

        template <StaticStr Name>
        static constexpr bool has                   = _idx<Name, Args...> != n;
        static constexpr bool has_pos               = n_pos > 0;
        static constexpr bool has_posonly           = n_posonly > 0;
        static constexpr bool has_kw                = n_kw > 0;
        static constexpr bool has_kwonly            = n_kwonly > 0;
        static constexpr bool has_opt               = n_opt > 0;
        static constexpr bool has_opt_pos           = n_opt_pos > 0;
        static constexpr bool has_opt_posonly       = n_opt_posonly > 0;
        static constexpr bool has_opt_kw            = n_opt_kw > 0;
        static constexpr bool has_opt_kwonly        = n_opt_kwonly > 0;
        static constexpr bool has_args              = _args_idx<Args...> != n;
        static constexpr bool has_kwargs            = _kwargs_idx<Args...> != n;

        template <StaticStr Name> requires (has<Name>)
        static constexpr size_t idx                 = _idx<Name, Args...>;
        static constexpr size_t kw_idx              = _kw_idx<Args...>;
        static constexpr size_t kwonly_idx          = _kwonly_idx<Args...>;
        static constexpr size_t opt_idx             = _opt_idx<Args...>;
        static constexpr size_t opt_pos_idx         = _opt_pos_idx<Args...>;
        static constexpr size_t opt_posonly_idx     = _opt_posonly_idx<Args...>;
        static constexpr size_t opt_kw_idx          = _opt_kw_idx<Args...>;
        static constexpr size_t opt_kwonly_idx      = _opt_kwonly_idx<Args...>;
        static constexpr size_t args_idx            = _args_idx<Args...>;
        static constexpr size_t kwargs_idx          = _kwargs_idx<Args...>;

        template <size_t I> requires (I < n)
        using Arg = impl::unpack_type<I, Args...>;

        template <StaticStr Name> requires (has<Name>)
        using Kwarg = impl::unpack_type<idx<Name>, Args...>;

    private:

        template <size_t I, typename... Ts>
        static constexpr bool validate = true;

        template <size_t I, typename T, typename... Ts>
        static constexpr bool validate<I, T, Ts...> = [] {
            if constexpr (Param<T>::pos) {
                static_assert(
                    I == idx<Param<T>::name> || Param<T>::name == "",
                    "signature must not contain multiple arguments with the same name"
                );
                static_assert(
                    I < kw_idx,
                    "positional-only arguments must precede keywords"
                );
                static_assert(
                    I < args_idx,
                    "positional-only arguments must precede variadic positional arguments"
                );
                static_assert(
                    I < kwargs_idx,
                    "positional-only arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I < opt_idx || Param<T>::opt,
                    "all arguments after the first optional argument must also be optional"
                );

            } else if constexpr (Param<T>::kw) {
                static_assert(
                    I == idx<Param<T>::name>,
                    "signature must not contain multiple arguments with the same name"
                );
                static_assert(
                    I < kwonly_idx || Param<T>::kwonly,
                    "positional-or-keyword arguments must precede keyword-only arguments"
                );
                static_assert(
                    !(Param<T>::kwonly && has_args && I < args_idx),
                    "keyword-only arguments must not precede variadic positional arguments"
                );
                static_assert(
                    I < kwargs_idx,
                    "keyword arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I < opt_idx || Param<T>::opt || Param<T>::kwonly,
                    "all arguments after the first optional argument must also be optional"
                );

            } else if constexpr (Param<T>::args) {
                static_assert(
                    I < kwargs_idx,
                    "variadic positional arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I == args_idx,
                    "signature must not contain multiple variadic positional arguments"
                );

            } else if constexpr (Param<T>::kwargs) {
                static_assert(
                    I == kwargs_idx,
                    "signature must not contain multiple variadic keyword arguments"
                );
            }

            return validate<I + 1, Ts...>;
        }();

    public:

        /* If the target signature does not conform to Python calling conventions, throw
        an informative compile error describing the problem. */
        static constexpr bool valid = validate<0, Args...>;

        /* Check whether the signature contains a keyword with the given name, where
        the name can only be known at runtime. */
        static bool contains(const char* name) {
            return ((std::strcmp(Param<Args>::name, name) == 0) || ...);
        }

        struct Defaults {
        private:

            /// TODO: Value might not want to remove the reference of the ::type, but
            /// only for the value itself, so that I can forward them correctly when
            /// calling the function.  If something must be accepted as an rvalue, but
            /// has a default value, then this would generate a copy of the value in
            /// the defaults tuple, and then move it when calling the function.  Same
            /// for temporaries that are passed by value, but not lvalues, which can
            /// simply reference the stored value.
            /// -> What about python objects as default values?  In this case, the
            /// object wrapper would be copied and moved, but that would just be a
            /// reference count on the underlying object, so it's somewhat unclear how
            /// state should be managed.  The only two ways I can see to do this are:
            ///     1.  Store the actual initializer in a getter of some kind and
            ///         re-evaluate it, converting to the expected parameter type each
            ///         time the default value is accessed.
            ///     2.  Warn about the same semantics as Python, where mutable default
            ///         values should not be used.
            /// If I can get 1. to work, then that would potentially be the best
            /// outcome, but 2. is also viable, and may be faster in some cases since
            /// more work is done upfront.

            /* The type of a single value in the defaults tuple.  The templated index
            is used to correlate the default value with its corresponding argument in
            the enclosing signature. */
            template <size_t I>
            struct Value  {
                using annotation = Parameters::template Arg<I>;
                using type = std::remove_reference_t<typename Param<annotation>::type>;
                static constexpr StaticStr name = Param<annotation>::name;
                static constexpr size_t index = I;
                type value;
            };

            /* Build a sub-signature holding only the arguments marked as optional from
            the enclosing signature.  This will be a specialization of the enclosing
            class, which is used to bind arguments to this class's constructor using
            the same semantics as the function's call operator. */
            template <typename Sig, typename... Ts>
            struct _Pars {
                using type = Sig;
            };
            template <typename... Sig, typename T, typename... Ts>
            struct _Pars<Parameters<Sig...>, T, Ts...> {
                template <typename U>
                struct helper {
                    using type = _Pars<Parameters<Sig...>, Ts...>::type;
                };
                template <typename U> requires (Param<U>::opt)
                struct helper<U> {
                    using type =_Pars<
                        Parameters<Sig..., typename Param<U>::no_opt>,
                        Ts...
                    >::type;
                };
                using type = helper<T>::type;
            };
            using Pars = _Pars<Parameters<>, Args...>::type;

            /* Build a std::tuple of Value<I> instances to hold the default values
            themselves. */
            template <size_t I, typename Tuple, typename... Ts>
            struct _Tuple { using type = Tuple; };
            template <size_t I, typename... Partial, typename T, typename... Ts>
            struct _Tuple<I, std::tuple<Partial...>, T, Ts...> {
                template <typename U>
                struct ToValue {
                    using type = _Tuple<I + 1, std::tuple<Partial...>, Ts...>::type;
                };
                template <typename U> requires (Param<U>::opt)
                struct ToValue<U> {
                    using type = _Tuple<I + 1, std::tuple<Partial..., Value<I>>, Ts...>::type;
                };
                using type = ToValue<T>::type;
            };
            using Tuple = _Tuple<0, std::tuple<>, Args...>::type;

            template <size_t I, typename... Ts>
            struct _find { static constexpr size_t value = 0; };
            template <size_t I, typename T, typename... Ts>
            struct _find<I, std::tuple<T, Ts...>> { static constexpr size_t value = 
                (I == T::index) ? 0 : 1 + _find<I, std::tuple<Ts...>>::value;
            };

        public:
            static constexpr size_t n               = Pars::n;
            static constexpr size_t n_pos           = Pars::n_pos;
            static constexpr size_t n_posonly       = Pars::n_posonly;
            static constexpr size_t n_kw            = Pars::n_kw;
            static constexpr size_t n_kwonly        = Pars::n_kwonly;

            template <StaticStr Name>
            static constexpr bool has               = Pars::template has<Name>;
            static constexpr bool has_pos           = Pars::has_pos;
            static constexpr bool has_posonly       = Pars::has_posonly;
            static constexpr bool has_kw            = Pars::has_kw;
            static constexpr bool has_kwonly        = Pars::has_kwonly;

            template <StaticStr Name> requires (has<Name>)
            static constexpr size_t idx             = Pars::template idx<Name>;
            static constexpr size_t kw_idx          = Pars::kw_idx;
            static constexpr size_t kwonly_idx      = Pars::kwonly_idx;

            template <size_t I> requires (I < n)
            using Arg = Pars::template Arg<I>;

            template <StaticStr Name> requires (has<Name>)
            using Kwarg = Pars::template Kwarg<Name>;

            template <typename... Values>
            using Bind = Pars::template Bind<Values...>;

            /* Given an index into the enclosing signature, find the corresponding index
            in the defaults tuple if that index corresponds to a default value. */
            template <size_t I> requires (Param<typename Parameters::Arg<I>>::opt)
            static constexpr size_t find = _find<I, Tuple>::value;

        private:

            template <size_t J, typename... Values>
            static constexpr decltype(auto) build_recursive(Values&&... values) {
                using source = Parameters<Values...>;
                using S = source::template Arg<J>;
                using type = Param<S>::type;
                constexpr StaticStr name = Param<S>::name;

                if constexpr (Param<S>::kw) {
                    constexpr size_t idx = source::template idx<name>;
                    return impl::unpack_arg<idx>(std::forward<Values>(values)...).value;

                } else if constexpr (Param<S>::args) {
                    /// TODO: some wierd logic here

                } else if constexpr (Param<S>::kwargs) {
                    /// TODO: same

                } else {
                    return impl::unpack_arg<J>(std::forward<Values>(values)...);
                }
            }

            template <size_t... Is, typename... Values>
            static constexpr Tuple build(std::index_sequence<Is...>, Values&&... values) {
                return {build_recursive<Is>(std::forward<Values>(values)...)...};
            }

        public:
            Tuple values;

            /* The default values' constructor takes Python-style arguments just like
            the call operator, and is only enabled if the call signature is well-formed
            and all optional arguments have been accounted for. */
            template <typename... Values> requires (Bind<Values...>::enable)
            constexpr Defaults(Values&&... values) : values(build(
                std::index_sequence_for<Values...>{},
                std::forward<Values>(values)...
            )) {}
            constexpr Defaults(const Defaults& other) = default;
            constexpr Defaults(Defaults&& other) = default;

            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed. */
            template <size_t I> requires (I < n)
            auto& get() {
                return std::get<I>(values).value;
            }

            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed. */
            template <size_t I> requires (I < n)
            const auto& get() const {
                return std::get<I>(values).value;
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional. */
            template <StaticStr Name> requires (has<Name>)
            auto& get() {
                return std::get<idx<Name>>(values).value;
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional. */
            template <StaticStr Name> requires (has<Name>)
            const auto& get() const {
                return std::get<idx<Name>>(values).value;
            }

        };

        template <typename... Values>
        struct Bind {
        private:
            using target = Parameters<Args...>;
            using source = Parameters<Values...>;

            template <size_t I>
            using Target = target::template Arg<I>;
            template <size_t J>
            using Source = source::template Arg<J>;

            /* Upon encountering a variadic positional pack in the target signature,
            recursively traverse the remaining source positional arguments and ensure that
            each is convertible to the target type. */
            template <size_t J, typename Pos>
            static constexpr bool consume_target_args = true;
            template <size_t J, typename Pos> requires (J < source::kw_idx)
            static constexpr bool consume_target_args<J, Pos> = [] {
                if constexpr (Param<Source<J>>::args) {
                    if constexpr (
                        !std::convertible_to<typename Param<Source<J>>::type, Pos>
                    ) {
                        return false;
                    }
                } else {
                    if constexpr (!std::convertible_to<Source<J>, Pos>) {
                        return false;
                    }
                }
                return consume_target_args<J + 1, Pos>;
            }();

            /* Upon encountering a variadic keyword pack in the target signature,
            recursively traverse the source keyword arguments and extract any that aren't
            present in the target signature, ensuring they are convertible to the target
            type. */
            template <size_t J, typename Kw>
            static constexpr bool consume_target_kwargs = true;
            template <size_t J, typename Kw> requires (J < source::n)
            static constexpr bool consume_target_kwargs<J, Kw> = [] {
                if constexpr (Param<Source<J>>::kwargs) {
                    if constexpr (
                        !std::convertible_to<typename Param<Source<J>>::type, Kw>
                    ) {
                        return false;
                    }
                } else {
                    if constexpr (
                        (
                            target::has_args &&
                            target::template idx<Param<Source<J>>::name> < target::args_idx
                        ) || (
                            !target::template has<Param<Source<J>>::name> &&
                            !std::convertible_to<typename Param<Source<J>>::type, Kw>
                        )
                    ) {
                        return false;
                    }
                }
                return consume_target_kwargs<J + 1, Kw>;
            }();

            /* Upon encountering a variadic positional pack in the source signature,
            recursively traverse the target positional arguments and ensure that the source
            type is convertible to each target type. */
            template <size_t I, typename Pos>
            static constexpr bool consume_source_args = true;
            template <size_t I, typename Pos>
                requires (I < target::n && I <= target::args_idx)
            static constexpr bool consume_source_args<I, Pos> = [] {
                if constexpr (Param<Target<I>>::args) {
                    if constexpr (
                        !std::convertible_to<Pos, typename Param<Target<I>>::type>
                    ) {
                        return false;
                    }
                } else {
                    if constexpr (
                        !std::convertible_to<Pos, typename Param<Target<I>>::type> ||
                        source::template has<Param<Target<I>>::name>
                    ) {
                        return false;
                    }
                }
                return consume_source_args<I + 1, Pos>;
            }();

            /* Upon encountering a variadic keyword pack in the source signature,
            recursively traverse the target keyword arguments and extract any that aren't
            present in the source signature, ensuring that the source type is convertible
            to each target type. */
            template <size_t I, typename Kw>
            static constexpr bool consume_source_kwargs = true;
            template <size_t I, typename Kw> requires (I < target::n)
            static constexpr bool consume_source_kwargs<I, Kw> = [] {
                /// TODO: does this work as expected?
                if constexpr (Param<Target<I>>::kwargs) {
                    if constexpr (
                        !std::convertible_to<Kw, typename Param<Target<I>>::type>
                    ) {
                        return false;
                    }
                } else {
                    if constexpr (
                        !std::convertible_to<Kw, typename Param<Target<I>>::type>
                    ) {
                        return false;
                    }
                }
                return consume_source_kwargs<I + 1, Kw>;
            }();

            /* Recursively check whether the source arguments conform to Python calling
            conventions (i.e. no positional arguments after a keyword, no duplicate
            keywords, etc.), fully satisfy the target signature, and are convertible to
            the expected types, after accounting for parameter packs in both signatures.

            The actual algorithm is quite complex, especially since it is evaluated at
            compile time via template recursion and must validate both signatures at
            once.  Here's a rough outline of how it works:

                1.  Generate two indices, I and J, which are used to traverse over the
                    target and source signatures, respectively.
                2.  For each I, J, inspect the target argument at index I:
                    a.  If the target argument is positional-only, check that the
                        source argument is not a keyword, otherwise check that the
                        target argument is marked as optional.
                    b.  If the target argument is positional-or-keyword, check that the
                        source argument meets the criteria for (a), or that the source
                        signature contains a matching keyword argument.
                    c.  If the target argument is keyword-only, check that the source's
                        positional arguments have been exhausted, and that the source
                        signature contains a matching keyword argument or the target
                        argument is marked as optional.
                    d.  If the target argument is variadic positional, recur until all
                        source positional arguments have been exhausted, ensuring that
                        each is convertible to the target type.
                    e.  If the target argument is variadic keyword, recur until all
                        source keyword arguments have been exhausted, ensuring that
                        each is convertible to the target type.
                3.  Then inspect the source argument at index J:
                    a.  If the source argument is positional, check that it is
                        convertible to the target type.
                    b.  If the source argument is keyword, check that the target
                        signature contains a matching keyword argument, and that the
                        source argument is convertible to the target type.
                    c.  If the source argument is variadic positional, recur until all
                        target positional arguments have been exhausted, ensuring that
                        each is convertible from the source type.
                    d.  If the source argument is variadic keyword, recur until all
                        target keyword arguments have been exhausted, ensuring that
                        each is convertible from the source type.
                4.  Advance to the next argument pair and repeat until all arguments
                    have been checked.  If there are more target arguments than source
                    arguments, then the remaining target arguments must be optional or
                    variadic.  If there are more source arguments than target
                    arguments, then we return false, as the only way to satisfy the
                    target signature is for it to include variadic arguments, which
                    would have avoided this case.

            If all of these conditions are met, then the call operator is enabled and
            the arguments can be translated by the reordering operators listed below.
            Otherwise, the call operator is disabled and the arguments are rejected in
            a way that allows LSPs to provide informative feedback to the user. */
            template <size_t I, size_t J>
            static constexpr bool enable_recursive = true;
            template <size_t I, size_t J> requires (I < target::n && J >= source::n)
            static constexpr bool enable_recursive<I, J> = [] {
                if constexpr (Param<Target<I>>::args || Param<Target<I>>::opt) {
                    return enable_recursive<I + 1, J>;
                } else if constexpr (Param<Target<I>>::kwargs) {
                    return consume_target_kwargs<
                        source::kw_idx,
                        typename Param<Target<I>>::type
                    >;
                }
                return false;
            }();
            template <size_t I, size_t J> requires (I >= target::n && J < source::n)
            static constexpr bool enable_recursive<I, J> = false;
            template <size_t I, size_t J> requires (I < target::n && J < source::n)
            static constexpr bool enable_recursive<I, J> = [] {
                // ensure target arguments are present & expand variadic parameter packs
                if constexpr (Param<Target<I>>::pos) {
                    if constexpr (
                        (J >= source::kw_idx && !Param<Target<I>>::opt) ||
                        (
                            Param<Target<I>>::name != "" &&
                            source::template has<Param<Target<I>>::name>
                        )
                    ) {
                        return false;
                    }
                } else if constexpr (Param<Target<I>>::kwonly) {
                    if constexpr (
                        J < source::kw_idx ||
                        (
                            !source::template has<Param<Target<I>>::name> &&
                            !Param<Target<I>>::opt
                        )
                    ) {
                        return false;
                    }
                } else if constexpr (Param<Target<I>>::kw) {
                    if constexpr ((
                        J < source::kw_idx &&
                        source::template has<Param<Target<I>>::name>
                    ) || (
                        J >= source::kw_idx &&
                        !source::template has<Param<Target<I>>::name> &&
                        !Param<Target<I>>::opt
                    )) {
                        return false;
                    }
                } else if constexpr (Param<Target<I>>::args) {
                    if constexpr (
                        !consume_target_args<J, typename Param<Target<I>>::type>
                    ) {
                        return false;
                    }
                    // skip to source keywords
                    return enable_recursive<I + 1, source::kw_idx>;
                } else if constexpr (Param<Target<I>>::kwargs) {
                    return consume_target_kwargs<
                        source::kw_idx,
                        typename Param<Target<I>>::type
                    >;  // end of expression
                } else {
                    return false;  // not reachable
                }

                // validate source arguments & expand unpacking operators
                if constexpr (Param<Source<J>>::pos) {
                    if constexpr (!std::convertible_to<Source<J>, Target<I>>) {
                        return false;
                    }
                } else if constexpr (Param<Source<J>>::kw) {
                    if constexpr (target::template has<Param<Source<J>>::name>) {
                        using type = Target<target::template idx<Param<Source<J>>::name>>;
                        if constexpr (!std::convertible_to<Source<J>, type>) {
                            return false;
                        }
                    } else if constexpr (!target::has_kwargs) {
                        using type = Param<Target<target::kwargs_idx>>::type;
                        if constexpr (!std::convertible_to<Source<J>, type>) {
                            return false;
                        }
                    }
                } else if constexpr (Param<Source<J>>::args) {
                    if constexpr (
                        !consume_source_args<I, typename Param<Source<J>>::type>
                    ) {
                        return false;
                    }
                    // skip to target keywords
                    return enable_recursive<target::args_idx + 1, J + 1>;
                } else if constexpr (Param<Source<J>>::kwargs) {
                    // end of expression
                    return consume_source_kwargs<I, typename Param<Source<J>>::type>;
                } else {
                    return false;  // not reachable
                }

                // advance to next argument pair
                return enable_recursive<I + 1, J + 1>;
            }();

            template <size_t I, typename T>
            static constexpr void build_kwargs(
                std::unordered_map<std::string, T>& map,
                Values&&... args
            ) {
                using Arg = Source<source::kw_idx + I>;
                if constexpr (!target::template has<Param<Arg>::name>) {
                    map.emplace(
                        Param<Arg>::name,
                        impl::unpack_arg<source::kw_index + I>(
                            std::forward<Source>(args)...
                        )
                    );
                }
            }

        public:

            /* Call operator is only enabled if source arguments are well-formed and
            match the target signature. */
            static constexpr bool enable = source::valid && enable_recursive<0, 0>;

            //////////////////////////
            ////    C++ -> C++    ////
            //////////////////////////

            /* The cpp_to_cpp() method is used to convert an index sequence over the
             * enclosing signature into the corresponding values pulled from either the
             * call site or the function's defaults.  It is complicated by the presence
             * of variadic parameter packs in both the target signature and the call
             * arguments, which have to be handled as a cross product of possible
             * combinations.
             *
             * Unless a variadic parameter pack is given at the call site, all of these
             * are resolved entirely at compile time by reordering the arguments using
             * template recursion.  However, because the size of a variadic parameter
             * pack cannot be determined at compile time, calls that use these will have
             * to extract values at runtime, and may therefore raise an error if a
             * corresponding value does not exist in the parameter pack, or if there are
             * extras that are not included in the target signature.
             */

            /// TODO: this side of things might need modifications to be able to handle
            /// the `self` parameter.

            /// TODO: I also need to think hard about references and lifetimes.  If
            /// we're pulling from the call arguments, then I should be able to just
            /// forward those directly.  Default values may need some copying/moving
            /// to yield the expected type.  Also, the containers for *args, **kwargs
            /// are not able to store references, so this gets rather complicated.
            /// Luckily for *args, **kwargs, they'll always store a homogenous type, so
            /// I can probably just use std::reference_wrapper<>.

            template <size_t I>
            static constexpr Param<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                Values&&... args
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    if constexpr (source::template has<name>) {
                        constexpr size_t idx = source::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(args)...);
                    } else {
                        return defaults.template get<I>();
                    }

                } else if constexpr (Param<T>::kw) {
                    if constexpr (I < source::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(args)...);
                    } else if constexpr (source::template has<name>) {
                        constexpr size_t idx = source::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(args)...);
                    } else {
                        return defaults.template get<I>();
                    }

                } else if constexpr (Param<T>::args) {
                    using Pack = std::vector<type>;
                    Pack vec;
                    if constexpr (I < source::kw_idx) {
                        constexpr size_t diff = source::kw_idx - I;
                        vec.reserve(diff);
                        []<size_t... Js>(
                            std::index_sequence<Js...>,
                            Pack& vec,
                            Values&&... args
                        ) {
                            (vec.push_back(
                                impl::unpack_arg<I + Js>(std::forward<Values>(args)...)
                            ), ...);
                        }(
                            std::make_index_sequence<diff>{},
                            vec,
                            std::forward<Values>(args)...
                        );
                    }
                    return vec;

                } else if constexpr (Param<T>::kwargs) {
                    using Pack = std::unordered_map<std::string, type>;
                    Pack pack;
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        Pack& pack,
                        Values&&... args
                    ) {
                        (build_kwargs<Js>(pack, std::forward<Values>(args)...), ...);
                    }(
                        std::make_index_sequence<source::n - source::kw_idx>{},
                        pack,
                        std::forward<Values>(args)...
                    );
                    return pack;

                } else {
                    if constexpr (I < source::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(args)...);
                    } else {
                        /// TODO: rvalues would need to be copied and then moved here to
                        /// conform to the correct type.
                        return defaults.template get<I>();
                    }
                }
            }

            template <size_t I, std::input_iterator Iter, std::sentinel_for<Iter> End>
            static constexpr Param<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                size_t size,
                Iter& iter,
                const End& end,
                Values&&... args
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(args)...);

                } else if constexpr (Param<T>::kw) {
                    if constexpr (I < source::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(args)...);
                    } else if constexpr (source::template has<name>) {
                        constexpr size_t idx = source::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(args)...);
                    } else {
                        if (iter == end) {
                            if constexpr (Param<T>::opt) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "could not unpack positional args - no match for "
                                    "parameter '" + name + "' at index " +
                                    std::to_string(I)
                                );
                            }
                        } else {
                            return *(iter++);
                        }
                    }

                } else if constexpr (Param<T>::args) {
                    using Pack = std::vector<type>;  /// TODO: can't store references
                    Pack vec;
                    if constexpr (I < source::args_idx) {
                        constexpr size_t diff = source::args_idx - I;
                        vec.reserve(diff + size);
                        []<size_t... Js>(
                            std::index_sequence<Js...>,
                            Pack& vec,
                            Values&&... args
                        ) {
                            (vec.push_back(
                                impl::unpack_arg<I + Js>(std::forward<Values>(args)...)
                            ), ...);
                        }(
                            std::make_index_sequence<diff>{},
                            vec,
                            std::forward<Values>(args)...
                        );
                        vec.insert(vec.end(), iter, end);
                    }
                    return vec;

                } else if constexpr (Param<T>::kwargs) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(args)...);

                } else {
                    if constexpr (I < source::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(args)...);
                    } else {
                        if (iter == end) {
                            if constexpr (Param<T>::opt) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "could not unpack positional args - no match for "
                                    "positional-only parameter at index " +
                                    std::to_string(I)
                                );
                            }
                        } else {
                            return *(iter++);
                        }
                    }
                }
            }

            template <size_t I, typename Mapping>
            static constexpr Param<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                const Mapping& map,
                Values&&... args
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    auto val = map.find(name);
                    if constexpr (source::template contains<name>) {
                        if (val != map.end()) {
                            throw TypeError(
                                "duplicate value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = source::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(args)...);
                    } else {
                        if (val != map.end()) {
                            return *val;
                        } else {
                            if constexpr (Param<T>::opt) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "could not unpack keyword args - no match for "
                                    "parameter '" + name + "' at index " +
                                    std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (Param<T>::kw) {
                    auto val = map.find(name);
                    if constexpr (I < source::kw_idx) {
                        if (val != map.end()) {
                            throw TypeError(
                                "duplicate value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(args)...);
                    } else if constexpr (source::template has<name>) {
                        if (val != map.end()) {
                            throw TypeError(
                                "duplicate value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = source::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(args)...);
                    } else {
                        if (val != map.end()) {
                            return *val;
                        } else {
                            if constexpr (Param<T>::opt) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "could not unpack keyword args - no match for "
                                    "parameter '" + name + "' at index " +
                                    std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (Param<T>::args) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(args)...);

                } else if constexpr (Param<T>::kwargs) {
                    using Pack = std::unordered_map<std::string, type>;
                    Pack pack;
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        Pack& pack,
                        Values&&... args
                    ) {
                        (build_kwargs<Js>(pack, std::forward<Values>(args)...), ...);
                    }(
                        std::make_index_sequence<source::n - source::kw_idx>{},
                        pack,
                        std::forward<Values>(args)...
                    );
                    for (const auto& [key, value] : map) {
                        if (pack.contains(key)) {
                            throw TypeError(
                                "duplicate value for parameter '" + key + "'"
                            );
                        }
                        pack[key] = value;
                    }
                    return pack;

                } else {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(args)...);
                }
            }

            template <
                size_t I,
                std::input_iterator Iter,
                std::sentinel_for<Iter> End,
                typename Mapping
            >
            static constexpr Param<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                size_t size,
                Iter& iter,
                const End& end,
                const Mapping& map,
                Values&&... args
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    return cpp_to_cpp<I>(
                        defaults,
                        map,
                        std::forward<Values>(args)...
                    );

                } else if constexpr (Param<T>::kw) {
                    auto val = map.find(name);
                    if constexpr (I < source::kw_idx) {
                        if (val != map.end()) {
                            throw TypeError(
                                "duplicate value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(args)...);
                    } else if constexpr (source::template has<name>) {
                        if (val != map.end()) {
                            throw TypeError(
                                "duplicate value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = source::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(args)...);
                    } else {
                        if (iter != end) {
                            return *(iter++);
                        } else if (val != map.end()) {
                            return *val;
                        } else {
                            if constexpr (Param<T>::opt) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "could not unpack args - no match for parameter '" +
                                    name + "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (Param<T>::args) {
                    return cpp_to_cpp<I>(
                        defaults,
                        size,
                        iter,
                        end,
                        std::forward<Values>(args)...
                    );

                } else if constexpr (Param<T>::kwargs) {
                    return cpp_to_cpp<I>(
                        defaults,
                        map,
                        std::forward<Values>(args)...
                    );

                } else {
                    return cpp_to_cpp<I>(
                        defaults,
                        size,
                        iter,
                        end,
                        std::forward<Values>(args)...
                    );
                }
            }

            /////////////////////////////
            ////    PYTHON -> C++    ////
            /////////////////////////////

            /* The py_to_cpp() method is used to convert an index sequence over the
             * enclosing signature into the corresponding values pulled from either an
             * array of Python vectorcall arguments or the function's defaults.  This
             * requires us to parse an array of PyObject* pointers with a binary layout
             * that looks something like this:
             *
             *                          ( kwnames tuple )
             *      -------------------------------------
             *      | x | p | p | p |...| k | k | k |...|
             *      -------------------------------------
             *            ^             ^
             *            |             nargs ends here
             *            *args starts here
             *
             * Where 'x' is an optional first element that can be temporarily written to
             * in order to efficiently forward the `self` argument for bound methods,
             * etc.  The presence of this argument is determined by the
             * PY_VECTORCALL_ARGUMENTS_OFFSET flag, which is encoded in nargs.  You can
             * check for its presence by bitwise AND-ing against nargs, and the true
             * number of arguments must be extracted using `PyVectorcall_NARGS(nargs)`
             * to account for this.
             *
             * If PY_VECTORCALL_ARGUMENTS_OFFSET is set and 'x' is written to, then it must
             * always be reset to its original value before the function returns.  This
             * allows for nested forwarding/scoping using the same argument list, with no
             * extra allocations.
             */

            template <size_t I>
            static Param<Target<I>>::type py_to_cpp(
                const Defaults& defaults,
                PyObject* const* args,
                size_t nargsf,
                PyObject* kwnames,
                size_t kwcount
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;
                bool has_self = nargsf & PY_VECTORCALL_ARGUMENTS_OFFSET;
                size_t nargs = PyVectorcall_NARGS(nargsf);

                if constexpr (Param<T>::kwonly) {
                    if (kwnames != nullptr) {
                        for (size_t i = 0; i < kwcount; ++i) {
                            const char* kwname = PyUnicode_AsUTF8(
                                PyTuple_GET_ITEM(kwnames, i)
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (std::strcmp(kwname, name) == 0) {
                                return reinterpret_borrow<Object>(args[i]);
                            }
                        }
                    }
                    if constexpr (Param<T>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "missing required keyword-only argument '" + name + "'"
                        );
                    }

                } else if constexpr (Param<T>::kw) {
                    if (I < nargs) {
                        return reinterpret_borrow<Object>(args[I]);
                    } else if (kwnames != nullptr) {
                        for (size_t i = 0; i < kwcount; ++i) {
                            const char* kwname = PyUnicode_AsUTF8(
                                PyTuple_GET_ITEM(kwnames, i)
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (std::strcmp(kwname, name) == 0) {
                                return reinterpret_borrow<Object>(args[nargs + i]);
                            }
                        }
                    }
                    if constexpr (Param<T>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "missing required argument '" + name + "' at index " +
                            std::to_string(I)
                        );
                    }

                } else if constexpr (Param<T>::args) {
                    std::vector<type> vec;
                    for (size_t i = I; i < nargs; ++i) {
                        vec.push_back(reinterpret_borrow<Object>(args[i]));
                    }
                    return vec;

                } else if constexpr (Param<T>::kwargs) {
                    std::unordered_map<std::string, type> map;
                    if (kwnames != nullptr) {
                        auto sequence = std::make_index_sequence<target::n_kw>{};
                        for (size_t i = 0; i < kwcount; ++i) {
                            Py_ssize_t length;
                            const char* kwname = PyUnicode_AsUTF8AndSize(
                                PyTuple_GET_ITEM(kwnames, i),
                                &length
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (!target::contains(sequence)) {
                                map.emplace(
                                    std::string(kwname, length),
                                    reinterpret_borrow<Object>(args[nargs + i])
                                );
                            }
                        }
                    }
                    return map;

                } else {
                    if (I < nargs) {
                        return reinterpret_borrow<Object>(args[I]);
                    }
                    if constexpr (Param<T>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "missing required positional-only argument at index " +
                            std::to_string(I)
                        );
                    }
                }
            }

            /////////////////////////////
            ////    C++ -> PYTHON    ////
            /////////////////////////////

            /* The cpp_to_py() method is used to allocate a Python vectorcall argument
             * array and populate it according to a C++ argument list.  This is
             * essentially the inverse of the py_to_cpp() method, and requires us to
             * allocate an array with the same binary layout as described above.  That
             * array can then be used to efficiently call a Python function using the
             * vectorcall protocol, which is the fastest possible way to call a Python
             * function from C++.
             */

            template <size_t J>
            static void cpp_to_py(
                PyObject* kwnames,
                PyObject** args,
                Values&&... values
            ) {
                using S = Source<J>;
                using type = Param<S>::type;
                constexpr StaticStr name = Param<S>::name;

                if constexpr (Param<S>::kw) {
                    try {
                        PyTuple_SET_ITEM(
                            kwnames,
                            J - source::kw_idx,
                            Py_NewRef(impl::TemplateString<name>::ptr)
                        );
                        args[J + 1] = release(as_object(
                            impl::unpack_arg<J>(std::forward<Values>(values)...)
                        ));
                    } catch (...) {
                        for (size_t i = 1; i <= J; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else if constexpr (Param<S>::args) {
                    size_t curr = J + 1;
                    try {
                        const auto& var_args = impl::unpack_arg<J>(
                            std::forward<Values>(values)...
                        );
                        for (const auto& value : var_args) {
                            args[curr] = release(as_object(value));
                            ++curr;
                        }
                    } catch (...) {
                        for (size_t i = 1; i < curr; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else if constexpr (Param<S>::kwargs) {
                    size_t curr = J + 1;
                    try {
                        const auto& var_kwargs = impl::unpack_arg<J>(
                            std::forward<Values>(values)...
                        );
                        for (const auto& [key, value] : var_kwargs) {
                            PyObject* name = PyUnicode_FromStringAndSize(
                                key.data(),
                                key.size()
                            );
                            if (name == nullptr) {
                                Exception::from_python();
                            }
                            PyTuple_SET_ITEM(kwnames, curr - source::kw_idx, name);
                            args[curr] = release(as_object(value));
                            ++curr;
                        }
                    } catch (...) {
                        for (size_t i = 1; i < curr; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else {
                    try {
                        args[J + 1] = release(as_object(
                            impl::unpack_arg<J>(std::forward<Values>(values)...)
                        ));
                    } catch (...) {
                        for (size_t i = 1; i <= J; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }
                }
            }

            //////////////////////
            ////    INVOKE    ////
            //////////////////////

            /* After invoking a function with variadic positional arguments, the
            argument iterators must be exhausted, otherwise there are additional
            positional arguments that were not consumed. */
            template <std::input_iterator Iter, std::sentinel_for<Iter> End>
            static void assert_args_are_consumed(Iter& iter, const End& end) {
                if (iter != end) {
                    std::string message =
                        "too many arguments in positional parameter pack: ['" +
                        repr(*iter);
                    while (++iter != end) {
                        message += "', '";
                        message += repr(*iter);
                    }
                    message += "']";
                    throw TypeError(message);
                }
            }

            /* Before invoking a function with variadic keyword arguments, those
            arguments need to be scanned to ensure each of them are recognized and do
            not interfere with other keyword arguments given in the source signature. */
            template <size_t... Is, typename Kwargs>
            static void assert_kwargs_are_recognized(
                std::index_sequence<Is...>,
                Kwargs&& kwargs
            ) {
                std::vector<std::string> extra;
                for (const auto& [key, value] : kwargs) {
                    bool is_empty = key == "";
                    bool match = ((key == Param<Arg<Is>>::name) || ...);
                    if (source::contains(key.c_str())) {
                        throw TypeError("duplicate keyword argument: '" + key + "'");
                    } else if (is_empty || !match) {
                        extra.push_back(key);
                    }
                }
                if (!extra.empty()) {
                    auto iter = extra.begin();
                    auto end = extra.end();
                    std::string message =
                        "unexpected keyword arguments: ['" + repr(*iter);
                    while (++iter != end) {
                        message += "', '";
                        message += repr(*iter);
                    }
                    message += "']";
                    throw TypeError(message);
                }
            }

            /* Invoke an external C++ function using the given arguments and default
            values. */
            template <typename Func>
                requires (enable && std::is_invocable_v<Func, Args...>)
            static decltype(auto) operator()(
                const Defaults& defaults,
                Func&& func,
                Values&&... args
            ) {
                // using Return = std::invoke_result_t<Func, Args...>;
                // return []<size_t... Is>(
                //     std::index_sequence<Is...>,
                //     const Defaults& defaults,
                //     Func&& func,
                //     Values&&... args
                // ) {

                //     // NOTE: we MUST use aggregate initialization of argument proxies in order
                //     // to extend the lifetime of temporaries for the duration of the function
                //     // call.  This is the only guaranteed way of avoiding UB in this context.

                //     // if there are no parameter packs, then we simply reorder the arguments
                //     // and call the function directly
                //     if constexpr (!source::has_args && !source::has_kwargs) {
                //         if constexpr (std::is_void_v<Return>) {
                //             func({Arguments<Source...>::template cpp<Is>(
                //                 defaults,
                //                 std::forward<Source>(args)...
                //             )}...);
                //         } else {
                //             return func({Arguments<Source...>::template cpp<Is>(
                //                 defaults,
                //                 std::forward<Source>(args)...
                //             )}...);
                //         }

                //     // variadic positional arguments are passed as an iterator range, which
                //     // must be exhausted after the function call completes
                //     } else if constexpr (source::has_args && !source::has_kwargs) {
                //         auto var_args = impl::unpack_arg<source::args_index>(
                //             std::forward<Source>(args)...
                //         );
                //         auto iter = var_args.begin();
                //         auto end = var_args.end();
                //         if constexpr (std::is_void_v<Return>) {
                //             func({Arguments<Source...>::template cpp<Is>(
                //                 defaults,
                //                 var_args.size(),
                //                 iter,
                //                 end,
                //                 std::forward<Source>(args)...
                //             )}...);
                //             if constexpr (!target::has_args) {
                //                 impl::assert_var_args_are_consumed(iter, end);
                //             }
                //         } else {
                //             decltype(auto) result = func({Arguments<Source...>::template cpp<Is>(
                //                 defaults,
                //                 var_args.size(),
                //                 iter,
                //                 end,
                //                 std::forward<Source>(args)...
                //             )}...);
                //             if constexpr (!target::has_args) {
                //                 impl::assert_var_args_are_consumed(iter, end);
                //             }
                //             return result;
                //         }

                //     // variadic keyword arguments are passed as a dictionary, which must be
                //     // validated up front to ensure all keys are recognized
                //     } else if constexpr (!source::has_args && source::has_kwargs) {
                //         auto var_kwargs = impl::unpack_arg<source::kwargs_index>(
                //             std::forward<Source>(args)...
                //         );
                //         if constexpr (!target::has_kwargs) {
                //             impl::assert_var_kwargs_are_recognized<target>(
                //                 std::make_index_sequence<target::size>{},
                //                 var_kwargs
                //             );
                //         }
                //         if constexpr (std::is_void_v<Return>) {
                //             func({Arguments<Source...>::template cpp<Is>(
                //                 defaults,
                //                 var_kwargs,
                //                 std::forward<Source>(args)...
                //             )}...);
                //         } else {
                //             return func({Arguments<Source...>::template cpp<Is>(
                //                 defaults,
                //                 var_kwargs,
                //                 std::forward<Source>(args)...
                //             )}...);
                //         }

                //     // interpose the two if there are both positional and keyword argument packs
                //     } else {
                //         auto var_kwargs = impl::unpack_arg<source::kwargs_index>(
                //             std::forward<Source>(args)...
                //         );
                //         if constexpr (!target::has_kwargs) {
                //             impl::assert_var_kwargs_are_recognized<target>(
                //                 std::make_index_sequence<target::size>{},
                //                 var_kwargs
                //             );
                //         }
                //         auto var_args = impl::unpack_arg<source::args_index>(
                //             std::forward<Source>(args)...
                //         );
                //         auto iter = var_args.begin();
                //         auto end = var_args.end();
                //         if constexpr (std::is_void_v<Return>) {
                //             func({Arguments<Source...>::template cpp<Is>(
                //                 defaults,
                //                 var_args.size(),
                //                 iter,
                //                 end,
                //                 var_kwargs,
                //                 std::forward<Source>(args)...
                //             )}...);
                //             if constexpr (!target::has_args) {
                //                 impl::assert_var_args_are_consumed(iter, end);
                //             }
                //         } else {
                //             decltype(auto) result = func({Arguments<Source...>::template cpp<Is>(
                //                 defaults,
                //                 var_args.size(),
                //                 iter,
                //                 end,
                //                 var_kwargs,
                //                 std::forward<Source>(args)...
                //             )}...);
                //             if constexpr (!target::has_args) {
                //                 impl::assert_var_args_are_consumed(iter, end);
                //             }
                //             return result;
                //         }
                //     }
                // }(
                //     std::make_index_sequence<target::size>{},
                //     defaults,
                //     std::forward<Func>(func),
                //     std::forward<Source>(args)...
                // );
            }

            /* Invoke an external Python function using the given arguments.  This will
            always return a new reference to a raw Python object, or throw a runtime
            error if the arguments are malformed in some way. */
            template <typename = void> requires (enable)
            static PyObject* operator()(
                PyObject* func,
                Values&&... args
            ) {

            }

        };

    };

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer, reference, or object, such as a lambda type. */
    template <typename R, typename... A>
    struct Signature<R(A...)> {
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = false;
        using type = R(*)(A...);
        using Return = R;
        using Self = void;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, A...>;
    };
    template <typename R, typename... A>
    struct Signature<R(A...) noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = true;
        using type = R(*)(A...);
        using Return = R;
        using Self = void;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, A...>;
    };
    template <typename R, typename... A>
    struct Signature<R(*)(A...)> {
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = false;
        using type = R(*)(A...);
        using Return = R;
        using Self = void;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, A...>;
    };
    template <typename R, typename... A>
    struct Signature<R(*)(A...) noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = true;
        using type = R(*)(A...);
        using Return = R;
        using Self = void;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...)> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        using type = R(C::*)(A...);
        using Return = R;
        using Self = C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) &> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        using type = R(C::*)(A...);
        using Return = R;
        using Self = C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using type = R(C::*)(A...);
        using Return = R;
        using Self = C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) & noexcept > {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using type = R(C::*)(A...);
        using Return = R;
        using Self = C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        using type = R(C::*)(A...) const;
        using Return = R;
        using Self = const C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const &> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        using type = R(C::*)(A...) const;
        using Return = R;
        using Self = const C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using type = R(C::*)(A...) const;
        using Return = R;
        using Self = const C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const & noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using type = R(C::*)(A...) const;
        using Return = R;
        using Self = const C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        using type = R(C::*)(A...) volatile;
        using Return = R;
        using Self = volatile C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile &> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        using type = R(C::*)(A...) volatile;
        using Return = R;
        using Self = volatile C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using type = R(C::*)(A...) volatile;
        using Return = R;
        using Self = volatile C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile & noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using type = R(C::*)(A...) volatile;
        using Return = R;
        using Self = volatile C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        using type = R(C::*)(A...) const volatile;
        using Return = R;
        using Self = const volatile C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile &> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        using type = R(C::*)(A...) const volatile;
        using Return = R;
        using Self = const volatile C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using type = R(C::*)(A...) const volatile;
        using Return = R;
        using Self = const volatile C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile & noexcept> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using type = R(C::*)(A...) const volatile;
        using Return = R;
        using Self = const volatile C&;
        using Parameters = impl::Parameters<A...>;
        template <typename Func>
        static constexpr bool matches = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <impl::has_call_operator T>
    struct Signature<T> {
        static constexpr bool enable = Signature<decltype(&T::operator())>::enable;
        static constexpr bool has_self = Signature<decltype(&T::operator())>::has_self;
        static constexpr bool has_noexcept = Signature<decltype(&T::operator())>::has_noexcept;
        using type = Signature<decltype(&T::operator())>::type;
        using Return = Signature<decltype(&T::operator())>::Return;
        using Self = Signature<decltype(&T::operator())>::Self;
        using Parameters = Signature<decltype(&T::operator())>::Parameters;
        template <typename Func>
        static constexpr bool matches = Signature<decltype(&T::operator())>::template matches<Func>;
    };

    /* An interface to a std::tuple holding default values for all of the optional
    arguments in the target signature. */
    template <typename... Target>
    struct Defaults {
    private:
        using target = Signature<Target...>;

        template <size_t I>
        struct Value  {
            using annotation = target::template type<I>;
            using type = std::remove_cvref_t<typename Param<annotation>::type>;
            static constexpr StaticStr name = Param<annotation>::name;
            static constexpr size_t index = I;
            type value;
        };

        template <size_t I, typename Tuple, typename... Ts>
        struct TupleType { using type = Tuple; };
        template <size_t I, typename... Defaults, typename T, typename... Ts>
        struct TupleType<I, std::tuple<Defaults...>, T, Ts...> {
            template <typename U>
            struct ToValue {
                using type = TupleType<I + 1, std::tuple<Defaults...>, Ts...>::type;
            };
            template <typename U> requires (Param<U>::opt)
            struct ToValue<U> {
                using type = TupleType<I + 1, std::tuple<Defaults..., Value<I>>, Ts...>::type;
            };
            using type = ToValue<T>::type;
        };

        template <size_t I, typename... Ts>
        struct find_helper { static constexpr size_t value = 0; };
        template <size_t I, typename T, typename... Ts>
        struct find_helper<I, std::tuple<T, Ts...>> { static constexpr size_t value = 
            (I == T::index) ? 0 : 1 + find_helper<I, std::tuple<Ts...>>::value;
        };

    public:

        /* A std::tuple type where each element is a Value<I> wrapper around all the
        arguments marked as ::opt in the target signature. */
        using tuple = TupleType<0, std::tuple<>, Target...>::type;

        /* The total number of (optional) arguments that the function accepts, not
        including variadic positional or keyword arguments. */
        static constexpr size_t size = std::tuple_size<tuple>::value;

        /* The number of (optional) positional-only arguments that the function
        accepts, not including variadic positional arguments. */
        static constexpr size_t pos_size = target::pos_opt_count;

        /* The number of (optional) keyword-only arguments that the function accepts,
        not including variadic keyword arguments. */
        static constexpr size_t kw_size = target::kw_only_opt_count;

        /* Given an index into the target signature, find the corresponding index in
        the `type` tuple if that index corresponds to a default value.  Otherwise,
        return the size of the defaults tuple. */
        template <size_t I>
        static constexpr size_t find = find_helper<I, tuple>::value;

        /* Find the index of the named argument, or `size` if the argument is not
        present. */
        template <StaticStr name>
        static constexpr size_t index = find<target::template index<name>>;

        /* Check if the named argument is present in the defaults tuple. */
        template <StaticStr name>
        static constexpr bool contains = index<name> != size;

    private:

        tuple values;

        template <typename... Source>
        struct Constructor {
            using source = Signature<Source...>;

            /* Recursively check whether the default values fully satisfy the target
            signature.  `I` refers to the index in the defaults tuple, while `J` refers
            to the index of the source signature given to the Function constructor. */
            template <size_t I, size_t J>
            static constexpr bool enable_recursive = true;
            template <size_t I, size_t J> requires (I < size && J < source::size)
            static constexpr bool enable_recursive<I, J> = [] {
                using Value = std::tuple_element<I, tuple>::type;
                using Arg = source::template type<J>;
                if constexpr (Param<Arg>::pos) {
                    if constexpr (!std::convertible_to<
                        typename Param<Arg>::type,
                        typename Value::type
                    >) {
                        return false;
                    }
                } else if constexpr (Param<Arg>::kw) {
                    if constexpr (
                        !target::template contains<Param<Arg>::name> ||
                        !source::template contains<Value::name>
                    ) {
                        return false;
                    } else {
                        constexpr size_t idx = index<Param<Arg>::name>;
                        using Match = std::tuple_element<idx, tuple>::type;
                        if constexpr (!std::convertible_to<
                            typename Param<Arg>::type,
                            typename Match::type
                        >) {
                            return false;
                        }
                    }
                } else {
                    return false;
                }
                return enable_recursive<I + 1, J + 1>;
            }();

            /* Constructor is only enabled if the default values are fully satisfied. */
            static constexpr bool enable =
                source::valid && source::size == size  && enable_recursive<0, 0>;

            template <size_t I>
            static constexpr decltype(auto) build_recursive(Source&&... values) {
                if constexpr (I < source::kw_index) {
                    return impl::unpack_arg<I>(std::forward<Source>(values)...);
                } else {
                    using Value = std::tuple_element<I, tuple>::type;
                    return impl::unpack_arg<source::template index<Value::name>>(
                        std::forward<Source>(values)...
                    ).value;
                }
            }

            /* Invoke the constructor to build the default values tuple from the
            provided arguments, reordering them as needed to account for keywords. */
            template <size_t... Is>
            static constexpr tuple operator()(
                std::index_sequence<Is...>,
                Source&&... values
            ) {
                return {build_recursive<Is>(std::forward<Source>(values)...)...};
            }

        };

    public:

        /* Indicates whether the source arguments fully satisfy the default values in
        the target signature. */
        template <typename... Source>
        static constexpr bool enable = Constructor<Source...>::enable;

        template <typename... Source> requires (enable<Source...>)
        Defaults(Source&&... source) : values(Constructor<Source...>{}(
            std::index_sequence_for<Source...>{},
            std::forward<Source>(source)...
        )) {}

        Defaults(const Defaults& other) = default;
        Defaults(Defaults&& other) = default;

        /* Get the default value associated with the target argument at index I. */
        template <size_t I>
        auto& get() {
            return std::get<find<I>>(values).value;
        };

        /* Get the default value associated with the target argument at index I. */
        template <size_t I>
        const auto& get() const {
            return std::get<find<I>>(values).value;
        };

        /* Get the default value associated with the named target argument. */
        template <StaticStr name>
        auto& get() {
            return std::get<index<name>>(values).value;
        };

        /* Get the default value associated with the named target argument. */
        template <StaticStr name>
        const auto& get() const {
            return std::get<index<name>>(values).value;
        };

    };


}


template <typename F> requires (impl::Signature<F>::enable)
struct Interface<Function<F>> : impl::FunctionTag {
    static_assert(impl::Signature<F>::valid, "invalid function signature");

    /* The normalized function pointer type for this specialization. */
    using Signature = impl::Signature<F>::type;

    /* The function's return type. */
    using Return = impl::Signature<F>::Return;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = impl::Signature<F>::Self;

    /* A type holding the function's default values, which are inferred from the input
    signature and stored as a `std::tuple`. */
    using Defaults = impl::Signature<F>::Parameters::Defaults;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = impl::Signature<F>::Parameters::n;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = impl::Signature<F>::Parameters::n_pos;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = impl::Signature<F>::Parameters::n_posonly;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = impl::Signature<F>::Parameters::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = impl::Signature<F>::Parameters::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = impl::Signature<F>::Parameters::n_opt;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = impl::Signature<F>::Parameters::n_opt_pos;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = impl::Signature<F>::Parameters::n_opt_posonly;

    /* The total number of optional keyword arguments that the function accepts,
    counting both keyword-only and positional-or-keyword arguments, but not
    positional-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_kw = impl::Signature<F>::Parameters::n_opt_kw;

    /* The total number of optional keyword-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_kwonly = impl::Signature<F>::Parameters::n_opt_kwonly;

    /* Check if the named argument is present in the function signature. */
    template <StaticStr Name>
    static constexpr bool has = impl::Signature<F>::Parameters::template has<Name>;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = impl::Signature<F>::Parameters::has_pos;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = impl::Signature<F>::Parameters::has_posonly;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = impl::Signature<F>::Parameters::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = impl::Signature<F>::Parameters::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = impl::Signature<F>::Parameters::has_opt;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = impl::Signature<F>::Parameters::has_opt_pos;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = impl::Signature<F>::Parameters::has_opt_posonly;

    /* Check if the function accepts at least one optional keyword argument.  This will
    match either positional-or-keyword or keyword-only arguments. */
    static constexpr bool has_opt_kw = impl::Signature<F>::Parameters::has_opt_kw;

    /* Check if the function accepts at least one optional keyword-only argument. */
    static constexpr bool has_opt_kwonly = impl::Signature<F>::Parameters::has_opt_kwonly;

    /* Check if the function has a `self` parameter, indicating that it can be called
    as a member function. */
    static constexpr bool has_self = impl::Signature<F>::has_self;

    /* Check if the function accepts variadic positional arguments. */
    static constexpr bool has_args = impl::Signature<F>::Parameters::has_args;

    /* Check if the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = impl::Signature<F>::Parameters::has_kwargs;

    /* Find the index of the named argument, if it is present. */
    template <StaticStr Name> requires (has<Name>)
    static constexpr size_t idx = impl::Signature<F>::Parameters::template idx<Name>;

    /* Find the index of the first keyword argument that appears in the function
    signature.  This will match either a positional-or-keyword argument or a
    keyword-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t kw_idx = impl::Signature<F>::Parameters::kw_index;

    /* Find the index of the first keyword-only argument that appears in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwonly_idx = impl::Signature<F>::Parameters::kw_only_index;

    /* Find the index of the first optional argument in the function signature.  If no
    such argument is present, this will return `n`. */
    static constexpr size_t opt_idx = impl::Signature<F>::Parameters::opt_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = impl::Signature<F>::Parameters::opt_pos_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = impl::Signature<F>::Parameters::opt_posonly_index;

    /* Find the index of the first optional keyword argument in the function signature.
    This will match either a positional-or-keyword argument or a keyword-only argument.
    If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kw_idx = impl::Signature<F>::Parameters::opt_kw_index;

    /* Find the index of the first optional keyword-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kwonly_idx = impl::Signature<F>::Parameters::opt_kwonly_index;

    /* Find the index of the variadic positional arguments in the function signature,
    if they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t args_idx = impl::Signature<F>::Parameters::args_index;

    /* Find the index of the variadic keyword arguments in the function signature, if
    they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwargs_idx = impl::Signature<F>::Parameters::kwargs_index;

    /* Get the (possibly) annotated type of the argument at index I. */
    template <size_t I> requires (I < n)
    using Arg = impl::Signature<F>::Parameters::template Arg<I>;

    /* Get the annotated type of the named argument. */
    template <StaticStr Name> requires (has<Name>)
    using Kwarg = impl::Signature<F>::Parameters::template Kwarg<Name>;

    /* Get the type expected by the function's variadic positional arguments, or void
    if it does not accept variadic positional arguments. */
    using Args = impl::Signature<F>::Parameters::Args;

    /* Get the type expected by the function's variadic keyword arguments, or void if
    it does not accept variadic keyword arguments. */
    using Kwargs = impl::Signature<F>::Parameters::Kwargs;

    /// TODO: add an invocable<> predicate to check if the function can be called with
    /// the given arguments.





    /* Call an external C++ function that matches the target signature using
    Python-style arguments.  The default values (if any) must be provided as an
    initializer list immediately before the function to be invoked, or constructed
    elsewhere and passed by reference.
    
    This helper has no runtime overhead over a traditional C++ function call, not
    counting any implicit logic when constructing default values. */
    template <typename Func, typename... Args>
        requires (
            impl::Signature<F>::template matches<Func> &&
            impl::Signature<F>::Parameters::template Bind<Args...>::enable
        )
    static Return invoke(const Defaults& defaults, Func&& func, Args&&... args);

    /* Call an external Python function using Python-style arguments.  The optional
    `Return` template parameter specifies an intermediate return type, which is used to
    interpret the result of the raw CPython call.  The intermediate result will then be
    implicitly converted to the function's final return type, triggering type safety
    checks along the way.  The default value is `Object`, which always incurs a check
    on conversion.  If the exact return type is known in advance, then setting `Return`
    equal to that type will avoid any extra checks at the expense of safety.  */
    template <typename Return = Object, typename... Args>
        requires (impl::Signature<F>::Parameters::template Bind<Args...>::enable)
    static Return invoke(PyObject* func, Args&&... args);

    __declspec(property(get=_get_name, put=_set_name)) std::string __name__;
    [[nodiscard]] std::string _get_name(this const auto& self);
    void _set_name(this auto& self, const std::string& name);

    __declspec(property(get=_get_doc, put=_set_doc)) std::string __doc__;
    [[nodiscard]] std::string _get_doc(this const auto& self);
    void _set_doc(this auto& self, const std::string& doc);

    __declspec(property(get=_get_qualname, put=_set_qualname))
        std::optional<std::string> __qualname__;
    [[nodiscard]] std::optional<std::string> _get_qualname(this const auto& self);
    void _set_qualname(this auto& self, const std::string& qualname);

    __declspec(property(get=_get_module, put=_set_module))
        std::optional<std::string> __module__;
    [[nodiscard]] std::optional<std::string> _get_module(this const auto& self);
    void _set_module(this auto& self, const std::string& module);

    __declspec(property(get=_get_code, put=_set_code)) std::optional<Code> __code__;
    [[nodiscard]] std::optional<Code> _get_code(this const auto& self);
    void _set_code(this auto& self, const Code& code);

    /// TODO: defined in __init__.h
    __declspec(property(get=_get_globals)) std::optional<Dict<Str, Object>> __globals__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_globals(this const auto& self);

    __declspec(property(get=_get_closure)) std::optional<Tuple<Object>> __closure__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_closure(this const auto& self);

    __declspec(property(get=_get_defaults, put=_set_defaults))
        std::optional<Tuple<Object>> __defaults__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_defaults(this const auto& self);
    void _set_defaults(this auto& self, const Tuple<Object>& defaults);

    __declspec(property(get=_get_annotations, put=_set_annotations))
        std::optional<Dict<Str, Object>> __annotations__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_annotations(this const auto& self);
    void _set_annotations(this auto& self, const Dict<Str, Object>& annotations);

    __declspec(property(get=_get_kwdefaults, put=_set_kwdefaults))
        std::optional<Dict<Str, Object>> __kwdefaults__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_kwdefaults(this const auto& self);
    void _set_kwdefaults(this auto& self, const Dict<Str, Object>& kwdefaults);

    __declspec(property(get=_get_type_params, put=_set_type_params))
        std::optional<Tuple<Object>> __type_params__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_type_params(this const auto& self);
    void _set_type_params(this auto& self, const Tuple<Object>& type_params);

    /// TODO: instance methods (given as pointer to member functions) support
    /// additional __self__ and __func__ properties?

};
template <typename Signature> requires (impl::GetSignature<Signature>::enable)
struct Interface<Type<Function<Signature>>> {
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __name__(const Self& self) {
        return self.__name__;
    }
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __doc__(const Self& self) {
        return self.__doc__;
    }
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<std::string> __qualname__(const Self& self) {
        return self.__qualname__;
    }
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<std::string> __module__(const Self& self) {
        return self.__module__;
    }
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Code> __code__(const Self& self) {
        return self.__code__;
    }

    /// TODO: defined in __init__.h
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __globals__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __closure__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __defaults__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __annotations__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __kwdefaults__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __type_params__(const Self& self);
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
template <typename F = Object(
    Arg<"args", const Object&>::args,
    Arg<"kwargs", const Object&>::kwargs
)> requires (impl::GetSignature<F>::enable)
struct Function : Function<typename impl::GetSignature<F>::type> {};


/// TODO: in order to properly forward `self`, this class will need to be split into
/// a bunch of different specializations, depending on how the self argument needs to
/// be handled.  That will require me to move most of the shared logic into the impl::
/// namespace.  The bound method versions might also have to implement a different
/// Python type in order to handle the forwarding semantics.

/// TODO: class methods can be indicated by a member method of Type<Wrapper>.  That
/// would allow this mechanism to scale arbitrarily.


/* Specialization for static functions without a bound `self` argument. */
template <typename Return, typename... Target>
struct Function<Return(*)(Target...)> : Object, Interface<Function<Return(*)(Target...)>> {
protected:

    using target = impl::Signature<Target...>;
    static_assert(impl::Signature<Target...>::valid);

public:

    /// TODO: perhaps these should be placed in the interface for each specialization.

    /* The total number of arguments that the function accepts, not including variadic
    positional or keyword arguments. */
    static constexpr size_t size = target::size - target::has_args - target::has_kwargs;

    /* The number of positional-only arguments that the function accepts, not including
    variadic positional arguments. */
    static constexpr size_t pos_size = target::pos_count;

    /* The number of keyword-only arguments that the function accepts, not including
    variadic keyword arguments. */
    static constexpr size_t kw_size = target::kw_only_count;

    /* Indicates whether an argument with the given name is present. */
    template <StaticStr name>
    static constexpr bool has_arg =
        target::template contains<name> &&
        target::template index<name> < target::kw_only_index;

    /* indicates whether a keyword argument with the given name is present. */
    template <StaticStr name>
    static constexpr bool has_kwarg =
        target::template contains<name> &&
        target::template index<name> >= target::kw_index;

    /* Indicates whether the function accepts variadic positional arguments. */
    static constexpr bool has_var_args = target::has_args;

    /* Indicates whether the function accepts variadic keyword arguments. */
    static constexpr bool has_var_kwargs = target::has_kwargs;

    using Defaults = impl::Defaults<Target...>;

    struct __python__ : def<__python__, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A Python wrapper around a templated C++ `std::function`.

Notes
-----
This type is not directly instantiable from Python.  Instead, it serves as an entry
point to the template hierarchy, which can be navigated by subscripting the type
just like the `Callable` type hint.

Examples
--------
>>> from bertrand import Function
>>> Function[None, [int, str]]
py::Function<void(const py::Int&, const py::Str&)>
>>> Function[int, ["x": int, "y": int]]
py::Function<py::Int(py::Arg<"x", const py::Int&>, py::Arg<"y", const py::Int&>)>

As long as these types have been instantiated somewhere at the C++ level, then these
accessors will resolve to a unique Python type that wraps that instantiation.  If no
instantiation could be found, then a `TypeError` will be raised.

Because each of these types is unique, they can be used with Python's `isinstance()`
and `issubclass()` functions to check against the exact template signature.  A global
check against this root type will determine whether the function is implemented in C++
(True) or Python (False).)doc";

        std::string name;
        std::string docstring;
        std::function<Return(Target...)> func;
        Defaults defaults;
        vectorcallfunc vectorcall;

        __python__(
            std::string&& name,
            std::string&& docstring,
            std::function<Return(Target...)>&& func,
            Defaults&& defaults
        ) : name(std::move(name)),
            docstring(std::move(docstring)),
            func(std::move(func)),
            defaults(std::move(defaults)),
            vectorcall(&__vectorcall__)
        {}

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings) {
            /// TODO: manually initialize a heap type with a correct vectorcall offset.
            /// This has to be an instance of the metaclass, which may require a
            /// forward reference here.
        }

        /// TODO: have to do some checking to see if there's a self argument I need to
        /// forward to.  Perhaps Function can be templated to accept member functions?

        static PyObject* __vectorcall__(
            __python__* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {
            try {
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyFunction* self,
                    PyObject* const* args,
                    Py_ssize_t nargsf,
                    PyObject* kwnames
                ) {
                    size_t nargs = PyVectorcall_NARGS(nargsf);
                    if constexpr (!target::has_args) {
                        constexpr size_t expected = target::size - target::kw_only_count;
                        if (nargs > expected) {
                            throw TypeError(
                                "expected at most " + std::to_string(expected) +
                                " positional arguments, but received " +
                                std::to_string(nargs)
                            );
                        }
                    }
                    size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
                    if constexpr (!target::has_kwargs) {
                        for (size_t i = 0; i < kwcount; ++i) {
                            Py_ssize_t length;
                            const char* name = PyUnicode_AsUTF8AndSize(
                                PyTuple_GET_ITEM(kwnames, i),
                                &length
                            );
                            if (name == nullptr) {
                                Exception::from_python();
                            } else if (!target::has_keyword(name)) {
                                throw TypeError(
                                    "unexpected keyword argument '" +
                                    std::string(name, length) + "'"
                                );
                            }
                        }
                    }
                    if constexpr (std::is_void_v<Return>) {
                        self->func(
                            {impl::Arguments<Target...>::template from_python<Is>(
                                self->defaults,
                                args,
                                nargs,
                                kwnames,
                                kwcount
                            )}...
                        );
                        Py_RETURN_NONE;
                    } else {
                        return release(as_object(
                            self->func(
                                {impl::Arguments<Target...>::template from_python<Is>(
                                    self->defaults,
                                    args,
                                    nargs,
                                    kwnames,
                                    kwcount
                                )}...
                            )
                        ));
                    }
                }(
                    std::make_index_sequence<target::size>{},
                    self,
                    args,
                    nargsf,
                    kwnames
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(__python__* self) {
            static const std::string demangled =
                impl::demangle(typeid(Function).name());
            std::string s = "<" + demangled + " at " +
                std::to_string(reinterpret_cast<size_t>(self)) + ">";
            PyObject* string = PyUnicode_FromStringAndSize(s.c_str(), s.size());
            if (string == nullptr) {
                return nullptr;
            }
            return string;
        }

    };

    using ReturnType = Return;
    using ArgTypes = std::tuple<typename impl::Param<Target>::type...>;
    using Annotations = std::tuple<Target...>;

    /* Instantiate a new function type with the same arguments but a different return
    type. */
    template <typename R>
    using with_return = Function<R(Target...)>;

    /* Instantiate a new function type with the same return type but different
    argument annotations. */
    template <typename... Ts>
    using with_args = Function<Return(Ts...)>;

    /* Template constraint that evaluates to true if this function can be called with
    the templated argument types. */
    template <typename... Source>
    static constexpr bool invocable = impl::Arguments<Source...>::enable;

    // TODO: constructor should fail if the function type is a subclass of my root
    // function type, but not a subclass of this specific function type.  This
    // indicates a type mismatch in the function signature, which is a violation of
    // static type safety.  I can then print a helpful error message with the demangled
    // function types which can show their differences.
    // -> This can be implemented in the actual call operator itself, but it would be
    // better to implement it on the constructor side.  Perhaps including it in the
    // isinstance()/issubclass() checks would be sufficient, since those are used
    // during implicit conversion anyways.

    Function(PyObject* p, borrowed_t t) : Object(p, t) {}
    Function(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Function>::template enable<Args...>)
    Function(Args&&... args) : Object(
        implicit_ctor<Function>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Function>::template enable<Args...>)
    explicit Function(Args&&... args) : Object(
        explicit_ctor<Function>{},
        std::forward<Args>(args)...
    ) {}

    /// TODO: invoke() should be unified for both Python functions and C++ functions.
    /// the python overload will take a PyObject* as the first argument, followed by
    /// Python-style argument annotations for the call itself.  The C++ overload will
    /// take a Defaults tuple followed by a function that is invocable with the target
    /// signature.  There will also have to be two additional overloads for when
    /// invoke() is called on a Python object.  If the object is implemented in C++,
    /// unwrap it and forward to invoke() on the C++ object.  If it is implemented in
    /// Python, get its ptr() and forward to invoke() on the Python object.

    /* Call an external C++ function that matches the target signature using the
    given defaults and Python-style arguments. */
    template <typename Func, typename... Source>
        requires (std::is_invocable_r_v<Return, Func, Target...> && invocable<Source...>)
    static Return invoke(const Defaults& defaults, Func&& func, Source&&... args) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Defaults& defaults,
            Func&& func,
            Source&&... args
        ) {
            using source = impl::Signature<Source...>;

            // NOTE: we MUST use aggregate initialization of argument proxies in order
            // to extend the lifetime of temporaries for the duration of the function
            // call.  This is the only guaranteed way of avoiding UB in this context.

            // if there are no parameter packs, then we simply reorder the arguments
            // and call the function directly
            if constexpr (!source::has_args && !source::has_kwargs) {
                if constexpr (std::is_void_v<Return>) {
                    func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        std::forward<Source>(args)...
                    )}...);
                } else {
                    return func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        std::forward<Source>(args)...
                    )}...);
                }

            // variadic positional arguments are passed as an iterator range, which
            // must be exhausted after the function call completes
            } else if constexpr (source::has_args && !source::has_kwargs) {
                auto var_args = impl::unpack_arg<source::args_index>(
                    std::forward<Source>(args)...
                );
                auto iter = var_args.begin();
                auto end = var_args.end();
                if constexpr (std::is_void_v<Return>) {
                    func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        std::forward<Source>(args)...
                    )}...);
                    if constexpr (!target::has_args) {
                        impl::assert_var_args_are_consumed(iter, end);
                    }
                } else {
                    decltype(auto) result = func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        std::forward<Source>(args)...
                    )}...);
                    if constexpr (!target::has_args) {
                        impl::assert_var_args_are_consumed(iter, end);
                    }
                    return result;
                }

            // variadic keyword arguments are passed as a dictionary, which must be
            // validated up front to ensure all keys are recognized
            } else if constexpr (!source::has_args && source::has_kwargs) {
                auto var_kwargs = impl::unpack_arg<source::kwargs_index>(
                    std::forward<Source>(args)...
                );
                if constexpr (!target::has_kwargs) {
                    impl::assert_var_kwargs_are_recognized<target>(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                if constexpr (std::is_void_v<Return>) {
                    func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )}...);
                } else {
                    return func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )}...);
                }

            // interpose the two if there are both positional and keyword argument packs
            } else {
                auto var_kwargs = impl::unpack_arg<source::kwargs_index>(
                    std::forward<Source>(args)...
                );
                if constexpr (!target::has_kwargs) {
                    impl::assert_var_kwargs_are_recognized<target>(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                auto var_args = impl::unpack_arg<source::args_index>(
                    std::forward<Source>(args)...
                );
                auto iter = var_args.begin();
                auto end = var_args.end();
                if constexpr (std::is_void_v<Return>) {
                    func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )}...);
                    if constexpr (!target::has_args) {
                        impl::assert_var_args_are_consumed(iter, end);
                    }
                } else {
                    decltype(auto) result = func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )}...);
                    if constexpr (!target::has_args) {
                        impl::assert_var_args_are_consumed(iter, end);
                    }
                    return result;
                }
            }
        }(
            std::make_index_sequence<target::size>{},
            defaults,
            std::forward<Func>(func),
            std::forward<Source>(args)...
        );
    }

    /* Call an external Python function using Python-style arguments.  The optional
    `R` template parameter specifies an interim return type, which is used to interpret
    the result of the raw CPython call.  The interim result will then be implicitly
    converted to the function's final return type.  The default value is `Object`,
    which incurs an additional dynamic type check on conversion.  If the exact return
    type is known in advance, then setting `R` equal to that type will avoid any extra
    checks or conversions at the expense of safety if that type is incorrect.  */
    template <typename R = Object, typename... Source> requires (invocable<Source...>)
    static Return invoke(PyObject* func, Source&&... args) {
        static_assert(
            std::derived_from<R, Object>,
            "Interim return type must be a subclass of Object"
        );

        // bypass the Python interpreter if the function is an instance of the coupled
        // function type, which guarantees that the template signatures exactly match
        if (PyType_IsSubtype(Py_TYPE(func), &PyFunction::type)) {
            PyFunction* func = reinterpret_cast<PyFunction*>(ptr(func));
            if constexpr (std::is_void_v<Return>) {
                invoke(
                    func->defaults,
                    func->func,
                    std::forward<Source>(args)...
                );
            } else {
                return invoke(
                    func->defaults,
                    func->func,
                    std::forward<Source>(args)...
                );
            }
        }

        // fall back to an optimized Python call
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            PyObject* func,
            Source&&... args
        ) {
            using source = Signature<Source...>;
            PyObject* result;

            // if there are no arguments, we can use the no-args protocol
            if constexpr (source::size == 0) {
                result = PyObject_CallNoArgs(func);

            // if there are no variadic arguments, we can stack allocate the argument
            // array with a fixed size
            } else if constexpr (!source::has_args && !source::has_kwargs) {
                // if there is only one argument, we can use the one-arg protocol
                if constexpr (source::size == 1) {
                    if constexpr (source::has_kw) {
                        result = PyObject_CallOneArg(
                            func,
                            ptr(as_object(
                                impl::unpack_arg<0>(std::forward<Source>(args)...).value
                            ))
                        );
                    } else {
                        result = PyObject_CallOneArg(
                            func,
                            ptr(as_object(
                                impl::unpack_arg<0>(std::forward<Source>(args)...)
                            ))
                        );
                    }

                // if there is more than one argument, we construct a vectorcall
                // argument array
                } else {
                    // PY_VECTORCALL_ARGUMENTS_OFFSET requires an extra element
                    PyObject* array[source::size + 1];
                    array[0] = nullptr;  // first element is reserved for 'self'
                    PyObject* kwnames;
                    if constexpr (source::has_kw) {
                        kwnames = PyTuple_New(source::kw_count);
                    } else {
                        kwnames = nullptr;
                    }
                    (
                        Arguments<Source...>::template to_python<Is>(
                            kwnames,
                            array,  // 1-indexed
                            std::forward<Source>(args)...
                        ),
                        ...
                    );
                    Py_ssize_t npos = source::size - source::kw_count;
                    result = PyObject_Vectorcall(
                        func,
                        array + 1,  // skip the first element
                        npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    );
                    for (size_t i = 1; i <= source::size; ++i) {
                        Py_XDECREF(array[i]);  // release all argument references
                    }
                }

            // otherwise, we have to heap-allocate the array with a variable size
            } else if constexpr (source::has_args && !source::has_kwargs) {
                auto var_args =
                    impl::unpack_arg<source::args_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 1 + var_args.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames;
                if constexpr (source::has_kw) {
                    kwnames = PyTuple_New(source::kw_count);
                } else {
                    kwnames = nullptr;
                }
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                Py_ssize_t npos = source::size - 1 - source::kw_count + var_args.size();
                result = PyObject_Vectorcall(
                    func,
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;

            // The following specializations handle the cross product of
            // positional/keyword parameter packs which differ only in initialization
            } else if constexpr (!source::has_args && source::has_kwargs) {
                auto var_kwargs =
                    impl::unpack_arg<source::kwargs_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 1 + var_kwargs.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames = PyTuple_New(source::kw_count + var_kwargs.size());
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                Py_ssize_t npos = source::size - 1 - source::kw_count;
                result = PyObject_Vectorcall(
                    func,
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;

            } else {
                auto var_args =
                    impl::unpack_arg<source::args_index>(std::forward<Source>(args)...);
                auto var_kwargs =
                    impl::unpack_arg<source::kwargs_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 2 + var_args.size() + var_kwargs.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames = PyTuple_New(source::kw_count + var_kwargs.size());
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                size_t npos = source::size - 2 - source::kw_count + var_args.size();
                result = PyObject_Vectorcall(
                    func,
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;
            }

            // A null return value indicates an error
            if (result == nullptr) {
                Exception::from_python();
            }

            // Python void functions return a reference to None, which we must release
            if constexpr (std::is_void_v<Return>) {
                Py_DECREF(result);
            } else {
                return reinterpret_steal<R>(result);
            }
        }(
            std::make_index_sequence<sizeof...(Source)>{},
            func,
            std::forward<Source>(args)...
        );
    }

    /// TODO: operator() should be a specialization of __call__

    /* Call the function with the given arguments.  If the wrapped function is of the
    coupled Python type, then this will be translated into a raw C++ call, bypassing
    Python entirely. */
    template <typename... Source> requires (invocable<Source...>)
    Return operator()(Source&&... args) const {
        if constexpr (std::is_void_v<Return>) {
            invoke(m_ptr, std::forward<Source>(args)...);  // Don't reference m_ptr directly, and turn this into a control struct
        } else {
            return invoke(m_ptr, std::forward<Source>(args)...);
        }
    }

};


/* Specialization for mutable instance methods. */
template <typename Return, typename Self, typename... Target>
struct Function<Return(Self::*)(Target...)> :
    Object, Interface<Function<Return(Self::*)(Target...)>>
{

};


/* Specialization for const instance methods. */
template <typename Return, typename Self, typename... Target>
struct Function<Return(Self::*)(Target...) const> :
    Object, Interface<Function<Return(Self::*)(Target...) const>>
{

};


/* Specialization for volatile instance methods. */
template <typename Return, typename Self, typename... Target>
struct Function<Return(Self::*)(Target...) volatile> :
    Object, Interface<Function<Return(Self::*)(Target...) volatile>>
{

};


/* Specialization for const volatile instance methods. */
template <typename Return, typename Self, typename... Target>
struct Function<Return(Self::*)(Target...) const volatile> :
    Object, Interface<Function<Return(Self::*)(Target...) const volatile>>
{

};






template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(F, D...) -> Function<typename impl::GetSignature<std::decay_t<F>>::type>;
template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(std::string, F, D...) -> Function<
    typename impl::GetSignature<std::decay_t<F>>::type
>;
template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(std::string, std::string, F, D...) -> Function<
    typename impl::GetSignature<std::decay_t<F>>::type
>;


template <typename R, typename... A>
struct __as_object__<R(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename... A>
struct __as_object__<R(*)(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) volatile> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) volatile noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const volatile> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const volatile noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename... A>
struct __as_object__<std::function<R(A...)>> : Returns<Function<R(A...)>> {};


template <typename T, typename R, typename... A>
struct __isinstance__<T, Function<R(A...)>> : Returns<bool> {
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

template <typename T, typename R, typename... A>
struct __issubclass__<T, Function<R(A...)>> : Returns<bool> {
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



/* Get the name of the wrapped function. */
template <typename Signature> requires (impl::GetSignature<Signature>::enable)
[[nodiscard]] std::string Interface<Function<Signature>>::_get_name(this const auto& self) {
    // TODO: get the base type and check against it.
    if (true) {
        Type<Function> func_type;
        if (PyType_IsSubtype(
            Py_TYPE(ptr(self)),
            reinterpret_cast<PyTypeObject*>(ptr(func_type))
        )) {
            PyFunction* func = reinterpret_cast<PyFunction*>(ptr(self));
            return func->base.name;
        }
        // TODO: print out a detailed error message with both signatures
        throw TypeError("signature mismatch");
    }

    PyObject* result = PyObject_GetAttrString(ptr(self), "__name__");
    if (result == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t length;
    const char* data = PyUnicode_AsUTF8AndSize(result, &length);
    Py_DECREF(result);
    if (data == nullptr) {
        Exception::from_python();
    }
    return std::string(data, length);
}


// // get_default<I>() returns a reference to the default value of the I-th argument
// // get_default<name>() returns a reference to the default value of the named argument

// // TODO:
// // .defaults -> MappingProxy<Str, Object>
// // .annotations -> MappingProxy<Str, Type<Object>>
// // .posonly -> Tuple<Str>
// // .kwonly -> Tuple<Str>

// // /* Get a read-only dictionary mapping argument names to their default values. */
// // __declspec(property(get=_get_defaults)) MappingProxy<Dict<Str, Object>> defaults;
// // [[nodiscard]] MappingProxy<Dict<Str, Object>> _get_defaults() const {
// //     // TODO: check for the PyFunction type and extract the defaults directly
// //     if (true) {
// //         Type<Function> func_type;
// //         if (PyType_IsSubtype(
// //             Py_TYPE(ptr(*this)),
// //             reinterpret_cast<PyTypeObject*>(ptr(func_type))
// //         )) {
// //             // TODO: generate a dictionary using a fold expression over the
// //             // defaults, then convert to a MappingProxy.
// //             // Or maybe return a std::unordered_map
// //             throw NotImplementedError();
// //         }
// //         // TODO: print out a detailed error message with both signatures
// //         throw TypeError("signature mismatch");
// //     }

// //     // check for positional defaults
// //     PyObject* pos_defaults = PyFunction_GetDefaults(ptr(*this));
// //     if (pos_defaults == nullptr) {
// //         if (code.kwonlyargcount() > 0) {
// //             Object kwdefaults = attr<"__kwdefaults__">();
// //             if (kwdefaults.is(None)) {
// //                 return MappingProxy(Dict<Str, Object>{});
// //             } else {
// //                 return MappingProxy(reinterpret_steal<Dict<Str, Object>>(
// //                     kwdefaults.release())
// //                 );
// //             }
// //         } else {
// //             return MappingProxy(Dict<Str, Object>{});
// //         }
// //     }

// //     // extract positional defaults
// //     size_t argcount = code.argcount();
// //     Tuple<Object> defaults = reinterpret_borrow<Tuple<Object>>(pos_defaults);
// //     Tuple<Str> names = code.varnames()[{argcount - defaults.size(), argcount}];
// //     Dict result;
// //     for (size_t i = 0; i < defaults.size(); ++i) {
// //         result[names[i]] = defaults[i];
// //     }

// //     // merge keyword-only defaults
// //     if (code.kwonlyargcount() > 0) {
// //         Object kwdefaults = attr<"__kwdefaults__">();
// //         if (!kwdefaults.is(None)) {
// //             result.update(Dict(kwdefaults));
// //         }
// //     }
// //     return result;
// // }

// // /* Set the default value for one or more arguments.  If nullopt is provided,
// // then all defaults will be cleared. */
// // void defaults(Dict&& dict) {
// //     Code code = this->code();

// //     // TODO: clean this up.  The logic should go as follows:
// //     // 1. check for positional defaults.  If found, build a dictionary with the new
// //     // values and remove them from the input dict.
// //     // 2. check for keyword-only defaults.  If found, build a dictionary with the
// //     // new values and remove them from the input dict.
// //     // 3. if any keys are left over, raise an error and do not update the signature
// //     // 4. set defaults to Tuple(positional_defaults.values()) and update kwdefaults
// //     // in-place.

// //     // account for positional defaults
// //     PyObject* pos_defaults = PyFunction_GetDefaults(self());
// //     if (pos_defaults != nullptr) {
// //         size_t argcount = code.argcount();
// //         Tuple<Object> defaults = reinterpret_borrow<Tuple<Object>>(pos_defaults);
// //         Tuple<Str> names = code.varnames()[{argcount - defaults.size(), argcount}];
// //         Dict positional_defaults;
// //         for (size_t i = 0; i < defaults.size(); ++i) {
// //             positional_defaults[*names[i]] = *defaults[i];
// //         }

// //         // merge new defaults with old ones
// //         for (const Object& key : positional_defaults) {
// //             if (dict.contains(key)) {
// //                 positional_defaults[key] = dict.pop(key);
// //             }
// //         }
// //     }

// //     // check for keyword-only defaults
// //     if (code.kwonlyargcount() > 0) {
// //         Object kwdefaults = attr<"__kwdefaults__">();
// //         if (!kwdefaults.is(None)) {
// //             Dict temp = {};
// //             for (const Object& key : kwdefaults) {
// //                 if (dict.contains(key)) {
// //                     temp[key] = dict.pop(key);
// //                 }
// //             }
// //             if (dict) {
// //                 throw ValueError("no match for arguments " + Str(List(dict.keys())));
// //             }
// //             kwdefaults |= temp;
// //         } else if (dict) {
// //             throw ValueError("no match for arguments " + Str(List(dict.keys())));
// //         }
// //     } else if (dict) {
// //         throw ValueError("no match for arguments " + Str(List(dict.keys())));
// //     }

// //     // TODO: set defaults to Tuple(positional_defaults.values()) and update kwdefaults

// // }

// // /* Get a read-only dictionary holding type annotations for the function. */
// // [[nodiscard]] MappingProxy<Dict<Str, Object>> annotations() const {
// //     PyObject* result = PyFunction_GetAnnotations(self());
// //     if (result == nullptr) {
// //         return MappingProxy(Dict<Str, Object>{});
// //     }
// //     return reinterpret_borrow<MappingProxy<Dict<Str, Object>>>(result);
// // }

// // /* Set the type annotations for the function.  If nullopt is provided, then the
// // current annotations will be cleared.  Otherwise, the values in the dictionary will
// // be used to update the current values in-place. */
// // void annotations(std::optional<Dict> annotations) {
// //     if (!annotations.has_value()) {  // clear all annotations
// //         if (PyFunction_SetAnnotations(self(), Py_None)) {
// //             Exception::from_python();
// //         }

// //     } else if (!annotations.value()) {  // do nothing
// //         return;

// //     } else {  // update annotations in-place
// //         Code code = this->code();
// //         Tuple<Str> args = code.varnames()[{0, code.argcount() + code.kwonlyargcount()}];
// //         MappingProxy existing = this->annotations();

// //         // build new dict
// //         Dict result = {};
// //         for (const Object& arg : args) {
// //             if (annotations.value().contains(arg)) {
// //                 result[arg] = annotations.value().pop(arg);
// //             } else if (existing.contains(arg)) {
// //                 result[arg] = existing[arg];
// //             }
// //         }

// //         // account for return annotation
// //         static const Str s_return = "return";
// //         if (annotations.value().contains(s_return)) {
// //             result[s_return] = annotations.value().pop(s_return);
// //         } else if (existing.contains(s_return)) {
// //             result[s_return] = existing[s_return];
// //         }

// //         // check for unmatched keys
// //         if (annotations.value()) {
// //             throw ValueError(
// //                 "no match for arguments " +
// //                 Str(List(annotations.value().keys()))
// //             );
// //         }

// //         // push changes
// //         if (PyFunction_SetAnnotations(self(), ptr(result))) {
// //             Exception::from_python();
// //         }
// //     }
// // }

// /* Set the default value for one or more arguments. */
// template <typename... Values> requires (sizeof...(Values) > 0)  // and all are valid
// void defaults(Values&&... values);

// /* Get a read-only mapping of argument names to their type annotations. */
// [[nodiscard]] MappingProxy<Dict<Str, Object>> annotations() const;

// /* Set the type annotation for one or more arguments. */
// template <typename... Annotations> requires (sizeof...(Annotations) > 0)  // and all are valid
// void annotations(Annotations&&... annotations);


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
        auto meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(self),
            TemplateString<Name>::ptr
        ));
        if (ptr(meth) == nullptr) {
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
        auto meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(Self::type),
            TemplateString<Name>::ptr
        ));
        if (ptr(meth) == nullptr) {
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

}


/////////////////////////
////    OVERLOADS    ////
/////////////////////////


/* An Python-compatible overload set that dispatches to a collection of functions using
an efficient trie-based data structure. */
struct Overload : Object {
private:
    using Base = Object;
    friend class Type<OverloadSet>;

    /* Argument maps are topologically sorted such that parent classes are always
    checked after subclasses. */
    struct Compare {
        bool operator()(PyObject* lhs, PyObject* rhs) const {
            int result = PyObject_IsSubclass(lhs, rhs);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    /* Each node in the trie has 2 maps and a terminal function if this is the last
    argument in the list.  The first map is a topological map of positional arguments
    to the next node in the trie.  The second is a map of keyword names to another
    topological map with the same behavior. */
    struct Node {
        std::map<PyObject*, Node*, Compare> positional;
        std::unordered_map<std::string, std::map<PyObject*, Node*, Compare>> keyword;
        PyObject* func = nullptr;
    };

    /* The Python representation of an Overload object. */
    struct PyOverload {
        PyObject_HEAD
        Node root;  // describes the zeroth arg (return type) of the overload set
        std::unordered_set<PyObject*> funcs;  // all functions in the overload set

        static PyObject* overload(PyObject* self, PyObject* func) {
            PyErr_SetString(PyExc_NotImplementedError, nullptr);
        }

        inline static PyMethodDef methods[] = {
            {
                "overload",
                overload,
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyTypeObject* type = {

        };

    };

public:

    OverloadSet(PyObject* p, borrowed_t t) : Base(p, t) {}
    OverloadSet(PyObject* p, stolen_t t) : Base(p, t) {}

    template <typename... Args> requires (implicit_ctor<OverloadSet>::template enable<Args...>)
    OverloadSet(Args&&... args) : Base(
        implicit_ctor<OverloadSet>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<OverloadSet>::template enable<Args...>)
    explicit OverloadSet(Args&&... args) : Base(
        explicit_ctor<OverloadSet>{},
        std::forward<Args>(args)...
    ) {}

    /* Add an overload from C++. */
    template <typename Return, typename... Target>
    void overload(const Function<Return(Target...)>& func) {

    }

    /* Attach the overload set to a type as a descriptor. */
    template <StaticStr Name, typename T>
    void attach(Type<T>& type) {

    }

    // TODO: index into an overload set to resolve a particular function

};


template <>
struct Type<OverloadSet> : Object {
    using __python__ = OverloadSet::PyOverload;

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::template enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::template enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename Return, typename... Target>
    static void overload(OverloadSet& self, const Function<Return(Target...)>& func) {
        self.overload(func);
    }

    template <StaticStr Name, typename T>
    static void attach(OverloadSet& self, Type<T>& type) {
        self.attach<Name>(type);
    }

};


template <>
struct __init__<Type<OverloadSet>> : Returns<Type<OverloadSet>> {
    static Type<OverloadSet> operator()() {
        return reinterpret_borrow<Type<OverloadSet>>(
            reinterpret_cast<PyObject*>(Type<OverloadSet>::__python__::type)
        );
    }
};


}  // namespace py


#endif
