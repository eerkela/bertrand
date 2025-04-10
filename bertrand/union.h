#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


/// TODO: the name for this style of typing should be something like "magic" typing
/// ("magically typed").


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
            // 1. Convert arguments to a 2D pack of packs representing all possible
            //    permutations of the union types.
            template <typename First, typename... Rest>
            struct permute {
                using type = impl::visitable<First>::pack::template product<
                    typename impl::visitable<Rest>::pack...
                >;
            };
            using permutations = permute<Args...>::type;

            // 2. Filter out any permutations that are not valid inputs to the
            //    visitor `F` and deduce the unique return types as we go.
            template <typename>
            struct filter;
            template <typename... permutations>
            struct filter<bertrand::args<permutations...>> {
                // 2a. `invoke<P>` satisfied if `P` is a valid argument list for `F`
                template <typename>
                static constexpr bool invoke = false;
                template <typename... As>
                static constexpr bool invoke<bertrand::args<As...>> =
                    meta::invocable<F, As...>;

                // 2b. `nothrow_invoke<P>` satisfied if `P` is a valid argument list
                //     for `F` and does not throw exceptions.
                template <typename>
                static constexpr bool nothrow_invoke = false;
                template <typename... As>
                static constexpr bool nothrow_invoke<bertrand::args<As...>> =
                    meta::nothrow::invocable<F, As...>;

                // 2c. `returns<P, Rs...>` deduces the return type for a valid argument
                //     list `P` and appends it to `Rs...` if it is not void and not
                //     already present.
                template <typename...>
                struct returns;
                template <typename... As, typename... Rs>
                    requires (
                        meta::invocable<F, As...> &&
                        !meta::invoke_returns<void, F, As...> &&
                        (!::std::same_as<Rs, meta::invoke_type<F, As...>> && ...)
                    )
                struct returns<bertrand::args<As...>, Rs...> {
                    using type = bertrand::args<Rs..., meta::invoke_type<F, As...>>;
                    static constexpr bool has_void = false;
                };
                template <typename... As, typename... Rs>
                    requires (
                        meta::invocable<F, As...> &&
                        !meta::invoke_returns<void, F, As...> &&
                        (::std::same_as<Rs, meta::invoke_type<F, As...>> || ...)
                    )
                struct returns<bertrand::args<As...>, Rs...> {
                    using type = bertrand::args<Rs...>;
                    static constexpr bool has_void = false;
                };
                template <typename... As, typename... Rs>
                    requires (meta::invoke_returns<void, F, As...>)
                struct returns<bertrand::args<As...>, Rs...> {
                    using type = bertrand::args<Rs...>;
                    static constexpr bool has_void = true;
                };

                // 2d. `validate<>` recurs over all permutations, applying the above
                //     criteria.
                template <bool, bool, typename...>
                struct validate;

                // 2e. Invalid permutation - advance to next permutation
                template <
                    bool nothrow,  // true if all prior permutations are noexcept
                    bool has_void,  // true if a valid permutation returned void
                    typename... Rs,  // valid return types found so far
                    typename... valid,  // valid permutations found so far
                    typename P,  // permutation under consideration
                    typename... Ps  // remaining permutations
                >
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

                // 2f. Valid permutation - check noexcept, deduce return type, append
                //     to valid permutations, and then advance to next permutation
                template <
                    bool nothrow,
                    bool has_void,
                    typename... Rs,
                    typename... valid,
                    typename P,
                    typename... Ps
                > requires (invoke<P>)
                struct validate<
                    nothrow,
                    has_void,
                    bertrand::args<Rs...>,
                    bertrand::args<valid...>,
                    P,
                    Ps...
                > {
                    using result = validate<
                        nothrow && nothrow_invoke<P>,
                        has_void || returns<P, Rs...>::has_void,
                        typename returns<P, Rs...>::type,
                        bertrand::args<valid..., P>,
                        Ps...
                    >;
                    static constexpr bool nothrow_ = result::nothrow_;
                    static constexpr bool consistent = result::consistent;
                    static constexpr bool has_void_ = result::has_void_;
                    using return_types = result::return_types;
                    using subset = result::subset;
                };

                // 2g. Base case - no more permutations to check
                template <bool nothrow, bool has_void, typename... Rs, typename... valid>
                struct validate<
                    nothrow,
                    has_void,
                    bertrand::args<Rs...>,
                    bertrand::args<valid...>
                > {
                    static constexpr bool nothrow_ = nothrow;
                    static constexpr bool consistent = sizeof...(Rs) <= 1;
                    static constexpr bool has_void_ = has_void;
                    using return_types = bertrand::args<Rs...>;
                    using subset = bertrand::args<valid...>;
                };

                // 2h. Evaluate the `validate<>` metafunction and report results.
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
            using valid_permutations = filter<permutations>::subset;

            // 3. Once all valid permutations and unique return types have been
            //    identified, deduce the final return type and propagate any unhandled
            //    states.
            template <bool, typename...>
            struct deduce;
            template <
                bool option,  // true if a void return type or unhandled empty state exists
                typename... errors,  // tracks unhandled exception states
                typename... Ps,  // valid permutations
                typename A,  // argument under consideration
                typename... As  // remaining arguments
            >
            struct deduce<
                option,
                bertrand::args<errors...>,
                bertrand::args<Ps...>,
                A,
                As...
            > {
                static constexpr size_t I = sizeof...(Args) - (sizeof...(As) + 1);

                template <typename...>
                static constexpr bool valid = false;
                template <typename alt, typename... Ts>
                static constexpr bool valid<alt, bertrand::args<Ts...>> =
                    ::std::same_as<alt, meta::unpack_type<I, Ts...>>;

                template <typename alt>
                static constexpr bool empty_state =
                    meta::is<alt, typename impl::visitable<A>::empty>;

                template <typename alt>
                static constexpr bool error_state =
                    meta::Expected<A> && meta::inherits<alt, Exception>;

                template <bool, typename...>
                struct scan;

                // 3a. For every alternative of `A`, check to see if it is present in
                //     the valid permutations.  If so, then it is a valid case for the
                //     visitor, and we can proceed to the next alternative.
                template <bool opt, typename alt, typename... alts, typename... errs>
                struct scan<opt, bertrand::args<alt, alts...>, errs...> {
                    using type = scan<opt, bertrand::args<alts...>, errs...>::type;
                };

                // 3b. Otherwise, if the alternative is missing and represents the
                //     empty state of an `Optional`, then we can set `opt` to true and
                //     proceed to the next alternative.
                template <bool opt, typename alt, typename... alts, typename... errs>
                    requires (empty_state<alt> && (!valid<alt, Ps> && ...))
                struct scan<opt, bertrand::args<alt, alts...>, errs...> {
                    using type = scan<true, bertrand::args<alts...>, errs...>::type;
                };

                // 3c. Otherwise, if the alternative is missing and represents an error
                //     state of an `Expected` that has not previously been encountered,
                //     then we can add it to the `errors...` pack and proceed to the
                //     next alternative.
                template <bool opt, typename alt, typename... alts, typename... errs>
                    requires (
                        error_state<alt> &&
                        (!valid<alt, Ps> && ...) &&
                        (!::std::same_as<alt, errs> && ...)
                    )
                struct scan<opt, bertrand::args<alt, alts...>, errs...> {
                    using type = scan<opt, bertrand::args<alts...>, errs..., alt>::type;
                };
                template <bool opt, typename alt, typename... alts, typename... errs>
                    requires (
                        error_state<alt> &&
                        (!valid<alt, Ps> && ...) &&
                        (::std::same_as<alt, errs> || ...)
                    )
                struct scan<opt, bertrand::args<alt, alts...>, errs...> {
                    using type = scan<opt, bertrand::args<alts...>, errs...>::type;
                };

                // 3d. Otherwise, if the alternative is missing and does not correspond
                //     to either of the above cases, then we must insert an error state
                //     into the `errors...` pack and proceed to the next alternative.
                template <bool opt, typename alt, typename... alts, typename... errs>
                    requires (
                        !empty_state<alt> &&
                        !error_state<alt> &&
                        (!valid<alt, Ps> && ...) &&
                        (!::std::same_as<BadUnionAccess, errs> && ...)
                    )
                struct scan<opt, bertrand::args<alt, alts...>, errs...> {
                    using type = scan<
                        opt,
                        bertrand::args<alts...>,
                        errs...,
                        BadUnionAccess
                    >::type;
                };
                template <bool opt, typename alt, typename... alts, typename... errs>
                    requires (
                        !empty_state<alt> &&
                        !error_state<alt> &&
                        (!valid<alt, Ps> && ...) &&
                        (::std::same_as<BadUnionAccess, errs> || ...)
                    )
                struct scan<opt, bertrand::args<alt, alts...>, errs...> {
                    using type = scan<opt, bertrand::args<alts...>, errs...>::type;
                };

                // 3e. Once all alternatives have been scanned, recur for the next
                //     argument.
                template <bool opt, typename... errs>
                struct scan<opt, bertrand::args<>, errs...> {
                    using type = deduce<
                        opt,
                        bertrand::args<errs...>,
                        bertrand::args<Ps...>,
                        As...
                    >::type;
                };

                // 3f. Execute the `scan<>` metafunction.
                using type = scan<
                    option,
                    typename impl::visitable<A>::pack,
                    errors...
                >::type;
            };
            template <bool option, typename... errors, typename... Ps>
            struct deduce<option, bertrand::args<errors...>, bertrand::args<Ps...>> {
                template <typename>
                struct to_union;

                // 3g. If no non-void return types are found, then the return type must
                //     be consistently void by definition.
                template <>
                struct to_union<bertrand::args<>> { using type = void; };

                // 3h. If there is precisely one non-void return type, then that is
                //     the final return type, possible wrapped in `Optional` if there
                //     was an unhandled empty state or void return type.
                template <typename R>
                struct to_union<bertrand::args<R>> { using type = R; };

                // 3i. If there are multiple non-void return types, then we need to
                //     return a `Union` of those types, possibly wrapped in `Optional`
                //     if there was an unhandled empty state or void return type.
                template <typename... Rs>
                struct to_union<bertrand::args<Rs...>> {
                    using type = bertrand::Union<Rs...>;
                };

                // 3j. If `option` is true (indicating either an unhandled empty state
                //     or void return type), then the result deduces to `Optional<R>`,
                //     where `R` is the union of all non-void return types.  If `R` is
                //     itself `Optional`, then it will be flattened into the output.
                template <typename R, bool cnd>
                struct to_optional { using type = R; };
                template <meta::not_void R>
                struct to_optional<R, true> { using type = bertrand::Optional<R>; };
                template <meta::Optional R>
                struct to_optional<R, true> {
                    using type =
                        bertrand::Optional<decltype((::std::declval<R>().value()))>::type;
                };

                // 3k. Convert the final return type to an `Expected` encoding the
                //     unhandled error states, if any.
                template <typename R, typename... Es>
                struct to_expected { using type = bertrand::Expected<R, Es...>; };
                template <typename R>
                struct to_expected<R> { using type = R; };
                /// TODO: specializations to flatten R if R is already an `Expected`?

                // 3l. Evaluate the final return type.
                using type = to_expected<
                    typename to_optional<
                        typename to_union<
                            typename filter<permutations>::return_types
                        >::type,
                        option
                    >::type,
                    errors...
                >::type;
            };

        public:
            /* `meta::visitor` evaluates to true if at least one valid argument
            permutation exists. */
            static constexpr bool enable = valid_permutations::size() > 0;

            /* `meta::exhaustive` evaluates to true if all argument permutations are
            valid. */
            static constexpr bool exhaustive =
                permutations::size() == valid_permutations::size();

            /* `meta::consistent` evaluates to true if all valid argument permutations
            return the same type, or if there are no valid paths. */
            static constexpr bool consistent = filter<permutations>::consistent;

            /* `meta::nothrow::visitor` evaluates to true if all valid argument
            permutations are noexcept or if there are no valid paths. */
            static constexpr bool nothrow = filter<permutations>::nothrow;

            /* The final return type is deduced from the valid argument permutations,
            with special rules for unhandled empty or error states, which can be
            implicitly propagated. */
            using type = deduce<
                filter<permutations>::has_void,  // initially only recognize void return types
                bertrand::args<>,  // initially no unhandled errors
                valid_permutations,
                Args...
            >::type;
        };

        template <typename F>
        struct visit<F> {
        private:
            template <typename F2>
            struct returns { using type = void; };
            template <meta::invocable F2>
            struct returns<F2> { using type = meta::invoke_type<F2>; };

        public:
            static constexpr bool enable = meta::invocable<F>;
            static constexpr bool exhaustive = enable;
            static constexpr bool consistent = enable;
            static constexpr bool nothrow = meta::nothrow::invocable<F>;
            using type = returns<F>::type;
        };

        template <typename T, typename R>
        static constexpr bool visit_returns = meta::convertible_to<T, R>;
        template <typename... Ts, typename R>
        static constexpr bool visit_returns<bertrand::Union<Ts...>, R> =
            (visit_returns<Ts, R> && ...);
        template <typename T, typename R>
        static constexpr bool visit_returns<bertrand::Optional<T>, R> =
            visit_returns<T, R> && meta::convertible_to<void, R>;
        template <typename T, typename... Es, typename R>
        static constexpr bool visit_returns<bertrand::Expected<T, Es...>, R> =
            visit_returns<T, R>;

        template <typename T, typename R>
        static constexpr bool nothrow_visit_returns = meta::nothrow::convertible_to<T, R>;
        template <typename... Ts, typename R>
        static constexpr bool nothrow_visit_returns<bertrand::Union<Ts...>, R> =
            (nothrow_visit_returns<Ts, R> && ...);
        template <typename T, typename R>
        static constexpr bool nothrow_visit_returns<bertrand::Optional<T>, R> =
            nothrow_visit_returns<T, R> && meta::nothrow::convertible_to<void, R>;
        template <typename T, typename... Es, typename R>
        static constexpr bool nothrow_visit_returns<bertrand::Expected<T, Es...>, R> =
            nothrow_visit_returns<T, R>;

    }

    /* A visitor function can only be applied to a set of arguments if at least one
    permutation of the union types are valid. */
    template <typename F, typename... Args>
    concept visitor = detail::visit<F, Args...>::enable;

    /* Specifies that all permutations of the union types must be valid for the visitor
    function. */
    template <typename F, typename... Args>
    concept exhaustive = detail::visit<F, Args...>::exhaustive;

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
    expected type for every possible permutation. */
    template <typename Ret, typename F, typename... Args>
    concept visit_returns =
        visitor<F, Args...> && detail::visit_returns<visit_type<F, Args...>, Ret>;

    namespace nothrow {

        template <typename F, typename... Args>
        concept visitor =
            meta::visitor<F, Args...> && detail::visit<F, Args...>::nothrow;

        template <typename F, typename... Args>
        concept exhaustive =
            meta::exhaustive<F, Args...> && detail::visit<F, Args...>::nothrow;

        template <typename F, typename... Args>
        concept consistent =
            meta::consistent<F, Args...> && detail::visit<F, Args...>::nothrow;

        template <typename F, typename... Args> requires (visitor<F, Args...>)
        using visit_type = meta::visit_type<F, Args...>;

        template <typename Ret, typename F, typename... Args>
        concept visit_returns =
            visitor<F, Args...> &&
            detail::nothrow_visit_returns<visit_type<F, Args...>, Ret>;

    }

}


