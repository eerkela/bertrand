#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


/* A simple convenience struct implementing the overload pattern for `visit()`-style
functions.

The intended use of this type is more or less as follows:

    Union<int, std::string> u = "hello, world!";
    u.visit(visitor{
        [](int i) { return print(i); },
        [](const std::string& s) { return print(s); }
    });

Without the `visitor{}` wrapper, one would need to use a generic lambda or write their
own elaborated visitor class to handle the various types in the union, which is much
more verbose.  The `visitor{}` wrapper allows each case to be clearly enumerated and
handled directly at the call site using lambdas, which may be passed in from elsewhere
as an expression of the strategy pattern.

Ideally, the `visitor` type could be omitted entirely, yielding a syntax like:

    Union<int, std::string> u = "hello, world!";
    u.visit({
        [](int i) { return print(i); },
        [](const std::string& s) { return print(s); }
    });

... But this is not currently possible in C++ due to limitations around class template
argument deduction (CTAD) for function arguments.  Currently, the only workaround is
to explicitly specify the `visitor` type during the invocation of `visit()`, as shown
in the first example.  This restriction may be lifted in a future version of C++,
at which point the second syntax could be standardized as well.  One proposal that
would allow this is P2998 (https://open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2998r0.html),
which would get around this by using alias deduction guides at a language level.  Until
that paper or a similar proposal is standardized, an explicit `visitor` constructor is
unavoidable. */
template <typename... Funcs>
struct visitor : public Funcs... { using Funcs::operator()...; };


namespace impl {
    struct union_tag {};
    struct optional_tag {};
    struct expected_tag {};

    /* Provides an extensible mechanism for controlling the dispatching behavior of
    the `meta::visitor` concept and `bertrand::visit()` operator, including return
    type deduction from the possible alternatives and customizable dispatch logic.
    Users can specialize this structure to extend those utilities to arbitrary types,
    without needing to reimplement the entire compile-time dispatch mechanism. */
    template <typename T>
    struct visitable;

}


namespace meta {

    template <typename T>
    concept visitable = impl::visitable<T>::enable;

    template <typename T>
    concept monad = impl::visitable<T>::monad;

    template <typename T>
    concept Union = inherits<T, impl::union_tag>;

    template <typename T>
    concept Optional = inherits<T, impl::optional_tag>;

    template <typename T>
    concept Expected = inherits<T, impl::expected_tag>;

}


/* A type-safe union capable of storing any number of arbitrarily-qualified types.

This is similar to `std::variant<Ts...>`, but with the following changes:

    1.  `Ts...` may have cvref qualifications, allowing the union to model references
        and other cvref-qualified types without requiring an extra copy or
        `std::reference_wrapper` workaround.  Note that the union does not extend the
        lifetime of the referenced objects, so the usual guidelines for references
        still apply.
    2.  The union can never be in an invalid state, meaning the index is always within
        range and points to a valid member of the union.
    3.  The constructor is more precise than `std::variant`, preferring exact matches
        if possible, then the most proximal cvref-qualified alternative or base class,
        with implicit conversions being considered only as a last resort.  If an
        implicit conversion is selected, then it will always be the leftmost match in
        the union signature.  The same is true for the default constructor, which
        always constructs the leftmost default-constructible member.
    4. `get()` and `get_if()` return `Expected<T, BadUnionAccess>` and `Optional<T>`,
        respectively, instead of just `T&` or `T*`.  This allows the type system to
        enforce exhaustive error handling on union access, without substantively
        changing the familiar variant interface.
    5.  `visit()` is implemented according to `bertrand::visit()`, which has much
        better support for partial coverage and heterogenous return types than
       `std::visit()`, once again with exhaustive error handling via
        `Expected<..., BadUnionAccess>` and `Optional<...>`.
    6.  All built-in operators are supported via a monadic interface, forwarding to
        equivalent visitors.  This allows users to chain unions together in a type-safe
        and idiomatic manner, again with exhaustive error handling enforced by the
        compiler.  Any error or empty states will propagate through the chain along
        with the deduced return type(s), and must be acknowledged before the result
        can be safely accessed.
    7.  An extra `flatten()` method is provided to collapse unions of compatible types
        into a single common type where possible.  This can be thought of as an
        explicit exit point for the monadic interface, allowing users to materialize
        results into a scalar type when needed.
    8.  All operations are available at compile time as long as the underlying types
        support them, including `get()`, `visit()`, `flatten()`, and all operator
        overloads.

This class serves as the basis for both `Optional<T>` and `Expected<T, Es...>`, which
together form a complete, safe, and performant type-erasure mechanism for arbitrary C++
code, including value-based exceptions at both compile time and runtime, which must be
explicitly acknowledged through the type system. */
template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        (meta::not_void<Ts> && ...) &&
        meta::types_are_unique<Ts...>
    )
struct Union;


/* A wrapper for an arbitrarily qualified type that can also represent an empty state.

This is similar to `std::optional<T>` but with the following changes:

    1.  `T` may have cvref qualifications, allowing it to model optional references,
        without requiring an extra copy or `std::reference_wrapper` workaround.  These
        function just like raw pointers, but without pointer arithmetic, and instead
        exposing a monadic interface for chaining operations while propagating the
        empty state.
    2.  The whole mechanism is safe at compile time, and can be used in constant
        expressions, where `std::optional` may be invalid.
    3.  `visit()` is supported just as for `Union`s, allowing type-safe access to both
        states.  If the optional is in the empty state, then the visitor will be passed
        a `std::nullopt_t` input, which can be handled explicitly.

`Optional` references can be used as drop-in replacements for raw pointers in most
cases, especially when integrating with Python or other languages where pointers are
not first-class citizens.  The `Union` interface does this automatically for
`get_if()`, avoiding confusion with pointer arithmetic or explicit dereferencing, and
forcing the user to handle the empty state explicitly.  Bertrand's binding generators
will make the same transformation from pointers to `Optional` references automatically
when exporting C++ code to Python. */
template <meta::not_void T> requires (!meta::is<T, std::nullopt_t>)
struct Optional;


template <typename T>
Optional(T) -> Optional<T>;


/* A wrapper for an arbitrarily qualified return type `T` that can also store one or
more possible exception types.

This class effectively encodes the error state(s) into the C++ type system, forcing
downstream users to explicitly acknowledge them without relying on try/catch semantics,
promoting exhaustive error coverage.  This is similar in spirit to
`std::expected<T, E>`, but with the following changes:

    1. `T` may have arbitrary cvref qualifications, allowing it to model functions that
        return references, without requiring an extra copy or `std::reference_wrapper`
        workaround.
    2.  The whole mechanism is safe at compile time, allowing `Expected` wrappers to be
        used in constant expressions, where error handling is typically difficult.
    3.  `E` and `Es...` are constrained to subclasses of `Exception` (the default).
        Note that exception types will not be treated polymorphically, so initializers
        will be sliced to the most proximal error type as specified in the template
        signature.  Users are encouraged to list the exact error types as closely as
        possible to allow the compiler to enforce exhaustive error handling for each
        case.
    4.  The exception state can be trivially re-thrown via `Expected.raise()`, which
        propagates the error using normal try/catch semantics.  Such errors can also be
        caught and converted back to `Expected` wrappers temporarily, allowing
        bidirectional transfer from one domain to the other.
    5.  `Expected` supports the same monadic operator interface as `Union` and
        `Optional`, allowing users to chain operations on the contained value while
        propagating errors and accumulating possible exception states.  The type system
        will force the user to account for all possible exceptions along the chain
        before the result can be safely accessed.

Bertrand uses this class internally to represent any errors that could occur during
normal program execution, outside of debug assertions.  The same convention is
encouraged (though not required) for downstream users as well, so that the compiler
can enforce exhaustive error handling via the type system. */
template <typename T, meta::unqualified E = Exception, meta::unqualified... Es>
    requires (meta::inherits<E, Exception> && ... && meta::inherits<Es, Exception>)
struct Expected;


namespace meta {

    namespace detail {

        template <typename F, typename... Args>
        struct visit {
        private:
            // 1. Expand arguments into a 2D pack of packs representing all possible
            //    permutations of the union types.
            template <typename First, typename... Rest>
            struct permute {
                using type = impl::visitable<First>::pack::template product<
                    typename impl::visitable<Rest>::pack...
                >;
            };
            using permutations = permute<Args...>::type;

            // 2. Filter out any permutations that are not valid inputs to the
            //    visitor `F` and deduce the unique, non-void return types as we go.
            template <typename... permutations>
            struct filter {
                // 2a. `P::template eval<invoke>` detects whether the argument list
                //     represented by permutation `P` is a valid input to the visitor
                //     function `F`, then deduces the return type/noexcept status if
                //     so.  The return type is only appended to `Rs...` if it is not
                //     void and not already present.
                template <typename... As>
                struct invoke {
                    static constexpr bool enable = false;
                    static constexpr bool nothrow = true;
                };
                template <typename... As> requires (meta::invocable<F, As...>)
                struct invoke<As...> {
                    template <typename... Rs>
                    struct helper { using type = bertrand::args<Rs...>; };
                    template <typename... Rs>
                        requires (!::std::same_as<meta::invoke_type<F, As...>, Rs> && ...)
                    struct helper<Rs...> {
                        using type = bertrand::args<Rs..., meta::invoke_type<F, As...>>;
                    };
                    static constexpr bool enable = true;
                    static constexpr bool nothrow = meta::nothrow::invocable<F, As...>;
                    template <typename... Rs>
                    using type = helper<Rs...>::type;
                    static constexpr bool has_void = false;
                };
                template <typename... As> requires (meta::invoke_returns<void, F, As...>)
                struct invoke<As...> {
                    static constexpr bool enable = true;
                    static constexpr bool nothrow = meta::nothrow::invocable<F, As...>;
                    template <typename... Rs>
                    using type = bertrand::args<Rs...>;
                    static constexpr bool has_void = true;
                };

                // 2b. `validate<>` recurs over all permutations, applying the above
                //     criteria.
                template <
                    bool nothrow,  // true if all prior permutations are noexcept
                    bool has_void,  // true if a valid permutation returned void
                    typename returns,  // valid return types found so far
                    typename valid,  // valid permutations found so far
                    typename...
                >
                struct validate {
                    static constexpr bool nothrow_ = nothrow;
                    static constexpr bool consistent = returns::size() <= 1;
                    static constexpr bool has_void_ = has_void;
                    using return_types = returns;
                    using subset = valid;
                };

                // 2d. Valid permutation - check noexcept, deduce return type, append
                //     to valid permutations, and then advance to next permutation
                template <
                    bool nothrow,
                    bool has_void,
                    typename... Rs,
                    typename... valid,
                    typename P,
                    typename... Ps
                > requires (P::template eval<invoke>::enable)
                struct validate<
                    nothrow,
                    has_void,
                    bertrand::args<Rs...>,
                    bertrand::args<valid...>,
                    P,
                    Ps...
                > {
                    using result = validate<
                        nothrow && P::template eval<invoke>::nothrow,
                        has_void || P::template eval<invoke>::has_void,
                        typename P::template eval<invoke>::template type<Rs...>,
                        bertrand::args<valid..., P>,
                        Ps...
                    >;
                    static constexpr bool nothrow_ = result::nothrow_;
                    static constexpr bool consistent = result::consistent;
                    static constexpr bool has_void_ = result::has_void_;
                    using return_types = result::return_types;
                    using subset = result::subset;
                };

                // 2c. Invalid permutation - advance to next permutation
                template <
                    bool nothrow,
                    bool has_void,
                    typename... Rs,
                    typename... valid,
                    typename P,
                    typename... Ps
                > requires (!P::template eval<invoke>::enable)
                struct validate<
                    nothrow,
                    has_void,
                    bertrand::args<Rs...>,
                    bertrand::args<valid...>,
                    P,
                    Ps...
                > {
                    using result = validate<
                        nothrow,
                        has_void,
                        bertrand::args<Rs...>,
                        bertrand::args<valid...>,
                        Ps...
                    >;
                    static constexpr bool nothrow_ = result::nothrow_;
                    static constexpr bool consistent = result::consistent;
                    static constexpr bool has_void_ = result::has_void_;
                    using return_types = result::return_types;
                    using subset = result::subset;
                };

                // 2e. Evaluate the `validate<>` metafunction and report results.
                using result = validate<
                    true,  // initially nothrow by default
                    false,  // initially no void return types
                    bertrand::args<>,  // initially no valid return types
                    bertrand::args<>,  // initially no valid permutations
                    permutations...
                >;
                static constexpr bool nothrow = result::nothrow_;
                static constexpr bool consistent = result::consistent;
                static constexpr bool has_void = result::has_void_;
                using return_types = result::return_types;
                using subset = result::subset;
            };
            using valid_permutations = permutations::template eval<filter>::subset;
            using return_types = permutations::template eval<filter>::return_types;

            // 3. Once all valid permutations and unique return types have been
            //    identified, deduce the final return type and propagate any unhandled
            //    states.
            template <typename... valid>
            struct deduce {
                template <
                    bool option,  // true if a void return type or unhandled empty state exists
                    typename errors,  // tracks unhandled exception states 
                    typename...
                >
                struct get_type;

                // 3a. Iterate over all arguments to discover the precise alternatives
                //     that are left unhandled by the visitor, and apply custom
                //     forwarding behavior on that basis.
                template <bool option, typename... errors, typename A, typename... As>
                struct get_type<option, bertrand::args<errors...>, A, As...> {
                    static constexpr size_t I = sizeof...(Args) - (sizeof...(As) + 1);

                    // 3b. `state<alt>` deduces whether the alternative is handled by the
                    //     visitor, or if it is a special empty or error state, which can
                    //     be implicitly propagated
                    template <typename alt>
                    struct state {
                        template <typename... Ts>
                        struct helper {
                            static constexpr bool value =
                                ::std::same_as<alt, meta::unpack_type<I, Ts...>>;
                        };
                        static constexpr bool handled =
                            (valid::template eval<helper>::value || ...);
                        static constexpr bool empty =
                            meta::is<typename impl::visitable<A>::empty, alt>;
                        static constexpr bool error =
                            impl::visitable<A>::errors::template contains<meta::unqualify<alt>>();
                    };

                    // 3c. For every alternative of `A`, check to see if it is present in
                    //     the valid permutations or can be implicitly propagated.  If not,
                    //     insert a `BadUnionAccess` state into the `errors...` pack.
                    template <typename...>
                    struct scan {
                        template <bool opt, typename... errs>
                        using result = get_type<
                            opt,  // accumulated empty state
                            bertrand::args<errs...>,  // accumulated errors
                            As...
                        >;
                        template <bool opt, typename... errs>
                        using type = result<opt, errs...>::type;
                    };

                    // 3d. If the alternative is explicitly handled by the visitor, then we
                    //     can proceed to the next alternative.
                    template <typename alt, typename... alts>
                    struct scan<alt, alts...> {
                        template <bool opt, typename... errs>
                        using type = scan<alts...>::template type<opt, errs...>;
                    };

                    // 3e. Otherwise, if an unhandled alternative represents the empty
                    //     state of an optional, then we can set `opt` to true and proceed
                    //     to the next alternative.
                    template <typename alt, typename... alts>
                        requires (state<alt>::empty && !state<alt>::handled)
                    struct scan<alt, alts...> {
                        template <bool opt, typename... errs>
                        using type = scan<alts...>::template type<true, errs...>;
                    };

                    // 3f. Otherwise, if an unhandled alternative represents an error state
                    //     of an expected that has not previously been encountered, then we
                    //     can add it to the `errors...` pack and proceed to the next
                    //     alternative.
                    template <typename alt, typename... alts>
                        requires (state<alt>::error && !state<alt>::handled)
                    struct scan<alt, alts...> {
                        template <bool opt, typename... errs>
                        struct helper {
                            using type = scan<alts...>::template type<opt, errs...>;
                        };
                        template <bool opt, typename... errs>
                            requires (!::std::same_as<errs, alt> && ...)
                        struct helper<opt, errs...> {
                            using type = scan<alts...>::template type<
                                opt,
                                errs...,
                                meta::unqualify<alt>
                            >;
                        };
                        template <bool opt, typename... errs>
                        using type = helper<opt, errs...>::type;
                    };

                    // 3g. Otherwise, we must insert a `BadUnionAccess` error state into
                    //     the `errors...` pack and proceed to the next alternative.
                    template <typename alt, typename... alts>
                        requires (
                            !state<alt>::error &&
                            !state<alt>::empty &&
                            !state<alt>::handled
                        )
                    struct scan<alt, alts...> {
                        template <bool opt, typename... errs>
                        struct helper {
                            using type = scan<alts...>::template type<opt, errs...>;
                        };
                        template <bool opt, typename... errs>
                            requires (!::std::same_as<errs, BadUnionAccess> && ...)
                        struct helper<opt, errs...> {
                            using type = scan<alts...>::template type<
                                opt,
                                errs...,
                                BadUnionAccess
                            >;
                        };
                        template <bool opt, typename... errs>
                        using type = helper<opt, errs...>::type;
                    };

                    // 3h. Execute the `scan<>` metafunction.
                    using result = impl::visitable<A>::pack::template eval<scan>;
                    using type = result::template type<option, errors...>;
                };

