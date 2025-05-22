#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


/* An alias for `std::nullopt_t`, which can be used similar to Python's
`types.NoneType`. */
using NoneType = std::nullopt_t;


/* An alias for `std::nullopt`, which can be used similar to Python's `None`. */
static constexpr NoneType None = std::nullopt;


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

    template <typename T>
    concept None = meta::is<T, NoneType>;

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
    requires (sizeof...(Ts) > 1 && (meta::not_void<Ts> && ...) && meta::unique<Ts...>)
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
        a `bertrand::NoneType` (aka `std::nullopt_t`) input, which can be handled
        explicitly.

`Optional` references can be used as drop-in replacements for raw pointers in most
cases, especially when integrating with Python or other languages where pointers are
not first-class citizens.  The `Union` interface does this automatically for
`get_if()`, avoiding confusion with pointer arithmetic or explicit dereferencing, and
forcing the user to handle the empty state explicitly.  Bertrand's binding generators
will make the same transformation from pointers to `Optional` references automatically
when exporting C++ code to Python. */
template <meta::not_void T> requires (!meta::None<T>)
struct Optional;


template <typename T>
Optional(T) -> Optional<T>;


template <typename T> requires (meta::not_void<typename impl::visitable<T>::empty>)
Optional(T) -> Optional<meta::remove_rvalue<typename impl::visitable<T>::wrapped>>;


