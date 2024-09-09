#ifndef BERTRAND_PYTHON_CORE_FUNC_H
#define BERTRAND_PYTHON_CORE_FUNC_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"


/// TODO: this file should include the implementation for Object's call operator, just
/// below the dereference operator, at the end of the file.


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

    template <bool optional>
    struct Positional;
    template <bool optional>
    struct Keyword;

    template <bool positional, bool keyword>
    struct Optional : impl::ArgTag {
    private:

        template <bool pos, bool kw>
        struct NoOpt {
            using type = Arg;
        };

        template <>
        struct NoOpt<true, false> {
            using type = Positional<false>;
        };

        template <>
        struct NoOpt<false, true> {
            using type = Keyword<false>;
        };

    public:
        using type = T;
        using no_opt = NoOpt<positional, keyword>::type;
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
    struct KwargPack : ArgTag {
        using key_type = T::key_type;
        using mapped_type = T::mapped_type;
        using type = mapped_type;

        static constexpr StaticStr name = "";
        static constexpr bool is_pos = false;
        static constexpr bool is_kw = false;
        static constexpr bool is_opt = false;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = true;

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
    struct ArgPack : ArgTag {
        using type = iter_type<T>;

        static constexpr StaticStr name = "";
        static constexpr bool is_pos = false;
        static constexpr bool is_kw = false;
        static constexpr bool is_opt = false;
        static constexpr bool is_args = true;
        static constexpr bool is_kwargs = false;

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

    /// TODO: I need to add a parameter for Self, and then handle it carefully in the
    /// call operator.  This won't affect the defaults, since `self` cannot be
    /// optional by definition.  I'm not entirely sure how this needs to work,
    /// especially considering how the std::function wrapper will need to be stored,
    /// but perhaps that's a problem that needs to be solved in the __python__ wrapper
    /// itself, or perhaps in the bindings, via a member dereference operator.  Perhaps
    /// if all you do to implement it is pass a member function pointer, then I can
    /// generate a lambda that captures the member function pointer and adds a `self`
    /// argument, which is dereferenced to forward the remaining arguments.  That would
    /// automate the process and possibly add a layer wherein everything is stored
    /// lambdas rather than std::functions, which could be more efficient and avoid the
    /// only remaining overhead.  For that to work, though, all of the functions would
    /// need to be wrapped in lambdas for consistency, which adds some extra complexity.

    /* Inspect an unannotated C++ argument in a py::Function. */
    template <typename T>
    struct Param : BertrandTag {
        using type                          = T;
        using no_opt                        = T;
        static constexpr StaticStr name     = "";
        static constexpr bool opt           = false;
        static constexpr bool pos           = true;
        static constexpr bool posonly       = true;
        static constexpr bool kw            = false;
        static constexpr bool kwonly        = false;
        static constexpr bool args          = false;
        static constexpr bool kwargs        = false;
    };

    /* Inspect an argument annotation in a py::Function. */
    template <inherits<ArgTag> T>
    struct Param<T> : BertrandTag {
    private:

        template <typename U>
        struct NoOpt {
            using type = T;
        };
        template <typename U> requires (U::is_opt)
        struct NoOpt<U> {
            template <typename T2>
            struct helper {
                using type = U::no_opt;
            };
            template <typename T2>
            struct helper<T2&> {
                using type = U::no_opt&;
            };
            template <typename T2>
            struct helper<T2&&> {
                using type = U::no_opt&&;
            };
            template <typename T2>
            struct helper<const T2> {
                using type = const U::no_opt;
            };
            template <typename T2>
            struct helper<const T2&> {
                using type = const U::no_opt&;
            };
            template <typename T2>
            struct helper<const T2&&> {
                using type = const U::no_opt&&;
            };
            template <typename T2>
            struct helper<volatile T2> {
                using type = volatile U::no_opt;
            };
            template <typename T2>
            struct helper<volatile T2&> {
                using type = volatile U::no_opt&;
            };
            template <typename T2>
            struct helper<volatile T2&&> {
                using type = volatile U::no_opt&&;
            };
            template <typename T2>
            struct helper<const volatile T2> {
                using type = const volatile U::no_opt;
            };
            template <typename T2>
            struct helper<const volatile T2&> {
                using type = const volatile U::no_opt&;
            };
            template <typename T2>
            struct helper<const volatile T2&&> {
                using type = const volatile U::no_opt&&;
            };
            using type = helper<T>;
        };

    public:
        using U                             = std::remove_reference_t<T>;
        using type                          = U::type;
        using no_opt                        = NoOpt<U>::type;
        static constexpr StaticStr name     = U::name;
        static constexpr bool opt           = U::is_opt;
        static constexpr bool pos           = U::is_pos;
        static constexpr bool posonly       = U::is_pos && !U::is_kw;
        static constexpr bool kw            = U::is_kw;
        static constexpr bool kwonly        = U::is_kw && !U::is_pos;
        static constexpr bool args          = U::is_args;
        static constexpr bool kwargs        = U::is_kwargs;
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
        using at = unpack_type<I, Args...>;
        template <size_t I> requires (I < args_idx && I < kwonly_idx)
        using Arg = Param<unpack_type<I, Args...>>::type;
        template <StaticStr Name> requires (has<Name>)
        using Kwarg = Param<unpack_type<idx<Name>, Args...>>::type;

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

        /* After invoking a function with variadic positional arguments, the argument
        iterators must be fully consumed, otherwise there are additional positional
        arguments that were not consumed. */
        template <std::input_iterator Iter, std::sentinel_for<Iter> End>
        static void assert_args_are_exhausted(Iter& iter, const End& end) {
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

        /* Before invoking a function with variadic keyword arguments, those arguments
        need to be scanned to ensure each of them are recognized and do not interfere
        with other keyword arguments given in the source signature. */
        template <typename Outer, typename Inner, typename Map>
        static void assert_kwargs_are_recognized(const Map& kwargs) {
            []<size_t... Is>(std::index_sequence<Is...>, const Map& kwargs) {
                std::vector<std::string> extra;
                for (const auto& [key, value] : kwargs) {
                    if (
                        key == "" ||
                        !((key == Param<typename Outer::template at<Is>>::name) || ...)
                    ) {
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
            }(std::make_index_sequence<Outer::n>{}, kwargs);
        }

    public:

        /* If the target signature does not conform to Python calling conventions, throw
        an informative compile error describing the problem. */
        static constexpr bool valid = validate<0, Args...>;

        /* Check whether the signature contains a keyword with the given name, where
        the name can only be known at runtime. */
        static bool contains(const char* name) {
            return ((std::strcmp(Param<Args>::name, name) == 0) || ...);
        }

        /* A tuple holding the default values for each argument that is marked as
        optional in the enclosing signature. */
        struct Defaults {
        private:
            using Outer = Parameters;

            /* The type of a single value in the defaults tuple.  The templated index
            is used to correlate the default value with its corresponding argument in
            the enclosing signature. */
            template <size_t I>
            struct Value  {
                using annotation = Outer::template at<I>;
                using type = Param<annotation>::type;
                static constexpr StaticStr name = Param<annotation>::name;
                static constexpr size_t index = I;
                std::remove_reference_t<type> value;

                /* Retrieve the default value as its proper type, including reference
                qualifiers. */
                type get() {
                    if constexpr (std::is_rvalue_reference_v<type>) {
                        return std::remove_cvref_t<type>(value);
                    } else {
                        return value;
                    }
                }

                /* Retrieve the default value as its proper type, including reference
                qualifiers. */
                type get() const {
                    if constexpr (std::is_rvalue_reference_v<type>) {
                        return std::remove_cvref_t<type>(value);
                    } else {
                        return value;
                    }
                }

            };

            /* Build a sub-signature holding only the arguments marked as optional from
            the enclosing signature.  This will be a specialization of the enclosing
            class, which is used to bind arguments to this class's constructor using
            the same semantics as the function's call operator. */
            template <typename Sig, typename... Ts>
            struct _Inner { using type = Sig; };
            template <typename... Sig, typename T, typename... Ts>
            struct _Inner<Parameters<Sig...>, T, Ts...> {
                template <typename U>
                struct helper {
                    using type = _Inner<Parameters<Sig...>, Ts...>::type;
                };
                template <typename U> requires (Param<U>::opt)
                struct helper<U> {
                    using type =_Inner<
                        Parameters<Sig..., typename Param<U>::no_opt>,
                        Ts...
                    >::type;
                };
                using type = helper<T>::type;
            };
            using Inner = _Inner<Parameters<>, Args...>::type;

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
            static constexpr size_t n               = Inner::n;
            static constexpr size_t n_pos           = Inner::n_pos;
            static constexpr size_t n_posonly       = Inner::n_posonly;
            static constexpr size_t n_kw            = Inner::n_kw;
            static constexpr size_t n_kwonly        = Inner::n_kwonly;

            template <StaticStr Name>
            static constexpr bool has               = Inner::template has<Name>;
            static constexpr bool has_pos           = Inner::has_pos;
            static constexpr bool has_posonly       = Inner::has_posonly;
            static constexpr bool has_kw            = Inner::has_kw;
            static constexpr bool has_kwonly        = Inner::has_kwonly;

            template <StaticStr Name> requires (has<Name>)
            static constexpr size_t idx             = Inner::template idx<Name>;
            static constexpr size_t kw_idx          = Inner::kw_idx;
            static constexpr size_t kwonly_idx      = Inner::kwonly_idx;

            template <size_t I> requires (I < n)
            using at = Inner::template at<I>;
            template <size_t I> requires (I < kwonly_idx)
            using Arg = Inner::template Arg<I>;
            template <StaticStr Name> requires (has<Name>)
            using Kwarg = Inner::template Kwarg<Name>;

            /* Bind an argument list to the default values tuple using the
            sub-signature's normal Bind<> machinery. */
            template <typename... Values>
            using Bind = Inner::template Bind<Values...>;

            /* Given an index into the enclosing signature, find the corresponding index
            in the defaults tuple if that index corresponds to a default value. */
            template <size_t I> requires (Param<typename Outer::at<I>>::opt)
            static constexpr size_t find = _find<I, Tuple>::value;

        private:

            template <size_t I, typename... Values>
            static constexpr decltype(auto) element(Values&&... values) {
                using observed = Parameters<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    constexpr size_t idx = observed::template idx<name>;
                    return impl::unpack_arg<idx>(std::forward<Values>(values)...).value;

                } else if constexpr (Param<T>::kw) {
                    if constexpr (I < observed::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        constexpr size_t idx = observed::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...).value;
                    }

                } else {
                    return impl::unpack_arg<I>(std::forward<Values>(values)...);
                }
            }

            template <
                size_t I,
                typename... Values,
                std::input_iterator Iter,
                std::sentinel_for<Iter> End
            >
            static constexpr decltype(auto) element(
                size_t args_size,
                Iter& iter,
                const End& end,
                Values&&... values
            ) {
                using observed = Parameters<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    constexpr size_t idx = observed::template idx<name>;
                    return impl::unpack_arg<idx>(std::forward<Values>(values)...).value;

                } else if constexpr (Param<T>::kw) {
                    if constexpr (I < observed::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            if constexpr (observed::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                decltype(auto) result = *iter;
                                ++iter;
                                return result;
                            }

                        } else {
                            if constexpr (observed::template has<name>) {
                                constexpr size_t idx = observed::template idx<name>;
                                return impl::unpack_arg<idx>(
                                    std::forward<Values>(values)...
                                ).value;
                            } else {
                                throw TypeError(
                                    "no match for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else {
                    if constexpr (I < observed::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            decltype(auto) result = *iter;
                            ++iter;
                            return result;
                        } else {
                            throw TypeError(
                                "no match for positional-only parmater at index " +
                                std::to_string(I)
                            );
                        }
                    }
                }
            }

            template <
                size_t I,
                typename... Values,
                typename Map
            >
            static constexpr decltype(auto) element(
                const Map& map,
                Values&&... values
            ) {
                using observed = Parameters<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    auto item = map.find(name);
                    if constexpr (observed::template has<name>) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = observed::template idx<name>;
                        return impl::unpack_arg<idx>(
                            std::forward<Values>(values)...
                        ).value;
                    } else {
                        if (item != map.end()) {
                            return item->second;
                        } else {
                            throw TypeError(
                                "no match for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                    }

                } else if constexpr (Param<T>::kw) {
                    auto item = map.find(name);
                    if constexpr (I < observed::kw_idx) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else if constexpr (observed::template has<name>) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = observed::template idx<name>;
                        return impl::unpack_arg<idx>(
                            std::forward<Values>(values)...
                        ).value;
                    } else {
                        if (item != map.end()) {
                            return item->second;
                        } else {
                            throw TypeError(
                                "no match for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                    }

                } else {
                    return impl::unpack_arg<I>(std::forward<Values>(values)...);
                }
            }

            template <
                size_t I,
                typename... Values,
                std::input_iterator Iter,
                std::sentinel_for<Iter> End,
                typename Map
            >
            static constexpr decltype(auto) element(
                size_t args_size,
                Iter& iter,
                const End& end,
                const Map& map,
                Values&&... values
            ) {
                using observed = Parameters<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    return element<I>(
                        map,
                        std::forward<Values>(values)...
                    );

                } else if constexpr (Param<T>::kw) {
                    auto item = map.find(name);
                    if constexpr (I < observed::kw_idx) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            if constexpr (observed::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                if (item != map.end()) {
                                    throw TypeError(
                                        "conflicting values for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                } else {
                                    decltype(auto) result = *iter;
                                    ++iter;
                                    return result;
                                }
                            }
                        } else {
                            if constexpr (observed::template has<name>) {
                                if (item != map.end()) {
                                    throw TypeError(
                                        "conflicting values for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                } else {
                                    constexpr size_t idx = observed::template idx<name>;
                                    return impl::unpack_arg<idx>(
                                        std::forward<Values>(values)...
                                    ).value;
                                }
                            } else {
                                if (item != map.end()) {
                                    return item->second;
                                } else {
                                    throw TypeError(
                                        "no match for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                }
                            }
                        }
                    }

                } else {
                    return element<I>(
                        args_size,
                        iter,
                        end,
                        std::forward<Values>(values)...
                    );
                }
            }

            template <size_t... Is, typename... Values>
            static constexpr Tuple build(
                std::index_sequence<Is...>,
                Values&&... values
            ) {
                using observed = Parameters<Values...>;

                if constexpr (observed::has_args && observed::has_kwargs) {
                    const auto& kwargs = impl::unpack_arg<observed::kwargs_idx>(
                        std::forward<Values>(values)...
                    );
                    assert_kwargs_are_recognized<Inner, observed>(kwargs);
                    const auto& args = impl::unpack_arg<observed::args_idx>(
                        std::forward<Values>(values)...
                    );
                    auto iter = std::ranges::begin(args);
                    auto end = std::ranges::end(args);
                    Tuple result = {{
                        element<Is>(
                            std::size(args),
                            iter,
                            end,
                            kwargs,
                            std::forward<Values>(values)...
                        )
                    }...};
                    assert_args_are_exhausted(iter, end);
                    return result;

                } else if constexpr (observed::has_args) {
                    const auto& args = impl::unpack_arg<observed::args_idx>(
                        std::forward<Values>(values)...
                    );
                    auto iter = std::ranges::begin(args);
                    auto end = std::ranges::end(args);
                    Tuple result = {{
                        element<Is>(
                            std::size(args),
                            iter,
                            end,
                            std::forward<Values>(values)...
                        )
                    }...};
                    assert_args_are_exhausted(iter, end);
                    return result;

                } else if constexpr (observed::has_kwargs) {
                    const auto& kwargs = impl::unpack_arg<observed::kwargs_idx>(
                        std::forward<Values>(values)...
                    );
                    assert_kwargs_are_recognized<Inner, observed>(kwargs);
                    return {{element<Is>(kwargs, std::forward<Values>(values)...)}...};

                } else {
                    return {{element<Is>(std::forward<Values>(values)...)}...};
                }
            }

        public:
            Tuple values;

            /* The default values' constructor takes Python-style arguments just like
            the call operator, and is only enabled if the call signature is well-formed
            and all optional arguments have been accounted for. */
            template <typename... Values> requires (Bind<Values...>::enable)
            constexpr Defaults(Values&&... values) : values(build(
                std::make_index_sequence<Inner::n>{},
                std::forward<Values>(values)...
            )) {}
            constexpr Defaults(const Defaults& other) = default;
            constexpr Defaults(Defaults&& other) = default;

            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed. */
            template <size_t I> requires (I < n)
            decltype(auto) get() {
                return std::get<I>(values).get();
            }

            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed. */
            template <size_t I> requires (I < n)
            decltype(auto) get() const {
                return std::get<I>(values).get();
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional. */
            template <StaticStr Name> requires (has<Name>)
            decltype(auto) get() {
                return std::get<idx<Name>>(values).get();
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional. */
            template <StaticStr Name> requires (has<Name>)
            decltype(auto) get() const {
                return std::get<idx<Name>>(values).get();
            }

        };

        /* A helper that binds observed arguments to the enclosing signature and
        performs the necessary translation to invoke a matching C++ or Python
        function. */
        template <typename... Values>
        struct Bind {
        private:
            using Outer = Parameters;
            using Inner = Parameters<Values...>;

            template <size_t I>
            using Target = Outer::template at<I>;
            template <size_t J>
            using Source = Inner::template at<J>;

            /* Upon encountering a variadic positional pack in the target signature,
            recursively traverse the remaining source positional arguments and ensure that
            each is convertible to the target type. */
            template <size_t J, typename Pos>
            static constexpr bool consume_target_args = true;
            template <size_t J, typename Pos> requires (J < Inner::kw_idx)
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
            template <size_t J, typename Kw> requires (J < Inner::n)
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
                            Outer::has_args &&
                            Outer::template idx<Param<Source<J>>::name> < Outer::args_idx
                        ) || (
                            !Outer::template has<Param<Source<J>>::name> &&
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
            template <size_t I, typename Pos> requires (I < Outer::n && I <= Outer::args_idx)
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
                        Inner::template has<Param<Target<I>>::name>
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
            template <size_t I, typename Kw> requires (I < Outer::n)
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
            template <size_t I, size_t J> requires (I < Outer::n && J >= Inner::n)
            static constexpr bool enable_recursive<I, J> = [] {
                if constexpr (Param<Target<I>>::args || Param<Target<I>>::opt) {
                    return enable_recursive<I + 1, J>;
                } else if constexpr (Param<Target<I>>::kwargs) {
                    return consume_target_kwargs<
                        Inner::kw_idx,
                        typename Param<Target<I>>::type
                    >;
                }
                return false;
            }();
            template <size_t I, size_t J> requires (I >= Outer::n && J < Inner::n)
            static constexpr bool enable_recursive<I, J> = false;
            template <size_t I, size_t J> requires (I < Outer::n && J < Inner::n)
            static constexpr bool enable_recursive<I, J> = [] {
                // ensure target arguments are present & expand variadic parameter packs
                if constexpr (Param<Target<I>>::pos) {
                    if constexpr (
                        (J >= Inner::kw_idx && !Param<Target<I>>::opt) ||
                        (
                            Param<Target<I>>::name != "" &&
                            Inner::template has<Param<Target<I>>::name>
                        )
                    ) {
                        return false;
                    }
                } else if constexpr (Param<Target<I>>::kwonly) {
                    if constexpr (
                        J < Inner::kw_idx ||
                        (
                            !Inner::template has<Param<Target<I>>::name> &&
                            !Param<Target<I>>::opt
                        )
                    ) {
                        return false;
                    }
                } else if constexpr (Param<Target<I>>::kw) {
                    if constexpr ((
                        J < Inner::kw_idx &&
                        Inner::template has<Param<Target<I>>::name>
                    ) || (
                        J >= Inner::kw_idx &&
                        !Inner::template has<Param<Target<I>>::name> &&
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
                    return enable_recursive<I + 1, Inner::kw_idx>;
                } else if constexpr (Param<Target<I>>::kwargs) {
                    return consume_target_kwargs<
                        Inner::kw_idx,
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
                    if constexpr (Outer::template has<Param<Source<J>>::name>) {
                        using type = Target<Outer::template idx<Param<Source<J>>::name>>;
                        if constexpr (!std::convertible_to<Source<J>, type>) {
                            return false;
                        }
                    } else if constexpr (!Outer::has_kwargs) {
                        using type = Param<Target<Outer::kwargs_idx>>::type;
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
                    return enable_recursive<Outer::args_idx + 1, J + 1>;
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
                using Arg = Source<Inner::kw_idx + I>;
                if constexpr (!Outer::template has<Param<Arg>::name>) {
                    map.emplace(
                        Param<Arg>::name,
                        impl::unpack_arg<Inner::kw_index + I>(
                            std::forward<Source>(args)...
                        )
                    );
                }
            }

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

            template <size_t I>
            static constexpr Param<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                Values&&... values
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    if constexpr (Inner::template has<name>) {
                        constexpr size_t idx = Inner::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                    } else {
                        return defaults.template get<I>();
                    }

                } else if constexpr (Param<T>::kw) {
                    if constexpr (I < Inner::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else if constexpr (Inner::template has<name>) {
                        constexpr size_t idx = Inner::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                    } else {
                        return defaults.template get<I>();
                    }

                } else if constexpr (Param<T>::args) {
                    using Pack = std::vector<type>;
                    Pack vec;
                    if constexpr (I < Inner::kw_idx) {
                        constexpr size_t diff = Inner::kw_idx - I;
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
                            std::forward<Values>(values)...
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
                        std::make_index_sequence<Inner::n - Inner::kw_idx>{},
                        pack,
                        std::forward<Values>(values)...
                    );
                    return pack;

                } else {
                    if constexpr (I < Inner::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        return defaults.template get<I>();
                    }
                }
            }

            template <size_t I, std::input_iterator Iter, std::sentinel_for<Iter> End>
            static constexpr Param<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                size_t args_size,
                Iter& iter,
                const End& end,
                Values&&... values
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(values)...);

                } else if constexpr (Param<T>::kw) {
                    if constexpr (I < Inner::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            if constexpr (Inner::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                decltype(auto) result = *iter;
                                ++iter;
                                return result;
                            }

                        } else {
                            if constexpr (Inner::template has<name>) {
                                constexpr size_t idx = Inner::template idx<name>;
                                return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                            } else {
                                if constexpr (Param<T>::opt) {
                                    return defaults.template get<I>();
                                } else {
                                    throw TypeError(
                                        "no match for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                }
                            }
                        }
                    }

                } else if constexpr (Param<T>::args) {
                    using Pack = std::vector<type>;  /// TODO: can't store references
                    Pack vec;
                    if constexpr (I < Inner::args_idx) {
                        constexpr size_t diff = Inner::args_idx - I;
                        vec.reserve(diff + args_size);
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
                            std::forward<Values>(values)...
                        );
                        vec.insert(vec.end(), iter, end);
                    }
                    return vec;

                } else if constexpr (Param<T>::kwargs) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(values)...);

                } else {
                    if constexpr (I < Inner::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            decltype(auto) result = *iter;
                            ++iter;
                            return result;
                        } else {
                            if constexpr (Param<T>::opt) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "no match for positional-only parameter at index " +
                                    std::to_string(I)
                                );
                            }
                        }
                    }
                }
            }

            template <size_t I, typename Mapping>
            static constexpr Param<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                const Mapping& map,
                Values&&... values
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    auto item = map.find(name);
                    if constexpr (Inner::template has<name>) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = Inner::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                    } else {
                        if (item != map.end()) {
                            return item->second;
                        } else {
                            if constexpr (Param<T>::opt) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "no match for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (Param<T>::kw) {
                    auto item = map.find(name);
                    if constexpr (I < Inner::kw_idx) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else if constexpr (Inner::template has<name>) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = Inner::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                    } else {
                        if (item != map.end()) {
                            return item->second;
                        } else {
                            if constexpr (Param<T>::opt) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "no match for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (Param<T>::args) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(values)...);

                } else if constexpr (Param<T>::kwargs) {
                    using Pack = std::unordered_map<std::string, type>;
                    Pack pack;
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        Pack& pack,
                        Values&&... values
                    ) {
                        (build_kwargs<Js>(pack, std::forward<Values>(values)...), ...);
                    }(
                        std::make_index_sequence<Inner::n - Inner::kw_idx>{},
                        pack,
                        std::forward<Values>(values)...
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
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(values)...);
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
                size_t args_size,
                Iter& iter,
                const End& end,
                const Mapping& map,
                Values&&... values
            ) {
                using T = Target<I>;
                using type = Param<T>::type;
                constexpr StaticStr name = Param<T>::name;

                if constexpr (Param<T>::kwonly) {
                    return cpp_to_cpp<I>(
                        defaults,
                        map,
                        std::forward<Values>(values)...
                    );

                } else if constexpr (Param<T>::kw) {
                    auto item = map.find(name);
                    if constexpr (I < Inner::kw_idx) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            if constexpr (Inner::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                if (item != map.end()) {
                                    throw TypeError(
                                        "conflicting values for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                } else {
                                    decltype(auto) result = *iter;
                                    ++iter;
                                    return result;
                                }
                            }
                        } else {
                            if constexpr (Inner::template has<name>) {
                                if (item != map.end()) {
                                    throw TypeError(
                                        "conflicting values for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                } else {
                                    constexpr size_t idx = Inner::template idx<name>;
                                    return impl::unpack_arg<idx>(
                                        std::forward<Values>(values)...
                                    ).value;
                                }
                            } else {
                                if (item != map.end()) {
                                    return item->second;
                                } else {
                                    if constexpr (Param<T>::opt) {
                                        return defaults.template get<I>();
                                    } else {
                                        throw TypeError(
                                            "no match for parameter '" + name +
                                            "' at index " + std::to_string(I)
                                        );
                                    }
                                }
                            }
                        }
                    }

                } else if constexpr (Param<T>::args) {
                    return cpp_to_cpp<I>(
                        defaults,
                        args_size,
                        iter,
                        end,
                        std::forward<Values>(values)...
                    );

                } else if constexpr (Param<T>::kwargs) {
                    return cpp_to_cpp<I>(
                        defaults,
                        map,
                        std::forward<Values>(values)...
                    );

                } else {
                    return cpp_to_cpp<I>(
                        defaults,
                        args_size,
                        iter,
                        end,
                        std::forward<Values>(values)...
                    );
                }
            }

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
                        auto sequence = std::make_index_sequence<Outer::n_kw>{};
                        for (size_t i = 0; i < kwcount; ++i) {
                            Py_ssize_t length;
                            const char* kwname = PyUnicode_AsUTF8AndSize(
                                PyTuple_GET_ITEM(kwnames, i),
                                &length
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (!Outer::contains(kwname)) {
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
                            J - Inner::kw_idx,
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
                            PyTuple_SET_ITEM(kwnames, curr - Inner::kw_idx, name);
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

        public:
            static constexpr size_t n               = sizeof...(Values);
            static constexpr size_t n_pos           = Inner::n_pos;
            static constexpr size_t n_kw            = Inner::n_kw;

            template <StaticStr Name>
            static constexpr bool has               = Inner::template has<Name>;
            static constexpr bool has_pos           = Inner::has_pos;
            static constexpr bool has_kw            = Inner::has_kw;
            static constexpr bool has_args          = Inner::has_args;
            static constexpr bool has_kwargs        = Inner::has_kwargs;

            template <StaticStr Name> requires (has<Name>)
            static constexpr size_t idx             = Inner::template idx<Name>;
            static constexpr size_t kw_idx          = Inner::kw_idx;
            static constexpr size_t args_idx        = Inner::args_idx;
            static constexpr size_t kwargs_idx      = Inner::kwargs_idx;

            template <size_t I> requires (I < n)
            using at = Inner::template at<I>;
            template <size_t I> requires (I < kw_idx)
            using Arg = Inner::template Arg<I>;
            template <StaticStr Name> requires (has<Name>)
            using Kwarg = Inner::template Kwarg<Name>;

            /* Call operator is only enabled if source arguments are well-formed and
            match the target signature. */
            static constexpr bool enable = Inner::valid && enable_recursive<0, 0>;

            /// TODO: both of these call operators need to be updated to handle the
            /// `self` parameter.

            /* Invoke an external C++ function using the given arguments and default
            values. */
            template <typename Func>
                requires (enable && std::is_invocable_v<Func, Args...>)
            static auto operator()(
                const Defaults& defaults,
                Func&& func,
                Values&&... values
            ) -> std::invoke_result_t<Func, Args...> {
                using Return = std::invoke_result_t<Func, Args...>;
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    const Defaults& defaults,
                    Func&& func,
                    Values&&... values
                ) {
                    if constexpr (Inner::has_args && Inner::has_kwargs) {
                        const auto& kwargs = impl::unpack_arg<Inner::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        if constexpr (!Outer::has_kwargs) {
                            assert_kwargs_are_recognized<Outer, Inner>(kwargs);
                        }
                        const auto& args = impl::unpack_arg<Inner::args_idx>(
                            std::forward<Values>(values)...
                        );
                        auto iter = std::ranges::begin(args);
                        auto end = std::ranges::end(args);
                        if constexpr (std::is_void_v<Return>) {
                            func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::size(args),
                                    iter,
                                    end,
                                    kwargs,
                                    std::forward<Values>(values)...
                                )
                            }...);
                            if constexpr (!Outer::has_args) {
                                assert_args_are_exhausted(iter, end);
                            }
                        } else {
                            decltype(auto) result = func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::size(args),
                                    iter,
                                    end,
                                    kwargs,
                                    std::forward<Values>(values)...
                                )
                            }...);
                            if constexpr (!Outer::has_args) {
                                assert_args_are_exhausted(iter, end);
                            }
                            return result;
                        }

                    // variadic positional arguments are passed as an iterator range, which
                    // must be exhausted after the function call completes
                    } else if constexpr (Inner::has_args) {
                        const auto& args = impl::unpack_arg<Inner::args_idx>(
                            std::forward<Values>(values)...
                        );
                        auto iter = std::ranges::begin(args);
                        auto end = std::ranges::end(args);
                        if constexpr (std::is_void_v<Return>) {
                            func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::size(args),
                                    iter,
                                    end,
                                    std::forward<Values>(values)...
                                )
                            }...);
                            if constexpr (!Outer::has_args) {
                                assert_args_are_exhausted(iter, end);
                            }
                        } else {
                            decltype(auto) result = func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::size(args),
                                    iter,
                                    end,
                                    std::forward<Source>(values)...
                                )
                            }...);
                            if constexpr (!Outer::has_args) {
                                assert_args_are_exhausted(iter, end);
                            }
                            return result;
                        }

                    // variadic keyword arguments are passed as a dictionary, which must be
                    // validated up front to ensure all keys are recognized
                    } else if constexpr (Inner::has_kwargs) {
                        const auto& kwargs = impl::unpack_arg<Inner::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        if constexpr (!Outer::has_kwargs) {
                            assert_kwargs_are_recognized<Outer, Inner>(kwargs);
                        }
                        if constexpr (std::is_void_v<Return>) {
                            func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    kwargs,
                                    std::forward<Values>(values)...
                                )
                            }...);
                        } else {
                            return func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    kwargs,
                                    std::forward<Values>(values)...
                                )
                            }...);
                        }

                    // interpose the two if there are both positional and keyword argument packs
                    } else {
                        if constexpr (std::is_void_v<Return>) {
                            func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::forward<Source>(values)...
                                )
                            }...);
                        } else {
                            return func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::forward<Source>(values)...
                                )
                            }...);
                        }
                    }
                }(
                    std::make_index_sequence<Outer::n>{},
                    defaults,
                    std::forward<Func>(func),
                    std::forward<Source>(values)...
                );
            }

            /* Invoke an external Python function using the given arguments.  This will
            always return a new reference to a raw Python object, or throw a runtime
            error if the arguments are malformed in some way. */
            template <typename = void> requires (enable)
            static PyObject* operator()(
                PyObject* func,
                Values&&... values
            ) {
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyObject* func,
                    Values&&... values
                ) {
                    PyObject* result;

                    // if there are no arguments, we can use the no-args protocol
                    if constexpr (Inner::n == 0) {
                        result = PyObject_CallNoArgs(func);

                    // if there are no variadic arguments, we can stack allocate the argument
                    // array with a fixed size
                    } else if constexpr (!Inner::has_args && !Inner::has_kwargs) {
                        // if there is only one argument, we can use the one-arg protocol
                        if constexpr (Inner::n == 1) {
                            if constexpr (Inner::has_kw) {
                                result = PyObject_CallOneArg(
                                    func,
                                    ptr(as_object(
                                        impl::unpack_arg<0>(
                                            std::forward<Values>(values)...
                                        ).value
                                    ))
                                );
                            } else {
                                result = PyObject_CallOneArg(
                                    func,
                                    ptr(as_object(
                                        impl::unpack_arg<0>(
                                            std::forward<Values>(values)...
                                        )
                                    ))
                                );
                            }

                        // if there is more than one argument, we construct a vectorcall
                        // argument array
                        } else {
                            PyObject* array[Inner::n + 1];
                            array[0] = nullptr;
                            PyObject* kwnames;
                            if constexpr (Inner::has_kw) {
                                kwnames = PyTuple_New(Inner::n_kw);
                            } else {
                                kwnames = nullptr;
                            }
                            (
                                cpp_to_python<Is>(
                                    kwnames,
                                    array,  // 1-indexed
                                    std::forward<Values>(values)...
                                ),
                                ...
                            );
                            Py_ssize_t npos = Inner::n - Inner::n_kw;
                            result = PyObject_Vectorcall(
                                func,
                                array,
                                npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                kwnames
                            );
                            for (size_t i = 1; i <= Inner::n; ++i) {
                                Py_XDECREF(array[i]);  // release all argument references
                            }
                        }

                    // otherwise, we have to heap-allocate the array with a variable size
                    } else if constexpr (Inner::has_args && !Inner::has_kwargs) {
                        const auto& args = impl::unpack_arg<Inner::args_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs = Inner::n - 1 + std::size(args);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        PyObject* kwnames;
                        if constexpr (Inner::has_kw) {
                            kwnames = PyTuple_New(Inner::n_kw);
                        } else {
                            kwnames = nullptr;
                        }
                        (
                            cpp_to_python<Is>(
                                kwnames,
                                array,
                                std::forward<Values>(values)...
                            ),
                            ...
                        );
                        Py_ssize_t npos = Inner::n - 1 - Inner::n_kw + std::size(args);
                        result = PyObject_Vectorcall(
                            func,
                            array,
                            npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        for (size_t i = 1; i <= Inner::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;

                    // The following specializations handle the cross product of
                    // positional/keyword parameter packs which differ only in initialization
                    } else if constexpr (!Inner::has_args && Inner::has_kwargs) {
                        const auto& kwargs = impl::unpack_arg<Inner::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs = Inner::n - 1 + std::size(kwargs);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        PyObject* kwnames = PyTuple_New(Inner::n_kw + std::size(kwargs));
                        (
                            cpp_to_python<Is>(
                                kwnames,
                                array,
                                std::forward<Values>(values)...
                            ),
                            ...
                        );
                        Py_ssize_t npos = Inner::n - 1 - Inner::n_kw;
                        result = PyObject_Vectorcall(
                            func,
                            array,
                            npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        );
                        for (size_t i = 1; i <= Inner::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;

                    } else {
                        const auto& args = impl::unpack_arg<Inner::args_idx>(
                            std::forward<Values>(values)...
                        );
                        const auto& kwargs = impl::unpack_arg<Inner::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs = Inner::n - 2 + std::size(args) + std::size(kwargs);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        PyObject* kwnames = PyTuple_New(Inner::n_kw + std::size(kwargs));
                        (
                            cpp_to_python<Is>(
                                kwnames,
                                array,
                                std::forward<Values>(values)...
                            ),
                            ...
                        );
                        size_t npos = Inner::n - 2 - Inner::n_kw + std::size(args);
                        result = PyObject_Vectorcall(
                            func,
                            array,
                            npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        );
                        for (size_t i = 1; i <= Inner::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;
                    }

                    // A null return value indicates an error
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return result;  // will be None if the function returns void
                }(
                    std::make_index_sequence<Outer::n>{},
                    func,
                    std::forward<Values>(values)...
                );
            }

        };

    };

    /* Convert a non-member function pointer into a member function pointer of the
    given, cvref-qualified type.  Passing void as the enclosing class will return the
    non-member function pointer as-is. */
    template <typename Func, typename Self>
    struct _func_as_member;
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct _func_as_member<R(*)(A...), Self> {
        using type = R(*)(A...);
    };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct _func_as_member<R(*)(A...) noexcept, Self> {
        using type = R(*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), Self> {
        using type = R(std::remove_cvref_t<Self>::*)(A...);
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, Self> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), Self&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) &;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, Self&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), Self&&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) &&;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, Self&&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), const Self> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, const Self> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), const Self&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, const Self&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), const Self&&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &&;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, const Self&&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), volatile Self> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, volatile Self> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), volatile Self&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, volatile Self&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), volatile Self&&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, volatile Self&&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), const volatile Self> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, const volatile Self> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), const volatile Self&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, const volatile Self&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...), const volatile Self&&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct _func_as_member<R(*)(A...) noexcept, const volatile Self&&> {
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile && noexcept;
    };
    template <typename Func, typename Self>
    using func_as_member = typename _func_as_member<Func, Self>::type;

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer, reference, or object, such as a lambda type. */
    template <typename R, typename... A>
    struct Signature<R(A...)> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = void;
        using Args = Parameters<A...>;
        using type = R(A...);
        using to_ptr = Signature<R(*)(A...)>;
        using to_value = Signature;
        template <typename R2>
        using with_return = Signature<R2(A...)>;
        template <typename C>
        using with_self = Signature<func_as_member<R(*)(A...), C>>;
        template <typename... A2>
        using with_args = Signature<R(A2...)>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, A...>;
    };
    template <typename R, typename... A>
    struct Signature<R(A...) noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = void;
        using Args = Parameters<A...>;
        using type = R(A...) noexcept;
        using to_ptr = Signature<R(*)(A...) noexcept>;
        using to_value = Signature;
        template <typename R2>
        using with_return = Signature<R2(A...) noexcept>;
        template <typename C>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C>>;
        template <typename... A2>
        using with_args = Signature<R(A2...) noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, A...>;
    };
    template <typename R, typename... A>
    struct Signature<R(*)(A...)> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = void;
        using Args = Parameters<A...>;
        using type = R(*)(A...);
        using to_ptr = Signature;
        using to_value = Signature<R(A...)>;
        template <typename R2>
        using with_return = Signature<R2(*)(A...)>;
        template <typename C>
        using with_self = Signature<func_as_member<R(*)(A...), C>>;
        template <typename... A2>
        using with_args = Signature<R(*)(A2...)>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, A...>;
    };
    template <typename R, typename... A>
    struct Signature<R(*)(A...) noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = void;
        using Args = Parameters<A...>;
        using type = R(*)(A...) noexcept;
        using to_ptr = Signature;
        using to_value = Signature<R(A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(*)(A...) noexcept>;
        template <typename C>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C>>;
        template <typename... A2>
        using with_args = Signature<R(*)(A2...) noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...)> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...);
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...)>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...)>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) &> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = true;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...);
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) &>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) &>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        using Return = R;
        using Self = C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...);
        using to_ptr = Signature<R(*)(Self, A...) noexcept>;
        using to_value = Signature<R(Self, A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) noexcept>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) & noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = true;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...);
        using to_ptr = Signature<R(*)(Self, A...) noexcept>;
        using to_value = Signature<R(Self, A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) & noexcept>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) & noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = const C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) const;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const &> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = true;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = const C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) const;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const &>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const &>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = const C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) const;
        using to_ptr = Signature<R(*)(Self, A...) noexcept>;
        using to_value = Signature<R(Self, A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const noexcept>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const & noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = true;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = const C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) const;
        using to_ptr = Signature<R(*)(Self, A...) noexcept>;
        using to_value = Signature<R(Self, A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const & noexcept>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const & noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = volatile C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) volatile;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) volatile>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile &> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = true;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = volatile C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) volatile;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) volatile &>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile &>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = volatile C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) volatile;
        using to_ptr = Signature<R(*)(Self, A...) noexcept>;
        using to_value = Signature<R(Self, A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) volatile noexcept>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile & noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = true;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = volatile C&;
        using Args = impl::Parameters<A...>;
        using type = R(C::*)(A...) volatile;
        using to_ptr = Signature<R(*)(Self, A...) noexcept>;
        using to_value = Signature<R(Self, A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) volatile & noexcept>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile & noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = const volatile C&;
        using Args = impl::Parameters<A...>;
        using type = R(C::*)(A...) const volatile;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const volatile>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile &> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = false;
        static constexpr bool has_lvalue = true;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = const volatile C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) const volatile;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const volatile &>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile &>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = false;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = const volatile C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) const volatile;
        using to_ptr = Signature<R(*)(Self, A...) noexcept>;
        using to_value = Signature<R(Self, A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const volatile noexcept>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile & noexcept> : Parameters<A...> {
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool has_noexcept = true;
        static constexpr bool has_lvalue = true;
        static constexpr bool has_rvalue = false;
        using Return = R;
        using Self = const volatile C&;
        using Args = Parameters<A...>;
        using type = R(C::*)(A...) const volatile;
        using to_ptr = Signature<R(*)(Self, A...) noexcept>;
        using to_value = Signature<R(Self, A...) noexcept>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const volatile & noexcept>;
        template <typename C2>
        using with_self = Signature<func_as_member<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile & noexcept>;
        template <typename Func>
        static constexpr bool compatible = std::is_invocable_r_v<R, Func, Self, A...>;
    };
    template <impl::has_call_operator T> requires (Signature<decltype(&T::operator())>::enable)
    struct Signature<T> : Signature<decltype(&T::operator())>::template with_self<void>::Args {
    private:
        using Parent = Signature<decltype(&T::operator())>::template with_self<void>;

    public:
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool has_noexcept = Parent::has_noexcept;
        static constexpr bool has_lvalue = Parent::has_lvalue;
        static constexpr bool has_rvalue = Parent::has_rvalue;
        using Return = Parent::Return;
        using Self = Parent::Self;
        using Args = Parent::Args;
        using type = T;
        using to_ptr = Parent::to_ptr;
        using to_value = Parent::to_value;
        template <typename R>
        using with_return = Parent::template with_return<R>;
        template <typename C>
        using with_self = Parent::template with_self<C>;
        template <typename... A>
        using with_args = Parent::template with_args<A...>;
        template <typename Func>
        static constexpr bool compatible = Parent::template compatible<Func>;
    };

    /* The function's Python representation, which is separated from the Functions
    themselves to promote reuse.  This is the standard implementation, which stores
    the function as a `std::function` wrapper, which introduces a small amount of
    overhead. */
    template <typename Sig>
    struct PyFunction : PyObject {
        using Defaults = Sig::Defaults;

        std::string name;
        std::string docstring;
        std::function<typename Sig::to_value::type> func;
        Defaults defaults;
        vectorcallfunc call;

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type> func,
            Defaults defaults
        ) : name(std::move(name)),
            docstring(std::move(docstring)),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call()  /// TODO: assign this
        {}

        /// TODO: implement Python-level call operator.

    };

    /* A specialization for functions and function objects that can be implicitly
    converted to a function pointer, in which case we can avoid the `std::function`
    wrapper and associated overhead. */
    template <typename Sig>
        requires (
            !Sig::has_self &&
            std::convertible_to<typename Sig::type, typename Sig::to_ptr::type>
        )
    struct PyFunction<Sig> : PyObject {
        using Defaults = Sig::Defaults;

        std::string name;
        std::string docstring;
        typename Sig::to_ptr::type func;
        Defaults defaults;
        vectorcallfunc call;

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            typename Sig::to_ptr::type func,
            Defaults&& defaults
        ) : name(std::move(name)),
            docstring(std::move(docstring)),
            func(func),
            defaults(std::move(defaults)),
            call()  /// TODO: assign this
        {}

        /// TODO: implement Python-level call operator.

    };

    /* A specialization for member functions expecting a `self` parameter. */
    template <typename Sig> requires (Sig::has_self)
    struct PyFunction<Sig> : PyObject {
        using Defaults = Sig::Defaults;

        std::optional<std::remove_reference_t<typename Sig::Self>> self;
        std::string name;
        std::string docstring;
        std::function<typename Sig::to_value::type> func;
        Defaults defaults;
        vectorcallfunc call;

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Defaults&& defaults
        ) : self(std::nullopt),
            name(std::move(name)),
            docstring(std::move(docstring)),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call()  /// TODO: assign this
        {}

        PyFunction(
            std::remove_reference_t<typename Sig::Self>&& self,
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Defaults&& defaults
        ) : self(std::move(self)),
            name(std::move(name)),
            docstring(std::move(docstring)),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call()  /// TODO: assign this
        {}

        /// TODO: implement Python-level call operator.

    };

}