                // 3i. Once all alternatives have been scanned, deduce the final return
                //     type using the accumulated `option` and `errors...` states.
                template <bool option, typename... errors>
                struct get_type<option, bertrand::args<errors...>> {
                    // 3j. If there are multiple non-void return types, then we need to
                    //     return a `Union` of those types.
                    template <typename... Rs>
                    struct to_union { using type = bertrand::Union<Rs...>; };

                    // 3k. If there is precisely one non-void return type, then that is
                    //     the final return type.
                    template <typename R>
                    struct to_union<R> { using type = R; };

                    // 3l. If no non-void return types are found, then the return type
                    //     must be consistently void by definition.
                    template <>
                    struct to_union<> { using type = void; };

                    // 3m. If `option` is true (indicating either an unhandled empty
                    //     state or void return type), then the result deduces to
                    //     `Optional<R>`, where `R` is the union of all non-void return
                    //     types.  If `R` is itself an optional type, then it will be
                    //     flattened into the output.
                    template <typename R, bool cnd>
                    struct to_optional { using type = R; };
                    template <meta::not_void R>
                    struct to_optional<R, true> {
                        using type = impl::visitable<R>::template to_optional<>;
                    };

                    // 3n. If there are any unhandled error states, then the result
                    //     will be further wrapped in `Expected<R, errors...>` to
                    //     propagate them.  If `R` is itself an expected type, then the
                    //     new errors will be flattened into the output.
                    template <typename R, typename... Es>
                    struct to_expected {
                        using result = impl::visitable<R>::errors::template append<Es...>;
                        using type = result::to_unique::template eval<
                            impl::visitable<R>::template to_expected
                        >;
                    };
                    template <typename R>
                    struct to_expected<R> { using type = R; };

                    // 3o. Evaluate the final return type.
                    using type = to_expected<
                        typename to_optional<
                            typename return_types::template eval<to_union>::type,
                            option
                        >::type,
                        errors...
                    >::type;
                };

                // 3p. evaluate the get_type<> metafunction
                using type = get_type<
                    permutations::template eval<filter>::has_void,
                    bertrand::args<>,
                    Args...
                >::type;
            };

            // 4. Return type conversions are applied at the permutation level to
            //    bypass any wrappers appended by `deduce<>`.
            template <typename... Rs>
            struct _returns {  // no void
                template <typename T>
                static constexpr bool value =
                    (meta::convertible_to<Rs, T> && ...);
                template <typename T>
                static constexpr bool nothrow =
                    (meta::nothrow::convertible_to<Rs, T> && ...);
            };
            template <typename... Rs>
                requires (
                    sizeof...(Rs) > 0 &&
                    permutations::template eval<filter>::has_void
                )
            struct _returns<Rs...> {  // some void, some non-void
                template <typename T>
                static constexpr bool value = (
                    meta::convertible_to<Rs, T> &&
                    ... &&
                    meta::convertible_to<::std::nullopt_t, T>
                );
                template <typename T>
                static constexpr bool nothrow = (
                    meta::nothrow::convertible_to<Rs, T> &&
                    ... &&
                    meta::nothrow::convertible_to<::std::nullopt_t, T>
                );
            };
            template <typename... Rs> requires (sizeof...(Rs) == 0)
            struct _returns<Rs...> {  // all void
                template <typename T>
                static constexpr bool value = meta::convertible_to<void, T>;
                template <typename T>
                    requires (requires{typename impl::visitable<T>::wrapped;})
                static constexpr bool value<T> =
                    value<typename impl::visitable<T>::wrapped>;

                template <typename T>
                static constexpr bool nothrow = meta::nothrow::convertible_to<void, T>;
                template <typename T>
                    requires (requires{typename impl::visitable<T>::wrapped;})
                static constexpr bool nothrow<T> =
                    nothrow<typename impl::visitable<T>::wrapped>;
            };

        public:
            /* The final return type is deduced from the valid argument permutations,
            with special rules for unhandled empty or error states, which can be
            implicitly propagated. */
            using type = valid_permutations::template eval<deduce>::type;

            /* `meta::visitor<F, Args...>` evaluates to true iff at least one valid
            argument permutation exists. */
            static constexpr bool enable =
                valid_permutations::size() > 0;

            /* `meta::exhaustive<F, Args...>` evaluates to true iff all argument
            permutations are valid. */
            static constexpr bool exhaustive =
                permutations::size() == valid_permutations::size();

            /* `meta::consistent<F, Args...>` evaluates to true iff all valid argument
            permutations return the same type, or if there are no valid paths. */
            static constexpr bool consistent =
                permutations::template eval<filter>::consistent;

            /* `meta::nothrow::visitor<F, Args...>` evaluates to true iff all valid
            argument permutations are noexcept or if there are no valid paths. */
            static constexpr bool nothrow =
                permutations::template eval<filter>::nothrow;

            /* `meta::visit_returns<R, F, Args...>` evaluates to true iff the return
            types for all valid permutations are convertible to `R`. */
            template <typename R>
            static constexpr bool returns =
                return_types::template eval<_returns>::template value<R>;

            /* `meta::nothrow::visit_returns<R, F, Args...>` evaluates to true iff the
            return types for all valid permutations are nothrow convertible to `R`. */
            template <typename R>
            static constexpr bool nothrow_returns =
                return_types::template eval<_returns>::template nothrow<R>;
        };

        template <typename F>
        struct visit<F> {
        private:
            template <typename F2>
            struct _type { using type = void; };
            template <meta::invocable F2>
            struct _type<F2> { using type = meta::invoke_type<F2>; };

        public:
            static constexpr bool enable = meta::invocable<F>;
            static constexpr bool exhaustive = enable;
            static constexpr bool consistent = enable;
            static constexpr bool nothrow = meta::nothrow::invocable<F>;
            template <typename R>
            static constexpr bool returns = meta::invoke_returns<R, F>;
            template <typename R>
            static constexpr bool nothrow_returns = meta::nothrow::invoke_returns<R, F>;
            using type = _type<F>::type;
        };

    }

    /* A visitor function can only be applied to a set of arguments if at least one
    permutation of the union types are valid. */
    template <typename F, typename... Args>
    concept visitor =
        detail::visit<F, Args...>::enable &&
        detail::visit<F, Args...>::template returns<
            typename detail::visit<F, Args...>::type
        >;

    /* Specifies that all permutations of the union types must be valid for the visitor
    function. */
    template <typename F, typename... Args>
    concept exhaustive = visitor<F, Args...> && detail::visit<F, Args...>::exhaustive;

    /* Specifies that all valid permutations of the union types have an identical
    return type from the visitor function. */
    template <typename F, typename... Args>
    concept consistent = visitor<F, Args...> && detail::visit<F, Args...>::consistent;

    /* A visitor function returns a new union of all possible results for each
    permutation of the input unions.  If all permutations return the same type, then
    that type is returned instead.  If some of the permutations return `void` and
    others do not, then the result will be wrapped in an `Optional`. */
    template <typename F, typename... Args> requires (visitor<F, Args...>)
    using visit_type = detail::visit<F, Args...>::type;

    /* Tests whether the return type of a visitor is implicitly convertible to the
    expected type for every possible permutation of the arguments, with unions unpacked
    to their individual alternatives. */
    template <typename Ret, typename F, typename... Args>
    concept visit_returns =
        visitor<F, Args...> && detail::visit<F, Args...>::template returns<Ret>;

    namespace nothrow {

        template <typename F, typename... Args>
        concept visitor =
            meta::visitor<F, Args...> &&
            detail::visit<F, Args...>::template nothrow_returns<
                meta::visit_type<F, Args...>
            >;

        template <typename F, typename... Args>
        concept exhaustive =
            visitor<F, Args...> && meta::exhaustive<F, Args...>;

        template <typename F, typename... Args>
        concept consistent =
            visitor<F, Args...> && meta::consistent<F, Args...>;

        template <typename F, typename... Args> requires (visitor<F, Args...>)
        using visit_type = meta::visit_type<F, Args...>;

        template <typename Ret, typename F, typename... Args>
        concept visit_returns =
            visitor<F, Args...> &&
            detail::visit<F, Args...>::template nothrow_returns<Ret>;

    }

}


namespace impl {

    /* Scaling is needed to uniquely encode the index sequence of all possible
    permutations for a given set of union types. */
    template <typename... As>
    static constexpr size_t vtable_size = (impl::visitable<As>::pack::size() * ... * 1);

    /* Helper function to decode a vtable index for the current level of recursion.
    This is exactly equivalent to dividing the index by the appropriate visit scale
    for the subsequent in `A...`, which is equivalent to the size of their cartesian
    product. */
    template <size_t idx, typename... A, size_t... Prev, size_t... Next>
    constexpr size_t visit_index(
        std::index_sequence<Prev...>,
        std::index_sequence<Next...>
    ) noexcept {
        return idx / vtable_size<meta::unpack_type<sizeof...(Prev) + 1 + Next, A...>...>;
    }

    /* Helper function for implementing recursive visit dispatchers.  If this is the
    last argument at the call site, then this will invoke the function directly and
    return the result.  Otherwise, it unpacks the next argument and calls the
    appropriate dispatch implementation. */
    template <
        typename R,
        size_t idx,
        typename T,
        size_t... Prev,
        size_t... Next,
        typename F,
        typename... A
    >
    [[gnu::always_inline]] constexpr R visit_recursive(
        T&& curr,
        std::index_sequence<Prev...>,
        std::index_sequence<Next...>,
        F&& func,
        A&&... args
    ) noexcept(meta::nothrow::visit_returns<R, F, A...>) {
        static constexpr size_t I = sizeof...(Prev);
        if constexpr (I + 1 == sizeof...(A)) {
            if constexpr (meta::is_void<meta::invoke_type<
                F,
                meta::unpack_type<Prev, A...>...,
                T
            >> && meta::default_constructible<R>) {
                std::forward<F>(func)(
                    meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                    std::forward<T>(curr)
                );
                return {};
            } else {
                return std::forward<F>(func)(
                    meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                    std::forward<T>(curr)
                );
            }
        } else {
            return visitable<
                meta::unpack_type<I + 1, A...>
            >::template dispatch<R, idx % vtable_size<
                meta::unpack_type<I + 1 + Next, A...>...
            >>(
                std::make_index_sequence<I + 1>{},
                std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                std::forward<F>(func),
                meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                std::forward<T>(curr),
                meta::unpack_arg<I + 1 + Next>(std::forward<A>(args)...)...
            );
        }
    }

    template <typename T>
    struct visitable {
        static constexpr bool enable = false;  // meta::visitable<T> evaluates to false
        static constexpr bool monad = false;  // meta::monad<T> evaluates to false
        using type = T;
        using pack = bertrand::args<T>;
        using empty = void;  // no empty state
        using errors = bertrand::args<>;  // no error states
        template <typename U = T>  // extra template needed to delay instantiation
        using to_optional = Optional<U>;  // a value for U will never be supplied
        template <typename... Errs>
        using to_expected = Expected<T, Errs...>;

        /* The active index for non-visitable types is trivially zero. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return 0;
        }

        /* Dispatching to a non-visitable type trivially forwards to the next arg. */
        template <
            typename R,  // deduced return type
            size_t idx,  // encoded index of the current argument
                         // -> use impl::visit_index<idx, A...>(prev, next) to decode
            size_t... Prev,  // index sequence over processed arguments
            size_t... Next,  // index sequence over remaining arguments
            typename F,  // visitor function
            typename... A  // partially decoded arguments in-flight
        >
        [[gnu::always_inline]] static constexpr R dispatch(
            std::index_sequence<Prev...> prev,
            std::index_sequence<Next...> next,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visit_returns<R, F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < pack::size() &&
                meta::visit_returns<R, F, A...>
            )
        {
            /// NOTE: this case is manually optimized to minimize compile-time overhead
            static constexpr size_t I = sizeof...(Prev);
            if constexpr (I + 1 == sizeof...(A)) {
                return std::forward<F>(func)(
                    std::forward<A>(args)...  // no extra unpacking
                );
            } else {
                return visitable<meta::unpack_type<I + 1, A...>>::template dispatch<
                    R,
                    idx  // active index is always zero, so no need for adjustments
                >(
                    std::make_index_sequence<I + 1>{},
                    std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                    std::forward<F>(func),
                    std::forward<A>(args)...  // no changes
                );
            }
        }
    };

    /* Unions are converted into a pack of the same length. */
    template <meta::Union T>
    struct visitable<T> {
    private:
        static constexpr size_t N = meta::unqualify<T>::alternatives;

        template <size_t I, typename... Ts>
        struct _pack {
            using type = _pack<
                I + 1,
                Ts...,
                decltype((std::declval<T>().m_storage.template get<I>()))
            >::type;
        };
        template <typename... Ts>
        struct _pack<N, Ts...> { using type = bertrand::args<Ts...>; };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using pack = _pack<0>::type;
        using empty = void;
        using errors = bertrand::args<>;
        template <typename U = T>
        using to_optional = Optional<U>;
        template <typename... Errs>
        using to_expected = Expected<T, Errs...>;

