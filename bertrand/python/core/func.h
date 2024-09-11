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

        [[nodiscard]] operator T() && {
            if constexpr (std::is_lvalue_reference_v<T>) {
                return value;
            } else {
                return std::move(value);
            }
        }
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

        [[nodiscard]] operator T() && {
            if constexpr (std::is_lvalue_reference_v<T>) {
                return value;
            } else {
                return std::move(value);
            }
        }
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

        [[nodiscard]] operator T() && {
            if constexpr (std::is_lvalue_reference_v<T>) {
                return value;
            } else {
                return std::move(value);
            }
        }
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

        [[nodiscard]] operator std::vector<type>() && {
            return std::move(value);
        }
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

        [[nodiscard]] operator std::unordered_map<std::string, T>() && {
            return std::move(value);
        }
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

    [[nodiscard]] operator T() && {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return value;
        } else {
            return std::move(value);
        }
    }
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


/// TODO: list all the special rules of container unpacking here?


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

    /// TODO: Args... now includes the `self` argument as the first argument, which
    /// should be fine, but may require adjustments to call logic.  It won't affect
    /// any of the compile-time logic, nor should it change the `Defaults` class, since
    /// the `self` argument cannot be optional by definition.
    /// -> It may require adjustments to std::is_invocable_r_v<> in order to account
    /// for member function pointers, which do not include the `self` argument as
    /// written (this is handled by casting the input function type to its ptr
    /// equivalent before proceeding with the check).  The only other change might be
    /// in how the Python call protocols are invoked with respect to the descriptor
    /// protocol and NoArgs/OneArg protocols.  Maybe the answer is to just uniformly
    /// use the vectorcall protocol and place self on the special -1 index, which would
    /// be fairly consistent.
    /// -> The invoke() function is going to need to account for member functions being
    /// passed in?  Maybe not?  If it's a member function, then the first argument is
    /// always going to be the self argument, and then I just need to dereference the
    /// function pointer to create the actual function call.  I have both parts already,
    /// so it should be a simple matter of combining them.  That combination will
    /// likely need a constexpr branch to account for it, as well as perhaps adjusting
    /// the index sequence to account for it in the final call.  Rather than passing
    /// all arguments on to the final call, I would exclude the first, which must be
    /// provided according to the compile-time constraints.
    /// -> The same adjustment would need to be made for the C++ to python argument
    /// translation, wherein I need to allocate a vectorcall argument array.

    /// TODO: variadic args/kwargs will likely need handling when performing
    /// `compatible` checks.  A function that accepts arbitrary args/kwargs is, by
    /// definition, compatible with any other function, since it can accept any
    /// arguments, and all other functions accept only a subset.  compatible<> should
    /// be thought of as a generality check.  One function is compatible with
    /// another function if the first function can be called with the arguments of
    /// the second function, and the return type of the first function is convertible
    /// to the return type of the second function.
    /// -> I should avoid std::is_invocable for that kind of check, since it will
    /// not account for the annotated types.

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
    struct _as_member_func {
        static constexpr bool enable = false;
    };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct _as_member_func<R(*)(A...), Self> {
        static constexpr bool enable = true;
        using type = R(*)(A...);
    };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct _as_member_func<R(*)(A...) noexcept, Self> {
        static constexpr bool enable = true;
        using type = R(*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...);
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) &;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) &&;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &&;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile && noexcept;
    };
    template <typename Func, typename Self>
    using as_member_func = _as_member_func<Func, Self>::type;

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer, reference, or object, such as a lambda type. */
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
        template <typename C> requires (as_member_func<R(*)(A...), C>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C>>;
        template <typename... A2>
        using with_args = Signature<R(*)(A2...)>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, A...>;
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
        template <typename C> requires (as_member_func<R(*)(A...) noexcept, C>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C>>;
        template <typename... A2>
        using with_args = Signature<R(*)(A2...) noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...)> : Parameters<C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...)>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) noexcept> : Parameters<C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...) noexcept, C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) &> : Parameters<C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) &>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) & noexcept> : Parameters<C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...) noexcept, C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) & noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const> : Parameters<const C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const noexcept> : Parameters<const C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...) noexcept, C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const &> : Parameters<const C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const &>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const & noexcept> : Parameters<const C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...) noexcept, C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const & noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile> : Parameters<volatile C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile noexcept> : Parameters<volatile C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...) noexcept, C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile &> : Parameters<volatile C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile &>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile & noexcept> : Parameters<volatile C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...) noexcept, C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile & noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile> : Parameters<const volatile C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile noexcept> : Parameters<const volatile C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...) noexcept, C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile &> : Parameters<const volatile C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile &>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile & noexcept> : Parameters<const volatile C&, A...> {
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
        template <typename C2> requires (as_member_func<R(*)(A...) noexcept, C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...) noexcept, C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile & noexcept>;
        template <typename Func> requires (Signature<Func>::enable)
        static constexpr bool compatible =
            std::is_invocable_r_v<R, typename Signature<Func>::to_ptr, Self, A...>;
    };
    template <impl::has_call_operator T> requires (Signature<decltype(&T::operator())>::enable)
    struct Signature<T> : Signature<decltype(&T::operator())>::template with_self<void> {};

    /// TODO: I need a static parent class that serves as the template interface for
    /// all function types, and which implements __class_getitem__ to allow for
    /// similar class subscription syntax as for Python callables:
    ///
    /// Function[Foo: None, [bool, "x": int, "/", "y", "*args": float, "*", "z": str: ..., "**kwargs": type]]
    /// 
    /// This describes a bound method of Foo which returns void and takes a bool and
    /// an int as positional parameters, the second of which is explicitly named "x"
    /// (although this is ignored in the final key as a normalization).  It then
    /// takes a positional-or-keyword argument named "y" with any type, followed by
    /// an arbitrary number of positional arguments of type float.  Finally, it takes
    /// a keyword-only argument named "z" with a default value, and then
    /// an arbitrary number of keyword arguments pointing to type objects.

    /// TODO: one of these signatures is going to have to be generated for each
    /// function type, and must include at least a return type and parameter list,
    /// which may be Function[None, []] for a void function with no arguments.
    /// TODO: there has to also be some complex logic to make sure that type
    /// identifiers are compatible, possibly requiring an overload of isinstance()/
    /// issubclass() to ensure the same semantics are followed in both Python and C++.

}


