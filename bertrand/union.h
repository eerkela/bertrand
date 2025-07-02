#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include <cstddef>
#include <iterator>


namespace bertrand {


/* Unions emit internal vtables when provided as inputs to `def` visitors and the
`impl::visit()` function, whose sizes are equal to the cross product of all possible
alternatives.  In order to optimize performance and reduce code bloat, these vtables
are only emitted when the cross product exceeds a certain threshold, as controlled by
an equivalent compilation flag.  Profile Guided Optimization (PGO) may be used to
optimally select this constant for a given architecture, with a hardware-dependent
sweet spot for each platform.  Note that since `impl::visit()` is used internally for
all monadic operators, cross products that fit within this threshold will generally
run faster, with more predictable performance characteristics compared to vtable-based
alternatives. */
#ifdef BERTRAND_MIN_VTABLE_SIZE
    inline constexpr size_t MIN_VTABLE_SIZE = BERTRAND_MIN_VTABLE_SIZE;
#else
    inline constexpr size_t MIN_VTABLE_SIZE = 5;
#endif


namespace impl {
    struct union_tag {};
    struct optional_tag {};
    struct expected_tag {};

    /* Provides an extensible mechanism for controlling the dispatching behavior of
    the `meta::visitor` concept and `impl::visit()` operator, including return
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
    concept unqualified_exception = unqualified<T> && Exception<T>;

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
    5.  `visit()` is implemented according to `impl::visit()`, which has much
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
template <meta::not_void... Ts> requires (sizeof...(Ts) > 1 && meta::unique<Ts...>)
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
template <typename T, meta::unqualified_exception E = Exception, meta::unqualified_exception... Es>
    requires (meta::unique<T, E, Es...>)
struct Expected;


/// TODO: the visitation internals (especially the impl::visitable hooks) might be
/// refactored to optimize compilation speed.


namespace meta {

    /// TODO: meta::visit_size tells the user the size of the vtable that will be
    /// emitted for a hypothetical visitor and arguments.

    /* Scaling is needed to uniquely encode the index sequence of all possible
    permutations for a given set of union types. */
    template <typename... As>
    static constexpr size_t visit_size = (impl::visitable<As>::alternatives::size() * ... * 1);

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
                template <typename... As> requires (meta::callable<F, As...>)
                struct invoke<As...> {
                    template <typename... Rs>
                    struct helper { using type = meta::pack<Rs...>; };
                    template <typename... Rs>
                        requires (!::std::same_as<meta::call_type<F, As...>, Rs> && ...)
                    struct helper<Rs...> {
                        using type = meta::pack<Rs..., meta::call_type<F, As...>>;
                    };
                    static constexpr bool enable = true;
                    static constexpr bool nothrow = meta::nothrow::callable<F, As...>;
                    template <typename... Rs>
                    using type = helper<Rs...>::type;
                    static constexpr bool has_void = false;
                };
                template <typename... As> requires (meta::call_returns<void, F, As...>)
                struct invoke<As...> {
                    static constexpr bool enable = true;
                    static constexpr bool nothrow = meta::nothrow::callable<F, As...>;
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
                            impl::visitable<A>::errors::template contains<meta::unqualify<alt>>();
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
                permutations::size() == valid_permutations::size();

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
            template <meta::callable F2>
            struct _type<F2> { using type = meta::call_type<F2>; };

        public:
            static constexpr bool enable = meta::callable<F>;
            static constexpr bool exhaustive = enable;
            static constexpr bool consistent = enable;
            static constexpr bool nothrow = meta::nothrow::callable<F>;
            template <typename R>
            static constexpr bool returns = meta::call_returns<R, F>;
            template <typename R>
            static constexpr bool nothrow_returns = meta::nothrow::call_returns<R, F>;
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

    /// TODO: apply() no longer exists, and is completely replaced by def() functions
    /// and Python-style unpacking operators.

    namespace detail {

        template <typename...>
        struct apply {
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
            using type = void;
        };
        template <typename F, typename... out> requires (meta::callable<F, out...>)
        struct apply<F, meta::pack<out...>> {
            static constexpr bool enable = true;
            static constexpr bool nothrow = meta::nothrow::callable<F, out...>;
            using type = meta::call_type<F, out...>;
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
            template <size_t... prev, size_t... next, meta::is<F> G, typename... A>
            static constexpr decltype(auto) operator()(
                ::std::index_sequence<prev...>,
                ::std::index_sequence<next...>,
                G&& func,
                A&&... args
            )
                noexcept(meta::nothrow::callable<G, A...>)
                requires(meta::callable<G, A...>)
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
            template <size_t... prev, size_t... next, meta::is<F> G, typename... A>
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
            template <size_t... prev, size_t... next, meta::is<F> G, typename... A>
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
            template <size_t... prev, size_t... next, meta::is<F> G, typename... A>
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
            template <size_t... prev, size_t... next, meta::is<F> G, typename... A>
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

    namespace detail {

        template <typename... Ts>
        constexpr bool exact_size<bertrand::Union<Ts...>> = (exact_size<Ts> && ...);

        template <typename T>
        constexpr bool exact_size<bertrand::Optional<T>> = exact_size<T>;

        template <typename T, typename... Es>
        constexpr bool exact_size<bertrand::Expected<T, Es...>> = exact_size<T>;

    }

}


namespace impl {