        /* Get the active index for a union of this type. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return u.index();
        }

        /* Perfectly forward the member at index I for a union of this type. */
        template <size_t I, meta::is<T> U> requires (I < pack::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).m_storage.template get<I>())
        ) {
            return (std::forward<U>(u).m_storage.template get<I>());
        }

        /* Dispatch to the proper index of this union to reveal the exact type, then
        recur for the next argument.  If the visitor can't handle the current type,
        then return an error indicating as such. */
        template <
            typename R,
            size_t idx,
            size_t... Prev,
            size_t... Next,
            typename F,
            typename... A
        >
        [[gnu::always_inline]] static constexpr R dispatch(
            std::index_sequence<Prev...> prev,
            std::index_sequence<Next...> next,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visit_returns<R, F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < pack::size() &&
                meta::visit_returns<R, F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);
            if constexpr (meta::visitor<
                F,
                meta::unpack_type<Prev, A...>...,
                decltype((get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)))),
                meta::unpack_type<I + 1 + Next, A...>...
            >) {
                return visit_recursive<R, idx>(
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                    prev,
                    next,
                    std::forward<F>(func),
                    std::forward<A>(args)...
                );
            } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                return BadUnionAccess(
                    "failed to invoke visitor for union argument " +
                    impl::int_to_static_string<I> + " with active type '" + type_name<
                        typename meta::unqualify<T>::template alternative<J>
                    > + "'"
                );
            } else {
                static_assert(
                    meta::convertible_to<BadUnionAccess, R>,
                    "unreachable: a non-exhaustive iterator must always return an "
                    "`Expected` result"
                );
                std::unreachable();
            }
        }
    };

    /* `std::variant`s are treated like unions. */
    template <meta::std::variant T>
    struct visitable<T> {
    private:
        static constexpr size_t N = std::variant_size_v<meta::unqualify<T>>;

        template <size_t I, typename... Ts>
        struct _pack {
            using type = _pack<
                I + 1,
                Ts...,
                decltype((std::get<I>(std::declval<T>())))
            >::type;
        };
        template <typename... Ts>
        struct _pack<N, Ts...> { using type = bertrand::args<Ts...>; };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using pack = _pack<0>::type;
        using empty = void;
        using errors = bertrand::args<>;
        template <typename U = T>
        using to_optional = Optional<U>;
        template <typename... Errs>
        using to_expected = Expected<T, Errs...>;

        /* Get the active index for a union of this type. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return u.index();
        }

        /* Perfectly forward the member at index I for a union of this type. */
        template <size_t I, meta::is<T> U> requires (I < pack::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::get<I>(std::forward<U>(u)))
        ) {
            return (std::get<I>(std::forward<U>(u)));
        }

        /* Dispatch to the proper index of this variant to reveal the exact type, then
        recur for the next argument.  If the visitor can't handle the current type,
        then return an error indicating as such. */
        template <
            typename R,
            size_t idx,
            size_t... Prev,
            size_t... Next,
            typename F,
            typename... A
        >
        [[gnu::always_inline]] static constexpr R dispatch(
            std::index_sequence<Prev...> prev,
            std::index_sequence<Next...> next,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visit_returns<R, F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < pack::size() &&
                meta::visit_returns<R, F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);
            if constexpr (meta::visitor<
                F,
                meta::unpack_type<Prev, A...>...,
                decltype((get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)))),
                meta::unpack_type<I + 1 + Next, A...>...
            >) {
                return visit_recursive<R, idx>(
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                    prev,
                    next,
                    std::forward<F>(func),
                    std::forward<A>(args)...
                );
            } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                return BadUnionAccess(
                    "failed to invoke visitor for union argument " +
                    impl::int_to_static_string<I> + " with active type '" + type_name<
                        typename meta::unqualify<T>::template alternative<J>
                    > + "'"
                );
            } else {
                static_assert(
                    meta::convertible_to<BadUnionAccess, R>,
                    "unreachable: a non-exhaustive iterator must always return an "
                    "`Expected` result"
                );
                std::unreachable();
            }
        }
    };

    /* Optionals are converted into packs of length 2. */
    template <meta::Optional T>
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using wrapped = decltype((std::declval<T>().m_storage.value()));
        using pack = bertrand::args<wrapped, std::nullopt_t>;
        using empty = std::nullopt_t;
        using errors = bertrand::args<>;
        template <typename U = T>
        using to_optional = U;  // always flatten
        template <typename... Errs>
        using to_expected = Expected<T, Errs...>;

        /* The active index of an optional is 0 for the empty state and 1 for the
        non-empty state. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return !u.has_value();
        }

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I == 0 && I < pack::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).m_storage.value())
        ) {
            return (std::forward<U>(u).m_storage.value());
        }

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < pack::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (std::nullopt);
        }

        /* Dispatch to the proper state of this optional, then recur for the next
        argument.  If the visitor can't handle the current state, and it is not the
        empty state, then return an error indicating as such.  Otherwise, implicitly
        propagate the empty state. */
        template <
            typename R,
            size_t idx,
            size_t... Prev,
            size_t... Next,
            typename F,
            typename... A
        >
        [[gnu::always_inline]] static constexpr R dispatch(
            std::index_sequence<Prev...> prev,
            std::index_sequence<Next...> next,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visit_returns<R, F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < pack::size() &&
                meta::visit_returns<R, F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);
            using type = decltype((
                get<J>(meta::unpack_arg<I>(std::forward<A>(args)...))
            ));

            // valid state returns an error if unhandled
            if constexpr (J == 0) {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                        prev,
                        next,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                    return BadUnionAccess(
                        "failed to invoke visitor for empty optional argument " +
                        impl::int_to_static_string<I>
                    );
                } else {
                    static_assert(
                        meta::convertible_to<BadUnionAccess, R>,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }

            // empty state is implicitly propagated if left unhandled
            } else {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                        prev,
                        next,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else if constexpr (meta::convertible_to<type, R>) {
                    return get<J>(meta::unpack_arg<I>(std::forward<A>(args)...));
                } else if constexpr (meta::is_void<R>) {
                    return;
                } else {
                    static_assert(
                        meta::convertible_to<type, R>,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }
            }
        }
    };

    /* `std::optional`s are treated like `Optional`. */
    template <meta::std::optional T>
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using wrapped = decltype((std::declval<T>().value()));
        using pack = bertrand::args<wrapped, std::nullopt_t>;
        using empty = std::nullopt_t;
        using errors = bertrand::args<>;
        template <typename U = T>
        using to_optional = U;  // always flatten
        template <typename... Errs>
        using to_expected = Expected<T, Errs...>;

        /* The active index of an optional is 0 for the empty state and 1 for the
        non-empty state. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return !u.has_value();
        }

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I == 0 && I < pack::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).value())
        ) {
            return (std::forward<U>(u).value());
        }

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < pack::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (std::nullopt);
        }

        /* Dispatch to the proper state of this optional, then recur for the next
        argument.  If the visitor can't handle the current state, and it is not the
        empty state, then return an error indicating as such.  Otherwise, implicitly
        propagate the empty state. */
        template <
            typename R,
            size_t idx,
            size_t... Prev,
            size_t... Next,
            typename F,
            typename... A
        >
        [[gnu::always_inline]] static constexpr R dispatch(
            std::index_sequence<Prev...> prev,
            std::index_sequence<Next...> next,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visit_returns<R, F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < pack::size() &&
                meta::visit_returns<R, F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);
            using type = decltype((
                get<J>(meta::unpack_arg<I>(std::forward<A>(args)...))
            ));

            // valid state returns an error if unhandled
            if constexpr (J == 0) {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                        prev,
                        next,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                    return BadUnionAccess(
                        "failed to invoke visitor for empty optional argument " +
                        impl::int_to_static_string<I>
                    );
                } else {
                    static_assert(
                        meta::convertible_to<BadUnionAccess, R>,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }

            // empty state is implicitly propagated if left unhandled
            } else {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        std::nullopt,
                        prev,
                        next,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else if constexpr (meta::convertible_to<type, R>) {
                    return get<J>(meta::unpack_arg<I>(std::forward<A>(args)...));
                } else if constexpr (meta::is_void<R>) {
                    return;
                } else {
                    static_assert(
                        meta::convertible_to<type, R>,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }
            }
        }
    };

    /* Expecteds are converted into packs including the result type (if not void)
    followed by all error types. */
    template <meta::Expected T>
    struct visitable<T> {
    private:

        template <typename V>
        struct _wrapped { using type = void; };
        template <meta::not_void V>
        struct _wrapped<V> {
            using type = decltype((std::declval<T>().get_result()));
        };

        template <size_t I, typename... Errs>
        struct _pack {
            using type = _pack<
                I + 1,
                Errs...,
                decltype((std::declval<T>().template get_error<I>()))
            >::type;
        };
        template <size_t I, typename... Errs>
            requires (
                meta::not_void<typename meta::unqualify<T>::value_type> &&
                I == meta::unqualify<T>::errors::size()
            )
        struct _pack<I, Errs...> {
            using type = bertrand::args<
                decltype((std::declval<T>().get_result())),
                Errs...
            >;
        };
        template <size_t I, typename... Errs>
            requires (
                meta::is_void<typename meta::unqualify<T>::value_type> &&
                I == meta::unqualify<T>::errors::size()
            )
        struct _pack<I, Errs...> {
            using type = bertrand::args<std::nullopt_t, Errs...>;
        };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using wrapped = _wrapped<typename meta::unqualify<T>::value_type>::type;
        using pack = _pack<0>::type;  // rename these to alternatives
        using empty = void;
        using errors = meta::unqualify<T>::errors;
        template <typename U = T>
        using to_optional = Optional<U>;
        template <typename... Errs>
        using to_expected = Expected<typename meta::unqualify<T>::value_type, Errs...>;

        /* The active index of an expected is 0 for the empty state and > 0 for the
        error state(s). */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            if constexpr (errors::size() > 1) {
                return u.has_result() ? 0 : u.get_error().index() + 1;
            } else {
                return u.has_error();
            }
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I == 0 && meta::not_void<wrapped>)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).get_result())
        ) {
            return (std::forward<U>(u).get_result());
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I == 0 && meta::is_void<wrapped>)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (std::nullopt);
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < pack::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).template get_error<I - 1>())
        ) {
            return (std::forward<U>(u).template get_error<I - 1>());
        }

        /* Dispatch to the proper state of this expected, then recur for the next
        argument.  If the visitor can't handle the current state, and it is not an
        error state, return an error indicating as such.  Otherwise, implicitly
        propagate the error state. */
        template <
            typename R,
            size_t idx,
            size_t... Prev,
            size_t... Next,
            typename F,
            typename... A
        >
        [[gnu::always_inline]] static constexpr R dispatch(
            std::index_sequence<Prev...> prev,
            std::index_sequence<Next...> next,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visit_returns<R, F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < pack::size() &&
                meta::visit_returns<R, F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);
            using type = decltype((
                get<J>(meta::unpack_arg<I>(std::forward<A>(args)...))
            ));

            // valid state returns an error if unhandled
            if constexpr (J == 0) {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                        prev,
                        next,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                    return BadUnionAccess(
                        "failed to invoke visitor for empty expected argument" +
                        impl::int_to_static_string<I>
                    );
                } else {
                    static_assert(
                        meta::convertible_to<BadUnionAccess, R>,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }

            // error states are implicitly propagated if left unhandled
            } else {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                        prev,
                        next,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else if constexpr (meta::convertible_to<type, R>) {
                    return get<J>(meta::unpack_arg<I>(std::forward<A>(args)...));
                } else {
                    static_assert(
                        meta::convertible_to<type, R>,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }
            }
        }
    };

    /* `std::expected`s are treated like `Expected`. */
    template <meta::std::expected T>
    struct visitable<T> {
    private:

        template <typename V>
        struct _wrapped { using type = void; };
        template <meta::not_void V>
        struct _wrapped<V> {
            using type = decltype((std::declval<T>().value()));
        };

        template <typename U>
        struct _pack { using type = args<decltype((std::declval<T>().value()))>; };
        template <meta::is_void U>
        struct _pack<U> { using type = args<std::nullopt_t>; };

        template <typename... Errs>
        struct _to_expected {
            using type =
                std::expected<typename meta::unqualify<T>::value_type, Union<Errs...>>;
        };
        template <typename E>
        struct _to_expected<E> {
            using type =
                std::expected<typename meta::unqualify<T>::value_type, E>;
        };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using wrapped = _wrapped<typename meta::unqualify<T>::value_type>::type;
        using pack =
            _pack<typename meta::unqualify<T>::value_type>::type::template concat<
                visitable<decltype((std::declval<T>().error()))>::pack
            >;
        using empty = void;
        using errors = visitable<decltype((std::declval<T>().error()))>::pack;
        template <typename U = T>
        using to_optional = Optional<U>;
        template <typename... Errs>
        using to_expected = _to_expected<Errs...>::type;

        /* The active index of an expected is 0 for the empty state and > 0 for the
        error state(s). */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            if constexpr (errors::size() > 1) {
                return u.has_value() ?
                    0 :
                    visitable<decltype((u.error()))>::index(u.error()) + 1;
            } else {
                return u.has_value();
            }
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U>
            requires (
                meta::not_void<typename meta::unqualify<T>::value_type> &&
                I == 0 && I < pack::size()
            )
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).value())
        ) {
            return (std::forward<U>(u).value());
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U>
            requires (
                meta::is_void<typename meta::unqualify<T>::value_type> &&
                I == 0 && I < pack::size()
            )
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (std::nullopt);
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < pack::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(visitable<decltype((std::forward<U>(u).error()))>::template get<I - 1>(
                std::forward<U>(u).error()
            ))
        ) {
            return (visitable<decltype((std::forward<U>(u).error()))>::template get<I - 1>(
                std::forward<U>(u).error()
            ));
        }

        /* Dispatch to the proper state of this expected, then recur for the next
        argument.  If the visitor can't handle the current state, and it is not an
        error state, return an error indicating as such.  Otherwise, implicitly
        propagate the error state. */
        template <
            typename R,
            size_t idx,
            size_t... Prev,
            size_t... Next,
            typename F,
            typename... A
        >
        [[gnu::always_inline]] static constexpr R dispatch(
            std::index_sequence<Prev...> prev,
            std::index_sequence<Next...> next,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visit_returns<R, F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < pack::size() &&
                meta::visit_returns<R, F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);
            using type = decltype((
                get<J>(meta::unpack_arg<I>(std::forward<A>(args)...))
            ));

            // valid state returns an error if unhandled
            if constexpr (J == 0) {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                        prev,
                        next,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                    return BadUnionAccess(
                        "failed to invoke visitor for empty expected argument" +
                        impl::int_to_static_string<I>
                    );
                } else {
                    static_assert(
                        meta::convertible_to<BadUnionAccess, R>,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }

            // error states are implicitly propagated if left unhandled
            } else {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                        prev,
                        next,
                        std::forward<F>(func),
                        std::forward<A>(args)...
                    );
                } else if constexpr (meta::convertible_to<type, R>) {
                    return get<J>(meta::unpack_arg<I>(std::forward<A>(args)...));
                } else {
                    static_assert(
                        meta::convertible_to<type, R>,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }
            }
        }
    };

    /* Helper function to compute an encoded index for a sequence of visitable
    arguments at runtime, which can then be used to index into a 1D vtable for all
    arguments simultaneously. */
    constexpr size_t vtable_index(size_t i) noexcept { return i; }
    template <typename A, typename... As>
    constexpr size_t vtable_index(size_t i, const A& a, const As&... as) noexcept {
        return vtable_index(i + (visitable<A>::index(a) * vtable_size<As...>), as...);
    }

}


/* Non-member `visit(f, args...)` operator, similar to `std::visit()`, but with greatly
extended metaprogramming capabilities.  A member version of this operator is provided
for `Union`, `Optional`, and `Expected` objects, which allows for chaining.

The visitor is constructed from either a single function or a set of functions defined
using `bertrand::visitor` or a similar overload set.  The subsequent arguments will be
passed to the visitor in the order they are defined, with each union being unwrapped to
its actual type within the visitor context.  A compilation error will occur if the
visitor is not callable with at least one permutation of the unwrapped values.

Note that the visitor does not need to be exhaustive over all permutations of the
unwrapped values, unlike `std::visit()`.  In that case, the function will return an
`Expected<R, BadUnionAccess>`, where `R` is the deduced return type for the valid
permutations, and `BadUnionAccess` indicates that an invalid permutation was
encountered at runtime.  If `R` is itself an expected type, then the `BadUnionAccess`
state will be flattened into its list of errors automatically.  Alternatively, if the
unhandled case(s) represent the empty state of an optional or error state of an
expected input, then those states will be implicitly propagated to the return type,
potentially converting it into an `Optional` or `Expected` if it was not one already.
This equates to adding implicit overloads to the visitor, which trivially forward these
states without any modifications.  In practice, this means that visitors can treat
optional and expected types as if they were always in the valid state, with invalid
states implicitly propagated unless otherwise handled.  The
`bertrand::meta::exhaustive<F, Args...>` concept can be used to selectively forbid the
non-exhaustive case at compile time, which brings the semantics back to those of
`std::visit()` at the expense of more verbose (and potentially brittle) code.

Similarly, unlike `std::visit()`, the visitor is not constrained to return a single
consistent type for all permutations.  If it does not, then `R` will deduce to
`bertrand::Union<Rs...>`, where `Rs...` are the unique return types for all valid
permutations.  Otherwise, if all permutations return the same type, then `R` will
simply be that type.  If some permutations return `void` and others return non-`void`,
then `R` will be wrapped in an `Optional`, where the empty state indicates a void
return type at runtime.  Similar to above, if `R` is itself an optional type, then
the empty states will be merged so as to avoid nested optionals.  Also as above, the
`bertrand::meta::consistent<F, Args...>` concept can be used to selectively forbid the
inconsistent case at compile time.  Applying that concept in combination with
`bertrand::meta::exhaustive<F, Args...>` will yield the exact same visitor semantics as
`std::visit()`, again at the cost of more verbose (though possibly safer) code.

If neither of the above features are disabled, then a worst-case, non-exhaustive and
inconsistent visitor, where some permutations return void and others do not, can yield
a return type of the form `bertrand::Expected<Optional<Union<Rs...>>, BadUnionAccess>`.
For an exhaustive visitor, this may be reduced to `Optional<Union<Rs...>>`, and for
visitors that never return void, it will simply be `Union<Rs...>`.  If the visitor
further returns a single consistent type, then this will collapse to just that type,
without a union, in which case the semantics are identical to `std::visit()`.

Finally, note that arguments are not strictly limited to union types, in contrast to
`std::visit()`.  If no unions are present, then this function is identical to invoking
the visitor normally, without the `visit()` wrapper.  Custom unions can be accounted
for by specializing the `bertrand::impl::visitable` class for that type, allowing it to
be automatically unwrapped when passed to the `visit()` operator.  This is done by
default for `Union`, `Optional`, and `Expected`, as well as `std::variant`,
`std::optional`, and `std::expected`, all of which can be passed to `visit()` without
any special handling. */
template <typename F, typename... Args> requires (meta::visitor<F, Args...>)
constexpr meta::visit_type<F, Args...> visit(F&& f, Args&&... args)
    noexcept(meta::nothrow::visitor<F, Args...>)
{
    // only generate a vtable if unions are present in the signature
    if constexpr ((meta::visitable<Args> || ...)) {
        using R = meta::visit_type<F, Args...>;
        using T = meta::unpack_type<0, Args...>;

        // flat vtable encodes all permutations simultaneously, minimizing binary size
        // and compile-time/runtime overhead by only doing a single array lookup.
        static constexpr auto vtable =
            []<size_t... Is>(std::index_sequence<Is...>) noexcept {
                return std::array{
                    +[](meta::as_lvalue<F> f, meta::as_lvalue<Args>... args)
                        noexcept(meta::nothrow::visitor<F, Args...>) -> R
                    {
                        return impl::visitable<T>::template dispatch<R, Is>(
                            std::make_index_sequence<0>{},
                            std::make_index_sequence<sizeof...(Args) - 1>{},
                            std::forward<F>(f),
                            std::forward<Args>(args)...
                        );
                    }...
                };
            }(std::make_index_sequence<impl::vtable_size<Args...>>{});

        // `impl::vtable_index()` produces an index into the flat vtable with the
        // appropriate scaling.
        return vtable[impl::vtable_index(0, args...)](f, args...);

    // otherwise, call the function directly
    } else {
        return std::forward<F>(f)(std::forward<Args>(args)...);
    }
}


template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        (meta::not_void<Ts> && ...) &&
        meta::types_are_unique<Ts...>
    )
struct Union : impl::union_tag {
    /* An argument pack holding the templated alternatives for posterity. */
    using types = bertrand::args<Ts...>;

    /* The number of alternatives that were supplied to the union specialization. */
    static constexpr size_t alternatives = sizeof...(Ts);

    /* Get the templated type at index `I`.  Fails to compile if the index is out of
    bounds. */
    template <size_t I> requires (I < alternatives)
    using alternative = meta::unpack_type<I, Ts...>;

    /* Evaluates to true if `T` is contained within the list of alternatives for this
    union.  False otherwise. */
    template <typename T>
    static constexpr bool valid_alternative = (std::same_as<T, Ts> || ...);

    /* Get the index of type `T`, assuming it is present in the union's template
    signature.  Fails to compile otherwise. */
    template <typename T> requires (valid_alternative<T>)
    static constexpr size_t index_of = meta::index_of<T, Ts...>;

private:
    template <typename T>
    friend struct impl::visitable;
    template <meta::not_void T> requires (!meta::is<T, std::nullopt_t>)
    friend struct bertrand::Optional;
    template <typename T, meta::unqualified E, meta::unqualified... Es>
        requires (meta::inherits<E, Exception> && ... && meta::inherits<Es, Exception>)
    friend struct bertrand::Expected;