/// TODO: Signature's semantics have changed slightly, now all the conversions return
/// new Signature types, and Signature::type is used to get the underlying function
/// type.  This needs to be accounted for in the Function<> specializations, and there
/// needs to be a separate Function<> type for each Signature specialization, so that
/// type information is never lost.






/// TODO: maximum efficiency would require me to implement separate overloads for
/// py::Function based on whether you supply a function pointer or a stateless lambda
/// which can be converted to a function pointer, in which case I store that within
/// the Python representation and forward calls accordingly.  That would allow me to
/// avoid type erasure, which would yield the same performance as a direct function
/// call, with all the same inlining and optimizations that would be applied at the
/// C++ level.  Capturing lambdas and other function objects would have to go through
/// std::function instead, which would pay the extra indirection cost, but would still
/// allow `py::Function` to represent both cases as greedily as possible.
/// -> The way to implement this is to check whether the argument has a call operator,
/// and can be implicitly converted to the function pointer type.  If so, then I can
/// use the happy path, and will happily do so.
/// -> Note that for member functions, I would have to store two pointers, one for the
/// member function and another for its static equivalent, where self is passed as the
/// first argument.  If a matching member function is passed, then I store it in the
/// first pointer and then generate a wrapper that's stored in the second slot and
/// simply dereferences the first pointer on the self argument.  If a static function
/// pointer or an object that can be converted to such a pointer is given, then I
/// store it directly in the second slot and leave the first slot null.  When the
/// function is called, only the second slot will ever actually be used.  In the
/// Python case, I'll check for a self argument in the vectorcall array, and in the
/// C++ case, I would just inject the self argument into the constructor, and then
/// pass it into the invocation function as the first argument, which calls the
/// static function pointer and so on and so forth.  That means member functions are
/// perfectly represented, and are constructible from both function pointers and
/// callable objects, which may be implemented using static syntax.