/// TODO: Signature's semantics have changed slightly, now all the conversions return
/// new Signature types, and Signature::type is used to get the underlying function
/// type.  This needs to be accounted for in the Function<> specializations, and there
/// needs to be a separate Function<> type for each Signature specialization, so that
/// type information is never lost.




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

    /* Register an overload for this function. */
    void overload(const Function<F>& func) {

    }

    /* Attach the function as a bound method of a Python type. */
    template <typename T>
    void attach(Type<T>& type) {

    }

    /// TODO: index into a Function in order to resolve certain overloads.



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
template <typename F = Object(*)(
    Arg<"args", const Object&>::args,
    Arg<"kwargs", const Object&>::kwargs
)> requires (impl::Signature<F>::enable)
struct Function : Object, Interface<Function<F>> {
private:

    /* An individual entry in an overload key, which describes the expected type of a
    single parameter, including its name, category, and whether it has a default
    value. */
    struct Param {
        enum Category {
            POS_DELIMITER,
            KWONLY_DELIMITER,
            POS,
            KW,
            KWONLY,
            ARGS,
            KWARGS,
            POS_DEFAULT,
            KW_DEFAULT,
            KWONLY_DEFAULT,
        };

        std::string_view name;
        Category category;
        PyTypeObject* type;

        bool delimiter() const noexcept {
            return category < POS;
        }

        bool has_default() const noexcept {
            return category > KWARGS;
        }

        bool pos() const noexcept {
            return category == POS || category == POS_DEFAULT;
        }

        bool kw() const noexcept {
            return category == KW || category == KW_DEFAULT;
        }

        bool kwonly() const noexcept {
            return category == KWONLY || category == KWONLY_DEFAULT;
        }

        bool args() const noexcept {
            return category == ARGS;
        }

        bool kwargs() const noexcept {
            return category == KWARGS;
        }

        std::string description() const noexcept {
            switch (category) {
                case POS_DELIMITER: return "positional '/' delimiter";
                case KWONLY_DELIMITER: return "keyword-only '*' delimiter";
                case POS: return "positional";
                case KW: return "keyword";
                case KWONLY: return "keyword-only";
                case ARGS: return "variadic positional";
                case KWARGS: return "variadic keyword";
                case POS_DEFAULT: return "positional with default";
                case KW_DEFAULT: return "keyword with default";
                case KWONLY_DEFAULT: return "keyword-only with default";
                default: return "unknown";
            }
        }

        size_t hash() const noexcept {
            return impl::hash_combine(
                std::hash<std::string_view>{}(name),
                reinterpret_cast<size_t>(type),
                static_cast<size_t>(category)
            );
        }
    };

    /* Describes a single node in the function's overload trie. */
    struct Node {
        /* Argument maps are topologically sorted such that parent classes are always
        checked after subclasses, ensuring that the most specific matching overload is
        always chosen. */
        struct Compare {
            static bool operator()(PyTypeObject* lhs, PyTypeObject* rhs) {
                int result = PyObject_IsSubclass(
                    reinterpret_cast<PyObject*>(lhs),
                    reinterpret_cast<PyObject*>(rhs)
                );
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };
        using TopoMap = std::map<PyTypeObject*, Node*, Compare>;

        size_t alive;  // number of functions that are reachable from this node
        PyObject* func;  // can be null if this is not a terminal node
        TopoMap positional;
        std::unordered_map<std::string_view, TopoMap> keyword;
        TopoMap args;
        TopoMap kwargs;

        /// TODO: the args and kwargs maps need special treatment to enforce
        /// consistency.  The first time I try to match against a variadic argument,
        /// I look it up in these maps and then force all subsequent arguments to
        /// match against the same type.  I can't check the args map more than once
        /// because that would allow potentially mixed *args types, which would
        /// otherwise trigger a backtracking search.

        Node(PyObject* func) : alive(0), func(Py_XNewRef(func)) {}
        Node(const Node&) = delete;
        Node(Node&&) = delete;
        ~Node() noexcept {
            for (auto [type, node] : positional) {
                delete node;
                Py_DECREF(type);
            }
            for (auto [name, map] : keyword) {
                for (auto& [type, node] : map) {
                    delete node;
                    Py_DECREF(type);
                }
            }
            for (auto [type, node] : args) {
                delete node;
                Py_DECREF(type);
            }
            for (auto [type, node] : kwargs) {
                delete node;
                Py_DECREF(type);
            }
            Py_XDECREF(func);
        }

        /// TODO: perhaps search() should raise an error if the key does not fully
        /// satisfy the function signature, regardless of whether a corresponding
        /// overload is found.

        /* Topologically search the overload trie for a matching signature, starting
        from the current node.  This will recursively backtrack until a matching node
        is found or the trie is exhausted, returning nullptr on a failed search.  The
        results will be cached within the PyFunction representation to bypass this
        method on subsequent invocations. */
        Node* search(const std::vector<Param>& key, size_t idx) const noexcept {
            // if we reach the end of the key, either return the current node if it is
            // terminal, or null to backtrack one level and continue searching
            if (idx == key.size()) {
                return func ? this : nullptr;
            }

            // iterate through a topological argument map, checking `issubclass()` at
            // every index, recurring if a match is found
            constexpr auto traverse = [](
                const std::vector<Param>& key,
                size_t idx,
                const Param& lookup,
                const TopoMap& map
            ) -> Node* {
                for (auto [type, node] : map) {
                    if (Compare{}(lookup.type, type)) {
                        // If a match is found, increment the index and recur.  If this
                        // returns nullptr, continue searching
                        Node* result = node->search(key, idx + 1);
                        if (result) {
                            return result;
                        }
                    }
                }
                return nullptr;
            };

            const Param& lookup = key[idx];
            if (lookup.name.empty()) {
                return traverse(key, idx, lookup, positional);
            }
            auto it = keyword.find(lookup.name);
            if (it == keyword.end()) {
                return nullptr;
            }
            return traverse(key, idx, lookup, it->second);
        }