namespace impl {

    /// TODO: visitable<T> should probably expose more information that can be used in
    /// visit<>, but it's hard to know what exactly I need.  I probably need some way
    /// to flatten Optional<std::optional<T>> and std::optional<Optional<T>>

    template <typename T>
    struct visitable {
        static constexpr bool enable = false;  // meta::visitable<T> evaluates to false
        static constexpr bool monad = false;  // meta::monad<T> evaluates to false
        using type = T;
        using pack = bertrand::args<T>;
        using empty = void;  // no empty state

        /* The active index for non-visitable types is trivially zero. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return 0;
        }

        /* Dispatching to a non-visitable type trivially forwards to the next arg. */
        template <
            typename R,  // deduced return type
            size_t idx,  // encoded index of the current argument
            size_t... Prev,  // index sequence over processed arguments
            size_t... Next,  // index sequence over remaining arguments
            typename F,  // visitor function
            typename... A  // current arguments in-flight
        >
        [[gnu::always_inline]] static constexpr R dispatch(
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visitor<F, A...>)
        {
            constexpr size_t I = sizeof...(Prev);
            if constexpr (I + 1 == sizeof...(A)) {
                return std::forward<F>(func)(
                    meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                    meta::unpack_arg<I>(std::forward<A>(args)...)
                );
            } else {
                return visitable<meta::unpack_type<I + 1, A...>>::template dispatch<
                    R,
                    idx  // this index is guaranteed to be zero, so we can ignore it
                >(
                    std::make_index_sequence<I + 1>{},
                    std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                    std::forward<F>(func),
                    meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                    meta::unpack_arg<I>(std::forward<A>(args)...),
                    meta::unpack_arg<I + 1 + Next>(std::forward<A>(args)...)...
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

        /* Get the active index for a union of this type. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return u.index();
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
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visitor<F, A...>)
        {
            constexpr size_t I = sizeof...(Prev);
            constexpr size_t scale = (
                visitable<meta::unpack_type<I + 1 + Next, A...>>::pack::size() * ... * 1
            );
            constexpr size_t J = idx / scale;
            using type = decltype((meta::unpack_arg<I>(
                std::forward<A>(args)...
            ).m_storage.template get<J>()));

            if constexpr (meta::visitor<
                F,
                meta::unpack_type<Prev, A...>...,
                type,
                meta::unpack_type<I + 1 + Next, A...>...
            >) {
                if constexpr (I + 1 == sizeof...(A)) {
                    return std::forward<F>(func)(
                        meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<I>(
                            std::forward<A>(args)...
                        ).m_storage.template get<J>()
                    );
                } else {
                    return visitable<meta::unpack_type<I + 1, A...>>::template dispatch<
                        R,
                        idx % scale
                    >(
                        std::make_index_sequence<I + 1>{},
                        std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                        std::forward<F>(func),
                        meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<I>(
                            std::forward<A>(args)...
                        ).m_storage.template get<J>(),
                        meta::unpack_arg<I + 1 + Next>(
                            std::forward<A>(args)...
                        )...
                    );
                }
            } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                return BadUnionAccess(
                    "failed to invoke visitor for union argument " +
                    impl::int_to_static_string<I> + " with active type '" + type_name<
                        typename meta::unqualify<T>::template alternative<J>
                    > + "'"
                );
            } else {
                static_assert(
                    false,
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

        /* Get the active index for a union of this type. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return u.index();
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
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visitor<F, A...>)
        {
            constexpr size_t I = sizeof...(Prev);
            constexpr size_t scale = (
                visitable<meta::unpack_type<I + 1 + Next, A...>>::pack::size() * ... * 1
            );
            constexpr size_t J = idx / scale;
            using type = decltype((std::get<J>(meta::unpack_arg<I>(
                std::forward<A>(args)...
            ))));

            if constexpr (meta::visitor<
                F,
                meta::unpack_type<Prev, A...>...,
                type,
                meta::unpack_type<I + 1 + Next, A...>...
            >) {
                if constexpr (I + 1 == sizeof...(A)) {
                    return std::forward<F>(func)(
                        meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                        meta::unpack_arg<I>(
                            std::forward<A>(args)...
                        ).m_storage.template get<J>()
                    );
                } else {
                    return visitable<meta::unpack_type<I + 1, A...>>::template dispatch<
                        R,
                        idx % scale
                    >(
                        std::make_index_sequence<I + 1>{},
                        std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                        std::forward<F>(func),
                        meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                        std::get<J>(meta::unpack_arg<I>(
                            std::forward<A>(args)...
                        )),
                        meta::unpack_arg<I + 1 + Next>(
                            std::forward<A>(args)...
                        )...
                    );
                }
            } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                return BadUnionAccess(
                    "failed to invoke visitor for union argument " +
                    impl::int_to_static_string<I> + " with active type '" + type_name<
                        typename meta::unqualify<T>::template alternative<J>
                    > + "'"
                );
            } else {
                static_assert(
                    false,
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
        using pack = bertrand::args<
            std::nullopt_t,
            decltype((std::declval<T>().m_storage.value()))
        >;
        using empty = std::nullopt_t;

        /* The active index of an optional is 0 for the empty state and 1 for the
        non-empty state. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return u.has_value();
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
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visitor<F, A...>)
        {
            constexpr size_t I = sizeof...(Prev);
            constexpr size_t scale = (
                visitable<meta::unpack_type<I + 1 + Next, A...>>::pack::size() * ... * 1
            );
            constexpr size_t J = idx / scale;

            // empty state is implicitly propagated if left unhandled
            if constexpr (J == 0) {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    std::nullopt_t,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    if constexpr (I + 1 == sizeof...(A)) {
                        return std::forward<F>(func)(
                            meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                            std::nullopt
                        );
                    } else {
                        return visitable<meta::unpack_type<I + 1, A...>>::template dispatch<
                            R,
                            idx  // we know J == 0, so no need to take the remainder
                        >(
                            std::make_index_sequence<I + 1>{},
                            std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                            std::forward<F>(func),
                            meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                            std::nullopt,
                            meta::unpack_arg<I + 1 + Next>(
                                std::forward<A>(args)...
                            )...
                        );
                    }
                } else if constexpr (meta::convertible_to<std::nullopt_t, R>) {
                    return std::nullopt;
                } else {
                    static_assert(
                        false,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }

            // non-empty state returns an error if unhandled
            } else {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    decltype((meta::unpack_arg<I>(
                        std::forward<A>(args)...
                    ).m_storage.value())),
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    if constexpr (I + 1 == sizeof...(A)) {
                        return std::forward<F>(func)(
                            meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<I>(
                                std::forward<A>(args)...
                            ).m_storage.value()
                        );
                    } else {
                        return visitable<meta::unpack_type<I + 1, A...>>::template dispatch<
                            R,
                            idx - scale  // J == 1, so we can reduce to subtraction
                        >(
                            std::make_index_sequence<I + 1>{},
                            std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                            std::forward<F>(func),
                            meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<I>(
                                std::forward<A>(args)...
                            ).m_storage.value(),
                            meta::unpack_arg<I + 1 + Next>(
                                std::forward<A>(args)...
                            )...
                        );
                    }
                } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                    return BadUnionAccess(
                        "failed to invoke visitor for empty optional argument " +
                        impl::int_to_static_string<I>
                    );
                } else {
                    static_assert(
                        false,
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
        using pack = bertrand::args<
            std::nullopt_t,
            decltype((std::declval<T>().value()))
        >;
        using empty = std::nullopt_t;

        /* The active index of an optional is 0 for the empty state and 1 for the
        non-empty state. */
        template <meta::is<T> U>
        [[gnu::always_inline]] static constexpr size_t index(const U& u) noexcept {
            return u.has_value();
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
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>,
            F&& func,
            A&&... args
        )
            noexcept(meta::nothrow::visitor<F, A...>)
        {
            constexpr size_t I = sizeof...(Prev);
            constexpr size_t scale = (
                visitable<meta::unpack_type<I + 1 + Next, A...>>::pack::size() * ... * 1
            );
            constexpr size_t J = idx / scale;

            // empty state is implicitly propagated if left unhandled
            if constexpr (J == 0) {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    std::nullopt_t,
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    if constexpr (I + 1 == sizeof...(A)) {
                        return std::forward<F>(func)(
                            meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                            std::nullopt
                        );
                    } else {
                        return visitable<meta::unpack_type<I + 1, A...>>::template dispatch<
                            R,
                            idx  // we know J == 0, so no need to take the remainder
                        >(
                            std::make_index_sequence<I + 1>{},
                            std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                            std::forward<F>(func),
                            meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                            std::nullopt,
                            meta::unpack_arg<I + 1 + Next>(
                                std::forward<A>(args)...
                            )...
                        );
                    }
                } else if constexpr (meta::convertible_to<std::nullopt_t, R>) {
                    return std::nullopt;
                } else {
                    static_assert(
                        false,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }

            // non-empty state returns an error if unhandled
            } else {
                if constexpr (meta::visitor<
                    F,
                    meta::unpack_type<Prev, A...>...,
                    decltype((meta::unpack_arg<I>(
                        std::forward<A>(args)...
                    ).value())),
                    meta::unpack_type<I + 1 + Next, A...>...
                >) {
                    if constexpr (I + 1 == sizeof...(A)) {
                        return std::forward<F>(func)(
                            meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<I>(
                                std::forward<A>(args)...
                            ).value()
                        );
                    } else {
                        return visitable<meta::unpack_type<I + 1, A...>>::template dispatch<
                            R,
                            idx - scale  // J == 1, so we can reduce to subtraction
                        >(
                            std::make_index_sequence<I + 1>{},
                            std::make_index_sequence<sizeof...(A) - (I + 2)>{},
                            std::forward<F>(func),
                            meta::unpack_arg<Prev>(std::forward<A>(args)...)...,
                            meta::unpack_arg<I>(
                                std::forward<A>(args)...
                            ).value(),
                            meta::unpack_arg<I + 1 + Next>(
                                std::forward<A>(args)...
                            )...
                        );
                    }
                } else if constexpr (meta::convertible_to<BadUnionAccess, R>) {
                    return BadUnionAccess(
                        "failed to invoke visitor for empty optional argument " +
                        impl::int_to_static_string<I>
                    );
                } else {
                    static_assert(
                        false,
                        "unreachable: a non-exhaustive iterator must always "
                        "return an `Expected` result"
                    );
                    std::unreachable();
                }
            }
        }
    };

    /// TODO: finish implementing visitable<Expected>

    /* Expecteds are converted into packs including the result type (if not void)
    followed by all error types. */
    template <meta::Expected T>
    struct visitable<T> {
    private:
        static constexpr bool is_void =
            meta::is_void<typename meta::unqualify<T>::value_type>;
        static constexpr size_t n_errors = meta::unqualify<T>::errors::size();

        template <size_t I, typename... Ts>
        struct build_pack {
            using type = build_pack<
                I + 1,
                Ts...,
                decltype((std::declval<T>().m_storage.template get_error<I>()))
            >::type;
        };
        template <typename... Ts>
        struct build_pack<n_errors, Ts...> { using type = bertrand::args<Ts...>; };

        template <typename V>
        struct _pack {
            using type = build_pack<0, decltype((std::declval<T>().result()))>::type;

        };
        template <meta::is_void V>
        struct _pack<V> {
            using type = build_pack<0, std::nullopt_t>::type;
        };
    
    public:
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using pack = _pack<typename meta::unqualify<T>::value_type>::type;
        using empty = void;

        template <typename R, size_t I, typename F, typename... A>
            requires (
                I < sizeof...(A) &&
                meta::is<T, meta::unpack_type<I, A...>> &&
                meta::visitor<F, A...>
            )
        static constexpr R dispatch(F&& func, A&&... args)
            noexcept(meta::nothrow::visitor<F, A...>)
        {
            static constexpr auto vtable = []<size_t... Prev, size_t... Next>(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>
            ) {
                return std::array{
                    // valid state has to handle void types
                    +[](meta::as_lvalue<F> func, meta::as_lvalue<A>... args) noexcept(
                        meta::nothrow::visitor<F, A...>
                    ) -> R {
                        /// TODO: what's the best way to handle void?
                    },
                    // error state has to recur on union types
                    +[](meta::as_lvalue<F> func, meta::as_lvalue<A>... args) noexcept(
                        meta::nothrow::visitor<F, A...>
                    ) -> R {
                        /// TODO: differentiate between single error and unions of
                        /// errors.  delegates to `visitable<Union<errors...>>::dispatch()`
                        /// to reuse existing logic
                    }
                };
            }(
                std::make_index_sequence<I>{},
                std::make_index_sequence<sizeof...(A) - I - 1>{}
            );

            // search the vtable based on valid/error state of the expected
            return vtable[meta::unpack_arg<I>(args...).has_error()](func, args...);
        }
    };

    /* `std::expected`s are treated like `Expected`. */
    template <meta::std::expected T>
    struct visitable<T> {
        /// TODO: implement this similar to the above
    };

    /* Helper function to compute an encoded index for a sequence of visitable
    arguments at runtime, which can then be used to index into a 1D vtable for all
    arguments simultaneously. */
    constexpr size_t visit_index(size_t i) noexcept {
        return i;
    }
    template <typename A, typename... As>
    constexpr size_t visit_index(size_t i, const A& first, const As&... rest) noexcept {
        constexpr size_t scale = (impl::visitable<As>::pack::size() * ... * 1);
        return visit_index(i + (visitable<A>::index(first) * scale), rest...);
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
        static constexpr size_t N = (impl::visitable<Args>::pack::size() * ... * 1);

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
            }(std::make_index_sequence<N>{});

        // `impl::visit_index()` produces an index into the vtable that encodes all
        // visitable arguments simultaneously.
        return vtable[impl::visit_index(0, args...)](f, args...);

    // otherwise, call the function directly
    } else {
        return std::forward<F>(f)(std::forward<Args>(args)...);
    }
}


/// TODO: figure out proper noexcept specifiers everywhere


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
                !meta::is_void<proximal_type<T>> &&
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