/// TODO: I actually can't create a static wrapper around a member function pointer
/// because I can't capture the member function within the static function.  What I can
/// do is outlaw passing member functions entirely, and force the user to always
/// provide a static version, with the self argument explicitly specified.  I can still
/// do a pointer optimization for this case, as long as the function can be converted to
/// such a pointer, but ALL other cases will have to use std::function instead.


/// TODO: I would also need some way to disambiguate static functions from member
/// functions when doing CTAD.  This is probably accomplished by providing an extra
/// argument to the constructor which holds the `self` value, and is implicitly
/// convertible to the function's first parameter type.  In that case, the CTAD
/// guide would always deduce to a member function over a static function.  If the
/// extra argument is given and is not convertible to the first parameter type, then
/// we issue a compile error, and if the extra argument is not given at all, then we
/// interpret it as a static function.


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
    using Defaults = impl::Signature<F>::Defaults;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R>
    using with_return = Function<typename impl::Signature<F>::template with_return<R>>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
    using with_args = Function<typename impl::Signature<F>::template with_args<A...>>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
    using with_self = Function<typename impl::Signature<F>::template with_self<C>>;

    /* Check whether a target function can be called with this function signature, i.e.
    that it can be invoked with this function's parameters and returns a type that
    can be converted to this function's return type. */
    template <typename Func>
    static constexpr bool compatible = impl::Signature<F>::template compatible<Func>;

    /* Check whether the function is invocable with the given arguments at
    compile-time. */
    template <typename... Args>
    static constexpr bool invocable = impl::Signature<F>::template Bind<Args...>::enable;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = impl::Signature<F>::n;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = impl::Signature<F>::n_pos;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = impl::Signature<F>::n_posonly;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = impl::Signature<F>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = impl::Signature<F>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = impl::Signature<F>::n_opt;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = impl::Signature<F>::n_opt_pos;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = impl::Signature<F>::n_opt_posonly;

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

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = impl::Signature<F>::has_pos;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = impl::Signature<F>::has_posonly;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = impl::Signature<F>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = impl::Signature<F>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = impl::Signature<F>::has_opt;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = impl::Signature<F>::has_opt_pos;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = impl::Signature<F>::has_opt_posonly;

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

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = impl::Signature<F>::opt_pos_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = impl::Signature<F>::opt_posonly_index;

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

    /* Get the (possibly annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = impl::Signature<F>::template at<I>;

    /* Get the type of the positional argument at index I. */
    template <size_t I> requires (I < args_idx && I < kwonly_idx)
    using Arg = impl::Signature<F>::template Arg<I>;

    /* Get the type of the named keyword argument. */
    template <StaticStr Name> requires (has<Name>)
    using Kwarg = impl::Signature<F>::template Kwarg<Name>;

    /* Get the type expected by the function's variadic positional arguments, or void
    if it does not accept variadic positional arguments. */
    using Args = impl::Signature<F>::Args;

    /* Get the type expected by the function's variadic keyword arguments, or void if
    it does not accept variadic keyword arguments. */
    using Kwargs = impl::Signature<F>::Kwargs;

    /* Call an external C++ function that matches the target signature using
    Python-style arguments.  The default values (if any) must be provided as an
    initializer list immediately before the function to be invoked, or constructed
    elsewhere and passed by reference.
    
    This helper has no runtime overhead over a traditional C++ function call, not
    counting any implicit logic when constructing default values. */
    template <typename Func, typename... Args>
        requires (compatible<Func> && invocable<Args...>)
    static Return invoke(const Defaults& defaults, Func&& func, Args&&... args) {
        using Call = impl::Signature<F>::template Bind<Args...>;
        return Call{}(
            defaults,
            std::forward<Func>(func),
            std::forward<Args>(args)...
        );
    }

    /* Call an external Python function using Python-style arguments.  The optional
    `R` template parameter specifies an interim return type, which is used to interpret
    the result of the raw CPython call.  The interim result will then be implicitly
    converted to the function's final return type.  The default value is `Object`,
    which incurs an additional dynamic type check on conversion.  If the exact return
    type is known in advance, then setting `R` equal to that type will avoid any extra
    checks or conversions at the expense of safety if that type is incorrect.  */
    template <typename R = Object, typename... Args> requires (invocable<Args...>)
    static Return invoke(PyObject* func, Args&&... args) {
        static_assert(
            !std::is_reference_v<R>,
            "Interim return type must not be a reference"
        );
        static_assert(
            std::derived_from<R, Object>,
            "Interim return type must be a subclass of Object"
        );

        /// TODO: this type check gets wierd.  I'll have to revisit it when I immerse
        /// myself in the type system again.  I need a way to traverse from
        /// `Type<Function>` to the parent template interface, so this has the lowest
        /// overhead possible.

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

        using Call = impl::Signature<F>::template Bind<Args...>;
        PyObject* result = Call{}(
            func,
            std::forward<Args>(args)...
        );

        // Python void functions return a reference to None, which we must release
        if constexpr (std::is_void_v<Return>) {
            Py_DECREF(result);
        } else {
            return reinterpret_steal<R>(result);
        }
    }

    /// TODO: when getting and setting these properties, do I need to use an Attr
    /// proxy?


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
template <typename F> requires (impl::Signature<F>::enable)
struct Interface<Type<Function<F>>> {
    static_assert(impl::Signature<F>::valid, "invalid function signature");