        /// TODO: insert() is going to have to be able to account for variadic
        /// arguments in the inserted function, which need special handling within the
        /// trie using some kind of self-referential node that consumes the remaining
        /// arguments in that category.  Kwargs may need to be represented as an empty
        /// key in the keyword map, which, if present, will be matched against the
        /// remaining keyword arguments in the call.  This will by definition have only
        /// a single child, which will be a self reference to the current node, such
        /// that all remaining keyword arguments will be consumed by the function.
        /// Perhaps the positional args can be handled similarly by just inserting a
        /// key into the positional map that holds a similar self-reference.
        /// -> Perhaps self references aren't great, since there could be other
        /// overloads that can tamper with this system and cause unexpected behavior.
        /// It might be better to store variadic args/kwargs separately in some way,
        /// perhaps as a separate maps in addition to the standard positional and
        /// keyword maps.

        /* Insert a function into the overload trie, or throw a TypeError if it
        conflicts with another node and would thus be ambiguous. */
        void insert(const std::vector<Param>& key, PyObject* func) {
            Node* curr = this;

            // iterate through a topological argument map, checking for exact type
            // identity at every index.  If a match is found, update curr to reflect
            // it, otherwise insert a new node and continue.
            constexpr auto traverse = [](
                TopoMap& map,
                Node*& curr,
                PyTypeObject* type,
                PyObject* func
            ) -> void {
                auto it = curr->positional.begin();
                auto end = curr->positional.end();
                while (it != end) {
                    if (it->first == type) {
                        curr = it->second;
                        return;
                    }
                    ++it;
                }
                /// TODO: insert a new node if no match is found
            };

            // iterate through the key, inserting each argument type into the trie
            // until we reach the final node, which will store the terminal function
            for (const Param& lookup : key) {
                if (lookup.name.empty()) {
                    traverse(curr, curr->positional, lookup.type, func);
                } else {
                    auto it = curr->keyword.find(lookup.name);
                    if (it == curr->keyword.end()) {
                        /// TODO: insert a keyword with an empty map
                    }
                    traverse(curr, it->second, lookup.type, func);
                }
            }

            // if the final node already has a terminal function, then the overloads
            // are ambiguous, and we raise a runtime error.
            if (curr->func) {
                throw TypeError("overload already exists for this signature");
            }
            curr->func = Py_NewRef(func);
        }

        /* Remove a node from the overload trie and prune any dead-ends that lead to
        it.  Returns true if the node was found, or false otherwise. */
        bool remove(const std::vector<Param>& key, size_t idx) noexcept {
            // if we reach the end of the key, either return false if the current node
            // is not terminal, or clear the terminal function and prune the trie
            if (idx == key.size()) {
                if (func) {
                    PyObject* temp = func;
                    func = nullptr;
                    Py_DECREF(temp);
                    if (--alive == 0) {
                        delete this;
                    }
                    return true;
                } else {
                    return false;
                }
            }

            // iterate through a topological argument map, checking `issubclass()` at
            // every index, recurring if a match is found
            constexpr auto traverse = [](
                const std::vector<Param>& key,
                size_t idx,
                const Param& lookup,
                const TopoMap& map
            ) {
                for (auto [type, node] : map) {
                    if (Compare{}(lookup.type, type)) {
                        // if a match is found, increment the index and recur.  If this
                        // returns false, continue searching
                        bool result = node->remove(key, idx + 1);
                        if (result) {
                            if (--node->alive == 0) {
                                delete node;
                            }
                            return true;
                        }
                    }
                }
                return false;
            };

            const Param& lookup = key[idx];
            if (lookup.name.empty()) {
                return traverse(key, idx, lookup, positional);
            }
            auto it = keyword.find(lookup.name);
            if (it == keyword.end()) {
                return false;
            }
            return traverse(key, idx, lookup, it->second);
        }

    };

    /* A helper that inspects the signature of a pure Python function using the
    `inspect` module and translates its inline annotations into overload keys, such
    that Python functions can be easily added to the trie with no additional syntax.
    Annotations will also be inspected when performing an `isinstance()` check on a
    pure Python function against a templated `py::Function<...>` type, ensuring runtime
    type safety when passing Python functions to C++.  Unless the annotations satisfy
    the expected signature, such a conversion will cause an error. */
    struct Inspect {
    private:

        /* Get an iterator over the parameters in the signature, and record its
        length. */
        PyObject* get_iter(Py_ssize_t& len) const {
            PyObject* parameters = PyObject_GetAttr(
                signature,
                impl::TemplateString<"parameters">::ptr
            );
            if (parameters == nullptr) {
                Exception::from_python();
            }
            PyObject* parameters_items = PyObject_CallMethodNoArgs(
                parameters,
                impl::TemplateString<"items">::ptr
            );
            Py_DECREF(parameters);
            if (parameters_items == nullptr) {
                Exception::from_python();
            }
            PyObject* iter = PyObject_GetIter(parameters_items);
            Py_DECREF(parameters_items);
            if (iter == nullptr) {
                Exception::from_python();
            }
            len = PyObject_Length(parameters);
            if (len == -1) {
                Py_DECREF(iter);
                Exception::from_python();
            }
            return iter;
        }

        static std::vector<std::vector<Lookup>> recursive_overloads(
            std::vector<std::vector<Lookup>>&& overloads,
            PyObject* iter,
            Py_ssize_t len,
            Py_ssize_t idx
        ) {
            if (idx == len) {
                return std::move(overloads);
            }
            PyObject* item = PyIter_Next(iter);
            if (item == nullptr) {
                Exception::from_python();
            }

            PyObject* name = PyTuple_GET_ITEM(item, 0);
            PyObject* parameter = PyTuple_GET_ITEM(item, 1);
            PyObject* kind = PyObject_GetAttr(
                parameter,
                impl::TemplateString<"kind">::ptr
            );
            if (kind == nullptr) {
                Py_DECREF(item);
                Py_DECREF(iter);
                Exception::from_python();
            }
            // PyObject* annotation = PyObject_GetAttr(
            //     parameter,
            //     impl::TemplateString<"annotation">::ptr
            // );
            // if (annotation == nullptr) {
            //     Py_DECREF(kind);
            //     Py_DECREF(item);
            //     Py_DECREF(iter);
            //     Exception::from_python();
            // }
            // int is_positional_only = PyObject_RichCompareBool(
            //     kind,
            //     POSITIONAL_ONLY,
            //     Py_EQ
            // );
            // if (is_positional_only < 0) {
            //     Exception::from_python();
            // } else if (is_positional_only) {
            //     vec.emplace_back(
            //         "",

            //     )

            // } else {

            // }



            Py_DECREF(item);
        }

