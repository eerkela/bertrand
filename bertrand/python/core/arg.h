#ifndef BERTRAND_CORE_ARG_H
#define BERTRAND_CORE_ARG_H

#include "declarations.h"
#include "object.h"
#include "except.h"


namespace py {


/////////////////////////
////    ARGUMENTS    ////
/////////////////////////


namespace impl {

    struct ArgKind {
        enum Flags : uint8_t {
            POS                 = 0b1,
            KW                  = 0b10,
            OPT                 = 0b100,
            VARIADIC            = 0b1000,
        } flags;

        constexpr ArgKind(uint8_t flags = 0) noexcept :
            flags(static_cast<Flags>(flags))
        {}
    
        constexpr operator uint8_t() const noexcept {
            return flags;
        }

        constexpr bool posonly() const noexcept {
            return (flags & ~OPT) == POS;
        }

        constexpr bool pos() const noexcept {
            return (flags & POS) & ~(flags & VARIADIC);
        }

        constexpr bool args() const noexcept {
            return flags == (POS | VARIADIC);
        }

        constexpr bool kwonly() const noexcept {
            return (flags & ~OPT) == KW;
        }

        constexpr bool kw() const noexcept {
            return (flags & KW) & ~(flags & VARIADIC);
        }

        constexpr bool kwargs() const noexcept {
            return flags == (KW | VARIADIC);
        }

        constexpr bool opt() const noexcept {
            return flags & OPT;
        }

        constexpr bool variadic() const noexcept {
            return flags & VARIADIC;
        }
    };

    template <typename T>
    struct OptionalArg {
        using type = T::type;
        using opt = OptionalArg;
        static constexpr StaticStr name = T::name;
        static constexpr ArgKind kind = T::kind | ArgKind::OPT;

        type value;

        [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }

        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <StaticStr Name, typename T>
    struct PositionalArg {
        using type = T;
        using opt = OptionalArg<PositionalArg>;
        static constexpr StaticStr name = Name;
        static constexpr ArgKind kind = ArgKind::POS;

        type value;

        [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }

        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <StaticStr Name, typename T>
    struct KeywordArg {
        using type = T;
        using opt = OptionalArg<KeywordArg>;
        static constexpr StaticStr name = Name;
        static constexpr ArgKind kind = ArgKind::KW;

        type value;

        [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }

        template <typename U> requires (std::convertible_to<type, U>)
        [[nodiscard]] constexpr operator U() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <StaticStr Name, typename T>
    struct VarArgs {
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
        static constexpr ArgKind kind = ArgKind::POS | ArgKind::VARIADIC;

        std::vector<type> value;

        [[nodiscard]] std::vector<type>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::vector<type>& operator*() const {
            return value;
        }
        [[nodiscard]] std::vector<type>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::vector<type>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator std::vector<type>() && {
            return std::move(value);
        }
    };

    template <StaticStr Name, typename T>
    struct VarKwargs {
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
        static constexpr ArgKind kind = ArgKind::KW | ArgKind::VARIADIC;

        std::unordered_map<std::string, T> value;

        [[nodiscard]] constexpr std::unordered_map<std::string, T>& operator*() {
            return value;
        }
        [[nodiscard]] constexpr const std::unordered_map<std::string, T>& operator*() const {
            return value;
        }
        [[nodiscard]] constexpr std::unordered_map<std::string, T>* operator->() {
            return &value;
        }
        [[nodiscard]] constexpr const std::unordered_map<std::string, T>* operator->() const {
            return &value;
        }

        [[nodiscard]] constexpr operator std::unordered_map<std::string, T>() && {
            return std::move(value);
        }
    };

    /* A keyword parameter pack obtained by double-dereferencing a Python object. */
    template <mapping_like T> requires (has_size<T>)
    struct KwargPack {
        using key_type = T::key_type;
        using mapped_type = T::mapped_type;
        using type = mapped_type;
        static constexpr StaticStr name = "";
        static constexpr ArgKind kind = ArgKind::KW | ArgKind::VARIADIC;

        T value;

    private:

        template <typename U>
        static constexpr bool can_iterate =
            impl::yields_pairs_with<U, key_type, mapped_type> ||
            impl::has_items<U> ||
            (impl::has_keys<U> && impl::has_values<U>) ||
            (impl::yields<U, key_type> && impl::lookup_yields<U, mapped_type, key_type>) ||
            (impl::has_keys<U> && impl::lookup_yields<U, mapped_type, key_type>);

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