    /* The normalized function pointer type for this specialization. */
    using Signature = Interface<Function<F>>::Signature;

    /* The function's return type. */
    using Return = Interface<Function<F>>::Return;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = Interface<Function<F>>::Self;

    /* A type holding the function's default values, which are inferred from the input
    signature and stored as a `std::tuple`. */
    using Defaults = Interface<Function<F>>::Defaults;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = Interface<Function<F>>::n;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = Interface<Function<F>>::n_pos;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = Interface<Function<F>>::n_posonly;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = Interface<Function<F>>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = Interface<Function<F>>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = Interface<Function<F>>::n_opt;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = Interface<Function<F>>::n_opt_pos;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = Interface<Function<F>>::n_opt_posonly;

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

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = Interface<Function<F>>::has_pos;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = Interface<Function<F>>::has_posonly;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = Interface<Function<F>>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = Interface<Function<F>>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = Interface<Function<F>>::has_opt;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = Interface<Function<F>>::has_opt_pos;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = Interface<Function<F>>::has_opt_posonly;

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

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = Interface<Function<F>>::opt_pos_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = Interface<Function<F>>::opt_posonly_index;

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

    /* Get the type of the positional argument at index I. */
    template <size_t I> requires (I < args_idx && I < kwonly_idx)
    using Arg = Interface<Function<F>>::template Arg<I>;