    size_t m_index;
    storage<Ts...> m_storage;

    template <size_t I, typename Self> requires (I < alternatives)
    using access = decltype((std::declval<Self>().m_storage.template get<I>()));

    template <size_t I, typename Self> requires (I < alternatives)
    using exp_val = Expected<access<I, Self>, BadUnionAccess>;

    template <size_t I, typename Self> requires (I < alternatives)
    using opt_val = Optional<access<I, Self>>;

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
    [[nodiscard]] constexpr exp_val<I, Self> get(this Self&& self) noexcept {
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
    [[nodiscard]] constexpr exp_val<index_of<T>, Self> get(this Self&& self) noexcept {
        if (self.index() != index_of<T>) {
            /// TODO: this can't work until static_str is fully defined
            // return get_type_error<T>[self.index()]();
        }
        return std::forward<Self>(self).m_storage.template get<index_of<T>>();
    }

    /* Get an optional wrapper for the value of the type at index `I`.  If that is not
    the active type, returns an empty optional instead. */
    template <size_t I, typename Self> requires (I < alternatives)
    [[nodiscard]] constexpr opt_val<I, Self> get_if(this Self&& self) noexcept(
        noexcept(opt_val<I, Self>(std::forward<Self>(self).m_storage.template get<I>()))
    ) {
        if (self.index() != I) {
            return {};
        }
        return std::forward<Self>(self).m_storage.template get<I>();
    }

    /* Get an optional wrapper for the value of the templated type.  If that is not
    the active type, returns an empty optional instead. */
    template <typename T, typename Self> requires (valid_alternative<T>)
    [[nodiscard]] constexpr opt_val<index_of<T>, Self> get_if(this Self&& self) noexcept(
        noexcept(opt_val<index_of<T>, Self>(
            std::forward<Self>(self).m_storage.template get<index_of<T>>()
        ))
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
    constexpr meta::visit_type<F, Self, Args...> visit(
        this Self&& self,
        F&& f,
        Args&&... args
    ) noexcept(
        meta::nothrow::visitor<F, Self, Args...>
    ) {
        return bertrand::visit(
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }

    /* Flatten the union into a single type, implicitly converting the active member to
    the common type, assuming one exists.  If no common type exists, then this will
    fail to compile.  Users can check the `can_flatten` flag to guard against this. */
    template <typename Self> requires (can_flatten)
    [[nodiscard]] constexpr common_type flatten(this Self&& self) noexcept(
        (meta::nothrow::convertible_to<Ts, common_type> && ...)
    ) {
        return std::forward<Self>(self).visit([](auto&& value) -> common_type {
            return std::forward<decltype(value)>(value);
        });
    }

private:
    static constexpr bool default_constructible = meta::not_void<default_type>;

    template <typename T>
    static constexpr bool can_convert = meta::not_void<conversion_type<T>>;

    template <size_t I, typename... Args>
        requires (I < alternatives && meta::constructible_from<alternative<I>, Args...>)
    constexpr Union(create_t<I>, Args&&... args)
        noexcept(noexcept(storage<Ts...>(create_t<I>{}, std::forward<Args>(args)...)))
    :
        m_index(I),
        m_storage(create_t<I>{}, std::forward<Args>(args)...)
    {}

    /// NOTE: copy/move constructors, destructors, and swap operators are implemented
    /// using manual vtables rather than relying on visit() in order to avoid possible
    /// infinite recursion.
    using copy_fn = storage<Ts...>(*)(const Union&);
    using move_fn = storage<Ts...>(*)(Union&&);
    using swap_fn = void(*)(Union&, Union&);
    using destructor_fn = void(*)(Union&);

    template <typename... Us> requires (meta::copyable<Us> && ...)
    static constexpr std::array<copy_fn, alternatives> copy_constructors {
        +[](const Union& other) noexcept(
            (meta::nothrow::copyable<Ts> && ...)
        ) -> storage<Ts...> {
            return {other.m_storage.template get<index_of<Ts>>()};
        }...
    };

    template <typename... Us> requires (meta::movable<Us> && ...)
    static constexpr std::array<move_fn, alternatives> move_constructors {
        +[](Union&& other) noexcept(
            (meta::nothrow::movable<Ts> && ...)
        ) -> storage<Ts...> {
            return {std::move(other).m_storage.template get<index_of<Ts>>()};
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
    static constexpr std::array<swap_fn, alternatives> pairwise_swap() noexcept {
        constexpr size_t I = index_of<T>;
        return {
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

    template <typename... Us> requires (meta::destructible<Us> && ...)
    static constexpr std::array<destructor_fn, alternatives> destructors {
        +[](Union& self) noexcept((meta::nothrow::destructible<Ts> && ...)) {
            if consteval {
                /// NOTE: compile-time exceptions are trivially destructible, and
                /// attempting to call its destructor explicitly will trip the
                /// compiler's UB filters, even though it should be safe by design.
                if constexpr (
                    !meta::trivially_destructible<Ts> &&
                    !meta::inherits<Ts, Exception>
                ) {
                    std::destroy_at(&self.m_storage.template get<index_of<Ts>>());
                }
            } else {
                if constexpr (!meta::trivially_destructible<Ts>) {
                    std::destroy_at(&self.m_storage.template get<index_of<Ts>>());
                }
            }
        }...
    };

public:
    /* Default constructor finds the first type in `Ts...` that can be default
    constructed.  If no such type exists, the default constructor is disabled. */
    constexpr Union()
        noexcept(meta::nothrow::default_constructible<default_type>)
        requires(default_constructible)
    :
        m_index(index_of<default_type>),
        m_storage()
    {}

    /* Conversion constructor finds the most proximal type in `Ts...` that can be
    implicitly converted from the input type.  This will prefer exact matches or
    differences in qualifications (preferring the least qualified) first, then
    inheritance relationships (preferring the most derived and least qualified), and
    finally implicit conversions (preferring the first match and ignoring lvalues).  If
    no such type exists, the conversion constructor is disabled. */
    template <typename T> requires (can_convert<T>)
    constexpr Union(T&& v)
        noexcept(meta::nothrow::convertible_to<T, conversion_type<T>>)
    :
        m_index(index_of<conversion_type<T>>),
        m_storage(std::forward<T>(v))
    {}

    /* Copy constructor.  The resulting union will have the same index as the input
    union, and will be initialized by copy constructing the other stored type. */
    constexpr Union(const Union& other)
        noexcept((meta::nothrow::copyable<Ts> && ...))
        requires((meta::copyable<Ts> && ...))
    :
        m_index(other.index()),
        m_storage(copy_constructors<Ts...>[other.index()](other))
    {}

    /* Move constructor.  The resulting union will have the same index as the input
    union, and will be initialized by move constructing the other stored type. */
    constexpr Union(Union&& other)
        noexcept((meta::nothrow::movable<Ts> && ...))
        requires((meta::movable<Ts> && ...))
    :
        m_index(other.index()),
        m_storage(move_constructors<Ts...>[other.index()](std::move(other)))
    {}

    /* Copy assignment operator.  Destroys the current value and then copy constructs
    the other stored type into the active index. */
    constexpr Union& operator=(const Union& other)
        noexcept((meta::nothrow::copyable<Ts> && ...) && (nothrow_swappable<Ts> && ...))
        requires((meta::copyable<Ts> && ...) && (swappable<Ts> && ...))
    {
        if (this != &other) {
            Union temp(other);
            swap_operators<Ts...>[index()][temp.index()](*this, temp);
        }
        return *this;
    }

    /* Move assignment operator.  Destroys the current value and then move constructs
    the other stored type into the active index. */
    constexpr Union& operator=(Union&& other)
        noexcept((meta::nothrow::movable<Ts> && ...) && (nothrow_swappable<Ts> && ...))
        requires((meta::movable<Ts> && ...) && (swappable<Ts> && ...))
    {
        if (this != &other) {
            Union temp(std::move(other));
            swap_operators<Ts...>[index()][temp.index()](*this, temp);
        }
        return *this;
    }

    /* Destructor destroys the currently-active type. */
    constexpr ~Union()
        noexcept((meta::nothrow::destructible<Ts> && ...))
        requires((meta::destructible<Ts> && ...))
    {
        destructors<Ts...>[index()](*this);
    }

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

    /// TODO: implicit conversion to Unions of wider types (e.g. Union<A, B> to
    /// Union<A, B, C>) Maybe also to and from `std::variant`

    /* Explicitly convert the union into another type to which at least one member is
    convertible.  If the active member does not support the conversion, then a
    `TypeError` will be thrown. */
    template <typename Self, typename T>
        requires (meta::explicitly_convertible_to<Ts, T> || ...)
    [[nodiscard]] constexpr explicit operator T(this Self&& self)
        noexcept((meta::nothrow::explicitly_convertible_to<Ts, T> && ...))
    {
        return std::forward<Self>(self).visit([]<typename V>(V&& value) -> T {
            if constexpr (meta::explicitly_convertible_to<V, T>) {
                return static_cast<T>(value);
            } else {
                /// TODO: elaborate a bit on the error message
                throw TypeError("Cannot convert union type to target type T");
            }
        });
    }

    template <typename Self, typename... Args>
        requires (meta::visitor<impl::Call, Self, Args...>)
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args)
        noexcept(meta::nothrow::visitor<impl::Call, Self, Args...>)
    {
        return std::forward<Self>(self).visit(
            impl::Call{},
            std::forward<Args>(args)...
        );
    }

    template <typename Self, typename... Key>
        requires (meta::visitor<impl::Subscript, Self, Key...>)
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys)
        noexcept(meta::nothrow::visitor<impl::Subscript, Self, Key...>)
    {
        return std::forward<Self>(self).visit(
            impl::Subscript{},
            std::forward<Key>(keys)...
        );
    }

    /// TODO: dereference, ->* operator overloads?  These should be global operators?
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
            data(value_type(std::forward<V>(value)))
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

    template <typename R>
    struct _and_then_t { using type = Optional<R>; };
    template <meta::Optional R>
    struct _and_then_t<R> { using type = meta::unqualify<R>; };
    template <typename F, typename Self, typename... Args>
        requires (meta::invocable<F, access<Self>, Args...>)
    using and_then_t = _and_then_t<meta::invoke_type<F, access<Self>, Args...>>::type;

    template <typename R>
    struct _or_else_t {
        using type = Optional<meta::common_type<value_type, R>>;
    };
    template <meta::Optional R>
    struct _or_else_t<R> {
        using type = Optional<meta::common_type<
            value_type,
            typename meta::unqualify<R>::value_type
        >>;
    };
    template <typename F, typename... Args>
    using or_else_t = _or_else_t<meta::invoke_type<F, Args...>>::type;

    template <typename R>
    struct _opt { using type = Optional<R>; };
    template <meta::Optional R>
    struct _opt<R> { using type = Optional<decltype((std::declval<R>().value()))>; };
    template <typename R>
    using opt = _opt<R>::type;

public:
    [[nodiscard]] constexpr Optional() noexcept = default;
    [[nodiscard]] constexpr Optional(std::nullopt_t t) noexcept {}

    template <meta::convertible_to<value_type> V>
    [[nodiscard]] constexpr Optional(V&& v) noexcept(
        meta::nothrow::convertible_to<V, value_type>
    ) : 
        m_storage(std::forward<V>(v))
    {}

    template <typename... Args> requires (meta::constructible_from<value_type, Args...>)
    [[nodiscard]] constexpr explicit Optional(Args&&... args) noexcept(
        meta::nothrow::constructible_from<value_type, Args...>
    ) :
        m_storage(value_type(std::forward<Args>(args)...))
    {}

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

    /* Contextually convert the optional to a boolean.  Equivalent to checking
    `optional.has_value()`. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
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

    /* Access the stored value.  Throws a `BadUnionAccess` assertion if the program is
    compiled in debug mode and the optional is currently in the empty state. */
    template <typename Self>
    [[nodiscard]] constexpr access<Self> operator*(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.has_value()) {
                throw BadUnionAccess("Cannot access value of an empty Optional");
            }
        }
        return std::forward<Self>(self).m_storage.value();
    }

    /* Access a member of the stored value.  Throws a `BadUnionAccess` assertion if the
    program is compiled in debug mode and the optional is currently in the empty
    state. */
    [[nodiscard]] constexpr pointer operator->() noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!has_value()) {
                throw BadUnionAccess("Cannot access value of an empty Optional");
            }
        }
        return &m_storage.value();
    }