        auto size() const { return std::ranges::size(value); }
        template <typename U = T> requires (can_iterate<U>)
        auto begin() const { return std::ranges::begin(transform()); }
        template <typename U = T> requires (can_iterate<U>)
        auto cbegin() const { return begin(); }
        template <typename U = T> requires (can_iterate<U>)
        auto end() const { return std::ranges::end(transform()); }
        template <typename U = T> requires (can_iterate<U>)
        auto cend() const { return end(); }
    };

    /* A positional parameter pack obtained by dereferencing a Python object. */
    template <iterable T> requires (has_size<T>)
    struct ArgPack {
        using type = iter_type<T>;
        static constexpr StaticStr name = "";
        static constexpr ArgKind kind = ArgKind::POS | ArgKind::VARIADIC;

        T value;

        auto size() const { return std::ranges::size(value); }
        auto begin() const { return std::ranges::begin(value); }
        auto cbegin() const { return begin(); }
        auto end() const { return std::ranges::end(value); }
        auto cend() const { return end(); }

        template <typename U = T>
            requires (impl::mapping_like<U> && std::convertible_to<
                typename std::remove_reference_t<U>::key_type,
                std::string
            >)
        auto operator*() const {
            return KwargPack<T>{std::forward<T>(value)};
        }
    };

    template <typename T>
    static constexpr bool _arg_pack = false;
    template <typename T>
    static constexpr bool _arg_pack<ArgPack<T>> = true;
    template <typename T>
    concept arg_pack = _arg_pack<std::remove_cvref_t<T>>;

    template <typename T>
    static constexpr bool _kwarg_pack = false;
    template <typename T>
    static constexpr bool _kwarg_pack<KwargPack<T>> = true;
    template <typename T>
    concept kwarg_pack = _kwarg_pack<std::remove_cvref_t<T>>;

}


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a `py::Function`.  Uses aggregate initialization to extend the lifetime of
temporaries.  Can also be used to indicate structural members in a `py::Union` or
`py::Intersection` type, or named members in a `py::Tuple` type. */
template <StaticStr Name, typename T> requires (!Name.empty())
struct Arg {
    using type = T;
    using pos = impl::PositionalArg<Name, T>;
    using args = impl::VarArgs<Name, T>;
    using kw = impl::KeywordArg<Name, T>;
    using kwargs = impl::VarKwargs<Name, T>;
    using opt = impl::OptionalArg<Arg>;

    static constexpr StaticStr name = Name;
    static constexpr impl::ArgKind kind = impl::ArgKind::POS | impl::ArgKind::KW;

    type value;

    [[nodiscard]] constexpr std::remove_reference_t<type>& operator*() {
        return value;
    }
    [[nodiscard]] constexpr const std::remove_reference_t<type>& operator*() const {
        return value;
    }
    [[nodiscard]] constexpr std::remove_reference_t<type>* operator->() {
        return &value;
    }
    [[nodiscard]] constexpr const std::remove_reference_t<type>* operator->() const {
        return &value;
    }

    /* Argument rvalues are normally generated whenever a function is called.  Making
    them convertible to the underlying type means they can be used to call external
    C++ functions that are not aware of Python argument annotations. */
    [[nodiscard]] constexpr operator type() && {
        if constexpr (std::is_lvalue_reference_v<type>) {
            return value;
        } else {
            return std::move(value);
        }
    }

    /* Conversions to other types are also allowed, as long as the underlying type
    supports it. */
    template <typename U> requires (std::convertible_to<type, U>)
    [[nodiscard]] constexpr operator U() && {
        if constexpr (std::is_lvalue_reference_v<type>) {
            return value;
        } else {
            return std::move(value);
        }
    }
};


namespace impl {

    /* A singleton argument factory that allows arguments to be constructed via
    familiar assignment syntax, which extends the lifetime of temporaries. */
    template <StaticStr Name>
    struct ArgFactory {
        template <typename T>
        constexpr Arg<Name, T> operator=(T&& value) const {
            return {std::forward<T>(value)};
        }
    };

    template <typename T>
    static constexpr bool _is_arg = false;
    template <typename T>
    static constexpr bool _is_arg<OptionalArg<T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<PositionalArg<Name, T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<Arg<Name, T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<KeywordArg<Name, T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<VarArgs<Name, T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<VarKwargs<Name, T>> = true;
    template <typename T>
    static constexpr bool _is_arg<ArgPack<T>> = true;
    template <typename T>
    static constexpr bool _is_arg<KwargPack<T>> = true;
    template <typename T>
    concept is_arg = _is_arg<std::remove_cvref_t<T>>;