    /* Get the type of the named keyword argument. */
    template <StaticStr Name> requires (has<Name>)
    using Kwarg = Interface<Function<F>>::template Kwarg<Name>;

    /* Get the type expected by the function's variadic positional arguments, or void
    if it does not accept variadic positional arguments. */
    using Args = Interface<Function<F>>::Args;

    /* Get the type expected by the function's variadic keyword arguments, or void if
    it does not accept variadic keyword arguments. */
    using Kwargs = Interface<Function<F>>::Kwargs;




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


/// TODO: this is all going to take some pretty deep thought to get correct.  Perhaps
/// every kind of valid signature needs a separate specialization, so that the
/// Interface<> traits always have the full template signature available to them.
/// -> I could potentially lift the __python__ type into impl:: just below Signature
/// in order to facilitate this.  It would have two basic forms, one which stores the
/// function as a pointer, and the other which stores it as a std::function.  They
/// have the same general interface otherwise, and all the function specializations
/// would inherit from this type to avoid reimplementing everything too much.
/// -> Maybe it's not possible to fully decouple everything, but I can at least try to
/// avoid repeating myself as much as possible.  I might have to take a peacemeal
/// approach to do this, though, unless I pipe in all the dependent types as template
/// parameters, which might actually just work.  Each function specialization would
/// probably need to write its own __export__ script, however, since I can't get
/// access to the def<> helper from the outside.


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
)> requires (impl::Signature<F>::enable)
struct Function : Function<typename impl::Signature<F>::type> {};


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
private:
    using Interface = Interface<Function<Return(*)(Target...)>>;