    /* Access a member of the stored value.  Throws a `BadUnionAccess` assertion if the
    program is compiled in debug mode and the optional is currently in the empty
    state. */
    [[nodiscard]] constexpr const_pointer operator->() const noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!has_value()) {
                throw BadUnionAccess("Cannot access value of an empty Optional");
            }
        }
        return &m_storage.value();
    }

    /* Access the stored value or return the default value if the optional is empty,
    converting to the common type between the wrapped type and the default value. */
    template <typename Self, typename V>
        requires (meta::has_common_type<access<Self>, V>)
    [[nodiscard]] constexpr meta::common_type<access<Self>, V> value_or(
        this Self&& self,
        V&& fallback
    ) noexcept(
        meta::nothrow::convertible_to<meta::common_type<access<Self>, V>, access<Self>> &&
        meta::nothrow::convertible_to<meta::common_type<access<Self>, V>, V>
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
    constexpr meta::visit_type<F, Self, Args...> visit(
        this Self&& self,
        F&& f,
        Args&&... args
    ) noexcept(
        meta::nothrow::visitor<F, Self, Args...>
    ) {
        return bertrand::visit(
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }

    /* If the optional is not empty, invoke a boolean predicate on the value, returning
    a new optional with an identical value where the result is `true`, or an empty
    optional where it is `false`.  Any additional arguments in `Args...` will be
    perfectly forwarded to the predicate function in the order they are provided.
    If the original optional was empty to begin with, the predicate will not be
    invoked, and the optional will remain in the empty state.

                self                    invoke                      result
        -------------------------------------------------------------------------
        1.  Optional<T>(empty)  => (no call)                => Optional<T>(empty)
        2.  Optional<T>(x)      => f(x, args...) -> false   => Optional<T>(empty)
        3.  Optional<T>(x)      => f(x, args...) -> true    => Optional<T>(x)
    */
    template <typename Self, typename F, typename... Args>
        requires (meta::invoke_returns<bool, F, access<Self>, Args...>)
    constexpr Optional filter(this Self&& self, F&& f, Args&&... args) noexcept(
        meta::nothrow::invoke_returns<bool, F, access<Self>, Args...>
    ) {
        if (!self.has_value() || !std::forward<F>(f)(
            std::forward<Self>(self).m_storage.value(),
            std::forward<Args>(args)...
        )) {
            return {};
        }
        return std::forward<Self>(self);
    }

    /* Invoke a function on the wrapped value if the optional is not empty, returning
    another optional containing the transformed result.  If the original optional was
    in the empty state, then the function will not be invoked, and the result will be
    empty as well.  If the function returns an `Optional`, then it will be flattened
    into the result, merging the empty states.
        
                self                    invoke                      result
        -------------------------------------------------------------------------
        1.  Optional<T>(empty)  => (no call)                => Optional<U>(empty)
        2.  Optional<T>(x)      => f(x, args...) -> empty   => Optional<U>(empty)
        3.  Optional<T>(x)      => f(x, args...) -> y       => Optional<U>(y)

    Some functional languages call this operation "flatMap" or "bind".

    NOTE: this method's flattening behavior differs from `std::optional::and_then()`
    and `std::optional::transform()`.  For those methods, the former requires the
    function to always return another `std::optional`, whereas the latter implicitly
    promotes the return type to `std::optional`, even if it was one already (possibly
    resulting in a nested optional).  Bertrand's `and_then()` method splits the
    difference by allowing the function to return either an `Optional<U>` or `U`
    directly, producing a flattened `Optional<U>` in both cases.  This is done to guard
    against nested optionals, which Bertrand considers to be an anti-pattern, as
    accessing the result is necessarily more verbose and can easily cause confusion.
    This is especially true if the nesting is dependent on the order of operations,
    which can lead to subtle bugs.  If the empty states must remain distinct, then it
    is preferable to explicitly handle the original empty state before chaining, or by
    converting the optional into a `Union<Ts...>` or `Expected<T, Union<Es...>>`
    beforehand, both of which can represent multiple distinct empty states without
    nesting. */
    template <typename Self, typename F, typename... Args>
        requires (
            meta::invocable<F, access<Self>, Args...> &&
            meta::convertible_to<
                meta::invoke_type<F, access<Self>, Args...>,
                and_then_t<F, Self, Args...>
            >
        )
    [[nodiscard]] constexpr and_then_t<F, Self, Args...> and_then(
        this Self&& self,
        F&& f,
        Args&&... args
    ) noexcept(meta::nothrow::invoke_returns<
        and_then_t<F, Self, Args...>,
        F,
        access<Self>,
        Args...
    >) {
        if (!self.has_value()) {
            return {};
        }
        return std::forward<F>(f)(
            std::forward<Self>(self).m_storage.value(),
            std::forward<Args>(args)...
        );
    }

    /* Invoke a function if the optional is empty, returning another optional
    containing either the original value or the transformed result.  If the original
    optional was not empty, then the function will not be invoked, and the value will
    be converted to the common type between the original value and the result of the
    function.  If the function returns an `Optional`, then it will be flattened into
    the result.

                self                    invoke                      result
        -------------------------------------------------------------------------
        1.  Optional<T>(empty)  => f(args...) -> empty      => Optional<U>(empty)
        2.  Optional<T>(empty)  => f(args...) -> y          => Optional<U>(y)
        3.  Optional<T>(x)      => (no call)                => Optional<U>(x)

    Where `U` is the common type between the original value type `T` and the return
    type of the function `f()`.  If `f()` returns an `Optional<V>`, then `U` will be
    the common type between `T` and `V`.

    NOTE: this method's flattening behavior differs from `std::optional::or_else()`,
    which forces the function to always return another `std::optional`.  In Bertrand's
    `or_else()` method, the function can return either an `Optional<U>` or `U` directly,
    producing a flattened `Optional<U>` in both cases.  This is done for consistency
    with `and_then()`, which avoids nested optionals. */
    template <typename Self, typename F, typename... Args>
        requires (
            meta::invocable<F, Args...> &&
            meta::convertible_to<meta::invoke_type<F, Args...>, or_else_t<F, Args...>>
        )
    [[nodiscard]] constexpr or_else_t<F, Args...> or_else(
        this Self&& self,
        F&& f,
        Args&&... args
    ) noexcept(
        meta::nothrow::invoke_returns<or_else_t<F, Args...>, F, Args...> &&
        meta::nothrow::convertible_to<or_else_t<F, Args...>, access<Self>>
    ) {
        if (self.has_value()) {
            return std::forward<Self>(self).m_storage.value();
        }
        return std::forward<F>(f)(std::forward<Args>(args)...);
    }

    /* Monadic call operator.  If the optional type is a function-like object and is
    not in the empty state, then this will return the result of that function wrapped
    in another optional.  Otherwise, it will return the empty state.  If the function
    returns void, then the result will be void in both cases, and the function will
    not be invoked for the empty state. */
    template <typename Self, typename... Args>
        requires (meta::visitor<impl::Call, Self, Args...>)
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args)
        noexcept(meta::nothrow::visitor<impl::Call, Self, Args...>)
    {
        return std::forward<Self>(self).visit(
            impl::Call{},
            std::forward<Args>(args)...
        );
    }

    /* Monadic subscript operator.  If the optional type is a container that supports
    indexing with the given key, then this will return the result of the access wrapped
    in another optional.  Otherwise, it will return the empty state. */
    template <typename Self, typename... Key>
        requires (meta::visitor<impl::Subscript, Self, Key...>)
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys)
        noexcept(meta::nothrow::visitor<impl::Subscript, Self, Key...>)
    {
        return std::forward<Self>(self).visit(
            impl::Subscript{},
            std::forward<Key>(keys)...
        );
    }
};


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

    template <typename out, typename...>
    struct _search_error { using type = out; };
    template <typename out, typename in, typename curr, typename... next>
    struct _search_error<out, in, curr, next...> {
        using type = _search_error<out, in, next...>::type;
    };
    template <typename out, typename in, typename curr, typename... next>
        requires (!meta::constructible_from<value_type, in> && meta::inherits<in, curr>)
    struct _search_error<out, in, curr, next...> {
        template <typename prev>
        struct most_derived { using type = _search_error<prev, in, next...>::type; };
        template <typename prev>
            requires (meta::is_void<prev> || meta::inherits<curr, prev>)
        struct most_derived<prev> { using type = _search_error<curr, in, next...>::type; };
        using type = most_derived<out>::type;
    };
    template <typename in>
    using err_type = _search_error<void, in, E, Es...>::type;

    union storage {
        template <typename V>
        struct _Result { using type = meta::remove_rvalue<V>; };
        template <meta::lvalue V>
        struct _Result<V> { using type = meta::as_pointer<V>; };
        template <meta::is_void V>
        struct _Result<V> { struct type {}; };
        using Result = _Result<value_type>::type;

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

        union type {
            Result result;
            Error error;

            // default constructor for void results
            constexpr type() noexcept requires(meta::is_void<value_type>) :
                result()
            {}

            // forwarding constructor for non-void rvalue or prvalue results
            template <typename... As>
                requires (
                    meta::constructible_from<value_type, As...> &&
                    !meta::lvalue<value_type>
                )
            constexpr type(As&&... args)
                noexcept(meta::nothrow::constructible_from<value_type, As...>)
            :
                result(std::forward<As>(args)...)
            {}

            // forwarding constructor for lvalue results that converts to a pointer
            template <meta::convertible_to<value_type> V>
                requires (meta::lvalue<value_type>)
            constexpr type(V& v)
                noexcept(meta::nothrow::convertible_to<V, value_type>)
            :
                result(&static_cast<value_type>(v))
            {}

            // error constructor
            template <typename V> requires (meta::not_void<err_type<V>>)
            constexpr type(V&& e)
                noexcept(noexcept(_Error<E, Es...>{}(std::forward<V>(e))))
            :
                error(_Error<E, Es...>{}(std::forward<V>(e)))
            {}

            constexpr ~type() noexcept {}

            static constexpr type construct(bool ok, const type& other) noexcept(
                false
            ) {
                if (ok) {
                    return {.result = other.result};
                } else {
                    return {.error = other.error};
                }
            }

            static constexpr type construct(bool ok, type&& other) noexcept(
                false
            ) {
                if (ok) {
                    return {.result = std::move(other.result)};
                } else {
                    return {.error = std::move(other.error)};
                }
            }

            constexpr void assign(bool ok, bool other_ok, const type& other) noexcept(
                false
            ) {
                if (ok) {
                    if (other.has_result()) {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        result = other.result;
                    } else {
                        std::destroy_at(&result);
                        std::construct_at(&error, other.error);
                    }
                } else {
                    if (other.has_result()) {
                        std::destroy_at(&error);
                        std::construct_at(&result, other.result);
                    } else {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        error = other.error;
                    }
                }
            }

            constexpr void assign(bool ok, bool other_ok, type&& other) noexcept(
                false
            ) {
                if (ok) {
                    if (other.has_result()) {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        result = std::move(other.result);
                    } else {
                        std::destroy_at(&result);
                        std::construct_at(&error, std::move(other.error));
                    }
                } else {
                    if (other.has_result()) {
                        std::destroy_at(&error);
                        std::construct_at(&result, std::move(other.result));
                    } else {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        error = std::move(other.error);
                    }
                }
            }

            constexpr void destroy(bool ok) noexcept(
                false
            ) {
                if (ok) {
                    std::destroy_at(&result);
                } else {
                    std::destroy_at(&error);
                }
            }
        };

        type compile_time;
        type run_time;
        constexpr ~storage() noexcept {}
    } m_storage;
    bool m_compiled;
    bool m_ok;