    /* Helper function to decode a vtable index for the current level of recursion.
    This is exactly equivalent to dividing the index by the appropriate visit scale
    for the subsequent in `A...`, which is equivalent to the size of their cartesian
    product. */
    template <size_t idx, typename... A, size_t... Prev, size_t... Next>
    constexpr size_t visit_index(
        std::index_sequence<Prev...>,
        std::index_sequence<Next...>
    ) noexcept {
        return idx / meta::visit_size<meta::unpack_type<sizeof...(Prev) + 1 + Next, A...>...>;
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
            if constexpr (meta::is_void<meta::call_type<
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
            >::template dispatch<R, idx % meta::visit_size<
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

    /// TODO: maybe visitable specializations can come after all the union internals,
    /// so that they can benefit from them?

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
        template <size_t I, meta::is<T> U> requires (I < alternatives::size())
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
                visit_index<idx, A...>(prev, next) < alternatives::size() &&
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
        static constexpr size_t N = meta::unqualify<T>::types::size();

        template <size_t I, typename... Ts>
        struct _pack {
            using type = _pack<
                I + 1,
                Ts...,
                decltype((std::declval<T>().storage.template value<I>()))
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
            return u.storage.index;
        }

        /* Perfectly forward the member at index I for a union of this type. */
        template <size_t I, meta::is<T> U> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).storage.template value<I>())
        ) {
            return (std::forward<U>(u).storage.template value<I>());
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
                visit_index<idx, A...>(prev, next) < alternatives::size() &&
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
        template <size_t I, meta::is<T> U> requires (I < alternatives::size())
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
                visit_index<idx, A...>(prev, next) < alternatives::size() &&
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
        using wrapped = decltype((std::declval<T>().storage.value()));
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
        template <size_t I, meta::is<T> U> requires (I == 0 && I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).storage.value())
        ) {
            return (std::forward<U>(u).storage.value());
        }

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < alternatives::size())
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
                visit_index<idx, A...>(prev, next) < alternatives::size() &&
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
        template <size_t I, meta::is<T> U> requires (I == 0 && I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).value())
        ) {
            return (std::forward<U>(u).value());
        }

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < alternatives::size())
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
                visit_index<idx, A...>(prev, next) < alternatives::size() &&
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
            using type = decltype((std::declval<T>().storage.value()));
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
            using type = meta::pack<
                decltype((std::declval<T>().storage.value())),
                Errs...
            >;
        };
        template <size_t I, typename... Errs>
            requires (
                meta::is_void<typename meta::unqualify<T>::value_type> &&
                I == meta::unqualify<T>::errors::size()
            )
        struct _pack<I, Errs...> {
            using type = meta::pack<NoneType, Errs...>;
        };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using wrapped = _wrapped<typename meta::unqualify<T>::value_type>::type;
        using alternatives = _pack<0>::type;  // TODO: rename these to alternatives
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
                return u.has_value() ? 0 : u.get_error().index() + 1;
            } else {
                return u.has_error();
            }
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I == 0 && meta::not_void<wrapped>)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u).storage.value())
        ) {
            return (std::forward<U>(u).storage.value());
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I == 0 && meta::is_void<wrapped>)
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (None);
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < alternatives::size())
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
                visit_index<idx, A...>(prev, next) < alternatives::size() &&
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
                I == 0 && I < alternatives::size()
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
                I == 0 && I < alternatives::size()
            )
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept {
            return (None);
        }

        /* Perfectly forward the member at index I for an expected of this type. */
        template <size_t I, meta::is<T> U> requires (I > 0 && I < alternatives::size())
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
            noexcept (meta::nothrow::visit<F, A...>)
            requires (
                sizeof...(Prev) < sizeof...(A) &&
                meta::is<T, meta::unpack_type<sizeof...(Prev), A...>> &&
                visit_index<idx, A...>(prev, next) < alternatives::size() &&
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
        return vtable_index(i + (visitable<A>::index(a) * meta::visit_size<As...>), as...);
    }

    /* Invoke a function with the given arguments, unwrapping any sum types in the
    process.  This is similar to `std::visit()`, but with greatly expanded
    metaprogramming capabilities.

    The visitor is constructed from either a single function or a set of functions
    arranged into an overload set.  The subsequent arguments will be passed to the
    visitor in the order they are defined, with each union being unwrapped to its
    actual type within the visitor context.  A compilation error will occur if the
    visitor is not callable with all nontrivial permutations of the unwrapped values.

    Note that the visitor does not need to be exhaustive over all possible
    permutations; only the valid ones.  Practically speaking, this means that the
    visitor is free to ignore the empty states of optionals and error states of
    expected inputs, which will be implicitly propagated to the return type if left
    unhandled.  On the other hand, union states and non-empty/result states of
    optionals and expecteds must be explicitly handled, else a compilation error will
    occur.  This equates to adding implicit overloads to the visitor that trivially
    forward these states without any modifications, allowing visitors to treat optional
    and expected types as if they were always in the valid state.  The
    `bertrand::meta::exhaustive<F, Args...>` concept can be used to selectively forbid
    these cases at compile time, which returns the semantics to those of `std::visit()`
    with respect to visitor exhaustiveness.

    Similarly, unlike `std::visit()`, the visitor is not constrained to return a single
    consistent type for all permutations.  If it does not, then the return type `R`
    will deduce to `bertrand::Union<Rs...>`, where `Rs...` are the unique return types
    for all valid permutations.  Otherwise, if all permutations return the same type,
    then `R` will simply be that type.  If some permutations return `void` and others
    return non-`void`, then `R` will be wrapped in an `Optional`, where the empty state
    indicates a void return type at runtime.  If `R` is itself an optional type, then
    the empty states will be merged so as to avoid nested optionals.  Similar to above,
    the `bertrand::meta::consistent<F, Args...>` concept can be used to selectively
    forbid these cases at compile time.  Applying that concept in combination with
    `bertrand::meta::exhaustive<F, Args...>` will yield the same visitor semantics as
    `std::visit()`.

    If both of the above features are enabled (the default), then a worst-case,
    inconsistent visitor applied to an `Expected<Union<Ts...>, Es...>` input, where
    some permutations of `Ts...` return void and others do not, can potentially yield a
    return type of the form `Expected<Optional<Union<Rs...>>, Es...>`, where `Rs...`
    are the non-void return types for the valid permutations, `Optional` signifies that
    the return type may be void, and `Es...` are the original error types that were
    implicitly propagated from the input.  If the visitor exhaustively handles the
    error states, then the return type will reduce to `Optional<Union<Rs...>>`.  If the
    visitor never returns void, then the return type will further reduce to
    `Union<Rs...>`, and if it returns a single consistent type, then `R` will collapse
    to just that type, in which case the semantics are identical to `std::visit()`.

    Finally, note that the arguments are fully generic, and not strictly limited to
    union types, in contrast to `std::visit()`.  If no unions are present, then
    `visit()` devolves to invoking the visitor normally, without any special handling.
    Otherwise, the component unions are expanded according to the rules laid out in
    `bertrand::impl::visitable`, which describes how to register custom visitable types
    for use with this function.  Built-in specializations exist for `Union`,
    `Optional`, and `Expected`, as well as `std::variant`, `std::optional`, and
    `std::expected`, each of which can be passed to `visit()` according to the
    described semantics. */
    template <typename F, typename... Args>
    constexpr meta::visit_type<F, Args...> visit(F&& f, Args&&... args)
        noexcept (meta::nothrow::visit<F, Args...>)
        requires (meta::visit<F, Args...>)
    {
        // only generate a vtable if unions are present in the signature
        if constexpr ((meta::visitable<Args> || ...)) {
            using R = meta::visit_type<F, Args...>;
            using T = meta::unpack_type<0, Args...>;

            /// TODO: this internal vtable can be lifted out into the impl:: namespace
            /// to reduce binary bloat.

            // flat vtable encodes all permutations simultaneously, minimizing binary size
            // and compile-time/runtime overhead by only doing a single array lookup.
            static constexpr auto vtable =
                []<size_t... Is>(std::index_sequence<Is...>) noexcept {
                    return std::array{
                        +[](meta::as_lvalue<F> f, meta::as_lvalue<Args>... args)
                            noexcept (meta::nothrow::visit<F, Args...>) -> R
                        {
                            return impl::visitable<T>::template dispatch<R, Is>(
                                std::make_index_sequence<0>{},
                                std::make_index_sequence<sizeof...(Args) - 1>{},
                                std::forward<F>(f),
                                std::forward<Args>(args)...
                            );
                        }...
                    };
                }(std::make_index_sequence<meta::visit_size<Args...>>{});

            // `impl::vtable_index()` produces an index into the flat vtable with the
            // appropriate scaling.
            return vtable[impl::vtable_index(0, args...)](f, args...);

        // otherwise, call the function directly
        } else {
            return std::forward<F>(f)(std::forward<Args>(args)...);
        }
    }

    /// TODO: probably just delete apply().  The unpack() operator is much more
    /// expressive, in combination with def() functions.

    /* Invoke a function with the given arguments, unpacking any product types in the
    process.  This is similar to `std::apply()`, but permits any number of tuples as
    arguments, including none, whereby it devolves to a simple function call.

    Like `impl::visit()`, the visitor is constructed from either a single function
    or a set of functions arranged into an overload set.  The subsequent arguments will
    be passed to the visitor in the order they are defined, with any type that defines
    `std::tuple_size<T>` and `std::get<I>()` being unpacked into its individual
    elements, which are then passed as separate, consecutive arguments.  A compilation
    error will occur if the visitor is not callable with the unpacked arguments.

    In the same way that structured bindings can be used to unpack tuples into their
    constituent elements, `impl::apply()` unpacks them as arguments to a function.
    For example:

        ```
        std::pair<int, std::string> p{2, "hello"};
        assert(apply(
            [](int i, const std::string& s, double d) { return i + s.size() + d; },
            p,
            3.25
        ) == 10.25);
        ```

    Note that `impl::apply()` and `impl::visit()` operate very similarly, but are not
    interchangeable, and one does not imply the other.  This equates to the difference
    between sum types and product types in an algebraic type system.  In particular,
    sum types consist of a collection of types joined by a logical OR (`A` OR `B` OR
    `C`), whereas product types represent the logical AND of those same types (`A` AND
    `B` AND `C`).  `impl::visit()` is used to unwrap the former, while `impl::apply()`
    handles the latter. */
    template <typename F, typename... Ts>
    constexpr decltype(auto) apply(F&& func, Ts&&... args)
        noexcept (meta::nothrow::apply<F, Ts...>)
        requires (meta::apply<F, Ts...>)
    {
        return (meta::detail::do_apply<0, F, Ts...>{}(
            std::make_index_sequence<0>{},
            std::make_index_sequence<sizeof...(Ts) - (sizeof...(Ts) > 0)>{},
            std::forward<F>(func),
            std::forward<Ts>(args)...
        ));
    }

    /* The tracking index stored within a `Union` is defined as the smallest unsigned
    integer type big enough to hold all alternatives, in order to exploit favorable
    packing dynamics with respect to the aligned contents. */
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
    template <size_t N> requires (N <= std::numeric_limits<size_t>::max())
    using union_index_type = _union_index_type<N>::type;

    /* A tag class used to select a particular alternative during initialization of a
    `union_storage` buffer. */
    template <size_t I>
    struct union_select {};

    /* Accessing the wrong index of a union yields a standardized error message from a
    centralized vtable, to limit binary bloat. */
    template <size_t I>
    struct union_index_error {
    private:
        using ptr = BadUnionAccess(*)() noexcept;

        template <size_t J>
        static constexpr BadUnionAccess fn() noexcept {
            static constexpr static_str msg =
                impl::int_to_static_string<I> + " is not the active type in the union "
                "(active is " + impl::int_to_static_string<J> + ")";
            return BadUnionAccess(msg);
        }

        template <typename>
        static constexpr ptr tbl[0] {};
        template <size_t... Is>
        static constexpr ptr tbl<std::index_sequence<Is...>>[sizeof...(Is)] {&fn<Is>... };


        template <size_t J, size_t max>
        [[gnu::always_inline]] static constexpr BadUnionAccess recur(size_t index) noexcept {
            if constexpr ((J + 1) < max) {
                if (index == J) {
                    return fn<J>();
                } else {
                    return recur<J + 1>(index);
                }
            } else {
                return fn<J>();
            }
        }

    public:
        template <typename... Ts> requires (sizeof...(Ts) > 0)
        static constexpr BadUnionAccess error(size_t index) noexcept {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return tbl<std::index_sequence_for<Ts...>>[index]();
            } else {
                return recur<0, sizeof...(Ts)>(index);
            }
        }
    };

    /* Accessing the wrong type of a union yields a standardized error message from a
    centralized vtable, to limit binary bloat. */
    template <typename T>
    struct union_type_error {
    private:
        using ptr = BadUnionAccess(*)() noexcept;

        template <typename U>
        static constexpr BadUnionAccess fn() noexcept {
            static constexpr static_str msg =
                "'" + demangle<T>() + "' is not the active type in the union "
                "(active is '" + demangle<U>() + "')";
            return BadUnionAccess(msg);
        }

        template <typename... Ts>
        static constexpr ptr tbl[sizeof...(Ts)] { &fn<T, Ts>... };

        template <size_t J, typename U, typename... Us>
        [[gnu::always_inline]] static constexpr BadUnionAccess recur(size_t index) noexcept {
            if constexpr (sizeof...(Us) > 0) {
                if (index == J) {
                    return fn<U>();
                } else {
                    return recur<J + 1, Us...>(index);
                }
            } else {
                return fn<U>();
            }
        }

    public:
        template <typename... Ts> requires (sizeof...(Ts) > 0)
        static constexpr BadUnionAccess error(size_t index) noexcept {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return tbl<Ts...>[index]();
            } else {
                return recur<0, Ts...>(index);
            }
        }
    };

    /* Determine the common type for the members of the union, if one exists. */
    template <typename Self, typename = std::index_sequence<meta::unqualify<Self>::types::size()>>
    struct union_common_type {
        using type = void;
        static constexpr bool nothrow = true;
    };
    template <typename Self, size_t... Is>
        requires (meta::has_common_type<decltype(std::declval<Self>().template value<Is>())...>)
    struct union_common_type<Self, std::index_sequence<Is...>> {
        using type = meta::common_type<decltype(std::declval<Self>().template value<Is>())...>;
        static constexpr bool nothrow = (meta::nothrow::convertible_to<
            decltype(std::declval<Self>().template value<Is>()),
            type
        > && ...);
    };

    /* Find the first type in Ts... that is default constructible (void if none) */
    template <typename...>
    struct _union_default_type { using type = void; };
    template <meta::default_constructible T, typename... Ts>
    struct _union_default_type<T, Ts...> { using type = T; };
    template <typename T, typename... Ts>
    struct _union_default_type<T, Ts...> : _union_default_type<Ts...> {};
    template <typename... Ts>
    using union_default_type = _union_default_type<Ts...>::type;

    /// TODO: maybe I add another restriction that says that union_storage cannot be
    /// instantiated with rvalue references as `Ts`.  The outer types can do this, but
    /// the rvalue is always stripped before instantiating the union_storage internal
    /// type.  This would probably simplify the implementation.

    /* A basic tagged union of alternatives `Ts...`, which automatically forwards
    lvalue references and provides vtable-based copy, move, swap, and destruction
    operators, as well as a `value()` accessor that walks the overall structure.
    This is a fundamental building block for sum types, which can dramatically reduce
    the amount of bookkeeping necessary to safely work with raw C unions. */
    template <meta::not_void... Ts> requires (sizeof...(Ts) > 1)
    struct union_storage {
        using indices = std::index_sequence_for<Ts...>;
        using types = meta::pack<Ts...>;
        using default_type = impl::union_default_type<Ts...>;

        template <size_t I> requires (I < sizeof...(Ts))
        using tag = impl::union_select<I>;

    private:
        /// NOTE: A recursive C union that stores each alternative as efficiently as
        /// possible.  This class is essentially a wrapper around such a union that
        /// makes it provably safe for the compiler to use without UB, including in
        /// constexpr contexts.

        /// TODO: it might be possible to omit the constexpr destructors, but I won't be
        /// able to tell until I can construct and test unions to verify that.

        template <typename... Us>
        union _type { constexpr ~_type() noexcept {}; };
        template <typename U, typename... Us> requires (!meta::lvalue<U>)
        union _type<U, Us...> {
            [[no_unique_address]] meta::remove_rvalue<U> curr;
            [[no_unique_address]] _type<Us...> rest;

            constexpr _type() noexcept {}
            constexpr ~_type() noexcept {}

            template <typename... A>
            constexpr _type(tag<0>, A&&... args)
                noexcept (meta::nothrow::constructible_from<meta::remove_rvalue<U>, A...>)
                requires (meta::constructible_from<meta::remove_rvalue<U>, A...>)
            :
                curr(std::forward<A>(args)...)
            {}

            template <size_t I, typename... A> requires (I > 0)
            constexpr _type(tag<I>, A&&... args)
                noexcept (meta::nothrow::constructible_from<_type<Us...>, tag<I - 1>, A...>)
                requires (meta::constructible_from<_type<Us...>, tag<I - 1>, A...>)
            :
                rest(tag<I - 1>{}, std::forward<A>(args)...)  // recur
            {}

            template <size_t I, typename Self>
            constexpr decltype(auto) value(this Self&& self) noexcept {
                if constexpr (I == 0) {
                    return (std::forward<Self>(self).curr);
                } else {
                    return (std::forward<Self>(self).rest.template value<I - 1>());
                }
            }

            template <size_t I, typename... A> requires (I == 0)
            constexpr void construct(A&&... args)
                noexcept (requires{
                    {std::construct_at(&curr, std::forward<A>(args)...)} noexcept;
                })
                requires (requires{
                    {std::construct_at(&curr, std::forward<A>(args)...)};
                })
            {
                std::construct_at(&curr, std::forward<A>(args)...);
            }

            template <size_t I, typename... A> requires (I > 0)
            constexpr void construct(A&&... args)
                noexcept (requires{
                    {rest.template construct<I - 1>(std::forward<A>(args)...)} noexcept;
                })
                requires (requires{
                    {rest.template construct<I - 1>(std::forward<A>(args)...)};
                })
            {
                rest.template construct<I - 1>(std::forward<A>(args)...);
            }

            template <size_t I> requires (I == 0)
            constexpr void destroy()
                noexcept (
                    !meta::trivially_destructible<meta::remove_rvalue<U>> ||
                    requires{{std::destroy_at(&curr)} noexcept;}
                )
                requires (
                    !meta::trivially_destructible<meta::remove_rvalue<U>> ||
                    requires{{std::destroy_at(&curr)};}
                )
            {
                if constexpr (!meta::trivially_destructible<meta::remove_rvalue<U>>) {
                    std::destroy_at(&curr);
                }
            }

            template <size_t I> requires (I > 0)
            constexpr void destroy()
                noexcept (requires{{rest.template destroy<I - 1>()} noexcept;})
                requires (requires{{rest.template destroy<I - 1>()};})
            {
                rest.template destroy<I - 1>();
            }
        };
        template <meta::lvalue U, typename... Us>
        union _type<U, Us...> {
            [[no_unique_address]] struct { U ref; } curr;
            [[no_unique_address]] _type<Us...> rest;

            constexpr _type() noexcept {}
            constexpr ~_type() noexcept {}

            constexpr _type(tag<0>, U value) noexcept : curr(value) {}

            template <size_t I, typename... A> requires (I > 0)
            constexpr _type(tag<I>, A&&... args)
                noexcept (meta::nothrow::constructible_from<_type<Us...>, tag<I - 1>, A...>)
                requires (meta::constructible_from<_type<Us...>, tag<I - 1>, A...>)
            :
                rest(tag<I - 1>{}, std::forward<A>(args)...)  // recur
            {}

            template <size_t I, typename Self>
            constexpr decltype(auto) value(this Self&& self) noexcept {
                if constexpr (I == 0) {
                    return (std::forward<Self>(self).curr.ref);
                } else {
                    return (std::forward<Self>(self).rest.template value<I - 1>());
                }
            }

            template <size_t I> requires (I == 0)
            constexpr void construct(U other) noexcept {
                std::construct_at(&curr, other.curr.ref);
            }

            template <size_t I, typename... A> requires (I > 0)
            constexpr void construct(A&&... args)
                noexcept (requires{
                    {rest.template construct<I - 1>(std::forward<A>(args)...)} noexcept;
                })
                requires (requires{
                    {rest.template construct<I - 1>(std::forward<A>(args)...)};
                })
            {
                rest.template construct<I - 1>(std::forward<A>(args)...);
            }

            template <size_t I> requires (I == 0)
            constexpr void destroy() noexcept {}  // trivially destructible

            template <size_t I> requires (I > 0)
            constexpr void destroy()
                noexcept (requires{{rest.template destroy<I - 1>()} noexcept;})
                requires (requires{{rest.template destroy<I - 1>()};})
            {
                rest.template destroy<I - 1>();
            }
        };

    public:
        using type = _type<Ts...>;
        [[no_unique_address]] type m_data;
        [[no_unique_address]] impl::union_index_type<sizeof...(Ts)> m_index;

        /* Default constructor selects the first default-constructible type in `Ts...`,
        and initializes the union to that type. */
        [[nodiscard]] constexpr union_storage()
            noexcept (meta::nothrow::default_constructible<default_type>)
            requires (meta::not_void<default_type>)
        :
            m_data(tag<meta::index_of<union_default_type<Ts...>, Ts...>>{}),
            m_index(meta::index_of<union_default_type<Ts...>, Ts...>)
        {}

        /* Tagged constructor specifically initializes the alternative at index `I`
        with the given arguments. */
        template <size_t I, typename... A>
        [[nodiscard]] explicit constexpr union_storage(tag<I> t, A&&... args)
            noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, Ts...>, A...>)
            requires (meta::constructible_from<meta::unpack_type<I, Ts...>, A...>)
        :
            m_data(t, std::forward<A>(args)...),
            m_index(I)
        {}

        /* Return the index of the active alternative. */
        [[nodiscard]] constexpr size_t index() const noexcept { return m_index; }

        /* Get a standardized index error for index `I`. */
        template <size_t I>
        [[nodiscard]] constexpr BadUnionAccess index_error() const noexcept {
            return impl::union_index_error<I>::template error<Ts...>(index());
        }

        /* Get a standardized type error for index `I`. */
        template <typename T>
        [[nodiscard]] constexpr BadUnionAccess type_error() const noexcept {
            return impl::union_type_error<T>::template error<Ts...>(index());
        }

        /* Access a specific value by index, where the index is known at compile
        time. */
        template <size_t I, typename Self> requires (I < sizeof...(Ts))
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_data.template value<I>());
        }

    private:
        /// NOTE: copy/move constructors, assignment operators, and destructors have to
        /// be generated using manual vtables in order to make the union memory-safe
        /// at all times, with minimal overhead.  Assignment operators use the
        /// copy-and-swap idiom to provide strong exception safety, and reduce the
        /// overall surface area of the union interface.  Similarly, the destructor
        /// vtable will not be generated if all types within the union are trivially
        /// destructible (including all lvalue references), saving a dispatch in that
        /// case.

        static constexpr bool copyable =
            ((meta::lvalue<Ts> || meta::copyable<meta::remove_rvalue<Ts>>) && ...);
        static constexpr bool nothrow_copyable =
            ((meta::lvalue<Ts> || meta::nothrow::copyable<meta::remove_rvalue<Ts>>) && ...);
        static constexpr bool trivially_copyable =
            ((meta::lvalue<Ts> || meta::trivially_copyable<meta::remove_rvalue<Ts>>) && ...);

        static constexpr bool movable =
            ((meta::lvalue<Ts> || meta::movable<meta::remove_rvalue<Ts>>) && ...);
        static constexpr bool nothrow_movable =
            ((meta::lvalue<Ts> || meta::nothrow::movable<meta::remove_rvalue<Ts>>) && ...);

        static constexpr bool destructible =
            ((meta::lvalue<Ts> || meta::destructible<meta::remove_rvalue<Ts>>) && ...);
        static constexpr bool nothrow_destructible =
            ((meta::lvalue<Ts> || meta::nothrow::destructible<meta::remove_rvalue<Ts>>) && ...);
        static constexpr bool trivially_destructible =
            ((meta::lvalue<Ts> || meta::trivially_destructible<meta::remove_rvalue<Ts>>) && ...);

        static constexpr bool swappable = ((meta::lvalue<Ts> || (
            meta::destructible<meta::remove_rvalue<Ts>> &&
            meta::movable<meta::remove_rvalue<Ts>>
        )) && ...);
        static constexpr bool nothrow_swappable = ((meta::lvalue<Ts> || (
            meta::nothrow::destructible<meta::remove_rvalue<Ts>> &&
            meta::nothrow::movable<meta::remove_rvalue<Ts>> && (
                meta::nothrow::swappable<meta::remove_rvalue<Ts>> || (
                    !meta::swappable<meta::remove_rvalue<Ts>> &&
                    meta::nothrow::move_assignable<meta::remove_rvalue<Ts>>
                ) || (
                    !meta::swappable<meta::remove_rvalue<Ts>> &&
                    !meta::move_assignable<meta::remove_rvalue<Ts>>
                )
            )
        )) && ...);

        using copy_ptr = type(*)(const union_storage&) noexcept (nothrow_copyable);
        using move_ptr = type(*)(union_storage&&) noexcept (nothrow_movable);
        using destroy_ptr = void(*)(union_storage&) noexcept (nothrow_destructible);
        using swap_ptr = void(*)(union_storage&, union_storage&) noexcept (nothrow_swappable);

        template <size_t I>
        static constexpr type copy_fn(const union_storage& other)
            noexcept (nothrow_copyable)
        {
            return {tag<I>{}, other.m_data.template value<I>()};
        }

        template <size_t I>
        static constexpr type move_fn(union_storage&& other)
            noexcept (nothrow_movable)
        {
            return {tag<I>{}, std::move(other).m_data.template value<I>()};
        }

        template <size_t I>
        static constexpr void destroy_fn(union_storage& self)
            noexcept (nothrow_destructible)
        {
            self.m_data.template destroy<I>();
        }

        template <size_t I>
        static constexpr void swap_fn(union_storage& self, union_storage& other)
            noexcept (nothrow_swappable)
        {
            static constexpr size_t J = I / sizeof...(Ts);
            static constexpr size_t K = I % sizeof...(Ts);
            using T = meta::remove_rvalue<meta::unpack_type<J, Ts...>>;

            // prefer a direct swap if the indices match and a corresponding operator
            // is available
            if constexpr (J == K && !meta::lvalue<T> && meta::swappable<T>) {
                std::ranges::swap(
                    self.m_data.template value<J>(),
                    other.m_data.template value<K>()
                );

            // otherwise, if the indices match and the types are move assignable, use a
            // temporary variable with best-effort error recovery
            } else if constexpr (J == K && !meta::lvalue<T> && meta::move_assignable<T>) {
                T temp(std::move(self).m_data.template value<J>());
                try {
                    self.m_data.template value<J>() = std::move(other).m_data.template value<K>();
                    try {
                        other.m_data.template value<K>() = std::move(temp);
                    } catch (...) {
                        other.m_data.template value<K>() =
                            std::move(self).m_data.template value<J>();
                        throw;
                    }
                } catch (...) {
                    self.m_data.template value<J>() = std::move(temp);
                    throw;
                }

            // If the indices differ or the types are lvalues, then we need to move
            // construct and destroy the original value behind us.
            } else {
                T temp(std::move(self).m_data.template value<J>());
                self.m_data.template destroy<J>();
                try {
                    self.m_data.template construct<K>(
                        std::move(other).m_data.template value<K>()
                    );
                    other.m_data.template destroy<K>();
                    try {
                        other.m_data.template construct<J>(std::move(temp));
                    } catch (...) {
                        other.m_data.template construct<K>(
                            std::move(self).m_data.template value<K>()
                        );
                        self.m_data.template destroy<K>();
                        throw;
                    }
                } catch (...) {
                    self.m_data.template construct<J>(std::move(temp));
                    throw;
                }
                other.m_index = J;
                self.m_index = K;
            }
        }

        template <typename = indices>
        static constexpr copy_ptr copy_tbl[0] {};
        template <size_t... Is> requires (copyable)
        static constexpr copy_ptr copy_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &copy_fn<Is>...
        };

        template <typename = indices>
        static constexpr move_ptr move_tbl[0] {};
        template <size_t... Is> requires (movable)
        static constexpr move_ptr move_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &move_fn<Is>...
        };

        template <typename = indices>
        static constexpr destroy_ptr destroy_tbl[0] {};
        template <size_t... Is>
            requires (
                destructible && (!meta::trivially_destructible<meta::remove_rvalue<Ts>> || ...)
            )
        static constexpr destroy_ptr destroy_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &destroy_fn<Is>...
        };

        template <typename = std::make_index_sequence<sizeof...(Ts) * sizeof...(Ts)>>
        static constexpr swap_ptr swap_tbl[0] {};
        template <size_t... Is> requires (swappable)
        static constexpr swap_ptr swap_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &swap_fn<Is>...
        };

        [[gnu::always_inline]] static constexpr type trivial_copy(const union_storage& other)
            noexcept
            requires (trivially_copyable)
        {
            type result;
            std::memcpy(&result, &other.m_data, sizeof(type));
            return result;
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr type recursive_copy(
            const union_storage& other,
            size_t index
        ) noexcept (nothrow_copyable) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (index == I) {
                    return copy_fn<I>(other);
                } else {
                    return recursive_copy<I + 1>(other, index);
                }
            } else {
                return copy_fn<I>(other);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr type recursive_move(
            union_storage&& other,
            size_t index
        ) noexcept (nothrow_movable) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (index == I) {
                    return move_fn<I>(std::move(other));
                } else {
                    return recursive_move<I + 1>(std::move(other), index);
                }
            } else {
                return move_fn<I>(std::move(other));
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr void recursive_destroy(
            union_storage& other,
            size_t index
        ) noexcept (nothrow_destructible) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (index == I) {
                    destroy_fn<I>(other);
                } else {
                    recursive_destroy<I + 1>(other, index);
                }
            } else {
                destroy_fn<I>(other);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr void recursive_swap(
            union_storage& lhs,
            union_storage& rhs,
            size_t index
        ) noexcept (nothrow_swappable) {
            if constexpr ((I + 1) < (sizeof...(Ts) * sizeof...(Ts))) {
                if (index == I) {
                    swap_fn<I>(lhs, rhs);
                } else {
                    recursive_swap<I + 1>(lhs, rhs, index);
                }
            } else {
                swap_fn<I>(lhs, rhs);
            }
        }

        static constexpr type dispatch_copy(const union_storage& other)
            noexcept (nothrow_copyable)
            requires (copyable)
        {
            if consteval {
                if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                    return copy_tbl<>[other.index()](other);
                } else {
                    return recursive_copy<0>(other, other.index());
                }
            } else {
                if constexpr (trivially_copyable) {
                    return trivial_copy(other);
                } else if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                    return copy_tbl<>[other.index()](other);
                } else {
                    return recursive_copy<0>(other, other.index());
                }
            }
        }

        static constexpr type dispatch_move(union_storage&& other)
            noexcept (nothrow_movable)
            requires (movable)
        {
            if consteval {
                if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                    return move_tbl<>[other.index()](other);
                } else {
                    return recursive_move<0>(other, other.index());
                }
            } else {
                if constexpr (trivially_copyable) {
                    return trivial_copy(other);
                } else if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                    return move_tbl<>[other.index()](other);
                } else {
                    return recursive_move<0>(other, other.index());
                }
            }
        }

        static constexpr void dispatch_destroy(union_storage& other)
            noexcept (nothrow_destructible)
            requires (destructible)
        {
            if constexpr (!trivially_destructible) {
                if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                    destroy_tbl<>[other.index()](other);
                } else {
                    recursive_destroy<0>(other, other.index());
                }
            }
        }

        static constexpr void dispatch_swap(union_storage& lhs, union_storage& rhs)
            noexcept (nothrow_swappable)
            requires (swappable)
        {
            if consteval {
                size_t index = lhs.index() * sizeof...(Ts) + rhs.index();
                if constexpr ((sizeof...(Ts) * sizeof...(Ts)) >= MIN_VTABLE_SIZE) {
                    swap_tbl<>[index](lhs, rhs);
                } else {
                    recursive_swap<0>(lhs, rhs, index);
                }
            } else {
                if constexpr (trivially_copyable) {
                    type temp;
                    auto idx = lhs.m_index;
                    std::memcpy(&temp, &lhs.m_data, sizeof(type));
                    std::memcpy(&lhs.m_data, &rhs.m_data, sizeof(type));
                    std::memcpy(&rhs.m_data, &temp, sizeof(type));
                    lhs.m_index = rhs.m_index;
                    rhs.m_index = idx;
                } else {
                    size_t index = lhs.index() * sizeof...(Ts) + rhs.index();
                    if constexpr ((sizeof...(Ts) * sizeof...(Ts)) >= MIN_VTABLE_SIZE) {
                        swap_tbl<>[index](lhs, rhs);
                    } else {
                        recursive_swap<0>(lhs, rhs, index);
                    }
                }
            }
        }

        /// NOTE: some unions may share a common type to which they can all be
        /// converted.  If so, then we generate an additional vtable that performs that
        /// conversion, perfectly forwarding all alternatives.

        template <typename Self>
        using common_type = impl::union_common_type<Self>::type;

        template <typename Self>
        static constexpr bool has_common_type = meta::not_void<common_type<Self>>;

        template <typename Self>
        static constexpr bool nothrow_common_type = impl::union_common_type<Self>::nothrow;

        template <typename Self>
        using flatten_ptr = common_type<Self>(*)(Self) noexcept (nothrow_common_type<Self>);

        template <typename Self, size_t I>
        static constexpr common_type<Self> flatten_fn(Self self)
            noexcept (nothrow_common_type<Self>)
        {
            return std::forward<Self>(self).m_data.template value<I>();
        }

        template <typename Self, typename = indices>
        static constexpr flatten_ptr<Self> flatten_tbl[0] {};
        template <typename Self, size_t... Is> requires (has_common_type<Self>)
        static constexpr flatten_ptr<Self> flatten_tbl<Self, std::index_sequence<Is...>>[
            sizeof...(Is)
        ] { &flatten_fn<Self, Is>...};

        template <size_t I, typename Self>
        [[gnu::always_inline]] static constexpr common_type<Self> recursive_flatten(
            Self&& self,
            size_t index
        ) noexcept (nothrow_common_type<meta::forward<Self>>) {
            if constexpr (I < sizeof...(Ts)) {
                if (index == I) {
                    return flatten_fn<meta::forward<Self>, I>(std::forward<Self>(self));
                } else {
                    return recursive_flatten<I + 1>(std::forward<Self>(self), index);
                }
            } else {
                return flatten_fn<meta::forward<Self>, I>(std::forward<Self>(self));
            }
        }

        template <typename Self>
        static constexpr common_type<Self> dispatch_flatten(Self&& self)
            noexcept (nothrow_common_type<meta::forward<Self>>)
            requires (has_common_type<meta::forward<Self>>)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return flatten_tbl<meta::forward<Self>>[self.index()](std::forward<Self>(self));
            } else {
                return recursive_flatten<0>(std::forward<Self>(self), self.index());
            }
        }

    public:
        [[nodiscard]] constexpr union_storage(const union_storage& other)
            noexcept (nothrow_copyable)
            requires (copyable)
        :
            m_data(dispatch_copy(other)),
            m_index(other.m_index)
        {}

        [[nodiscard]] constexpr union_storage(union_storage&& other)
            noexcept (nothrow_movable)
            requires (movable)
        :
            m_data(dispatch_move(std::move(other))),
            m_index(other.m_index)
        {}

        constexpr union_storage& operator=(const union_storage& other)
            noexcept (nothrow_copyable && nothrow_swappable)
            requires (copyable && swappable)
        {
            if (this != &other) {
                union_storage temp(other);
                dispatch_swap(*this, temp);
            }
            return *this;
        }

        constexpr union_storage& operator=(union_storage&& other)
            noexcept (nothrow_movable && nothrow_swappable)
            requires (movable && swappable)
        {
            if (this != &other) {
                union_storage temp(std::move(other));
                dispatch_swap(*this, temp);
            }
            return *this;
        }

        constexpr ~union_storage()
            noexcept (nothrow_destructible)
            requires (destructible)
        {
            
            dispatch_destroy(*this);
        }

        constexpr void swap(union_storage& other)
            noexcept (nothrow_swappable)
            requires (swappable)
        {
            if (this != &other) {
                dispatch_swap(*this, other);
            }
        }

        /* If all alternatives share a common, interconvertible type, then convert to
        that type.  Fails to compile if no such type exists. */
        template <typename Self>
        [[nodiscard]] constexpr common_type<Self> flatten(this Self&& self)
            noexcept (nothrow_common_type<meta::forward<Self>>)
            requires (has_common_type<meta::forward<Self>>)
        {
            return dispatch_flatten(std::forward<Self>(self));
        }
    };

    /* A special case of `union_storage` for optional references, which encode the
    reference as a raw pointer, with null representing the empty state.  This removes
    the need for an additional tracking index. */
    template <meta::None empty, meta::lvalue ref> requires (meta::has_address<ref>)
    struct union_storage<empty, ref> {
        using indices = std::index_sequence_for<empty, ref>;
        using types = meta::pack<empty, ref>;
        using default_type = empty;
        using type = meta::address_type<ref>;

        template <size_t I> requires (I < 2)
        using tag = impl::union_select<I>;

        [[no_unique_address]] type m_data;

        /* Default constructor always initializes to the empty state. */
        [[nodiscard]] constexpr union_storage(tag<0> = {}) noexcept : m_data(nullptr) {};

        /* Tagged constructor specifically initializes the alternative at index `I`
        with the given arguments. */
        [[nodiscard]] explicit constexpr union_storage(tag<1>, ref r)
            noexcept (meta::nothrow::has_address<ref>)
        :
            m_data(std::addressof(r))
        {}

        /* Special constructor that takes a pointer directly, enabling direct
        conversions. */
        [[nodiscard]] explicit constexpr union_storage(type p) noexcept : m_data(p) {}

        constexpr void swap(union_storage& other) noexcept {
            std::swap(m_data, other.m_data);
        }

        /* Return the index of the active alternative. */
        [[nodiscard]] constexpr size_t index() const noexcept { return m_data != nullptr; }

        /* Get a standardized index error for index `I`. */
        template <size_t I>
        [[nodiscard]] constexpr BadUnionAccess index_error() const noexcept {
            return impl::union_index_error<I>::template error<empty, ref>(index());
        }

        /* Get a standardized type error for index `I`. */
        template <typename T>
        [[nodiscard]] constexpr BadUnionAccess type_error() const noexcept {
            return impl::union_type_error<T>::template error<empty, ref>(index());
        }

        /* Access a specific value by index, where the index is known at compile
        time. */
        template <size_t I, typename Self> requires (I < 2)
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (None);
            } else {
                return (*self.m_data);
            }
        }

    private:
        using common = impl::union_common_type<const union_storage&>;

    public:
        /* If all alternatives share a common, interconvertible type, then convert to
        that type.  Fails to compile if no such type exists. */
        [[nodiscard]] constexpr common::type flatten() const
            noexcept (common::nothrow)
            requires (meta::not_void<typename common::type>)
        {
            if (m_data == nullptr) {
                return None;
            } else {
                return *m_data;
            }
        }
    };

    /* Result 1: convert to proximal type */
    template <typename from, typename proximal, typename convert, typename...>
    struct _union_convert_from { using type = proximal; };

    /* Result 2: convert to first implicitly convertible type (void if none) */
    template <typename from, meta::is_void proximal, typename convert>
    struct _union_convert_from<from, proximal, convert> { using type = convert; };

    template <typename from, typename curr>
    concept union_proximal =
        meta::inherits<from, curr> &&
        meta::convertible_to<from, curr> &&
        (meta::lvalue<from> ? !meta::rvalue<curr> : !meta::lvalue<curr>);

    /* Recursive 1: prefer the most derived and least qualified matching alternative,
    with lvalues binding to lvalues and prvalues, and rvalues binding to rvalues and
    prvalues.  If the result type is void, the candidate is more derived than it, or
    the candidate is less qualified, replace the intermediate result */
    template <typename from, typename proximal, typename convert, typename curr, typename... next>
        requires (union_proximal<from, curr>)
    struct _union_convert_from<from, proximal, convert, curr, next...> : _union_convert_from<
        from,
        std::conditional_t<
            meta::is_void<proximal> ||
            (meta::inherits<curr, proximal> && !meta::is<curr, proximal>) || (
                meta::is<curr, proximal> && (
                    (meta::lvalue<curr> && !meta::lvalue<proximal>) ||
                    meta::more_qualified_than<proximal, curr>
                )
            ),
            curr,
            proximal
        >,
        convert,
        next...
    > {};

    template <typename from, typename curr, typename convert>
    concept union_convertible =
        meta::is_void<convert> && !meta::lvalue<curr> && meta::convertible_to<from, curr>;

    /* Recursive 2: if no proximal match is found, prefer the leftmost implicitly
    convertible type. */
    template <typename from, typename proximal, typename convert, typename curr, typename... next>
        requires (!union_proximal<from, curr> && union_convertible<from, curr, convert>)
    struct _union_convert_from<from, proximal, convert, curr, next...> :
        _union_convert_from<from, proximal, curr, next...>
    {};

    /* Recursive 3: no match at this index, discard curr */
    template <typename from, typename proximal, typename convert, typename curr, typename... next>
        requires (!union_proximal<from, curr> && !union_convertible<from, curr, convert>)
    struct _union_convert_from<from, proximal, convert, curr, next...> :
        _union_convert_from<from, proximal, convert, next...>
    {};

    /* A simple visitor that backs the implicit constructor for a `Union<Ts...>`
    object, returning a corresponding `impl::union_storage` primitive type. */
    template <typename, typename...>
    struct union_convert_from {};
    template <typename... Ts, typename in> requires (!meta::monad<in>)
    struct union_convert_from<meta::pack<Ts...>, in> {
        template <typename from>
        using type = _union_convert_from<from, void, void, Ts...>::type;
        template <typename from>
        static constexpr impl::union_storage<Ts...> operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, type<from>>)
            requires (meta::not_void<type<from>>)
        {
            return {
                impl::union_select<meta::index_of<type<from>, Ts...>>{},
                std::forward<from>(arg)
            };
        }
    };

    /* A simple visitor that backs the explicit constructor for a `Union<Ts...>`
    object, returning a corresponding `impl::union_storage` primitive type.  Note that
    this only applies if `union_convert_from` would be invalid. */
    template <typename... A>
    struct _union_construct_from {
        template <typename... Ts>
        struct select { using type = void; };
        template <meta::constructible_from<A...> T, typename... Ts>
        struct select<T, Ts...> { using type = T; };
        template <typename T, typename... Ts>
        struct select<T, Ts...> : select<Ts...> {};
    };
    template <typename... Ts>
    struct union_construct_from {
        template <typename... A>
        using type = _union_construct_from<A...>::template select<Ts...>::type;
        template <typename... A>
        static constexpr impl::union_storage<Ts...> operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<type<A...>, A...>)
            requires (meta::not_void<type<A...>>)
        {
            return {
                impl::union_select<meta::index_of<type<A...>, Ts...>>{},
                std::forward<A>(args)...
            };
        }
    };

    /* A simple visitor that backs the implicit constructor for an `Optional<T>`
    object, returning a corresponding `impl::union_storage` primitive type. */
    template <typename T, typename...>
    struct optional_convert_from {};
    template <typename T, typename in> requires (!meta::monad<in>)
    struct optional_convert_from<T, in> {
        using type = meta::remove_rvalue<T>;
        using empty = impl::visitable<in>::empty;

        // 1) prefer direct conversion to `T` if possible
        template <typename alt>
        static constexpr impl::union_storage<NoneType, T> operator()(alt&& v)
            noexcept (meta::nothrow::convertible_to<alt, type>)
            requires (meta::convertible_to<alt, type>)
        {
            return {union_select<1>{}, std::forward<alt>(v)};
        }

        // 2) otherwise, if the argument is in an empty state as defined by the input's
        // `impl::visitable` specification, then we return it as an empty
        // `union_storage` object. 
        template <typename alt>
        static constexpr impl::union_storage<NoneType, T> operator()(alt&&)
            noexcept (meta::nothrow::default_constructible<impl::union_storage<NoneType, T>>)
            requires (!meta::convertible_to<alt, type> && meta::is<alt, empty>)
        {
            return {};
        }

        // 3) if `out` is an lvalue, then an extra conversion is enabled from raw
        // pointers, where nullptr gets translated into an empty `union_storage`
        // object, exploiting the pointer optimization.
        template <typename alt>
        static constexpr impl::union_storage<NoneType, T> operator()(alt&& p)
            noexcept (meta::nothrow::convertible_to<alt, meta::address_type<type>>)
            requires (
                !meta::convertible_to<alt, type> &&
                !meta::is<alt, empty> &&
                meta::lvalue<type> &&
                meta::has_address<type> &&
                meta::convertible_to<alt, meta::address_type<type>>
            )
        {
            return {p};
        }
    };

    /* A simple visitor that backs the explicit constructor for an `Optional<T>`
    object, returning a corresponding `impl::union_storage` primitive type.  Note that
    this only applies if `optional_convert_from` is invalid. */
    template <typename out>
    struct optional_construct_from {
        using result = impl::union_storage<NoneType, out>;
        using type = meta::remove_rvalue<out>;
        template <typename... A>
        static constexpr result operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<type, A...>)
            requires (meta::constructible_from<type, A...>)
        {
            return {typename result::template tag<1>{}, std::forward<A>(args)...};
        }
    };

    /* Given an initializer that inherits from at least one expected exception type,
    determine the most proximal exception to initialize. */
    template <typename out, typename, typename...>
    struct _expected_convert_error { using type = out; };
    template <typename out, typename from, typename curr, typename... next>
        requires (meta::inherits<from, curr> && (meta::is_void<out> || meta::inherits<curr, out>))
    struct _expected_convert_error<out, from, curr, next...> :
        _expected_convert_error<curr, from, next...>  // replace `out`
    {};
    template <typename out, typename from, typename curr, typename... next>
    struct _expected_convert_error<out, from, curr, next...> :
        _expected_convert_error<out, from, next...>  // keep `out`
    {};
    template <typename from, typename... errors>
    using expected_convert_error = _expected_convert_error<void, from, errors...>::type;

    /* A simple visitor that backs the implicit constructor for an `Expected<T, Es...>`
    object, returning a corresponding `impl::union_storage` primitive type. */
    template <typename, typename...>
    struct expected_convert_from {};
    template <typename T, typename... Es, typename in> requires (!meta::monad<in>)
    struct expected_convert_from<meta::pack<T, Es...>, in> {
        template <typename from>
        using type = expected_convert_error<from, Es...>;

        // 1) prefer direct conversion to `out` if possible
        template <typename from>
        static constexpr impl::union_storage<T, Es...> operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, meta::remove_rvalue<T>>)
            requires (meta::convertible_to<from, meta::remove_rvalue<T>>)
        {
            return {impl::union_select<0>{}, std::forward<from>(arg)};
        }

        // 2) otherwise, if the input inherits from one of the expected error types,
        // then we convert it to the most proximal such type.
        template <typename from>
        static constexpr impl::union_storage<T, Es...> operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, type<from>>)
            requires (
                !meta::convertible_to<from, meta::remove_rvalue<T>> &&
                meta::not_void<type<from>>
            )
        {
            return {
                impl::union_select<meta::index_of<type<from>, Es...>>{},
                std::forward<from>(arg)
            };
        }
    };

    /* A simple visitor that backs the explicit constructor for an `Expected<T, Es...>`
    object, returning a corresponding `impl::union_storage` primitive type.  Note that
    this only applies if `expected_convert_from` is invalid. */
    template <typename T, typename... Es>
    struct expected_construct_from {
        using type = meta::remove_rvalue<T>;
        template <typename... A>
        static constexpr impl::union_storage<T, Es...> operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<type, A...>)
            requires (meta::constructible_from<type, A...>)
        {
            return {impl::union_select<0>{}, std::forward<A>(args)...};
        }
    };

    /* Safely access a perfectly-forwarded type from a `union_storage` container as
    either an `Expected` or `Optional`, corresponding to the `.value()` and
    `.value_if()` methods of a `Union`, respectively. */
    template <typename Self>
    struct union_access {
        using types = meta::unqualify<Self>::types;
        using size_type = types::size_type;
        using index_type = types::index_type;

        template <index_type I>
        static constexpr bool valid_index = impl::valid_index<types::ssize(), I>;

        template <index_type I> requires (valid_index<I>)
        static constexpr size_type normalize_index = impl::normalize_index<types::ssize(), I>();

        template <typename T>
        static constexpr bool contains = types::template contains<T>();

        template <typename T>
        static constexpr size_type index_of = types::template index<T>();

        template <index_type I> requires (valid_index<I>)
        using idx = decltype(std::declval<Self>().template value<normalize_index<I>>());

        template <typename T> requires (contains<T>)
        using type = idx<index_of<T>>;

        template <index_type I> requires (valid_index<I>)
        using opt_idx = bertrand::Optional<idx<I>>;

        template <typename T> requires (contains<T>)
        using opt_type = bertrand::Optional<type<T>>;

        template <index_type I> requires (valid_index<I>)
        using exp_idx = bertrand::Expected<idx<I>, BadUnionAccess>;

        template <typename T> requires (contains<T>)
        using exp_type = bertrand::Expected<type<T>, BadUnionAccess>;

        template <index_type I> requires (valid_index<I>)
        [[nodiscard]] constexpr exp_idx<I> value(Self self)
            noexcept (requires{
                {
                    self.template index_error<normalize_index<I>>()
                } noexcept -> meta::nothrow::convertible_to<exp_idx<I>>;
                {
                    std::forward<Self>(self).template value<normalize_index<I>>()
                } noexcept -> meta::nothrow::convertible_to<exp_idx<I>>;
            })
            requires (requires{
                {
                    self.template index_error<normalize_index<I>>()
                } -> meta::convertible_to<exp_idx<I>>;
                {
                    std::forward<Self>(self).template value<normalize_index<I>>()
                } -> meta::convertible_to<exp_idx<I>>;
            })
        {
            if (self.index() != I) {
                return self.template index_error<normalize_index<I>>();
            }
            return std::forward<Self>(self).template value<normalize_index<I>>();
        }

        template <typename T> requires (contains<T>)
        [[nodiscard]] constexpr exp_type<T> value_if(Self self)
            noexcept (requires{
                {
                    self.template type_error<T>()
                } noexcept -> meta::nothrow::convertible_to<exp_type<T>>;
                {
                    std::forward<Self>(self).template value<index_of<T>>()
                } noexcept -> meta::nothrow::convertible_to<exp_type<T>>;
            })
            requires (requires{
                {
                    self.template type_error<T>()
                } -> meta::convertible_to<exp_type<T>>;
                {
                    std::forward<Self>(self).template value<index_of<T>>()
                } -> meta::convertible_to<exp_type<T>>;
            })
        {
            if (self.index() != index_of<T>) {
                return self.template type_error<T>();
            }
            return std::forward<Self>(self).template value<index_of<T>>();
        }

        template <index_type I> requires (valid_index<I>)
        [[nodiscard]] constexpr opt_idx<I> value_if(Self self)
            noexcept (requires{{
                std::forward<Self>(self).template value<normalize_index<I>>()
            } noexcept -> meta::nothrow::convertible_to<opt_idx<I>>;})
            requires (requires{{
                std::forward<Self>(self).template value<normalize_index<I>>()
            } -> meta::convertible_to<opt_idx<I>>;})
        {
            if (self.index() != I) {
                return None;
            }
            return std::forward<Self>(self).template value<normalize_index<I>>();
        }

        template <typename T> requires (contains<T>)
        [[nodiscard]] constexpr opt_type<T> value_if(Self self)
            noexcept (requires{{
                std::forward<Self>(self).template value<index_of<T>>()
            } noexcept -> meta::nothrow::convertible_to<opt_type<T>>;})
            requires (requires{{
                std::forward<Self>(self).template value<index_of<T>>()
            } -> meta::convertible_to<opt_type<T>>;})
        {
            if (self.index() != index_of<T>) {
                return None;
            }
            return std::forward<Self>(self).template value<index_of<T>>();
        }

    };
    template <meta::Union Self>
    struct union_access<Self> : union_access<decltype(std::declval<Self>()._value)> {};
    template <meta::Optional Self>
    struct union_access<Self> : union_access<decltype(std::declval<Self>()._value)> {};
    template <meta::Expected Self>
    struct union_access<Self> : union_access<decltype(std::declval<Self>()._value)> {};



    /// TODO: ensure that impl::visitable<Self> exposes the right fields.  When I
    /// refactor the visit internals, I can come back to this and ensure it all works,
    /// and possibly expand it to make the other trivial visitors generic in the
    /// same way.  That way they don't depend on the underlying union type or how it is
    /// implemented, using impl::visitable<Self> to handle the details.

    /* A simple visitor that backs the `Optional.value_or(f)` accessor, where `f` is
    a function callable with zero arguments that will be invoked only if the optional
    is empty. */
    template <typename Self>
    struct optional_or_else {
        using empty_type = impl::visitable<Self>::empty_type;
        template <typename F, typename T> requires (!meta::is<empty_type, T>)
        static constexpr decltype(auto) operator()(F&&, T&& value) noexcept {
            return (std::forward<T>(value));
        }
        template <typename F, meta::is<empty_type> T>
        static constexpr decltype(auto) operator()(F&& func, T&&)
            noexcept (meta::nothrow::callable<F>)
            requires (meta::callable<F>)
        {
            return (std::forward<F>(func)());
        }
    };

    /* A helper type to extract the error type from an `Expected<T, Es...>`. */
    template <typename... Es>
    struct _expected_error_type { using type = bertrand::Union<Es...>; };
    template <typename E>
    struct _expected_error_type<E> { using type = E; };
    template <typename... Es>
    using expected_error = _expected_error_type<Es...>::type;

    /* A simple visitor that backs the `Expected.value_or(fs...)` accessor, where
    `fs...` are one or more functions that are collectively callable with all error
    states of the expected.  If the expected specializes a `None` or `void` return
    type, then the value path will return void, causing the visitor logic to promote
    the final return type to an optional, or eliminate it entirely if the function also
    returns void. */
    template <typename Self>
    struct expected_or_else {
        using value_type = impl::visitable<Self>::value_type;
        using empty_type = impl::visitable<Self>::empty_type;
        template <typename F, meta::is<value_type> T>
        static constexpr decltype(auto) operator()(F&&, T&& value) noexcept {
            if constexpr (!meta::is<value_type, empty_type>) {
                return (std::forward<T>(value));
            }
        }
        template <typename F, typename E> requires (!meta::is<value_type, E>)
        static constexpr decltype(auto) operator()(F&& func, E&& error)
            noexcept (meta::nothrow::callable<F, E>)
            requires (meta::callable<F, E>)
        {
            return (std::forward<F>(func)(std::forward<E>(error)));
        }
    };

    /* A simple visitor that backs the implicit conversion operator from `Optional<T>`,
    which attempts a normal visitor conversion where possible, falling back to a
    conversion from `std::nullopt` or `nullptr` to cover all STL types and raw
    pointers in the case of optional lvalues. */
    template <typename Self, typename to>
    struct optional_convert_to {
        using value_type = impl::visitable<Self>::value_type;
        using empty_type = impl::visitable<Self>::empty_type;
        template <typename from>
        static constexpr to operator()(from&& value)
            noexcept (meta::nothrow::convertible_to<from, to>)
            requires (meta::convertible_to<from, to>)
        {
            return std::forward<from>(value);
        }
        template <meta::is<empty_type> from>
        static constexpr to operator()(from&&)
            noexcept (meta::nothrow::convertible_to<const std::nullopt_t&, to>)
            requires (
                !meta::convertible_to<from, to> &&
                meta::convertible_to<const std::nullopt_t&, to>
            )
        {
            return std::nullopt;
        }
        template <meta::is<empty_type> from> requires (meta::lvalue<value_type>)
        static constexpr to operator()(from&&)
            noexcept (meta::nothrow::convertible_to<std::nullptr_t, to>)
            requires (
                !meta::convertible_to<from, to> &&
                !meta::convertible_to<const std::nullopt_t&, to> &&
                meta::convertible_to<std::nullptr_t, to>
            )
        {
            return nullptr;
        }
    };

    /* A simple visitor that backs the implicit conversion operator from
    `Expected<T, Es...>`, which attempts a normal visitor conversion where possible,
    falling back to a conversion from `std::unexpected` to cover all STL types. */
    template <typename Self, typename to>
    struct expected_convert_to {
        using value_type = impl::visitable<Self>::value_type;
        template <typename from>
        static constexpr to operator()(from&& value)
            noexcept (meta::nothrow::convertible_to<from, to>)
            requires (meta::convertible_to<from, to>)
        {
            return std::forward<from>(value);
        }
        template <typename from>
        static constexpr to operator()(from&& value)
            noexcept (meta::nothrow::convertible_to<std::unexpected<meta::unqualify<from>>, to>)
            requires (
                !meta::is<from, value_type> &&
                !meta::convertible_to<from, to> &&
                meta::convertible_to<std::unexpected<meta::unqualify<from>>, to>
            )
        {
            return std::unexpected<meta::unqualify<from>>(std::forward<from>(value));
        }
    };

    /* A trivial iterator that only yields a single value, which is represented as a
    raw pointer internally.  This is the iterator type for `Optional<T>` where `T` is
    not itself iterable. */
    template <meta::not_void T>
    struct single_iterator {
        using wrapped = T;
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = meta::remove_reference<T>;
        using reference = meta::as_lvalue<T>;
        using const_reference = meta::as_const<reference>;
        using pointer = meta::as_pointer<reference>;
        using const_pointer = meta::as_pointer<const_reference>;

        pointer value;

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return *value;
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept {
            return value;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
            return *(value + n);
        }

        constexpr single_iterator& operator++() noexcept {
            ++value;
            return *this;
        }

        [[nodiscard]] constexpr single_iterator operator++(int) noexcept {
            auto tmp = *this;
            ++value;
            return tmp;
        }

        [[nodiscard]] friend constexpr single_iterator operator+(
            const single_iterator& self,
            difference_type n
        ) noexcept {
            return {self.value + n};
        }

        [[nodiscard]] friend constexpr single_iterator operator+(
            difference_type n,
            const single_iterator& self
        ) noexcept {
            return {self.value + n};
        }

        constexpr single_iterator& operator+=(difference_type n) noexcept {
            value += n;
            return *this;
        }

        constexpr single_iterator& operator--() noexcept {
            --value;
            return *this;
        }

        [[nodiscard]] constexpr single_iterator operator--(int) noexcept {
            auto tmp = *this;
            --value;
            return tmp;
        }

        [[nodiscard]] constexpr single_iterator operator-(difference_type n) const noexcept {
            return {value - n};
        }

        [[nodiscard]] constexpr difference_type operator-(
            const single_iterator& other
        ) const noexcept {
            return value - other.value;
        }

        constexpr single_iterator& operator-=(difference_type n) noexcept {
            value -= n;
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const single_iterator& other) const noexcept {
            return value == other.value;
        }

        [[nodiscard]] constexpr auto operator<=>(const single_iterator& other) const noexcept {
            return value <=> other.value;
        }
    };

    /* A wrapper for an arbitrary iterator that allows it to be constructed in an empty
    state in order to represent empty optionals.  This is the iterator type for
    `Optional<T>`, where `T` is iterable.  In that case, this iterator behaves exactly
    like the underlying iterator type, with the caveat that it can only be compared
    against other `optional_iterator<U>` wrappers, where `U` may be different from
    `T`. */
    template <meta::unqualified T> requires (meta::iterator<T>)
    struct optional_iterator {
    private:
        union storage {
            [[no_unique_address]] T iter;
            constexpr storage() noexcept {};
            constexpr storage(const T& it) 
                noexcept (meta::nothrow::copyable<T>)
                requires (meta::copyable<T>)
            : iter(it) {}
            constexpr storage(T&& it)
                noexcept (meta::nothrow::movable<T>)
                requires (meta::movable<T>)
            : iter(std::move(it)) {}
            constexpr ~storage()
                noexcept (meta::nothrow::destructible<T>)
                requires (meta::destructible<T>)
            {}
        };

        static constexpr storage copy(const optional_iterator& other)
            noexcept (requires{{storage{other._value.iter}} noexcept;})
            requires (requires{{storage{other._value.iter}};})
        {
            if (other.initialized) {
                return {other._value.iter};
            } else {
                return {};
            }
        }

        static constexpr storage move(optional_iterator&& other)
            noexcept (requires{{storage{std::move(other)._value.iter}} noexcept;})
            requires (requires{{storage{std::move(other)._value.iter}};})
        {
            if (other.initialized) {
                return {std::move(other)._value.iter};
            } else {
                return {};
            }
        }

    public:
        using iterator_category = meta::iterator_category<T>;
        using difference_type = meta::iterator_difference_type<T>;
        using value_type = meta::iterator_value_type<T>;
        using reference = meta::iterator_reference_type<T>;
        using pointer = meta::iterator_pointer_type<T>;

        [[no_unique_address]] storage _value;
        [[no_unique_address]] bool initialized;

        [[nodiscard]] constexpr optional_iterator() noexcept : initialized(false) {}

        [[nodiscard]] constexpr optional_iterator(const T& it)
            noexcept (meta::nothrow::copyable<T>)
            requires (meta::copyable<T>)
        :
            _value(it),
            initialized(true)
        {}

        [[nodiscard]] constexpr optional_iterator(T&& it)
            noexcept (meta::nothrow::movable<T>)
            requires (meta::movable<T>)
        :
            _value(std::move(it)),
            initialized(true)
        {}

        [[nodiscard]] constexpr optional_iterator(const optional_iterator& other)
            noexcept (meta::nothrow::copyable<T>)
            requires (meta::copyable<T>)
        :
            _value(copy(other)),
            initialized(other.initialized)
        {}

        [[nodiscard]] constexpr optional_iterator(optional_iterator&& other)
            noexcept (meta::nothrow::movable<T>)
            requires (meta::movable<T>)
        :
            _value(move(std::move(other))),
            initialized(other.initialized)
        {
            other.initialized = false;
        }

        [[nodiscard]] constexpr optional_iterator& operator=(const optional_iterator& other)
            noexcept (meta::nothrow::copyable<T> && meta::nothrow::copy_assignable<T>)
            requires (meta::copyable<T> && meta::copy_assignable<T>)
        {
            if (initialized && other.initialized) {
                if (this != other) {
                    _value.iter = other._value.iter;
                }
            } else if (initialized) {
                std::destroy_at(&_value.iter);
                initialized = false;
            } else if (other.initialized) {
                std::construct_at(&_value.iter, other._value.iter);
                initialized = true;
            }
            return *this;
        }

        [[nodiscard]] constexpr optional_iterator& operator=(optional_iterator&& other)
            noexcept (meta::nothrow::copyable<T> && meta::nothrow::copy_assignable<T>)
            requires (meta::copyable<T> && meta::copy_assignable<T>)
        {
            if (initialized && other.initialized) {
                if (this != other) {
                    _value.iter = std::move(other)._value.iter;
                    other.initialized = false;
                }
            } else if (initialized) {
                std::destroy_at(&_value.iter);
                initialized = false;
            } else if (other.initialized) {
                std::construct_at(&_value.iter, std::move(other)._value.iter);
                initialized = true;
                other.initialized = false;
            }
            return *this;
        }

        constexpr ~optional_iterator()
            noexcept (meta::nothrow::destructible<T>)
            requires (meta::destructible<T>)
        {
            if (initialized) {
                std::destroy_at(&_value.iter);
            }
            initialized = false;
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self)._value.iter} noexcept;})
            requires (requires{{*std::forward<Self>(self)._value.iter};})
        {
            return (*std::forward<Self>(self)._value.iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{std::to_address(std::forward<Self>(self)._value.iter)} noexcept;})
            requires (requires{{std::to_address(std::forward<Self>(self)._value.iter)};})
        {
            return std::to_address(std::forward<Self>(self)._value.iter);
        }

        template <typename Self, typename V>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, V&& v)
            noexcept (requires{
                {std::forward<Self>(self)._value.iter[std::forward<V>(v)]} noexcept;
            })
            requires (requires{
                {std::forward<Self>(self)._value.iter[std::forward<V>(v)]};
            })
        {
            return (std::forward<Self>(self)._value.iter[std::forward<V>(v)]);
        }

        constexpr optional_iterator& operator++()
            noexcept (requires{{++_value.iter} noexcept;})
            requires (requires{{++_value.iter};})
        {
            ++_value.iter;
            return *this;
        }

        [[nodiscard]] constexpr optional_iterator operator++(int)
            noexcept (requires{
                {optional_iterator(*this)} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {optional_iterator(*this)};
                {++*this};
            })
        {
            optional_iterator tmp(*this);
            ++*this;
            return tmp;
        }

        [[nodiscard]] friend constexpr optional_iterator operator+(
            const optional_iterator& lhs,
            difference_type rhs
        )
            noexcept (requires{{optional_iterator(lhs._value.iter + rhs)} noexcept;})
            requires (requires{{optional_iterator(lhs._value.iter + rhs)};})
        {
            return optional_iterator(lhs._value.iter + rhs);
        }

        [[nodiscard]] friend constexpr optional_iterator operator+(
            difference_type lhs,
            const optional_iterator& rhs
        )
            noexcept (requires{{optional_iterator(lhs + rhs._value.iter)} noexcept;})
            requires (requires{{optional_iterator(lhs + rhs._value.iter)};})
        {
            return optional_iterator(lhs + rhs._value.iter);
        }

        constexpr optional_iterator& operator+=(difference_type n)
            noexcept (requires{{_value.iter += n} noexcept;})
            requires (requires{{_value.iter += n};})
        {
            _value.iter += n;
            return *this;
        }

        constexpr optional_iterator& operator--()
            noexcept (requires{{--_value.iter} noexcept;})
            requires (requires{{--_value.iter};})
        {
            --_value.iter;
            return *this;
        }

        [[nodiscard]] constexpr optional_iterator operator--(int)
            noexcept (requires{
                {optional_iterator(*this)} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {optional_iterator(*this)};
                {--*this};
            })
        {
            optional_iterator tmp(*this);
            --*this;
            return tmp;
        }

        [[nodiscard]] constexpr optional_iterator operator-(difference_type n) const
            noexcept (requires{{optional_iterator(_value.iter - n)} noexcept;})
            requires (requires{{optional_iterator(_value.iter - n)};})
        {
            return optional_iterator(_value.iter - n);
        }

        [[nodiscard]] difference_type operator-(const optional_iterator& other) const
            noexcept (requires{{
                _value.iter - other._value.iter
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{
                _value.iter - other._value.iter
            } -> meta::convertible_to<difference_type>;})
        {
            return _value.iter - other._value.iter;
        }

        constexpr optional_iterator& operator-=(difference_type n)
            noexcept (requires{{_value.iter -= n} noexcept;})
            requires (requires{{_value.iter -= n};})
        {
            _value.iter -= n;
            return *this;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<(const optional_iterator<U>& other) const
            noexcept (requires{{
                initialized && other.initialized && _value.iter < other._value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                initialized && other.initialized && _value.iter < other._value.iter
            } -> meta::convertible_to<bool>;})
        {
            return initialized && other.initialized && _value.iter < other._value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<=(const optional_iterator<U>& other) const
            noexcept (requires{{
                !initialized || !other.initialized || _value.iter <= other._value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                !initialized || !other.initialized || _value.iter <= other._value.iter
            } -> meta::convertible_to<bool>;})
        {
            return !initialized || !other.initialized || _value.iter <= other._value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator==(const optional_iterator<U>& other) const
            noexcept (requires{{
                !initialized || !other.initialized || _value.iter == other._value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                !initialized || !other.initialized || _value.iter == other._value.iter
            } -> meta::convertible_to<bool>;})
        {
            return !initialized || !other.initialized || _value.iter == other._value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>=(const optional_iterator<U>& other) const
            noexcept (requires{{
                !initialized || !other.initialized || _value.iter >= other._value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                !initialized || !other.initialized || _value.iter >= other._value.iter
            } -> meta::convertible_to<bool>;})
        {
            return !initialized || !other.initialized || _value.iter >= other._value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>(const optional_iterator<U>& other) const
            noexcept (requires{{
                initialized && other.initialized && _value.iter > other._value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                initialized && other.initialized && _value.iter > other._value.iter
            } -> meta::convertible_to<bool>;})
        {
            return initialized && other.initialized && _value.iter > other._value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr auto operator<=>(const optional_iterator<U>& other) const
            noexcept (requires{{_value.iter <=> other._value.iter} noexcept;})
            requires (requires{{_value.iter <=> other._value.iter};})
        {
            using type = meta::unqualify<decltype(_value.iter <=> other._value.iter)>;
            if (!initialized || !other.initialized) {
                return type::equivalent;
            }
            return _value.iter <=> other._value.iter;
        }
    };

    template <typename T>
    struct make_optional_begin { using type = void; };
    template <meta::iterable T>
    struct make_optional_begin<T> { using type = optional_iterator<meta::begin_type<T>>; };

    template <typename T>
    struct make_optional_end { using type = void; };
    template <meta::iterable T>
    struct make_optional_end<T> { using type = optional_iterator<meta::end_type<T>>; };

    template <typename T>
    struct make_optional_rbegin { using type = void; };
    template <meta::reverse_iterable T>
    struct make_optional_rbegin<T> { using type = optional_iterator<meta::rbegin_type<T>>; };

    template <typename T>
    struct make_optional_rend { using type = void; };
    template <meta::reverse_iterable T>
    struct make_optional_rend<T> { using type = optional_iterator<meta::rend_type<T>>; };

    template <meta::lvalue T>
    struct make_optional_iterator {
        static constexpr bool trivial = true;
        using type = decltype(std::declval<T>()._value.template value<1>());
        using begin_type = single_iterator<type>;
        using end_type = begin_type;
        using rbegin_type = std::reverse_iterator<begin_type>;
        using rend_type = rbegin_type;

        static constexpr auto begin(T opt)
            noexcept (requires{
                {begin_type(std::addressof(opt._value.template value<1>()) + !opt.has_value())} noexcept;
            })
            requires (requires{
                {begin_type(std::addressof(opt._value.template value<1>()) + !opt.has_value())};
            })
        {
            return begin_type{std::addressof(opt._value.template value<1>()) + !opt.has_value()};
        }

        static constexpr auto end(T opt)
            noexcept (requires{
                {end_type(std::addressof(opt._value.template value<1>()) + 1)} noexcept;
            })
            requires (requires{
                {end_type(std::addressof(opt._value.template value<1>()) + 1)};
            })
        {
            return end_type{std::addressof(opt._value.template value<1>()) + 1};
        }

        static constexpr auto rbegin(T opt)
            noexcept (requires{{std::make_reverse_iterator(end(opt))} noexcept;})
            requires (requires{{std::make_reverse_iterator(end(opt))};})
        {
            return std::make_reverse_iterator(end(opt));
        }

        static constexpr auto rend(T opt)
            noexcept (requires{{std::make_reverse_iterator(begin(opt))} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin(opt))};})
        {
            return std::make_reverse_iterator(begin(opt));
        }
    };
    template <meta::lvalue T>
        requires (
            meta::iterable<decltype(std::declval<T>()._value.template value<1>())> ||
            meta::reverse_iterable<decltype(std::declval<T>()._value.template value<1>())>
        )
    struct make_optional_iterator<T> {
        static constexpr bool trivial = false;
        using type = decltype(std::declval<T>()._value.template value<1>());
        using begin_type = make_optional_begin<type>::type;
        using end_type = make_optional_end<type>::type;
        using rbegin_type = make_optional_rbegin<type>::type;
        using rend_type = make_optional_rend<type>::type;

        static constexpr auto begin(T opt)
            noexcept (requires{
                {begin_type{std::ranges::begin(opt._value.template value<1>())}} noexcept;
                {begin_type{}} noexcept;
            })
            requires (requires{
                {begin_type{std::ranges::begin(opt._value.template value<1>())}};
                {begin_type{}};
            })
        {
            if (opt.has_value()) {
                return begin_type{std::ranges::begin(opt._value.template value<1>())};
            } else {
                return begin_type{};
            }
        }

        static constexpr auto end(T opt)
            noexcept (requires{
                {end_type{std::ranges::end(opt._value.template value<1>())}} noexcept;
                {end_type{}} noexcept;
            })
            requires (requires{
                {end_type{std::ranges::end(opt._value.template value<1>())}};
                {end_type{}};
            })
        {
            if (opt.has_value()) {
                return end_type{std::ranges::end(opt._value.template value<1>())};
            } else {
                return end_type{};
            }
        }

        static constexpr auto rbegin(T opt)
            noexcept (requires{
                {rbegin_type{std::ranges::rbegin(opt._value.template value<1>())}} noexcept;
                {rbegin_type{}} noexcept;
            })
            requires (requires{
                {rbegin_type{std::ranges::rbegin(opt._value.template value<1>())}};
                {rbegin_type{}};
            })
        {
            if (opt.has_value()) {
                return rbegin_type{std::ranges::rbegin(opt._value.template value<1>())};
            } else {
                return rbegin_type{};
            }
        }

        static constexpr auto rend(T opt)
            noexcept (requires{
                {rend_type{std::ranges::rend(opt._value.template value<1>())}} noexcept;
                {rend_type{}} noexcept;
            })
            requires (requires{
                {rend_type{std::ranges::rend(opt._value.template value<1>())}};
                {rend_type{}};
            })
        {
            if (opt.has_value()) {
                return rend_type{std::ranges::rend(opt._value.template value<1>())};
            } else {
                return rend_type{};
            }
        }
    };

    /* Union iterators can dereference to exactly one type or a `Union` of 2 or more
    possible types, depending on the configuration of the input iterators. */
    template <typename, typename...>
    struct _union_iterator_ref { using type = void; };
    template <typename out>
    struct _union_iterator_ref<meta::pack<out>> { using type = meta::remove_rvalue<out>; };
    template <typename... out> requires (sizeof...(out) > 1)
    struct _union_iterator_ref<meta::pack<out...>> { using type = bertrand::Union<out...>; };
    template <typename... out, meta::has_dereference T, typename... Ts>
        requires (meta::index_of<meta::dereference_type<T>, out...> == sizeof...(out))
    struct _union_iterator_ref<meta::pack<out...>, T, Ts...> :
        _union_iterator_ref<meta::pack<out..., meta::dereference_type<T>>, Ts...>
    {};
    template <typename... out, typename T, typename... Ts>
    struct _union_iterator_ref<meta::pack<out...>, T, Ts...> :
        _union_iterator_ref<meta::pack<out...>, Ts...>
    {};
    template <meta::iterator... Ts>
    using union_iterator_ref = _union_iterator_ref<meta::pack<>, Ts...>::type;

    /* The `->` indirection operator is only enabled if all of the iterators support
    it and return a consistent pointer type.  Otherwise, `pointer` is trivial, and the
    `->` is disabled. */
    template <typename... Ts>
    struct _union_iterator_ptr { using type = meta::as_pointer<union_iterator_ref<Ts...>>; };
    template <meta::has_arrow... Ts>
        requires (meta::has_common_type<typename meta::arrow_type<Ts>...>)
    struct _union_iterator_ptr<Ts...> {
        using type = meta::common_type<typename meta::arrow_type<Ts>...>;
    };
    template <meta::iterator... Ts>
    using union_iterator_ptr = _union_iterator_ptr<Ts...>::type;


    /// TODO: union iterators can probably shave off like 1000+ lines by reusing the
    /// same visitor logic.  That could be done quite easily if impl::union_storage
    /// had a visitable<> specialization, which could also save a couple hundred lines
    /// if all of the impl::visitable<> specializations for the subtypes could reuse
    /// logic and just add the relevant metadata, without rewriting the core dispatch
    /// logic.

    /* A union of iterator types `Ts...`, which attempts to forward their combined
    interface as faithfully as possible.  All operations are enabled if each of the
    underlying types supports them, and will use vtables to exhaustively cover them.
    If any operations would cause the result to narrow to a single type, then that
    type will be returned directly, else it will be returned as a union of types,
    except where otherwise specified.  Iteration performance will be reduced slightly
    due to the extra dynamic dispatch, but should otherwise not degrade functionality
    in any way. */
    template <meta::unqualified... Ts>
        requires (
            (meta::iterator<Ts> && ... && (sizeof...(Ts) > 1)) &&
            meta::has_common_type<meta::iterator_category<Ts>...> &&
            meta::has_common_type<meta::iterator_difference_type<Ts>...>
        )
    struct union_iterator {
        using types = meta::pack<Ts...>;
        using iterator_category = meta::common_type<meta::iterator_category<Ts>...>;
        using difference_type = meta::common_type<meta::iterator_difference_type<Ts>...>;
        using reference = union_iterator_ref<Ts...>;
        using pointer = union_iterator_ptr<Ts...>;
        using value_type = meta::remove_reference<reference>;

        union_storage<Ts..., NoneType> _value;

    private:
        static constexpr bool dereference = (
            requires(meta::as_const_ref<Ts> it) {
                { *it } -> meta::convertible_to<reference>;
            } && ...);
        static constexpr bool nothrow_dereference = (
            requires(meta::as_const_ref<Ts> it) {
                { *it } noexcept -> meta::nothrow::convertible_to<reference>;
            } && ...);

        template <typename... Us>
        static constexpr bool _arrow = false;
        template <meta::has_arrow... Us>
        static constexpr bool _arrow<Us...> = meta::has_common_type<meta::arrow_type<Us>...>;
        static constexpr bool arrow = _arrow<meta::as_const_ref<Ts>...>;
        template <typename... Us>
        static constexpr bool _nothrow_arrow = false;
        template <meta::nothrow::has_arrow... Us>
        static constexpr bool _nothrow_arrow<Us...> =
            meta::nothrow::has_common_type<meta::nothrow::arrow_type<Us>...>;
        static constexpr bool nothrow_arrow =  _nothrow_arrow<meta::as_const_ref<Ts>...>;

        static constexpr bool index = (
            requires(meta::as_const_ref<Ts> it, difference_type n) {
                {it[n]} -> meta::convertible_to<reference>;
            } && ...);
        static constexpr bool nothrow_index = (
            requires(meta::as_const_ref<Ts> it, difference_type n) {
                {it[n]} noexcept -> meta::nothrow::convertible_to<reference>;
            } && ...);

        static constexpr bool increment = (meta::has_preincrement<Ts> && ...);
        static constexpr bool nothrow_increment = (meta::nothrow::has_preincrement<Ts> && ...);

        static constexpr bool forward_add =
            (requires(meta::as_const_ref<Ts> it, difference_type n) {
                {it + n} -> meta::convertible_to<Ts>;
            } && ...);
        static constexpr bool reverse_add =
            (requires(difference_type n, meta::as_const_ref<Ts> it) {
                {n + it} -> meta::convertible_to<Ts>;
            } && ...);
        static constexpr bool nothrow_forward_add =
            (requires(meta::as_const_ref<Ts> it, difference_type n) {
                {it + n} noexcept -> meta::nothrow::convertible_to<Ts>;
            } && ...);
        static constexpr bool nothrow_reverse_add =
            (requires(difference_type n, meta::as_const_ref<Ts> it) {
                {n + it} noexcept -> meta::nothrow::convertible_to<Ts>;
            } && ...);

        static constexpr bool inplace_add = (meta::has_iadd<Ts, difference_type> && ...);
        static constexpr bool nothrow_inplace_add =
            (meta::nothrow::has_iadd<Ts, difference_type> && ...);

        static constexpr bool decrement = (meta::has_predecrement<Ts> && ...);
        static constexpr bool nothrow_decrement = (meta::nothrow::has_predecrement<Ts> && ...);

        static constexpr bool subtract =
            (requires(meta::as_const_ref<Ts> it, difference_type n) {
                {it - n} -> meta::convertible_to<Ts>;
            } && ...);
        static constexpr bool nothrow_subtract =
            (requires(meta::as_const_ref<Ts> it, difference_type n) {
                {it - n} noexcept -> meta::nothrow::convertible_to<Ts>;
            } && ...);

        static constexpr bool inplace_subtract = (meta::has_isub<Ts, difference_type> && ...);
        static constexpr bool nothrow_inplace_subtract =
            (meta::nothrow::has_isub<Ts, difference_type> && ...);

        using deref_ptr = reference(*)(const union_iterator&) noexcept (nothrow_dereference);
        using arrow_ptr = pointer(*)(const union_iterator&) noexcept (nothrow_arrow);
        using index_ptr = reference(*)(const union_iterator&, difference_type)
            noexcept (nothrow_index);
        using increment_ptr = void(*)(union_iterator&) noexcept (nothrow_increment);
        using forward_add_ptr = union_iterator(*)(const union_iterator&, difference_type)
            noexcept (nothrow_forward_add);
        using reverse_add_ptr = union_iterator(*)(difference_type, const union_iterator&)
            noexcept (nothrow_reverse_add);
        using inplace_add_ptr = void(*)(union_iterator&, difference_type)
            noexcept (nothrow_inplace_add);
        using decrement_ptr = void(*)(union_iterator&) noexcept (nothrow_decrement);
        using subtract_ptr = difference_type(*)(const union_iterator&, difference_type)
            noexcept (nothrow_subtract);
        using inplace_subtract_ptr = void(*)(union_iterator&, difference_type)
            noexcept (nothrow_inplace_subtract);

        template <size_t I>
        static constexpr reference deref_fn(const union_iterator& self)
            noexcept (nothrow_dereference)
        {
            return *self._value.template value<I>();
        }

        template <size_t I>
        static constexpr pointer arrow_fn(const union_iterator& self)
            noexcept (nothrow_arrow)
        {
            return std::to_address(self._value.template value<I>());
        }

        template <size_t I>
        static constexpr reference index_fn(const union_iterator& self, difference_type n)
            noexcept (nothrow_index)
        {
            return self._value.template value<I>()[n];
        }

        template <size_t I>
        static constexpr void increment_fn(union_iterator& self)
            noexcept (nothrow_increment)
        {
            ++self._value.template value<I>();
        }

        template <size_t I>
        static constexpr union_iterator forward_add_fn(const union_iterator& self, difference_type n)
            noexcept (nothrow_forward_add)
        {
            return {._value = {union_select<I>{}, self._value.template value<I>() + n}};
        }

        template <size_t I>
        static constexpr union_iterator reverse_add_fn(difference_type n, const union_iterator& self)
            noexcept (nothrow_reverse_add)
        {
            return {._value = {union_select<I>{}, n + self._value.template value<I>()}};
        }

        template <size_t I>
        static constexpr void inplace_add_fn(union_iterator& self, difference_type n)
            noexcept (nothrow_inplace_add)
        {
            self._value.template value<I>() += n;
        }

        template <size_t I>
        static constexpr void decrement_fn(union_iterator& self)
            noexcept (nothrow_decrement)
        {
            --self._value.template value<I>();
        }

        template <size_t I>
        static constexpr union_iterator subtract_fn(const union_iterator& self, difference_type n)
            noexcept (nothrow_subtract)
        {
            return {._value = {union_select<I>{}, self._value.template value<I>() - n}};
        }

        template <size_t I>
        static constexpr void inplace_subtract_fn(union_iterator& self, difference_type n)
            noexcept (nothrow_inplace_subtract)
        {
            self._value.template value<I>() -= n;
        }

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr deref_ptr deref_tbl[0] {};
        template <size_t... Is> requires (dereference)
        static constexpr deref_ptr deref_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &deref_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr arrow_ptr arrow_tbl[0] {};
        template <size_t... Is> requires (arrow)
        static constexpr arrow_ptr arrow_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &arrow_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr index_ptr index_tbl[0] {};
        template <size_t... Is> requires (index)
        static constexpr index_ptr index_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &index_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr increment_ptr increment_tbl[0] {};
        template <size_t... Is> requires (increment)
        static constexpr increment_ptr increment_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &increment_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr forward_add_ptr forward_add_tbl[0] {};
        template <size_t... Is> requires (forward_add)
        static constexpr forward_add_ptr forward_add_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &forward_add_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr reverse_add_ptr reverse_add_tbl[0] {};
        template <size_t... Is> requires (reverse_add)
        static constexpr reverse_add_ptr reverse_add_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &reverse_add_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr inplace_add_ptr inplace_add_tbl[0] {};
        template <size_t... Is> requires (inplace_add)
        static constexpr inplace_add_ptr inplace_add_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &inplace_add_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr decrement_ptr decrement_tbl[0] {};
        template <size_t... Is> requires (decrement)
        static constexpr decrement_ptr decrement_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &decrement_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr subtract_ptr subtract_tbl[0] {};
        template <size_t... Is> requires (subtract)
        static constexpr subtract_ptr subtract_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &subtract_fn<Is>...
        };

        template <typename = std::index_sequence_for<Ts...>>
        static constexpr inplace_subtract_ptr inplace_subtract_tbl[0] {};
        template <size_t... Is> requires (inplace_subtract)
        static constexpr inplace_subtract_ptr inplace_subtract_tbl<std::index_sequence<Is...>>[
            sizeof...(Is)
        ] { &inplace_subtract_fn<Is>... };

        template <size_t I>
        [[gnu::always_inline]] static constexpr reference recursive_deref(
            const union_iterator& self
        ) noexcept (nothrow_dereference) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return deref_fn<I>(self);
                } else {
                    return recursive_deref<I + 1>(self);
                }
            } else {
                return deref_fn<I>(self);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr pointer recursive_arrow(
            const union_iterator& self
        ) noexcept (nothrow_arrow) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return arrow_fn<I>(self);
                } else {
                    return recursive_arrow<I + 1>(self);
                }
            } else {
                return arrow_fn<I>(self);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr reference recursive_index(
            const union_iterator& self
        ) noexcept (nothrow_index) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return index_fn<I>(self, 0);
                } else {
                    return recursive_index<I + 1>(self);
                }
            } else {
                return index_fn<I>(self, 0);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr void recursive_increment(
            union_iterator& self
        ) noexcept (nothrow_increment) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return increment_fn<I>(self);
                } else {
                    return recursive_increment<I + 1>(self);
                }
            } else {
                return increment_fn<I>(self);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr union_iterator recursive_forward_add(
            const union_iterator& self,
            difference_type n
        ) noexcept (nothrow_forward_add) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return forward_add_fn<I>(self, n);
                } else {
                    return recursive_forward_add<I + 1>(self, n);
                }
            } else {
                return forward_add_fn<I>(self, n);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr union_iterator recursive_reverse_add(
            difference_type n,
            const union_iterator& self
        ) noexcept (nothrow_reverse_add) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return reverse_add_fn<I>(n, self);
                } else {
                    return recursive_reverse_add<I + 1>(n, self);
                }
            } else {
                return reverse_add_fn<I>(n, self);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr void recursive_inplace_add(
            union_iterator& self,
            difference_type n
        ) noexcept (nothrow_inplace_add) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return inplace_add_fn<I>(self, n);
                } else {
                    return recursive_inplace_add<I + 1>(self, n);
                }
            } else {
                return inplace_add_fn<I>(self, n);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr void recursive_decrement(
            union_iterator& self
        ) noexcept (nothrow_decrement) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return decrement_fn<I>(self);
                } else {
                    return recursive_decrement<I + 1>(self);
                }
            } else {
                return decrement_fn<I>(self);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr union_iterator recursive_subtract(
            const union_iterator& self,
            difference_type n
        ) noexcept (nothrow_subtract) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return subtract_fn<I>(self, n);
                } else {
                    return recursive_subtract<I + 1>(self, n);
                }
            } else {
                return subtract_fn<I>(self, n);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr void recursive_inplace_subtract(
            union_iterator& self,
            difference_type n
        ) noexcept (nothrow_inplace_subtract) {
            if constexpr ((I + 1) < sizeof...(Ts)) {
                if (self._value.index() == I) {
                    return inplace_subtract_fn<I>(self, n);
                } else {
                    return recursive_inplace_subtract<I + 1>(self, n);
                }
            } else {
                return inplace_subtract_fn<I>(self, n);
            }
        }

        template <meta::unqualified other>
        struct compare {
            static constexpr bool forward_less = (meta::lt_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool nothrow_forward_less = (meta::nothrow::lt_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool reverse_less = (meta::lt_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);
            static constexpr bool nothrow_reverse_less = (meta::nothrow::lt_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);

            static constexpr bool forward_less_equal = (meta::le_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool nothrow_forward_less_equal = (meta::nothrow::le_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool reverse_less_equal = (meta::le_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);
            static constexpr bool nothrow_reverse_less_equal = (meta::nothrow::le_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);

            static constexpr bool forward_equal = (meta::eq_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool nothrow_forward_equal = (meta::nothrow::eq_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool reverse_equal = (meta::eq_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);
            static constexpr bool nothrow_reverse_equal = (meta::nothrow::eq_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);

            static constexpr bool forward_unequal = (meta::ne_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool nothrow_forward_unequal = (meta::nothrow::ne_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool reverse_unequal = (meta::ne_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);
            static constexpr bool nothrow_reverse_unequal = (meta::nothrow::ne_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);

            static constexpr bool forward_greater_equal = (meta::ge_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool nothrow_forward_greater_equal = (meta::nothrow::ge_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool reverse_greater_equal = (meta::ge_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);
            static constexpr bool nothrow_reverse_greater_equal = (meta::nothrow::ge_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);

            static constexpr bool forward_greater = (meta::gt_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool nothrow_forward_greater = (meta::nothrow::gt_returns<
                bool,
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool reverse_greater = (meta::gt_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);
            static constexpr bool nothrow_reverse_greater = (meta::nothrow::gt_returns<
                bool,
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);

            template <typename...>
            static constexpr bool _forward_spaceship = false;
            template <typename... Vs>
                requires (meta::has_spaceship<Vs, meta::as_const_ref<other>> && ...)
            static constexpr bool _forward_spaceship<Vs...> =
                meta::has_common_type<meta::spaceship_type<Vs, meta::as_const_ref<other>>...>;
            static constexpr bool forward_spaceship =
                _forward_spaceship<meta::as_const_ref<Ts>...>;
            template <typename...>
            static constexpr bool _nothrow_forward_spaceship = false;
            template <typename... Vs>
                requires (meta::nothrow::has_spaceship<Vs, meta::as_const_ref<other>> && ...)
            static constexpr bool _nothrow_forward_spaceship<Vs...> =
                meta::nothrow::has_common_type<
                    meta::nothrow::spaceship_type<Vs, meta::as_const_ref<other>>...
                >;
            static constexpr bool nothrow_forward_spaceship =
                _nothrow_forward_spaceship<meta::as_const_ref<Ts>...>;
            template <bool>
            struct _forward_spaceship_type { using type = meta::common_type<
                meta::spaceship_type<meta::as_const_ref<Ts>, meta::as_const_ref<other>>...
            >; };
            template <>
            struct _forward_spaceship_type<false> { using type = void; };
            using forward_spaceship_type = _forward_spaceship_type<forward_spaceship>::type;

            template <typename...>
            static constexpr bool _reverse_spaceship = false;
            template <typename... Vs>
                requires (meta::has_spaceship<meta::as_const_ref<other>, Vs> && ...)
            static constexpr bool _reverse_spaceship<Vs...> =
                meta::has_common_type<meta::spaceship_type<meta::as_const_ref<other>, Vs>...>;
            static constexpr bool reverse_spaceship =
                _reverse_spaceship<meta::as_const_ref<Ts>...>;
            template <typename...>
            static constexpr bool _nothrow_reverse_spaceship = false;
            template <typename... Vs>
                requires (meta::nothrow::has_spaceship<meta::as_const_ref<other>, Vs> && ...)
            static constexpr bool _nothrow_reverse_spaceship<Vs...> =
                meta::nothrow::has_common_type<
                    meta::nothrow::spaceship_type<meta::as_const_ref<other>, Vs>...
                >;
            static constexpr bool nothrow_reverse_spaceship =
                _nothrow_reverse_spaceship<meta::as_const_ref<Ts>...>;
            template <bool>
            struct _reverse_spaceship_type { using type = meta::common_type<
                meta::spaceship_type<meta::as_const_ref<other>, meta::as_const_ref<Ts>>...
            >; };
            template <>
            struct _reverse_spaceship_type<false> { using type = void; };
            using reverse_spaceship_type = _reverse_spaceship_type<reverse_spaceship>::type;

            static constexpr bool forward_distance = (meta::sub_returns<
                difference_type, 
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool nothrow_forward_distance = (meta::nothrow::sub_returns<
                difference_type, 
                meta::as_const_ref<Ts>,
                meta::as_const_ref<other>
            > && ...);
            static constexpr bool reverse_distance = (meta::sub_returns<
                difference_type, 
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);
            static constexpr bool nothrow_reverse_distance = (meta::nothrow::sub_returns<
                difference_type, 
                meta::as_const_ref<other>,
                meta::as_const_ref<Ts>
            > && ...);

            using forward_less_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_less);
            using reverse_less_ptr = bool(*)(const other&, const union_iterator&)
                noexcept (nothrow_reverse_less);
            using forward_less_equal_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_less_equal);
            using reverse_less_equal_ptr = bool(*)(const other&, const union_iterator&)
                noexcept (nothrow_reverse_less_equal);
            using forward_equal_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_equal);
            using reverse_equal_ptr = bool(*)(const other&, const union_iterator&)
                noexcept (nothrow_reverse_equal);
            using forward_unequal_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_unequal);
            using reverse_unequal_ptr = bool(*)(const other&, const union_iterator&)
                noexcept (nothrow_reverse_unequal);
            using forward_greater_equal_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_greater_equal);
            using reverse_greater_equal_ptr = bool(*)(const other&, const union_iterator&)
                noexcept (nothrow_reverse_greater_equal);
            using forward_greater_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_greater);
            using reverse_greater_ptr = bool(*)(const other&, const union_iterator&)
                noexcept (nothrow_reverse_greater);
            using forward_spaceship_ptr = forward_spaceship_type(*)(
                const union_iterator&,
                const other&
            ) noexcept (nothrow_forward_spaceship);
            using reverse_spaceship_ptr = reverse_spaceship_type(*)(
                const other&,
                const union_iterator&
            ) noexcept (nothrow_reverse_spaceship);
            using forward_distance_ptr = difference_type(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_distance);
            using reverse_distance_ptr = difference_type(*)(const other&, const union_iterator&)
                noexcept (nothrow_reverse_distance);

            template <size_t I>
            static constexpr bool forward_less_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_less)
            {
                return lhs._value.template value<I>() < rhs;
            }

            template <size_t I>
            static constexpr bool reverse_less_fn(const other& lhs, const union_iterator& rhs)
                noexcept (nothrow_reverse_less)
            {
                return lhs < rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_less_equal_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_less_equal)
            {
                return lhs._value.template value<I>() <= rhs;
            }

            template <size_t I>
            static constexpr bool reverse_less_equal_fn(const other& lhs, const union_iterator& rhs)
                noexcept (nothrow_reverse_less_equal)
            {
                return lhs <= rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_equal_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_equal)
            {
                return lhs._value.template value<I>() == rhs;
            }

            template <size_t I>
            static constexpr bool reverse_equal_fn(const other& lhs, const union_iterator& rhs)
                noexcept (nothrow_reverse_equal)
            {
                return lhs == rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_unequal_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_unequal)
            {
                return lhs._value.template value<I>() != rhs;
            }

            template <size_t I>
            static constexpr bool reverse_unequal_fn(const other& lhs, const union_iterator& rhs)
                noexcept (nothrow_reverse_unequal)
            {
                return lhs != rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_greater_equal_fn(
                const union_iterator& lhs,
                const other& rhs
            )
                noexcept (nothrow_forward_greater_equal)
            {
                return lhs._value.template value<I>() >= rhs;
            }

            template <size_t I>
            static constexpr bool reverse_greater_equal_fn(
                const other& lhs,
                const union_iterator& rhs
            )
                noexcept (nothrow_reverse_greater_equal)
            {
                return lhs >= rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_greater_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_greater)
            {
                return lhs._value.template value<I>() > rhs;
            }

            template <size_t I>
            static constexpr bool reverse_greater_fn(const other& lhs, const union_iterator& rhs)
                noexcept (nothrow_reverse_greater)
            {
                return lhs > rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr forward_spaceship_type forward_spaceship_fn(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_spaceship) {
                return lhs._value.template value<I>() <=> rhs;
            }

            template <size_t I>
            static constexpr reverse_spaceship_type reverse_spaceship_fn(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_spaceship) {
                return lhs <=> rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr difference_type forward_distance_fn(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_distance) {
                return lhs._value.template value<I>() - rhs;
            }

            template <size_t I>
            static constexpr difference_type reverse_distance_fn(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_distance) {
                return lhs - rhs._value.template value<I>();
            }

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_less_ptr forward_less_tbl[0] {};
            template <size_t... Is> requires (forward_less)
            static constexpr forward_less_ptr forward_less_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_less_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr reverse_less_ptr reverse_less_tbl[0] {};
            template <size_t... Is> requires (reverse_less)
            static constexpr reverse_less_ptr reverse_less_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &reverse_less_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_less_equal_ptr forward_less_equal_tbl[0] {};
            template <size_t... Is> requires (forward_less_equal)
            static constexpr forward_less_equal_ptr forward_less_equal_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &forward_less_equal_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr reverse_less_equal_ptr reverse_less_equal_tbl[0] {};
            template <size_t... Is> requires (reverse_less_equal)
            static constexpr reverse_less_equal_ptr reverse_less_equal_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &reverse_less_equal_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_equal_ptr forward_equal_tbl[0] {};
            template <size_t... Is> requires (forward_equal)
            static constexpr forward_equal_ptr forward_equal_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_equal_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr reverse_equal_ptr reverse_equal_tbl[0] {};
            template <size_t... Is> requires (reverse_equal)
            static constexpr reverse_equal_ptr reverse_equal_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &reverse_equal_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_unequal_ptr forward_unequal_tbl[0] {};
            template <size_t... Is> requires (forward_unequal)
            static constexpr forward_unequal_ptr forward_unequal_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_unequal_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr reverse_unequal_ptr reverse_unequal_tbl[0] {};
            template <size_t... Is> requires (reverse_unequal)
            static constexpr reverse_unequal_ptr reverse_unequal_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &reverse_unequal_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_greater_equal_ptr forward_greater_equal_tbl[0] {};
            template <size_t... Is> requires (forward_greater_equal)
            static constexpr forward_greater_equal_ptr forward_greater_equal_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &forward_greater_equal_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr reverse_greater_equal_ptr reverse_greater_equal_tbl[0] {};
            template <size_t... Is> requires (reverse_greater_equal)
            static constexpr reverse_greater_equal_ptr reverse_greater_equal_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &reverse_greater_equal_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_greater_ptr forward_greater_tbl[0] {};
            template <size_t... Is> requires (forward_greater)
            static constexpr forward_greater_ptr forward_greater_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_greater_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr reverse_greater_ptr reverse_greater_tbl[0] {};
            template <size_t... Is> requires (reverse_greater)
            static constexpr reverse_greater_ptr reverse_greater_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &reverse_greater_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_spaceship_ptr forward_spaceship_tbl[0] {};
            template <size_t... Is> requires (forward_spaceship)
            static constexpr forward_spaceship_ptr forward_spaceship_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &forward_spaceship_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr reverse_spaceship_ptr reverse_spaceship_tbl[0] {};
            template <size_t... Is> requires (reverse_spaceship)
            static constexpr reverse_spaceship_ptr reverse_spaceship_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &reverse_spaceship_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_distance_ptr forward_distance_tbl[0] {};
            template <size_t... Is> requires (forward_distance)
            static constexpr forward_distance_ptr forward_distance_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_distance_fn<Is>...};

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr reverse_distance_ptr reverse_distance_tbl[0] {};
            template <size_t... Is> requires (reverse_distance)
            static constexpr reverse_distance_ptr reverse_distance_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &reverse_distance_fn<Is>... };

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_less(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_less) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_less_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_less<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_less_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_reverse_less(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_less) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (rhs._value.index() == I) {
                        return reverse_less_fn<I>(lhs, rhs);
                    } else {
                        return recursive_reverse_less<I + 1>(lhs, rhs);
                    }
                } else {
                    return reverse_less_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_less_equal(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_less_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_less_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_less_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_less_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_reverse_less_equal(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_less_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (rhs._value.index() == I) {
                        return reverse_less_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_reverse_less_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return reverse_less_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_equal(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_reverse_equal(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (rhs._value.index() == I) {
                        return reverse_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_reverse_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return reverse_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_unequal(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_unequal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_unequal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_unequal<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_unequal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_reverse_unequal(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_unequal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (rhs._value.index() == I) {
                        return reverse_unequal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_reverse_unequal<I + 1>(lhs, rhs);
                    }
                } else {
                    return reverse_unequal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_greater_equal(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_greater_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_greater_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_greater_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_greater_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_reverse_greater_equal(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_greater_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (rhs._value.index() == I) {
                        return reverse_greater_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_reverse_greater_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return reverse_greater_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_greater(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_greater) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_greater_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_greater<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_greater_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_reverse_greater(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_greater) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (rhs._value.index() == I) {
                        return reverse_greater_fn<I>(lhs, rhs);
                    } else {
                        return recursive_reverse_greater<I + 1>(lhs, rhs);
                    }
                } else {
                    return reverse_greater_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr forward_spaceship_type recursive_forward_spaceship(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_spaceship) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_spaceship_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_spaceship<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_spaceship_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr reverse_spaceship_type recursive_reverse_spaceship(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_spaceship) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (rhs._value.index() == I) {
                        return reverse_spaceship_fn<I>(lhs, rhs);
                    } else {
                        return recursive_reverse_spaceship<I + 1>(lhs, rhs);
                    }
                } else {
                    return reverse_spaceship_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr difference_type recursive_forward_distance(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_distance) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_distance_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_distance<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_distance_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr difference_type recursive_reverse_distance(
                const other& lhs,
                const union_iterator& rhs
            ) noexcept (nothrow_reverse_distance) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (rhs._value.index() == I) {
                        return reverse_distance_fn<I>(lhs, rhs);
                    } else {
                        return recursive_reverse_distance<I + 1>(lhs, rhs);
                    }
                } else {
                    return reverse_distance_fn<I>(lhs, rhs);
                }
            }
        };

        template <typename... Us>
        struct compare<union_iterator<Us...>> {
            using other = union_iterator<Us...>;
            static constexpr bool size_match = sizeof...(Ts) == sizeof...(Us);

            static constexpr bool forward_less =
                (size_match && ... && meta::lt_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool nothrow_forward_less =
                (size_match && ... && meta::nothrow::lt_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool reverse_less = false;
            static constexpr bool nothrow_reverse_less = false;

            static constexpr bool forward_less_equal =
                (size_match && ... && meta::le_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool nothrow_forward_less_equal =
                (size_match && ... && meta::nothrow::le_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool reverse_less_equal = false;
            static constexpr bool nothrow_reverse_less_equal = false;

            static constexpr bool forward_equal =
                (size_match && ... && meta::eq_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool nothrow_forward_equal =
                (size_match && ... && meta::nothrow::eq_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool reverse_equal = false;
            static constexpr bool nothrow_reverse_equal = false;

            static constexpr bool forward_unequal =
                (size_match && ... && meta::ne_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool nothrow_forward_unequal =
                (size_match && ... && meta::nothrow::ne_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool reverse_unequal = false;
            static constexpr bool nothrow_reverse_unequal = false;

            static constexpr bool forward_greater_equal =
                (size_match && ... && meta::ge_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool nothrow_forward_greater_equal =
                (size_match && ... && meta::nothrow::ge_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool reverse_greater_equal = false;
            static constexpr bool nothrow_reverse_greater_equal = false;

            static constexpr bool forward_greater =
                (size_match && ... && meta::gt_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool nothrow_forward_greater =
                (size_match && ... && meta::nothrow::gt_returns<
                    bool,
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool reverse_greater = false;
            static constexpr bool nothrow_reverse_greater = false;

            template <typename...>
            static constexpr bool _forward_spaceship = false;
            template <typename... Vs>
                requires (size_match && ... && meta::has_spaceship<Vs, meta::as_const_ref<Us>>)
            static constexpr bool _forward_spaceship<Vs...> =
                meta::has_common_type<meta::spaceship_type<Vs, meta::as_const_ref<Us>>...>;
            static constexpr bool forward_spaceship =
                _forward_spaceship<meta::as_const_ref<Ts>...>;
            template <typename...>
            static constexpr bool _nothrow_forward_spaceship = false;
            template <typename... Vs>
                requires (
                    size_match &&
                    ... &&
                    meta::nothrow::has_spaceship<Vs, meta::as_const_ref<Us>>
                )
            static constexpr bool _nothrow_forward_spaceship<Vs...> =
                meta::nothrow::has_common_type<
                    meta::nothrow::spaceship_type<Vs, meta::as_const_ref<Us>>...
                >;
            static constexpr bool nothrow_forward_spaceship =
                _nothrow_forward_spaceship<meta::as_const_ref<Ts>...>;
            static constexpr bool reverse_spaceship = false;
            static constexpr bool nothrow_reverse_spaceship = false;
            template <bool>
            struct _forward_spaceship_type { using type = meta::common_type<
                meta::spaceship_type<meta::as_const_ref<Ts>, meta::as_const_ref<Us>>...
            >; };
            template <>
            struct _forward_spaceship_type<false> { using type = void; };
            using forward_spaceship_type = _forward_spaceship_type<forward_spaceship>::type;

            static constexpr bool forward_distance =
                (size_match && ... && meta::sub_returns<
                    difference_type, 
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool nothrow_forward_distance =
                (size_match && ... && meta::nothrow::sub_returns<
                    difference_type, 
                    meta::as_const_ref<Ts>,
                    meta::as_const_ref<Us>
                >);
            static constexpr bool reverse_distance = false;
            static constexpr bool nothrow_reverse_distance = false;

            using forward_less_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_less);
            using forward_less_equal_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_less_equal);
            using forward_equal_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_equal);
            using forward_unequal_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_unequal);
            using forward_greater_equal_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_greater_equal);
            using forward_greater_ptr = bool(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_greater);
            using forward_spaceship_ptr = forward_spaceship_type(*)(
                const union_iterator&,
                const other&
            ) noexcept (nothrow_forward_spaceship);
            using forward_distance_ptr = difference_type(*)(const union_iterator&, const other&)
                noexcept (nothrow_forward_distance);

            template <size_t I>
            static constexpr bool forward_less_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_less)
            {
                return lhs._value.template value<I>() < rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_less_equal_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_less_equal)
            {
                return lhs._value.template value<I>() <= rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_equal_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_equal)
            {
                return lhs._value.template value<I>() == rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_unequal_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_unequal)
            {
                return lhs._value.template value<I>() != rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_greater_equal_fn(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_greater_equal) {
                return lhs._value.template value<I>() >= rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr bool forward_greater_fn(const union_iterator& lhs, const other& rhs)
                noexcept (nothrow_forward_greater)
            {
                return lhs._value.template value<I>() > rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr forward_spaceship_type forward_spaceship_fn(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_spaceship) {
                return lhs._value.template value<I>() <=> rhs._value.template value<I>();
            }

            template <size_t I>
            static constexpr difference_type forward_distance_fn(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_distance) {
                return lhs._value.template value<I>() - rhs._value.template value<I>();
            }

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_less_ptr forward_less_tbl[0] {};
            template <size_t... Is> requires (forward_less)
            static constexpr forward_less_ptr forward_less_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_less_fn<Is>... };

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_less_equal_ptr forward_less_equal_tbl[0] {};
            template <size_t... Is> requires (forward_less_equal)
            static constexpr forward_less_equal_ptr forward_less_equal_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &forward_less_equal_fn<Is>... };

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_equal_ptr forward_equal_tbl[0] {};
            template <size_t... Is> requires (forward_equal)
            static constexpr forward_equal_ptr forward_equal_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_equal_fn<Is>... };

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_unequal_ptr forward_unequal_tbl[0] {};
            template <size_t... Is> requires (forward_unequal)
            static constexpr forward_unequal_ptr forward_unequal_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_unequal_fn<Is>... };

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_greater_equal_ptr forward_greater_equal_tbl[0] {};
            template <size_t... Is> requires (forward_greater_equal)
            static constexpr forward_greater_equal_ptr forward_greater_equal_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &forward_greater_equal_fn<Is>... };

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_greater_ptr forward_greater_tbl[0] {};
            template <size_t... Is> requires (forward_greater)
            static constexpr forward_greater_ptr forward_greater_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_greater_fn<Is>... };

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_spaceship_ptr forward_spaceship_tbl[0] {};
            template <size_t... Is> requires (forward_spaceship)
            static constexpr forward_spaceship_ptr forward_spaceship_tbl<
                std::index_sequence<Is...>
            >[sizeof...(Is)] { &forward_spaceship_fn<Is>... };

            template <typename = std::index_sequence_for<Ts...>>
            static constexpr forward_distance_ptr forward_distance_tbl[0] {};
            template <size_t... Is> requires (forward_distance)
            static constexpr forward_distance_ptr forward_distance_tbl<std::index_sequence<Is...>>[
                sizeof...(Is)
            ] { &forward_distance_fn<Is>... };

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_less(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_less) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_less_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_less<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_less_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_less_equal(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_less_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_less_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_less_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_less_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_equal(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_unequal(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_unequal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_unequal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_unequal<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_unequal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_greater_equal(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_greater_equal) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_greater_equal_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_greater_equal<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_greater_equal_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr bool recursive_forward_greater(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_greater) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_greater_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_greater<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_greater_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr forward_spaceship_type recursive_forward_spaceship(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_spaceship) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_spaceship_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_spaceship<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_spaceship_fn<I>(lhs, rhs);
                }
            }

            template <size_t I>
            [[gnu::always_inline]] static constexpr difference_type recursive_forward_distance(
                const union_iterator& lhs,
                const other& rhs
            ) noexcept (nothrow_forward_distance) {
                if constexpr ((I + 1) < sizeof...(Ts)) {
                    if (lhs._value.index() == I) {
                        return forward_distance_fn<I>(lhs, rhs);
                    } else {
                        return recursive_forward_distance<I + 1>(lhs, rhs);
                    }
                } else {
                    return forward_distance_fn<I>(lhs, rhs);
                }
            }
        };

    public:
        [[nodiscard]] constexpr reference operator*() const
            noexcept (nothrow_dereference)
            requires (dereference)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return deref_tbl<>[_value.index()](*this);
            } else {
                return recursive_deref<0>(*this);
            }
        }

        [[nodiscard]] constexpr pointer operator->() const
            noexcept (nothrow_arrow)
            requires (arrow)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return arrow_tbl<>[_value.index()](*this);
            } else {
                return recursive_arrow<0>(*this);
            }
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const
            noexcept (nothrow_index)
            requires (index)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return index_tbl<>[_value.index()](*this, n);
            } else {
                return recursive_index<0>(*this, n);
            }
        }

        constexpr union_iterator& operator++()
            noexcept (nothrow_increment)
            requires (increment)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                increment_tbl<>[_value.index()](*this);
            } else {
                recursive_increment<0>(*this);
            }
            return *this;
        }

        [[nodiscard]] constexpr union_iterator operator++(int)
            noexcept (meta::nothrow::copyable<union_iterator> && requires{{++*this} noexcept;})
            requires (meta::copyable<union_iterator> && requires{{++*this};})
        {
            union_iterator tmp(*this);
            ++*this;
            return tmp;
        }

        [[nodiscard]] friend constexpr union_iterator operator+(
            const union_iterator& self,
            difference_type n
        )
            noexcept (nothrow_forward_add)
            requires (forward_add)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return forward_add_tbl<>[self._value.index()](self, n);
            } else {
                return recursive_forward_add<0>(self, n);
            }
        }

        [[nodiscard]] friend constexpr union_iterator operator+(
            difference_type n,
            const union_iterator& self
        )
            noexcept (nothrow_reverse_add)
            requires (reverse_add)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return reverse_add_tbl<>[self._value.index()](n, self);
            } else {
                return recursive_reverse_add<0>(n, self);
            }
        }

        constexpr union_iterator& operator+=(difference_type n)
            noexcept (nothrow_inplace_add)
            requires (inplace_add)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                inplace_add_tbl<>[_value.index()](*this, n);
            } else {
                recursive_inplace_add<0>(*this, n);
            }
            return *this;
        }

        constexpr union_iterator& operator--()
            noexcept (nothrow_decrement)
            requires (decrement)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                decrement_tbl<>[_value.index()](*this);
            } else {
                recursive_decrement<0>(*this);
            }
            return *this;
        }

        [[nodiscard]] constexpr union_iterator operator--(int)
            noexcept (meta::nothrow::copyable<union_iterator> && requires{{--*this} noexcept;})
            requires (meta::copyable<union_iterator> && requires{{--*this};})
        {
            union_iterator tmp(*this);
            --*this;
            return tmp;
        }

        [[nodiscard]] friend constexpr union_iterator operator-(
            const union_iterator& self,
            difference_type n
        )
            noexcept (nothrow_subtract)
            requires (subtract)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return subtract_tbl<>[self._value.index()](self, n);
            } else {
                return recursive_subtract<0>(self, n);
            }
        }

        constexpr union_iterator& operator-=(difference_type n)
            noexcept (nothrow_inplace_subtract)
            requires (inplace_subtract)
        {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                inplace_subtract_tbl<>[_value.index()](*this, n);
            } else {
                recursive_inplace_subtract<0>(*this, n);
            }
            return *this;
        }

        template <typename other> requires (compare<other>::forward_distance)
        [[nodiscard]] friend constexpr difference_type operator-(
            const union_iterator& lhs,
            const other& rhs
        ) noexcept (compare<other>::nothrow_forward_distance) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template forward_distance_tbl<>[
                    lhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_forward_distance<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::reverse_distance)
        [[nodiscard]] friend constexpr difference_type operator-(
            const other& lhs,
            const union_iterator& rhs
        ) noexcept (compare<other>::nothrow_reverse_distance) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template reverse_distance_tbl<>[
                    rhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_reverse_distance<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::forward_less)
        [[nodiscard]] friend constexpr bool operator<(
            const union_iterator& lhs,
            const other& rhs
        ) noexcept (compare<other>::nothrow_forward_less) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template forward_less_tbl<>[
                    lhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_forward_less<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::reverse_less)
        [[nodiscard]] friend constexpr bool operator<(
            const other& lhs,
            const union_iterator& rhs
        ) noexcept (compare<other>::nothrow_reverse_less) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template reverse_less_tbl<>[
                    rhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_reverse_less<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::forward_less_equal)
        [[nodiscard]] friend constexpr bool operator<=(
            const union_iterator& lhs,
            const other& rhs
        ) noexcept (compare<other>::nothrow_forward_less_equal) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template forward_less_equal_tbl<>[
                    lhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_forward_less_equal<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::reverse_less_equal)
        [[nodiscard]] friend constexpr bool operator<=(
            const other& lhs,
            const union_iterator& rhs
        ) noexcept (compare<other>::nothrow_reverse_less_equal) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template reverse_less_equal_tbl<>[
                    rhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_reverse_less_equal<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::forward_equal)
        [[nodiscard]] friend constexpr bool operator==(
            const union_iterator& lhs,
            const other& rhs
        ) noexcept (compare<other>::nothrow_forward_equal) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template forward_equal_tbl<>[
                    lhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_forward_equal<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::reverse_equal)
        [[nodiscard]] friend constexpr bool operator==(
            const other& lhs,
            const union_iterator& rhs
        ) noexcept (compare<other>::nothrow_reverse_equal) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template reverse_equal_tbl<>[
                    rhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_reverse_equal<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::forward_unequal)
        [[nodiscard]] friend constexpr bool operator!=(
            const union_iterator& lhs,
            const other& rhs
        ) noexcept (compare<other>::nothrow_forward_unequal) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template forward_unequal_tbl<>[
                    lhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_forward_unequal<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::reverse_unequal)
        [[nodiscard]] friend constexpr bool operator!=(
            const other& lhs,
            const union_iterator& rhs
        ) noexcept (compare<other>::nothrow_reverse_unequal) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template reverse_unequal_tbl<>[
                    rhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_reverse_unequal<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::forward_greater_equal)
        [[nodiscard]] friend constexpr bool operator>=(
            const union_iterator& lhs,
            const other& rhs
        ) noexcept (compare<other>::nothrow_forward_greater_equal) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template forward_greater_equal_tbl<>[
                    lhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_forward_greater_equal<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::reverse_greater_equal)
        [[nodiscard]] friend constexpr bool operator>=(
            const other& lhs,
            const union_iterator& rhs
        ) noexcept (compare<other>::nothrow_reverse_greater_equal) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template reverse_greater_equal_tbl<>[
                    rhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_reverse_greater_equal<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::forward_greater)
        [[nodiscard]] friend constexpr bool operator>(
            const union_iterator& lhs,
            const other& rhs
        ) noexcept (compare<other>::nothrow_forward_greater) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template forward_greater_tbl<>[
                    lhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_forward_greater<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::reverse_greater)
        [[nodiscard]] friend constexpr bool operator>(
            const other& lhs,
            const union_iterator& rhs
        ) noexcept (compare<other>::nothrow_reverse_greater) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return compare<other>::template reverse_greater_tbl<>[
                    rhs._value.index()
                ](lhs, rhs);
            } else {
                return compare<other>::template recursive_reverse_greater<0>(lhs, rhs);
            }
        }

        template <typename other> requires (compare<other>::forward_spaceship)
        [[nodiscard]] friend constexpr decltype(auto) operator<=>(
            const union_iterator& lhs,
            const other& rhs
        ) noexcept (compare<other>::nothrow_forward_spaceship) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return (compare<other>::template forward_spaceship_tbl<>[
                    lhs._value.index()
                ](lhs, rhs));
            } else {
                return (compare<other>::template recursive_forward_spaceship<0>(lhs, rhs));
            }
        }

        template <typename other> requires (compare<other>::reverse_spaceship)
        [[nodiscard]] friend constexpr decltype(auto) operator<=>(
            const other& lhs,
            const union_iterator& rhs
        ) noexcept (compare<other>::nothrow_reverse_spaceship) {
            if constexpr (sizeof...(Ts) >= MIN_VTABLE_SIZE) {
                return (compare<other>::template reverse_spaceship_tbl<>[
                    rhs._value.index()
                ](lhs, rhs));
            } else {
                return (compare<other>::template recursive_reverse_spaceship<0>(lhs, rhs));
            }
        }
    };

    /* `make_union_iterator<Ts...>` accepts a union of types `Ts...` and composes a set
    of iterators over it, which can be used to traverse the union, possibly yielding
    further unions.  If all types share the same iterator type, then the iterator will
    be returned directly.  Otherwise, a `union_iterator<Is...>` will be returned, where
    `Is...` are the (index-aligned) iterator types that were detected. */
    template <size_t, typename...>
    struct _make_union_iterator;
    template <size_t N, typename U, typename... B, typename... E, typename... RB, typename... RE>
    struct _make_union_iterator<
        N,
        U,
        meta::pack<B...>,
        meta::pack<E...>,
        meta::pack<RB...>,
        meta::pack<RE...>
    > {
    private:
        template <typename unique, typename... S>
        struct _iter {
            static constexpr bool direct = false;
            using type = union_iterator<S...>;
        };
        template <typename T, typename... S>
        struct _iter<meta::pack<T>, S...> {
            static constexpr bool direct = true;
            using type = T;
        };
        template <typename... S>
        struct _iter<meta::pack<>, S...> { using type = void; };
        template <typename... S>
        using iter = _iter<meta::to_unique<S...>, S...>;

        template <typename = std::make_index_sequence<N>>
        struct _size_type { using type = void; };
        template <size_t... Is>
            requires (meta::has_size<decltype(std::declval<U>()._value.template value<Is>())> && ...)
        struct _size_type<std::index_sequence<Is...>> {
            using type = meta::common_type<
                meta::size_type<decltype(std::declval<U>()._value.template value<Is>())>...
            >;
        };

        template <typename = std::make_index_sequence<N>>
        struct _ssize_type { using type = void; };
        template <size_t... Is>
            requires (meta::has_ssize<decltype(std::declval<U>()._value.template value<Is>())> && ...)
        struct _ssize_type<std::index_sequence<Is...>> {
            using type = meta::common_type<
                meta::ssize_type<decltype(std::declval<U>()._value.template value<Is>())>...
            >;
        };

    public:
        using begin_type = iter<B...>::type;
        using end_type = iter<E...>::type;
        using rbegin_type = iter<RB...>::type;
        using rend_type = iter<RE...>::type;
        using size_type = _size_type<>::type;
        using ssize_type = _ssize_type<>::type;

    private:
        template <typename = std::make_index_sequence<N>>
        static constexpr bool _enable_begin = false;
        template <size_t... Is> requires (sizeof...(B) == sizeof...(Is) && iter<B...>::direct)
        static constexpr bool _enable_begin<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::begin(u._value.template value<Is>())
        } -> meta::convertible_to<begin_type>;} && ...);
        template <size_t... Is> requires (sizeof...(B) == sizeof...(Is) && !iter<B...>::direct)
        static constexpr bool _enable_begin<std::index_sequence<Is...>> = (requires(U u) {{
            begin_type{{union_select<Is>{}, std::ranges::begin(u._value.template value<Is>())}}
        };} && ...);
        static constexpr bool enable_begin = _enable_begin<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _nothrow_begin = false;
        template <size_t... Is> requires (sizeof...(B) == sizeof...(Is) && iter<B...>::direct)
        static constexpr bool _nothrow_begin<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::begin(u._value.template value<Is>())
        } noexcept -> meta::nothrow::convertible_to<begin_type>;} && ...);
        template <size_t... Is> requires (sizeof...(B) == sizeof...(Is) && !iter<B...>::direct)
        static constexpr bool _nothrow_begin<std::index_sequence<Is...>> = (requires(U u) {{
            begin_type{{union_select<Is>{}, std::ranges::begin(u._value.template value<Is>())}}
        } noexcept;} && ...);
        static constexpr bool nothrow_begin = _nothrow_begin<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _enable_end = false;
        template <size_t... Is> requires (sizeof...(E) == sizeof...(Is) && iter<E...>::direct)
        static constexpr bool _enable_end<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::end(u._value.template value<Is>())
        } -> meta::convertible_to<end_type>;} && ...);
        template <size_t... Is> requires (sizeof...(E) == sizeof...(Is) && !iter<E...>::direct)
        static constexpr bool _enable_end<std::index_sequence<Is...>> = (requires(U u) {{
            end_type{{union_select<Is>{}, std::ranges::end(u._value.template value<Is>())}}
        };} && ...);
        static constexpr bool enable_end = _enable_end<>;
        
        template <typename = std::make_index_sequence<N>>
        static constexpr bool _nothrow_end = false;
        template <size_t... Is> requires (sizeof...(E) == sizeof...(Is) && iter<E...>::direct)
        static constexpr bool _nothrow_end<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::end(u._value.template value<Is>())
        } noexcept -> meta::nothrow::convertible_to<end_type>;} && ...);
        template <size_t... Is> requires (sizeof...(E) == sizeof...(Is) && !iter<E...>::direct)
        static constexpr bool _nothrow_end<std::index_sequence<Is...>> = (requires(U u) {{
            end_type{{union_select<Is>{}, std::ranges::end(u._value.template value<Is>())}}
        } noexcept;} && ...);
        static constexpr bool nothrow_end = _nothrow_end<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _enable_rbegin = false;
        template <size_t... Is> requires (sizeof...(RB) == sizeof...(Is) && iter<RB...>::direct)
        static constexpr bool _enable_rbegin<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::rbegin(u._value.template value<Is>())
        } -> meta::convertible_to<rbegin_type>;} && ...);
        template <size_t... Is> requires (sizeof...(RB) == sizeof...(Is) && !iter<RB...>::direct)
        static constexpr bool _enable_rbegin<std::index_sequence<Is...>> = (requires(U u) {{
            rbegin_type{{union_select<Is>{}, std::ranges::rbegin(u._value.template value<Is>())}}
        };} && ...);
        static constexpr bool enable_rbegin = _enable_rbegin<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _nothrow_rbegin = false;
        template <size_t... Is> requires (sizeof...(RB) == sizeof...(Is) && iter<RB...>::direct)
        static constexpr bool _nothrow_rbegin<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::rbegin(u._value.template value<Is>())
        } noexcept -> meta::nothrow::convertible_to<rbegin_type>;} && ...);
        template <size_t... Is> requires (sizeof...(RB) == sizeof...(Is) && !iter<RB...>::direct)
        static constexpr bool _nothrow_rbegin<std::index_sequence<Is...>> = (requires(U u) {{
            rbegin_type{{union_select<Is>{}, std::ranges::rbegin(u._value.template value<Is>())}}
        } noexcept;} && ...);
        static constexpr bool nothrow_rbegin = _nothrow_rbegin<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _enable_rend = false;
        template <size_t... Is> requires (sizeof...(RE) == sizeof...(Is) && iter<RE...>::direct)
        static constexpr bool _enable_rend<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::rend(u._value.template value<Is>())
        } -> meta::convertible_to<rend_type>;} && ...);
        template <size_t... Is> requires (sizeof...(RE) == sizeof...(Is) && !iter<RE...>::direct)
        static constexpr bool _enable_rend<std::index_sequence<Is...>> = (requires(U u) {{
            rend_type{{union_select<Is>{}, std::ranges::rend(u._value.template value<Is>())}}
        };} && ...);
        static constexpr bool enable_rend = _enable_rend<>;
        
        template <typename = std::make_index_sequence<N>>
        static constexpr bool _nothrow_rend = false;
        template <size_t... Is> requires (sizeof...(RE) == sizeof...(Is) && iter<RE...>::direct)
        static constexpr bool _nothrow_rend<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::rend(u._value.template value<Is>())
        } noexcept -> meta::nothrow::convertible_to<rend_type>;} && ...);
        template <size_t... Is> requires (sizeof...(RE) == sizeof...(Is) && !iter<RE...>::direct)
        static constexpr bool _nothrow_rend<std::index_sequence<Is...>> = (requires(U u) {{
            rend_type{{union_select<Is>{}, std::ranges::rend(u._value.template value<Is>())}}
        } noexcept;} && ...);
        static constexpr bool nothrow_rend = _nothrow_rend<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _enable_size = false;
        template <size_t... Is>
        static constexpr bool _enable_size<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::size(u._value.template value<Is>())
        } -> meta::convertible_to<size_type>;} && ...);
        static constexpr bool enable_size = _enable_size<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _nothrow_size = false;
        template <size_t... Is>
        static constexpr bool _nothrow_size<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::size(u._value.template value<Is>())
        } noexcept -> meta::nothrow::convertible_to<size_type>;} && ...);
        static constexpr bool nothrow_size = _nothrow_size<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _enable_ssize = false;
        template <size_t... Is>
        static constexpr bool _enable_ssize<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::ssize(u._value.template value<Is>())
        } -> meta::convertible_to<ssize_type>;} && ...);
        static constexpr bool enable_ssize = _enable_ssize<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _nothrow_ssize = false;
        template <size_t... Is>
        static constexpr bool _nothrow_ssize<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::ssize(u._value.template value<Is>())
        } noexcept -> meta::nothrow::convertible_to<ssize_type>;} && ...);
        static constexpr bool nothrow_ssize = _nothrow_ssize<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _enable_empty = false;
        template <size_t... Is>
        static constexpr bool _enable_empty<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::empty(u._value.template value<Is>())
        } -> meta::convertible_to<bool>;} && ...);
        static constexpr bool enable_empty = _enable_empty<>;

        template <typename = std::make_index_sequence<N>>
        static constexpr bool _nothrow_empty = false;
        template <size_t... Is>
        static constexpr bool _nothrow_empty<std::index_sequence<Is...>> = (requires(U u) {{
            std::ranges::empty(u._value.template value<Is>())
        } noexcept -> meta::nothrow::convertible_to<bool>;} && ...);
        static constexpr bool nothrow_empty = _nothrow_empty<>;

        using begin_ptr = begin_type(*)(U) noexcept (nothrow_begin);
        using end_ptr = end_type(*)(U) noexcept (nothrow_end);
        using rbegin_ptr = rbegin_type(*)(U) noexcept (nothrow_rbegin);
        using rend_ptr = rend_type(*)(U) noexcept (nothrow_rend);
        using size_ptr = size_type(*)(U) noexcept (nothrow_size);
        using ssize_ptr = ssize_type(*)(U) noexcept (nothrow_ssize);
        using empty_ptr = bool(*)(U) noexcept (nothrow_empty);

        template <size_t I>
        static constexpr begin_type begin_fn(U u) noexcept (nothrow_begin) {
            if constexpr (iter<B...>::direct) {
                return std::ranges::begin(u._value.template value<I>());
            } else {
                return {{union_select<I>{}, std::ranges::begin(u._value.template value<I>())}};
            }
        }

        template <size_t I>
        static constexpr end_type end_fn(U u) noexcept (nothrow_end) {
            if constexpr (iter<E...>::direct) {
                return std::ranges::end(u._value.template value<I>());
            } else {
                return {{union_select<I>{}, std::ranges::end(u._value.template value<I>())}};
            }
        }

        template <size_t I>
        static constexpr rbegin_type rbegin_fn(U u) noexcept (nothrow_rbegin) {
            if constexpr (iter<RB...>::direct) {
                return std::ranges::rbegin(u._value.template value<I>());
            } else {
                return {{union_select<I>{}, std::ranges::rbegin(u._value.template value<I>())}};
            }
        }

        template <size_t I>
        static constexpr rend_type rend_fn(U u) noexcept (nothrow_rend) {
            if constexpr (iter<RE...>::direct) {
                return std::ranges::rend(u._value.template value<I>());
            } else {
                return {{union_select<I>{}, std::ranges::rend(u._value.template value<I>())}};
            }
        }

        template <size_t I>
        static constexpr size_type size_fn(U u) noexcept (nothrow_size) {
            if constexpr (meta::has_size<decltype(u._value.template value<I>())>) {
                return std::ranges::size(u._value.template value<I>());
            } else {
                return 0;
            }
        }

        template <size_t I>
        static constexpr ssize_type ssize_fn(U u) noexcept (nothrow_ssize) {
            if constexpr (meta::has_ssize<decltype(u._value.template value<I>())>) {
                return std::ranges::ssize(u._value.template value<I>());
            } else {
                return 0;
            }
        }

        template <size_t I>
        static constexpr bool empty_fn(U u) noexcept (nothrow_empty) {
            if constexpr (meta::has_empty<decltype(u._value.template value<I>())>) {
                return std::ranges::empty(u._value.template value<I>());
            } else {
                return true;
            }
        }

        template <typename = std::make_index_sequence<N>>
        static constexpr begin_ptr begin_tbl[0] {};
        template <size_t... Is> requires (enable_begin)
        static constexpr begin_ptr begin_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &begin_fn<Is>...
        };

        template <typename = std::make_index_sequence<N>>
        static constexpr end_ptr end_tbl[0] {};
        template <size_t... Is> requires (enable_end)
        static constexpr end_ptr end_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &end_fn<Is>...
        };

        template <typename = std::make_index_sequence<N>>
        static constexpr rbegin_ptr rbegin_tbl[0] {};
        template <size_t... Is> requires (enable_rbegin)
        static constexpr rbegin_ptr rbegin_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &rbegin_fn<Is>...
        };

        template <typename = std::make_index_sequence<N>>
        static constexpr rend_ptr rend_tbl[0] {};
        template <size_t... Is> requires (enable_rend)
        static constexpr rend_ptr rend_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &rend_fn<Is>...
        };

        template <typename = std::make_index_sequence<N>>
        static constexpr size_ptr size_tbl[0] {};
        template <size_t... Is>
        static constexpr size_ptr size_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &size_fn<Is>...
        };

        template <typename = std::make_index_sequence<N>>
        static constexpr ssize_ptr ssize_tbl[0] {};
        template <size_t... Is>
        static constexpr ssize_ptr ssize_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            &ssize_fn<Is>...
        };

        template <typename = std::make_index_sequence<N>>
        static constexpr empty_ptr empty_tbl[0] {};
        template <size_t... Is>
        static constexpr empty_ptr empty_tbl<std::index_sequence<Is...>>[sizeof...(Is)] {
            empty_fn<Is>...
        };

        template <size_t I>
        [[gnu::always_inline]] static constexpr begin_type recursive_begin(U u)
            noexcept (nothrow_begin)
        {
            if constexpr ((I + 1) < N) {
                if (u._value.index() == I) {
                    return begin_fn<I>(u);
                } else {
                    return recursive_begin<I + 1>(u);
                }
            } else {
                return begin_fn<I>(u);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr end_type recursive_end(U u)
            noexcept (nothrow_end)
        {
            if constexpr ((I + 1) < N) {
                if (u._value.index() == I) {
                    return end_fn<I>(u);
                } else {
                    return recursive_end<I + 1>(u);
                }
            } else {
                return end_fn<I>(u);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr rbegin_type recursive_rbegin(U u)
            noexcept (nothrow_rbegin)
        {
            if constexpr ((I + 1) < N) {
                if (u._value.index() == I) {
                    return rbegin_fn<I>(u);
                } else {
                    return recursive_rbegin<I + 1>(u);
                }
            } else {
                return rbegin_fn<I>(u);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr rend_type recursive_rend(U u)
            noexcept (nothrow_rend)
        {
            if constexpr ((I + 1) < N) {
                if (u._value.index() == I) {
                    return rend_fn<I>(u);
                } else {
                    return recursive_rend<I + 1>(u);
                }
            } else {
                return rend_fn<I>(u);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr size_type recursive_size(U u)
            noexcept (nothrow_size)
        {
            if constexpr ((I + 1) < N) {
                if (u._value.index() == I) {
                    return size_fn<I>(u);
                } else {
                    return recursive_size<I + 1>(u);
                }
            } else {
                return size_fn<I>(u);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr ssize_type recursive_ssize(U u)
            noexcept (nothrow_ssize)
        {
            if constexpr ((I + 1) < N) {
                if (u._value.index() == I) {
                    return ssize_fn<I>(u);
                } else {
                    return recursive_ssize<I + 1>(u);
                }
            } else {
                return ssize_fn<I>(u);
            }
        }

        template <size_t I>
        [[gnu::always_inline]] static constexpr bool recursive_empty(U u)
            noexcept (nothrow_empty)
        {
            if constexpr ((I + 1) < N) {
                if (u._value.index() == I) {
                    return empty_fn<I>(u);
                } else {
                    return recursive_empty<I + 1>(u);
                }
            } else {
                return empty_fn<I>(u);
            }
        }

    public:
        static constexpr begin_type begin(U u)
            noexcept (nothrow_begin)
            requires (enable_begin)
        {
            if constexpr (N >= MIN_VTABLE_SIZE) {
                return begin_tbl<>[u._value.index()](u);
            } else {
                return recursive_begin<0>(u);
            }
        }

        static constexpr end_type end(U u)
            noexcept (nothrow_end)
            requires (enable_end)
        {
            if constexpr (N >= MIN_VTABLE_SIZE) {
                return end_tbl<>[u._value.index()](u);
            } else {
                return recursive_end<0>(u);
            }
        }

        static constexpr rbegin_type rbegin(U u)
            noexcept (nothrow_rbegin)
            requires (enable_rbegin)
        {
            if constexpr (N >= MIN_VTABLE_SIZE) {
                return rbegin_tbl<>[u._value.index()](u);
            } else {
                return recursive_rbegin<0>(u);
            }
        }

        static constexpr rend_type rend(U u)
            noexcept (nothrow_rend)
            requires (enable_rend)
        {
            if constexpr (N >= MIN_VTABLE_SIZE) {
                return rend_tbl<>[u._value.index()](u);
            } else {
                return recursive_rend<0>(u);
            }
        }

        static constexpr size_type size(U u)
            noexcept (nothrow_size)
            requires (enable_size)
        {
            if constexpr (N >= MIN_VTABLE_SIZE) {
                return size_tbl<>[u._value.index()](u);
            } else {
                return recursive_size<0>(u);
            }
        }

        static constexpr ssize_type ssize(U u)
            noexcept (nothrow_ssize)
            requires (enable_ssize)
        {
            if constexpr (N >= MIN_VTABLE_SIZE) {
                return ssize_tbl<>[u._value.index()](u);
            } else {
                return recursive_ssize<0>(u);
            }
        }

        static constexpr bool empty(U u)
            noexcept (nothrow_empty)
            requires (enable_empty)
        {
            if constexpr (N >= MIN_VTABLE_SIZE) {
                return empty_tbl<>[u._value.index()](u);
            } else {
                return recursive_empty<0>(u);
            }
        }
    };
    template <
        size_t I,
        typename U,
        typename... begin,
        typename... end,
        typename... rbegin,
        typename... rend
    > requires (
        I < meta::remove_reference<U>::types::size() &&
        meta::iterable<decltype(std::declval<U>()._value.template value<I>())> &&
        meta::reverse_iterable<decltype(std::declval<U>()._value.template value<I>())>
    )
    struct _make_union_iterator<
        I,
        U,
        meta::pack<begin...>,
        meta::pack<end...>,
        meta::pack<rbegin...>,
        meta::pack<rend...>
    > : _make_union_iterator<
        I + 1,
        U,
        meta::pack<begin..., meta::unqualify<meta::begin_type<
            decltype(std::declval<U>()._value.template value<I>())
        >>>,
        meta::pack<end..., meta::unqualify<meta::end_type<
            decltype(std::declval<U>()._value.template value<I>())
        >>>,
        meta::pack<rbegin..., meta::unqualify<meta::rbegin_type<
            decltype(std::declval<U>()._value.template value<I>())
        >>>,
        meta::pack<rend..., meta::unqualify<meta::rend_type<
            decltype(std::declval<U>()._value.template value<I>())
        >>>
    > {};
    template <
        size_t I,
        typename U,
        typename... begin,
        typename... end,
        typename... rbegin,
        typename... rend
    > requires (
        I < meta::remove_reference<U>::types::size() &&
        meta::iterable<decltype(std::declval<U>()._value.template value<I>())> &&
        !meta::reverse_iterable<decltype(std::declval<U>()._value.template value<I>())>
    )
    struct _make_union_iterator<
        I,
        U,
        meta::pack<begin...>,
        meta::pack<end...>,
        meta::pack<rbegin...>,
        meta::pack<rend...>
    > : _make_union_iterator<
        I + 1,
        U,
        meta::pack<begin..., meta::unqualify<meta::begin_type<
            decltype(std::declval<U>()._value.template value<I>())
        >>>,
        meta::pack<end..., meta::unqualify<meta::end_type<
            decltype(std::declval<U>()._value.template value<I>())
        >>>,
        meta::pack<rbegin...>,
        meta::pack<rend...>
    > {};
    template <
        size_t I,
        typename U,
        typename... begin,
        typename... end,
        typename... rbegin,
        typename... rend
    > requires (
        I < meta::remove_reference<U>::types::size() &&
        !meta::iterable<decltype(std::declval<U>()._value.template value<I>())> &&
        meta::reverse_iterable<decltype(std::declval<U>()._value.template value<I>())>
    )
    struct _make_union_iterator<
        I,
        U,
        meta::pack<begin...>,
        meta::pack<end...>,
        meta::pack<rbegin...>,
        meta::pack<rend...>
    > : _make_union_iterator<
        I + 1,
        U,
        meta::pack<begin...>,
        meta::pack<end...>,
        meta::pack<rbegin..., meta::unqualify<meta::rbegin_type<
            decltype(std::declval<U>()._value.template value<I>())
        >>>,
        meta::pack<rend..., meta::unqualify<meta::rend_type<
            decltype(std::declval<U>()._value.template value<I>())
        >>>
    > {};
    template <
        size_t I,
        typename U,
        typename... begin,
        typename... end,
        typename... rbegin,
        typename... rend
    > requires (
        I < meta::remove_reference<U>::types::size() &&
        !meta::iterable<decltype(std::declval<U>()._value.template value<I>())> &&
        !meta::reverse_iterable<decltype(std::declval<U>()._value.template value<I>())>
    )
    struct _make_union_iterator<
        I,
        U,
        meta::pack<begin...>,
        meta::pack<end...>,
        meta::pack<rbegin...>,
        meta::pack<rend...>
    > : _make_union_iterator<
        I + 1,
        U,
        meta::pack<begin...>,
        meta::pack<end...>,
        meta::pack<rbegin...>,
        meta::pack<rend...>
    > {};
    template <meta::lvalue U> requires (meta::Union<U>)
    using make_union_iterator = _make_union_iterator<
        0,
        U,
        meta::pack<>,
        meta::pack<>,
        meta::pack<>,
        meta::pack<>
    >;

    /* Tuple iterators can be optimized away if the tuple is empty, or into an array of
    pointers if all elements unpack to the same lvalue type.  Otherwise, they must
    build a vtable and perform a dynamic dispatch to yield a proper value type, which
    may be a union. */
    enum class tuple_iterator_kind {
        NO_COMMON_TYPE,
        EMPTY,
        ARRAY,
        DYNAMIC,
        NOTHROW_DYNAMIC
    };

    /* Tuple iterators may dereference to either a single type if all access types in
    the tuple are consistent, or a `Union` of 2 or more access types as needed. */
    template <typename>
    struct _tuple_iterator_ref {
        using types = meta::pack<>;
        using reference = const NoneType&;
        static constexpr tuple_iterator_kind kind = tuple_iterator_kind::EMPTY;
    };
    template <typename T>
    struct _tuple_iterator_ref<meta::pack<T>> {
        using types = meta::pack<T>;
        using reference = T;
        static constexpr tuple_iterator_kind kind =
            meta::lvalue<T> && meta::has_address<T> ?
                tuple_iterator_kind::ARRAY :
                tuple_iterator_kind::NOTHROW_DYNAMIC;
    };
    template <typename... Ts> requires (sizeof...(Ts) > 1)
    struct _tuple_iterator_ref<meta::pack<Ts...>> {
        using types = meta::pack<Ts...>;
        using reference = bertrand::Union<Ts...>;
        static constexpr tuple_iterator_kind kind =
            !(meta::convertible_to<Ts, reference> && ...) ?
                tuple_iterator_kind::NO_COMMON_TYPE :
                (meta::nothrow::convertible_to<Ts, reference> && ...) ?
                    tuple_iterator_kind::NOTHROW_DYNAMIC :
                    tuple_iterator_kind::DYNAMIC;
    };
    template <meta::lvalue T> requires (meta::tuple_like<T>)
    using tuple_iterator_ref =
        _tuple_iterator_ref<typename meta::tuple_types<T>::template eval<meta::to_unique>>;

    template <typename T>
    concept enable_tuple_iterator =
        meta::lvalue<T> &&
        meta::tuple_like<T> &&
        tuple_iterator_ref<T>::kind != tuple_iterator_kind::NO_COMMON_TYPE;

    /* An iterator over an otherwise non-iterable tuple type, which constructs a vtable
    of callback functions yielding each value.  This allows tuples to be used as inputs
    to iterable algorithms, as long as those algorithms are built to handle possible
    `Union` values. */
    template <enable_tuple_iterator T>
    struct tuple_iterator {
        using types = tuple_iterator_ref<T>::types;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_iterator_ref<T>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

    private:
        static constexpr bool nothrow =
            tuple_iterator_ref<T>::kind == tuple_iterator_kind::NOTHROW_DYNAMIC;

        using indices = std::make_index_sequence<types::size()>;
        using storage = meta::as_pointer<T>;
        using fn_ptr = reference(*)(T) noexcept (nothrow);

        template <size_t I>
        static constexpr reference fn(T t) noexcept (nothrow) {
            return meta::tuple_get<I>(t);
        }

        template <typename = indices>
        static constexpr fn_ptr vtbl[0] {};
        template <size_t... Is>
        static constexpr fn_ptr vtbl<std::index_sequence<Is...>>[sizeof...(Is)] { &fn<Is>... };

        [[nodiscard]] constexpr tuple_iterator(storage data, difference_type index) noexcept :
            data(data),
            index(index)
        {}

    public:
        storage data;
        difference_type index;

        [[nodiscard]] constexpr tuple_iterator(difference_type index = 0) noexcept :
            data(nullptr),
            index(index)
        {}

        [[nodiscard]] constexpr tuple_iterator(T tuple, difference_type index = 0)
            noexcept (meta::nothrow::address_returns<storage, T>)
            requires (meta::address_returns<storage, T>)
        :
            data(std::addressof(tuple)),
            index(index)
        {}

        [[nodiscard]] constexpr reference operator*() const noexcept (nothrow) {
            return vtbl<>[index](*data);
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept (nothrow) {
            return vtbl<>[index + n](*data);
        }

        constexpr tuple_iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator++(int) noexcept {
            tuple_iterator tmp = *this;
            ++index;
            return tmp;
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            const tuple_iterator& self,
            difference_type n
        ) noexcept {
            return {self.data, self.index + n};
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type n,
            const tuple_iterator& self
        ) noexcept {
            return {self.data, self.index + n};
        }

        constexpr tuple_iterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        constexpr tuple_iterator& operator--() noexcept {
            --index;
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator--(int) noexcept {
            tuple_iterator tmp = *this;
            --index;
            return tmp;
        }

        [[nodiscard]] constexpr tuple_iterator operator-(difference_type n) const noexcept {
            return {data, index - n};
        }

        [[nodiscard]] constexpr difference_type operator-(
            const tuple_iterator& other
        ) const noexcept {
            return index - other.index;
        }

        constexpr tuple_iterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] constexpr auto operator<=>(const tuple_iterator& other) const noexcept {
            return index <=> other.index;
        }

        [[nodiscard]] constexpr bool operator==(const tuple_iterator& other) const noexcept {
            return index == other.index;
        }
    };

    /* A special case of `tuple_iterator` for tuples where all elements share the
    same addressable type.  In this case, the vtable is reduced to a simple array of
    pointers that are initialized on construction, without requiring dynamic
    dispatch. */
    template <enable_tuple_iterator T>
        requires (tuple_iterator_ref<T>::kind == tuple_iterator_kind::ARRAY)
    struct tuple_iterator<T> {
        using types = tuple_iterator_ref<T>::types;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_iterator_ref<T>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::address_type<reference>;

    private:
        using indices = std::make_index_sequence<types::size()>;
        using array = std::array<pointer, types::size()>;

        template <size_t... Is>
        static constexpr array init(std::index_sequence<Is...>, T t)
            noexcept ((requires{{
                std::addressof(meta::tuple_get<Is>(t))
            } noexcept -> meta::nothrow::convertible_to<pointer>;} && ...))
        {
            return {std::addressof(meta::tuple_get<Is>(t))...};
        }

        [[nodiscard]] constexpr tuple_iterator(const array& arr, difference_type index)
            noexcept (meta::nothrow::copyable<array>)
        :
            arr(arr),
            index(index)
        {}

    public:
        array arr;
        difference_type index;

        [[nodiscard]] constexpr tuple_iterator(difference_type index = 0)
            noexcept (meta::nothrow::default_constructible<array>)
        :
            arr{},
            index(index)
        {}

        [[nodiscard]] constexpr tuple_iterator(T t, difference_type index)
            noexcept (requires{{init(indices{}, t)} noexcept;})
        :
            arr(init(indices{}, t)),
            index(index)
        {}

        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{{*arr[index]} noexcept -> meta::nothrow::convertible_to<reference>;})
            requires (requires{{*arr[index]} -> meta::convertible_to<reference>;})
        {
            return *arr[index];
        }

        [[nodiscard]] constexpr pointer operator->() const
            noexcept (requires{{arr[index]} noexcept -> meta::nothrow::convertible_to<pointer>;})
            requires (requires{{arr[index]} -> meta::convertible_to<pointer>;})
        {
            return arr[index];
        }

        [[nodiscard]] constexpr reference operator[](difference_type n)
            noexcept (requires{
                {*arr[index + n]} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{{*arr[index + n]} -> meta::convertible_to<reference>;})
        {
            return *arr[index + n];
        }

        constexpr tuple_iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator++(int) noexcept {
            auto tmp = *this;
            ++index;
            return tmp;
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            const tuple_iterator& self,
            difference_type n
        ) noexcept {
            return {self.arr, self.index + n};
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type n,
            const tuple_iterator& self
        ) noexcept {
            return {self.arr, self.index + n};
        }

        constexpr tuple_iterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        constexpr tuple_iterator& operator--() noexcept {
            --index;
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator--(int) noexcept {
            auto tmp = *this;
            --index;
            return tmp;
        }

        [[nodiscard]] constexpr tuple_iterator operator-(difference_type n) const noexcept {
            return {arr, index - n};
        }

        [[nodiscard]] constexpr difference_type operator-(const tuple_iterator& rhs) const noexcept {
            return index - index;
        }

        constexpr tuple_iterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] constexpr auto operator<=>(const tuple_iterator& other) const noexcept {
            return index <=> other.index;
        }

        [[nodiscard]] constexpr bool operator==(const tuple_iterator& other) const noexcept {
            return index == other.index;
        }
    };

    /* A special case of `tuple_iterator` for empty tuples, which do not yield any
    results, and are optimized away by the compiler. */
    template <enable_tuple_iterator T>
        requires (tuple_iterator_ref<T>::kind == tuple_iterator_kind::EMPTY)
    struct tuple_iterator<T> {
        using types = meta::pack<>;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const NoneType;
        using pointer = const NoneType*;
        using reference = const NoneType&;

        [[nodiscard]] constexpr tuple_iterator(difference_type = 0) noexcept {}
        [[nodiscard]] constexpr tuple_iterator(T, difference_type) noexcept {}

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return None;
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept {
            return &None;
        }

        [[nodiscard]] constexpr reference operator[](difference_type) const noexcept {
            return None;
        }

        constexpr tuple_iterator& operator++() noexcept {
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator++(int) noexcept {
            return *this;
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            const tuple_iterator& self,
            difference_type
        ) noexcept {
            return self;
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type,
            const tuple_iterator& self
        ) noexcept {
            return self;
        }

        constexpr tuple_iterator& operator+=(difference_type) noexcept {
            return *this;
        }

        constexpr tuple_iterator& operator--() noexcept {
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator--(int) noexcept {
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator-(difference_type) const noexcept {
            return *this;
        }

        [[nodiscard]] constexpr difference_type operator-(const tuple_iterator&) const noexcept {
            return 0;
        }

        constexpr tuple_iterator& operator-=(difference_type) noexcept {
            return *this;
        }

        [[nodiscard]] constexpr auto operator<=>(const tuple_iterator&) const noexcept {
            return std::strong_ordering::equal;
        }

        [[nodiscard]] constexpr bool operator==(const tuple_iterator&) const noexcept {
            return true;
        }
    };






    /// TODO: tuple_storage may eventually need std::tuple-like destructuring operators

    /// TODO: also, the storage class might be able to come after the iterator class,
    /// and therefore be iterable by default.  It can also implement the array
    /// optimization if all types are the same, and therefore be trivially iterable.


    /* A basic implementation of a tuple using recursive inheritance, meant to be used
    in conjunction with `union_storage` as the basis for algebraic types. */
    template <meta::not_void...>
    struct tuple_storage {
        using types = meta::pack<>;
        template <size_t I, typename Self> requires (false)  // never actually called
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept;
    };

    template <meta::not_void T, meta::not_void... Ts>
    struct tuple_storage<T, Ts...> : tuple_storage<Ts...> {
    private:
        using type = meta::remove_rvalue<T>;

    public:
        using types = meta::pack<T, Ts...>;
        [[no_unique_address]] type data;

        [[nodiscard]] constexpr tuple_storage(T val, Ts... rest)
            noexcept (
                meta::nothrow::convertible_to<T, type> &&
                meta::nothrow::constructible_from<tuple_storage<Ts...>, Ts...>
            )
            requires (
                meta::convertible_to<T, type> &&
                meta::constructible_from<tuple_storage<Ts...>, Ts...>
            )
        :
            tuple_storage<Ts...>(std::forward<Ts>(rest)...),
            data(std::forward<T>(val))
        {}

        template <size_t I, typename Self> requires (I < sizeof...(Ts) + 1)
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).data);
            } else {
                using base = meta::qualify<tuple_storage<Ts...>, Self>;
                return (std::forward<base>(self).template value<I - 1>());
            }
        }
    };

    template <meta::not_void T, meta::not_void... Ts> requires (meta::lvalue<T>)
    struct tuple_storage<T, Ts...> : tuple_storage<Ts...> {
        using types = meta::pack<T, Ts...>;
        [[no_unique_address]] struct { T ref; } data;

        [[nodiscard]] constexpr tuple_storage(T ref, Ts... rest)
            noexcept (meta::nothrow::constructible_from<tuple_storage<Ts...>, Ts...>)
            requires (meta::constructible_from<tuple_storage<Ts...>, Ts...>)
        :
            tuple_storage<Ts...>(std::forward<Ts>(rest)...),
            data{ref}
        {}

        constexpr tuple_storage(const tuple_storage&) = default;
        constexpr tuple_storage(tuple_storage&&) = default;

        constexpr tuple_storage& operator=(const tuple_storage& other) {
            tuple_storage<Ts...>::operator=(other);
            std::construct_at(&data, other.data.ref);
            return *this;
        };

        constexpr tuple_storage& operator=(tuple_storage&& other) {
            tuple_storage<Ts...>::operator=(std::move(other));
            std::construct_at(&data, other.data.ref);
            return *this;
        };

        template <size_t I, typename Self> requires (I < sizeof...(Ts) + 1)
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).data.ref);
            } else {
                using base = meta::qualify<tuple_storage<Ts...>, Self>;
                return (std::forward<base>(self).template value<I - 1>());
            }
        }
    };

    template <typename... Ts>
    tuple_storage(Ts&&...) -> tuple_storage<meta::remove_rvalue<Ts>...>;

    /* A variation of `tuple_storage` that specifically represents an overload set
    with the given functions. */
    template <meta::not_void...>
    struct overloads {
        using types = meta::pack<>;
        template <size_t I, typename Self> requires (false)  // never actually called
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept;
    };

    template <meta::not_void F, meta::not_void... Fs> requires (!meta::lvalue<F>)
    struct overloads<F, Fs...> : overloads<Fs...> {
    private:
        using type = meta::remove_rvalue<F>;

    public:
        using types = meta::pack<F, Fs...>;
        [[no_unique_address]] type func;

        [[nodiscard]] constexpr overloads(F val, Fs... rest)
            noexcept (
                meta::nothrow::convertible_to<F, type> &&
                meta::nothrow::constructible_from<overloads<Fs...>, Fs...>
            )
            requires (
                meta::convertible_to<F, type> &&
                meta::constructible_from<overloads<Fs...>, Fs...>
            )
        :
            overloads<Fs...>(std::forward<Fs>(rest)...),
            func(std::forward<F>(val))
        {}

        template <size_t I, typename Self> requires (I < sizeof...(Fs) + 1)
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).func);
            } else {
                return (std::forward<overloads<Fs...>>(self).template value<I - 1>());
            }
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).func(std::forward<A>(args)...)} noexcept;
            })
            requires (
                requires{{std::forward<Self>(self).func(std::forward<A>(args)...)};} &&
                !requires{{std::forward<meta::qualify<overloads<Fs...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            return (std::forward<Self>(self).func(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<overloads<Fs...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).func(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<overloads<Fs...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<overloads<Fs...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }
    };

    template <meta::not_void F, meta::not_void... Fs> requires (meta::lvalue<F>)
    struct overloads<F, Fs...> : overloads<Fs...> {
        using types = meta::pack<F, Fs...>;
        [[no_unique_address]] struct { F ref; } func;

        [[nodiscard]] constexpr overloads(F ref, Fs... rest)
            noexcept (meta::nothrow::constructible_from<overloads<Fs...>, Fs...>)
            requires (meta::constructible_from<overloads<Fs...>, Fs...>)
        :
            overloads<Fs...>(std::forward<Fs>(rest)...),
            func{ref}
        {}

        constexpr overloads(const overloads&) = default;
        constexpr overloads(overloads&&) = default;

        constexpr overloads& operator=(const overloads& other) {
            overloads<Fs...>::operator=(other);
            std::construct_at(&func, other.func.ref);
            return *this;
        };

        constexpr overloads& operator=(overloads&& other) {
            overloads<Fs...>::operator=(std::move(other));
            std::construct_at(&func, other.func.ref);
            return *this;
        };

        template <size_t I, typename Self> requires (I < sizeof...(Fs) + 1)
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).func.ref);
            } else {
                return (std::forward<overloads<Fs...>>(self).template value<I - 1>());
            }
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).func.ref(std::forward<A>(args)...)} noexcept;
            })
            requires (
                requires{{std::forward<Self>(self).func.ref(std::forward<A>(args)...)};} &&
                !requires{{std::forward<meta::qualify<overloads<Fs...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            return (std::forward<Self>(self).func.ref(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<overloads<Fs...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).func.ref(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<overloads<Fs...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<overloads<Fs...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }
    };

    template <typename... Fs>
    overloads(Fs&&...) -> overloads<meta::remove_rvalue<Fs>...>;

}


template <meta::not_void... Ts> requires (sizeof...(Ts) > 1 && meta::unique<Ts...>)
struct Union : impl::union_tag {
    using types = meta::pack<Ts...>;

private:

    template <size_t I, typename... A> requires (I < sizeof...(Ts))
    constexpr Union(impl::union_select<I> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, Ts...>, A...>)
        requires (meta::constructible_from<meta::unpack_type<I, Ts...>, A...>)
    :
        _value{tag, std::forward<A>(args)...}
    {}

public:
    impl::union_storage<Ts...> _value;

    /* Default constructor finds the first type in `Ts...` that can be default
    constructed.  If no such type exists, then the default constructor is disabled. */
    [[nodiscard]] constexpr Union()
        noexcept (meta::nothrow::default_constructible<impl::union_storage<Ts...>>)
        requires (impl::union_storage<Ts...>::default_constructible)
    :
        _value()
    {}

    /* Converting constructor finds the most proximal type in `Ts...` that can be
    implicitly converted from the input type.  This will prefer exact matches or
    differences in qualifications (preferring the least qualified) first, followed by
    inheritance relationships (preferring the most derived and least qualified), and
    finally implicit conversions (preferring the first match and ignoring lvalues).  If
    no such type exists, the conversion constructor is disabled.  If a visitable type
    is provided, then the conversion must be exhaustive over all alternatives, enabling
    implicit conversions from other union types, regardless of source. */
    template <typename from>
    [[nodiscard]] constexpr Union(from&& v)
        noexcept (meta::nothrow::exhaustive<impl::union_convert_from<types, from>, from>)
        requires (
            meta::exhaustive<impl::union_convert_from<types, from>, from> &&
            meta::consistent<impl::union_convert_from<types, from>, from>
        )
    :
        _value(impl::visit(
            impl::union_convert_from<types, from>{},
            std::forward<from>(v)
        ))
    {}

    /* Explicit constructor finds the first type in `Ts...` that can be constructed
    from the given arguments.  If no such type exists, the explicit constructor is
    disabled.  If one or more visitables are provided, then the constructor must be
    exhaustive over all alternatives, enabling explicit conversions from other
    union types, regardless of source. */
    template <typename... A>
    [[nodiscard]] constexpr explicit Union(A&&... args)
        noexcept (meta::nothrow::exhaustive<impl::union_construct_from<Ts...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::exhaustive<impl::union_convert_from<types, A...>, A...> &&
            meta::exhaustive<impl::union_construct_from<Ts...>, A...> &&
            meta::consistent<impl::union_construct_from<Ts...>, A...>
        )
    :
        _value(impl::visit(impl::union_construct_from<Ts...>{}, std::forward<A>(args)...))
    {}

    /* Swap the contents of two unions as efficiently as possible.  This will use
    swap operators for the wrapped alternatives if possible, otherwise falling back to
    a 3-way move using a temporary of the same type. */
    constexpr void swap(Union& other)
        noexcept (requires{{_value.swap(other._value)} noexcept;})
        requires (requires{{_value.swap(other._value)};})
    {
        _value.swap(other._value);
    }

    /* Implicit conversion operator allows conversions toward any type to which all
    alternatives can be exhaustively converted.  This allows conversion to scalar types
    as well as union types (regardless of source) that satisfy the conversion
    criteria. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr operator to(this Self&& self)
        noexcept (meta::nothrow::exhaustive<impl::ConvertTo<to>, Self>)
        requires (meta::exhaustive<impl::ConvertTo<to>, Self>)
    {
        return impl::visit(impl::ConvertTo<to>{}, std::forward<Self>(self));
    }

    /* Explicit conversion operator allows functional-style conversions toward any type
    to which all alternatives can be explicitly converted.  This allows conversion to
    scalar types as well as union types (regardless of source) that satisfy the
    conversion criteria. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr explicit operator to(this Self&& self)
        noexcept (meta::nothrow::exhaustive<impl::ExplicitConvertTo<to>, Self>)
        requires (
            !meta::exhaustive<impl::ConvertTo<to>, Self> &&
            meta::exhaustive<impl::ExplicitConvertTo<to>, Self>
        )
    {
        return impl::visit(impl::ExplicitConvertTo<to>{}, std::forward<Self>(self));
    }

    /* Check whether the union holds a specific type, identified by index. */
    template <ssize_t I> requires (impl::valid_index<types::ssize(), I>)
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return ssize_t(_value.index()) == impl::normalize_index<types::size(), I>();
    }

    /* Check whether the union holds a specific type. */
    template <typename T> requires (types::template contains<T>())
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return _value.index() == types::template index<T>();
    }

    /* Flatten the union into a common type, assuming one exists.  Fails to compile if
    no common type can be found.  Note that the contents will be perfectly forwarded
    according to their storage qualifiers as well as those of the union itself. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) value(this Self&& self)
        noexcept (requires{{std::forward<Self>(self)._value.flatten()} noexcept;})
        requires (requires{{std::forward<Self>(self)._value.flatten()};}
        )
    {
        return (std::forward<Self>(self)._value.flatten());
    }

    /* Get the value of the type at index `I`.  Fails to compile if the index is out of
    range.  Otherwise, returns an `Expected<T, BadUnionAccess>` where `T` is the type
    at index `I`, forwarded according the qualifiers of the enclosing union. */
    template <ssize_t I, typename Self> requires (impl::valid_index<types::ssize(), I>)
    [[nodiscard]] constexpr auto value(this Self&& self)
        noexcept (requires{{impl::union_access<Self>::template value<I>(
            std::forward<Self>(self)._value
        )} noexcept;})
        requires (requires{{impl::union_access<Self>::template value<I>(
            std::forward<Self>(self)._value
        )};})
    {
        return impl::union_access<Self>::template value<I>(std::forward<Self>(self)._value);
    }

    /* Get the value for the templated type.  Fails to compile if the templated type
    is not a valid union member.  Otherwise, returns an `Expected<T, BadUnionAccess>`,
    where `T` is forwarded according to the qualifiers of the enclosing union. */
    template <typename T, typename Self> requires (types::template contains<T>())
    [[nodiscard]] constexpr auto value(this Self&& self)
        noexcept (requires{{impl::union_access<Self>::template value<T>(
            std::forward<Self>(self)._value
        )} noexcept;})
        requires (requires{{impl::union_access<Self>::template value<T>(
            std::forward<Self>(self)._value
        )};})
    {
        return impl::union_access<Self>::template value<T>(std::forward<Self>(self)._value);
    }

    /* Invoke one or more visitor functions over the values of the union.  This is
    identical to passing the union as input to a `def` function, except that the
    functions must all be callable with a single value representing the current
    alternative, and with slightly nicer syntax in that case. */
    template <typename Self, typename... Fs>
    constexpr decltype(auto) value(this Self&& self, Fs&&... fs)
        noexcept (meta::nothrow::visit<impl::overloads<meta::remove_rvalue<Fs>...>, Self>)
        requires (meta::visit<impl::overloads<meta::remove_rvalue<Fs>...>, Self>)
    {
        return (impl::visit(
            impl::overloads<meta::remove_rvalue<Fs>...>{std::forward<Fs>(fs)...},
            std::forward<Self>(self)
        ));
    }

    /* Get the value of the type at index `I` if it is the active type.  Fails to
    compile if the index is out of range.  Otherwise, returns an `Optional<T>` where
    `T` is the type at index `I`, and the empty state is returned if it differs from
    the active type.  The value is forwarded according to the qualifiers of the
    enclosing union. */
    template <ssize_t I, typename Self> requires (impl::valid_index<types::ssize(), I>)
    [[nodiscard]] constexpr auto value_if(this Self&& self)
        noexcept (requires{{impl::union_access<Self>::template value_if<I>(
            std::forward<Self>(self)._value
        )} noexcept;})
        requires (requires{{impl::union_access<Self>::template value_if<I>(
            std::forward<Self>(self)._value
        )};})
    {
        return impl::union_access<Self>::template value_if<I>(std::forward<Self>(self)._value);
    }

    /* Get the value for the templated type if it is currently active.  Fails to
    compile if the templated type is not a valid union member.  Otherwise, returns an
    `Optional<T>`, where the empty state signals an inactive type.  The value is
    forwarded according to the qualifiers of the enclosing union. */
    template <typename T, typename Self> requires (types::template contains<T>())
    [[nodiscard]] constexpr auto value_if(this Self&& self)
        noexcept (requires{{impl::union_access<Self>::template value_if<T>(
            std::forward<Self>(self)._value
        )} noexcept;})
        requires (requires{{impl::union_access<Self>::template value_if<T>(
            std::forward<Self>(self)._value
        )};})
    {
        return impl::union_access<Self>::template value_if<T>(std::forward<Self>(self)._value);
    }

    /* Explicitly construct a union with the alternative at index `I` using the
    provided arguments.  This is more explicit than using the standard constructors,
    for cases where only a specific alternative should be considered. */
    template <ssize_t I, typename... A> requires (impl::valid_index<types::ssize(), I>)
    [[nodiscard]] static constexpr Union with_value(A&&... args)
        noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, Ts...>, A...>)
        requires (meta::constructible_from<meta::unpack_type<I, Ts...>, A...>)
    {
        return {impl::union_select<
            impl::normalize_index<types::ssize(), I>()>{},
            std::forward<A>(args)...
        };
    }

    /* Explicitly construct a union with the specified alternative using the given
    arguments.  This is more explicit than using the standard constructors, for cases
    where only a specific alternative should be considered. */
    template <typename T, typename... A> requires (types::template contains<T>())
    [[nodiscard]] static constexpr Union with_value(A&&... args)
        noexcept (meta::nothrow::constructible_from<T, A...>)
        requires (meta::constructible_from<T, A...>)
    {
        return {impl::union_select<meta::index_of<T, Ts...>>{}, std::forward<A>(args)...};
    }

    /* Monadic call operator.  If any of the union types are function-like objects that
    are callable with the given arguments, then this will return the result of that
    function, possibly wrapped in another union to represent heterogenous results.  If
    some of the return types are `void` and others are not, then the result may be
    converted to `Optional<R>`, where `R` is the return type(s) of the invocable
    functions.  If not all of the union types are invocable with the given arguments,
    then the result will be further wrapped in an `Expected<R, BadUnionAccess>`, just
    as for `impl::visit()`. */
    template <typename Self, typename... A>
    constexpr decltype(auto) operator()(this Self&& self, A&&... args)
        noexcept (meta::nothrow::visit<impl::Call, Self, A...>)
        requires (meta::visit<impl::Call, Self, A...>)
    {
        return (impl::visit(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<A>(args)...
        ));
    }

    /* Monadic subscript operator.  If any of the union types support indexing with the
    given key, then this will return the result of that operation, possibly wrapped in
    another union to represent heterogenous results.  If some of the return types are
    `void` and others are not, then the result may be converted to `Optional<R>`, where
    `R` is the return type(s) of the indexable types.  If not all of the union types
    are indexable with the given arguments, then the result will be further wrapped in
    an `Expected<R, BadUnionAccess>`, just as for `impl::visit()`. */
    template <typename Self, typename... K>
    constexpr decltype(auto) operator[](this Self&& self, K&&... keys)
        noexcept (meta::nothrow::visit<impl::Subscript, Self, K...>)
        requires (meta::visit<impl::Subscript, Self, K...>)
    {
        return (impl::visit(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<K>(keys)...
        ));
    }

    /* Returns the result of `std::ranges::size()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) size(this Self&& self)
        noexcept (requires{{impl::make_union_iterator<Self&>::size(self)} noexcept;})
        requires (requires{{impl::make_union_iterator<Self&>::size(self)};})
    {
        return (impl::make_union_iterator<Self&>::size(self));
    }

    /* Returns the result of `std::ranges::ssize()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) ssize(this Self&& self)
        noexcept (requires{{impl::make_union_iterator<Self&>::ssize(self)} noexcept;})
        requires (requires{{impl::make_union_iterator<Self&>::ssize(self)};})
    {
        return (impl::make_union_iterator<Self&>::ssize(self));
    }

    /* Returns the result of `std::ranges::empty()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) empty(this Self&& self)
        noexcept (requires{{impl::make_union_iterator<Self&>::empty(self)} noexcept;})
        requires (requires{{impl::make_union_iterator<Self&>::empty(self)};})
    {
        return (impl::make_union_iterator<Self&>::empty(self));
    }

    /* Get a forward iterator over the union, assuming all alternatives are iterable.
    Fails to compile otherwise.  The result is either passed through as-is if all
    alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self& self)
        noexcept (requires{{impl::make_union_iterator<Self&>::begin(self)} noexcept;})
        requires (requires{{impl::make_union_iterator<Self&>::begin(self)};})
    {
        return impl::make_union_iterator<Self&>::begin(self);
    }

    /* Get a forward iterator over the union, assuming all alternatives are iterable.
    Fails to compile otherwise.  The result is either passed through as-is if all
    alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    [[nodiscard]] constexpr auto cbegin() const
        noexcept (requires{{impl::make_union_iterator<const Union&>::begin(*this)} noexcept;})
        requires (requires{{impl::make_union_iterator<const Union&>::begin(*this)};})
    {
        return impl::make_union_iterator<const Union&>::begin(*this);
    }

    /* Get a forward sentinel for the union, assuming all alternatives are iterable.
    Fails to compile otherwise.  The result is either passed through as-is if all
    alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self& self)
        noexcept (requires{{impl::make_union_iterator<Self&>::end(self)} noexcept;})
        requires (requires{{impl::make_union_iterator<Self&>::end(self)};})
    {
        return impl::make_union_iterator<Self&>::end(self);
    }

    /* Get a forward sentinel for the union, assuming all alternatives are iterable.
    Fails to compile otherwise.  The result is either passed through as-is if all
    alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    [[nodiscard]] constexpr auto cend() const
        noexcept (requires{{impl::make_union_iterator<const Union&>::end(*this)} noexcept;})
        requires (requires{{impl::make_union_iterator<const Union&>::end(*this)};})
    {
        return impl::make_union_iterator<const Union&>::end(*this);
    }

    /* Get a reverse iterator over the union, assuming all alternatives are reverse
    iterable.  Fails to compile otherwise.  The result is either passed through as-is
    if all alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self& self)
        noexcept (requires{{impl::make_union_iterator<Self&>::rbegin(self)} noexcept;})
        requires (requires{{impl::make_union_iterator<Self&>::rbegin(self)};})
    {
        return impl::make_union_iterator<Self&>::rbegin(self);
    }

    /* Get a reverse iterator over the union, assuming all alternatives are reverse
    iterable.  Fails to compile otherwise.  The result is either passed through as-is
    if all alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    [[nodiscard]] constexpr auto crbegin() const
        noexcept (requires{{impl::make_union_iterator<const Union&>::rbegin(*this)} noexcept;})
        requires (requires{{impl::make_union_iterator<const Union&>::rbegin(*this)};})
    {
        return impl::make_union_iterator<const Union&>::begin(*this);
    }

    /* Get a reverse sentinel for the union, assuming all alternatives are reverse
    iterable.  Fails to compile otherwise.  The result is either passed through as-is
    if all alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self& self)
        noexcept (requires{{impl::make_union_iterator<Self&>::rend(self)} noexcept;})
        requires (requires{{impl::make_union_iterator<Self&>::rend(self)};})
    {
        return impl::make_union_iterator<Self&>::rend(self);
    }

    /* Get a reverse sentinel for the union, assuming all alternatives are reverse
    iterable.  Fails to compile otherwise.  The result is either passed through as-is
    if all alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    [[nodiscard]] constexpr auto crend() const
        noexcept (requires{{impl::make_union_iterator<const Union&>::rend(*this)} noexcept;})
        requires (requires{{impl::make_union_iterator<const Union&>::rend(*this)};})
    {
        return impl::make_union_iterator<const Union&>::rend(*this);
    }
};


/// TODO: do I need to provide an explicit comparison against `std::nullopt`?  That
/// would ordinarily not pass the monadic constraints unless `T` is itself comparable
/// against `std::nullopt`, which is not guaranteed to be the case.  If it is, then
/// we can simply visit it like normal, but otherwise, the non-empty case will always
/// return false.  Basically, that just means an extra visitor.  As long as I
/// support `std::nullopt`, the `None` template operators should kick in automatically
/// and cover that case as well, without allowing comparison against nullptr unless
/// T also supports it.


template <meta::not_void T> requires (!meta::None<T>)
struct Optional : impl::optional_tag {
    using types = meta::pack<T>;
    using value_type = T;
    using empty_type = NoneType;

    impl::union_storage<empty_type, value_type> _value;

    /* Default constructor.  Initializes the optional in the empty state. */
    [[nodiscard]] constexpr Optional() = default;

    /* Converting constructor.  Implicitly converts the input to the value type, and
    initializes the optional with the result.  Also allows implicit conversions from
    any type `U` where `bertrand::impl::visitable<U>::empty` is not void and all
    non-empty alternatives can be converted to the value type (e.g. `std::optional<V>`,
    where `V` is convertible to `T`), or from raw pointers in case `T` is an lvalue
    reference. */
    template <typename from>
    [[nodiscard]] constexpr Optional(from&& v)
        noexcept (meta::nothrow::exhaustive<impl::optional_convert_from<value_type, from>, from>)
        requires (
            meta::exhaustive<impl::optional_convert_from<value_type, from>, from> &&
            meta::consistent<impl::optional_convert_from<value_type, from>, from>
        )
    : 
        _value(impl::visit(
            impl::optional_convert_from<value_type, from>{},
            std::forward<from>(v)
        ))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the value type's
    constructor, and initializes the optional with the result. */
    template <typename... A>
    [[nodiscard]] constexpr explicit Optional(A&&... args)
        noexcept (meta::nothrow::exhaustive<impl::optional_construct_from<value_type>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::exhaustive<impl::optional_convert_from<value_type, A...>, A...> &&
            meta::exhaustive<impl::optional_construct_from<value_type>, A...> &&
            meta::consistent<impl::optional_construct_from<value_type>, A...>
        )
    :
        _value(impl::visit(
            impl::optional_construct_from<value_type>{},
            std::forward<A>(args)...
        ))
    {}

    /* Swap the contents of two optionals as efficiently as possible. */
    constexpr void swap(Optional& other)
        noexcept (requires{{_value.swap(other._value)} noexcept;})
        requires (requires{{_value.swap(other._value)};})
    {
        _value.swap(other._value);
    }

    /* Implicit conversion from `Optional<T>` to any type that is convertible from both
    the perfectly-forwarded value type and any of `None`, `std::nullopt`, or `nullptr`
    (if `T` is an lvalue reference). */
    template <typename Self, typename to>
    [[nodiscard]] constexpr operator to(this Self&& self)
        noexcept (meta::nothrow::exhaustive<impl::optional_convert_to<Self, to>>)
        requires (
            meta::exhaustive<impl::optional_convert_to<Self, to>, Self> &&
            meta::consistent<impl::optional_convert_to<Self, to>, Self>
        )
    {
        return impl::visit(
            impl::optional_convert_to<Self, to>{},
            std::forward<Self>(self)
        );
    }

    /* Returns `true` if the optional currently holds a value, or `false` if it is in
    the empty state. */
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return _value.index();
    }

    /* Access the stored value.  Throws a `BadUnionAccess` assertion if the program is
    compiled in debug mode and the optional is currently in the empty state. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept (!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.has_value()) {
                static constexpr static_str msg =
                    "Empty optional has no value: " + demangle<Self>();
                throw BadUnionAccess(msg);
            }
        }
        return (std::forward<Self>(self)._value.template value<1>());
    }

    /* If the optional is empty, invoke a given function and return its result,
    otherwise propagate the non-empty state.  This is identical to invoking a manual
    visitor (e.g. a `def` statement) over the optional, except that the visitor
    function does not need to accept `bertrand::NoneType` (aka `std::nullopt_t`)
    explicitly, and the non-empty state is implicitly forwarded, rather than the
    empty state.  All other rules (including promotion to union or handling of void
    return types) remain the same.

    For most visitors, where `f()` returns the same type as `Optional.value()`:

                self                invoke              result
        -----------------------------------------------------------------------
        1.  Optional<T>(empty)      f() -> t            T(t)
        2.  Optional<T>(t)          (no call)           T(t)

    If `f()` returns a type different from the optional:

                self                invoke              result
        -----------------------------------------------------------------------
        1.  Optional<T>(empty)      f() -> u            Union<T, U>(u)
        2.  Optional<T>(t)          (no call)           Union<T, U>(t)

    If `f()` returns an `Expected<U, E...>`:

                self                invoke              result
        ----------------------------------------------------------------------
        1.  Optional<T>(empty)      f() -> e            Expected<Union<T, U>, E...>(e)
        2.  Optional<T>(t)          (no call)           Expected<Union<T, U>, E...>(t)

    If the visitor returns void:

                self                invoke              result
        -----------------------------------------------------------------------
        1.  Optional<T>(empty)      f() -> void         Optional<T>(empty)
        2.  Optional<T>(t)          (no call)           Optional<T>(t)
    */
    template <typename Self, typename F>
    constexpr decltype(auto) value_or(this Self&& self, F&& f)
        noexcept (meta::nothrow::visit<impl::optional_or_else<Self>, F, Self>)
        requires (meta::callable<F> && meta::visit<impl::optional_or_else<Self>, F, Self>)
    {
        return (impl::visit(
            impl::optional_or_else<Self>{},
            std::forward<F>(f),
            std::forward<Self>(self)
        ));
    }

    /* Monadic call operator.  If the optional type is a function-like object and is
    not in the empty state, then this will return the result of that function wrapped
    in another optional.  Otherwise, it will propagate the empty state.  If the
    function returns void, then the result will be void in both cases, and the function
    will not be invoked for the empty state. */
    template <typename Self, typename... A>
    constexpr decltype(auto) operator()(this Self&& self, A&&... args)
        noexcept (meta::nothrow::visit<impl::Call, Self, A...>)
        requires (meta::visit<impl::Call, Self, A...>)
    {
        return (impl::visit(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<A>(args)...
        ));
    }

    /* Monadic subscript operator.  If the optional type is a container that supports
    indexing with the given key, then this will return the result of the access wrapped
    in another optional.  Otherwise, it will propagate the empty state. */
    template <typename Self, typename... K>
    constexpr decltype(auto) operator[](this Self&& self, K&&... keys)
        noexcept (meta::nothrow::visit<impl::Subscript, Self, K...>)
        requires (meta::visit<impl::Subscript, Self, K...>)
    {
        return (impl::visit(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<K>(keys)...
        ));
    }

    /* Return 0 if the optional is empty or `std::ranges::size(value())` otherwise.
    If `std::ranges::size(value())` would be malformed and the value is not iterable
    (meaning that iterating over the optional would return just a single element), then
    the result will be identical to `has_value()`.  If neither option is available,
    then this method will fail to compile. */
    [[nodiscard]] constexpr auto size() const
        noexcept (
            meta::nothrow::has_size<value_type> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
        requires (
            meta::has_size<value_type> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
    {
        if constexpr (meta::has_size<value_type>) {
            if (has_value()) {
                return std::ranges::size(_value.template value<1>());
            } else {
                return meta::size_type<T>(0);
            }
        } else {
            return size_t(has_value());
        }
    }

    /* Return 0 if the optional is empty or `std::ranges::ssize(value())` otherwise.
    If `std::ranges::ssize(value())` would be malformed and the value is not iterable
    (meaning that iterating over the optional would return just a single element), then
    the result will be identical to `has_value()`.  If neither option is available,
    then this method will fail to compile. */
    [[nodiscard]] constexpr auto ssize() const
        noexcept (
            meta::nothrow::has_ssize<value_type> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
        requires (
            meta::has_ssize<value_type> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
    {
        if constexpr (meta::has_ssize<value_type>) {
            if (has_value()) {
                return std::ranges::ssize(_value.template value<1>());
            } else {
                return meta::ssize_type<T>(0);
            }
        } else {
            return ssize_t(has_value());
        }
    }

    /* Return true if the optional is empty or `std::ranges::empty(value())` otherwise.
    If `std::ranges::empty(value())` would be malformed and the value is not iterable
    (meaning that iterating over the optional would return just a single element), then
    the result will be identical to `!has_value()`.  If neither option is available,
    then this method will fail to compile. */
    [[nodiscard]] constexpr bool empty() const
        noexcept (
            meta::nothrow::has_empty<value_type> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
        requires (
            meta::has_empty<value_type> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
    {
        if constexpr (meta::has_empty<value_type>) {
            return has_value() ? std::ranges::empty(_value.template value<1>()) : true;
        } else {
            return !has_value();
        }
    }

    /* Get a forward iterator over the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `begin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `end()` iterator if the
    optional is currently empty. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self& self)
        noexcept (requires{{impl::make_optional_iterator<Self&>::begin(self)} noexcept;})
        requires (requires{{impl::make_optional_iterator<Self&>::begin(self)};})
    {
        return impl::make_optional_iterator<Self&>::begin(self);
    }

    /* Get a forward iterator over the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `cbegin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `end()` iterator if the
    optional is currently empty. */
    [[nodiscard]] constexpr auto cbegin() const
        noexcept (requires{
            {impl::make_optional_iterator<const Optional&>::begin(*this)} noexcept;
        })
        requires (requires{
            {impl::make_optional_iterator<const Optional&>::begin(*this)};
        })
    {
        return impl::make_optional_iterator<const Optional&>::begin(*this);
    }

    /* Get a forward sentinel for the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `end()` type.  Otherwise, it will
    return an empty iterator. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self& self)
        noexcept (requires{{impl::make_optional_iterator<Self&>::end(self)} noexcept;})
        requires (requires{{impl::make_optional_iterator<Self&>::end(self)};})
    {
        return impl::make_optional_iterator<Self&>::end(self);
    }

    /* Get a forward sentinel for the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `cend()` type.  Otherwise, it will
    return an empty iterator. */
    [[nodiscard]] constexpr auto cend() const
        noexcept (requires{
            {impl::make_optional_iterator<const Optional&>::end(*this)} noexcept;}
        )
        requires (requires{
            {impl::make_optional_iterator<const Optional&>::end(*this)};
        })
    {
        return impl::make_optional_iterator<const Optional&>::end(*this);
    }

    /* Get a reverse iterator over the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `rbegin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `rend()` iterator if the
    optional is currently empty. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self& self)
        noexcept (requires{{impl::make_optional_iterator<Self&>::rbegin(self)} noexcept;})
        requires (requires{{impl::make_optional_iterator<Self&>::rbegin(self)};})
    {
        return impl::make_optional_iterator<Self&>::rbegin(self);
    }

    /* Get a reverse iterator over the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `crbegin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `crend()` iterator if the
    optional is currently empty. */
    [[nodiscard]] constexpr auto crbegin() const
        noexcept (requires{
            {impl::make_optional_iterator<const Optional&>::rbegin(*this)} noexcept;
        })
        requires (requires{
            {impl::make_optional_iterator<const Optional&>::rbegin(*this)};
        })
    {
        return impl::make_optional_iterator<const Optional&>::rbegin(*this);
    }

    /* Get a reverse sentinel for the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `rend()` type.  Otherwise, it will
    return an empty iterator. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self& self)
        noexcept (requires{{impl::make_optional_iterator<Self&>::rend(self)} noexcept;})
        requires (requires{{impl::make_optional_iterator<Self&>::rend(self)};})
    {
        return impl::make_optional_iterator<Self&>::rend(self);
    }

    /* Get a reverse sentinel for the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `crend()` type.  Otherwise, it will
    return an empty iterator. */
    [[nodiscard]] constexpr auto crend() const
        noexcept (requires{
            {impl::make_optional_iterator<const Optional&>::rend(*this)} noexcept;
        })
        requires (requires{
            {impl::make_optional_iterator<const Optional&>::rend(*this)};
        })
    {
        return impl::make_optional_iterator<const Optional&>::rend(*this);
    }
};


template <typename T, meta::unqualified_exception E, meta::unqualified_exception... Es>
    requires (meta::unique<T, E, Es...>)
struct Expected : impl::expected_tag {
    using types = meta::pack<T, E, Es...>;
    using errors = meta::pack<E, Es...>;
    using value_type = std::conditional_t<meta::is_void<T>, NoneType, T>;
    using error_type = impl::expected_error<E, Es...>;

    impl::union_storage<value_type, E, Es...> _value;

    /* Default constructor.  Enabled iff the result type is default constructible or
    void. */
    [[nodiscard]] constexpr Expected()
        noexcept (meta::nothrow::default_constructible<meta::remove_rvalue<value_type>>)
        requires (meta::default_constructible<meta::remove_rvalue<value_type>>)
    {}

    /* Converting constructor.  Implicitly converts the input to the value type if
    possible, otherwise accepts subclasses of the error states.  Also allows conversion
    from other visitable types whose alternatives all meet the conversion criteria. */
    template <typename from>
    [[nodiscard]] constexpr Expected(from&& v)
        noexcept (meta::nothrow::exhaustive<impl::expected_convert_from<types, from>, from>)
        requires (
            meta::exhaustive<impl::expected_convert_from<types, from>, from> &&
            meta::consistent<impl::expected_convert_from<types, from>, from>
        )
    :
        _value(impl::visit(
            impl::expected_convert_from<types, from>{},
            std::forward<from>(v)
        ))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the result type's
    constructor, and initializes the expected with the result. */
    template <typename... A>
    [[nodiscard]] constexpr explicit Expected(A&&... args)
        noexcept (meta::nothrow::exhaustive<impl::expected_construct_from<T, E, Es...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::exhaustive<impl::expected_convert_from<types, A...>, A...> &&
            meta::exhaustive<impl::expected_construct_from<T, E, Es...>, A...> &&
            meta::consistent<impl::expected_construct_from<T, E, Es...>, A...>
        )
    :
        _value(impl::visit(
            impl::expected_construct_from<T, E, Es...>{},
            std::forward<A>(args)...
        ))
    {}

    /* Implicitly convert the `Expected` to any other type to which all alternatives
    can be converted.  If an error state is not directly convertible to the type, the
    algorithm will try again with the type wrapped in `std::unexpected` instead. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr operator to(this Self&& self)
        noexcept (meta::nothrow::exhaustive<impl::expected_convert_to<Self, to>, Self>)
        requires (meta::exhaustive<impl::expected_convert_to<Self, to>, Self>)
    {
        return impl::visit(
            impl::expected_convert_to<Self, to>{},
            std::forward<Self>(self)
        );
    }

    /* Swap the contents of two expecteds as efficiently as possible. */
    constexpr void swap(Expected& other)
        noexcept (requires{{_value.swap(other._value)} noexcept;})
        requires (requires{{_value.swap(other._value)};})
    {
        if (this != &other) {
            _value.swap(other._value);
        }
    }

    /* True if the `Expected` stores a valid result.  `False` if it is in an error
    state. */
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return _value.index() == 0;
    }

    /* Access the valid state.  Throws a `BadUnionAccess` assertion if the expected is
    currently in the error state and the program is compiled in debug mode.  Fails to
    compile if the result type is void. */
    template <typename Self> requires (meta::not_void<value_type>)
    [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept (!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_error()) {
                throw BadUnionAccess("Expected in error state has no result");
            }
        }
        return (std::forward<Self>(self)._value.template value<0>());
    }

    /* If the expected is in an error state, invoke a given visitor over the possible
    errors and return its result, otherwise propagate the non-error state.  This is
    identical to invoking a manual visitor (e.g. a `def` statement) over the expected,
    except that the visitor function must be exhaustive over all possible errors, and
    the non-error state is implicitly forwarded, rather than the error state(s).  All
    other rules (including promotion to union, handling of void return types) remain
    the same.

    For most visitors, where `f(e)...` returns the same type as `Expected.value()`:

                self                    invoke                  result
        -----------------------------------------------------------------------
        1.  Expected<T, Es...>(e)       f(e) -> t           T(t)
        2.  Expected<T, Es...>(t)       (no call)           T(t)

    If `f(e)...` returns one or more `Us...` types that differ from the expected:

                self                    invoke                  result
        -----------------------------------------------------------------------
        1.  Expected<T, Es...>(e)       f(e) -> u           Union<T, Us...>(u)
        2.  Expected<T, Es...>(t)       (no call)           Union<T, Us...>(t)

    If `f(e)...` returns one or more `Expected<U, Xs...>`, where `U` and/or `Xs...`
    may or may not overlap with `T` and `Es...`, and may differ across overloads:

                self                    invoke                  result
        -----------------------------------------------------------------------
        1.  Expected<T, Es...>(e)       f(e) -> x           Expected<Union<T, Us...>, Xs...>(x)
        2.  Expected<T, Es...>(t)       (no call)           Expected<Union<T, Us...>, Xs...>(t)

    If the visitor returns void for one or more error states:

                self                    invoke                  result
        -----------------------------------------------------------------------
        1.  Expected<T, Es...>(e)       f(e) -> void        Optional<T>(empty)
        2.  Expected<T, Es...>(t)       (no call)           Optional<T>(t)

    Finally, special handling is used if `T` is set to `void` or `NoneType` with
    arbitrary qualifications, which is converted into an optional empty state or
    dropped altogether, depending on the visitor:

                self                    invoke                  result
        -----------------------------------------------------------------------
        1. Expected<void, Es...>(e)     f(e) -> u           Optional<U>(u)
        2. Expected<void, Es...>(e)     f(e) -> x           Expected<Optional<Union<Us...>>, Xs...>(x)
        3. Expected<void, Es...>(e)     f(e) -> void        void
    */
    template <typename Self, typename... Fs>
    constexpr decltype(auto) value_or(this Self&& self, Fs&&... fs)
        noexcept (meta::nothrow::exhaustive<
            impl::expected_or_else<Self>,
            impl::overloads<meta::remove_rvalue<Fs>...>,
            Self
        >)
        requires (meta::exhaustive<
            impl::expected_or_else<Self>,
            impl::overloads<meta::remove_rvalue<Fs>...>,
            Self
        >)
    {
        return (impl::visit(
            impl::expected_or_else<Self>{},
            impl::overloads<meta::remove_rvalue<Fs>...>{std::forward<Fs>(fs)...},
            std::forward<Self>(self)
        ));
    }

    /* True if the `Expected` is in an error state.  False if it stores a valid
    result. */
    [[nodiscard]] constexpr bool has_error() const noexcept {
        return _value.index() > 0;
    }

    /* True if the `Expected` is in a specific error state identified by index.  False
    if it stores a valid result or an error other than the one indicated.  If only one
    error state is permitted, then this is identical to calling `has_error()` without
    any template parameters. */
    template <ssize_t I> requires (impl::valid_index<sizeof...(Es) + 1, I>)
    [[nodiscard]] constexpr bool has_error() const noexcept {
        return _value.index() == impl::normalize_index<sizeof...(Es) + 1, I>() + 1;
    }

    /* True if the `Expected` is in a specific error state indicated by type.  False
    if it stores a valid result or an error other than the one indicated.  If only one
    error state is permitted, then this is identical to calling `has_error()` without
    any template parameters. */
    template <typename Err> requires (errors::template contains<Err>())
    [[nodiscard]] constexpr bool has_error() const noexcept {
        return _value.index() == meta::index_of<Err, E, Es...> + 1;
    }

    /* Access the error state.  Throws a `BadUnionAccess` exception if the expected is
    currently in the valid state and the program is compiled in debug mode.  The result
    is either a single error or `bertrand::Union<>` of errors if there are more than
    one. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) error(this Self&& self) noexcept (!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_value()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
        }
        /// TODO: this has to be a vtable or if chain to extract the right error type.
        /// Probably I just build a manual vtable into the private section here, or
        /// define it as a visitor.  The second option is probably better, since it
        /// reuses all the same logic as union visitors.
        /// -> It probably can't be a visitor, because it can't handle the valid case.
        /// It therefore has to be a manual vtable and if chain.  When I refactor the
        /// union iterators to get rid of those, I should replicate that logic here
        /// in the private section, and probably keep the vtables in union_storage,
        /// to avoid possible metaprogramming hiccups and circular definitions.

        return (std::forward<Self>(self)._value.template value<1>());
    }

    /* Access a particular error by index.  This is equivalent to the non-templated
    `error()` method in the single error case, and is an optimized shorthand for
    `error().value<I>().value()` in the union case.  A `BadUnionAccess` exception will
    be thrown in debug mode if the expected is currently in the valid state, or if the
    indexed error is not the active member of the union. */
    template <ssize_t I, typename Self> requires (impl::valid_index<sizeof...(Es) + 1, I>)
    [[nodiscard]] constexpr decltype(auto) error(this Self&& self) noexcept (!DEBUG) {
        static constexpr size_t J = impl::normalize_index<sizeof...(Es) + 1, I>() + 1;
        if constexpr (DEBUG) {
            if (self._value.index() != J) {
                if (self._value.index() == 0) {
                    throw BadUnionAccess("Expected in valid state has no error");
                } else {
                    throw impl::union_index_error<J - 1>::template error<E, Es...>(
                        self._value.index() - 1
                    );
                }
            }
        }
        return (std::forward<Self>(self)._value.template value<J>());
    }

    /* Access a particular error by type.  This is equivalent to the non-templated
    `error()` method in the single error case, and is an optimized shorthand for
    `error().value<T>().value()` in the union case.  A `BadUnionAccess` exception will
    be thrown in debug mode if the expected is currently in the valid state, or if the
    specified error is not the active member of the union. */
    template <typename Err, typename Self> requires (errors::template contains<Err>())
    [[nodiscard]] constexpr decltype(auto) error(this Self&& self) noexcept (!DEBUG) {
        static constexpr size_t J = meta::index_of<Err, E, Es...> + 1;
        if constexpr (DEBUG) {
            if (self._value.index() != J) {
                if (self._value.index() == 0) {
                    throw BadUnionAccess("Expected in valid state has no error");
                } else {
                    throw impl::union_type_error<Err>::template error<E, Es...>(
                        self._value.index() - 1
                    );
                }
            }
        }
        return (std::forward<Self>(self)._value.template value<J>());
    }

    /* Monadic call operator.  If the expected type is a function-like object and is
    not in the error state, then this will return the result of that function wrapped
    in another expected.  Otherwise, it will propagate the error state. */
    template <typename Self, typename... A>
    constexpr decltype(auto) operator()(this Self&& self, A&&... args)
        noexcept (meta::nothrow::visit<impl::Call, Self, A...>)
        requires (meta::visit<impl::Call, Self, A...>)
    {
        return (impl::visit(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<A>(args)...
        ));
    }

    /* Monadic subscript operator.  If the expected type is a container that supports
    indexing with the given key, then this will return the result of the access wrapped
    in another expected.  Otherwise, it will propagate the error state. */
    template <typename Self, typename... K>
    constexpr decltype(auto) operator[](this Self&& self, K&&... keys)
        noexcept (meta::nothrow::visit<impl::Subscript, Self, K...>)
        requires (meta::visit<impl::Subscript, Self, K...>)
    {
        return (impl::visit(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<K>(keys)...
        ));
    }

    /* Return 0 if the expected is empty or `std::ranges::size(value())` otherwise.
    If `std::ranges::size(value())` would be malformed and the value is not iterable
    (meaning that iterating over the expected would return just a single element), then
    the result will be identical to `has_value()`.  If neither option is available,
    then this method will fail to compile. */
    [[nodiscard]] constexpr auto size() const
        noexcept (
            meta::nothrow::has_size<value_type> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
        requires (
            meta::has_size<value_type> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
    {
        if constexpr (meta::has_size<value_type>) {
            if (has_value()) {
                return std::ranges::size(_value.template value<0>());
            } else {
                return meta::size_type<T>(0);
            }
        } else {
            return size_t(has_value());
        }
    }

    /* Return 0 if the expected is empty or `std::ranges::ssize(value())` otherwise.
    If `std::ranges::ssize(value())` would be malformed and the value is not iterable
    (meaning that iterating over the expected would return just a single element), then
    the result will be identical to `has_value()`.  If neither option is available,
    then this method will fail to compile. */
    [[nodiscard]] constexpr auto ssize() const
        noexcept (
            meta::nothrow::has_ssize<value_type> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
        requires (
            meta::has_ssize<value_type> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
    {
        if constexpr (meta::has_ssize<value_type>) {
            if (has_value()) {
                return std::ranges::ssize(_value.template value<0>());
            } else {
                return meta::ssize_type<T>(0);
            }
        } else {
            return ssize_t(has_value());
        }
    }

    /* Return true if the expected is in an error state or
    `std::ranges::empty(value())` otherwise.  If `std::ranges::empty(value())` would be
    malformed and the value is not iterable (meaning that iterating over the expected
    would return just a single element), then the result will be identical to
    `!has_value()`.  If neither option is available, then this method will fail to
    compile. */
    [[nodiscard]] constexpr bool empty() const
        noexcept (
            meta::nothrow::has_empty<value_type> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
        requires (
            meta::has_empty<value_type> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
    {
        if constexpr (meta::has_empty<value_type>) {
            return has_value() ? std::ranges::empty(_value.template value<0>()) : true;
        } else {
            return !has_value();
        }
    }

    /* Get a forward iterator over the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `begin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `end()` iterator if the
    expected is currently in an error state. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self& self)
        noexcept (requires{{impl::make_optional_iterator<Self&>::begin(self)} noexcept;})
        requires (requires{{impl::make_optional_iterator<Self&>::begin(self)};})
    {
        return impl::make_optional_iterator<Self&>::begin(self);
    }

    /* Get a forward iterator over the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `cbegin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `end()` iterator if the
    expected is currently in an error state. */
    [[nodiscard]] constexpr auto cbegin() const
        noexcept (requires{
            {impl::make_optional_iterator<const Expected&>::begin(*this)} noexcept;
        })
        requires (requires{
            {impl::make_optional_iterator<const Expected&>::begin(*this)};
        })
    {
        return impl::make_optional_iterator<const Expected&>::begin(*this);
    }

    /* Get a forward sentinel for the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `end()` type.  Otherwise, it will
    return an empty iterator. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self& self)
        noexcept (requires{{impl::make_optional_iterator<Self&>::end(self)} noexcept;})
        requires (requires{{impl::make_optional_iterator<Self&>::end(self)};})
    {
        return impl::make_optional_iterator<Self&>::end(self);
    }

    /* Get a forward sentinel for the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `cend()` type.  Otherwise, it will
    return an empty iterator. */
    [[nodiscard]] constexpr auto cend() const
        noexcept (requires{
            {impl::make_optional_iterator<const Expected&>::end(*this)} noexcept;
        })
        requires (requires{
            {impl::make_optional_iterator<const Expected&>::end(*this)};
        })
    {
        return impl::make_optional_iterator<const Expected&>::end(*this);
    }

    /* Get a reverse iterator over the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `rbegin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `rend()` iterator if the
    expected is currently in an error state. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self& self)
        noexcept (requires{{impl::make_optional_iterator<Self&>::rbegin(self)} noexcept;})
        requires (requires{{impl::make_optional_iterator<Self&>::rbegin(self)};})
    {
        return impl::make_optional_iterator<Self&>::rbegin(self);
    }

    /* Get a reverse iterator over the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `crbegin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `crend()` iterator if the
    expected is currently in an error state. */
    [[nodiscard]] constexpr auto crbegin() const
        noexcept (requires{
            {impl::make_optional_iterator<const Expected&>::rbegin(*this)} noexcept;
        })
        requires (requires{
            {impl::make_optional_iterator<const Expected&>::rbegin(*this)};
        })
    {
        return impl::make_optional_iterator<const Expected&>::rbegin(*this);
    }

    /* Get a reverse sentinel for the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `rend()` type.  Otherwise, it will
    return an empty iterator. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self& self)
        noexcept (requires{{impl::make_optional_iterator<Self&>::rend(self)} noexcept;})
        requires (requires{{impl::make_optional_iterator<Self&>::rend(self)};})
    {
        return impl::make_optional_iterator<Self&>::rend(self);
    }

    /* Get a reverse sentinel for the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `crend()` type.  Otherwise, it will
    return an empty iterator. */
    [[nodiscard]] constexpr auto crend() const
        noexcept (requires{
            {impl::make_optional_iterator<const Expected&>::rend(*this)} noexcept;
        })
        requires (requires{
            {impl::make_optional_iterator<const Expected&>::rend(*this)};
        })
    {
        return impl::make_optional_iterator<const Expected&>::rend(*this);
    }
};


/* ADL swap() operator for `bertrand::Union<Ts...>`.  Equivalent to calling `a.swap(b)`
as a member method. */
template <typename... Ts>
constexpr void swap(Union<Ts...>& a, Union<Ts...>& b)
    noexcept (requires{{a.swap(b)} noexcept;})
    requires (requires{{a.swap(b)};})
{
    a.swap(b);
}


/* ADL swap() operator for `bertrand::Optional<T>`.  Equivalent to calling `a.swap(b)`
as a member method. */
template <typename T>
constexpr void swap(Optional<T>& a, Optional<T>& b)
    noexcept (requires{{a.swap(b)} noexcept;})
    requires (requires{{a.swap(b)};})
{
    a.swap(b);
}


/* ADL swap() operator for `bertrand::Expected<T, E>`.  Equivalent to calling
`a.swap(b)` as a member method. */
template <typename T, typename E>
constexpr void swap(Expected<T, E>& a, Expected<T, E>& b)
    noexcept (requires{{a.swap(b)} noexcept;})
    requires (requires{{a.swap(b)};})
{
    a.swap(b);
}


/* Monadic logical NOT operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports logical NOT. */
template <meta::monad T>
constexpr decltype(auto) operator!(T&& val)
    noexcept (meta::nothrow::visit<impl::LogicalNot, T>)
    requires (meta::visit<impl::LogicalNot, T>)
{
    return (impl::visit(impl::LogicalNot{}, std::forward<T>(val)));
}


/* Monadic logical AND operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports logical AND against the other
operand (which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator&&(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::LogicalAnd, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::LogicalAnd, L, R>)
{
    return (impl::visit(impl::LogicalAnd{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic logical OR operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports logical OR against the other
operand (which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator||(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::LogicalOr, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::LogicalOr, L, R>)
{
    return (impl::visit(impl::LogicalOr{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic less-than comparison operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports less-than comparisons against the
other operand (which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator<(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Less, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Less, L, R>)
{
    return (impl::visit(impl::Less{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic less-than-or-equal comparison operator.  Delegates to `impl::visit()`,
and is automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports less-than-or-equal comparisons
against the other operand (which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator<=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::LessEqual, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::LessEqual, L, R>)
{
    return (impl::visit(impl::LessEqual{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic equality operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports equality comparisons against the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator==(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Equal, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Equal, L, R>)
{
    return (impl::visit(impl::Equal{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic inequality operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports equality comparisons against the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator!=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::NotEqual, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::NotEqual, L, R>)
{
    return (impl::visit(impl::NotEqual{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic greater-than-or-equal comparison operator.  Delegates to `impl::visit()`,
and is automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports greater-than-or-equal comparisons
against the other operand (which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator>=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::GreaterEqual, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::GreaterEqual, L, R>)
{
    return (impl::visit(impl::GreaterEqual{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic greater-than comparison operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports greater-than-or-equal comparisons
against the other operand (which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator>(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Greater, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Greater, L, R>)
{
    return (impl::visit(impl::Greater{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic three-way comparison operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports three-way comparisons against the
other operand (which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator<=>(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Spaceship, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Spaceship, L, R>)
{
    return (impl::visit(impl::Spaceship{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic unary plus operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports unary plus. */
template <meta::monad T>
constexpr decltype(auto) operator+(T&& val)
    noexcept (meta::nothrow::visit<impl::Pos, T>)
    requires (meta::visit<impl::Pos, T>)
{
    return (impl::visit(impl::Pos{}, std::forward<T>(val)));
}


/* Monadic unary minus operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports unary minus. */
template <meta::monad T>
constexpr decltype(auto) operator-(T&& val)
    noexcept (meta::nothrow::visit<impl::Neg, T>)
    requires (meta::visit<impl::Neg, T>)
{
    return (impl::visit(impl::Neg{}, std::forward<T>(val)));
}


/* Monadic prefix increment operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one tyoe within the monad supports prefix increments. */
template <meta::monad T>
constexpr decltype(auto) operator++(T&& val)
    noexcept (meta::nothrow::visit<impl::PreIncrement, T>)
    requires (meta::visit<impl::PreIncrement, T>)
{
    return (impl::visit(impl::PreIncrement{}, std::forward<T>(val)));
}


/* Monadic postfix increment operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports postfix increments. */
template <meta::monad T>
constexpr decltype(auto) operator++(T&& val, int)
    noexcept (meta::nothrow::visit<impl::PostIncrement, T>)
    requires (meta::visit<impl::PostIncrement, T>)
{
    return (impl::visit(impl::PostIncrement{}, std::forward<T>(val)));
}


/* Monadic prefix decrement operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports prefix decrements. */
template <meta::monad T>
constexpr decltype(auto) operator--(T&& val)
    noexcept (meta::nothrow::visit<impl::PreDecrement, T>)
    requires (meta::visit<impl::PreDecrement, T>)
{
    return (impl::visit(impl::PreDecrement{}, std::forward<T>(val)));
}

/* Monadic postfix decrement operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports postfix decrements. */
template <meta::monad T>
constexpr decltype(auto) operator--(T&& val, int)
    noexcept (meta::nothrow::visit<impl::PostDecrement, T>)
    requires (meta::visit<impl::PostDecrement, T>)
{
    return (impl::visit(impl::PostDecrement{}, std::forward<T>(val)));
}


/* Monadic addition operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports addition with the other operand (which may be
another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator+(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Add, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Add, L, R>)
{
    return (impl::visit(impl::Add{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place addition operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports addition with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator+=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceAdd, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceAdd, L, R>)
{
    return (impl::visit(impl::InplaceAdd{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic subtraction operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports subtraction with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator-(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Subtract, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Subtract, L, R>)
{
    return (impl::visit(impl::Subtract{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place subtraction operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports subtraction with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator-=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceSubtract, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceSubtract, L, R>)
{
    return (impl::visit(
        impl::InplaceSubtract{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic multiplication operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports multiplication with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator*(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Multiply, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Multiply, L, R>)
{
    return (impl::visit(impl::Multiply{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place multiplication operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports multiplication with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator*=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceMultiply, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceMultiply, L, R>)
{
    return (impl::visit(
        impl::InplaceMultiply{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic division operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports division with the other operand (which may be
another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator/(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Divide, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Divide, L, R>)
{
    return (impl::visit(impl::Divide{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place division operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports division with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator/=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceDivide, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceDivide, L, R>)
{
    return (impl::visit(
        impl::InplaceDivide{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic modulus operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports modulus with the other operand (which may be
another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator%(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::Modulus, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::Modulus, L, R>)
{
    return (impl::visit(impl::Modulus{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place modulus operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports modulus with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator%=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceModulus, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceModulus, L, R>)
{
    return (impl::visit(
        impl::InplaceModulus{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic left shift operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports left shifts with the other operand (which may be
another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator<<(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::LeftShift, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::LeftShift, L, R>)
{
    return (impl::visit(impl::LeftShift{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place left shift operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports left shifts with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator<<=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceLeftShift, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceLeftShift, L, R>)
{
    return (impl::visit(
        impl::InplaceLeftShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic right shift operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports right shifts with the other operand (which may be
another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator>>(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::RightShift, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::RightShift, L, R>)
{
    return (impl::visit( impl::RightShift{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place right shift operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports right shifts with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator>>=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceRightShift, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceRightShift, L, R>)
{
    return (impl::visit(
        impl::InplaceRightShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic bitwise NOT operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise NOT. */
template <meta::monad T>
constexpr decltype(auto) operator~(T&& val)
    noexcept (meta::nothrow::visit<impl::BitwiseNot, T>)
    requires (meta::visit<impl::BitwiseNot, T>)
{
    return (impl::visit(impl::BitwiseNot{}, std::forward<T>(val)));
}


/* Monadic bitwise AND operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise AND with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator&(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::BitwiseAnd, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::BitwiseAnd, L, R>)
{
    return (impl::visit( impl::BitwiseAnd{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place bitwise AND operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise AND with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator&=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceBitwiseAnd, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceBitwiseAnd, L, R>)
{
    return (impl::visit(
        impl::InplaceBitwiseAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic bitwise OR operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports bitwise OR with the other operand (which may be
another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator|(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::BitwiseOr, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::BitwiseOr, L, R>)
{
    return (impl::visit(impl::BitwiseOr{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place bitwise OR operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise OR with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator|=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceBitwiseOr, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceBitwiseOr, L, R>)
{
    return (impl::visit(
        impl::InplaceBitwiseOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


/* Monadic bitwise XOR operator.  Delegates to `impl::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports bitwise XOR with the other operand (which may be
another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator^(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::BitwiseXor, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::BitwiseXor, L, R>)
{
    return (impl::visit(impl::BitwiseXor{}, std::forward<L>(lhs), std::forward<R>(rhs)));
}


/* Monadic in-place bitwise XOR operator.  Delegates to `impl::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise XOR with the other operand
(which may be another monad). */
template <typename L, typename R>
constexpr decltype(auto) operator^=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::visit<impl::InplaceBitwiseXor, L, R>)
    requires ((meta::monad<L> || meta::monad<R>) && meta::visit<impl::InplaceBitwiseXor, L, R>)
{
    return (impl::visit(
        impl::InplaceBitwiseXor{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


}


namespace std {

    /// TODO: make sure std::get() et. al. are correctly adapted to the new
    /// union/optional/expected interface, and consider perfectly forwarding the
    /// tuple interface if available.

    template <bertrand::meta::monad T>
        requires (bertrand::meta::visit<bertrand::impl::Hash, T>)
    struct hash<T> {
        static constexpr auto operator()(auto&& value) noexcept (
            bertrand::meta::nothrow::visit<bertrand::impl::Hash, T>
        ) {
            return bertrand::impl::visit(
                bertrand::impl::Hash{},
                std::forward<decltype(value)>(value)
            );
        }
    };

    template <typename... Ts>
    struct variant_size<bertrand::Union<Ts...>> :
        std::integral_constant<size_t, bertrand::Union<Ts...>::types::size()>
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
        requires (remove_cvref_t<U>::types::template contains<T>())
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
    constexpr auto* get_if(U&& u) noexcept (noexcept(u.template get_if<I>())) {
        auto result = u.template get_if<I>();  // as lvalue
        return result.has_value() ? &result.value() : nullptr;
    }

    /* Non-member `std::get_if<T>(union)` returns a pointer to the value if the type is
    active, or `nullptr` otherwise, similar to `std::get_if<T>(variant)`,
    and different from `union.get_if<T>()`, which returns an optional according to the
    monadic interface. */
    template <typename T, bertrand::meta::Union U>
        requires (remove_cvref_t<U>::types::template contains<T>())
    constexpr auto* get_if(U&& u) noexcept (noexcept(u.template get_if<T>())) {
        auto result = u.template get_if<T>();  // as lvalue
        return result.has_value() ? &result.value() : nullptr;
    }

}


#endif  // BERTRAND_UNION_H