template <meta::pointer T>
Optional(T) -> Optional<meta::as_lvalue<meta::remove_pointer<T>>>;


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
                using type = meta::product<
                    typename impl::visitable<First>::alternatives,
                    typename impl::visitable<Rest>::alternatives...
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
                    struct helper { using type = meta::pack<Rs...>; };
                    template <typename... Rs>
                        requires (!::std::same_as<meta::invoke_type<F, As...>, Rs> && ...)
                    struct helper<Rs...> {
                        using type = meta::pack<Rs..., meta::invoke_type<F, As...>>;
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
                    using type = meta::pack<Rs...>;
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
                    static constexpr bool consistent = returns::size <= 1;
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
                    meta::pack<Rs...>,
                    meta::pack<valid...>,
                    P,
                    Ps...
                > {
                    using result = validate<
                        nothrow && P::template eval<invoke>::nothrow,
                        has_void || P::template eval<invoke>::has_void,
                        typename P::template eval<invoke>::template type<Rs...>,
                        meta::pack<valid..., P>,
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
                    meta::pack<Rs...>,
                    meta::pack<valid...>,
                    P,
                    Ps...
                > {
                    using result = validate<
                        nothrow,
                        has_void,
                        meta::pack<Rs...>,
                        meta::pack<valid...>,
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
                    meta::pack<>,  // initially no valid return types
                    meta::pack<>,  // initially no valid permutations
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
                struct infer;

                // 3a. Iterate over all arguments to discover the precise alternatives
                //     that are left unhandled by the visitor, and apply custom
                //     forwarding behavior on that basis.  If any states cannot be
                //     trivially forwarded, then `enable` will evaluate to false.
                template <bool option, typename... errors, typename A, typename... As>
                struct infer<option, meta::pack<errors...>, A, As...> {
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
                            impl::visitable<A>::errors::template contains<meta::unqualify<alt>>;
                    };

                    // 3c. For every alternative of `A`, check to see if it is present in
                    //     the valid permutations or can be implicitly propagated.  If not,
                    //     insert a `BadUnionAccess` state into the `errors...` pack.
                    template <typename...>
                    struct scan {
                        template <bool opt, typename... errs>
                        using type = infer<
                            opt,  // accumulated empty state
                            meta::pack<errs...>,  // accumulated errors
                            As...
                        >::type;
                        static constexpr bool enable = infer<
                            false,  // irrelevant for `enable` status
                            meta::pack<>,  // irrelevant for `enable` status
                            As...
                        >::enable;
                    };

                    // 3d. If the alternative is explicitly handled by the visitor, then we
                    //     can proceed to the next alternative.
                    template <typename alt, typename... alts>
                    struct scan<alt, alts...> {
                        template <bool opt, typename... errs>
                        using type = scan<alts...>::template type<opt, errs...>;
                        static constexpr bool enable = scan<alts...>::enable;
                    };

                    // 3e. Otherwise, if an unhandled alternative represents the empty
                    //     state of an optional, then we can set `opt` to true and proceed
                    //     to the next alternative.
                    template <typename alt, typename... alts>
                        requires (state<alt>::empty && !state<alt>::handled)
                    struct scan<alt, alts...> {
                        template <bool opt, typename... errs>
                        using type = scan<alts...>::template type<true, errs...>;
                        static constexpr bool enable = scan<alts...>::enable;
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
                        static constexpr bool enable = scan<alts...>::enable;
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
                        using type = void;  // terminate recursion
                        static constexpr bool enable = false;  // terminate recursion
                    };

                    // 3h. Execute the `scan<>` metafunction.
                    using result = impl::visitable<A>::alternatives::template eval<scan>;
                    using type = result::template type<option, errors...>;
                    static constexpr bool enable = result::enable;
                };

                // 3i. Once all alternatives have been scanned, deduce the final return
                //     type using the accumulated `option` and `errors...` states.
                template <bool option, typename... errors>
                struct infer<option, meta::pack<errors...>> {
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
                        using type = result::template eval<meta::to_unique>::template eval<
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
                    static constexpr bool enable = true;
                };

                // 3p. evaluate the infer<> metafunction'
                using result = infer<
                    permutations::template eval<filter>::has_void,
                    meta::pack<>,
                    Args...
                >;
                using type = result::type;
                static constexpr bool enable = result::enable;
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
                    meta::convertible_to<NoneType, T>
                );
                template <typename T>
                static constexpr bool nothrow = (
                    meta::nothrow::convertible_to<Rs, T> &&
                    ... &&
                    meta::nothrow::convertible_to<NoneType, T>
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

            /* `meta::visit<F, Args...>` evaluates to true iff all non-empty and
            non-error states are handled by the visitor function `F`. */
            static constexpr bool enable =
                valid_permutations::template eval<deduce>::enable;

            /* `meta::exhaustive<F, Args...>` evaluates to true iff all argument
            permutations are valid. */
            static constexpr bool exhaustive =
                permutations::size == valid_permutations::size;

            /* `meta::consistent<F, Args...>` evaluates to true iff all valid argument
            permutations return the same type, or if there are no valid paths. */
            static constexpr bool consistent =
                permutations::template eval<filter>::consistent;

            /* `meta::nothrow::visit<F, Args...>` evaluates to true iff all valid
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
    concept visit = detail::visit<F, Args...>::enable;

    /* Specifies that all permutations of the union types must be valid for the visitor
    function. */
    template <typename F, typename... Args>
    concept exhaustive = visit<F, Args...> && detail::visit<F, Args...>::exhaustive;

    /* Specifies that all valid permutations of the union types have an identical
    return type from the visitor function. */
    template <typename F, typename... Args>
    concept consistent = visit<F, Args...> && detail::visit<F, Args...>::consistent;

    /* A visitor function returns a new union of all possible results for each
    permutation of the input unions.  If all permutations return the same type, then
    that type is returned instead.  If some of the permutations return `void` and
    others do not, then the result will be wrapped in an `Optional`. */
    template <typename F, typename... Args> requires (visit<F, Args...>)
    using visit_type = detail::visit<F, Args...>::type;

    /* Tests whether the return type of a visitor is implicitly convertible to the
    expected type for every possible permutation of the arguments, with unions unpacked
    to their individual alternatives. */
    template <typename Ret, typename F, typename... Args>
    concept visit_returns =
        visit<F, Args...> && detail::visit<F, Args...>::template returns<Ret>;

    namespace nothrow {

        template <typename F, typename... Args>
        concept visit =
            meta::visit<F, Args...> && detail::visit<F, Args...>::nothrow;

        template <typename F, typename... Args>
        concept exhaustive =
            visit<F, Args...> && meta::exhaustive<F, Args...>;

        template <typename F, typename... Args>
        concept consistent =
            visit<F, Args...> && meta::consistent<F, Args...>;

        template <typename F, typename... Args> requires (visit<F, Args...>)
        using visit_type = meta::visit_type<F, Args...>;

        template <typename Ret, typename F, typename... Args>
        concept visit_returns =
            visit<F, Args...> && detail::visit<F, Args...>::template nothrow_returns<Ret>;

    }

    namespace detail {

        template <typename...>
        struct apply {
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
            using type = void;
        };
        template <typename F, typename... out> requires (meta::invocable<F, out...>)
        struct apply<F, meta::pack<out...>> {
            static constexpr bool enable = true;
            static constexpr bool nothrow = meta::nothrow::invocable<F, out...>;
            using type = meta::invoke_type<F, out...>;
        };
        template <typename F, typename... out, typename T, typename... Ts>
            requires (!meta::tuple_like<T>)
        struct apply<F, meta::pack<out...>, T, Ts...> {
            using result = apply<F, meta::pack<out..., T>, Ts...>;
            static constexpr bool enable = result::enable;
            static constexpr bool nothrow = result::nothrow;
            using type = result::type;
        };
        template <typename F, typename... out, meta::tuple_like T, typename... Ts>
        struct apply<F, meta::pack<out...>, T, Ts...> {
            using result = apply<
                F,
                meta::concat<meta::pack<out...>, meta::tuple_types<T>>,
                Ts...
            >;
            static constexpr bool enable = result::enable;
            static constexpr bool nothrow = result::nothrow;
            using type = result::type;
        };

        // base case: all arguments have been exhausted
        template <size_t count, typename F, typename... Ts>
        struct do_apply {
            template <size_t... prev, size_t... next, is<F> G, typename... A>
            static constexpr decltype(auto) operator()(
                ::std::index_sequence<prev...>,
                ::std::index_sequence<next...>,
                G&& func,
                A&&... args
            )
                noexcept(nothrow::invocable<G, A...>)
                requires(invocable<G, A...>)
            {
                static_assert(count == sizeof...(A));
                static_assert(sizeof...(prev) == sizeof...(A));
                static_assert(sizeof...(next) == 0);
                return (::std::forward<G>(func)(::std::forward<A>(args)...));
            }
        };

        // recursive case: unpack all elements of current tuple
        template <size_t count, typename F, tuple_like T, typename... Ts>
        struct do_apply<count, F, T, Ts...> {
            // If the current tuple is empty, skip it without unpacking any arguments
            template <size_t... prev, size_t... next, is<F> G, typename... A>
                requires (tuple_size<T> == 0)
            static constexpr decltype(auto) operator()(
                ::std::index_sequence<prev...>,
                ::std::index_sequence<next...>,
                G&& func,
                A&&... args
            )
                noexcept(noexcept(do_apply<count, F, Ts...>{}(
                    ::std::make_index_sequence<sizeof...(prev)>{},
                    ::std::make_index_sequence<
                        sizeof...(A) - (sizeof...(prev) + 1 + (sizeof...(next) != 0))
                    >{},
                    ::std::forward<G>(func),
                    unpack_arg<prev>(::std::forward<A>(args)...)...,
                    unpack_arg<sizeof...(prev) + 1 + next>(::std::forward<A>(args)...)...
                )))
            {
                return (do_apply<count, F, Ts...>{}(
                    ::std::make_index_sequence<sizeof...(prev)>{},
                    ::std::make_index_sequence<
                        sizeof...(A) - (sizeof...(prev) + 1 + (sizeof...(next) != 0))
                    >{},
                    ::std::forward<G>(func),
                    unpack_arg<prev>(::std::forward<A>(args)...)...,
                    unpack_arg<sizeof...(prev) + 1 + next>(::std::forward<A>(args)...)...
                ));
            }

            // Otherwise, if there are remaining tuple elements after this one, then
            // unpack the current element and carry the tuple forward
            template <size_t... prev, size_t... next, is<F> G, typename... A>
                requires ((sizeof...(prev) - count + 1) < tuple_size<T>)
            static constexpr decltype(auto) operator()(
                ::std::index_sequence<prev...>,
                ::std::index_sequence<next...>,
                G&& func,
                A&&... args
            )
                noexcept(noexcept(do_apply<count, F, T, Ts...>{}(
                    ::std::make_index_sequence<sizeof...(prev) + 1>{},
                    ::std::make_index_sequence<sizeof...(A) - (sizeof...(prev) + 1)>{},
                    ::std::forward<G>(func),
                    unpack_arg<prev>(::std::forward<A>(args)...)...,
                    tuple_get<sizeof...(prev) - count>(
                        unpack_arg<sizeof...(prev)>(::std::forward<A>(args)...)
                    ),
                    unpack_arg<sizeof...(prev)>(::std::forward<A>(args)...),  // carry
                    unpack_arg<sizeof...(prev) + 1 + next>(::std::forward<A>(args)...)...
                )))
            {
                return (do_apply<count, F, T, Ts...>{}(
                    ::std::make_index_sequence<sizeof...(prev) + 1>{},
                    ::std::make_index_sequence<sizeof...(A) - (sizeof...(prev) + 1)>{},
                    ::std::forward<G>(func),
                    unpack_arg<prev>(::std::forward<A>(args)...)...,
                    tuple_get<sizeof...(prev) - count>(
                        unpack_arg<sizeof...(prev)>(::std::forward<A>(args)...)
                    ),
                    unpack_arg<sizeof...(prev)>(::std::forward<A>(args)...),  // carry
                    unpack_arg<sizeof...(prev) + 1 + next>(::std::forward<A>(args)...)...
                ));
            }

            // Otherwise, if the current tuple is exhausted, unpack the last element
            // and discard it
            template <size_t... prev, size_t... next, is<F> G, typename... A>
                requires ((sizeof...(prev) - count + 1) == tuple_size<T>)
            static constexpr decltype(auto) operator()(
                ::std::index_sequence<prev...>,
                ::std::index_sequence<next...>,
                G&& func,
                A&&... args
            )
                noexcept(noexcept(do_apply<count + tuple_size<T>, F, Ts...>{}(
                    ::std::make_index_sequence<sizeof...(prev) + 1>{},
                    ::std::make_index_sequence<
                        sizeof...(A) - (sizeof...(prev) + 1 + (sizeof...(next) != 0))
                    >{},
                    ::std::forward<G>(func),
                    unpack_arg<prev>(::std::forward<A>(args)...)...,
                    tuple_get<sizeof...(prev) - count>(
                        unpack_arg<sizeof...(prev)>(::std::forward<A>(args)...)
                    ),
                    // discard tuple
                    unpack_arg<sizeof...(prev) + 1 + next>(::std::forward<A>(args)...)...
                )))
            {
                return (do_apply<count + tuple_size<T>, F, Ts...>{}(
                    ::std::make_index_sequence<sizeof...(prev) + 1>{},
                    ::std::make_index_sequence<
                        sizeof...(A) - (sizeof...(prev) + 1 + (sizeof...(next) != 0))
                    >{},
                    ::std::forward<G>(func),
                    unpack_arg<prev>(::std::forward<A>(args)...)...,
                    tuple_get<sizeof...(prev) - count>(
                        unpack_arg<sizeof...(prev)>(::std::forward<A>(args)...)
                    ),
                    // discard tuple
                    unpack_arg<sizeof...(prev) + 1 + next>(::std::forward<A>(args)...)...
                ));
            }
        };

        // recursive case: ignore non-tuple arguments and continue unpacking
        template <size_t count, typename F, typename T, typename... Ts>
        struct do_apply<count, F, T, Ts...> {
            template <size_t... prev, size_t... next, is<F> G, typename... A>
            static constexpr decltype(auto) operator()(
                ::std::index_sequence<prev...>,
                ::std::index_sequence<next...>,
                G&& func,
                A&&... args
            )
                noexcept(noexcept(do_apply<count + 1, F, Ts...>{}(
                    ::std::make_index_sequence<sizeof...(prev) + 1>{},
                    ::std::make_index_sequence<
                        sizeof...(A) - (sizeof...(prev) + 1 + (sizeof...(next) != 0))
                    >{},
                    ::std::forward<G>(func),
                    ::std::forward<A>(args)...
                )))
            {
                return (do_apply<count + 1, F, Ts...>{}(
                    ::std::make_index_sequence<sizeof...(prev) + 1>{},
                    ::std::make_index_sequence<
                        sizeof...(A) - (sizeof...(prev) + 1 + (sizeof...(next) != 0))
                    >{},
                    ::std::forward<G>(func),
                    ::std::forward<A>(args)...
                ));
            }
        };

    }

    template <typename F, typename... Ts>
    concept apply = detail::apply<F, meta::pack<>, Ts...>::enable;

    template <typename F, typename... Ts> requires (apply<F, Ts...>)
    using apply_type = detail::apply<F, meta::pack<>, Ts...>::type;

    template <typename Ret, typename F, typename... Ts>
    concept apply_returns =
        apply<F, Ts...> && convertible_to<apply_type<F, Ts...>, Ret>;

    namespace nothrow {

        template <typename F, typename... Ts>
        concept apply = detail::apply<F, meta::pack<>, Ts...>::nothrow;

        template <typename F, typename... Ts> requires (apply<F, Ts...>)
        using apply_type = detail::apply<F, meta::pack<>, Ts...>::type;

        template <typename Ret, typename F, typename... Ts>
        concept apply_returns =
            apply<F, Ts...> && convertible_to<apply_type<F, Ts...>, Ret>;

    }

}


namespace impl {

    /* Scaling is needed to uniquely encode the index sequence of all possible
    permutations for a given set of union types. */
    template <typename... As>
    static constexpr size_t vtable_size =
        (impl::visitable<As>::alternatives::size * ... * 1);

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
    ) noexcept(meta::nothrow::visit<F, A...>) {
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
        using alternatives = meta::pack<T>;
        using empty = void;  // no empty state
        using errors = meta::pack<>;  // no error states
        template <typename U = T>  // extra template needed to delay instantiation
        using to_optional = Optional<U>;  // a value for U will never be supplied
        template <typename... Errs>
        using to_expected = Expected<T, Errs...>;

        /* The active index for non-visitable types is trivially zero. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return 0;
        }

        /* Getting the value for a non-visitable type will trivially forward it. */
        template <size_t I, meta::is<T> U> requires (I < alternatives::size)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (std::forward<U>(u));
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
            noexcept(meta::nothrow::visit<F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < alternatives::size &&
                meta::visit<F, A...>
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
        static constexpr size_t N = meta::unqualify<T>::types::size;

        template <size_t I, typename... Ts>
        struct _pack {
            using type = _pack<
                I + 1,
                Ts...,
                decltype((std::declval<T>().m_storage.template get<I>()))
            >::type;
        };
        template <typename... Ts>
        struct _pack<N, Ts...> { using type = meta::pack<Ts...>; };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using alternatives = _pack<0>::type;
        using empty = void;
        using errors = meta::pack<>;
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
        template <size_t I, meta::is<T> U> requires (I < alternatives::size)
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
            noexcept(meta::nothrow::visit<F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < alternatives::size &&
                meta::visit<F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);
            return visit_recursive<R, idx>(
                get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                prev,
                next,
                std::forward<F>(func),
                std::forward<A>(args)...
            );
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
        struct _pack<N, Ts...> { using type = meta::pack<Ts...>; };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using alternatives = _pack<0>::type;
        using empty = void;
        using errors = meta::pack<>;
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
        template <size_t I, meta::is<T> U> requires (I < alternatives::size)
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
            noexcept(meta::nothrow::visit<F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < alternatives::size &&
                meta::visit<F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);
            return visit_recursive<R, idx>(
                get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                prev,
                next,
                std::forward<F>(func),
                std::forward<A>(args)...
            );
        }
    };

    /* Optionals are converted into packs of length 2. */
    template <meta::Optional T>
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using wrapped = decltype((std::declval<T>().m_storage.value()));
        using alternatives = meta::pack<wrapped, NoneType>;
        using empty = NoneType;
        using errors = meta::pack<>;
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
        template <size_t I, meta::is<T> U> requires (I == 0 && I < alternatives::size)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).m_storage.value())
        ) {
            return (std::forward<U>(u).m_storage.value());
        }

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < alternatives::size)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (None);
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
            noexcept(meta::nothrow::visit<F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < alternatives::size &&
                meta::visit<F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);

            // valid state is always handled by definition
            if constexpr (J == 0) {
                return visit_recursive<R, idx>(
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                    prev,
                    next,
                    std::forward<F>(func),
                    std::forward<A>(args)...
                );

            // empty state is implicitly propagated if left unhandled
            } else {
                using type = decltype((
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...))
                ));
                if constexpr (meta::visit<
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
        using alternatives = meta::pack<wrapped, NoneType>;
        using empty = NoneType;
        using errors = meta::pack<>;
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
        template <size_t I, meta::is<T> U> requires (I == 0 && I < alternatives::size)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).value())
        ) {
            return (std::forward<U>(u).value());
        }

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < alternatives::size)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (None);
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
            noexcept(meta::nothrow::visit<F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < alternatives::size &&
                meta::visit<F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);

            // valid state is always handled by definition
            if constexpr (J == 0) {
                return visit_recursive<R, idx>(
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                    prev,
                    next,
                    std::forward<F>(func),
                    std::forward<A>(args)...
                );

            // empty state is implicitly propagated if left unhandled
            } else {
                using type = decltype((
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...))
                ));
                if constexpr (meta::visit<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    type,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    return visit_recursive<R, idx>(
                        None,
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
            using type = decltype((std::declval<T>().get_value()));
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
                I == meta::unqualify<T>::errors::size
            )
        struct _pack<I, Errs...> {
            using type = meta::pack<
                decltype((std::declval<T>().get_value())),
                Errs...
            >;
        };
        template <size_t I, typename... Errs>
            requires (
                meta::is_void<typename meta::unqualify<T>::value_type> &&
                I == meta::unqualify<T>::errors::size
            )
        struct _pack<I, Errs...> {
            using type = meta::pack<NoneType, Errs...>;
        };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using wrapped = _wrapped<typename meta::unqualify<T>::value_type>::type;
        using alternatives = _pack<0>::type;  // rename these to alternatives
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
            if constexpr (errors::size > 1) {
                return u.has_value() ? 0 : u.get_error().index() + 1;
            } else {
                return u.has_error();
            }
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I == 0 && meta::not_void<wrapped>)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).get_value())
        ) {
            return (std::forward<U>(u).get_value());
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I == 0 && meta::is_void<wrapped>)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (None);
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < alternatives::size)
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
            noexcept(meta::nothrow::visit<F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < alternatives::size &&
                meta::visit<F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);

            // valid state is always handled by definition
            if constexpr (J == 0) {
                return visit_recursive<R, idx>(
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                    prev,
                    next,
                    std::forward<F>(func),
                    std::forward<A>(args)...
                );

            // error states are implicitly propagated if left unhandled
            } else {
                using type = decltype((
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...))
                ));
                if constexpr (meta::visit<
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
        struct _pack { using type = meta::pack<decltype((std::declval<T>().value()))>; };
        template <meta::is_void U>
        struct _pack<U> { using type = meta::pack<NoneType>; };

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
        using alternatives =
            _pack<typename meta::unqualify<T>::value_type>::type::template concat<
                typename visitable<decltype((std::declval<T>().error()))>::alternatives
            >;
        using empty = void;
        using errors = visitable<decltype((std::declval<T>().error()))>::alternatives;
        template <typename U = T>
        using to_optional = Optional<U>;
        template <typename... Errs>
        using to_expected = _to_expected<Errs...>::type;

        /* The active index of an expected is 0 for the empty state and > 0 for the
        error state(s). */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            if constexpr (errors::size > 1) {
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
                I == 0 && I < alternatives::size
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
                I == 0 && I < alternatives::size
            )
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (None);
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < alternatives::size)
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
            noexcept(meta::nothrow::visit<F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < alternatives::size &&
                meta::visit<F, A...>
            )
        {
            static constexpr size_t I = sizeof...(Prev);
            static constexpr size_t J = visit_index<idx, A...>(prev, next);

            // valid state is always handled by definition
            if constexpr (J == 0) {
                return visit_recursive<R, idx>(
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...)),
                    prev,
                    next,
                    std::forward<F>(func),
                    std::forward<A>(args)...
                );

            // error states are implicitly propagated if left unhandled
            } else {
                using type = decltype((
                    get<J>(meta::unpack_arg<I>(std::forward<A>(args)...))
                ));
                if constexpr (meta::visit<
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

    template <typename T>
    struct _union_member_type { using type = meta::remove_rvalue<T>; };
    template <meta::lvalue T>
    struct _union_member_type<T> { using type = meta::as_pointer<T>; };

    /* Union alternatives either have their lifetime extended, or are converted to
    pointers if they are lvalues. */
    template <typename T>
    using union_member_type = _union_member_type<T>::type;

    template <size_t N>
    struct _union_index_type { using type = size_t; };
    template <size_t N> requires (N <= std::numeric_limits<uint8_t>::max())
    struct _union_index_type<N> { using type = uint8_t; };
    template <size_t N>
        requires (
            N > std::numeric_limits<uint8_t>::max() &&
            N <= std::numeric_limits<uint16_t>::max()
        )
    struct _union_index_type<N> { using type = uint16_t; };
    template <size_t N>
        requires (
            N > std::numeric_limits<uint16_t>::max() &&
            N <= std::numeric_limits<uint32_t>::max()
        )
    struct _union_index_type<N> { using type = uint32_t; };

    /* The tracking index stored within a `Union` is defined as the smallest unsigned
    integer type big enough to hold all alternatives, in order to exploit favorable
    packing dynamics with respect to the aligned alternatives. */
    template <size_t N> requires (N <= std::numeric_limits<size_t>::max())
    using union_index_type = _union_index_type<N>::type;

}


/* Invoke a function with the given arguments, unwrapping any sum types in the process.
This is similar to `std::visit()`, but with greatly expanded metaprogramming
capabilities.

The visitor is constructed from either a single function or a set of functions defined
using `bertrand::visitor` or a similar overload set.  The subsequent arguments will be
passed to the visitor in the order they are defined, with each union being unwrapped to
its actual type within the visitor context.  A compilation error will occur if the
visitor is not callable with all nontrivial permutations of the unwrapped values.

Note that the visitor does not need to be exhaustive over all possible permutations;
only the valid ones.  Practically speaking, this means that the visitor is free to
ignore the empty states of optionals and error states of expected inputs, which will be
implicitly propagated to the return type if left unhandled.  On the other hand, union
states and non-empty/result states of optionals and expecteds must be explicitly
handled, else a compilation error will occur.  This equates to adding implicit
overloads to the visitor that trivially forward these states without any modifications,
allowing visitors to treat optional and expected types as if they were always in the
valid state.  The `bertrand::meta::exhaustive<F, Args...>` concept can be used to
selectively forbid these cases at compile time, which returns the semantics to those of
`std::visit()` with respect to visitor exhaustiveness.

Similarly, unlike `std::visit()`, the visitor is not constrained to return a single
consistent type for all permutations.  If it does not, then the return type `R` will
deduce to `bertrand::Union<Rs...>`, where `Rs...` are the unique return types for all
valid permutations.  Otherwise, if all permutations return the same type, then `R` will
simply be that type.  If some permutations return `void` and others return non-`void`,
then `R` will be wrapped in an `Optional`, where the empty state indicates a void
return type at runtime.  If `R` is itself an optional type, then the empty states will
be merged so as to avoid nested optionals.  Similar to above, the
`bertrand::meta::consistent<F, Args...>` concept can be used to selectively forbid
these cases at compile time.  Applying that concept in combination with
`bertrand::meta::exhaustive<F, Args...>` will yield the same visitor semantics as
`std::visit()`.

If both of the above features are enabled (the default), then a worst-case,
inconsistent visitor applied to an `Expected<Union<Ts...>, Es...>` input, where some
permutations of `Ts...` return void and others do not, can potentially yield a return
type of the form `Expected<Optional<Union<Rs...>>, Es...>`, where `Rs...` are the
non-void return types for the valid permutations, `Optional` signifies that the return
type may be void, and `Es...` are the original error types that were implicitly
propagated from the input.  If the visitor exhaustively handles the error states, then
the return type will reduce to `Optional<Union<Rs...>>`.  If the visitor never returns
void, then the return type will further reduce to `Union<Rs...>`, and if it returns a
single consistent type, then `R` will collapse to just that type, in which case the
semantics are identical to `std::visit()`.

Finally, note that the arguments are fully generic, and not strictly limited to union
types, in contrast to `std::visit()`.  If no unions are present, then `visit()`
devolves to invoking the visitor normally, without any special handling.  Otherwise,
the component unions are expanded according to the rules laid out in
`bertrand::impl::visitable`, which describes how to register custom visitable types
for use with this function.  Built-in specializations exist for `Union`, `Optional`,
and `Expected`, as well as `std::variant`, `std::optional`, and `std::expected`, each
of which can be passed to `visit()` according to the described semantics. */
template <typename F, typename... Args> requires (meta::visit<F, Args...>)
constexpr meta::visit_type<F, Args...> visit(F&& f, Args&&... args)
    noexcept(meta::nothrow::visit<F, Args...>)
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
                        noexcept(meta::nothrow::visit<F, Args...>) -> R
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


/* Invoke a function with the given arguments, unpacking any product types in the
process.  This is similar to `std::apply()`, but permits any number of tuples as
arguments, including none, whereby it devolves to a simple function call.

Like `bertrand::visit()`, the visitor is constructed from either a single function or a
set of functions defined using `bertrand::visitor` or a similar overload set.  The
subsequent arguments will be passed to the visitor in the order they are defined, with
any type that defines `std::tuple_size<T>` and `std::get<I>()` being unpacked into its
individual elements, which are then passed as separate, consecutive arguments.  A
compilation error will occur if the visitor is not callable with the unpacked
arguments.

In the same way that structured bindings can be used to unpack tuples into their
constituent elements, `bertrand::apply()` unpacks them as arguments to a function.
For example:

    ```
    std::pair<int, std::string> p{2, "hello"};
    assert(apply(
        [](int i, const std::string& s, double d) { return i + s.size() + d; },
        p,
        3.25
    ) == 10.25);
    ```

Note that `bertrand::apply()` and `bertrand::visit()` operate very similarly, but are
not interchangeable, and one does not imply the other.  This equates to the difference
between sum types and product types in an algebraic type system.  In particular, sum
types consist of a collection of types joined by a logical OR (`A` OR `B` OR `C`),
whereas product types represent the logical AND of those same types (`A` AND `B` AND
`C`).  `bertrand::visit()` is used to unwrap the former, while `bertrand::apply()`
handles the latter. */
template <typename F, typename... Ts>
constexpr decltype(auto) apply(F&& func, Ts&&... args)
    noexcept(meta::nothrow::apply<F, Ts...>)
    requires(meta::apply<F, Ts...>)
{
    return (meta::detail::do_apply<0, F, Ts...>{}(
        std::make_index_sequence<0>{},
        std::make_index_sequence<sizeof...(Ts) - (sizeof...(Ts) > 0)>{},
        std::forward<F>(func),
        std::forward<Ts>(args)...
    ));
}


template <typename... Ts>
    requires (sizeof...(Ts) > 1 && (meta::not_void<Ts> && ...) && meta::unique<Ts...>)
struct Union : impl::union_tag {
    using types = meta::pack<Ts...>;

private:
    template <typename T>
    friend struct impl::visitable;
    template <meta::not_void T> requires (!meta::None<T>)
    friend struct bertrand::Optional;
    template <typename T, meta::unqualified E, meta::unqualified... Es>
        requires (meta::inherits<E, Exception> && ... && meta::inherits<Es, Exception>)
    friend struct bertrand::Expected;

    // determine the common type for the members of the union, if one exists.
    template <typename... Us>
    struct get_common_type { using type = void; };
    template <typename... Us> requires (meta::has_common_type<Us...>)
    struct get_common_type<Us...> { using type = meta::common_type<Us...>; };

    // Find the first type in Ts... that is default constructible, or void
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

    template <typename T, typename U>
    static constexpr bool match_proximal =
        meta::inherits<T, U> &&
        meta::convertible_to<T, U> &&
        (meta::lvalue<T> ? !meta::rvalue<U> : !meta::lvalue<U>);

    template <typename T, typename U, typename convert>
    static constexpr bool match_convertible =
        meta::is_void<convert> &&
        !meta::lvalue<U> &&
        meta::convertible_to<T, U>;

    // result 1: convert to proximal type
    template <typename T, typename proximal, typename convert, typename... Us>
    struct _convert_type { using type = proximal; };

    // result 2: convert to first implicitly convertible type (void if none)
    template <typename T, meta::is_void proximal, typename convert>
    struct _convert_type<T, proximal, convert> { using type = convert; };

    // recursive 1: prefer the most derived and least qualified matching alternative,
    // with lvalues binding to lvalues and prvalues, and rvalues binding to rvalues and
    // prvalues
    template <typename T, typename proximal, typename convert, typename U, typename... Us>
        requires (match_proximal<T, U>)
    struct _convert_type<T, proximal, convert, U, Us...> {
        // if the result type is void, or if the candidate is more derived than it,
        // or if the candidate is less qualified, replace the intermediate result
        using type = _convert_type<
            T,
            std::conditional_t<
                meta::is_void<proximal> ||
                (meta::inherits<U, proximal> && !meta::is<U, proximal>) || (
                    meta::is<U, proximal> && (
                        (meta::lvalue<U> && !meta::lvalue<proximal>) ||
                        meta::more_qualified_than<proximal, U>
                    )
                ),
                U,
                proximal
            >,
            convert,
            Us...
        >::type;
    };

    // recursive 2: if no proximal match is found, prefer the leftmost implicitly
    // convertible type.
    template <typename T, typename proximal, typename convert, typename U, typename... Us>
        requires (!match_proximal<T, U> && match_convertible<T, U, convert>)
    struct _convert_type<T, proximal, convert, U, Us...> {
        using type = _convert_type<T, proximal, U, Us...>::type;
    };

    // recursive 3: no match at this index, advance U
    template <typename T, typename proximal, typename convert, typename U, typename... Us>
        requires (!match_proximal<T, U> && !match_convertible<T, U, convert>)
    struct _convert_type<T, proximal, convert, U, Us...> {
        using type = _convert_type<T, proximal, convert, Us...>::type;
    };

    // Find the alternative to which a type T can be converted, or void
    template <typename T>
    using convert_type = _convert_type<T, void, void, Ts...>::type;

    // explicit constructors can be used if no conversion type is found
    template <typename... A>
    struct _construct_type {
        template <typename... Us>
        struct infer { using type = void; };
        template <typename U, typename... Us>
        struct infer<U, Us...> { using type = infer<Us...>::type; };
        template <meta::constructible_from<A...> U, typename... Us>
        struct infer<U, Us...> { using type = U; };
    };
    template <typename... A>
    using construct_type = _construct_type<A...>::template infer<Ts...>::type;

    template <size_t I>
    struct tag {};

    [[no_unique_address]] struct storage {
        // recursive C unions are the only way to make Union<Ts...> provably safe at
        // compile time without tripping the compiler's UB filters
        template <typename... Us>
        union store { constexpr ~store() noexcept {}; };
        template <typename U, typename... Us>
        union store<U, Us...> {
            [[no_unique_address]] impl::union_member_type<U> curr;
            [[no_unique_address]] store<Us...> rest;
            constexpr ~store() noexcept {}

            template <typename... Args> requires (!meta::lvalue<U>)
            constexpr store(tag<0>, Args&&... args)
                noexcept(meta::nothrow::constructible_from<
                    impl::union_member_type<U>,
                    Args...
                >)
                requires(meta::constructible_from<
                    impl::union_member_type<U>,
                    Args...
                >)
            :
                curr(std::forward<Args>(args)...)  // not an lvalue by definition
            {}

            template <typename T> requires (meta::lvalue<U>)
            constexpr store(tag<0>, T&& value)
                noexcept(meta::nothrow::convertible_to<
                    meta::address_type<meta::as_lvalue<T>>,
                    impl::union_member_type<U>
                >)
                requires(meta::convertible_to<
                    meta::address_type<meta::as_lvalue<T>>,
                    impl::union_member_type<U>
                >)
            :
                curr(&value)  // lvalues are converted to pointers
            {}

            template <size_t I, typename... Args> requires (I > 0)
            constexpr store(tag<I>, Args&&... args)
                noexcept(meta::nothrow::constructible_from<
                    store<Us...>,
                    tag<I - 1>,
                    Args...
                >)
                requires(meta::constructible_from<
                    store<Us...>,
                    tag<I - 1>,
                    Args...
                >)
            :
                rest(tag<I - 1>{}, std::forward<Args>(args)...)  // recur
            {}

            template <size_t I> requires (I == 0)
            constexpr decltype(auto) get(this auto&& self) noexcept {
                if constexpr (meta::lvalue<U>) {
                    return (*std::forward<decltype(self)>(self).curr);
                } else {
                    return (std::forward<decltype(self)>(self).curr);
                }
            }

            template <size_t I> requires (I > 0)
            constexpr decltype(auto) get(this auto&& self) noexcept {
                return (std::forward<decltype(self)>(self).rest.template get<I - 1>());
            }
        };

        [[no_unique_address]] store<Ts...> data;
        [[no_unique_address]] impl::union_index_type<sizeof...(Ts)> index;

        // get() implementation, accounting for lvalues and recursion
        template <size_t I> requires (I < types::size)
        constexpr decltype(auto) get(this auto&& self) noexcept {
            return (std::forward<decltype(self)>(self).data.template get<I>());
        }
    } m_storage;

    template <size_t I, typename Self> requires (I < types::size)
    using access = decltype((std::declval<Self>().m_storage.template get<I>()));

    template <typename T, typename Self> requires (types::template contains<T>)
    using access_t = decltype((
        std::declval<Self>().m_storage.template get<types::template index<T>>()
    ));

    template <size_t I, typename Self> requires (I < types::size)
    using exp = Expected<access<I, Self>, BadUnionAccess>;

    template <typename T, typename Self> requires (types::template contains<T>)
    using exp_t = Expected<access_t<T, Self>, BadUnionAccess>;

    template <size_t I, typename Self> requires (I < types::size)
    using opt = Optional<access<I, Self>>;

    template <typename T, typename Self> requires (types::template contains<T>)
    using opt_t = Optional<access_t<T, Self>>;

    template <size_t I>
    static constexpr std::array get_index_error {+[] {
        return BadUnionAccess(
            impl::int_to_static_string<I> + " is not the active type in the union "
            "(active is " + impl::int_to_static_string<types::template index<Ts>> +
            ")"
        );
    }...};

    template <typename T>
    static constexpr std::array get_type_error {+[] {
        return BadUnionAccess(
            "'" + demangle<T>() + "' is not the active type in the union "
            "(active is '" + demangle<Ts>() + "')"
        );
    }...};

    template <typename... Us>
        requires (meta::copyable<meta::remove_rvalue<Us>> && ...)
    static constexpr std::array copy_constructors {
        +[](const Union& other) noexcept(
            (meta::nothrow::copyable<meta::remove_rvalue<Us>> && ...)
        ) -> storage::template store<Us...> {
            return {
                tag<types::template index<Us>>{},
                other.m_storage.template get<types::template index<Us>>()
            };
        }...
    };

    template <typename... Us>
        requires (meta::movable<meta::remove_rvalue<Us>> && ...)
    static constexpr std::array move_constructors {
        +[](Union&& other) noexcept(
            (meta::nothrow::movable<meta::remove_rvalue<Us>> && ...)
        ) -> storage::template store<Us...> {
            return {
                tag<types::template index<Us>>{},
                std::move(other).m_storage.template get<types::template index<Us>>()
            };
        }...
    };

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
        constexpr size_t I = types::template index<T>;
        return std::array{
            +[](Union& self, Union& other) noexcept(
                (nothrow_swappable<meta::remove_rvalue<Ts>> && ...)
            ) {
                using U = meta::remove_rvalue<Ts>;
                constexpr size_t J = types::template index<Ts>;

                // delegate to a swap operator if available
                if constexpr (meta::swappable_with<T, U>) {
                    std::ranges::swap(
                        self.m_storage.template get<I>(),
                        other.m_storage.template get<J>()
                    );

                // otherwise, fall back to move assignment operators with a temporary
                // value
                } else if constexpr (
                    meta::is<T, U> &&
                    meta::move_assignable<meta::as_lvalue<T>> &&
                    meta::move_assignable<meta::as_lvalue<U>>
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
                        if constexpr (!meta::trivially_destructible<U>) {
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
                        if constexpr (!meta::trivially_destructible<U>) {
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

    // swap operators require a 2D vtable for each pair of types in both unions
    template <typename... Us> requires (swappable<meta::remove_rvalue<Us>> && ...)
    static constexpr std::array swap_operators {
        pairwise_swap<meta::remove_rvalue<Us>>()...
    };

    template <typename...>
    struct ConvertFrom {};
    template <typename From> requires (!meta::monad<From>)
    struct ConvertFrom<From> {
        template <typename T>
        static constexpr storage operator()(T&& value)
            noexcept(meta::nothrow::convertible_to<T, convert_type<T>>)
            requires(meta::not_void<convert_type<T>>)
        {
            return {
                .data = {
                    tag<types::template index<convert_type<T>>>{},
                    std::forward<T>(value)
                },
                .index = types::template index<convert_type<T>>
            };
        }
    };

    struct ConstructFrom {
        template <typename... A>
        static constexpr storage operator()(A&&... args)
            noexcept(meta::nothrow::constructible_from<construct_type<A...>, A...>)
            requires(meta::not_void<construct_type<A...>>)
        {
            return {
                .data = {
                    tag<types::template index<construct_type<A...>>>{},
                    std::forward<A>(args)...
                },
                .index = types::template index<construct_type<A...>>
            };
        }
    };

    template <typename Self>
    struct Flatten {
        using type = get_common_type<access_t<Ts, Self>...>::type;
        template <typename T>
        static constexpr type operator()(T&& value)
            noexcept(meta::nothrow::convertible_to<T, type>)
            requires(meta::convertible_to<T, type>)
        {
            return std::forward<T>(value);
        }
    };

    template <size_t I, typename... Args> requires (I < types::size)
    constexpr Union(tag<I>, Args&&... args)
        noexcept(meta::nothrow::constructible_from<
            meta::remove_rvalue<meta::unpack_type<I, Ts...>>,
            Args...
        >)
        requires(meta::constructible_from<
            meta::remove_rvalue<meta::unpack_type<I, Ts...>>,
            Args...
        >)
    :
        m_storage{
            .data = {tag<I>{}, std::forward<Args>(args)...},
            .index = I
        }
    {}

public:
    /* Construct a union with an explicit type, rather than using the converting
    constructor, which can introduce ambiguity. */
    template <size_t I, typename... Args> requires (I < types::size)
    [[nodiscard]] static constexpr Union create(Args&&... args)
        noexcept(meta::nothrow::constructible_from<
            meta::remove_rvalue<meta::unpack_type<I, Ts...>>,
            Args...
        >)
        requires(meta::constructible_from<
            meta::remove_rvalue<meta::unpack_type<I, Ts...>>,
            Args...
        >)
    {
        return {tag<I>{}, std::forward<Args>(args)...};
    }

    /* Construct a union with an explicit type, rather than using the converting
    constructor, which can introduce ambiguity. */
    template <typename T, typename... Args> requires (types::template contains<T>)
    [[nodiscard]] static constexpr Union create(Args&&... args)
        noexcept(meta::nothrow::constructible_from<meta::remove_rvalue<T>, Args...>)
        requires(meta::constructible_from<meta::remove_rvalue<T>, Args...>)
    {
        return {tag<types::template index<T>>{}, std::forward<Args>(args)...};
    }

    /* Default constructor finds the first type in `Ts...` that can be default
    constructed.  If no such type exists, the default constructor is disabled. */
    [[nodiscard]] constexpr Union()
        noexcept(meta::nothrow::default_constructible<default_type>)
        requires(meta::not_void<default_type>)
    :
        m_storage{
            .data = {tag<types::template index<default_type>>{}},
            .index = types::template index<default_type>
        }
    {}

    /* Converting constructor finds the most proximal type in `Ts...` that can be
    implicitly converted from the input type.  This will prefer exact matches or
    differences in qualifications (preferring the least qualified) first, followed by
    inheritance relationships (preferring the most derived and least qualified), and
    finally implicit conversions (preferring the first match and ignoring lvalues).  If
    no such type exists, the conversion constructor is disabled.  If a visitable type
    is provided, then the conversion must be exhaustive over all alternatives, enabling
    implicit conversions from other union types, regardless of source. */
    template <typename T>
        requires (
            meta::exhaustive<ConvertFrom<T>, T> &&
            meta::consistent<ConvertFrom<T>, T>
        )
    [[nodiscard]] constexpr Union(T&& v)
        noexcept(meta::nothrow::exhaustive<ConvertFrom<T>, T>)
    :
        m_storage(bertrand::visit(ConvertFrom<T>{}, std::forward<T>(v)))
    {}

    /* Explicit constructor finds the first type in `Ts...` that can be constructed
    with the given arguments.  If no such type exists, the explicit constructor is
    disabled.  If one or more visitables are provided, then the constructor must be
    exhaustive over all alternatives, enabling explicit conversions from other
    union types, regardless of source. */
    template <typename... Args>
        requires (
            sizeof...(Args) > 0 &&
            !meta::exhaustive<ConvertFrom<Args...>, Args...> &&
            meta::exhaustive<ConstructFrom, Args...> &&
            meta::consistent<ConstructFrom, Args...>
        )
    [[nodiscard]] constexpr explicit Union(Args&&... args)
        noexcept(meta::nothrow::exhaustive<ConstructFrom, Args...>)
    :
        m_storage(bertrand::visit(ConstructFrom{}, std::forward<Args>(args)...))
    {}

    /* Copy constructor.  The resulting union will have the same index as the input
    union, and will be initialized by copy constructing the stored type. */
    [[nodiscard]] constexpr Union(const Union& other)
        noexcept((meta::nothrow::copyable<meta::remove_rvalue<Ts>> && ...))
        requires((meta::copyable<meta::remove_rvalue<Ts>> && ...))
    :
        m_storage{
            .data = copy_constructors<Ts...>[other.index()](other),
            .index = other.m_storage.index
        }
    {}

    /* Move constructor.  The resulting union will have the same index as the input
    union, and will be initialized by move constructing the stored type. */
    [[nodiscard]] constexpr Union(Union&& other)
        noexcept((meta::nothrow::movable<meta::remove_rvalue<Ts>> && ...))
        requires((meta::movable<meta::remove_rvalue<Ts>> && ...))
    :
        m_storage{
            .data = move_constructors<Ts...>[other.index()](std::move(other)),
            .index = other.m_storage.index
        }
    {}

    /* Copy assignment operator.  Uses copy and swap (CAS) for strong exception
    safety. */
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
            swap_operators<Ts...>[index()][temp.index()](*this, temp);
        }
        return *this;
    }

    /* Move assignment operator.  Uses move and swap (move-optimized CAS) for strong
    exception safety. */
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
            swap_operators<Ts...>[index()][temp.index()](*this, temp);
        }
        return *this;
    }

    /* Destructor cleans up the currently-active type, respecting lvalue semantics and
    constexpr contexts. */
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
                        &self.m_storage.template get<types::template index<Ts>>()
                    );
                }
            }...
        };
        destructors[index()](*this);
    }

    /* Implicit conversion operator allows conversions toward any type to which all
    alternatives can be exhaustively converted.  This allows conversion to scalar types
    as well as union types (regardless of source) that satisfy the conversion
    criteria. */
    template <typename Self, typename T>
    [[nodiscard]] constexpr operator T(this Self&& self)
        noexcept(meta::nothrow::exhaustive<impl::ConvertTo<T>, Self>)
        requires(meta::exhaustive<impl::ConvertTo<T>, Self>)
    {
        return bertrand::visit(
            impl::ConvertTo<T>{},
            std::forward<Self>(self)
        );
    }

    /* Explicit conversion operator allows functional-style conversions toward any type
    to which all alternatives can be exhaustively converted.  This allows conversion to
    scalar types as well as union types (regardless of source) that satisfy the
    conversion criteria. */
    template <typename Self, typename T>
    [[nodiscard]] constexpr explicit operator T(this Self&& self)
        noexcept(meta::nothrow::exhaustive<impl::ExplicitConvertTo<T>, T>)
        requires(
            !meta::exhaustive<impl::ConvertTo<T>, Self> &&
            meta::exhaustive<impl::ExplicitConvertTo<T>, Self>
        )
    {
        return bertrand::visit(
            impl::ExplicitConvertTo<T>{},
            std::forward<Self>(self)
        );
    }

    /* Swap the contents of two unions as efficiently as possible.  This will use
    swap operators for the wrapped alternatives if possible, otherwise falling back to
    a 3-way move using a temporary of the same type. */
    constexpr void swap(Union& other)
        noexcept((nothrow_swappable<Ts> && ...))
        requires((swappable<Ts> && ...))
    {
        if (this != &other) {
            swap_operators<Ts...>[index()][other.index()](*this, other);
            std::swap(m_storage.index, other.m_storage.index);
        }
    }
    
    /* Get the index of the currently-active type in the union. */
    [[nodiscard]] constexpr size_t index() const noexcept {
        return m_storage.index;
    }

    /* Check whether the variant holds a specific type. */
    template <size_t I> requires (I < types::size)
    [[nodiscard]] constexpr bool holds_alternative() const noexcept {
        return index() == I;
    }

    /* Check whether the variant holds a specific type. */
    template <typename T> requires (types::template contains<T>)
    [[nodiscard]] constexpr bool holds_alternative() const noexcept {
        return index() == types::template index<T>;
    }

    /* Get the value of the type at index `I`.  Fails to compile if the index is out of
    range.  Otherwise, returns an `Expected<T, BadUnionAccess>` where `T` is the type
    at index `I`, forwarded according the qualifiers of the enclosing union. */
    template <size_t I, typename Self> requires (I < types::size)
    [[nodiscard]] constexpr exp<I, Self> get(this Self&& self)
        noexcept(
            meta::nothrow::convertible_to<BadUnionAccess, exp<I, Self>> &&
            meta::nothrow::convertible_to<access<I, Self>, exp<I, Self>>
        )
        requires(
            meta::convertible_to<BadUnionAccess, exp<I, Self>> &&
            meta::convertible_to<access<I, Self>, exp<I, Self>>
        )
    {
        if (self.index() != I) {
            return get_index_error<I>[self.index()]();
        }
        return std::forward<Self>(self).m_storage.template get<I>();
    }

    /* Get the value for the templated type.  Fails to compile if the templated type
    is not a valid union member.  Otherwise, returns an `Expected<T, BadUnionAccess>`,
    where `T` is forwarded according to the qualifiers of the enclosing union. */
    template <typename T, typename Self> requires (types::template contains<T>)
    [[nodiscard]] constexpr exp_t<T, Self> get(this Self&& self)
        noexcept(
            meta::nothrow::convertible_to<BadUnionAccess, exp_t<T, Self>> &&
            meta::nothrow::convertible_to<access_t<T, Self>, exp_t<T, Self>>
        )
        requires(
            meta::convertible_to<BadUnionAccess, exp_t<T, Self>> &&
            meta::convertible_to<access_t<T, Self>, exp_t<T, Self>>
        )
    {
        if (self.index() != types::template index<T>) {
            return get_type_error<T>[self.index()]();
        }
        return std::forward<Self>(self).m_storage.template get<
            types::template index<T>
        >();
    }

    /* Get an optional wrapper for the value of the type at index `I`.  If that is not
    the active type, returns an empty optional instead. */
    template <size_t I, typename Self> requires (I < types::size)
    [[nodiscard]] constexpr opt<I, Self> get_if(this Self&& self)
        noexcept(
            meta::nothrow::default_constructible<opt<I, Self>> &&
            meta::nothrow::convertible_to<access<I, Self>, opt<I, Self>>
        )
        requires(
            meta::default_constructible<opt<I, Self>> &&
            meta::convertible_to<access<I, Self>, opt<I, Self>>
        )
    {
        if (self.index() != I) {
            return {};
        }
        return std::forward<Self>(self).m_storage.template get<I>();
    }

    /* Get an optional wrapper for the value of the templated type.  If that is not
    the active type, returns an empty optional instead. */
    template <typename T, typename Self> requires (types::template contains<T>)
    [[nodiscard]] constexpr opt_t<T, Self> get_if(this Self&& self)
        noexcept(
            meta::nothrow::default_constructible<opt_t<T, Self>> &&
            meta::nothrow::convertible_to<access_t<T, Self>, opt_t<T, Self>>
        )
        requires(
            meta::default_constructible<opt_t<T, Self>> &&
            meta::convertible_to<access_t<T, Self>, opt_t<T, Self>>
        )
    {
        if (self.index() != types::template index<T>) {
            return {};
        }
        return std::forward<Self>(self).m_storage.template get<
            types::template index<T>
        >();
    }

    /* A member equivalent for `bertrand::visit()`, which always inserts this union as
    the first argument, for chaining purposes.  See `bertrand::visit()` for more
    details. */
    template <typename F, typename Self, typename... Args>
    constexpr decltype(auto) visit(this Self&& self, F&& f, Args&&... args)
        noexcept(meta::nothrow::visit<F, Self, Args...>)
        requires(meta::visit<F, Self, Args...>)
    {
        return (bertrand::visit(
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        ));
    }

    /* Flatten the union into a single type, implicitly converting the active member to
    the common type, assuming one exists.  If no common type exists, then this will
    fail to compile.  Note that this takes cvref qualifiers for both the union and its
    alternatives into account at the point it is called. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) flatten(this Self&& self)
        noexcept(meta::nothrow::exhaustive<Flatten<Self>, Self>)
        requires(
            meta::exhaustive<Flatten<Self>, Self> &&
            meta::consistent<Flatten<Self>, Self>
        )
    {
        return (bertrand::visit(Flatten<Self>{}, std::forward<Self>(self)));
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
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args)
        noexcept(meta::nothrow::visit<impl::Call, Self, Args...>)
        requires(meta::visit<impl::Call, Self, Args...>)
    {
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
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys)
        noexcept(meta::nothrow::visit<impl::Subscript, Self, Key...>)
        requires(meta::visit<impl::Subscript, Self, Key...>)
    {
        return (bertrand::visit(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<Key>(keys)...
        ));
    }
};


template <meta::not_void T> requires (!meta::None<T>)
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
        using type = Union<value_type, NoneType>;
        type data = None;
        constexpr void swap(storage& other)
            noexcept(noexcept(data.swap(other.data)))
            requires(requires{data.swap(other.data);})
        {
            data.swap(other.data);
        }
        constexpr void reset() noexcept(noexcept(data = None)) {
            data = None;
        }
        constexpr bool has_value() const noexcept {
            return data.index() == 0;
        }
        template <typename Self>
        constexpr decltype(auto) value(this Self&& self) noexcept {
            return std::forward<Self>(self).data.m_storage.template get<0>();
        }
    };

    template <meta::lvalue U>
    struct storage<U> {
        using type = pointer;
        type data = nullptr;
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

    template <typename...>
    struct ConvertFrom {};
    template <typename From> requires (!meta::monad<From>)
    struct ConvertFrom<From> {
        using type = meta::remove_rvalue<value_type>;
        template <typename V>
        static constexpr storage<T> operator()(V&& value)
            noexcept(meta::nothrow::convertible_to<V, type>)
            requires(meta::convertible_to<V, type>)
        {
            return {std::forward<V>(value)};
        }
        template <typename V>
        static constexpr storage<T> operator()(V&& value)
            noexcept(meta::nothrow::default_constructible<storage<T>>)
            requires (
                !meta::convertible_to<V, type> &&
                meta::is<V, typename impl::visitable<From>::empty>
            )
        {
            return {None};
        }
    };
    template <typename From>
        requires (
            !meta::monad<From> &&
            meta::lvalue<value_type> &&
            meta::addressable<value_type> &&
            meta::convertible_to<meta::address_type<value_type>, pointer>
        )
    struct ConvertFrom<From> {
        static constexpr storage<T> operator()(pointer value) noexcept {
            return {value};
        }
        static constexpr storage<T> operator()(value_type value)
            noexcept(
                meta::nothrow::addressable<value_type> &&
                meta::nothrow::convertible_to<meta::address_type<value_type>, pointer>
            )
        {
            return {&value};
        }
        template <meta::is<typename impl::visitable<From>::empty> V>
        static constexpr storage<T> operator()(V&& value) noexcept {
            return {nullptr};
        }
    };

    struct ConstructFrom {
        using type = meta::remove_rvalue<value_type>;
        template <typename... A> requires (!meta::lvalue<type>)
        static constexpr storage<T> operator()(A&&... args)
            noexcept(meta::nothrow::constructible_from<type, A...>)
            requires(meta::constructible_from<type, A...>)
        {
            return {typename storage<T>::type(std::forward<A>(args)...)};
        }
    };

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
    [[nodiscard]] constexpr Optional(NoneType t = None) noexcept {}

    /* Implicit conversion from references to optional references, bypassing
    visitors. */
    [[nodiscard]] constexpr Optional(value_type v)
        noexcept(
            meta::nothrow::addressable<value_type> &&
            meta::nothrow::convertible_to<meta::address_type<value_type>, pointer>
        )
        requires(
            meta::lvalue<value_type> &&
            meta::addressable<value_type> &&
            meta::convertible_to<meta::address_type<value_type>, pointer>
        )
    :
        m_storage(&v)
    {}

    /* Implicit conversion from pointers to optional references. */
    [[nodiscard]] constexpr Optional(pointer p) noexcept
        requires(meta::lvalue<value_type>)
    :
        m_storage(p)
    {}

    /* Converting constructor.  Implicitly converts the input to the value type, and
    then initializes the optional in that state.  Also allows implicit conversions from
    any type `T` where `bertrand::impl::visitable<T>::empty` is not void and all
    non-empty alternatives can be converted to the value type (e.g. `std::optional<U>`,
    where `U` is convertible to `value_type`). */
    template <typename V>
        requires (
            meta::exhaustive<ConvertFrom<V>, V> &&
            meta::consistent<ConvertFrom<V>, V>
        )
    [[nodiscard]] constexpr Optional(V&& v)
        noexcept(meta::nothrow::exhaustive<ConvertFrom<V>, V>)
    : 
        m_storage(bertrand::visit(ConvertFrom<V>{}, std::forward<V>(v)))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the value type's
    constructor, calls it, and then initializes the optional with the result. */
    template <typename... Args>
        requires (
            sizeof...(Args) > 0 &&
            !meta::exhaustive<ConvertFrom<Args...>, Args...> &&
            meta::exhaustive<ConstructFrom, Args...> &&
            meta::consistent<ConstructFrom, Args...>
        )
    [[nodiscard]] constexpr explicit Optional(Args&&... args)
        noexcept(meta::nothrow::exhaustive<ConstructFrom, Args...>)
    :
        m_storage(bertrand::visit(ConstructFrom{}, std::forward<Args>(args)...))
    {}

    /* Implicit conversion from `Optional<T>` to `std::optional<T>` and other similar
    types that are convertible from both the value type and `bertrand::None` (aka
    `std::nullopt`). */
    template <typename Self, typename V>
        requires (
            meta::convertible_to<access<Self>, V> &&
            meta::convertible_to<NoneType, V>
        )
    [[nodiscard]] constexpr operator V(this Self&& self) noexcept(
        meta::nothrow::convertible_to<access<Self>, V> &&
        meta::nothrow::convertible_to<NoneType, V>
    ) {
        if (self.has_value()) {
            return std::forward<Self>(self).m_storage.value();
        }
        return None;
    }

    /* Implicit conversion from `Optional<T&>` to pointers and other similar types that
    are convertible from both the address type of the value and `nullptr`. */
    template <typename Self, typename V>
        requires (
            meta::lvalue<value_type> &&
            meta::convertible_to<meta::address_type<access<Self>>, V> &&
            meta::convertible_to<std::nullptr_t, V> &&
            !meta::convertible_to<access<Self>, V> &&
            !meta::convertible_to<NoneType, V>
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
    be invoked with an argument of type `bertrand::NoneType` (aka `std::nullopt_t`).
    Otherwise, it will be invoked with the output from `.value()`. */
    template <typename F, typename Self, typename... Args>
        requires (meta::visit<F, Self, Args...>)
    constexpr decltype(auto) visit(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visit<F, Self, Args...>
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
            meta::visit<and_then_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) and_then(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visit<and_then_fn<F, Self, Args...>, F, Self, Args...>
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
    need to accept `bertrand::NoneType` (aka `std::nullopt_t`) explicitly, and will not
    be invoked if the optional holds a value.  All other rules (including promotion to
    union or handling of void return types) remain the same.

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
            meta::visit<or_else_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) or_else(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visit<or_else_fn<F, Self, Args...>, F, Self, Args...>
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
        requires (meta::visit<impl::Call, Self, Args...>)
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args) noexcept(
        meta::nothrow::visit<impl::Call, Self, Args...>
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
        requires (meta::visit<impl::Subscript, Self, Key...>)
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys) noexcept(
        meta::nothrow::visit<impl::Subscript, Self, Key...>
    ) {
        return (bertrand::visit(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<Key>(keys)...
        ));
    }
};