public:
    /* Default constructor for void result types. */
    [[nodiscard]] constexpr Expected() noexcept requires(meta::is_void<value_type>) :
        m_storage([] noexcept -> storage {
            if consteval {
                return {.compile_time = {}};
            } else {
                return {.run_time = {}};
            }
        }()),
        m_compiled(std::is_constant_evaluated()),
        m_ok(true)
    {}

    /* Forwarding constructor for `value_type`. */
    template <typename... Args> requires (meta::constructible_from<value_type, Args...>)
    [[nodiscard]] constexpr Expected(Args&&... args) noexcept(
        noexcept(storage{.compile_time = {std::forward<Args>(args)...}}) &&
        noexcept(storage{.run_time = {std::forward<Args>(args)...}})
    ) :
        m_storage([](auto&&... args) noexcept(
            noexcept(storage{.compile_time = {std::forward<Args>(args)...}}) &&
            noexcept(storage{.run_time = {std::forward<Args>(args)...}})
        ) -> storage {
            if consteval {
                return {.compile_time = {std::forward<Args>(args)...}};
            } else {
                return {.run_time = {std::forward<Args>(args)...}};
            }
        }(std::forward<Args>(args)...)),
        m_compiled(std::is_constant_evaluated()),
        m_ok(true)
    {}

    /* Error state constructor.  This constructor only participates in overload
    resolution if the input inherits from one of the errors in the template signature
    and is not a valid initializer for `value_type`. */
    template <typename V>
        requires (
            !meta::constructible_from<value_type, V> &&
            (meta::inherits<V, E> || ... || meta::inherits<V, Es>)
        )
    [[nodiscard]] constexpr Expected(V&& e) noexcept(
        noexcept(storage{.compile_time = {std::forward<V>(e)}}) &&
        noexcept(storage{.run_time = {std::forward<V>(e)}})
    ) :
        m_storage([](auto&& e) noexcept(
            noexcept(storage{.compile_time = {std::forward<V>(e)}}) &&
            noexcept(storage{.run_time = {std::forward<V>(e)}})
        ) -> storage {
            if consteval {
                return {.compile_time = {std::forward<V>(e)}};
            } else {
                return {.run_time = {std::forward<V>(e)}};
            }
        }(std::forward<V>(e))),
        m_compiled(std::is_constant_evaluated()),
        m_ok(false)
    {}

    /* Copy constructor. */
    [[nodiscard]] constexpr Expected(const Expected& other)
        noexcept(meta::nothrow::copyable<typename storage::type>)
        requires(requires{meta::copyable<typename storage::type>;})
    :
        m_storage([](const Expected& other) noexcept(
            meta::nothrow::copyable<typename storage::type>
        ) -> storage {
            if consteval {
                return {.compile_time = storage::type::construct(
                    other.has_result(),
                    other.m_storage.compile_time
                )};
            } else {
                if (other.compiled()) {
                    return {.compile_time = storage::type::construct(
                        other.has_result(),
                        other.m_storage.compile_time
                    )};
                } else {
                    return {.run_time = storage::type::construct(
                        other.has_result(),
                        other.m_storage.run_time
                    )};
                }
            }
        }(other)),
        m_compiled(other.compiled()),
        m_ok(other.has_result())
    {}

    /* Move constructor. */
    [[nodiscard]] constexpr Expected(Expected&& other)
        noexcept(meta::nothrow::movable<typename storage::type>)
        requires(requires{meta::movable<typename storage::type>;})
    :
        m_storage([](Expected&& other) noexcept(
            meta::nothrow::movable<typename storage::type>
        ) -> storage {
            if consteval {
                return {.compile_time = storage::type::construct(
                    other.has_result(),
                    std::move(other.m_storage.compile_time)
                )};
            } else {
                if (other.compiled()) {
                    return {.compile_time = storage::type::construct(
                        other.has_result(),
                        std::move(other.m_storage.compile_time)
                    )};
                } else {
                    return {.run_time = storage::type::construct(
                        other.has_result(),
                        std::move(other.m_storage.run_time)
                    )};
                }
            }
        }(std::move(other))),
        m_compiled(other.compiled()),
        m_ok(other.has_result())
    {}

    /* Copy assignment operator. */
    constexpr Expected& operator=(const Expected& other) noexcept(
        false
    ) {
        if (this != &other) {
            if consteval {
                m_storage.compile_time.assign(
                    has_result(),
                    other.has_result(),
                    other.m_storage.compile_time
                );
            } else {
                if (compiled()) {
                    if (other.compiled()) {
                        m_storage.compile_time.assign(
                            has_result(),
                            other.has_result(),
                            other.m_storage.compile_time
                        );
                    } else {
                        std::destroy_at(&m_storage.compile_time);
                        std::construct_at(
                            &m_storage.run_time,
                            other.m_storage.run_time
                        );
                    }
                } else {
                    if (other.compiled()) {
                        std::destroy_at(&m_storage.run_time);
                        std::construct_at(
                            &m_storage.compile_time,
                            other.m_storage.compile_time
                        );
                    } else {
                        m_storage.run_time.assign(
                            has_result(),
                            other.has_result(),
                            other.m_storage.run_time
                        );
                    }
                }
            }
            m_compiled = other.compiled();
            m_ok = other.has_result();
        }
        return *this;
    }

    /* Move assignment operator. */
    constexpr Expected& operator=(Expected&& other) noexcept(
        false
    ) {
        if (this != &other) {
            if consteval {
                m_storage.compile_time.assign(
                    has_result(),
                    other.has_result(),
                    std::move(other.m_storage.compile_time)
                );
            } else {
                if (compiled()) {
                    if (other.compiled()) {
                        m_storage.compile_time.assign(
                            has_result(),
                            other.has_result(),
                            std::move(other.m_storage.compile_time)
                        );
                    } else {
                        std::destroy_at(&m_storage.run_time);
                        std::construct_at(
                            &m_storage.run_time,
                            std::move(other.m_storage.run_time)
                        );
                    }
                } else {
                    if (other.compiled()) {
                        std::destroy_at(&m_storage.run_time);
                        std::construct_at(
                            &m_storage.compile_time,
                            std::move(other.m_storage.compile_time)
                        );
                    } else {
                        m_storage.run_time.assign(
                            has_result(),
                            other.has_result(),
                            std::move(other.m_storage.run_time)
                        );
                    }
                }
            }
            m_compiled = other.compiled();
            m_ok = other.has_result();
        }
        return *this;
    }

    /* Destructor. */
    constexpr ~Expected() noexcept(meta::nothrow::destructible<typename storage::type>) {
        if consteval {
            /// NOTE: compile-time exceptions are trivially destructible, and
            /// attempting to call their destructors here will trip the compiler's UB
            /// filters, even though it should be safe by design.
            if constexpr (!meta::trivially_destructible<value_type>) {
                std::destroy_at(&m_storage.compile_time.data.result);
            }
        } else {
            if (compiled()) {
                m_storage.compile_time.destroy(has_result());
            } else {
                m_storage.run_time.destroy(has_result());
            }
        }
    }

    /// TODO: create<>() and swap()
    /// -> All exception types should have a .swap() method, so that I can generically
    /// call swap() on the error type and it will handle both the union and scalar
    /// cases.  Then I just need to focus on swapping the result types, which is
    /// simpler.

    /* `True` if the `Expected` was created at compile time.  `False` if it was created
    at runtime.  Compile-time exception states will not store a stack trace, and will
    not support polymorphism, so as to be constexpr-compatible. */
    [[nodiscard]] constexpr bool compiled() const noexcept { return m_compiled; }

    /* True if the `Expected` stores a valid result.  `False` if it is in an error
    state. */
    [[nodiscard]] constexpr bool has_result() const noexcept {
        return m_ok;
    }

    /* Access the valid state.  Throws a `BadUnionAccess` assertion if the expected is
    currently in the error state and the program is compiled in debug mode.  Fails to
    compile if the result type is void. */
    template <typename Self> requires (!meta::is_void<value_type>)
    [[nodiscard]] constexpr decltype(auto) result(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_error()) {
                throw BadUnionAccess("Expected in error state has no result");
            }
        }
        if constexpr (meta::lvalue<value_type>) {
            if consteval {
                return *std::forward<Self>(self).m_storage.compile_time.result;
            } else {
                if (self.compiled()) {
                    return *std::forward<Self>(self).m_storage.compile_time.result;
                } else {
                    return *std::forward<Self>(self).m_storage.run_time.result;
                }
            }
        } else {
            if consteval {
                return (std::forward<Self>(self).m_storage.compile_time.result);
            } else {
                if (self.compiled()) {
                    return (std::forward<Self>(self).m_storage.compile_time.result);
                } else {
                    return (std::forward<Self>(self).m_storage.run_time.result);
                }
            }
        }
    }

    /* True if the `Expected` is in an error state.  `False` if it stores a valid
    result. */
    [[nodiscard]] constexpr bool has_error() const noexcept {
        return !m_ok;
    }

    /* Access the error state.  Throws a `BadUnionAccess` exception if the expected is
    currently in the valid state and the program is compiled in debug mode. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) error(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (self.has_result()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
        }
        if consteval {
            return (std::forward<Self>(self).m_storage.compile_time.error);
        } else {
            if (self.compiled()) {
                return (std::forward<Self>(self).m_storage.compile_time.error);
            } else {
                return (std::forward<Self>(self).m_storage.run_time.error);
            }
        }
    }

    /* A member equivalent for `bertrand::visit()`, which always inserts this expected
    as the first argument, for chaining purposes.  See `bertrand::visit()` for more
    details. */
    template <typename F, typename Self, typename... Args>
        requires (meta::visitor<F, Self, Args...>)
    constexpr meta::visit_type<F, Self, Args...> visit(
        this Self&& self,
        F&& f,
        Args&&... args
    ) noexcept(
        meta::nothrow::visitor<F, Self, Args...>
    ) {
        return bertrand::visit(
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }

    /////////////////////
    ////    MONAD    ////
    /////////////////////

    /// TODO: and_then(), or_else() for Expected, similar to Optional.

    template <typename Self, typename... Args>
        requires (meta::visitor<impl::Call, Self, Args...>)
    constexpr decltype(auto) operator()(this Self&& self, Args&&... args)
        noexcept(meta::nothrow::visitor<impl::Call, Self, Args...>)
    {
        return std::forward<Self>(self).visit(
            impl::Call{},
            std::forward<Args>(args)...
        );
    }

    template <typename Self, typename... Key>
        requires (meta::visitor<impl::Subscript, Self, Key...>)
    constexpr decltype(auto) operator[](this Self&& self, Key&&... keys)
        noexcept(meta::nothrow::visitor<impl::Subscript, Self, Key...>)
    {
        return std::forward<Self>(self).visit(
            impl::Subscript{},
            std::forward<Key>(keys)...
        );
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
template <meta::monad T>
constexpr decltype(auto) operator!(T&& val)
    noexcept(meta::nothrow::visitor<impl::LogicalNot, T>)
{
    return visit(impl::LogicalNot{}, std::forward<T>(val));
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
constexpr decltype(auto) operator&&(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::LogicalAnd, L, R>)
{
    return visit(
        impl::LogicalAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator||(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::LogicalOr, L, R>)
{
    return visit(
        impl::LogicalOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator<(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Less, L, R>)
{
    return visit(
        impl::Less{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator<=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::LessEqual, L, R>)
{
    return visit(
        impl::LessEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator==(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Equal, L, R>)
{
    return visit(
        impl::Equal{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator!=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::NotEqual, L, R>)
{
    return visit(
        impl::NotEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator>=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::GreaterEqual, L, R>)
{
    return visit(
        impl::GreaterEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator>(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Greater, L, R>)
{
    return visit(
        impl::Greater{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator<=>(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Spaceship, L, R>)
{
    return visit(
        impl::Spaceship{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
}


/* Monadic unary plus operator.  Delegates to `bertrand::visit()`, and is automatically
defined for any type where `bertrand::meta::monad<T>` evaluates to true and at least
one type within the monad supports unary plus. */
template <meta::monad T> requires (meta::visitor<impl::Pos, T>)
constexpr decltype(auto) operator+(T&& val)
    noexcept(meta::nothrow::visitor<impl::Pos, T>)
{
    return visit(impl::Pos{}, std::forward<T>(val));
}


/* Monadic unary minus operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports unary minus. */
template <meta::monad T> requires (meta::visitor<impl::Neg, T>)
constexpr decltype(auto) operator-(T&& val)
    noexcept(meta::nothrow::visitor<impl::Neg, T>)
{
    return visit(impl::Neg{}, std::forward<T>(val));
}


/* Monadic prefix increment operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one tyoe within the monad supports prefix increments. */
template <meta::monad T> requires (meta::visitor<impl::PreIncrement, T>)
constexpr decltype(auto) operator++(T&& val)
    noexcept(meta::nothrow::visitor<impl::PreIncrement, T>)
{
    return visit(impl::PreIncrement{}, std::forward<T>(val));
}


/* Monadic postfix increment operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports postfix increments. */
template <meta::monad T> requires (meta::visitor<impl::PostIncrement, T>)
constexpr decltype(auto) operator++(T&& val, int)
    noexcept(meta::nothrow::visitor<impl::PostIncrement, T>)
{
    return visit(impl::PostIncrement{}, std::forward<T>(val));
}


/* Monadic prefix decrement operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports prefix decrements. */
template <meta::monad T> requires (meta::visitor<impl::PreDecrement, T>)
constexpr decltype(auto) operator--(T&& val)
    noexcept(meta::nothrow::visitor<impl::PreDecrement, T>)
{
    return visit(impl::PreDecrement{}, std::forward<T>(val));
}

/* Monadic postfix decrement operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to
true and at least one type within the monad supports postfix decrements. */
template <meta::monad T> requires (meta::visitor<impl::PostDecrement, T>)
constexpr decltype(auto) operator--(T&& val, int)
    noexcept(meta::nothrow::visitor<impl::PostDecrement, T>)
{
    return visit(impl::PostDecrement{}, std::forward<T>(val));
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
constexpr decltype(auto) operator+(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Add, L, R>)
{
    return visit(
        impl::Add{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator+=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::InplaceAdd, L, R>)
{
    return visit(
        impl::InplaceAdd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator-(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Subtract, L, R>)
{
    return visit(
        impl::Subtract{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator-=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::InplaceSubtract, L, R>)
{
    return visit(
        impl::InplaceSubtract{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator*(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Multiply, L, R>)
{
    return visit(
        impl::Multiply{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator*=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::InplaceMultiply, L, R>)
{
    return visit(
        impl::InplaceMultiply{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator/(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Divide, L, R>)
{
    return visit(
        impl::Divide{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator/=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::InplaceDivide, L, R>)
{
    return visit(
        impl::InplaceDivide{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator%(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::Modulus, L, R>)
{
    return visit(
        impl::Modulus{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator%=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::InplaceModulus, L, R>)
{
    return visit(
        impl::InplaceModulus{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
}


/* Monadic bitwise NOT operator.  Delegates to `bertrand::visit()`, and is
automatically defined for any type where `bertrand::meta::monad<T>` evaluates to true
and at least one type within the monad supports bitwise NOT. */
template <meta::monad T> requires (meta::visitor<impl::Invert, T>)
constexpr decltype(auto) operator~(T&& val)
    noexcept(meta::nothrow::visitor<impl::Invert, T>)
{
    return visit(impl::Invert{}, std::forward<T>(val));
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
constexpr decltype(auto) operator&(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::BitwiseAnd, L, R>)
{
    return visit(
        impl::BitwiseAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator&=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::InplaceBitwiseAnd, L, R>)
{
    return visit(
        impl::InplaceBitwiseAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator|(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::BitwiseOr, L, R>)
{
    return visit(
        impl::BitwiseOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator|=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::InplaceBitwiseOr, L, R>)
{
    return visit(
        impl::InplaceBitwiseOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator^(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::BitwiseXor, L, R>)
{
    return visit(
        impl::BitwiseXor{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
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
constexpr decltype(auto) operator^=(L&& lhs, R&& rhs)
    noexcept(meta::nothrow::visitor<impl::InplaceBitwiseXor, L, R>)
{
    return visit(
        impl::InplaceBitwiseXor{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    );
}


}


namespace std {

    template <bertrand::meta::Union U>
    struct variant_size<U> :
        std::integral_constant<size_t, remove_cvref_t<U>::alternatives>
    {};

    template <size_t I, bertrand::meta::Union U> requires (I < variant_size<U>::value)
    struct variant_alternative<I, U> {
        using type = remove_cvref_t<U>::template alternative<I>;
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
            static_assert(o3.value() == 4);
            static constexpr auto o4 = o3.filter(
                [](int x) { return x < 4; }
            );
            static_assert(!o4.has_value());
            // static_assert(o4.value() == 4);
            static_assert(o4.or_else([] { return 42; }).value() == 42);
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
            static constexpr TypeError err("An error occurred");
            static constexpr std::array<TypeError, 3> errs{err, err, err};
            static constexpr Expected<int, TypeError, ValueError> e1 = ValueError{"abc"};
            static_assert(e1.has_error());
            static_assert(e1.error().get<1>().result().message() == "abc");
            static constexpr Expected<int, TypeError, ValueError> e2 = 42;
            static_assert(!e2.has_error());
            static_assert(e2.result() == 42);
            static constexpr Union<TypeError, ValueError> u = TypeError{"abc"};
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
            static_assert(*o[1] == 2);
            static_assert(o->size() == 3);
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

        static_assert(sizeof(TypeError) == 24);
        static_assert(sizeof(Expected<int, TypeError>) == 32);
    }

}


#endif  // BERTRAND_UNION_H