    public:
        PyObject* func;

        /// TODO: this is going to get really complicated trying to parse Python's
        /// type hinting syntax and reflecting it in the overload keys.  Most likely,
        /// what I will need to do to account for things like parametrized containers
        /// is use the equivalent bertrand type rather than the Python type, so that
        /// all parametrizations evaluate to a unique Python type.  That also means
        /// isinstance() checks on these types will need to also match built-in
        /// Python equivalents using iteration, to ensure that the overload logic
        /// remains intact.
        /// -> That may not work, since C++ can only work with the list types that
        /// have been instantiated at the C++ level.  Although I might be able to
        /// fall back to Container<> in those cases.
        /// -> Also, how would you handle user-defined generics?  They would probably
        /// all need to fall back to the origin type.  Perhaps I can search for
        /// an equivalent bertrand type at the C++ level and apply the same
        /// logic as for the Python types, falling back to the origin type if no
        /// equivalent is found.
        /// -> Maybe the way to support generics of these types is to modify the
        /// bertrand.python type registration to map type OR GenericAlias objects to
        /// a corresponding bertrand type, such that I can directly map the type
        /// hint to a bertrand type if possible, or to the origin type if it is a
        /// GenericAlias, or to the dynamic Object otherwise.  The only way that will
        /// fully work is after modules have been defined, however, which is a long
        /// way away.

        PyObject* inspect;
        PyObject* signature;  // inspect.Signature
        PyObject* empty;  // inspect.Parameter.empty
        PyObject* POSITIONAL_ONLY;  // inspect.Parameter.POSITIONAL_ONLY
        PyObject* POSITIONAL_OR_KEYWORD;  // inspect.Parameter.POSITIONAL_OR_KEYWORD
        PyObject* VAR_POSITIONAL;  // inspect.Parameter.VAR_POSITIONAL
        PyObject* KEYWORD_ONLY;  // inspect.Parameter.KEYWORD_ONLY
        PyObject* VAR_KEYWORD;  // inspect.Parameter.VAR_KEYWORD

        PyObject* typing;
        PyObject* annotations;  // result of typing.get_type_hints(func)
        PyObject* Any;  // typing.Any -> object
        PyObject* AnyStr;  // typing.AnyStr -> str
        PyObject* LiteralString;  // typing.LiteralString -> str
        PyObject* Never;  // typing.Never -> nullptr
        PyObject* NoReturn;  // typing.NoReturn -> nullptr
        PyObject* Self;  // typing.Self -> object
        PyObject* Union;  // typing.Union -> recursively split into individual overloads
        PyObject* Optional;  // typing.Optional -> recursively split into two overloads
        PyObject* Literal;  // typing.Literal -> get the type within the literal
        PyObject* TypeGuard;  // typing.TypeGuard -> bool
        // PyObject* TypeVar;  // typing.TypeVar
        // PyObject* TypeVarTuple;  // typing.TypeVarTuple
        PyObject* get_origin;  // typing.get_origin
        PyObject* get_args;  // typing.get_args

        PyObject* types;
        PyObject* GenericAlias;  // types.GenericAlias -> use get_origin() + get_args()
        PyObject* UnionType;  // types.UnionType -> use get_args() to split into overloads
        PyObject* TypeAliasType;  // types.TypeAliasType -> access .__value__

        Inspect(PyObject* func) : func(Py_XNewRef(func)) {
            inspect = PyImport_Import(impl::TemplateString<"inspect">::ptr);
            if (inspect == nullptr) {
                Py_DECREF(func);
                Exception::from_python();
            }
            typing = PyImport_Import(impl::TemplateString<"typing">::ptr);
            if (typing == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Exception::from_python();
            }
            PyObject* signature_func = PyObject_GetAttr(
                inspect,
                impl::TemplateString<"signature">::ptr
            );
            if (signature_func == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Exception::from_python();
            }
            signature = PyObject_CallOneArg(signature_func, func);
            Py_DECREF(signature_func);
            if (signature == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Exception::from_python();
            }
            PyObject* annotations_func = PyObject_GetAttr(
                typing,
                impl::TemplateString<"get_type_hints">::ptr
            );
            if (annotations_func == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Exception::from_python();
            }
            annotations = PyObject_CallOneArg(annotations_func, func);
            Py_DECREF(annotations_func);
            if (annotations == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Exception::from_python();
            }
            PyObject* parameter = PyObject_GetAttr(
                inspect,
                impl::TemplateString<"Parameter">::ptr
            );
            if (parameter == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Py_DECREF(annotations);
                Exception::from_python();
            }
            empty = PyObject_GetAttr(
                parameter,
                impl::TemplateString<"empty">::ptr
            );
            if (empty == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Py_DECREF(annotations);
                Py_DECREF(parameter);
                Exception::from_python();
            }
            POSITIONAL_ONLY = PyObject_GetAttr(
                parameter,
                impl::TemplateString<"POSITIONAL_ONLY">::ptr
            );
            if (POSITIONAL_ONLY == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Py_DECREF(annotations);
                Py_DECREF(empty);
                Py_DECREF(parameter);
                Exception::from_python();
            }
            POSITIONAL_OR_KEYWORD = PyObject_GetAttr(
                parameter,
                impl::TemplateString<"POSITIONAL_OR_KEYWORD">::ptr
            );
            if (POSITIONAL_OR_KEYWORD == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Py_DECREF(annotations);
                Py_DECREF(empty);
                Py_DECREF(POSITIONAL_ONLY);
                Py_DECREF(parameter);
                Exception::from_python();
            }
            VAR_POSITIONAL = PyObject_GetAttr(
                parameter,
                impl::TemplateString<"VAR_POSITIONAL">::ptr
            );
            if (VAR_POSITIONAL == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Py_DECREF(annotations);
                Py_DECREF(empty);
                Py_DECREF(POSITIONAL_ONLY);
                Py_DECREF(POSITIONAL_OR_KEYWORD);
                Py_DECREF(parameter);
                Exception::from_python();
            }
            KEYWORD_ONLY = PyObject_GetAttr(
                parameter,
                impl::TemplateString<"KEYWORD_ONLY">::ptr
            );
            if (KEYWORD_ONLY == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Py_DECREF(annotations);
                Py_DECREF(empty);
                Py_DECREF(POSITIONAL_ONLY);
                Py_DECREF(POSITIONAL_OR_KEYWORD);
                Py_DECREF(VAR_POSITIONAL);
                Py_DECREF(parameter);
                Exception::from_python();
            }
            VAR_KEYWORD = PyObject_GetAttr(
                parameter,
                impl::TemplateString<"VAR_KEYWORD">::ptr
            );
            if (VAR_KEYWORD == nullptr) {
                Py_DECREF(func);
                Py_DECREF(inspect);
                Py_DECREF(typing);
                Py_DECREF(signature);
                Py_DECREF(annotations);
                Py_DECREF(empty);
                Py_DECREF(POSITIONAL_ONLY);
                Py_DECREF(POSITIONAL_OR_KEYWORD);
                Py_DECREF(VAR_POSITIONAL);
                Py_DECREF(KEYWORD_ONLY);
                Py_DECREF(parameter);
                Exception::from_python();
            }
            Py_DECREF(parameter);
        }