    /* Inspect a C++ argument at compile time.  Normalizes unannotated types to
    positional-only arguments to maintain C++ style. */
    template <typename T>
    struct ArgTraits {
        using type                                  = T;
        using as_default                            = T;
        static constexpr StaticStr name             = "";
        static constexpr ArgKind kind               = ArgKind::POS;
        static constexpr bool posonly() noexcept    { return kind.posonly(); }
        static constexpr bool pos() noexcept        { return kind.pos(); }
        static constexpr bool args() noexcept       { return kind.args(); }
        static constexpr bool kwonly() noexcept     { return kind.kwonly(); }
        static constexpr bool kw() noexcept         { return kind.kw(); }
        static constexpr bool kwargs() noexcept     { return kind.kwargs(); }
        static constexpr bool opt() noexcept        { return kind.opt(); }
        static constexpr bool variadic() noexcept   { return kind.variadic(); }
    };

    /* Inspect a C++ argument at compile time.  Forwards to the annotated type's
    interface where possible. */
    template <is_arg T>
    struct ArgTraits<T> {
    private:

        template <typename U>
        struct _as_default { using type = U; };
        template <typename U>
        struct _as_default<OptionalArg<U>> {
            using type = Arg<U::name, typename U::type>::kw;
        };

    public:
        using type                                  = std::remove_cvref_t<T>::type;
        using as_default                            = _as_default<std::remove_cvref_t<T>>::type;
        static constexpr StaticStr name             = std::remove_cvref_t<T>::name;
        static constexpr ArgKind kind               = std::remove_cvref_t<T>::kind;
        static constexpr bool posonly() noexcept    { return kind.posonly(); }
        static constexpr bool pos() noexcept        { return kind.pos(); }
        static constexpr bool args() noexcept       { return kind.args(); }
        static constexpr bool kwonly() noexcept     { return kind.kwonly(); }
        static constexpr bool kw() noexcept         { return kind.kw(); }
        static constexpr bool kwargs() noexcept     { return kind.kwargs(); }
        static constexpr bool opt() noexcept        { return kind.opt(); }
        static constexpr bool variadic() noexcept   { return kind.variadic(); }
    };

}


/* A compile-time factory for binding keyword arguments with Python syntax.  constexpr
instances of this class can be used to provide an even more Pythonic syntax:

    constexpr auto x = py::arg<"x">;
    my_func(x = 42);
*/
template <StaticStr name>
constexpr impl::ArgFactory<name> arg {};


/* Dereference operator is used to emulate Python container unpacking when calling a
`py::Function` object.

A single unpacking operator passes the contents of an iterable container as positional
arguments to a function.  Unlike Python, only one such operator is allowed per call,
and it must be the last positional argument in the parameter list.  This allows the
type checker to ensure that the container's value type is minimally convertible to each
of the remaining positional arguments ahead of time, although in most cases, the number
of arguments cannot be determined until runtime.  Thus, if any arguments are missing or
extras are provided, the call will raise an exception similar to Python, rather than
failing statically at compile time.  This can be avoided by using standard positional
and keyword arguments instead, which can be fully verified at compile time, or by
including variadic positional arguments in the function signature, which will fully
consume any remaining arguments according to Python semantics.

A second unpacking operator promotes the arguments into keywords, and can only be used
if the container is dict-like, meaning it possess both `::key_type` and `::mapped_type`
aliases, and that indexing it with an instance of the key type returns a value of the
mapped type.  The actual unpacking is robust, and does not depend on the container
returning key-value pairs, although it will prefer them if so, followed by the result
of the `.items()` method if present, or by zipping `.keys()` and `.values()` if both
exist, and finally by iterating over the keys and indexing the container.  Similar to
the positional unpacking operator, only one of these may be present as the last keyword
argument in the parameter list, and a compile-time check is made to ensure that the
mapped type is convertible to any missing keyword arguments that are not explicitly
provided.

In both cases, the extra runtime complexity results in a small performance degradation
over a typical function call, which is minimized as much as possible. */
template <impl::inherits<Object> Self> requires (impl::iterable<Self>)
[[nodiscard]] auto operator*(Self&& self) {
    return impl::ArgPack<Self>{std::forward<Self>(self)};
}


}


#endif  // BERTRAND_CORE_ACCESS_H