template <typename T, meta::unqualified E, meta::unqualified... Es>
    requires (meta::inherits<E, Exception> && ... && meta::inherits<Es, Exception>)
struct Expected : impl::expected_tag {
    using errors = meta::pack<E, Es...>;
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
            meta::nothrow::convertible_to<V, type>
        ) {
            return std::forward<V>(e);
        }
    };
    using Error = _Error<E, Es...>::type;

    using storage = Union<Result, Error>;
    storage m_storage;

    template <typename Self>
    constexpr decltype(auto) get_value(this Self&& self) noexcept {
        return (std::forward<Self>(self).m_storage.m_storage.template get<0>());
    }

    template <typename Self>
    constexpr decltype(auto) get_error(this Self&& self) noexcept {
        return (std::forward<Self>(self).m_storage.m_storage.template get<1>());
    }

    template <size_t I, typename Self> requires (I < errors::size)
    constexpr decltype(auto) get_error(this Self&& self) noexcept {
        if constexpr (sizeof...(Es)) {
            return (std::forward<Self>(self).get_error().m_storage.template get<I>());
        } else {
            return (std::forward<Self>(self).get_error());
        }
    }

    template <typename U, typename Self> requires (errors::template contains<U>)
    constexpr decltype(auto) get_error(this Self&& self) noexcept {
        return (
            std::forward<Self>(self).template get_error<errors::template index<U>>()
        );
    }

    template <typename Self>
    using access = decltype((std::declval<Self>().get_value()));

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
                (
                    meta::invocable<F, access_type<E, Self>, Args...> ||
                    ... ||
                    meta::invocable<F, access_type<Es, Self>, Args...>
                ) &&
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
            template <meta::None V>
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
            requires ((
                meta::invocable<F, access_type<E, Self>, Args...> ||
                ... ||
                meta::invocable<F, access_type<Es, Self>, Args...>
            ))
        struct or_else_fn {
            template <meta::None V>
            static constexpr void operator()(auto&& func, V&&, auto&&... args) noexcept {}
            template <typename Err> requires (!meta::None<Err>)
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

    template <typename...>
    struct ConvertFrom {};
    template <typename From> requires (!meta::monad<From>)
    struct ConvertFrom<From> {
        using type = meta::remove_rvalue<value_type>;
        template <typename V>
        static constexpr storage operator()(V&& value)
            noexcept(meta::nothrow::convertible_to<V, storage>)
            requires(meta::convertible_to<V, type>)
        {
            return {std::forward<V>(value)};
        }
        template <typename V>
        static constexpr storage operator()(V&& value)
            noexcept(meta::nothrow::convertible_to<V, storage>)
            requires(
                !meta::convertible_to<V, type> &&
                (meta::inherits<V, E> || ... || meta::inherits<V, Es>)
            )
        {
            return {std::forward<V>(value)};
        }
    };

    struct ConstructFrom {
        using type = meta::remove_rvalue<value_type>;
        template <typename... A> requires (!meta::lvalue<type>)
        static constexpr storage operator()(A&&... args)
            noexcept(meta::nothrow::constructible_from<type, A...>)
            requires(meta::constructible_from<type, A...>)
        {
            return storage(std::forward<A>(args)...);
        }
    };

    template <typename U>
    struct ConvertTo {
        template <meta::convertible_to<U> V>
        static constexpr U operator()(V&& value)
            noexcept(meta::nothrow::convertible_to<V, U>)
        {
            return std::forward<V>(value);
        }
        template <typename V>
            requires (
                !meta::is<value_type, V> &&
                !meta::convertible_to<V, U> &&
                meta::convertible_to<std::unexpected<meta::unqualify<V>>, U>
            )
        static constexpr U operator()(V&& value) noexcept(
            meta::nothrow::convertible_to<std::unexpected<meta::unqualify<V>>, U>
        ) {
            return std::unexpected<meta::unqualify<V>>(std::forward<V>(value));
        }
    };

public:
    /* Default constructor.  Enabled iff the result type is default constructible or
    void. */
    [[nodiscard]] constexpr Expected()
        noexcept(meta::nothrow::default_constructible<Result>)
        requires(meta::default_constructible<Result>)
    {}

    /* Implicit conversion from references to expected references, bypassing
    visitors. */
    [[nodiscard]] constexpr Expected(Result v)
        noexcept(meta::nothrow::convertible_to<Result, storage>)
        requires(meta::lvalue<Result> && meta::convertible_to<Result, storage>)
    :
        m_storage(v)
    {}

    /* Converting constructor.  Implicitly converts the input to the value type if
    possible, otherwise accepts subclasses of the error states.  Also allows conversion
    from other visitable types whose alternatives all meet the conversion criteria. */
    template <typename V>
        requires (
            meta::exhaustive<ConvertFrom<V>, V> &&
            meta::consistent<ConvertFrom<V>, V>
        )
    [[nodiscard]] constexpr Expected(V&& v)
        noexcept(meta::nothrow::exhaustive<ConvertFrom<V>, V>)
    :
        m_storage(bertrand::visit(ConvertFrom<V>{}, std::forward<V>(v)))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the result type's
    constructor, and initializes the expected with the result. */
    template <typename... Args>
        requires (
            sizeof...(Args) > 0 &&
            !meta::exhaustive<ConvertFrom<Args...>, Args...> &&
            meta::exhaustive<ConstructFrom, Args...> &&
            meta::consistent<ConstructFrom, Args...>
        )
    [[nodiscard]] constexpr explicit Expected(Args&&... args)
        noexcept(meta::nothrow::exhaustive<ConstructFrom, Args...>)
    :
        m_storage(bertrand::visit(ConstructFrom{}, std::forward<Args>(args)...))
    {}

    /* Implicitly convert the `Expected` to any other type to which all alternatives
    can be converted.  If an error state is not directly convertible to the type, the
    algorithm will try again with the type wrapped in `std::unexpected` instead. */
    template <typename Self, typename V>
        requires (meta::exhaustive<ConvertTo<V>, Self>)
    [[nodiscard]] constexpr operator V(this Self&& self) noexcept(
        meta::nothrow::exhaustive<ConvertTo<V>, Self>
    ) {
        return bertrand::visit(ConvertTo<V>{}, std::forward<Self>(self));
    }

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
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return m_storage.index() == 0;
    }

    /* Access the valid state.  Throws a `BadUnionAccess` assertion if the expected is
    currently in the error state and the program is compiled in debug mode.  Fails to
    compile if the result type is void. */
    template <typename Self> requires (meta::not_void<value_type>)
    [[nodiscard]] constexpr access<Self> value(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_error()) {
                throw BadUnionAccess("Expected in error state has no result");
            }
        }
        return std::forward<Self>(self).get_value();
    }

    /* Access the stored value or return the default value if the expected is in an
    error state, converting to the common type between the wrapped type and the default
    value. */
    template <typename Self, typename V>
        requires (meta::not_void<value_type> && meta::has_common_type<access<Self>, V>)
    [[nodiscard]] constexpr meta::common_type<access<Self>, V> value_or(
        this Self&& self,
        V&& fallback
    ) noexcept(
        meta::nothrow::convertible_to<access<Self>, meta::common_type<access<Self>, V>> &&
        meta::nothrow::convertible_to<V, meta::common_type<access<Self>, V>>
    ) {
        if (self.has_value()) {
            return std::forward<Self>(self).get_value();
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
    template <size_t I> requires (I < errors::size)
    [[nodiscard]] constexpr bool has_error() const noexcept {
        if constexpr (sizeof...(Es)) {
            return has_error() && get_error().template holds_alternative<I>();
        } else {
            return has_error();
        }
    }

    /* True if the `Expected` is in a specific error state indicated by type.  False
    if it stores a valid result or an error other than the one indicated.  If only one
    error state is permitted, then this is identical to calling `has_error()` without
    any template parameters. */
    template <typename Err> requires (errors::template contains<Err>)
    [[nodiscard]] constexpr bool has_error() const noexcept {
        if constexpr (sizeof...(Es)) {
            return has_error() && get_error().template holds_alternative<Err>();
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
            if (self.has_value()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
        }
        return std::forward<Self>(self).get_error();
    }

    /* Access a particular error by index.  This is equivalent to the non-templated
    `error()` accessor in the single error case, and is a shorthand for
    `error().get<I>().value()` in the union case.  A `BadUnionAccess` exception will
    be thrown in debug mode if the expected is currently in the valid state, or if the
    indexed error is not the active member of the union. */
    template <size_t I, typename Self> requires (I < errors::size)
    [[nodiscard]] constexpr access_at<I, Self> error(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_value()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
            if constexpr (sizeof...(Es)) {
                if (!self.get_error().template holds_alternative<I>()) {
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
    `error().get<T>().value()` in the union case.  A `BadUnionAccess` exception will
    be thrown in debug mode if the expected is currently in the valid state, or if the
    specified error is not the active member of the union. */
    template <typename Err, typename Self> requires (errors::template contains<Err>)
    [[nodiscard]] constexpr access_type<Err, Self> error(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_value()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
            if constexpr (sizeof...(Es)) {
                if (!self.get_error().template holds_alternative<Err>()) {
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
    the single error case, and is a shorthand for `error().get<I>().value_or(fallback)`
    in the union case.  A `BadUnionAccess` exception will be thrown in debug mode if
    the indexed error is not the active member of the union. */
    template <size_t I, typename Self, typename V>
        requires (I < errors::size && meta::has_common_type<access_at<I, Self>, V>)
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
                if (!self.get_error().template holds_alternative<I>()) {
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
    the single error case, and is a shorthand for `error().get<I>().value_or(fallback)`
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
                if (!self.get_error().template holds_alternative<Err>()) {
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
    be invoked with an argument of type `bertrand::NoneType` (aka `std::nullopt_t`)
    instead.  Otherwise, it will be invoked with the normal output from `.value()`. */
    template <typename F, typename Self, typename... Args>
        requires (meta::visit<F, Self, Args...>)
    constexpr decltype(auto) visit(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visit<F, Self, Args...>
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
            meta::visit<and_then_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) and_then(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visit<and_then_fn<F, Self, Args...>, F, Self, Args...>
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
            meta::visit<and_then_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) and_then(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visit<and_then_fn<F, Self, Args...>, F, Self, Args...>
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
            (
                meta::invocable<F, access_type<E, Self>, Args...> ||
                ... ||
                meta::invocable<F, access_type<Es, Self>, Args...>
            ) &&
            meta::visit<or_else_fn<F, Self, Args...>, F, Self, Args...>
        )
    constexpr decltype(auto) or_else(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::visit<or_else_fn<F, Self, Args...>, F, Self, Args...>
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
        requires (meta::visit<impl::Call, Self, Args...>)
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args) noexcept(
        meta::nothrow::visit<impl::Call, Self, Args...>
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
        requires (meta::visit<impl::Subscript, Self, Key...>)
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys) noexcept(
        meta::nothrow::visit<impl::Subscript, Self, Key...>
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
constexpr void swap(Union<Ts...>& a, Union<Ts...>& b)
    noexcept(noexcept(a.swap(b)))
    requires(requires{a.swap(b);})
{
    a.swap(b);
}


/* ADL swap() operator for `bertrand::Optional<T>`.  Equivalent to calling `a.swap(b)`
as a member method. */
template <typename T>
constexpr void swap(Optional<T>& a, Optional<T>& b)
    noexcept(noexcept(a.swap(b)))
    requires(requires{a.swap(b);})
{
    a.swap(b);
}


/* ADL swap() operator for `bertrand::Expected<T, E>`.  Equivalent to calling
`a.swap(b)` as a member method. */
template <typename T, typename E>
constexpr void swap(Expected<T, E>& a, Expected<T, E>& b)
    noexcept(noexcept(a.swap(b)))
    requires(requires{a.swap(b);})
{
    a.swap(b);
}


/* Monadic logical NOT operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports logical NOT. */
template <meta::monad T> requires (meta::visit<impl::LogicalNot, T>)
constexpr decltype(auto) operator!(T&& val) noexcept(
    meta::nothrow::visit<impl::LogicalNot, T>
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
        meta::visit<impl::LogicalAnd, L, R>
    )
constexpr decltype(auto) operator&&(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::LogicalAnd, L, R>
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
        meta::visit<impl::LogicalOr, L, R>
    )
constexpr decltype(auto) operator||(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::LogicalOr, L, R>
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
        meta::visit<impl::Less, L, R>
    )
constexpr decltype(auto) operator<(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Less, L, R>
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
        meta::visit<impl::LessEqual, L, R>
    )
constexpr decltype(auto) operator<=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::LessEqual, L, R>
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
        meta::visit<impl::Equal, L, R>
    )
constexpr decltype(auto) operator==(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Equal, L, R>
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
        meta::visit<impl::NotEqual, L, R>
    )
constexpr decltype(auto) operator!=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::NotEqual, L, R>
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
        meta::visit<impl::GreaterEqual, L, R>
    )
constexpr decltype(auto) operator>=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::GreaterEqual, L, R>
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
        meta::visit<impl::Greater, L, R>
    )
constexpr decltype(auto) operator>(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Greater, L, R>
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
        meta::visit<impl::Spaceship, L, R>
    )
constexpr decltype(auto) operator<=>(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Spaceship, L, R>
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
template <meta::monad T> requires (meta::visit<impl::Pos, T>)
constexpr decltype(auto) operator+(T&& val) noexcept(
    meta::nothrow::visit<impl::Pos, T>
) {
    return (visit(impl::Pos{}, std::forward<T>(val)));
}


/* Monadic unary minus operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports unary minus. */
template <meta::monad T> requires (meta::visit<impl::Neg, T>)
constexpr decltype(auto) operator-(T&& val) noexcept(
    meta::nothrow::visit<impl::Neg, T>
) {
    return (visit(impl::Neg{}, std::forward<T>(val)));
}


/* Monadic prefix increment operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one tyoe within the monad supports prefix increments. */
template <meta::monad T> requires (meta::visit<impl::PreIncrement, T>)
constexpr decltype(auto) operator++(T&& val) noexcept(
    meta::nothrow::visit<impl::PreIncrement, T>
) {
    return (visit(impl::PreIncrement{}, std::forward<T>(val)));
}


/* Monadic postfix increment operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports postfix increments. */
template <meta::monad T> requires (meta::visit<impl::PostIncrement, T>)
constexpr decltype(auto) operator++(T&& val, int) noexcept(
    meta::nothrow::visit<impl::PostIncrement, T>
) {
    return (visit(impl::PostIncrement{}, std::forward<T>(val)));
}


/* Monadic prefix decrement operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports prefix decrements. */
template <meta::monad T> requires (meta::visit<impl::PreDecrement, T>)
constexpr decltype(auto) operator--(T&& val) noexcept(
    meta::nothrow::visit<impl::PreDecrement, T>
) {
    return (visit(impl::PreDecrement{}, std::forward<T>(val)));
}

/* Monadic postfix decrement operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports postfix decrements. */
template <meta::monad T> requires (meta::visit<impl::PostDecrement, T>)
constexpr decltype(auto) operator--(T&& val, int) noexcept(
    meta::nothrow::visit<impl::PostDecrement, T>
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
        meta::visit<impl::Add, L, R>
    )
constexpr decltype(auto) operator+(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Add, L, R>
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
        meta::visit<impl::InplaceAdd, L, R>
    )
constexpr decltype(auto) operator+=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceAdd, L, R>
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
        meta::visit<impl::Subtract, L, R>
    )
constexpr decltype(auto) operator-(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Subtract, L, R>
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
        meta::visit<impl::InplaceSubtract, L, R>
    )
constexpr decltype(auto) operator-=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceSubtract, L, R>
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
        meta::visit<impl::Multiply, L, R>
    )
constexpr decltype(auto) operator*(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Multiply, L, R>
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
        meta::visit<impl::InplaceMultiply, L, R>
    )
constexpr decltype(auto) operator*=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceMultiply, L, R>
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
        meta::visit<impl::Divide, L, R>
    )
constexpr decltype(auto) operator/(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Divide, L, R>
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
        meta::visit<impl::InplaceDivide, L, R>
    )
constexpr decltype(auto) operator/=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceDivide, L, R>
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
        meta::visit<impl::Modulus, L, R>
    )
constexpr decltype(auto) operator%(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::Modulus, L, R>
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
        meta::visit<impl::InplaceModulus, L, R>
    )
constexpr decltype(auto) operator%=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceModulus, L, R>
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
        meta::visit<impl::LeftShift, L, R>
    )
constexpr decltype(auto) operator<<(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::LeftShift, L, R>
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
        meta::visit<impl::InplaceLeftShift, L, R>
    )
constexpr decltype(auto) operator<<=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceLeftShift, L, R>
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
        meta::visit<impl::RightShift, L, R>
    )
constexpr decltype(auto) operator>>(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::RightShift, L, R>
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
        meta::visit<impl::InplaceRightShift, L, R>
    )
constexpr decltype(auto) operator>>=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceRightShift, L, R>
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
template <meta::monad T> requires (meta::visit<impl::BitwiseNot, T>)
constexpr decltype(auto) operator~(T&& val) noexcept(
    meta::nothrow::visit<impl::BitwiseNot, T>
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
        meta::visit<impl::BitwiseAnd, L, R>
    )
constexpr decltype(auto) operator&(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::BitwiseAnd, L, R>
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
        meta::visit<impl::InplaceBitwiseAnd, L, R>
    )
constexpr decltype(auto) operator&=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceBitwiseAnd, L, R>
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
        meta::visit<impl::BitwiseOr, L, R>
    )
constexpr decltype(auto) operator|(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::BitwiseOr, L, R>
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
        meta::visit<impl::InplaceBitwiseOr, L, R>
    )
constexpr decltype(auto) operator|=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceBitwiseOr, L, R>
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
        meta::visit<impl::BitwiseXor, L, R>
    )
constexpr decltype(auto) operator^(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::BitwiseXor, L, R>
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
        meta::visit<impl::InplaceBitwiseXor, L, R>
    )
constexpr decltype(auto) operator^=(L&& lhs, R&& rhs) noexcept(
    meta::nothrow::visit<impl::InplaceBitwiseXor, L, R>
) {
    return (visit(
        impl::InplaceBitwiseXor{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


}


namespace std {

    template <bertrand::meta::monad T>
        requires (bertrand::meta::visit<bertrand::impl::Hash, T>)
    struct hash<T> {
        static constexpr auto operator()(auto&& value) noexcept(
            bertrand::meta::nothrow::visit<bertrand::impl::Hash, T>
        ) {
            return bertrand::visit(
                bertrand::impl::Hash{},
                std::forward<decltype(value)>(value)
            );
        }
    };

    template <typename... Ts>
    struct variant_size<bertrand::Union<Ts...>> :
        std::integral_constant<size_t, bertrand::Union<Ts...>::types::size>
    {};

    template <size_t I, typename... Ts>
        requires (I < variant_size<bertrand::Union<Ts...>>::value)
    struct variant_alternative<I, bertrand::Union<Ts...>> {
        using type = remove_reference_t<
            typename bertrand::Union<Ts...>::types::template at<I>
        >;
    };

    /* Non-member `std::get<I>(union)` returns a reference (possibly rvalue) and throws
    if the index is invalid, similar to `std::get<I>(variant)` and different from
    `union.get<I>()`, which returns an expected according to the monadic interface. */
    template <size_t I, bertrand::meta::Union U> requires (I < variant_size<U>::value)
    constexpr decltype(auto) get(U&& u) {
        auto result = std::forward<U>(u).template get<I>();
        if (result.has_error()) {
            throw std::move(result).error();
        }
        return (std::move(result).value());
    }

    /* Non-member `std::get<T>(union)` returns a reference (possibly rvalue) and throws
    if the type is not active, similar to `std::get<T>(variant)` and different from
    `union.get<T>()`, which returns an expected according to the monadic interface. */
    template <typename T, bertrand::meta::Union U>
        requires (remove_cvref_t<U>::types::template contains<T>)
    constexpr decltype(auto) get(U&& u) {
        auto result = std::forward<U>(u).template get<T>();
        if (result.has_error()) {
            throw std::move(result).error();
        }
        return (std::move(result).value());
    }

    /* Non-member `std::get_if<I>(union)` returns a pointer to the value if the index
    is valid, or `nullptr` otherwise, similar to `std::get_if<I>(variant)`, and
    different from `union.get_if<I>()`, which returns an optional according to the
    monadic interface. */
    template <size_t I, bertrand::meta::Union U> requires (I < variant_size<U>::value)
    constexpr auto* get_if(U&& u) noexcept(noexcept(u.template get_if<I>())) {
        auto result = u.template get_if<I>();  // as lvalue
        return result.has_value() ? &result.value() : nullptr;
    }

    /* Non-member `std::get_if<T>(union)` returns a pointer to the value if the type is
    active, or `nullptr` otherwise, similar to `std::get_if<T>(variant)`,
    and different from `union.get_if<T>()`, which returns an optional according to the
    monadic interface. */
    template <typename T, bertrand::meta::Union U>
        requires (remove_cvref_t<U>::types::template contains<T>)
    constexpr auto* get_if(U&& u) noexcept(noexcept(u.template get_if<T>())) {
        auto result = u.template get_if<T>();  // as lvalue
        return result.has_value() ? &result.value() : nullptr;
    }

}


#endif  // BERTRAND_UNION_H