        Inspect(const Inspect& other) = delete;
        Inspect(Inspect&& other) = delete;
        Inspect& operator=(const Inspect& other) = delete;
        Inspect& operator=(Inspect&& other) noexcept = delete;

        ~Inspect() noexcept {
            Py_XDECREF(func);
            Py_XDECREF(inspect);
            Py_XDECREF(typing);
            Py_XDECREF(signature);
            Py_XDECREF(annotations);
            Py_XDECREF(empty);
            Py_XDECREF(POSITIONAL_ONLY);
            Py_XDECREF(POSITIONAL_OR_KEYWORD);
            Py_XDECREF(VAR_POSITIONAL);
            Py_XDECREF(KEYWORD_ONLY);
            Py_XDECREF(VAR_KEYWORD);
        }

        /// TODO: this is going to have to return a collection of overload keys due
        /// to the presence of union types in the Python function signature, which must
        /// be collapsed somehow.  That may require recursion to properly handle.

        std::vector<std::vector<Lookup>> overload_keys() const {
            // get an iterator over the function's parameters
            Py_ssize_t len;
            PyObject* iter = get_iter(len);


            std::vector<Lookup> vec;
            vec.reserve(len);
            PyObject* item;
            while ((item = PyIter_Next(iter))) {
                PyObject* name = PyTuple_GET_ITEM(item, 0);
                PyObject* parameter = PyTuple_GET_ITEM(item, 1);
                PyObject* kind = PyObject_GetAttr(
                    parameter,
                    impl::TemplateString<"kind">::ptr
                );
                if (kind == nullptr) {
                    Py_DECREF(item);
                    Py_DECREF(iter);
                    Exception::from_python();
                }
                PyObject* annotation = PyObject_GetAttr(
                    parameter,
                    impl::TemplateString<"annotation">::ptr
                );
                if (annotation == nullptr) {
                    Py_DECREF(kind);
                    Py_DECREF(item);
                    Py_DECREF(iter);
                    Exception::from_python();
                }
                int is_positional_only = PyObject_RichCompareBool(
                    kind,
                    POSITIONAL_ONLY,
                    Py_EQ
                );
                if (is_positional_only < 0) {
                    Exception::from_python();
                } else if (is_positional_only) {
                    vec.emplace_back(
                        "",

                    )

                } else {

                }


                Py_DECREF(item);
            }
            Py_DECREF(iter);
            return vec;
        }

        bool satisfies() const {

        }

    };

    /// NOTE: The actual function type stored within the Python representation will
    /// always explicitly list the `self` parameter first in the case of member
    /// functions, following Python syntax.  This means that the internal function type
    /// can subtly differ from the public template signature, but the end result will
    /// be the same.  This translation is necessary to ensure consistent call behavior
    /// in both languages, and to simplify the internal logic as much as possible.

    /// NOTE: If the initializer can be converted to a function pointer (i.e. for
    /// stateless lambdas or raw function pointers), then we store it as such and
    /// avoid any overhead from `std::function`.  This means we only incur this overhead
    /// if a capturing lambda or other functor is used which does not provide an
    /// implicit conversion to a function pointer.  Using a tagged union to do this
    /// introduces some overhead of its own, but is negligible compared to the overhead
    /// of `std::function`. 

    /* Implementation for non-member functions. */
    template <typename Sig>
    struct PyFunction : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A Python wrapper around a C++ function.

Notes
-----
This type is not directly instantiable from Python.  Instead, it can only be accessed
through the `bertrand.Function` template interface, which can be navigated by
subscripting the interface similar to a `Callable` type hint.

Examples
--------
>>> from bertrand import Function
>>> Function[None, [int, str]]
py::Function<void(*)(py::Int, py::Str)>
>>> Function[int, ["x": int, "y": int]]
py::Function<py::Int(*)(py::Arg<"x", py::Int>, py::Arg<"y", py::Int>)>
>>> Function[str, str, [str, ["maxsplit": int]]]
py::Function<py::Str(py::Str::*)(py::Str, py::Arg<"maxsplit", py::Int>::opt)>

As long as these types have been instantiated somewhere at the C++ level, then these
accessors will resolve to a unique Python type that wraps that instantiation.  If no
C++ instantiation could be found, then a `TypeError` will be raised.

Because each of these types is unique, they can be used with Python's `isinstance()`
and `issubclass()` functions to check against the exact template signature.  A global
check against the interface type will determine whether the function is implemented in
C++ (True) or Python (False).

TODO: explain how to properly subscript the interface and how arguments are translated
to a corresponding C++ function signature.

)doc";