public:
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
        Interface::Defaults defaults;
        vectorcallfunc vectorcall;

        __python__(
            std::string&& name,
            std::string&& docstring,
            std::function<Return(Target...)>&& func,
            Interface::Defaults&& defaults
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
struct __object__<R(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename... A>
struct __object__<R(*)(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __object__<R(C::*)(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __object__<R(C::*)(A...) noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __object__<R(C::*)(A...) const> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __object__<R(C::*)(A...) const noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __object__<R(C::*)(A...) volatile> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __object__<R(C::*)(A...) volatile noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __object__<R(C::*)(A...) const volatile> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __object__<R(C::*)(A...) const volatile noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename... A>
struct __object__<std::function<R(A...)>> : Returns<Function<R(A...)>> {};


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


/* Call the function with the given arguments.  If the wrapped function is of the
coupled Python type, then this will be translated into a raw C++ call, bypassing
Python entirely. */
template <impl::inherits<impl::FunctionTag> Self, typename... Args>
    requires (std::remove_reference_t<Self>::invocable<Args...>)
struct __call__<Self, Args...> : Returns<typename std::remove_reference_t<Self>::Return> {
    using Func = std::remove_reference_t<Self>;
    static decltype(auto) operator()(Self&& self, Args&&... args) {
        if constexpr (std::is_void_v<typename Func::Return>) {
            invoke(ptr(self), std::forward<Source>(args)...);
        } else {
            return invoke(ptr(self), std::forward<Source>(args)...);
        }
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