    // determine the common type for the members of the union, if one exists.
    template <typename... Us>
    struct get_common_type { using type = void; };
    template <typename... Us> requires (meta::has_common_type<Us...>)
    struct get_common_type<Us...> { using type = meta::common_type<Us...>; };

    // Find the first type in Ts... that is default constructible.
    template <typename... Us>
    struct _default_type { using type = void; };
    template <typename U, typename... Us>
    struct _default_type<U, Us...> {
        template <typename V>
        struct filter { using type = _default_type<Us...>::type; };
        template <meta::default_constructible V>
        struct filter<V> { using type = V; };
        using type = filter<U>::type;
    };
    using default_type = _default_type<Ts...>::type;

    // implicit conversion to the union type will make a best effort to find the
    // narrowest possible match from the available options
    template <typename T, typename result, typename... Us>
    struct _proximal_type { using type = result; };
    template <typename T, typename result, typename U, typename... Us>
    struct _proximal_type<T, result, U, Us...> {
        // no match at this index: advance U
        template <typename V>
        struct filter { using type = _proximal_type<T, result, Us...>::type; };

        // prefer the most derived and least qualified matching alternative, with
        // lvalues binding to lvalues and prvalues, and rvalues binding to rvalues and
        // prvalues
        template <meta::inherits<U> V>
            requires (
                meta::convertible_to<V, U> &&
                meta::lvalue<V> ? !meta::rvalue<U> : !meta::lvalue<U>
            )
        struct filter<V> {
            template <typename R>
            struct replace { using type = R; };

            // if the result type is void, or if the candidate is more derived than it,
            // or if the candidate is less qualified, replace the intermediate result
            template <typename R>
                requires (
                    meta::is_void<R> ||
                    (meta::inherits<U, R> && !meta::is<U, R>) || (
                        meta::is<U, R> && (
                            (meta::lvalue<U> && !meta::lvalue<R>) ||
                            meta::more_qualified_than<R, U>
                        )
                    )
                )
            struct replace<R> { using type = U; };

            // recur with updated result
            using type = _proximal_type<T, typename replace<result>::type, Us...>::type;
        };

        // execute the metafunction
        using type = filter<T>::type;
    };
    template <typename T>
    using proximal_type = _proximal_type<T, void, Ts...>::type;

    // implicit conversion operators will only be called if no proximal type is found
    template <typename T, typename S>
    struct _conversion_type { using type = S; };
    template <typename T, meta::is_void S>
    struct _conversion_type<T, S> {
        template <typename... Us>
        struct convert { using type = void; };
        template <typename U, typename... Us>
        struct convert<U, Us...> { using type = convert<Us...>::type; };
        template <typename U, typename... Us>
            requires (!meta::lvalue<U> && meta::convertible_to<T, U>)
        struct convert<U, Us...> { using type = U; };
        using type = convert<Ts...>::type;
    };
    template <typename T>
    using conversion_type = _conversion_type<T, proximal_type<T>>::type;

    template <size_t I>
    struct create_t {};

    // recursive C unions are the only way to make Union<Ts...> provably safe at
    // compile time without tripping the compiler's UB filters
    template <typename... Us>
    union storage {
        constexpr storage() noexcept = default;
        constexpr storage(auto&&) noexcept {};
        constexpr ~storage() noexcept {};
    };
    template <typename U, typename... Us>
    union storage<U, Us...> {
        // C unions are unable to store reference types, so lvalues are converted to
        // pointers and rvalues are stripped
        template <typename V>
        struct _type { using type = meta::remove_rvalue<V>; };
        template <meta::lvalue V>
        struct _type<V> { using type = meta::as_pointer<V>; };
        using type = _type<U>::type;

        type curr;
        storage<Us...> rest;

        // base default constructor
        constexpr storage()
            noexcept(meta::nothrow::default_constructible<default_type>)
            requires(std::same_as<U, default_type>)
        :
            curr()  // not a reference type by definition
        {}

        // recursive default constructor
        constexpr storage()
            noexcept(meta::nothrow::default_constructible<default_type>)
            requires(!std::same_as<U, default_type>)
        :
            rest()
        {}

        // base proximal constructor
        template <typename T> requires (std::same_as<U, proximal_type<T>>)
        constexpr storage(T&& value) noexcept(
            noexcept(type(std::forward<T>(value)))
        ) :
            curr(std::forward<T>(value))
        {}

        // base proximal constructor for lvalue references
        template <typename T>
            requires (std::same_as<U, proximal_type<T>> && meta::lvalue<U>)
        constexpr storage(T&& value) noexcept(
            noexcept(type(&value))
        ) :
            curr(&value)
        {}

        // recursive proximal constructor
        template <typename T>
            requires (
                meta::not_void<proximal_type<T>> &&
                !std::same_as<U, proximal_type<T>>
            )
        constexpr storage(T&& value) noexcept(
            noexcept(storage<Us...>(std::forward<T>(value)))
        ) :
            rest(std::forward<T>(value))
        {}

        // base converting constructor
        template <typename T>
            requires (
                meta::is_void<proximal_type<T>> &&
                std::same_as<U, conversion_type<T>>
            )
        constexpr storage(T&& value) noexcept(
            noexcept(type(std::forward<T>(value)))
        ) :
            curr(std::forward<T>(value))
        {}

        // recursive converting constructor
        template <typename T>
            requires (
                meta::is_void<proximal_type<T>> &&
                !std::same_as<U, conversion_type<T>>
            )
        constexpr storage(T&& value) noexcept(
            noexcept(storage<Us...>(std::forward<T>(value)))
        ) :
            rest(std::forward<T>(value))
        {}

        // base create() constructor
        template <typename... Args>
        constexpr storage(create_t<0>, Args&&... args) noexcept(
            noexcept(type(std::forward<Args>(args)...))
        ) :
            curr(std::forward<Args>(args)...)
        {}

        // recursive create() constructor
        template <size_t J, typename... Args> requires (J > 0)
        constexpr storage(create_t<J>, Args&&... args) noexcept(
            noexcept(storage<Us...>(create_t<J - 1>{}, std::forward<Args>(args)...))
        ) :
            rest(create_t<J - 1>{}, std::forward<Args>(args)...)
        {}

        // destructor
        constexpr ~storage() noexcept {}

        // get() implementation, accounting for lvalues and recursion
        template <size_t I>
        constexpr decltype(auto) get(this auto&& self) noexcept {
            if constexpr (I == 0) {
                if constexpr (meta::lvalue<U>) {
                    return (*std::forward<decltype(self)>(self).curr);
                } else {
                    return (std::forward<decltype(self)>(self).curr);
                }
            } else {
                return (std::forward<decltype(self)>(self).rest.template get<I - 1>());
            }
        }
    };

    storage<Ts...> m_storage;
    unsigned int m_index;  // unsigned int to take advantage of alignment in Ts...

    template <size_t I, typename Self> requires (I < alternatives)
    using access = decltype((std::declval<Self>().m_storage.template get<I>()));

    template <size_t I, typename Self> requires (I < alternatives)
    using exp = Expected<access<I, Self>, BadUnionAccess>;

    template <size_t I, typename Self> requires (I < alternatives)
    using opt = Optional<access<I, Self>>;

    template <size_t I>
    static constexpr auto get_index_error = []<size_t... Is>(std::index_sequence<Is...>) {
        return std::array{+[] {
            return BadUnionAccess(
                impl::int_to_static_string<I> + " is not the active type in the union "
                "(active is " + impl::int_to_static_string<Is> + ")"
            );
        }...};
    }(std::index_sequence_for<Ts...>{});

    template <typename T>
    static constexpr std::array get_type_error {+[] {
        return BadUnionAccess(
            "'" + type_name<T> + "' is not the active type in the union "
            "(active is '" + type_name<Ts> + "')"
        );
    }...};

    struct Flatten {
        using type = get_common_type<Ts...>::type;
        template <typename T>
        static constexpr type operator()(T&& value)
            noexcept(meta::nothrow::convertible_to<T, type>)
            requires(meta::convertible_to<T, type>)
        {
            return std::forward<T>(value);
        }
    };

public:
    /* The common type to which all alternatives can be converted, if such a type
    exists.  Void otherwise. */
    using common_type = get_common_type<Ts...>::type;

    /* True if all alternatives share a common type to which they can be converted.
    If this is false, then the `flatten()` method will be disabled. */
    static constexpr bool can_flatten = meta::not_void<common_type>;

    /* Get the index of the currently-active type in the union. */
    [[nodiscard]] constexpr size_t index() const noexcept {
        return m_index;
    }

    /* Check whether the variant holds a specific type. */
    template <typename T> requires (valid_alternative<T>)
    [[nodiscard]] constexpr bool holds_alternative() const noexcept {
        return m_index == index_of<T>;
    }

    /* Get the value of the type at index `I`.  Fails to compile if the index is out of
    range.  Otherwise, returns an `Expected<T, BadUnionAccess>` where `T` is the type
    at index `I`, forwarded according the qualifiers of the enclosing union. */
    template <size_t I, typename Self> requires (I < alternatives)
    [[nodiscard]] constexpr exp<I, Self> get(this Self&& self) noexcept(
        meta::nothrow::convertible_to<access<I, Self>, exp<I, Self>>
    ) {
        if (self.index() != I) {
            /// TODO: this can't work until static_str is fully defined
            // return get_index_error<I>[self.index()]();
        }
        return std::forward<Self>(self).m_storage.template get<I>();
    }

    /* Get the value for the templated type.  Fails to compile if the templated type
    is not a valid union member.  Otherwise, returns an `Expected<T, BadUnionAccess>`,
    where `T` is forwarded according to the qualifiers of the enclosing union. */
    template <typename T, typename Self> requires (valid_alternative<T>)
    [[nodiscard]] constexpr exp<index_of<T>, Self> get(this Self&& self) noexcept(
        meta::nothrow::convertible_to<access<index_of<T>, Self>, exp<index_of<T>, Self>>
    ) {
        if (self.index() != index_of<T>) {
            /// TODO: this can't work until static_str is fully defined
            // return get_type_error<T>[self.index()]();
        }
        return std::forward<Self>(self).m_storage.template get<index_of<T>>();
    }

    /* Get an optional wrapper for the value of the type at index `I`.  If that is not
    the active type, returns an empty optional instead. */
    template <size_t I, typename Self> requires (I < alternatives)
    [[nodiscard]] constexpr opt<I, Self> get_if(this Self&& self) noexcept(
        meta::nothrow::convertible_to<access<I, Self>, opt<I, Self>>
    ) {
        if (self.index() != I) {
            return {};
        }
        return std::forward<Self>(self).m_storage.template get<I>();
    }

    /* Get an optional wrapper for the value of the templated type.  If that is not
    the active type, returns an empty optional instead. */
    template <typename T, typename Self> requires (valid_alternative<T>)
    [[nodiscard]] constexpr opt<index_of<T>, Self> get_if(this Self&& self) noexcept(
        meta::nothrow::convertible_to<access<index_of<T>, Self>, opt<index_of<T>, Self>>
    ) {
        if (self.index() != index_of<T>) {
            return {};
        }
        return std::forward<Self>(self).m_storage.template get<index_of<T>>();
    }