        std::string name;
        std::string docstring;
        const bool is_ptr;
        union {
            Sig::to_ptr::type func_ptr;
            std::function<typename Sig::to_value::type> func;
        };
        Sig::Defaults defaults;
        vectorcallfunc call;
        Node* root;
        size_t size;
        std::unordered_map<size_t, Node*> cache;  // value may be null

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            Sig::to_ptr::type func,
            Sig::Defaults&& defaults
        ) : name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(true),
            func_ptr(func),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Sig::Defaults&& defaults
        ) : name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(false),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        ~PyFunction() {
            if (root) {
                delete root;
            }
            if (!is_ptr) {
                func.~function();
            }
        }

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings) {
            /// TODO: manually initialize a heap type with a correct vectorcall offset.
            /// This has to be an instance of the metaclass, which may require a
            /// forward reference here.
            /// -> Perhaps this should be a pure static type, which would avoid an
            /// extra import statement whenever a function is called.  Same for the
            /// metaclass defined after this, which is used to distinguish between
            /// operands in the Python slots for binary operators.
        }

        /* Register an overload from Python. */
        static PyObject* overload(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {

            self->cache.clear();
        }

        /* Attach the function to a type as a descriptor. */
        static PyObject* attach(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {

        }

        /* Manually clear the function's internal overload cache from Python. */
        static PyObject* flush(PyFunction* self) {
            self->cache.clear();
            Py_RETURN_NONE;
        }

        /* Call the function from Python. */
        static PyObject* __call__(
            PyFunction* self,
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

        /// TODO: maybe I should raise an error if the index does not fully satisfy the
        /// function's signature?
        /// -> Yes, and potentially __getitem__ should return a self reference.
        /// use __contains__ if you want to check for the presence of an overload.

        /* Convert a Python string into a coherent argument name, ensuring that it is
        well-formed and does not contain any invalid characters. */
        static std::string_view arg_name(PyObject* str) {
            Py_ssize_t len;
            const char* data = PyUnicode_AsUTF8AndSize(str, &len);
            if (data == nullptr) {
                Exception::from_python();
            }
            std::string_view view{name_data, name_len};
            std::string_view sub = view.substr(
                view.starts_with("*") +
                view.starts_with("**")
            );
            /// TODO: check for duplicate names
            if (sub.empty()) {
                throw TypeError("argument name cannot be empty");
            }
            if (std::isdigit(sub.front())) {
                throw TypeError(
                    "argument name cannot start with a number: '" + sub + "'"
                );
            }
            for (const char c : sub) {
                if (!std::isalnum(c) && c != '_') {
                    throw TypeError(
                        "argument name must only contain alphanumerics and "
                        "underscores: '" + sub + "'"
                    );
                }
            }
            return view;
        }

        /// TODO: In order to validate an overload key once it's been created, I can
        /// very carefully recur over the overload key using both an index sequence
        /// over the target signature and parallel index into the overload key.

        /* Parse a Python object into a single entry in an overload key, . */
        static Param get_param(
            PyObject* item,
            Py_ssize_t idx,
            Py_ssize_t& posonly_idx,  // initialized to size
            Py_ssize_t& kw_idx,  // initialized to size
            Py_ssize_t& kwonly_idx,  // initialized to size
            Py_ssize_t& args_idx,  // initialized to size
            Py_ssize_t& kwargs_idx  // initialized to size
        ) {
            // raw types are interpreted as positional-only arguments
            if (PyType_Check(item)) {
                if (idx > posonly_idx) {
                    throw TypeError(
                        "positional-only argument cannot follow `/` delimiter: " +
                        repr(reinterpret_borrow<Object>(item))
                    );
                } else if (idx > kw_idx) {
                    throw TypeError(
                        "positional-only argument cannot follow keyword argument: " +
                        repr(reinterpret_borrow<Object>(item))
                    );
                } else if (idx > args_idx) {
                    throw TypeError(
                        "positional-only argument cannot follow variadic positional "
                        "arguments " + repr(reinterpret_borrow<Object>(item))
                    );
                }
                return {"", Param::POS, reinterpret_cast<PyTypeObject*>(key)};

            // strings either represent an argument delimiter (such as "/" for
            // positional-only parameters, or "*" for keyword-only args), or a
            // dynamically-typed keyword argument.
            } else if (PyUnicode_Check(item)) {
                PyObject* pos_str = PyUnicode_FromStringAndSize("/", 1);
                if (pos_str == nullptr) {
                    Exception::from_python();
                }
                int rc = PyObject_RichCompareBool(item, pos_str, Py_EQ);
                Py_DECREF(pos_str);
                if (rc < 0) {
                    Exception::from_python();
                } else if (rc) {
                    if (idx > posonly_idx) {
                        throw TypeError(
                            "positional-only argument delimiter '/' appears at "
                            "conflicting indices: " + std::to_string(posonly_idx) +
                            " and " + std::to_string(idx)
                        );
                    } else if (idx > kw_idx) {
                        throw TypeError(
                            "positional-only argument delimiter '/' cannot follow "
                            "keyword arguments at index"
                        );
                    } else if (idx > args_idx) {
                        throw TypeError(
                            "positional-only argument delimiter '/' cannot follow "
                            "variadic positional arguments"
                        );
                    } else if (idx > kwargs_idx) {
                        throw TypeError(
                            "positional-only argument delimiter '/' cannot follow "
                            "variadic keyword arguments"
                        );
                    }
                    posonly_idx = idx;
                    return {"/", Param::POS_DELIMITER, nullptr};
                }
                PyObject* kw_str = PyUnicode_FromStringAndSize("*", 1);
                if (kw_str == nullptr) {
                    Exception::from_python();
                }
                rc = PyObject_RichCompareBool(item, kw_str, Py_EQ);
                Py_DECREF(kw_str);
                if (rc < 0) {
                    Exception::from_python();
                } else if (rc) {
                    if (idx > kwonly_idx) {
                        throw TypeError(
                            "keyword-only argument delimiter '*' appears at "
                            "conflicting indices: " + std::to_string(kwonly_idx) +
                            " and " + std::to_string(idx)
                        );
                    } else if (idx > kwargs_idx) {
                        throw TypeError(
                            "keyword-only argument delimiter '*' cannot follow "
                            "variadic keyword arguments"
                        );
                    }
                    kwonly_idx = idx;
                    return {"*", Param::KWONLY_DELIMITER, nullptr};
                }
                if (idx > kwargs_idx) {
                    throw TypeError(
                        "keyword arguments cannot follow variadic keyword arguments"
                    );
                }
                std::string_view name = arg_name(item);
                if (name.starts_with("**")) {
                    if (idx > kwargs_idx) {
                        throw TypeError(
                            "variadic keyword arguments appear at conflicting "
                            "indices: " + std::to_string(kwargs_idx) + " and " +
                            std::to_string(idx)
                        );
                    }
                    return {name.substr(2), Param::KWARGS, PyObject_Type};
                } else if (name.starts_with("*")) {
                    if (idx > args_idx) {
                        throw TypeError(
                            "variadic positional arguments appear at conflicting "
                            "indices: " + std::to_string(args_idx) + " and " +
                            std::to_string(idx)
                        );
                    }
                    if (idx > kwonly_idx) {
                        throw TypeError(
                            "variadic positional arguments cannot follow keyword-only "
                            "argument delimiter '*': " +
                            repr(reinterpret_borrow<Object>(item))
                        );
                    }
                    return {name.substr(1), Param::ARGS, PyObject_Type};
                } else {
                    return {
                        std::move(name),
                        idx > kwonly_idx ? Param::KWONLY : Param::KW,
                        PyObject_Type
                    };
                }

            // slices are interpreted as keyword or keyword-only arguments, depending
            // on position with respect to the "*" delimiter
            } else if (PySlice_Check(item)) {
                PyObject* name;
                PyTypeObject* type;
                PySliceObject* slice = reinterpret_cast<PySliceObject*>(item);
                if (PyUnicode_Check(slice->start)) {
                    name = slice->start;
                } else {
                    throw TypeError(
                        "keyword argument name must be a string, not: " +
                        repr(reinterpret_borrow<Object>(slice->start))
                    );
                }
                if (PyType_Check(slice->stop)) {
                    type = reinterpret_cast<PyTypeObject*>(slice->stop);
                } else {
                    throw TypeError(
                        "keyword argument type must be a type object, not: " +
                        repr(reinterpret_borrow<Object>(slice->stop))
                    );
                }
                if (idx > kwargs_idx) {
                    throw TypeError(
                        "keyword argument cannot follow variadic keyword arguments: " +
                        repr(reinterpret_borrow<Object>(item))
                    );
                }
                return {
                    arg_name(name),
                    slice->step == Py_None ?
                        (idx > kwonly_idx ? Param::KWONLY : Param::KW) :
                        (idx > kwonly_idx ? Param::KWONLY_DEFAULT : Param::KW_DEFAULT),
                    reinterpret_cast<PyTypeObject*>(type)
                };

            // all other types are invalid
            } else {
                throw TypeError(
                    "expected a type for a positional argument, a string for a "
                    "keyword argument with a dynamic type, or a slice for a keyword "
                    "argument with a specific type, not: " +
                    repr(reinterpret_borrow<Object>(item))
                );
            }
        }

        /* Validate an overload key, ensuring that it matches the enclosing function
        signature. */
        template <size_t I>
        static void validate_key(const std::vector<Param>& key, Py_ssize_t& idx) {
            /// TODO: does issubclass<impl::Param<T>::type>(obj) work if T is a C++
            /// type or a reference, etc.?
            using T = typename Sig::template at<I>;
            using type = __as_object__<
                std::remove_cvref_t<typename impl::Param<T>::type>
            >::type;
            constexpr StaticStr name = impl::Param<T>::name;

            if constexpr (impl::Param<T>::kwonly) {
                if (idx < key.size()) {
                    const Param& param = key[idx];
                    if (param.name == name) {
                        if (!param.kwonly()) {
                            throw TypeError(
                                "expected argument '" + name +
                                "' to be keyword-only, not " + param.description()
                            );
                        }
                        if (!issubclass<type>(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        ))) {
                            throw TypeError(
                                "expected keyword-only argument '" + name +
                                "' to be a subclass of '" + repr(Type<type>()) +
                                "', not: '" + repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param.type)
                                )) + "'"
                            );
                        }
                        if (impl::Param<T>::opt ^ param.has_default()) {
                            throw TypeError(
                                "expected keyword-only argument '" +
                                name + impl::Param<T>::opt ?
                                    "' to have a default value" :
                                    "' to not have a default value"
                            );
                        }
                    } else if (!impl::Param<T>::opt) {
                        throw TypeError(
                            "missing required keyword-only argument: '" + name + "'"
                        );
                    }
                } else if (!impl::Param<T>::opt) {
                    throw TypeError(
                        "missing required keyword-only argument: '" + name + "'"
                    );
                }

            } else if constexpr (impl::Param<T>::kw) {
                if (idx < key.size()) {
                    const Param& param = key[idx];
                    if (param.name == name) {
                        if (!param.kw()) {
                            throw TypeError(
                                "expected argument '" + name +
                                "' to be a positional-or-keyword argument, not " +
                                param.description()
                            );
                        }
                        if (!issubclass<type>(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        ))) {
                            throw TypeError(
                                "expected positional-or-keyword argument '" + name +
                                "' to be a subclass of '" + repr(Type<type>()) +
                                "', not: '" + repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param.type)
                                )) + "'"
                            );
                        }
                        if (impl::Param<T>::opt ^ param.has_default()) {
                            throw TypeError(
                                "expected positional-or-keyword argument '" +
                                name + impl::Param<T>::opt ?
                                    "' to have a default value" :
                                    "' to not have a default value"
                            );
                        }
                    } else if (!impl::Param<T>::opt) {
                        throw TypeError(
                            "missing required positional-or-keyword argument: '" +
                            name + "'"
                        );
                    }
                } else if (!impl::Param<T>::opt) {
                    throw TypeError(
                        "missing required positional-or-keyword argument: '" +
                        name + "'"
                    );
                }

            } else if constexpr (impl::Param<T>::args) {
                while (idx < key.size()) {
                    const Param& param = key[idx];
                    if (!param.pos()) {
                        break;
                    }
                    if (!issubclass<type>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type)
                    ))) {
                        throw TypeError(
                            "expected positional argument '" + name +
                            "' to be a subclass of '" + repr(Type<type>()) +
                            "', not: '" + repr(reinterpret_borrow<Object>(
                                reinterpret_cast<PyObject*>(param.type)
                            )) + "'"
                        );
                    }
                    ++idx;
                }
                if (idx < key.size()) {
                    const Param& param = key[idx];
                    if (param.args()) {
                        if (!issubclass<type>(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        ))) {
                            throw TypeError(
                                "expected variadic positional arguments '" + name +
                                "' to be a subclass of '" + repr(Type<type>()) +
                                "', not: '" + repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param.type)
                                )) + "'"
                            );
                        }
                    }
                }

            } else if constexpr (impl::Param<T>::kwargs) {


            } else {

            }
        }

        /* Index the function to resolve a specific overload, as if the function were
        being called normally.  Returns `None` if no overload can be found, indicating
        that the function will be called with its base implementation. */
        static PyObject* __getitem__(PyFunction* self, PyObject* key) {
            constexpr auto resolve = [](PyObject* key) -> Param {


            };


            /// TODO: before doing anything else, convert the key to a vector of
            /// parameter and then check whether or not it is valid for the function
            /// signature.  If not, raise an error.  Otherwise, continue as normal.

            if (!self->root) {
                return Py_NewRef(self);
            }



            // paths through the trie are cached to avoid redundant searches.  If no
            // match is found, then the cache will store a null pointer, which gets
            // translated to None and indicates that the function will be called with
            // its base implementation, and not a custom overload.
            constexpr auto get = [](
                const std::vector<Lookup>& vec,
                size_t hash,
                PyFunction* self,
                PyObject* key
            ) -> PyObject* {
                auto it = self->cache.find(hash);
                if (it != self->cache.end()) {
                    return Py_NewRef(it->second ? it->second->func : Py_None);
                }
                Node* node = self->root->search(vec, 0);
                self->cache[hash] = node;
                return Py_NewRef(node ? node->func : Py_None);
            };

            try {
                // if the key is a scalar, then we analyze it as a single argument and
                // avoid unnecessary allocations
                if (!PyTuple_Check(key)) {
                    std::vector<Lookup> vec = {resolve(key)};
                    size_t hash = impl::hash_combine(
                        std::hash<std::string_view>{}(vec.front().first),
                        reinterpret_cast<size_t>(vec.front().second)
                    );
                    return get(vec, hash, self, key);
                }

                // otherwise, the key is a tuple, and we need to construct an argument
                // vector from it in order to search the trie
                Py_ssize_t size = PyTuple_GET_SIZE(key);
                std::vector<Lookup> vec;
                vec.reserve(size);
                size_t hash = 0;
                for (Py_ssize_t i = 0; i < size; ++i) {
                    vec[i] = resolve(PyTuple_GET_ITEM(key, i));
                    const Lookup& lookup = vec[i];
                    hash = impl::hash_combine(
                        hash,
                        std::hash<std::string_view>{}(lookup.first),
                        reinterpret_cast<size_t>(lookup.second)
                    );
                }
                return get(vec, hash, self, key);

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Delete a matching overload, removing it from the overload trie. */
        static int __delitem__(PyFunction* self, PyObject* key, PyObject* value) {
            if (value) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "functions do not support item assignment: use "
                    "`@func.overload` to register an overload instead"
                );
                return -1;
            }

            /// TODO: generate a lookup key from the Python object, and then
            /// call self->root->remove() to delete the overload.

            self->cache.clear();
        }

        /* Check whether the function is callable with a given key.  This just boils
        down to constructing a key and checking whether it conforms to the */
        static int __contains__(PyFunction* self, PyObject* key) {

        }

        /* `len(function)` will get the number of overloads that are currently being
        tracked. */
        static Py_ssize_t __len__(PyFunction* self) {
            return self->size;
        }

        /* Default `repr()` demangles the function name + signature. */
        static PyObject* __repr__(PyFunction* self) {
            static const std::string demangled =
                impl::demangle(typeid(Function<F>).name());
            std::string s = "<" + demangled + " at " +
                std::to_string(reinterpret_cast<size_t>(self)) + ">";
            PyObject* string = PyUnicode_FromStringAndSize(s.c_str(), s.size());
            if (string == nullptr) {
                return nullptr;
            }
            return string;
        }

        /// TODO: turns out I can make all functions introspectable via Python's
        /// `inspect` module by adding a __signature__ attribute, although this is
        /// subject to change, and I have to check against the Python source code.

    private:

        inline static PyMethodDef methods[] = {

        };

    };

    /* A specialization for member functions expecting a `self` parameter, which can
    be supplied either during construction or dynamically at the call site according to
    Python syntax. */
    template <typename Sig> requires (Sig::has_self)
    struct PyFunction<Sig> : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(TODO: another docstring describing member functions/methods, and how they
differ from non-member functions.)doc";;

        std::optional<std::remove_reference_t<typename Sig::Self>> self;
        std::string name;
        std::string docstring;
        const bool is_ptr;
        union {
            Sig::to_ptr::type func_ptr;
            std::function<typename Sig::to_value::type> func;
        };
        Sig::Defaults defaults;
        vectorcallfunc call;
        Node* root;
        size_t size;

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            Sig::to_ptr::type func,
            Sig::Defaults&& defaults
        ) : self(std::nullopt),
            name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(true),
            func_ptr(func),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        PyFunction(
            std::remove_reference_t<typename Sig::Self>&& self,
            std::string&& name,
            std::string&& docstring,
            Sig::to_ptr::type func,
            Sig::Defaults&& defaults
        ) : self(std::move(self)),
            name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(true),
            func_ptr(func),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Sig::Defaults&& defaults
        ) : self(std::nullopt),
            name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(false),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        PyFunction(
            std::remove_reference_t<typename Sig::Self>&& self,
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Sig::Defaults&& defaults
        ) : self(std::move(self)),
            name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(false),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        ~PyFunction() {
            if (root) {
                delete root;
            }
            if (!is_ptr) {
                func.~function();
            }
        }

        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {

        }

    };

public:
    using __python__ = PyFunction<impl::Signature<F>>;

    Function(PyObject* p, borrowed_t t) : Object(p, t) {}
    Function(PyObject* p, stolen_t t) : Object(p, t) {}

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


template <
    impl::is_callable_any Func,
    typename... D
> requires (!impl::python_like<Func>)
Function(Func&&, Defaults&&...) -> Function<
    typename impl::Signature<std::remove_reference_t<Func>>::type
>;
template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(std::string, F, D...) -> Function<
    typename impl::GetSignature<std::decay_t<F>>::type
>;
template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(std::string, std::string, F, D...) -> Function<
    typename impl::GetSignature<std::decay_t<F>>::type
>;


#define NON_MEMBER_FUNC(IN, OUT) \
    template <typename R, typename... A> \
    struct __object__<IN> : Returns<Function<OUT>> {};

#define MEMBER_FUNC(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __object__<IN> : Returns<Function<OUT>> {};

#define STD_MEM_FN(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __object__<decltype(std::mem_fn(std::declval<IN>()))> : Returns<Function<OUT>> {};


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


}  // namespace py


#endif