    /* A member equivalent for `bertrand::visit()`, which always inserts this union as
    the first argument, for chaining purposes.  See `bertrand::visit()` for more
    details. */
    template <typename F, typename Self, typename... Args>
        requires (meta::visitor<F, Self, Args...>)
    constexpr decltype(auto) visit(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visitor<F, Self, Args...>
    ) {
        return (bertrand::visit(
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* Flatten the union into a single type, implicitly converting the active member to
    the common type, assuming one exists.  If no common type exists, then this will
    fail to compile.  Users can check the `can_flatten` flag to guard against this. */
    template <typename Self> requires (can_flatten)
    [[nodiscard]] constexpr common_type flatten(this Self&& self)
        noexcept(meta::nothrow::visit_returns<common_type, Flatten, Self>)
    {
        return bertrand::visit(Flatten{}, std::forward<Self>(self));
    }

private:
    static constexpr bool default_constructible = meta::not_void<default_type>;

    template <typename T>
    static constexpr bool convert_from = meta::not_void<conversion_type<T>>;

    template <size_t I, typename Self, typename T>
    static constexpr bool _convert_to =
        meta::convertible_to<access<I, Self>, T> &&
        _convert_to<I + 1, Self, T>;
    template <typename Self, typename T>
    static constexpr bool _convert_to<alternatives, Self, T> = true;
    template <typename Self, typename T>
    static constexpr bool convert_to = _convert_to<0, Self, T>;

    template <size_t I, typename Self, typename T>
    static constexpr bool _nothrow_convert_to =
        meta::nothrow::convertible_to<access<I, Self>, T> &&
        _nothrow_convert_to<I + 1, Self, T>;
    template <typename Self, typename T>
    static constexpr bool _nothrow_convert_to<alternatives, Self, T> = true;
    template <typename Self, typename T>
    static constexpr bool nothrow_convert_to = _nothrow_convert_to<0, Self, T>;

    template <size_t I, typename Self, typename T>
    static constexpr bool _explicit_convert_to =
        meta::explicitly_convertible_to<access<I, Self>, T> ||
        _explicit_convert_to<I + 1, Self, T>;
    template <typename Self, typename T>
    static constexpr bool _explicit_convert_to<alternatives, Self, T> = false;
    template <typename Self, typename T>
    static constexpr bool explicit_convert_to = _explicit_convert_to<0, Self, T>;

    template <size_t I, typename Self, typename T>
    static constexpr bool _nothrow_explicit_convert_to =
        meta::nothrow::explicitly_convertible_to<access<I, Self>, T> &&
        _nothrow_explicit_convert_to<I + 1, Self, T>;
    template <typename Self, typename T>
    static constexpr bool _nothrow_explicit_convert_to<alternatives, Self, T> = true;
    template <typename Self, typename T>
    static constexpr bool nothrow_explicit_convert_to =
        _nothrow_explicit_convert_to<0, Self, T>;

    template <typename L, typename R>
    static constexpr bool _swappable =
        meta::swappable_with<L, R> || (
            meta::is<L, R> &&
            meta::move_assignable<meta::as_lvalue<L>> &&
            meta::move_assignable<meta::as_lvalue<R>>
        ) || (
            meta::destructible<L> &&
            meta::destructible<R> &&
            meta::movable<L> &&
            meta::movable<R>
        );
    template <typename L, typename R>
    static constexpr bool _nothrow_swappable =
        meta::nothrow::swappable_with<L, R> || (
            meta::is<L, R> &&
            meta::nothrow::move_assignable<meta::as_lvalue<L>> &&
            meta::nothrow::move_assignable<meta::as_lvalue<R>>
        ) || (
            meta::nothrow::destructible<L> &&
            meta::nothrow::destructible<R> &&
            meta::nothrow::movable<L> &&
            meta::nothrow::movable<R>
        );

    template <typename T>
    static constexpr bool swappable = (_swappable<T, Ts> && ...);
    template <typename T>
    static constexpr bool nothrow_swappable = (_nothrow_swappable<T, Ts> && ...);

    template <typename T>
    static constexpr auto pairwise_swap() noexcept {
        constexpr size_t I = index_of<T>;
        return std::array{
            +[](Union& self, Union& other) noexcept((nothrow_swappable<Ts> && ...)) {
                constexpr size_t J = index_of<Ts>;

                // delegate to a swap operator if available
                if constexpr (meta::swappable_with<T, Ts>) {
                    std::ranges::swap(
                        self.m_storage.template get<I>(),
                        other.m_storage.template get<J>()
                    );

                // otherwise, fall back to move assignment operators with a temporary
                // value
                } else if constexpr (
                    meta::is<T, Ts> &&
                    meta::move_assignable<meta::as_lvalue<T>> &&
                    meta::move_assignable<meta::as_lvalue<Ts>>
                ) {
                    T temp = std::move(self).m_storage.template get<I>();
                    try {
                        self.m_storage.template get<I>() =
                            std::move(other).m_storage.template get<J>();
                    } catch (...) {
                        self.m_storage.template get<I>() = std::move(temp);
                        throw;
                    }
                    try {
                        other.m_storage.template get<J>() = std::move(temp);
                    } catch (...) {
                        other.m_storage.template get<J>() =
                            std::move(self).m_storage.template get<I>();
                        self.m_storage.template get<I>() = std::move(temp);
                        throw;
                    }

                // if all else fails move construct into a temporary and destroy the
                // original value, allowing swaps between incompatible types.
                } else {
                    T temp = std::move(self).m_storage.template get<I>();
                    if constexpr (!meta::trivially_destructible<T>) {
                        std::destroy_at(&self.m_storage.template get<I>());
                    }

                    try {
                        std::construct_at(
                            &self.m_storage.template get<J>(),
                            std::move(other).m_storage.template get<J>()
                        );
                        if constexpr (!meta::trivially_destructible<Ts>) {
                            std::destroy_at(&other.m_storage.template get<J>());
                        }
                    } catch (...) {
                        std::construct_at(
                            &self.m_storage.template get<I>(),
                            std::move(temp)
                        );
                        throw;
                    }

                    try {
                        std::construct_at(
                            &other.m_storage.template get<I>(),
                            std::move(temp)
                        );
                    } catch (...) {
                        std::construct_at(
                            &other.m_storage.template get<J>(),
                            std::move(self).m_storage.template get<J>()
                        );
                        if constexpr (!meta::trivially_destructible<Ts>) {
                            std::destroy_at(&self.m_storage.template get<J>());
                        }
                        std::construct_at(
                            &self.m_storage.template get<I>(),
                            std::move(temp)
                        );
                        throw;
                    }
                }
            }...
        };
    }

    // swap operators require a 2D vtable for each pair of types in each union
    template <typename... Us> requires (swappable<Us> && ...)
    static constexpr std::array swap_operators {pairwise_swap<Us>()...};

    template <size_t I, typename... Args>
        requires (I < alternatives && meta::constructible_from<alternative<I>, Args...>)
    constexpr Union(create_t<I>, Args&&... args)
        noexcept(noexcept(storage<Ts...>(create_t<I>{}, std::forward<Args>(args)...)))
    :
        m_storage(create_t<I>{}, std::forward<Args>(args)...),
        m_index(I)
    {}

public:
    /* Construct a union with an explicit type, rather than using the implicit
    constructors, which can introduce ambiguity. */
    template <size_t I, typename... Args>
        requires (I < alternatives && meta::constructible_from<alternative<I>, Args...>)
    static constexpr Union create(Args&&... args)
        noexcept(noexcept(Union(create_t<I>{}, std::forward<Args>(args)...)))
    {
        return {create_t<I>{}, std::forward<Args>(args)...};
    }

    /* Construct a union with an explicit type, rather than using the implicit
    constructors, which can introduce ambiguity. */
    template <typename T, typename... Args>
        requires (valid_alternative<T> && meta::constructible_from<T, Args...>)
    static constexpr Union create(Args&&... args)
        noexcept(noexcept(Union(create_t<index_of<T>>{}, std::forward<Args>(args)...)))
    {
        return {create_t<index_of<T>>{}, std::forward<Args>(args)...};
    }

    /* Default constructor finds the first type in `Ts...` that can be default
    constructed.  If no such type exists, the default constructor is disabled. */
    constexpr Union()
        noexcept(meta::nothrow::default_constructible<default_type>)
        requires(default_constructible)
    :
        m_storage(),
        m_index(index_of<default_type>)
    {}

    /* Conversion constructor finds the most proximal type in `Ts...` that can be
    implicitly converted from the input type.  This will prefer exact matches or
    differences in qualifications (preferring the least qualified) first, then
    inheritance relationships (preferring the most derived and least qualified), and
    finally implicit conversions (preferring the first match and ignoring lvalues).  If
    no such type exists, the conversion constructor is disabled. */
    template <typename T> requires (convert_from<T>)
    constexpr Union(T&& v)
        noexcept(meta::nothrow::convertible_to<T, conversion_type<T>>)
    :
        m_storage(std::forward<T>(v)),
        m_index(index_of<conversion_type<T>>)
    {}

    /* Copy constructor.  The resulting union will have the same index as the input
    union, and will be initialized by copy constructing the other stored type. */
    constexpr Union(const Union& other)
        noexcept((meta::nothrow::copyable<Ts> && ...))
        requires((meta::copyable<Ts> && ...))
    :
        m_storage(bertrand::visit(
            [](const auto& value) -> storage<Ts...> { return {value}; },
            other
        )),
        m_index(other.index())
    {}

    /* Move constructor.  The resulting union will have the same index as the input
    union, and will be initialized by move constructing the other stored type. */
    constexpr Union(Union&& other)
        noexcept((meta::nothrow::movable<meta::remove_rvalue<Ts>> && ...))
        requires((meta::movable<meta::remove_rvalue<Ts>> && ...))
    :
        m_storage(bertrand::visit(
            [](auto&& value) -> storage<Ts...> {
                return {std::forward<decltype(value)>(value)};
            },
            std::move(other)
        )),
        m_index(other.index())
    {}

    /* Copy assignment operator.  Destroys the current value and then copy constructs
    the other stored type into the active index. */
    constexpr Union& operator=(const Union& other)
        noexcept(
            (meta::nothrow::copyable<meta::remove_rvalue<Ts>> && ...) &&
            (nothrow_swappable<meta::remove_rvalue<Ts>> && ...)
        )
        requires(
            (meta::copyable<meta::remove_rvalue<Ts>> && ...) &&
            (swappable<meta::remove_rvalue<Ts>> && ...)
        )
    {
        if (this != &other) {
            Union temp(other);
            swap_operators<meta::remove_rvalue<Ts>...>[index()][temp.index()](
                *this,
                temp
            );
        }
        return *this;
    }

    /* Move assignment operator.  Destroys the current value and then move constructs
    the other stored type into the active index. */
    constexpr Union& operator=(Union&& other)
        noexcept(
            (meta::nothrow::movable<meta::remove_rvalue<Ts>> && ...) &&
            (nothrow_swappable<meta::remove_rvalue<Ts>> && ...)
        )
        requires(
            (meta::movable<meta::remove_rvalue<Ts>> && ...) &&
            (swappable<meta::remove_rvalue<Ts>> && ...)
        )
    {
        if (this != &other) {
            Union temp(std::move(other));
            swap_operators<meta::remove_rvalue<Ts>...>[index()][temp.index()](
                *this,
                temp
            );
        }
        return *this;
    }

    /* Destructor destroys the currently-active type. */
    constexpr ~Union()
        noexcept((meta::nothrow::destructible<meta::remove_rvalue<Ts>> && ...))
        requires((meta::destructible<meta::remove_rvalue<Ts>> && ...))
    {
        static constexpr std::array destructors {
            +[](Union& self) noexcept(
                meta::nothrow::destructible<meta::remove_rvalue<Ts>>
            ) {
                if constexpr (!meta::trivially_destructible<meta::remove_rvalue<Ts>>) {
                    std::destroy_at(
                        &self.m_storage.template get<types::template index<Ts>()>()
                    );
                }
            }...
        };
        destructors[index()](*this);
    }

    /* Implicit conversion from `Union<Ts...>` to `std::variant<Us...>` and any other
    similar type to which all alternatives can be implicitly converted. */
    template <typename Self, typename T> requires (convert_to<Self, T>)
    [[nodiscard]] constexpr operator T(this Self&& self) noexcept(
        nothrow_convert_to<Self, T>
    ) {
        return bertrand::visit([](auto&& value) noexcept(
            nothrow_convert_to<Self, T>
        ) -> T {
            return std::forward<decltype(value)>(value);
        }, std::forward<Self>(self));
    }

    /* Explicit conversion from `Union<Ts...>` to any type that at least one member is
    explicitly convertible to.  If the active member does not support the conversion,
    then a `TypeError` will be thrown instead.  This equates to a functional-style cast
    from the union to type `T`. */
    template <typename Self, typename T>
        requires (!convert_to<Self, T> && explicit_convert_to<Self, T>)
    [[nodiscard]] constexpr explicit operator T(this Self&& self) noexcept(
        nothrow_explicit_convert_to<Self, T>
    ) {
        return bertrand::visit([]<typename V>(V&& value) noexcept(
            nothrow_explicit_convert_to<Self, T>
        ) -> T {
            if constexpr (meta::explicitly_convertible_to<V, T>) {
                return static_cast<T>(value);
            } else {
                throw TypeError(
                    "Cannot convert union with active type '" + type_name<V> +
                    "' to type '" + type_name<T> + "'"
                );
            }
        }, std::forward<Self>(self));
    }

    /* Swap the contents of two unions as efficiently as possible. */
    constexpr void swap(Union& other)
        noexcept((nothrow_swappable<Ts> && ...))
        requires((swappable<Ts> && ...))
    {
        if (this != &other) {
            swap_operators<Ts...>[index()][other.index()](*this, other);
            std::swap(m_index, other.m_index);
        }
    }

    /* Monadic call operator.  If any of the union types are function-like objects that
    are callable with the given arguments, then this will return the result of that
    function, possibly wrapped in another union to represent heterogenous results.  If
    some of the return types are `void` and others are not, then the result may be
    converted to `Optional<R>`, where `R` is the return type(s) of the invocable
    functions.  If not all of the union types are invocable with the given arguments,
    then the result will be further wrapped in an `Expected<R, BadUnionAccess>`, just
    as for `bertrand::visit()`. */
    template <typename Self, typename... Args>
        requires (meta::visitor<impl::Call, Self, Args...>)
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args) noexcept(
        meta::nothrow::visitor<impl::Call, Self, Args...>
    ) {
        return (bertrand::visit(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* Monadic subscript operator.  If any of the union types support indexing with the
    given key, then this will return the result of that operation, possibly wrapped in
    another union to represent heterogenous results.  If some of the return types are
    `void` and others are not, then the result may be converted to `Optional<R>`, where
    `R` is the return type(s) of the indexable types.  If not all of the union types
    are indexable with the given arguments, then the result will be further wrapped in
    an `Expected<R, BadUnionAccess>`, just as for `bertrand::visit()`. */
    template <typename Self, typename... Key>
        requires (meta::visitor<impl::Subscript, Self, Key...>)
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys) noexcept(
        meta::nothrow::visitor<impl::Subscript, Self, Key...>
    ) {
        return (bertrand::visit(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<Key>(keys)...
        ));
    }
};


template <meta::not_void T> requires (!meta::is<T, std::nullopt_t>)
struct Optional : impl::optional_tag {
    using value_type = T;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<value_type>;
    using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

private:
    template <typename U>
    friend struct impl::visitable;

    template <typename U>
    struct storage {
        Union<std::nullopt_t, T> data;

        constexpr storage() noexcept : data(std::nullopt) {};

        template <meta::convertible_to<value_type> V>
        constexpr storage(V&& value)
            noexcept(meta::nothrow::convertible_to<V, value_type>)
        :
            data(std::forward<V>(value))
        {}

        constexpr void swap(storage& other)
            noexcept(noexcept(data.swap(other.data)))
            requires(requires{data.swap(other.data);})
        {
            data.swap(other.data);
        }

        constexpr void reset() noexcept(noexcept(data = std::nullopt)) {
            data = std::nullopt;
        }

        constexpr bool has_value() const noexcept {
            return data.index() == 1;
        }

        template <typename Self>
        constexpr decltype(auto) value(this Self&& self) noexcept {
            return std::forward<Self>(self).data.m_storage.template get<1>();
        }
    };

    template <meta::lvalue U>
    struct storage<U> {
        pointer data = nullptr;
        constexpr storage() noexcept = default;
        constexpr storage(U value) noexcept(noexcept(&value)) : data(&value) {}
        constexpr void swap(storage& other) noexcept { std::swap(data, other.data); }
        constexpr void reset() noexcept { data = nullptr; }
        constexpr bool has_value() const noexcept { return data != nullptr; }
        template <typename Self>
        constexpr decltype(auto) value(this Self&& self) noexcept {
            return *std::forward<Self>(self).data;
        }
    };

    storage<T> m_storage;

    template <typename Self>
    using access = decltype((std::declval<Self>().m_storage.value()));

    template <typename F, typename Self, typename... Args>
        requires (meta::invocable<F, access<Self>, Args...>)
    struct and_then_fn {
        template <meta::is<value_type> V>
        static constexpr decltype(auto) operator()(auto&& func, V&& value, auto&&... args)
            noexcept(meta::nothrow::invocable<F, V, Args...>)
            requires(meta::invocable<F, V, Args...>)
        {
            return (std::forward<F>(func)(
                std::forward<V>(value),
                std::forward<Args>(args)...
            ));
        }
    };

    template <typename F, typename Self, typename... Args>
        requires (meta::invocable<F, Args...>)
    struct or_else_fn {
        template <meta::is<value_type> V>
        static constexpr decltype(auto) operator()(auto&& func, V&& value, auto&&... args)
            noexcept
        {
            return (std::forward<V>(value));
        }
        template <typename V> requires (!meta::is<value_type, V>)
        static constexpr decltype(auto) operator()(auto&& func, V&&, auto&&... args)
            noexcept(meta::nothrow::invocable<F, Args...>)
            requires(meta::invocable<F, Args...>)
        {
            return (std::forward<F>(func)(std::forward<Args>(args)...));
        }
    };

public:
    /* Default constructor.  Initializes the optional in the empty state. */
    [[nodiscard]] constexpr Optional(std::nullopt_t t = std::nullopt) noexcept {}

    /* Converting constructor.  Implicitly converts the input to the value type, and
    then initializes the optional in that state. */
    template <meta::convertible_to<value_type> V>
    [[nodiscard]] constexpr Optional(V&& v) noexcept(
        meta::nothrow::convertible_to<V, value_type>
    ) : 
        m_storage(std::forward<V>(v))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the value type's
    constructor, calls it, and then initializes the optional with the result. */
    template <typename... Args> requires (meta::constructible_from<value_type, Args...>)
    [[nodiscard]] constexpr explicit Optional(Args&&... args) noexcept(
        meta::nothrow::constructible_from<value_type, Args...>
    ) :
        m_storage(value_type(std::forward<Args>(args)...))
    {}

    /* Implicit conversion from `Optional<T>` to `std::optional<T>` and other similar
    types that are convertible from both the value type and `std::nullopt`. */
    template <typename Self, typename V>
        requires (
            meta::convertible_to<access<Self>, V> &&
            meta::convertible_to<std::nullopt_t, V>
        )
    [[nodiscard]] constexpr operator V(this Self&& self) noexcept(
        meta::nothrow::convertible_to<access<Self>, V> &&
        meta::nothrow::convertible_to<std::nullopt_t, V>
    ) {
        if (self.has_value()) {
            return std::forward<Self>(self).m_storage.value();
        }
        return std::nullopt;
    }

    /* Implicit conversion from `Optional<T&>` to pointers and other similar types that
    are convertible from both the address type of the value and `nullptr`. */
    template <typename Self, typename V>
        requires (
            meta::lvalue<value_type> &&
            meta::convertible_to<meta::address_type<access<Self>>, V> &&
            meta::convertible_to<std::nullptr_t, V> &&
            !meta::convertible_to<access<Self>, V> &&
            !meta::convertible_to<std::nullopt_t, V>
        )
    [[nodiscard]] constexpr operator V(this Self&& self) noexcept(
        meta::nothrow::convertible_to<meta::address_type<access<Self>>, V> &&
        meta::nothrow::convertible_to<std::nullptr_t, V>
    ) {
        if (self.has_value()) {
            return &self.m_storage.value();
        }
        return nullptr;
    }

    /* Swap the contents of two optionals as efficiently as possible. */
    constexpr void swap(Optional& other)
        noexcept(noexcept(m_storage.swap(other.m_storage)))
        requires(requires{m_storage.swap(other.m_storage);})
    {
        if (this != &other) {
            m_storage.swap(other.m_storage);
        }
    }

    /* Clear the current value, transitioning the optional to the empty state. */
    constexpr void reset() noexcept(noexcept(m_storage.reset())) {
        m_storage.reset();
    }

    /* Returns `true` if the optional currently holds a value, or `false` if it is in
    the empty state. */
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return m_storage.has_value();
    }

    /* Access the stored value.  Throws a `BadUnionAccess` assertion if the program is
    compiled in debug mode and the optional is currently in the empty state. */
    template <typename Self>
    [[nodiscard]] constexpr access<Self> value(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.has_value()) {
                throw BadUnionAccess("Cannot access value of an empty Optional");
            }
        }
        return std::forward<Self>(self).m_storage.value();
    }

    /* Access the stored value or return the default value if the optional is empty,
    converting to the common type between the wrapped type and the default value. */
    template <typename Self, typename V>
        requires (meta::has_common_type<access<Self>, V>)
    [[nodiscard]] constexpr meta::common_type<access<Self>, V> value_or(
        this Self&& self,
        V&& fallback
    ) noexcept(
        meta::nothrow::convertible_to<access<Self>, meta::common_type<access<Self>, V>> &&
        meta::nothrow::convertible_to<V, meta::common_type<access<Self>, V>>
    ) {
        if (self.has_value()) {
            return std::forward<Self>(self).m_storage.value();
        } else {
            return std::forward<V>(fallback);
        }
    }

    /* A member equivalent for `bertrand::visit()`, which always inserts this optional
    as the first argument, for chaining purposes.  See `bertrand::visit()` for more
    details.

    For optionals, the visitor function only needs to handle two cases corresponding
    to the empty and non-empty states.  If the optional is empty, then the visitor will
    be invoked with an argument of type `std::nullopt_t`.  Otherwise, it will be
    invoked with the output from `.value()`. */
    template <typename F, typename Self, typename... Args>
        requires (meta::visitor<F, Self, Args...>)
    constexpr decltype(auto) visit(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visitor<F, Self, Args...>
    ) {
        return (bertrand::visit(
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* If the optional is not empty, invoke a given function on the wrapped value,
    otherwise propagate the empty state.  This is identical to `visit()`, except that
    the visitor is forced to cover the non-empty state, and will not be invoked in the
    empty case.  All other rules (including flattening or handling of void return
    types) remain the same.

    For most visitors:

                self                    invoke                      result
        -------------------------------------------------------------------------
        1.  Optional<T>(empty)      (no call)                   Optional<U>(empty)
        2.  Optional<T>(t)          f(t, args...) -> empty      Optional<U>(empty)
        3.  Optional<T>(t)          f(t, args...) -> u          Optional<U>(u)

    If the visitor returns void:

                self                    invoke                      result
        -------------------------------------------------------------------------
        1.  Optional<T>(empty)      (no call)                   void
        2.  Optional<T>(t)          f(t, args...) -> void       void

    Some functional languages call this operation "flatMap" or "bind".

    NOTE: the extra flattening and void handling by this method differs from many
    functional languages, as well as `std::optional::and_then()` and
    `std::optional::transform()`.  For those methods, the former requires the visitor
    to always return another `std::optional`, whereas the latter implicitly promotes
    the return type to `std::optional`, even if it was one already (possibly resulting
    in a nested optional).  Thanks to the use of `visit()` under the hood, Bertrand's
    `and_then()` method unifies both models by allowing the function to return either
    an optional (of any kind, possibly nested), which will be used directly (merging
    with the original empty state), or a non-optional, which will be promoted to an
    optional unless it is void.  This guards against accidental nested optionals, which
    Bertrand considers to be an anti-pattern.  This is especially true if the nesting
    is dependent on the order of operations, which can lead to brittle code and subtle
    bugs.  If the empty states must remain distinct, then it is always preferable to
    explicitly handle the original empty state before chaining, or by converting the
    optional into a `Union<Ts...>` or `Expected<T, Es...>` beforehand, both of which
    can represent multiple distinct empty states without nesting. */
    template <typename Self, typename F, typename... Args>
        requires (
            meta::invocable<F, access<Self>, Args...> &&
            meta::visitor<and_then_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) and_then(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visitor<and_then_fn<F, Self, Args...>, F, Self, Args...>
    ) {
        return (bertrand::visit(
            and_then_fn<F, Self, Args...>{},
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* If the optional is empty, invoke a given function, otherwise propagate the
    non-empty state.  This is identical to `visit()`, except that the visitor does not
    need to accept `std::nullopt_t` explicitly, and will not be invoked if the optional
    holds a value.  All other rules (including promotion to union or handling of void
    return types) remain the same.

    For most visitors:

                self                    invoke                      result
        -------------------------------------------------------------------------
        1.  Optional<T>(empty)      f(args...) -> u             Union<T, U>(u)
        2.  Optional<T>(t)          (no call)                   Union<T, U>(t)

    If the visitor returns void:

                self                    invoke                      result
        -------------------------------------------------------------------------
        1.  Optional<T>(empty)      f(args...) -> void          Optional<T>(empty)
        2.  Optional<T>(t)          (no call)                   Optional<T>(t)

    NOTE: the extra union promotion and void handling by this method differs from many
    functional languages, as well as from `std::optional::or_else()`, which forces the
    function to always return another `std::optional` of the same type as the input.
    Thanks to the use of `visit()` under the hood, Bertrand's `or_else()` method is not
    as restrictive, and allows the function to possibly return a different type, in
    which case the result will be promoted to a union of the original optional type and
    the function's return type.  Functions that return void will map to optionals,
    where the original empty state will be preserved.  This is done for consistency
    with `visit()` as well as `and_then()`, all of which use the same underlying
    infrastructure. */
    template <typename Self, typename F, typename... Args>
        requires (
            meta::invocable<F, Args...> &&
            meta::visitor<or_else_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) or_else(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visitor<or_else_fn<F, Self, Args...>, F, Self, Args...>
    ) {
        return (bertrand::visit(
            or_else_fn<F, Self, Args...>{},
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* If the optional is not empty, invoke a boolean predicate on the value, returning
    a new optional with an identical value if the result is `true`, or an empty
    optional if it is `false`.  Any additional arguments in `Args...` will be
    perfectly forwarded to the predicate function in the order they are provided.
    If the original optional was empty to begin with, the predicate will not be
    invoked, and the optional will remain in the empty state.

                self                    invoke                      result
        -------------------------------------------------------------------------
        1.  Optional<T>(empty)      (no call)                   Optional<T>(empty)
        2.  Optional<T>(t)          f(t, args...) -> false      Optional<T>(empty)
        3.  Optional<T>(t)          f(t, args...) -> true       Optional<T>(t)
    */
    template <typename Self, typename F, typename... Args>
        requires (
            meta::invocable<F, access<Self>, Args...> &&
            meta::explicitly_convertible_to<
                meta::invoke_type<F, access<Self>, Args...>,
                bool
            >
        )
    constexpr Optional filter(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::invocable<F, access<Self>, Args...> &&
        meta::nothrow::explicitly_convertible_to<
            meta::invoke_type<F, access<Self>, Args...>,
            bool
        >
    ) {
        if (!self.has_value() || !std::forward<F>(f)(
            std::forward<Self>(self).m_storage.value(),
            std::forward<Args>(args)...
        )) {
            return {};
        }
        return std::forward<Self>(self);
    }

    /* Monadic call operator.  If the optional type is a function-like object and is
    not in the empty state, then this will return the result of that function wrapped
    in another optional.  Otherwise, it will propagate the empty state.  If the
    function returns void, then the result will be void in both cases, and the function
    will not be invoked for the empty state. */
    template <typename Self, typename... Args>
        requires (meta::visitor<impl::Call, Self, Args...>)
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args) noexcept(
        meta::nothrow::visitor<impl::Call, Self, Args...>
    ) {
        return (bertrand::visit(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* Monadic subscript operator.  If the optional type is a container that supports
    indexing with the given key, then this will return the result of the access wrapped
    in another optional.  Otherwise, it will propagate the empty state. */
    template <typename Self, typename... Key>
        requires (meta::visitor<impl::Subscript, Self, Key...>)
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys) noexcept(
        meta::nothrow::visitor<impl::Subscript, Self, Key...>
    ) {
        return (bertrand::visit(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<Key>(keys)...
        ));
    }
};


/// TODO: All exception types should have a .swap() method, so that I can generically
/// call swap() on the error type and it will handle both the union and scalar cases.
/// Then I just need to focus on swapping the result types, which is simpler.


template <typename T, meta::unqualified E, meta::unqualified... Es>
    requires (meta::inherits<E, Exception> && ... && meta::inherits<Es, Exception>)
struct Expected : impl::expected_tag {
    using errors = bertrand::args<E, Es...>;
    using value_type = T;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<value_type>;
    using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

private:
    template <typename U>
    friend struct impl::visitable;

    /* Given an initializer that inherits from at least one exception type, determine
    the most proximal exception to initialize. */
    template <typename out, typename...>
    struct _err_type { using type = out; };
    template <typename out, typename in, typename curr, typename... next>
    struct _err_type<out, in, curr, next...> {
        using type = _err_type<out, in, next...>::type;
    };
    template <typename out, typename in, typename curr, typename... next>
        requires (!meta::constructible_from<value_type, in> && meta::inherits<in, curr>)
    struct _err_type<out, in, curr, next...> {
        template <typename prev>
        struct most_derived { using type = _err_type<prev, in, next...>::type; };
        template <typename prev>
            requires (meta::is_void<prev> || meta::inherits<curr, prev>)
        struct most_derived<prev> { using type = _err_type<curr, in, next...>::type; };
        using type = most_derived<out>::type;
    };
    template <typename in>
    using err_type = _err_type<void, in, E, Es...>::type;

    /* Convert void results into an empty type that can be stored in a union. */
    template <typename U>
    struct _Result { using type = U; };
    template <meta::is_void U>
    struct _Result<U> { struct type {}; };
    using Result = _Result<value_type>::type;

    /* If only one exception state is given, store that directly.  Otherwise, store a
    union of all exception states. */
    template <typename... Us>
    struct _Error {
        using type = Union<Us...>;
        template <typename V>
        static constexpr type operator()(V&& e) noexcept(
            noexcept(type::template create<err_type<V>>(std::forward<V>(e)))
        ) {
            return type::template create<err_type<V>>(std::forward<V>(e));
        }
    };
    template <typename U>
    struct _Error<U> {
        using type = U;
        template <typename V>
        static constexpr type operator()(V&& e) noexcept(
            noexcept(type(std::forward<V>(e)))
        ) {
            return type(std::forward<V>(e));
        }
    };
    using Error = _Error<E, Es...>::type;

    using storage = Union<Result, Error>;
    storage m_storage;

    template <typename Self>
    constexpr decltype(auto) get_result(this Self&& self) noexcept {
        return (std::forward<Self>(self).m_storage.m_storage.template get<0>());
    }

    template <typename Self>
    constexpr decltype(auto) get_error(this Self&& self) noexcept {
        return (std::forward<Self>(self).m_storage.m_storage.template get<1>());
    }

    template <size_t I, typename Self> requires (I < errors::size())
    constexpr decltype(auto) get_error(this Self&& self) noexcept {
        if constexpr (sizeof...(Es)) {
            return (std::forward<Self>(self).get_error().m_storage.template get<I>());
        } else {
            return (std::forward<Self>(self).get_error());
        }
    }

    template <typename U, typename Self> requires (errors::template contains<U>())
    constexpr decltype(auto) get_error(this Self&& self) noexcept {
        return (
            std::forward<Self>(self).template get_error<errors::template index<U>()>()
        );
    }

    template <typename Self>
    using access = decltype((std::declval<Self>().get_result()));

    template <typename Self>
    using access_error = decltype((std::declval<Self>().get_error()));

    template <size_t I, typename Self>
    using access_at = decltype((std::declval<Self>().template get_error<I>())); 

    template <typename U, typename Self>
    using access_type = decltype((std::declval<Self>().template get_error<U>()));

    template <typename U>
    struct monadic {
        template <typename F, typename Self, typename... Args>
            requires (meta::invocable<F, access<Self>, Args...>)
        struct and_then_fn {
            template <meta::is<value_type> V>
            static constexpr decltype(auto) operator()(
                auto&& func,
                V&& value,
                auto&&... args
            )
                noexcept(meta::nothrow::invocable<F, V, Args...>)
                requires(meta::invocable<F, V, Args...>)
            {
                return (std::forward<F>(func)(
                    std::forward<V>(value),
                    std::forward<Args>(args)...
                ));
            }
        };

        template <typename F, typename Self, typename... Args>
            requires (
                meta::visitor<F, access_error<Self>, Args...> &&
                meta::has_common_type<
                    access<Self>,
                    meta::visit_type<F, access_error<Self>, Args...>
                >
            )
        struct or_else_fn {
            template <meta::is<value_type> V>
            static constexpr decltype(auto) operator()(
                auto&& func,
                V&& value,
                auto&&... args
            ) noexcept {
                return (std::forward<V>(value));
            }
            template <typename Err> requires (!meta::is<value_type, Err>)
            static constexpr decltype(auto) operator()(
                auto&& func,
                Err&& error,
                auto&&... args
            )
                noexcept(meta::nothrow::invocable<F, Err, Args...>)
                requires(meta::invocable<F, Err, Args...>)
            {
                return (std::forward<F>(func)(
                    std::forward<Err>(error),
                    std::forward<Args>(args)...
                ));
            }
        };
    };
    template <meta::is_void U>
    struct monadic<U> {
        template <typename F, typename Self, typename... Args>
            requires (meta::invocable<F, Args...>)
        struct and_then_fn {
            template <meta::is<std::nullopt_t> V>
            static constexpr decltype(auto) operator()(
                auto&& func,
                V&& value,
                auto&&... args
            )
                noexcept(meta::nothrow::invocable<F, Args...>)
                requires(meta::invocable<F, Args...>)
            {
                return (std::forward<F>(func)(std::forward<Args>(args)...));
            }
        };

        template <typename F, typename Self, typename... Args>
            requires (meta::visitor<F, access_error<Self>, Args...>)
        struct or_else_fn {
            template <meta::is<std::nullopt_t> V>
            static constexpr void operator()(auto&& func, V&&, auto&&... args) noexcept {}
            template <typename Err> requires (!meta::is<std::nullopt_t, Err>)
            static constexpr decltype(auto) operator()(
                auto&& func,
                Err&& error,
                auto&&... args
            )
                noexcept(meta::nothrow::invocable<F, Err, Args...>)
                requires(meta::invocable<F, Err, Args...>)
            {
                return (std::forward<F>(func)(
                    std::forward<Err>(error),
                    std::forward<Args>(args)...
                ));
            }
        };
    };

    template <typename F, typename Self, typename... Args>
        requires (requires{
            typename monadic<value_type>::template and_then_fn<F, Self, Args...>;
        })
    using and_then_fn = monadic<value_type>::template and_then_fn<F, Self, Args...>;

    template <typename F, typename Self, typename... Args>
        requires (requires{
            typename monadic<value_type>::template or_else_fn<F, Self, Args...>;
        })
    using or_else_fn = monadic<value_type>::template or_else_fn<F, Self, Args...>;

public:
    /* Converting constructor for `value_type`. */
    template <meta::convertible_to<Result> V>
    [[nodiscard]] constexpr Expected(V&& v) noexcept(
        meta::nothrow::convertible_to<V, Result>
    ) :
        m_storage(std::forward<V>(v))
    {}

    /* Forwarding constructor for `value_type`. */
    template <typename... Args> requires (meta::constructible_from<Result, Args...>)
    [[nodiscard]] constexpr Expected(Args&&... args) noexcept(
        meta::nothrow::constructible_from<Result, Args...> &&
        meta::nothrow::convertible_to<Result, storage>
    ) :
        m_storage(Result(std::forward<Args>(args)...))
    {}

    /* Error constructor.  This constructor only participates in overload resolution if
    the input inherits from one of the errors in the template signature and is not a
    valid initializer for `value_type`. */
    template <typename V>
        requires (
            !meta::constructible_from<Result, V> &&
            (meta::inherits<V, E> || ... || meta::inherits<V, Es>)
        )
    [[nodiscard]] constexpr Expected(V&& e) noexcept(
        meta::nothrow::convertible_to<V, storage>
    ) :
        m_storage(std::forward<V>(e))
    {}

    /* Swap the contents of two expecteds as efficiently as possible. */
    constexpr void swap(Expected& other)
        noexcept(noexcept(m_storage.swap(other.m_storage)))
        requires(requires{m_storage.swap(other.m_storage);})
    {
        if (this != &other) {
            m_storage.swap(other.m_storage);
        }
    }

    /* True if the `Expected` stores a valid result.  `False` if it is in an error
    state. */
    [[nodiscard]] constexpr bool has_result() const noexcept {
        return m_storage.index() == 0;
    }

    /* Access the valid state.  Throws a `BadUnionAccess` assertion if the expected is
    currently in the error state and the program is compiled in debug mode.  Fails to
    compile if the result type is void. */
    template <typename Self> requires (meta::not_void<value_type>)
    [[nodiscard]] constexpr access<Self> result(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_error()) {
                throw BadUnionAccess("Expected in error state has no result");
            }
        }
        return std::forward<Self>(self).get_result();
    }

    /* Access the stored value or return the default value if the expected is in an
    error state, converting to the common type between the wrapped type and the default
    value. */
    template <typename Self, typename V>
        requires (meta::not_void<value_type> && meta::has_common_type<access<Self>, V>)
    [[nodiscard]] constexpr meta::common_type<access<Self>, V> result_or(
        this Self&& self,
        V&& fallback
    ) noexcept(
        meta::nothrow::convertible_to<access<Self>, meta::common_type<access<Self>, V>> &&
        meta::nothrow::convertible_to<V, meta::common_type<access<Self>, V>>
    ) {
        if (self.has_result()) {
            return std::forward<Self>(self).get_result();
        } else {
            return std::forward<V>(fallback);
        }
    }

    /* True if the `Expected` is in an error state.  False if it stores a valid
    result. */
    [[nodiscard]] constexpr bool has_error() const noexcept {
        return m_storage.index() == 1;
    }

    /* True if the `Expected` is in a specific error state identified by index.  False
    if it stores a valid result or an error other than the one indicated.  If only one
    error state is permitted, then this is identical to calling `has_error()` without
    any template parameters. */
    template <size_t I> requires (I < errors::size())
    [[nodiscard]] constexpr bool has_error() const noexcept {
        if constexpr (sizeof...(Es)) {
            return has_error() && get_error().index() == I;
        } else {
            return has_error();
        }
    }

    /* True if the `Expected` is in a specific error state indicated by type.  False
    if it stores a valid result or an error other than the one indicated.  If only one
    error state is permitted, then this is identical to calling `has_error()` without
    any template parameters. */
    template <typename Err> requires (errors::template contains<Err>())
    [[nodiscard]] constexpr bool has_error() const noexcept {
        if constexpr (sizeof...(Es)) {
            return has_error() && get_error().index() == errors::template index<Err>();
        } else {
            return has_error();
        }
    }

    /* Access the error state.  Throws a `BadUnionAccess` exception if the expected is
    currently in the valid state and the program is compiled in debug mode.  The result
    is either a single error or `bertrand::Union<>` of errors if there are more than
    one. */
    template <typename Self>
    [[nodiscard]] constexpr access_error<Self> error(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_result()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
        }
        return std::forward<Self>(self).get_error();
    }

    /* Access a particular error by index.  This is equivalent to the non-templated
    `error()` accessor in the single error case, and is a shorthand for
    `error().get<I>().result()` in the union case.  A `BadUnionAccess` exception will
    be thrown in debug mode if the expected is currently in the valid state, or if the
    indexed error is not the active member of the union. */
    template <size_t I, typename Self> requires (I < errors::size())
    [[nodiscard]] constexpr access_at<I, Self> error(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_result()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
            if constexpr (sizeof...(Es)) {
                if (self.get_error().index() != I) {
                    throw self.get_error().template get_index_error<I>[
                        self.get_error().index()
                    ]();
                }
            }
        }
        return (std::forward<Self>(self).template get_error<I>());
    }

    /* Access a particular error by type.  This is equivalent to the non-templated
    `error()` accessor in the single error case, and is a shorthand for
    `error().get<T>().result()` in the union case.  A `BadUnionAccess` exception will
    be thrown in debug mode if the expected is currently in the valid state, or if the
    specified error is not the active member of the union. */
    template <typename Err, typename Self> requires (errors::template contains<Err>())
    [[nodiscard]] constexpr access_type<Err, Self> error(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_result()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
            if constexpr (sizeof...(Es)) {
                if (self.get_error().index() != errors::template index<Err>()) {
                    throw self.get_error().template get_type_error<Err>[
                        self.get_error().index()
                    ]();
                }
            }
        }
        return (std::forward<Self>(self).template get_error<Err>());
    }

    /* Access the error state or return the default value if the expected stores a
    valid result, converting to the common type between the error type and the default
    value. */
    template <typename Self, typename V>
        requires (meta::has_common_type<access_error<Self>, V>)
    [[nodiscard]] constexpr meta::common_type<access_error<Self>, V> error_or(
        this Self&& self,
        V&& fallback
    ) noexcept(
        meta::nothrow::convertible_to<
            access_error<Self>,
            meta::common_type<access_error<Self>, V>
        > &&
        meta::nothrow::convertible_to<
            V,
            meta::common_type<access_error<Self>, V>
        >
    ) {
        if (self.has_error()) {
            return std::forward<Self>(self).get_error();
        } else {
            return std::forward<V>(fallback);
        }
    }

    /* Access a particular error by index or return the default value if the expected
    stores a valid result, converting to the common type between the error and the
    default value.  This is equivalent to the non-templated `error_or()` accessor in
    the single error case, and is a shorthand for `error().get<I>().result_or(fallback)`
    in the union case.  A `BadUnionAccess` exception will be thrown in debug mode if
    the indexed error is not the active member of the union. */
    template <size_t I, typename Self, typename V>
        requires (I < errors::size() && meta::has_common_type<access_at<I, Self>, V>)
    [[nodiscard]] constexpr meta::common_type<access_at<I, Self>, V> error_or(
        this Self&& self,
        V&& fallback
    ) noexcept(
        meta::nothrow::convertible_to<
            access_at<I, Self>,
            meta::common_type<access_at<I, Self>, V>
        > &&
        meta::nothrow::convertible_to<
            V,
            meta::common_type<access_at<I, Self>, V>
        >
    ) {
        if (self.has_error()) {
            if constexpr (DEBUG && sizeof...(Es)) {
                if (self.get_error().index() != I) {
                    throw self.get_error().template get_index_error<I>[
                        self.get_error().index()
                    ]();
                }
            }
            return std::forward<Self>(self).template get_error<I>();
        } else {
            return std::forward<V>(fallback);
        }
    }

    /* Access a particular error by index or return the default value if the expected
    stores a valid result, converting to the common type between the error and the
    default value.  This is equivalent to the non-templated `error_or()` accessor in
    the single error case, and is a shorthand for `error().get<I>().result_or(fallback)`
    in the union case.  A `BadUnionAccess` exception will be thrown in debug mode if
    the indexed error is not the active member of the union. */
    template <typename Err, typename Self, typename V>
        requires (
            errors::template contains<Err> &&
            meta::has_common_type<access_type<Err, Self>, V>
        )
    [[nodiscard]] constexpr auto error_or(this Self&& self, V&& fallback) noexcept(
        meta::nothrow::convertible_to<
            access_type<Err, Self>,
            meta::common_type<access_type<Err, Self>, V>
        > &&
        meta::nothrow::convertible_to<
            V,
            meta::common_type<access_type<Err, Self>, V>
        >
    ) -> meta::common_type<access_type<Err, Self>, V>
    {
        if (self.has_error()) {
            if constexpr (DEBUG && sizeof...(Es)) {
                if (self.get_error().index() != errors::template index<Err>()) {
                    throw self.get_error().template get_type_error<Err>[
                        self.get_error().index()
                    ]();
                }
            }
            return std::forward<Self>(self).m_storage.template get_error<Err>();
        } else {
            return std::forward<V>(fallback);
        }
    }

    /* A member equivalent for `bertrand::visit()`, which always inserts this expected
    as the first argument, for chaining purposes.  See `bertrand::visit()` for more
    details.

    For expecteds, the visitor function can consider any combination of the result type
    and/or enumerated error types.  If the result type is `void`, then the visitor will
    be invoked with an argument of type `std::nullopt_t` instead.  Otherwise, it will be
    invoked with the normal output from `.result()`. */
    template <typename F, typename Self, typename... Args>
        requires (meta::visitor<F, Self, Args...>)
    constexpr decltype(auto) visit(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visitor<F, Self, Args...>
    ) {
        return (bertrand::visit(
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* If the optional stores a valid result, invoke a given function on the wrapped
    value, otherwise propagate the error state(s).  This is identical to `visit()`,
    except that the visitor is forced to cover the result state, and will not be
    invoked in the error case.  All other rules (including flattening or handling of
    void return types) remain the same.

    For most visitors:

                self                    invoke                      result
        -------------------------------------------------------------------------------
        1.  Expected<T, Es...>(err)     (no call)               Expected<U, Es...>(err)
        2.  Expected<T, Es...>(x)       f(x, args...) -> err    Expected<U, Es...>(err)
        3.  Expected<T, Es...>(x)       f(x, args...) -> y      Expected<U, Es...>(y)
        4.  Expected<void, Es...>()     f(args...) -> err       Expected<U, Es...>(err)
        5.  Expected<void, Es...>()     f(args...) -> y         Expected<U, Es...>(y)

    If the visitor returns void:

                self                    invoke                      result
        -------------------------------------------------------------------------------
        1.  Expected<T, Es...>(err)     (no call)               Expected<void, Es...>(err)
        2.  Expected<T, Es...>(x)       f(x, args...) -> void   Expected<void, Es...>()
        4.  Expected<void, Es...>()     f(args...) -> void      Expected<void, Es...>()

    Some functional languages call this operation "flatMap" or "bind".

    NOTE: the extra flattening and void handling by this method differs from many
    functional languages, as well as `std::expected::and_then()` and
    `std::expected::transform()`.  For those methods, the former requires the visitor
    to always return another `std::expected`, whereas the latter implicitly promotes
    the return type to `std::expected`, even if it was one already (possibly resulting
    in a nested expected).  Thanks to the use of `visit()` under the hood, Bertrand's
    `and_then()` method unifies both models by allowing the function to return either
    an expected (of any kind, possibly nested), which will be used directly (merging
    with the original error states), or a non-expected, which will be promoted to an
    expected.  This guards against accidental nested expecteds, which Bertrand
    considers to be an anti-pattern.  This is especially true if the nesting is
    dependent on the order of operations, which can lead to brittle code and subtle
    bugs.  If the error states must remain distinct, then it is always preferable to
    explicitly handle them before chaining. */
    template <typename Self, typename F, typename... Args>
        requires (
            meta::not_void<value_type> &&
            meta::invocable<F, access<Self>, Args...> &&
            meta::visitor<and_then_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) and_then(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visitor<and_then_fn<F, Self, Args...>, F, Self, Args...>
    ) {
        return (bertrand::visit(
            and_then_fn<F, Self, Args...>{},
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* If the optional stores a valid result, invoke a given function on the wrapped
    value, otherwise propagate the error state(s).  This is identical to `visit()`,
    except that the visitor is forced to cover the result state, and will not be
    invoked in the error case.  All other rules (including flattening or handling of
    void return types) remain the same.

    For most visitors:

                self                    invoke                      result
        -------------------------------------------------------------------------------
        1.  Expected<T, Es...>(err)     (no call)               Expected<U, Es...>(err)
        2.  Expected<T, Es...>(x)       f(x, args...) -> err    Expected<U, Es...>(err)
        3.  Expected<T, Es...>(x)       f(x, args...) -> y      Expected<U, Es...>(y)
        4.  Expected<void, Es...>()     f(args...) -> err       Expected<U, Es...>(err)
        5.  Expected<void, Es...>()     f(args...) -> y         Expected<U, Es...>(y)

    If the visitor returns void:

                self                    invoke                      result
        -------------------------------------------------------------------------------
        1.  Expected<T, Es...>(err)     (no call)               Expected<void, Es...>(err)
        2.  Expected<T, Es...>(x)       f(x, args...) -> void   Expected<void, Es...>()
        4.  Expected<void, Es...>()     f(args...) -> void      Expected<void, Es...>()

    Some functional languages call this operation "flatMap" or "bind".

    NOTE: the extra flattening and void handling by this method differs from many
    functional languages, as well as `std::expected::and_then()` and
    `std::expected::transform()`.  For those methods, the former requires the visitor
    to always return another `std::expected`, whereas the latter implicitly promotes
    the return type to `std::expected`, even if it was one already (possibly resulting
    in a nested expected).  Thanks to the use of `visit()` under the hood, Bertrand's
    `and_then()` method unifies both models by allowing the function to return either
    an expected (of any kind, possibly nested), which will be used directly (merging
    with the original error states), or a non-expected, which will be promoted to an
    expected.  This guards against accidental nested expecteds, which Bertrand
    considers to be an anti-pattern.  This is especially true if the nesting is
    dependent on the order of operations, which can lead to brittle code and subtle
    bugs.  If the error states must remain distinct, then it is always preferable to
    explicitly handle them before chaining. */
    template <typename Self, typename F, typename... Args>
        requires (
            meta::is_void<value_type> &&
            meta::invocable<F, Args...> &&
            meta::visitor<and_then_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) and_then(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visitor<and_then_fn<F, Self, Args...>, F, Self, Args...>
    ) {
        return (bertrand::visit(
            and_then_fn<F, Self, Args...>{},
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* If the expected is in an error state, invoke a given function, otherwise
    propagate the result state.  This is identical to `visit()`, except that the
    visitor will not be invoked if the expected holds a valid result.  All other rules
    (including promotion to union, handling of void return types, and implicit
    propagation of unhandled errors) remain the same.

    For most visitors:

                self                    invoke                      result
        -------------------------------------------------------------------------------
        1.  Expected<T, Es...>(e)       f(e, args...) -> u      Union<T, U>(u)
        2.  Expected<T, Es...>(t)       (no call)               Union<T, U>(t)
        3.  Expected<void, Es...>(e)    f(e, args...) -> u      Optional<U>(u)
        4.  Expected<void, Es...>()     (no call)               Optional<U>(empty)

    If the visitor returns void:

                self                    invoke                      result
        -------------------------------------------------------------------------------
        1.  Expected<T, Es...>(e)       f(e, args...) -> void   Optional<T>(empty)
        2.  Expected<T, Es...>(t)       (no call)               Optional<T>(t)
        3.  Expected<void, Es...>(e)    f(e, args...) -> void   void
        4.  Expected<void, Es...>()     (no call)               void

    If the visitor leaves some error states unhandled:

                self                    invoke                      result
        -------------------------------------------------------------------------------
        1.  Expected<T, Es...>(e)       f(e, args...) -> u      Expected<Union<T, U>, Rs...>(u)
        2.  Expected<T, Es...>(t)       (no call)               Expected<Union<T, U>, Rs...>(t)
        3.  Expected<void, Es...>(e)    f(e, args...) -> u      Expected<Optional<U>, Rs...>(u)
        4.  Expected<void, Es...>()     (no call)               Expected<Optional<U>, Rs...>(empty)

    And if it also returns void:

                self                    invoke                      result
        -------------------------------------------------------------------------------
        1.  Expected<T, Es...>(e)       f(e, args...) -> void   Expected<Optional<T>, Rs...>(empty)
        2.  Expected<T, Es...>(t)       (no call)               Expected<Optional<T>, Rs...>(t)
        3.  Expected<void, Es...>(e)    f(e, args...) -> void   Expected<void, Rs...>()
        4.  Expected<void, Es...>()     (no call)               Expected<void, Rs...>()

    ... where `Rs...` represent the remaining error states that were not handled by
    the visitor.

    NOTE: the extra union promotion and void handling by this method differs from many
    functional languages, as well as from `std::expected::or_else()`, which forces the
    function to always return another `std::expected` of the same type as the input.
    Thanks to the use of `visit()` under the hood, Bertrand's `or_else()` method is not
    as restrictive, and allows the function to possibly return a different type, in
    which case the result will be promoted to a union of the original expected type and
    the function's return type.  Functions that return void will map to optionals,
    where the void state is converted into an empty optional.  If the original expected
    was specialized to void, then the final return type will be either void (if the
    function returns void) or an optional of the function's return type, where
    propagating the result initializes the optional in the empty state.  This is done
    for consistency with `visit()` as well as `and_then()`, all of which use the same
    underlying infrastructure. */
    template <typename Self, typename F, typename... Args>
        requires (
            meta::visitor<F, access_error<Self>, Args...> &&
            meta::visitor<or_else_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) or_else(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visitor<or_else_fn<F, Self, Args...>, F, Self, Args...>
    ) {
        return (bertrand::visit(
            or_else_fn<F, Self, Args...>{},
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* Monadic call operator.  If the expected type is a function-like object and is
    not in the error state, then this will return the result of that function wrapped
    in another expected.  Otherwise, it will propagate the error state. */
    template <typename Self, typename... Args>
        requires (meta::visitor<impl::Call, Self, Args...>)
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args) noexcept(
        meta::nothrow::visitor<impl::Call, Self, Args...>
    ) {
        return (bertrand::visit(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* Monadic subscript operator.  If the expected type is a container that supports
    indexing with the given key, then this will return the result of the access wrapped
    in another optional.  Otherwise, it will propagate the error state. */
    template <typename Self, typename... Key>
        requires (meta::visitor<impl::Subscript, Self, Key...>)
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys) noexcept(
        meta::nothrow::visitor<impl::Subscript, Self, Key...>
    ) {
        return (bertrand::visit(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<Key>(keys)...
        ));
    }
};


/* ADL swap() operator for `bertrand::Union<Ts...>`.  Equivalent to calling `a.swap(b)`
as a member method. */
template <typename... Ts>
    requires (requires(Union<Ts...>& a, Union<Ts...>& b) { a.swap(b); })
constexpr void swap(Union<Ts...>& a, Union<Ts...>& b)
    noexcept(noexcept(a.swap(b)))
{
    a.swap(b);
}


/* ADL swap() operator for `bertrand::Optional<T>`.  Equivalent to calling `a.swap(b)`
as a member method. */
template <typename T>
    requires (requires(Optional<T>& a, Optional<T>& b) { a.swap(b); })
constexpr void swap(Optional<T>& a, Optional<T>& b)
    noexcept(noexcept(a.swap(b)))
{
    a.swap(b);
}


/* ADL swap() operator for `bertrand::Expected<T, E>`.  Equivalent to calling
`a.swap(b)` as a member method. */
template <typename T, typename E>
    requires (requires(Expected<T, E>& a, Expected<T, E>& b) { a.swap(b); })
constexpr void swap(Expected<T, E>& a, Expected<T, E>& b)
    noexcept(noexcept(a.swap(b)))
{
    a.swap(b);
}


/* Monadic logical NOT operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports logical NOT. */
template <meta::monad T> requires (meta::visitor<impl::LogicalNot, T>)
constexpr decltype(auto) operator!(T&& val) noexcept(
    meta::nothrow::visitor<impl::LogicalNot, T>
) {
    return (visit(impl::LogicalNot{}, std::forward<T>(val)));
}


/* Monadic logical AND operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports logical AND against the other
operand (which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::LogicalAnd, L, R>
    )
constexpr decltype(auto) operator&&(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::LogicalAnd, L, R>
) {
    return (visit(
        impl::LogicalAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic logical OR operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports logical OR against the other
operand (which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::LogicalOr, L, R>
    )
constexpr decltype(auto) operator||(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::LogicalOr, L, R>
) {
    return (visit(
        impl::LogicalOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic less-than comparison operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports less-than comparisons against the
other operand (which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Less, L, R>
    )
constexpr decltype(auto) operator<(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Less, L, R>
) {
    return (visit(
        impl::Less{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic less-than-or-equal comparison operator.  Delegates to `bertrand::visit()`,
and is automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports less-than-or-equal comparisons
against the other operand (which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::LessEqual, L, R>
    )
constexpr decltype(auto) operator<=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::LessEqual, L, R>
) {
    return (visit(
        impl::LessEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic equality operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports equality comparisons against the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Equal, L, R>
    )
constexpr decltype(auto) operator==(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Equal, L, R>
) {
    return (visit(
        impl::Equal{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic inequality operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports equality comparisons against the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::NotEqual, L, R>
    )
constexpr decltype(auto) operator!=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::NotEqual, L, R>
) {
    return (visit(
        impl::NotEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic greater-than-or-equal comparison operator.  Delegates to `bertrand::visit()`,
and is automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports greater-than-or-equal comparisons
against the other operand (which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::GreaterEqual, L, R>
    )
constexpr decltype(auto) operator>=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::GreaterEqual, L, R>
) {
    return (visit(
        impl::GreaterEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic greater-than comparison operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports greater-than-or-equal comparisons
against the other operand (which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Greater, L, R>
    )
constexpr decltype(auto) operator>(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Greater, L, R>
) {
    return (visit(
        impl::Greater{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic three-way comparison operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports three-way comparisons against the
other operand (which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Spaceship, L, R>
    )
constexpr decltype(auto) operator<=>(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Spaceship, L, R>
) {
    return (visit(
        impl::Spaceship{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic unary plus operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports unary plus. */
template <meta::monad T> requires (meta::visitor<impl::Pos, T>)
constexpr decltype(auto) operator+(T&& val) noexcept(
    meta::nothrow::visitor<impl::Pos, T>
) {
    return (visit(impl::Pos{}, std::forward<T>(val)));
}


/* Monadic unary minus operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports unary minus. */
template <meta::monad T> requires (meta::visitor<impl::Neg, T>)
constexpr decltype(auto) operator-(T&& val) noexcept(
    meta::nothrow::visitor<impl::Neg, T>
) {
    return (visit(impl::Neg{}, std::forward<T>(val)));
}


/* Monadic prefix increment operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one tyoe within the monad supports prefix increments. */
template <meta::monad T> requires (meta::visitor<impl::PreIncrement, T>)
constexpr decltype(auto) operator++(T&& val) noexcept(
    meta::nothrow::visitor<impl::PreIncrement, T>
) {
    return (visit(impl::PreIncrement{}, std::forward<T>(val)));
}


/* Monadic postfix increment operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports postfix increments. */
template <meta::monad T> requires (meta::visitor<impl::PostIncrement, T>)
constexpr decltype(auto) operator++(T&& val, int) noexcept(
    meta::nothrow::visitor<impl::PostIncrement, T>
) {
    return (visit(impl::PostIncrement{}, std::forward<T>(val)));
}


/* Monadic prefix decrement operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports prefix decrements. */
template <meta::monad T> requires (meta::visitor<impl::PreDecrement, T>)
constexpr decltype(auto) operator--(T&& val) noexcept(
    meta::nothrow::visitor<impl::PreDecrement, T>
) {
    return (visit(impl::PreDecrement{}, std::forward<T>(val)));
}

/* Monadic postfix decrement operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports postfix decrements. */
template <meta::monad T> requires (meta::visitor<impl::PostDecrement, T>)
constexpr decltype(auto) operator--(T&& val, int) noexcept(
    meta::nothrow::visitor<impl::PostDecrement, T>
) {
    return (visit(impl::PostDecrement{}, std::forward<T>(val)));
}


/* Monadic addition operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports addition with the other operand (which may be
another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Add, L, R>
    )
constexpr decltype(auto) operator+(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Add, L, R>
) {
    return (visit(
        impl::Add{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place addition operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports addition with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceAdd, L, R>
    )
constexpr decltype(auto) operator+=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceAdd, L, R>
) {
    return (visit(
        impl::InplaceAdd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic subtraction operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports subtraction with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Subtract, L, R>
    )
constexpr decltype(auto) operator-(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Subtract, L, R>
) {
    return (visit(
        impl::Subtract{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place subtraction operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports subtraction with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceSubtract, L, R>
    )
constexpr decltype(auto) operator-=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceSubtract, L, R>
) {
    return (visit(
        impl::InplaceSubtract{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic multiplication operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports multiplication with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Multiply, L, R>
    )
constexpr decltype(auto) operator*(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Multiply, L, R>
) {
    return (visit(
        impl::Multiply{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place multiplication operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports multiplication with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceMultiply, L, R>
    )
constexpr decltype(auto) operator*=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceMultiply, L, R>
) {
    return (visit(
        impl::InplaceMultiply{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic division operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports division with the other operand (which may be
another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Divide, L, R>
    )
constexpr decltype(auto) operator/(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Divide, L, R>
) {
    return (visit(
        impl::Divide{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place division operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports division with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceDivide, L, R>
    )
constexpr decltype(auto) operator/=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceDivide, L, R>
) {
    return (visit(
        impl::InplaceDivide{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic modulus operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports modulus with the other operand (which may be
another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::Modulus, L, R>
    )
constexpr decltype(auto) operator%(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::Modulus, L, R>
) {
    return (visit(
        impl::Modulus{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place modulus operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports modulus with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceModulus, L, R>
    )
constexpr decltype(auto) operator%=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceModulus, L, R>
) {
    return (visit(
        impl::InplaceModulus{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic left shift operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports left shifts with the other operand (which may be
another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::LeftShift, L, R>
    )
constexpr decltype(auto) operator<<(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::LeftShift, L, R>
) {
    return (visit(
        impl::LeftShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place left shift operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports left shifts with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceLeftShift, L, R>
    )
constexpr decltype(auto) operator<<=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceLeftShift, L, R>
) {
    return (visit(
        impl::InplaceLeftShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic right shift operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports right shifts with the other operand (which may be
another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::RightShift, L, R>
    )
constexpr decltype(auto) operator>>(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::RightShift, L, R>
) {
    return (visit(
        impl::RightShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place right shift operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports right shifts with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceRightShift, L, R>
    )
constexpr decltype(auto) operator>>=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceRightShift, L, R>
) {
    return (visit(
        impl::InplaceRightShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic bitwise NOT operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise NOT. */
template <meta::monad T> requires (meta::visitor<impl::BitwiseNot, T>)
constexpr decltype(auto) operator~(T&& val) noexcept(
    meta::nothrow::visitor<impl::BitwiseNot, T>
) {
    return (visit(impl::BitwiseNot{}, std::forward<T>(val)));
}


/* Monadic bitwise AND operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise AND with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::BitwiseAnd, L, R>
    )
constexpr decltype(auto) operator&(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::BitwiseAnd, L, R>
) {
    return (visit(
        impl::BitwiseAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place bitwise AND operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise AND with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceBitwiseAnd, L, R>
    )
constexpr decltype(auto) operator&=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceBitwiseAnd, L, R>
) {
    return (visit(
        impl::InplaceBitwiseAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic bitwise OR operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports bitwise OR with the other operand (which may be
another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::BitwiseOr, L, R>
    )
constexpr decltype(auto) operator|(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::BitwiseOr, L, R>
) {
    return (visit(
        impl::BitwiseOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place bitwise OR operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise OR with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceBitwiseOr, L, R>
    )
constexpr decltype(auto) operator|=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceBitwiseOr, L, R>
) {
    return (visit(
        impl::InplaceBitwiseOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic bitwise XOR operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports bitwise XOR with the other operand (which may be
another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::BitwiseXor, L, R>
    )
constexpr decltype(auto) operator^(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::BitwiseXor, L, R>
) {
    return (visit(
        impl::BitwiseXor{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic in-place bitwise XOR operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise XOR with the other operand
(which may be another monad). */
template <typename L, typename R>
    requires (
        (meta::monad<L> || meta::monad<R>) &&
        meta::visitor<impl::InplaceBitwiseXor, L, R>
    )
constexpr decltype(auto) operator^=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visitor<impl::InplaceBitwiseXor, L, R>
) {
    return (visit(
        impl::InplaceBitwiseXor{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


}


namespace std {

    template <typename... Ts>
    struct variant_size<bertrand::Union<Ts...>> :
        std::integral_constant<size_t, bertrand::Union<Ts...>::alternatives>
    {};

    template <size_t I, typename... Ts>
        requires (I < variant_size<bertrand::Union<Ts...>>::value)
    struct variant_alternative<I, bertrand::Union<Ts...>> {
        using type = bertrand::Union<Ts...>::template alternative<I>;
    };

    /// TODO: maybe `std::get()` should mirror the semantics of `std::variant` (i.e.
    /// no expected return type)?

    template <size_t I, bertrand::meta::Union U> requires (I < variant_size<U>::value)
    constexpr decltype(auto) get(U&& u)
        noexcept(noexcept(std::forward<U>(u).template get<I>()))
    {
        return std::forward<U>(u).template get<I>();
    }

    template <typename T, bertrand::meta::Union U>
        requires (remove_cvref_t<U>::template valid_alternative<T>)
    constexpr decltype(auto) get(U&& u)
        noexcept(noexcept(std::forward<U>(u).template get<T>()))
    {
        return std::forward<U>(u).template get<T>();
    }

    /// TODO: `std::get_if()`?

    template <bertrand::meta::monad T>
        requires (bertrand::meta::visitor<bertrand::impl::Hash, T>)
    struct hash<T> {
        static constexpr auto operator()(auto&& value) noexcept(
            bertrand::meta::nothrow::visitor<bertrand::impl::Hash, T>
        ) {
            return bertrand::visit(
                bertrand::impl::Hash{},
                std::forward<decltype(value)>(value)
            );
        }
    };

}


namespace bertrand {

    inline void test() {
        {
            static constexpr int i = 42;
            static constexpr Union<int, const int&, std::string> u = "abc";
            static constexpr Union<int, const int&, std::string> u2 = u;
            std::variant<int, std::string> variant = u;
            constexpr Union u3 = std::move(Union{u});
            constexpr decltype(auto) x = u.get<2>();
            static_assert(u.index() == 2);
            static_assert(u.get<u.index()>().result() == "abc");
            static_assert(u2.get<2>().result() == "abc");
            static_assert(u3.get<2>().result() == "abc");
            static_assert(u.get<std::string>().result() == "abc");

            Union<int, const int&, std::string> u4 = "abc";
            Union<int, const int&, std::string> u5 = u4;
            u4 = u;
            u4 = std::move(u5);

            constexpr auto u6 = Union<int, const int&, std::string>::create<std::string>("abc");
            static_assert(u6.index() == 2);
        }

        {
            static constexpr int value = 2;
            static constexpr Union<int, const int&> val = value;
            static constexpr Union<int, const int&> val2 = val;
            static constexpr double d = val;
            // static_assert(val.empty());
            static_assert(val <= 2);
            static_assert((val + 1) / val2 == 1);
            static constexpr auto f = [](int x, int y) noexcept {
                return x + y;
            };
            static_assert(val.get<val.index()>().result() == 2);
            static_assert(visit(f, val, 1) == 3);
            static_assert(val.visit(f, 1) == 3);
            static_assert(noexcept(visit(f, val, 1) == 3));
            static_assert(noexcept(val.visit(f, 1) == 3));
            static_assert(val.flatten() == 2);
            static_assert(noexcept(val.flatten() == 2));
            static_assert(double(val) == 2.0);
            static_assert(val.index() == 1);
            static_assert(val.holds_alternative<const int&>());
            static_assert(val.get_if<val.index()>().has_value());
            static_assert(val.get_if<val.index()>().value() == 2);
            decltype(auto) v = val.get<val.index()>();
            decltype(auto) v2 = val.get_if<0>();
            decltype(auto) v3 = val.visit(f, 1);
        }

        {
            static constexpr Union<int, double> u = 3.14;
            static constexpr auto f = []<typename X, typename Y>(X&& x, Y&& y) {
                return std::forward<X>(x) + std::forward<Y>(y);
            };
            static_assert(u.holds_alternative<double>());
            static_assert(visit(f, u, 1).holds_alternative<double>());
            decltype(auto) value = visit(f, u, 1);
        }

        {
            static constexpr std::string_view str = "abc";
            constexpr Optional<const std::string_view&> o = str;
            constexpr const std::string_view* p = o;
            static_assert(p->size() == 3);
            constexpr auto o2 = o.and_then([](std::string_view s) {
                return s.size();
            });
            static_assert(o2.value() == 3);
            static_assert(o.value_or("def") == "abc");
            static_assert(o.value().size() == 3);
            static_assert(o[2].value() == 'c');
            decltype(auto) ov = o.value();
            decltype(auto) ov2 = Optional{"abc"}.value();

            static constexpr Optional<int> o3 = 4;
            static_assert((-o3).value() == -4);
            static_assert(o3.has_value());
            static_assert(o3.and_then(
                [](int x) { return x + 1; }
            ).value() == 5);
            static_assert(o3.value() == 4);
            static constexpr auto o4 = o3.filter(
                [](int x) { return x < 4; }
            );
            static_assert(!o4.has_value());
            static_assert(o4.and_then(
                [](int x) { return x + 1; }
            ).has_value() == false);
            static_assert(o4.or_else([] { return 42; }) == 42);
            static_assert(o4.visit(visitor{
                [](int x) { return x; },
                [](std::nullopt_t) { return 0; }
            }) == 0);

            struct Func {
                static constexpr int operator()(int x) noexcept {
                    return x * 2;
                }
            };
            static constexpr Optional<Func> o5 = Func{};
            static_assert(o5(21).has_value());
            static_assert(o5(21).value() == 42);
        }

        {
            struct A { static constexpr int operator()() { return 42; } };
            struct B { static constexpr std::string operator()() { return "hello, world"; } };
            static constexpr Union<A, B> u = B{};
            static constexpr decltype(auto) x = u().get<std::string>();
            static_assert(u().get<std::string>().result() == "hello, world");

            static constexpr Union<std::array<int, 3>, std::string> u2 =
                std::array{1, 2, 3};
            static_assert(u2[1] == 2);

            static constexpr Optional<std::array<int, 3>> o = std::array{1, 2, 3};
            static_assert(o[1].value() == 2);
            static_assert(o.value().size() == 3);
        }

        {
            static constexpr Union<int, double> u = 3.14;
            static_assert(std::get<double>(u).result() == 3.14);

            static constexpr Union<int, std::string_view> u2 = 2;
            static constexpr Union<int, std::string_view> u3 = "abc";
            static_assert(visit(visitor{
                [](int x, int y) { return 1; },
                [](int x, std::string_view y) { return 2; },
                [](std::string_view x, int y) { return 3; },
                [](std::string_view x, std::string_view y) { return 4; }
            }, u2, u3) == 2);
        }

        {
            static_assert(sizeof(TypeError) == 24);
            static_assert(sizeof(Union<int, float>) == 8);
            static_assert(sizeof(std::variant<int, float>) == 8);
            static_assert(sizeof(Optional<int>) == 8);
            static_assert(sizeof(std::optional<int>) == 8);
            static_assert(sizeof(Expected<int, TypeError>) == 32);
            static_assert(sizeof(std::expected<int, TypeError>) == 24);

            static constexpr TypeError err("test");
            TypeError err2("test2");
            static_assert(err.message() == "test");

            static constexpr Expected<int, TypeError> e = 42;
            static_assert(e.result() == 42);
            static constexpr Expected<int, TypeError, ValueError> e2 = TypeError("test");
            static_assert(e2.result_or(0) == 0);
            static_assert(e2.has_error());
            static_assert(e2.has_error<TypeError>());
            static_assert(e2.error().get<0>().result().message() == "test");
            static_assert(e2.error<TypeError>().message() == "test");

            static constexpr auto r = e2.visit(visitor{
                [](int x) -> int { return x + 1; },
                [](TypeError x) { return 0; },
                [](ValueError x) { return 0; }
            });
            static_assert(r == 0);

            static constexpr Optional<int> o = 42;
            auto result = o.visit(visitor{
                [](int x) -> std::optional<int> { return x; },
                [](std::nullopt_t) -> int { return 0; }
            });

            static constexpr std::optional<int> o2 = 42;
            auto result2 = visit(visitor{
                [](int x, int y) -> std::optional<int> { return x; },
                [](std::nullopt_t, int y) -> int { return 0; }
            }, o2, 2);
        }

        {
            static constexpr TypeError err("An error occurred");
            static constexpr std::array<TypeError, 3> errs{err, err, err};
            static constexpr Expected<int, TypeError, ValueError> e1 = ValueError{"abc"};
            static_assert(e1.has_error());
            static_assert(e1.error().get<1>().result().message() == "abc");
            static constexpr Expected<int, TypeError, ValueError> e2 = 42;
            static_assert(!e2.has_error());
            static_assert(e2.result() == 42);
            static constexpr Union<TypeError, ValueError> u = TypeError{"abc"};

            static constexpr Expected<std::string_view, TypeError> e3 = "abc";
            static_assert(e3.result().size() == 3);
        }

        {
            static constexpr Expected<int, TypeError, ValueError> e1 = 42;
            static_assert(e1.and_then(
                [](int x) { return x + 1; }
            ).result() == 43);


            static constexpr Expected<void> e2 = Exception("abc");
            static_assert(e2.has_error());
            static_assert(e2.visit(visitor{
                [](std::nullopt_t) { return 1; },
                [](Exception) { return 2; }
            }) == 2);
            static_assert(
                e2.and_then([]() { return 1; }).has_error()
            );
            static_assert(
                e2.or_else([](Exception) { return 2; }).value() == 2
            );
        }

        {
            static constexpr auto f = [](int x) { return; };
            static constexpr Union<int, std::string_view> u = 42;
            static_assert(meta::visitor<decltype(f), decltype(u)>);
            auto x = u.visit(visitor{
                [](int) { return 1; },
                [](std::string_view) { return; }
            });
            // auto y = u.visit([](int) { return; });

            static constexpr Optional<int> o = 42;
            static_assert(meta::visitor<decltype(f), decltype(o)>);
            o.and_then(f);

            static constexpr Expected<int, TypeError> e = 42;
            auto y2 = e.and_then(f);
            auto y3 = e.visit(f);

            static constexpr Expected<void, TypeError, ValueError> e2;
            static_assert(e2.or_else([](TypeError) { return 2; }).has_result());
        }
    }

}


#endif  // BERTRAND_UNION_H
