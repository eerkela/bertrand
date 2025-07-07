#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"


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
static_assert(
    MIN_VTABLE_SIZE >= 2,
    "vtable sizes with fewer than 2 elements are not meaningful"
);


namespace impl {
    struct union_storage_tag {};
    struct union_tag {};
    struct optional_tag {};
    struct expected_tag {};
    struct tuple_storage_tag {};

    /* A helper class that generates a manual vtable for a visitor function `F`, which
    must be a template class that accepts a single `size_t` parameter representing the
    index of the alternative being invoked.  Indexing the vtable object sets the
    number of alternatives and requested index, and returns a function object that
    performs the necessary dispatch. */
    template <template <size_t> typename F>
    struct vtable {
        template <typename>
        struct dispatch;
        template <size_t I, size_t... Is>
        struct dispatch<std::index_sequence<I, Is...>> {
            size_t index;

            template <typename... A>
            static constexpr bool nothrow = (
                meta::nothrow::callable<F<I>, A...> &&
                ... &&
                meta::nothrow::callable<F<Is>, A...>
            );

            template <typename... A>
            using type = meta::call_type<F<I>, A...>;

            /* If the vtable size is less than `MIN_VTABLE_SIZE`, then we can optimize
            the dispatch to a recursive `if` chain, which is easier for the compiler to
            optimize. */
            template <size_t J = 0, typename... A>
            [[gnu::always_inline]] constexpr type<A...> operator()(A&&... args) const
                noexcept (nothrow<A...>)
                requires (
                    sizeof...(Is) + 1 < MIN_VTABLE_SIZE &&
                    (meta::callable<F<I>, A...> && ... && meta::callable<F<Is>, A...>) &&
                    (std::same_as<meta::call_type<F<I>, A...>, meta::call_type<F<Is>, A...>> && ...)
                )
            {
                if constexpr (J < sizeof...(Is)) {
                    if (index == J) {
                        return F<J>{}(std::forward<A>(args)...);
                    } else {
                        return operator()<J + 1>(std::forward<A>(args)...);
                    }
                } else {
                    return F<J>{}(std::forward<A>(args)...);                
                }
            }

            template <typename... A>
            using ptr = type<A...>(*)(A...) noexcept (nothrow<A...>);

            template <size_t J, typename... A>
            static constexpr type<A...> fn(A... args) noexcept (nothrow<A...>) {
                return F<J>{}(std::forward<A>(args)...);
            }

            template <typename... A>
            static constexpr ptr<A...> table[sizeof...(Is) + 1] {
                &fn<I, A...>,
                &fn<Is + 1, A...>...
            };

            /* Otherwise, a normal vtable will be emitted and stored in the binary. */
            template <typename... A>
            [[gnu::always_inline]] constexpr type<A...> operator()(A&&... args) const
                noexcept (nothrow<A...>)
                requires (
                    sizeof...(Is) + 1 >= MIN_VTABLE_SIZE &&
                    (meta::callable<F<I>, A...> && ... && meta::callable<F<Is>, A...>) &&
                    (std::same_as<meta::call_type<F<I>, A...>, meta::call_type<F<Is>, A...>> && ...)
                )
            {
                return table<meta::forward<A>...>[index](std::forward<A>(args)...);
            }
        };

        /* Set the vtable size to the index sequence set by the first argument, and
        then select the alternative at the given index.  Returns a function object that
        can invoked to perform the actual dispatch, passing the arguments to the
        selected alternative.  This effectively promotes the runtime index to compile
        time, encoding it in `Is...`, which can apply different logic for each
        alternative.  Note that if the total number of alternatives is less than
        `MIN_VTABLE_SIZE`, a recursive `if` chain will be generated instead of a full
        vtable. */
        template <size_t... Is>
            requires ((sizeof...(Is) > 0) && ... && meta::default_constructible<F<Is>>)
        static constexpr auto operator[](std::index_sequence<Is...>, size_t i) noexcept {
            return dispatch<std::index_sequence<Is...>>{i};
        }
    };

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
    concept union_storage = inherits<T, impl::union_storage_tag>;

    template <typename T>
    concept Union = inherits<T, impl::union_tag>;

    template <typename T>
    concept Optional = inherits<T, impl::optional_tag>;

    template <typename T>
    concept unqualified_exception = unqualified<T> && Exception<T>;

    template <typename T>
    concept Expected = inherits<T, impl::expected_tag>;

    template <typename T>
    concept tuple_storage = inherits<T, impl::tuple_storage_tag>;

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


/// TODO: make sure these CTAD guides are correct after the visitable refactor.


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



/// TODO: document tuples.  These won't be fully defined until func.h, so that they
/// can integrate with argument annotations for named tuple support.  Eventually with
/// reflection, I can probably even make the argument names available through the
/// recursive inheritance structure, so you'd be able to just do
/// Tuple t{"foo"_ = 1, "bar"_ = 2.5};
/// t.foo;  // 1
/// t.bar;  // 2.5


template <meta::not_void... Ts>
struct Tuple;


template <meta::not_void... Ts>
Tuple(Ts&&...) -> Tuple<Ts...>;


/// TODO: the visitation internals (especially the impl::visitable hooks) might be
/// refactored to optimize for compilation speed.


namespace meta {

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
    /// -> What I should try to do is write a `visitable<T>` for `impl::union_storage`,
    /// and then have the union, optional, and expected specializations simply inherit
    /// from that and override the necessary methods to point them to the internal
    /// storage object.

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
                decltype((std::declval<T>().storage.template get<I>()))
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
            noexcept(std::forward<U>(u).storage.template get<I>())
        ) {
            return (std::forward<U>(u).storage.template get<I>());
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
            return u._value.index();
        }

        /// NOTE: the previous specialization had index 0 as the non-empty state, and
        /// index 1 as the empty state, which has since been reversed.  Make sure
        /// nothing else is broken.

        /* Perfectly forward the member at index I for an optional of this type. */
        template <size_t I, meta::is<T> U> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(U&& u) noexcept(
            noexcept(std::forward<U>(u)._value.template get<I>())
        ) {
            return (std::forward<U>(u)._value.template get<I>());
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
        using alternatives = _pack<0>::type;
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

    /* The tracking index stored within a `Union` is defined as the smallest unsigned
    integer type big enough to hold all alternatives, in order to exploit favorable
    packing dynamics with respect to the (aligned) contents. */
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

    /* Determine the common type for the members of the union and compile a vtable of 
    conversions to it, if one exists. */
    template <typename Self, typename = std::index_sequence<meta::unqualify<Self>::types::size()>>
    struct _union_flatten {
        using type = void;
        static constexpr bool nothrow = true;
    };
    template <typename Self, size_t... Is>
        requires (meta::has_common_type<decltype(std::declval<Self>().template get<Is>())...>)
    struct _union_flatten<Self, std::index_sequence<Is...>> {
        using type = meta::common_type<decltype(std::declval<Self>().template get<Is>())...>;

        static constexpr bool nothrow = (meta::nothrow::convertible_to<
            decltype(std::declval<Self>().template get<Is>()),
            type
        > && ...);

        template <size_t I>
        struct visit {
            static constexpr type operator()(Self self) noexcept (nothrow) {
                return std::forward<Self>(self).m_data.template get<I>();
            }
        };
    };
    template <typename Self> requires (meta::not_void<typename _union_flatten<Self>::type>)
    constexpr vtable<_union_flatten<Self>::template visit> union_flatten;

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
    struct union_storage : union_storage_tag {
        using indices = std::index_sequence_for<Ts...>;
        using types = meta::pack<Ts...>;
        using default_type = impl::union_default_type<Ts...>;

        template <size_t I> requires (I < sizeof...(Ts))
        using tag = std::in_place_index_t<I>;

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
            constexpr decltype(auto) get(this Self&& self) noexcept {
                if constexpr (I == 0) {
                    return (std::forward<Self>(self).curr);
                } else {
                    return (std::forward<Self>(self).rest.template get<I - 1>());
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
            constexpr decltype(auto) get(this Self&& self) noexcept {
                if constexpr (I == 0) {
                    return (std::forward<Self>(self).curr.ref);
                } else {
                    return (std::forward<Self>(self).rest.template get<I - 1>());
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
        [[nodiscard]] constexpr union_storage(tag<I> t, A&&... args)
            noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, Ts...>, A...>)
            requires (meta::constructible_from<meta::unpack_type<I, Ts...>, A...>)
        :
            m_data(t, std::forward<A>(args)...),
            m_index(I)
        {}

        /* Return the index of the active alternative. */
        [[nodiscard]] constexpr size_t index() const noexcept {
            return m_index;
        }

        /* Access a specific value by index, where the index is known at compile
        time. */
        template <size_t I, typename Self> requires (I < sizeof...(Ts))
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_data.template get<I>());
        }

    private:
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

        template <typename Self>
        using common_type = impl::_union_flatten<Self>::type;

        template <typename Self>
        static constexpr bool has_common_type = meta::not_void<common_type<Self>>;

        template <typename Self>
        static constexpr bool nothrow_common_type = impl::_union_flatten<Self>::nothrow;

        [[gnu::always_inline]] static constexpr type trivial_copy(const union_storage& other)
            noexcept
            requires (trivially_copyable)
        {
            type result;
            std::memcpy(&result, &other.m_data, sizeof(type));
            return result;
        }

        template <size_t I>
        struct copy {
            static constexpr type operator()(const union_storage& other)
                noexcept (nothrow_copyable)
            {
                return {tag<I>{}, other.m_data.template get<I>()};
            }
        };

        template <size_t I>
        struct move {
            static constexpr type operator()(union_storage&& other)
                noexcept (nothrow_movable)
            {
                return {tag<I>{}, std::move(other).m_data.template get<I>()};
            }
        };

        template <size_t I>
        struct destroy {
            static constexpr void operator()(union_storage& self)
                noexcept (nothrow_destructible)
            {
                self.m_data.template destroy<I>();
            }
        };

        template <size_t I>
        struct _swap {
            static constexpr void operator()(union_storage& self, union_storage& other)
                noexcept (nothrow_swappable)
            {
                static constexpr size_t J = I / sizeof...(Ts);
                static constexpr size_t K = I % sizeof...(Ts);
                using T = meta::remove_rvalue<meta::unpack_type<J, Ts...>>;

                // prefer a direct swap if the indices match and a corresponding operator
                // is available
                if constexpr (J == K && !meta::lvalue<T> && meta::swappable<T>) {
                    std::ranges::swap(
                        self.m_data.template get<J>(),
                        other.m_data.template get<K>()
                    );

                // otherwise, if the indices match and the types are move assignable, use a
                // temporary variable with best-effort error recovery
                } else if constexpr (J == K && !meta::lvalue<T> && meta::move_assignable<T>) {
                    T temp(std::move(self).m_data.template get<J>());
                    try {
                        self.m_data.template get<J>() = std::move(other).m_data.template get<K>();
                        try {
                            other.m_data.template get<K>() = std::move(temp);
                        } catch (...) {
                            other.m_data.template get<K>() =
                                std::move(self).m_data.template get<J>();
                            throw;
                        }
                    } catch (...) {
                        self.m_data.template get<J>() = std::move(temp);
                        throw;
                    }

                // If the indices differ or the types are lvalues, then we need to move
                // construct and destroy the original value behind us.
                } else {
                    T temp(std::move(self).m_data.template get<J>());
                    self.m_data.template destroy<J>();
                    try {
                        self.m_data.template construct<K>(
                            std::move(other).m_data.template get<K>()
                        );
                        other.m_data.template destroy<K>();
                        try {
                            other.m_data.template construct<J>(std::move(temp));
                        } catch (...) {
                            other.m_data.template construct<K>(
                                std::move(self).m_data.template get<K>()
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
        };

        static constexpr type dispatch_copy(const union_storage& other)
            noexcept (nothrow_copyable)
            requires (copyable)
        {
            if consteval {
                return impl::vtable<copy>{}[indices{}, other.index()](other);
            } else {
                if constexpr (trivially_copyable) {
                    return trivial_copy(other);
                } else {
                    return impl::vtable<copy>{}[indices{}, other.index()](other);
                }
            }
        }

        static constexpr type dispatch_move(union_storage&& other)
            noexcept (nothrow_movable)
            requires (movable)
        {
            if consteval {
                return impl::vtable<move>{}[indices{}, other.index()](std::move(other));
            } else {
                if constexpr (trivially_copyable) {
                    return trivial_copy(other);
                } else {
                    return impl::vtable<move>{}[indices{}, other.index()](std::move(other));
                }
            }
        }

        static constexpr void dispatch_destroy(union_storage& self)
            noexcept (nothrow_destructible)
            requires (destructible)
        {
            if constexpr (!trivially_destructible) {
                impl::vtable<destroy>{}[indices{}, self.index()](self);
            }
        }

        static constexpr void dispatch_swap(union_storage& lhs, union_storage& rhs)
            noexcept (nothrow_swappable)
            requires (swappable)
        {
            if consteval {
                return impl::vtable<_swap>{}[
                    std::make_index_sequence<sizeof...(Ts) * sizeof...(Ts)>{},
                    lhs.index() * sizeof...(Ts) + rhs.index()
                ](lhs, rhs);
            } else {
                if constexpr (trivially_copyable) {
                    union_storage temp = lhs;
                    std::memcpy(&lhs, &rhs, sizeof(union_storage));
                    std::memcpy(&rhs, &temp, sizeof(union_storage));
                } else {
                    return impl::vtable<_swap>{}[
                        std::make_index_sequence<sizeof...(Ts) * sizeof...(Ts)>{},
                        lhs.index() * sizeof...(Ts) + rhs.index()
                    ](lhs, rhs);
                }
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
            return impl::union_flatten<meta::forward<Self>>[
                std::index_sequence_for<Ts...>{},
                self.index()
            ](std::forward<Self>(self));
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
        using tag = std::in_place_index_t<I>;

        [[no_unique_address]] type m_data;

        /* Default constructor always initializes to the empty state. */
        [[nodiscard]] constexpr union_storage(tag<0> = tag<0>{}) noexcept : m_data(nullptr) {};

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

        /* Swap the contents of two unions as efficiently as possible. */
        constexpr void swap(union_storage& other) noexcept {
            std::swap(m_data, other.m_data);
        }

        /* Return the index of the active alternative. */
        [[nodiscard]] constexpr size_t index() const noexcept { return m_data != nullptr; }

        /* Access a specific value by index, where the index is known at compile
        time. */
        template <size_t I, typename Self> requires (I < 2)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (None);
            } else {
                return (*self.m_data);
            }
        }

    private:
        using common = impl::_union_flatten<const union_storage&>;

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
                std::in_place_index<meta::index_of<type<from>, Ts...>>,
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
                std::in_place_index<meta::index_of<type<A...>, Ts...>>,
                std::forward<A>(args)...
            };
        }
    };

    /* Union iterators can dereference to exactly one type or a `Union` of 2 or more
    possible types, depending on the configuration of the input iterators.  The `->`
    indirection operator is only enabled if all of the iterators support it and return
    a consistent pointer type. */
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
    using union_iterator_ref = _union_iterator_ref<meta::pack<>, meta::as_const_ref<Ts>...>::type;

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
        using arrow_type = impl::arrow_proxy<reference>;
        using pointer = arrow_type::type;
        using value_type = meta::remove_reference<reference>;

        union_storage<Ts...> _value;

    private:
        using indices = std::index_sequence_for<Ts...>;

        template <size_t I>
        struct _deref {
            static constexpr reference operator()(const union_iterator& self)
                noexcept (requires{{
                    *self._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<reference>;})
                requires (requires{
                    {*self._value.template get<I>()} -> meta::convertible_to<reference>;
                })
            {
                return *self._value.template get<I>();
            }
        };
        using deref = impl::vtable<_deref>::template dispatch<indices>;

        template <size_t I>
        struct _subscript {
            static constexpr reference operator()(const union_iterator& self, difference_type n)
                noexcept (requires{{
                    self._value.template get<I>()[n]
                } noexcept -> meta::nothrow::convertible_to<reference>;})
                requires (requires{
                    {self._value.template get<I>()[n]} -> meta::convertible_to<reference>;
                })
            {
                return self._value.template get<I>()[n];
            }
        };
        using subscript = impl::vtable<_subscript>::template dispatch<indices>;

        template <size_t I>
        struct _increment {
            static constexpr void operator()(union_iterator& self)
                noexcept (requires{{++self._value.template get<I>()} noexcept;})
                requires (requires{{++self._value.template get<I>()};})
            {
                ++self._value.template get<I>();
            }
        };
        using increment = impl::vtable<_increment>::template dispatch<indices>;

        template <size_t I>
        struct _add {
            static constexpr union_iterator operator()(const union_iterator& self, difference_type n)
                noexcept (requires{{
                    union_iterator{{std::in_place_index<I>, self._value.template get<I>() + n}}
                } noexcept -> meta::nothrow::convertible_to<union_iterator>;})
                requires (requires{{
                    union_iterator{{std::in_place_index<I>, self._value.template get<I>() + n}}
                };})
            {
                return {{std::in_place_index<I>, self._value.template get<I>() + n}};
            }
            static constexpr union_iterator operator()(difference_type n, const union_iterator& self)
                noexcept (requires{{
                    union_iterator{{std::in_place_index<I>, n + self._value.template get<I>()}}
                } noexcept -> meta::nothrow::convertible_to<union_iterator>;})
                requires (requires{{
                    union_iterator{{std::in_place_index<I>, n + self._value.template get<I>()}}
                };})
            {
                return {{std::in_place_index<I>, n + self._value.template get<I>()}};
            }
        };
        using add = impl::vtable<_add>::template dispatch<indices>;

        template <size_t I>
        struct _iadd {
            static constexpr void operator()(union_iterator& self, difference_type n)
                noexcept (requires{{self._value.template get<I>() += n} noexcept;})
                requires (requires{{self._value.template get<I>() += n};})
            {
                self._value.template get<I>() += n;
            }
        };
        using iadd = impl::vtable<_iadd>::template dispatch<indices>;

        template <size_t I>
        struct _decrement {
            static constexpr void operator()(union_iterator& self)
                noexcept (requires{{--self._value.template get<I>()} noexcept;})
                requires (requires{{--self._value.template get<I>()};})
            {
                --self._value.template get<I>();
            }
        };
        using decrement = impl::vtable<_decrement>::template dispatch<indices>;

        template <size_t I>
        struct _subtract {
            static constexpr union_iterator operator()(const union_iterator& self, difference_type n)
                noexcept (requires{{
                    union_iterator{{std::in_place_index<I>, self._value.template get<I>() - n}}
                } noexcept -> meta::nothrow::convertible_to<union_iterator>;})
                requires (requires{{
                    union_iterator{{std::in_place_index<I>, self._value.template get<I>() - n}}
                };})
            {
                return {{std::in_place_index<I>, self._value.template get<I>() - n}};
            }
        };
        using subtract = impl::vtable<_subtract>::template dispatch<indices>;

        template <size_t I>
        struct _isub {
            static constexpr void operator()(union_iterator& self, difference_type n)
                noexcept (requires{{self._value.template get<I>() -= n} noexcept;})
                requires (requires{{self._value.template get<I>() -= n};})
            {
                self._value.template get<I>() -= n;
            }
        };
        using isub = impl::vtable<_isub>::template dispatch<indices>;

        template <size_t I>
        struct _distance {
            template <typename other>
            static constexpr difference_type operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs._value.template get<I>() - rhs
                } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                requires (requires{{
                    lhs._value.template get<I>() - rhs
                } -> meta::convertible_to<difference_type>;})
            {
                return lhs._value.template get<I>() - rhs;
            }

            template <typename other>
            static constexpr difference_type operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs - rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                requires (requires{{
                    lhs - rhs._value.template get<I>()
                } -> meta::convertible_to<difference_type>;})
            {
                return lhs - rhs._value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr difference_type operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() - rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                requires (requires{{
                    lhs._value.template get<I>() - rhs._value.template get<I>()
                } -> meta::convertible_to<difference_type>;})
            {
                return lhs._value.template get<I>() - rhs._value.template get<I>();
            }
        };
        using distance = impl::vtable<_distance>::template dispatch<indices>;

        template <size_t I>
        struct _less {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs._value.template get<I>() < rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() < rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() < rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs < rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs < rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs < rhs._value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() < rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() < rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() < rhs._value.template get<I>();
            }
        };
        using less = impl::vtable<_less>::template dispatch<indices>;

        template <size_t I>
        struct _less_equal {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs._value.template get<I>() <= rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() <= rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() <= rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs <= rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs <= rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs <= rhs._value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() <= rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() <= rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() <= rhs._value.template get<I>();
            }
        };
        using less_equal = impl::vtable<_less_equal>::template dispatch<indices>;

        template <size_t I>
        struct _equal {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs._value.template get<I>() == rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() == rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() == rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs == rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs == rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs == rhs._value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() == rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() == rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() == rhs._value.template get<I>();
            }
        };
        using equal = impl::vtable<_equal>::template dispatch<indices>;

        template <size_t I>
        struct _unequal {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs._value.template get<I>() != rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() != rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() != rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs != rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs != rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs != rhs._value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() == rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() == rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() != rhs._value.template get<I>();
            }
        };
        using unequal = impl::vtable<_unequal>::template dispatch<indices>;

        template <size_t I>
        struct _greater_equal {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs._value.template get<I>() >= rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() >= rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() >= rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs >= rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs >= rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs >= rhs._value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() >= rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() >= rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() >= rhs._value.template get<I>();
            }
        };
        using greater_equal = impl::vtable<_greater_equal>::template dispatch<indices>;

        template <size_t I>
        struct _greater {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs._value.template get<I>() > rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() > rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() > rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs > rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs > rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs > rhs._value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() > rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs._value.template get<I>() > rhs._value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs._value.template get<I>() > rhs._value.template get<I>();
            }
        };
        using greater = impl::vtable<_greater>::template dispatch<indices>;

        template <typename other>
        static constexpr bool forward_spaceship = (
            meta::has_spaceship<const Ts&, const other&> &&
            ... &&
            meta::has_common_type<meta::spaceship_type<const Ts&, const other&>...>
        );
        template <typename... Us>
        static constexpr bool forward_spaceship<union_iterator<Us...>> = (
            meta::has_spaceship<const Ts&, const Us&> &&
            ... &&
            meta::has_common_type<meta::spaceship_type<const Ts&, const Us&>...>
        );
        template <typename other> requires (forward_spaceship<other>)
        using forward_spaceship_type = meta::common_type<
            meta::spaceship_type<const Ts&, const other&>...
        >;

        template <typename other>
        static constexpr bool reverse_spaceship = (
            meta::has_spaceship<const other&, const Ts&> &&
            ... &&
            meta::has_common_type<meta::spaceship_type<const other&, const Ts&>...>
        );
        template <typename other> requires (reverse_spaceship<other>)
        using reverse_spaceship_type = meta::common_type<
            meta::spaceship_type<const other&, const Ts&>...
        >;

        template <size_t I>
        struct _spaceship {
            template <typename other> requires (forward_spaceship<other>)
            static constexpr forward_spaceship_type<other> operator()(
                const union_iterator& lhs,
                const other& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() <=> rhs
                } noexcept -> meta::nothrow::convertible_to<forward_spaceship_type<other>>;})
            {
                return lhs._value.template get<I>() <=> rhs;
            }

            template <typename other> requires (reverse_spaceship<other>)
            static constexpr reverse_spaceship_type<other> operator()(
                const other& lhs,
                const union_iterator& rhs
            )
                noexcept (requires{{
                    lhs <=> rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<reverse_spaceship_type<other>>;})
            {
                return lhs <=> rhs._value.template get<I>();
            }

            template <typename... Us>
                requires (sizeof...(Us) == sizeof...(Ts) && forward_spaceship<union_iterator<Us...>>)
            static constexpr forward_spaceship_type<union_iterator<Us...>> operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs._value.template get<I>() <=> rhs._value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<
                    forward_spaceship_type<union_iterator<Us...>>
                >;})
            {
                return lhs._value.template get<I>() <=> rhs._value.template get<I>();
            }
        };
        using spaceship = impl::vtable<_spaceship>::template dispatch<indices>;

    public:
        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{{deref{_value.index()}(*this)} noexcept;})
            requires (requires{{deref{_value.index()}(*this)};})
        {
            return deref{_value.index()}(*this);
        }

        [[nodiscard]] constexpr arrow_type operator->() const
            noexcept (requires{{arrow_type{**this}} noexcept;})
            requires (requires{{arrow_type{**this}};})
        {
            return {**this};
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const
            noexcept (requires{{subscript{_value.index()}(*this)} noexcept;})
            requires (requires{{subscript{_value.index()}(*this)};})
        {
            return subscript{_value.index()}(*this, n);
        }

        constexpr union_iterator& operator++()
            noexcept (requires{{increment{_value.index()}(*this)} noexcept;})
            requires (requires{{increment{_value.index()}(*this)};})
        {
            increment{_value.index()}(*this);
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
            noexcept (requires{{add{self._value.index()}(self, n)} noexcept;})
            requires (requires{{add{self._value.index()}(self, n)};})
        {
            return add{self._value.index()}(self, n);
        }

        [[nodiscard]] friend constexpr union_iterator operator+(
            difference_type n,
            const union_iterator& self
        )
            noexcept (requires{{add{self._value.index()}(n, self)} noexcept;})
            requires (requires{{add{self._value.index()}(n, self)};})
        {
            return add{self._value.index()}(n, self);
        }

        constexpr union_iterator& operator+=(difference_type n)
            noexcept (requires{{iadd{_value.index()}(*this, n)} noexcept;})
            requires (requires{{iadd{_value.index()}(*this, n)};})
        {
            iadd{_value.index()}(*this, n);
            return *this;
        }

        constexpr union_iterator& operator--()
            noexcept (requires{{decrement{_value.index()}(*this)} noexcept;})
            requires (requires{{decrement{_value.index()}(*this)};})
        {
            decrement{_value.index()}(*this);
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
            noexcept (requires{{subtract{self._value.index()}(self, n)} noexcept;})
            requires (requires{{subtract{self._value.index()}(self, n)};})
        {
            return subtract{self._value.index()}(self, n);
        }

        template <typename other>
        [[nodiscard]] friend constexpr difference_type operator-(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{distance{lhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{distance{lhs._value.index()}(lhs, rhs)};})
        {
            return distance{lhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr difference_type operator-(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{distance{rhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{distance{rhs._value.index()}(lhs, rhs)};})
        {
            return distance{rhs._value.index()}(lhs, rhs);
        }

        constexpr union_iterator& operator-=(difference_type n)
            noexcept (requires{{isub{_value.index()}(*this, n)} noexcept;})
            requires (requires{{isub{_value.index()}(*this, n)};})
        {
            isub{_value.index()}(*this, n);
            return *this;
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator<(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{less{lhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{less{lhs._value.index()}(lhs, rhs)};})
        {
            return less{lhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator<(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{less{rhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{less{rhs._value.index()}(lhs, rhs)};})
        {
            return less{rhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator<=(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{less_equal{lhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{less_equal{lhs._value.index()}(lhs, rhs)};})
        {
            return less_equal{lhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator<=(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{less_equal{rhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{less_equal{rhs._value.index()}(lhs, rhs)};})
        {
            return less_equal{rhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator==(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{equal{lhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{equal{lhs._value.index()}(lhs, rhs)};})
        {
            return equal{lhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator==(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{equal{rhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{equal{rhs._value.index()}(lhs, rhs)};})
        {
            return equal{rhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator!=(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{unequal{lhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{unequal{lhs._value.index()}(lhs, rhs)};})
        {
            return unequal{lhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator!=(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{unequal{rhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{unequal{rhs._value.index()}(lhs, rhs)};})
        {
            return unequal{rhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator>=(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{greater_equal{lhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{greater_equal{lhs._value.index()}(lhs, rhs)};})
        {
            return greater_equal{lhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator>=(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{greater_equal{rhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{greater_equal{rhs._value.index()}(lhs, rhs)};})
        {
            return greater_equal{rhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator>(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{greater{lhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{greater{lhs._value.index()}(lhs, rhs)};})
        {
            return greater{lhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator>(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{greater{rhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{greater{rhs._value.index()}(lhs, rhs)};})
        {
            return greater{rhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr decltype(auto) operator<=>(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{spaceship{lhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{spaceship{lhs._value.index()}(lhs, rhs)};})
        {
            return spaceship{lhs._value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr decltype(auto) operator<=>(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{spaceship{rhs._value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{spaceship{rhs._value.index()}(lhs, rhs)};})
        {
            return spaceship{rhs._value.index()}(lhs, rhs);
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
            requires (meta::has_size<decltype(std::declval<U>()._value.template get<Is>())> && ...)
        struct _size_type<std::index_sequence<Is...>> {
            using type = meta::common_type<
                meta::size_type<decltype(std::declval<U>()._value.template get<Is>())>...
            >;
        };

        template <typename = std::make_index_sequence<N>>
        struct _ssize_type { using type = void; };
        template <size_t... Is>
            requires (meta::has_ssize<decltype(std::declval<U>()._value.template get<Is>())> && ...)
        struct _ssize_type<std::index_sequence<Is...>> {
            using type = meta::common_type<
                meta::ssize_type<decltype(std::declval<U>()._value.template get<Is>())>...
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
        using indices = std::make_index_sequence<N>;

        template <size_t I>
        struct begin_fn {
            static constexpr begin_type operator()(U u)
                noexcept (requires{{
                    std::ranges::begin(u._value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<begin_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    std::ranges::begin(u._value.template get<I>())
                } -> meta::convertible_to<begin_type>;})
            {
                return std::ranges::begin(u._value.template get<I>());
            }
            static constexpr begin_type operator()(U u)
                noexcept (requires{{begin_type{
                    {std::in_place_index<I>, std::ranges::begin(u._value.template get<I>())}
                }} noexcept;})
                requires (sizeof...(B) == N && !iter<B...>::direct && requires{{begin_type{
                    {std::in_place_index<I>, std::ranges::begin(u._value.template get<I>())}
                }};})
            {
                return {{std::in_place_index<I>, std::ranges::begin(u._value.template get<I>())}};
            }
        };
        using _begin = impl::vtable<begin_fn>::template dispatch<indices>;

        template <size_t I>
        struct end_fn {
            static constexpr end_type operator()(U u)
                noexcept (requires{{
                    std::ranges::end(u._value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<end_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    std::ranges::end(u._value.template get<I>())
                } -> meta::convertible_to<end_type>;})
            {
                return std::ranges::end(u._value.template get<I>());
            }
            static constexpr end_type operator()(U u)
                noexcept (requires{{end_type{
                    {std::in_place_index<I>, std::ranges::end(u._value.template get<I>())}
                }} noexcept;})
                requires (sizeof...(B) == N && !iter<B...>::direct && requires{{end_type{
                    {std::in_place_index<I>, std::ranges::end(u._value.template get<I>())}
                }};})
            {
                return {{std::in_place_index<I>, std::ranges::end(u._value.template get<I>())}};
            }
        };
        using _end = impl::vtable<end_fn>::template dispatch<indices>;

        template <size_t I>
        struct rbegin_fn {
            static constexpr rbegin_type operator()(U u)
                noexcept (requires{{
                    std::ranges::rbegin(u._value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<rbegin_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    std::ranges::rbegin(u._value.template get<I>())
                } -> meta::convertible_to<rbegin_type>;})
            {
                return std::ranges::rbegin(u._value.template get<I>());
            }
            static constexpr rbegin_type operator()(U u)
                noexcept (requires{{rbegin_type{
                    {std::in_place_index<I>, std::ranges::rbegin(u._value.template get<I>())}
                }} noexcept;})
                requires (sizeof...(B) == N && !iter<B...>::direct && requires{{rbegin_type{
                    {std::in_place_index<I>, std::ranges::rbegin(u._value.template get<I>())}
                }};})
            {
                return {{std::in_place_index<I>, std::ranges::rbegin(u._value.template get<I>())}};
            }
        };
        using _rbegin = impl::vtable<rbegin_fn>::template dispatch<indices>;

        template <size_t I>
        struct rend_fn {
            static constexpr rend_type operator()(U u)
                noexcept (requires{{
                    std::ranges::rend(u._value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<rend_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    std::ranges::rend(u._value.template get<I>())
                } -> meta::convertible_to<rend_type>;})
            {
                return std::ranges::rend(u._value.template get<I>());
            }
            static constexpr rend_type operator()(U u)
                noexcept (requires{{rend_type{
                    {std::in_place_index<I>, std::ranges::rend(u._value.template get<I>())}
                }} noexcept;})
                requires (sizeof...(B) == N && !iter<B...>::direct && requires{{rend_type{
                    {std::in_place_index<I>, std::ranges::rend(u._value.template get<I>())}
                }};})
            {
                return {{std::in_place_index<I>, std::ranges::rend(u._value.template get<I>())}};
            }
        };
        using _rend = impl::vtable<rend_fn>::template dispatch<indices>;

        template <size_t I>
        struct size_fn {
            static constexpr size_type operator()(U u)
                noexcept (requires{{
                    std::ranges::size(u._value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<size_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    std::ranges::size(u._value.template get<I>())
                } -> meta::convertible_to<size_type>;})
            {
                return std::ranges::size(u._value.template get<I>());
            }
        };
        using _size = impl::vtable<size_fn>::template dispatch<indices>;

        template <size_t I>
        struct ssize_fn {
            static constexpr ssize_type operator()(U u)
                noexcept (requires{{
                    std::ranges::ssize(u._value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<ssize_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    std::ranges::ssize(u._value.template get<I>())
                } -> meta::convertible_to<ssize_type>;})
            {
                return std::ranges::ssize(u._value.template get<I>());
            }
        };
        using _ssize = impl::vtable<ssize_fn>::template dispatch<indices>;

        template <size_t I>
        struct empty_fn {
            static constexpr bool operator()(U u)
                noexcept (requires{{
                    std::ranges::empty(u._value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    std::ranges::empty(u._value.template get<I>())
                } -> meta::convertible_to<bool>;})
            {
                return std::ranges::empty(u._value.template get<I>());
            }
        };
        using _empty = impl::vtable<empty_fn>::template dispatch<indices>;

    public:
        static constexpr begin_type begin(U u)
            noexcept (requires{{_begin{u._value.index()}(u)} noexcept;})
            requires (requires{{_begin{u._value.index()}(u)};})
        {
            return _begin{u._value.index()}(u);
        }

        static constexpr end_type end(U u)
            noexcept (requires{{_end{u._value.index()}(u)} noexcept;})
            requires (requires{{_end{u._value.index()}(u)};})
        {
            return _end{u._value.index()}(u);
        }

        static constexpr rbegin_type rbegin(U u)
            noexcept (requires{{_rbegin{u._value.index()}(u)} noexcept;})
            requires (requires{{_rbegin{u._value.index()}(u)};})
        {
            return _rbegin{u._value.index()}(u);
        }

        static constexpr rend_type rend(U u)
            noexcept (requires{{_rend{u._value.index()}(u)} noexcept;})
            requires (requires{{_rend{u._value.index()}(u)};})
        {
            return _rend{u._value.index()}(u);
        }

        static constexpr size_type size(U u)
            noexcept (requires{{_size{u._value.index()}(u)} noexcept;})
            requires (requires{{_size{u._value.index()}(u)};})
        {
            return _size{u._value.index()}(u);
        }

        static constexpr ssize_type ssize(U u)
            noexcept (requires{{_ssize{u._value.index()}(u)} noexcept;})
            requires (requires{{_ssize{u._value.index()}(u)};})
        {
            return _ssize{u._value.index()}(u);
        }

        static constexpr bool empty(U u)
            noexcept (requires{{_empty{u._value.index()}(u)} noexcept;})
            requires (requires{{_empty{u._value.index()}(u)};})
        {
            return _empty{u._value.index()}(u);
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
        meta::iterable<decltype(std::declval<U>()._value.template get<I>())> &&
        meta::reverse_iterable<decltype(std::declval<U>()._value.template get<I>())>
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
            decltype(std::declval<U>()._value.template get<I>())
        >>>,
        meta::pack<end..., meta::unqualify<meta::end_type<
            decltype(std::declval<U>()._value.template get<I>())
        >>>,
        meta::pack<rbegin..., meta::unqualify<meta::rbegin_type<
            decltype(std::declval<U>()._value.template get<I>())
        >>>,
        meta::pack<rend..., meta::unqualify<meta::rend_type<
            decltype(std::declval<U>()._value.template get<I>())
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
        meta::iterable<decltype(std::declval<U>()._value.template get<I>())> &&
        !meta::reverse_iterable<decltype(std::declval<U>()._value.template get<I>())>
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
            decltype(std::declval<U>()._value.template get<I>())
        >>>,
        meta::pack<end..., meta::unqualify<meta::end_type<
            decltype(std::declval<U>()._value.template get<I>())
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
        !meta::iterable<decltype(std::declval<U>()._value.template get<I>())> &&
        meta::reverse_iterable<decltype(std::declval<U>()._value.template get<I>())>
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
            decltype(std::declval<U>()._value.template get<I>())
        >>>,
        meta::pack<rend..., meta::unqualify<meta::rend_type<
            decltype(std::declval<U>()._value.template get<I>())
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
        !meta::iterable<decltype(std::declval<U>()._value.template get<I>())> &&
        !meta::reverse_iterable<decltype(std::declval<U>()._value.template get<I>())>
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

}


/// TODO: instead of `unpack()` and `comprehension()` being public operators, the
/// new tuple iterable refactor means I should be able to roll them into `range()` -
/// which is ideal.  Unambiguous unpacking can therefore be accomplished by
/// `func(*range(opt))` instead of `func(unpack(opt))`.


template <meta::not_void... Ts> requires (sizeof...(Ts) > 1 && meta::unique<Ts...>)
struct Union : impl::union_tag {
    using types = meta::pack<Ts...>;

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

    /* Explicitly construct a union with the alternative at index `I` using the
    provided arguments.  This is more explicit than using the standard constructors,
    for cases where only a specific alternative should be considered. */
    template <size_t I, typename... A> requires (I < sizeof...(Ts))
    [[nodiscard]] explicit constexpr Union(std::in_place_index_t<I> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, Ts...>, A...>)
        requires (meta::constructible_from<meta::unpack_type<I, Ts...>, A...>)
    :
        _value{tag, std::forward<A>(args)...}
    {}

    /* Explicitly construct a union with the specified alternative using the given
    arguments.  This is more explicit than using the standard constructors, for cases
    where only a specific alternative should be considered. */
    template <typename T, typename... A> requires (types::template contains<T>())
    [[nodiscard]] explicit constexpr Union(std::in_place_type_t<T> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<T, A...>)
        requires (meta::constructible_from<T, A...>)
    :
        _value{std::in_place_index<meta::index_of<T, Ts...>>, std::forward<A>(args)...}
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

    /* Flatten the union into a common type, assuming one exists.  Fails to compile if
    no common type can be found.  Note that the contents will be perfectly forwarded
    according to their storage qualifiers as well as those of the union itself. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
        noexcept (requires{{std::forward<Self>(self)._value.flatten()} noexcept;})
        requires (requires{{std::forward<Self>(self)._value.flatten()};})
    {
        return (std::forward<Self>(self)._value.flatten());
    }

    /* Indirectly access an attribute of the flattened union type, assuming one exists.
    Fails to compile if no common type can be found.  Note that the contents will be
    perfectly forwarded according to their storage qualifiers as well as those of the
    union itself. */
    template <typename Self>
    [[nodiscard]] constexpr auto operator->(this Self&& self)
        noexcept (requires{{impl::arrow_proxy(*std::forward<Self>(self))} noexcept;})
        requires (requires{{impl::arrow_proxy(*std::forward<Self>(self))};})
    {
        return impl::arrow_proxy(*std::forward<Self>(self));
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


/* ADL swap() operator for `bertrand::Union<Ts...>`.  Equivalent to calling `a.swap(b)`
as a member method. */
template <typename... Ts>
constexpr void swap(Union<Ts...>& a, Union<Ts...>& b)
    noexcept (requires{{a.swap(b)} noexcept;})
    requires (requires{{a.swap(b)};})
{
    a.swap(b);
}


namespace impl {

    /* Return an informative error message if an expected is dereferenced while in an
    error state and `DEBUG` is true. */
    template <typename curr>
    struct _bad_optional_access {
        template <size_t J>
        struct visit {
            static constexpr BadUnionAccess operator()() {
                static constexpr static_str msg =
                    "'" + demangle<curr>() + "' is not the active type in the optional "
                    "(active is 'NoneType')";
                return BadUnionAccess(msg);
            }
        };
    };
    template <typename curr> requires (DEBUG)
    constexpr vtable<_bad_optional_access<curr>::template visit> bad_optional_access;

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
            return {std::in_place_index<1>, std::forward<alt>(v)};
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

    /* A simple visitor that backs the explicit conversion operator from `Optional<T>`,
    which attempts a normal visitor conversion where possible, falling back to a
    conversion from `std::nullopt` or `nullptr` to cover all STL types and raw pointers
    in the case of optional lvalues. */
    template <typename Self, typename to>
    struct optional_cast_to {
        using value_type = impl::visitable<Self>::value_type;
        using empty_type = impl::visitable<Self>::empty_type;
        template <typename from>
        static constexpr to operator()(from&& value)
            noexcept (meta::nothrow::explicitly_convertible_to<from, to>)
            requires (meta::explicitly_convertible_to<from, to>)
        {
            return std::forward<from>(value);
        }
        template <meta::is<empty_type> from>
        static constexpr to operator()(from&&)
            noexcept (meta::nothrow::explicitly_convertible_to<const std::nullopt_t&, to>)
            requires (
                !meta::explicitly_convertible_to<from, to> &&
                meta::explicitly_convertible_to<const std::nullopt_t&, to>
            )
        {
            return std::nullopt;
        }
        template <meta::is<empty_type> from> requires (meta::lvalue<value_type>)
        static constexpr to operator()(from&&)
            noexcept (meta::nothrow::explicitly_convertible_to<std::nullptr_t, to>)
            requires (
                !meta::explicitly_convertible_to<from, to> &&
                !meta::explicitly_convertible_to<const std::nullopt_t&, to> &&
                meta::explicitly_convertible_to<std::nullptr_t, to>
            )
        {
            return nullptr;
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
        using type = decltype(std::declval<T>()._value.template get<1>());
        using begin_type = single_iterator<type>;
        using end_type = begin_type;
        using rbegin_type = std::reverse_iterator<begin_type>;
        using rend_type = rbegin_type;

        static constexpr auto begin(T opt)
            noexcept (requires{
                {begin_type(std::addressof(opt._value.template get<1>()) + !opt.has_value())} noexcept;
            })
            requires (requires{
                {begin_type(std::addressof(opt._value.template get<1>()) + !opt.has_value())};
            })
        {
            return begin_type{std::addressof(opt._value.template get<1>()) + !opt.has_value()};
        }

        static constexpr auto end(T opt)
            noexcept (requires{
                {end_type(std::addressof(opt._value.template get<1>()) + 1)} noexcept;
            })
            requires (requires{
                {end_type(std::addressof(opt._value.template get<1>()) + 1)};
            })
        {
            return end_type{std::addressof(opt._value.template get<1>()) + 1};
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
            meta::iterable<decltype(std::declval<T>()._value.template get<1>())> ||
            meta::reverse_iterable<decltype(std::declval<T>()._value.template get<1>())>
        )
    struct make_optional_iterator<T> {
        static constexpr bool trivial = false;
        using type = decltype(std::declval<T>()._value.template get<1>());
        using begin_type = make_optional_begin<type>::type;
        using end_type = make_optional_end<type>::type;
        using rbegin_type = make_optional_rbegin<type>::type;
        using rend_type = make_optional_rend<type>::type;

        static constexpr auto begin(T opt)
            noexcept (requires{
                {begin_type{std::ranges::begin(opt._value.template get<1>())}} noexcept;
                {begin_type{}} noexcept;
            })
            requires (requires{
                {begin_type{std::ranges::begin(opt._value.template get<1>())}};
                {begin_type{}};
            })
        {
            if (opt.has_value()) {
                return begin_type{std::ranges::begin(opt._value.template get<1>())};
            } else {
                return begin_type{};
            }
        }

        static constexpr auto end(T opt)
            noexcept (requires{
                {end_type{std::ranges::end(opt._value.template get<1>())}} noexcept;
                {end_type{}} noexcept;
            })
            requires (requires{
                {end_type{std::ranges::end(opt._value.template get<1>())}};
                {end_type{}};
            })
        {
            if (opt.has_value()) {
                return end_type{std::ranges::end(opt._value.template get<1>())};
            } else {
                return end_type{};
            }
        }

        static constexpr auto rbegin(T opt)
            noexcept (requires{
                {rbegin_type{std::ranges::rbegin(opt._value.template get<1>())}} noexcept;
                {rbegin_type{}} noexcept;
            })
            requires (requires{
                {rbegin_type{std::ranges::rbegin(opt._value.template get<1>())}};
                {rbegin_type{}};
            })
        {
            if (opt.has_value()) {
                return rbegin_type{std::ranges::rbegin(opt._value.template get<1>())};
            } else {
                return rbegin_type{};
            }
        }

        static constexpr auto rend(T opt)
            noexcept (requires{
                {rend_type{std::ranges::rend(opt._value.template get<1>())}} noexcept;
                {rend_type{}} noexcept;
            })
            requires (requires{
                {rend_type{std::ranges::rend(opt._value.template get<1>())}};
                {rend_type{}};
            })
        {
            if (opt.has_value()) {
                return rend_type{std::ranges::rend(opt._value.template get<1>())};
            } else {
                return rend_type{};
            }
        }
    };

}


template <meta::not_void T> requires (!meta::None<T>)
struct Optional : impl::optional_tag {
    using types = meta::pack<T>;

    /// TODO: delete value_type and empty_type to provide a consistent interface.
    /// impl::visitable<T> will provide more detailed access.  That might be true for
    /// all aliases, actually.
    using value_type = T;
    using empty_type = NoneType;

    /// TODO: it may be best to make _value legitimately private, and then only provide
    /// generic access via impl::visitable<T>.  The only problem here is that optionals
    /// would not be valid as explicit template parameters.  I can probably solve that
    /// by keeping the value public, but renaming it to __value with double underscores
    /// instead.

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
        requires (meta::exhaustive<impl::optional_convert_to<Self, to>, Self>)
    {
        return impl::visit(
            impl::optional_convert_to<Self, to>{},
            std::forward<Self>(self)
        );
    }

    /* Explicit conversion from `Optional<T>` to any type that is explicitly
    convertible from both the perfectly-forwarded value type and any of `None`,
    `std::nullopt`, or `nullptr` (if `T` is an lvalue reference).  This operator only
    applies if an implicit conversion could not be found. */
    template <typename Self, typename to>
    [[nodiscard]] explicit constexpr operator to(this Self&& self)
        noexcept (meta::nothrow::exhaustive<impl::optional_cast_to<Self, to>>)
        requires (
            !meta::exhaustive<impl::optional_convert_to<Self, to>, Self> &&
            meta::exhaustive<impl::optional_cast_to<Self, to>, Self>
        )
    {
        return impl::visit(
            impl::optional_cast_to<Self, to>{},
            std::forward<Self>(self)
        );
    }

    /* Contextually convert the optional to a boolean, where true indicates the
    presence of a value. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return _value.index();
    }
    [[nodiscard, deprecated(
        "contextual bool conversions are potentially ambiguous when used on optional "
        "booleans.  Consider an explicit comparison against `None`, a dereference "
        "with a leading `*`, or an exhaustive visitor using trailing `->*` instead. "
    )]] explicit constexpr operator bool() const noexcept
        requires (DEBUG && meta::boolean<value_type>)
    {
        return _value.index();
    }

    /* Dereference to obtain the stored value, perfectly forwarding it according to the
    optional's current cvref qualifications.  A `BadUnionAccess` error will be thrown
    if the program is compiled in debug mode and the optional is empty.  This check
    requires a single extra conditional, which will be optimized out in release builds
    to maintain zero overhead. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept (!DEBUG) {
        if constexpr (DEBUG) {
            if (self._value.index() == 0) {
                throw impl::bad_optional_access<T>();
            }
        }
        return (std::forward<Self>(self)._value.template get<1>());
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  A `BadUnionAccess` error will be
    thrown if the program is compiled in debug mode and the optional is empty.  This
    check requires a single extra conditional, which will be optimized out in release
    builds to maintain zero overhead. */
    [[nodiscard]] constexpr auto operator->()
        noexcept (!DEBUG && (
            meta::nothrow::has_arrow<meta::as_lvalue<value_type>> || (
                !meta::has_arrow<meta::as_lvalue<value_type>> &&
                meta::nothrow::has_address<meta::as_lvalue<value_type>>
            )
        ))
        requires (
            meta::has_arrow<meta::as_lvalue<value_type>> ||
            meta::has_address<meta::as_lvalue<value_type>>
        )
    {
        if constexpr (DEBUG) {
            if (_value.index() == 0) {
                throw impl::bad_optional_access<T>();
            }
        }
        if constexpr (meta::has_arrow<meta::as_lvalue<value_type>>) {
            return std::to_address(_value.template get<1>());
        } else {
            return std::addressof(_value.template get<1>());
        }
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  A `BadUnionAccess` error will be
    thrown if the program is compiled in debug mode and the optional is empty.  This
    check requires a single extra conditional, which will be optimized out in release
    builds to maintain zero overhead. */
    [[nodiscard]] constexpr auto operator->() const
        noexcept (!DEBUG && (
            meta::nothrow::has_arrow<meta::as_const_ref<value_type>> || (
                !meta::has_arrow<meta::as_const_ref<value_type>> &&
                meta::nothrow::has_address<meta::as_const_ref<value_type>>
            )
        ))
        requires (
            meta::has_arrow<meta::as_const_ref<value_type>> ||
            meta::has_address<meta::as_const_ref<value_type>>
        )
    {
        if constexpr (DEBUG) {
            if (_value.index() == 0) {
                throw impl::bad_optional_access<T>();
            }
        }
        if constexpr (meta::has_arrow<meta::as_const_ref<value_type>>) {
            return std::to_address(_value.template get<1>());
        } else {
            return std::addressof(_value.template get<1>());
        }
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `None` or `std::nullopt`. */
    [[nodiscard]] friend constexpr bool operator==(const Optional& opt, NoneType) noexcept {
        return opt._value.index() == 0;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `None` or `std::nullopt`. */
    [[nodiscard]] friend constexpr bool operator==(NoneType, const Optional& opt) noexcept {
        return opt._value.index() == 0;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `nullptr`, assuming `T` is an lvalue reference. */
    [[nodiscard]] friend constexpr bool operator==(const Optional& opt, std::nullptr_t) noexcept
        requires (meta::lvalue<value_type>)
    {
        return opt._value.index() == 0;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `nullptr`, assuming `T` is an lvalue reference. */
    [[nodiscard]] friend constexpr bool operator==(std::nullptr_t, const Optional& opt) noexcept
        requires (meta::lvalue<value_type>)
    {
        return opt._value.index() == 0;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `None` or `std::nullopt`. */
    [[nodiscard]] friend constexpr bool operator!=(const Optional& opt, NoneType) noexcept {
        return opt._value.index() != 0;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `None` or `std::nullopt`. */
    [[nodiscard]] friend constexpr bool operator!=(NoneType, const Optional& opt) noexcept {
        return opt._value.index() != 0;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `nullptr`, assuming `T` is an lvalue reference. */
    [[nodiscard]] friend constexpr bool operator!=(const Optional& opt, std::nullptr_t) noexcept
        requires (meta::lvalue<value_type>)
    {
        return opt._value.index() != 0;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `nullptr`, assuming `T` is an lvalue reference. */
    [[nodiscard]] friend constexpr bool operator!=(std::nullptr_t, const Optional& opt) noexcept
        requires (meta::lvalue<value_type>)
    {
        return opt._value.index() != 0;
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
            if (_value.index()) {
                return std::ranges::size(_value.template get<1>());
            } else {
                return meta::size_type<T>(0);
            }
        } else {
            return size_t(_value.index());
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
            if (_value.index()) {
                return std::ranges::ssize(_value.template get<1>());
            } else {
                return meta::ssize_type<T>(0);
            }
        } else {
            return ssize_t(_value.index());
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
            return _value.index() ? std::ranges::empty(_value.template get<1>()) : true;
        } else {
            return !_value.index();
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


/* ADL swap() operator for `bertrand::Optional<T>`.  Equivalent to calling `a.swap(b)`
as a member method. */
template <typename T>
constexpr void swap(Optional<T>& a, Optional<T>& b)
    noexcept (requires{{a.swap(b)} noexcept;})
    requires (requires{{a.swap(b)};})
{
    a.swap(b);
}


namespace impl {

    /* Return an informative error message if an expected is dereferenced while in an
    error state and `DEBUG` is true. */
    template <typename curr, typename... Ts>
    struct _bad_expected_access {
        template <size_t J>
        struct visit {
            static constexpr BadUnionAccess operator()() {
                static constexpr static_str msg =
                    "'" + demangle<curr>() + "' is not the active type in the expected "
                    "(active is '" + demangle<meta::unpack_type<J, Ts...>>() + "')";
                return BadUnionAccess(msg);
            }
        };
    };
    template <typename curr, typename... Ts> requires (DEBUG)
    constexpr vtable<_bad_expected_access<curr, Ts...>::template visit> bad_expected_access;

    /* Given an initializer that inherits from at least one expected exception type,
    determine the most proximal exception to initialize. */
    template <typename out, typename, typename...>
    struct _expected_convert_from { using type = out; };
    template <typename out, typename from, typename curr, typename... next>
        requires (meta::inherits<from, curr> && (meta::is_void<out> || meta::inherits<curr, out>))
    struct _expected_convert_from<out, from, curr, next...> :
        _expected_convert_from<curr, from, next...>  // replace `out`
    {};
    template <typename out, typename from, typename curr, typename... next>
    struct _expected_convert_from<out, from, curr, next...> :
        _expected_convert_from<out, from, next...>  // keep `out`
    {};

    /* A simple visitor that backs the implicit constructor for an `Expected<T, Es...>`
    object, returning a corresponding `impl::union_storage` primitive type. */
    template <typename, typename...>
    struct expected_convert_from {};
    template <typename T, typename... Es, typename in> requires (!meta::monad<in>)
    struct expected_convert_from<meta::pack<T, Es...>, in> {
        template <typename from>
        using type = _expected_convert_from<void, from, Es...>::type;

        // 1) prefer direct conversion to `out` if possible
        template <typename from>
        static constexpr impl::union_storage<T, Es...> operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, meta::remove_rvalue<T>>)
            requires (meta::convertible_to<from, meta::remove_rvalue<T>>)
        {
            return {std::in_place_index<0>, std::forward<from>(arg)};
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
                std::in_place_index<meta::index_of<type<from>, Es...>>,
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
            return {std::in_place_index<0>, std::forward<A>(args)...};
        }
    };




    /// TODO: ensure that impl::visitable<Self> exposes the right fields.  When I
    /// refactor the visit internals, I can come back to this and ensure it all works,
    /// and possibly expand it to make the other trivial visitors generic in the
    /// same way.  That way they don't depend on the underlying union type or how it is
    /// implemented, using impl::visitable<Self> to handle the details.



    /* A helper type to extract the proper error type from an `Expected<T, Es...>`. */
    template <typename... Es>
    struct expected_error { using type = bertrand::Union<Es...>; };
    template <typename E>
    struct expected_error<E> { using type = E; };

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

}


template <typename T, meta::unqualified_exception E, meta::unqualified_exception... Es>
    requires (meta::unique<T, E, Es...>)
struct Expected : impl::expected_tag {
    using types = meta::pack<T, E, Es...>;
    using errors = meta::pack<E, Es...>;
    using value_type = std::conditional_t<meta::is_void<T>, NoneType, T>;
    using error_type = impl::expected_error<E, Es...>::type;

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

    /* Contextually convert the expected to a boolean, where true indicates the
    presence of a value. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return _value.index() == 0;
    }
    [[nodiscard, deprecated(
        "contextual bool conversions are potentially ambiguous when used on expected "
        "booleans.  Consider dereferencing with a leading `*`, or an exhaustive "
        "visitor using trailing `->*` instead. "
    )]] explicit constexpr operator bool() const noexcept
        requires (DEBUG && meta::boolean<value_type>)
    {
        return _value.index() == 0;
    }

    /* Dereference to obtain the stored value, perfectly forwarding it according to the
    expected's current cvref qualifications.  A `BadUnionAccess` error will be thrown
    if the program is compiled in debug mode and the expected is in an error state.
    This check requires a single extra conditional, which will be optimized out in
    release builds to maintain zero overhead. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept (!DEBUG) {
        if constexpr (DEBUG) {
            if (self._value.index() != 0) {
                throw impl::bad_expected_access<T, E, Es...>();
            }
        }
        return (std::forward<Self>(self)._value.template get<0>());
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  A `BadUnionAccess` error will be
    thrown if the program is compiled in debug mode and the optional is in an error
    state.  This check requires a single extra conditional, which will be optimized out
    in release builds to maintain zero overhead. */
    [[nodiscard]] constexpr auto operator->()
        noexcept (!DEBUG && (
            meta::nothrow::has_arrow<meta::as_lvalue<value_type>> || (
                !meta::has_arrow<meta::as_lvalue<value_type>> &&
                meta::nothrow::has_address<meta::as_lvalue<value_type>>
            )
        ))
        requires (
            meta::has_arrow<meta::as_lvalue<value_type>> ||
            meta::has_address<meta::as_lvalue<value_type>>
        )
    {
        if constexpr (DEBUG) {
            if (_value.index() != 0) {
                throw impl::bad_expected_access<T, E, Es...>();
            }
        }
        if constexpr (meta::has_arrow<meta::as_lvalue<value_type>>) {
            return std::to_address(_value.template get<0>());
        } else {
            return std::addressof(_value.template get<0>());
        }
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  A `BadUnionAccess` error will be
    thrown if the program is compiled in debug mode and the empty is in an error state.
    This check requires a single extra conditional, which will be optimized out in
    release builds to maintain zero overhead. */
    [[nodiscard]] constexpr auto operator->() const
        noexcept (!DEBUG && (
            meta::nothrow::has_arrow<meta::as_const_ref<value_type>> || (
                !meta::has_arrow<meta::as_const_ref<value_type>> &&
                meta::nothrow::has_address<meta::as_const_ref<value_type>>
            )
        ))
        requires (
            meta::has_arrow<meta::as_const_ref<value_type>> ||
            meta::has_address<meta::as_const_ref<value_type>>
        )
    {
        if constexpr (DEBUG) {
            if (_value.index() != 0) {
                throw impl::bad_expected_access<T, E, Es...>();
            }
        }
        if constexpr (meta::has_arrow<meta::as_const_ref<value_type>>) {
            return std::to_address(_value.template get<0>());
        } else {
            return std::addressof(_value.template get<0>());
        }
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
            if (_value.index() == 0) {
                return std::ranges::size(_value.template get<0>());
            } else {
                return meta::size_type<T>(0);
            }
        } else {
            return size_t(_value.index() == 0);
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
            if (_value.index() == 0) {
                return std::ranges::ssize(_value.template get<0>());
            } else {
                return meta::ssize_type<T>(0);
            }
        } else {
            return ssize_t(_value.index() == 0);
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
            return _value.index() != 0 || std::ranges::empty(_value.template get<0>());
        } else {
            return _value.index() != 0;
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


/* ADL swap() operator for `bertrand::Expected<T, E>`.  Equivalent to calling
`a.swap(b)` as a member method. */
template <typename T, typename E>
constexpr void swap(Expected<T, E>& a, Expected<T, E>& b)
    noexcept (requires{{a.swap(b)} noexcept;})
    requires (requires{{a.swap(b)};})
{
    a.swap(b);
}


namespace impl {

    /* Tuple iterators can be optimized away if the tuple is empty, or into an array of
    pointers if all elements unpack to the same lvalue type.  Otherwise, they must
    build a vtable and perform a dynamic dispatch to yield a proper value type, which
    may be a union. */
    enum class tuple_array_kind {
        NO_COMMON_TYPE,
        EMPTY,
        CONSISTENT,
        DYNAMIC
    };

    /* Indexing and/or iterating over a tuple requires the creation of some kind of
    array, which can either be a flat array of homogenous references or a vtable of
    function pointers that produce a common type (which may be a `Union`) to which all
    results are convertible. */
    template <typename, typename>
    struct _tuple_array {
        using types = meta::pack<>;
        using reference = const NoneType&;
        static constexpr tuple_array_kind kind = tuple_array_kind::EMPTY;
        static constexpr bool nothrow = true;
    };
    template <typename in, typename T>
    struct _tuple_array<in, meta::pack<T>> {
        using types = meta::pack<T>;
        using reference = T;
        static constexpr tuple_array_kind kind = meta::lvalue<T> && meta::has_address<T> ?
            tuple_array_kind::CONSISTENT : tuple_array_kind::DYNAMIC;
        static constexpr bool nothrow = true;
    };
    template <typename in, typename... Ts> requires (sizeof...(Ts) > 1)
    struct _tuple_array<in, meta::pack<Ts...>> {
        using types = meta::pack<Ts...>;
        using reference = bertrand::Union<Ts...>;
        static constexpr tuple_array_kind kind = (meta::convertible_to<Ts, reference> && ...) ?
            tuple_array_kind::DYNAMIC : tuple_array_kind::NO_COMMON_TYPE;
        static constexpr bool nothrow = (meta::nothrow::convertible_to<Ts, reference> && ...);
    };
    template <meta::tuple_like T>
    struct tuple_array :
        _tuple_array<T, typename meta::tuple_types<T>::template eval<meta::to_unique>>
    {
    private:
        using base = _tuple_array<
            T,
            typename meta::tuple_types<T>::template eval<meta::to_unique>
        >;

    public:
        using ptr = base::reference(*)(T) noexcept (base::nothrow);

        template <size_t I>
        static constexpr base::reference fn(T t) noexcept (base::nothrow) {
            return meta::tuple_get<I>(t);
        }

        template <typename = std::make_index_sequence<meta::tuple_size<T>>>
        static constexpr ptr tbl[0] {};
        template <size_t... Is>
        static constexpr ptr tbl<std::index_sequence<Is...>>[sizeof...(Is)] { &fn<Is>... };
    };

    template <typename>
    struct tuple_iterator {};

    template <typename T>
    concept enable_tuple_iterator =
        meta::lvalue<T> &&
        meta::tuple_like<T> &&
        tuple_array<T>::kind != tuple_array_kind::NO_COMMON_TYPE;

    /// TODO: figure out how to properly handle arrows for tuple iterators, as well as
    /// possibly optional iterators.

    /* An iterator over an otherwise non-iterable tuple type, which constructs a vtable
    of callback functions yielding each value.  This allows tuples to be used as inputs
    to iterable algorithms, as long as those algorithms are built to handle possible
    `Union` values. */
    template <enable_tuple_iterator T>
        requires (tuple_array<T>::kind == tuple_array_kind::DYNAMIC)
    struct tuple_iterator<T> {
        using types = tuple_array<T>::types;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_array<T>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

    private:
        using table = tuple_array<T>;
        using indices = std::make_index_sequence<types::size()>;
        using storage = meta::as_pointer<T>;

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

        [[nodiscard]] constexpr reference operator*() const noexcept (table::nothrow) {
            return table::template tbl<>[index](*data);
        }

        /// TODO: I can expose a perfectly normal operator->() if I wrap a reference

        [[nodiscard]] constexpr reference operator[](
            difference_type n
        ) const noexcept (table::nothrow) {
            return table::template tbl<>[index + n](*data);
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
        requires (tuple_array<T>::kind == tuple_array_kind::CONSISTENT)
    struct tuple_iterator<T> {
        using types = tuple_array<T>::types;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_array<T>::reference;
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

        [[nodiscard]] constexpr tuple_iterator(const array& arr, difference_type index) noexcept :
            arr(arr),
            index(index)
        {}

    public:
        array arr;
        difference_type index;

        [[nodiscard]] constexpr tuple_iterator(difference_type index = 0) noexcept :
            arr{},
            index(index)
        {}

        [[nodiscard]] constexpr tuple_iterator(T t, difference_type index)
            noexcept (requires{{init(indices{}, t)} noexcept;})
        :
            arr(init(indices{}, t)),
            index(index)
        {}

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return *arr[index];
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept {
            return arr[index];
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
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
        requires (tuple_array<T>::kind == tuple_array_kind::EMPTY)
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

    template <typename...>
    struct _tuple_storage : tuple_storage_tag {
        using types = meta::pack<>;
        constexpr void swap(_tuple_storage&) noexcept {}
        template <size_t I, typename Self> requires (false)  // never actually called
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept;
    };

    template <typename T, typename... Ts>
    struct _tuple_storage<T, Ts...> : _tuple_storage<Ts...> {
    private:
        using type = meta::remove_rvalue<T>;

    public:
        [[no_unique_address]] type data;

        [[nodiscard]] constexpr _tuple_storage() = default;
        [[nodiscard]] constexpr _tuple_storage(T val, Ts... rest)
            noexcept (
                meta::nothrow::convertible_to<T, type> &&
                meta::nothrow::constructible_from<_tuple_storage<Ts...>, Ts...>
            )
            requires (
                meta::convertible_to<T, type> &&
                meta::constructible_from<_tuple_storage<Ts...>, Ts...>
            )
        :
            _tuple_storage<Ts...>(std::forward<Ts>(rest)...),
            data(std::forward<T>(val))
        {}

        constexpr void swap(_tuple_storage& other)
            noexcept (
                meta::nothrow::swappable<meta::remove_rvalue<T>> &&
                meta::nothrow::swappable<_tuple_storage<Ts...>>
            )
            requires (
                meta::swappable<meta::remove_rvalue<T>> &&
                meta::swappable<_tuple_storage<Ts...>>
            )
        {
            _tuple_storage<Ts...>::swap(other);
            std::ranges::swap(data, other.data);
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data(std::forward<A>(args)...)} noexcept;
            })
            requires (
                requires{{std::forward<Self>(self).data(std::forward<A>(args)...)};} &&
                !requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            return (std::forward<Self>(self).data(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).data(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }

        template <size_t I, typename Self> requires (I < sizeof...(Ts) + 1)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).data);
            } else {
                using base = meta::qualify<_tuple_storage<Ts...>, Self>;
                return (std::forward<base>(self).template get<I - 1>());
            }
        }
    };

    template <meta::lvalue T, typename... Ts>
    struct _tuple_storage<T, Ts...> : _tuple_storage<Ts...> {
        [[no_unique_address]] struct { T ref; } data;

        /// NOTE: no default constructor for lvalue references
        [[nodiscard]] constexpr _tuple_storage(T ref, Ts... rest)
            noexcept (meta::nothrow::constructible_from<_tuple_storage<Ts...>, Ts...>)
            requires (meta::constructible_from<_tuple_storage<Ts...>, Ts...>)
        :
            _tuple_storage<Ts...>(std::forward<Ts>(rest)...),
            data{ref}
        {}

        constexpr _tuple_storage(const _tuple_storage&) = default;
        constexpr _tuple_storage(_tuple_storage&&) = default;
        constexpr _tuple_storage& operator=(const _tuple_storage& other) {
            _tuple_storage<Ts...>::operator=(other);
            std::construct_at(&data, other.data.ref);
            return *this;
        };
        constexpr _tuple_storage& operator=(_tuple_storage&& other) {
            _tuple_storage<Ts...>::operator=(std::move(other));
            std::construct_at(&data, other.data.ref);
            return *this;
        };

        constexpr void swap(_tuple_storage& other)
            noexcept (meta::nothrow::swappable<_tuple_storage<Ts...>>)
            requires (meta::swappable<_tuple_storage<Ts...>>)
        {
            _tuple_storage<Ts...>::swap(other);
            auto tmp = data;
            std::construct_at(&data, other.data.ref);
            std::construct_at(&other.data, tmp.ref);
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data.ref(std::forward<A>(args)...)} noexcept;
            })
            requires (
                requires{{std::forward<Self>(self).data.ref(std::forward<A>(args)...)};} &&
                !requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            return (std::forward<Self>(self).data.ref(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).data.ref(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }

        template <size_t I, typename Self> requires (I < sizeof...(Ts) + 1)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).data.ref);
            } else {
                using base = meta::qualify<_tuple_storage<Ts...>, Self>;
                return (std::forward<base>(self).template get<I - 1>());
            }
        }
    };

    /* A basic implementation of a tuple using recursive inheritance, meant to be used
    in conjunction with `union_storage` as the basis for further algebraic types.
    Tuples of this form can be destructured just like `std::tuple`, invoked as if they
    were overload sets, and iterated over/indexed like an array, possibly yielding
    `Union`s if the tuple types are heterogeneous. */
    template <meta::not_void... Ts>
    struct tuple_storage : _tuple_storage<Ts...> {
        using types = meta::pack<Ts...>;
        using size_type = size_t;
        using index_type = ssize_t;
        using iterator = tuple_iterator<tuple_storage&>;
        using const_iterator = tuple_iterator<const tuple_storage&>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        using _tuple_storage<Ts...>::_tuple_storage;

        /* Return the total number of elements within the tuple, as an unsigned
        integer. */
        [[nodiscard]] static constexpr size_type size() noexcept {
            return sizeof...(Ts);
        }

        /* Return the total number of elements within the tuple, as a signed
        integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept {
            return index_type(size());
        }

        /* Return true if the tuple holds no elements. */
        [[nodiscard]] static constexpr bool empty() noexcept {
            return size() == 0;
        }

        /* Perfectly forward the value at a specific index, where that index is known
        at compile time. */
        template <size_type I, typename Self> requires (I < size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self).template get<I>());
        }

        /* Swap the contents of two tuples with the same type specification. */
        constexpr void swap(tuple_storage& other)
            noexcept (meta::nothrow::swappable<_tuple_storage<Ts...>>)
            requires (meta::swappable<_tuple_storage<Ts...>>)
        {
            if (this != &other) {
                _tuple_storage<Ts...>::swap(other);
            }
        }

        /* Index into the tuple, perfectly forwarding the result according to the
        tuple's current cvref qualifications. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self)
            noexcept (tuple_array<meta::forward<Self>>::nothrow)
            requires (
                tuple_array<meta::forward<Self>>::kind != tuple_array_kind::NO_COMMON_TYPE &&
                tuple_array<meta::forward<Self>>::kind != tuple_array_kind::EMPTY
            )
        {
            return (tuple_array<meta::forward<Self>>::template tbl<>[
                self._value.index()
            ](std::forward<Self>(self)));
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr iterator at(size_type index)
            noexcept (requires{{iterator{*this, index}} noexcept;})
            requires (requires{{iterator{*this, index}};})
        {
            return {*this, index_type(index)};
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr const_iterator at(size_type index) const
            noexcept (requires{{const_iterator{*this, index}} noexcept;})
            requires (requires{{const_iterator{*this, index}};})
        {
            return {*this, index_type(index)};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr iterator begin()
            noexcept (requires{{iterator{*this, 0}} noexcept;})
            requires (requires{{iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator begin() const
            noexcept (requires{{const_iterator{*this, 0}} noexcept;})
            requires (requires{{const_iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator cbegin() const
            noexcept (requires{{const_iterator{*this, 0}} noexcept;})
            requires (requires{{const_iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr iterator end()
            noexcept (requires{{iterator{ssize()}} noexcept;})
            requires (requires{{iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator end() const
            noexcept (requires{{const_iterator{ssize()}} noexcept;})
            requires (requires{{const_iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator cend() const
            noexcept (requires{{const_iterator{ssize()}} noexcept;})
            requires (requires{{const_iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rbegin()
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rbegin() const
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crbegin() const
            noexcept (requires{{std::make_reverse_iterator(cend())} noexcept;})
            requires (requires{{std::make_reverse_iterator(cend())};})
        {
            return std::make_reverse_iterator(cend());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rend()
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rend() const
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crend() const
            noexcept (requires{{std::make_reverse_iterator(cbegin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(cbegin())};})
        {
            return std::make_reverse_iterator(cbegin());
        }
    };

    /* A special case of `tuple_storage<Ts...>` where all `Ts...` are identical,
    allowing the storage layout to optimize to a flat array instead of requiring
    recursive base classes, speeding up both compilation and indexing/iteration. */
    template <meta::not_void T, meta::not_void... Ts> requires (std::same_as<T, Ts> && ...)
    struct tuple_storage<T, Ts...> : tuple_storage_tag {
        using types = meta::pack<T, Ts...>;
        using size_type = size_t;
        using index_type = ssize_t;

        /* Return the total number of elements within the tuple, as an unsigned integer. */
        [[nodiscard]] static constexpr size_type size() noexcept {
            return sizeof...(Ts) + 1;
        }

        /* Return the total number of elements within the tuple, as a signed integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept {
            return index_type(size());
        }

        /* Return true if the tuple holds no elements. */
        [[nodiscard]] static constexpr bool empty() noexcept {
            return size() == 0;
        }

    private:
        struct store { meta::remove_rvalue<T> value; };
        using array = std::array<store, size()>;

    public:
        struct iterator {
            using iterator_category = std::contiguous_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = meta::remove_reference<T>;
            using reference = meta::as_lvalue<value_type>;
            using pointer = meta::as_pointer<value_type>;

            store* ptr;

            [[nodiscard]] constexpr reference operator*() const noexcept {
                return ptr->value;
            }

            [[nodiscard]] constexpr pointer operator->() const
                noexcept (meta::nothrow::address_returns<pointer, reference>)
                requires (meta::address_returns<pointer, reference>)
            {
                return std::addressof(ptr->value);
            }

            [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
                return ptr[n].value;
            }

            constexpr iterator& operator++() noexcept {
                ++ptr;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator++(int) noexcept {
                iterator tmp = *this;
                ++ptr;
                return tmp;
            }

            [[nodiscard]] friend constexpr iterator operator+(
                const iterator& self,
                difference_type n
            ) noexcept {
                return {self.ptr + n};
            }

            [[nodiscard]] friend constexpr iterator operator+(
                difference_type n,
                const iterator& self
            ) noexcept {
                return {self.ptr + n};
            }

            constexpr iterator& operator+=(difference_type n) noexcept {
                ptr += n;
                return *this;
            }

            constexpr iterator& operator--() noexcept {
                --ptr;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator--(int) noexcept {
                iterator tmp = *this;
                --ptr;
                return tmp;
            }

            [[nodiscard]] constexpr iterator operator-(difference_type n) const noexcept {
                return {ptr - n};
            }

            [[nodiscard]] constexpr difference_type operator-(const iterator& other) const noexcept {
                return ptr - other.ptr;
            }

            [[nodiscard]] constexpr bool operator==(const iterator& other) const noexcept {
                return ptr == other.ptr;
            }

            [[nodiscard]] constexpr auto operator<=>(const iterator& other) const noexcept {
                return ptr <=> other.ptr;
            }
        };

        struct const_iterator {
            using iterator_category = std::contiguous_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = meta::remove_reference<meta::as_const<T>>;
            using reference = meta::as_lvalue<value_type>;
            using pointer = meta::as_pointer<value_type>;

            const store* ptr;

            [[nodiscard]] constexpr reference operator*() const noexcept {
                return ptr->value;
            }

            [[nodiscard]] constexpr pointer operator->() const
                noexcept (meta::nothrow::address_returns<pointer, reference>)
                requires (meta::address_returns<pointer, reference>)
            {
                return std::addressof(ptr->value);
            }

            [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
                return ptr[n].value;
            }

            constexpr const_iterator& operator++() noexcept {
                ++ptr;
                return *this;
            }

            [[nodiscard]] constexpr const_iterator operator++(int) noexcept {
                const_iterator tmp = *this;
                ++ptr;
                return tmp;
            }

            [[nodiscard]] friend constexpr const_iterator operator+(
                const const_iterator& self,
                difference_type n
            ) noexcept {
                return {self.ptr + n};
            }

            [[nodiscard]] friend constexpr const_iterator operator+(
                difference_type n,
                const const_iterator& self
            ) noexcept {
                return {self.ptr + n};
            }

            constexpr const_iterator& operator+=(difference_type n) noexcept {
                ptr += n;
                return *this;
            }

            constexpr const_iterator& operator--() noexcept {
                --ptr;
                return *this;
            }

            [[nodiscard]] constexpr const_iterator operator--(int) noexcept {
                const_iterator tmp = *this;
                --ptr;
                return tmp;
            }

            [[nodiscard]] constexpr const_iterator operator-(difference_type n) const noexcept {
                return {ptr - n};
            }

            [[nodiscard]] constexpr difference_type operator-(
                const const_iterator& other
            ) const noexcept {
                return ptr - other.ptr;
            }

            [[nodiscard]] constexpr bool operator==(const const_iterator& other) const noexcept {
                return ptr == other.ptr;
            }

            [[nodiscard]] constexpr auto operator<=>(const const_iterator& other) const noexcept {
                return ptr <=> other.ptr;
            }
        };

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        array data;

        [[nodiscard]] constexpr tuple_storage()
            noexcept (meta::nothrow::default_constructible<meta::remove_rvalue<T>>)
            requires (!meta::lvalue<T> && meta::default_constructible<meta::remove_rvalue<T>>)
        :
            data{}
        {}

        [[nodiscard]] constexpr tuple_storage(T val, Ts... rest)
            noexcept (requires{
                {array{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}} noexcept;
            })
            requires (requires{
                {array{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}};
            })
        :
            data{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}
        {}

        [[nodiscard]] constexpr tuple_storage(const tuple_storage&) = default;
        [[nodiscard]] constexpr tuple_storage(tuple_storage&&) = default;

        constexpr tuple_storage& operator=(const tuple_storage& other)
            noexcept (meta::lvalue<T> || meta::nothrow::copy_assignable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::copy_assignable<meta::remove_rvalue<T>>)
        {
            if constexpr (meta::lvalue<T>) {
                if (this != &other) {
                    for (size_type i = 0; i < size(); ++i) {
                        std::construct_at(&data[i].value, other.data[i].value);
                    }
                }
            } else {
                data = other.data;
            }
            return *this;
        }

        constexpr tuple_storage& operator=(tuple_storage&& other)
            noexcept (meta::lvalue<T> || meta::nothrow::move_assignable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::move_assignable<meta::remove_rvalue<T>>)
        {
            if constexpr (meta::lvalue<T>) {
                if (this != &other) {
                    for (size_type i = 0; i < size(); ++i) {
                        std::construct_at(&data[i].value, other.data[i].value);
                    }
                }
            } else {
                data = std::move(other).data;
            }
            return *this;
        }

        /* Swap the contents of two tuples with the same type specification. */
        constexpr void swap(tuple_storage& other)
            noexcept (meta::lvalue<T> || meta::nothrow::swappable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::swappable<meta::remove_rvalue<T>>)
        {
            if (this != &other) {
                for (size_type i = 0; i < size(); ++i) {
                    if constexpr (meta::lvalue<T>) {
                        store tmp = data[i];
                        std::construct_at(&data[i].value, other.data[i].value);
                        std::construct_at(&other.data[i].value, tmp.value);
                    } else {
                        std::ranges::swap(data[i].value, other.data[i].value);
                    }
                }
            }
        }

        /* Invoke the tuple as an overload set, assuming precisely one element is
        invocable with the given arguments. */
        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data[0].value(std::forward<A>(args)...)} noexcept;
            })
            requires (size() == 1 && requires{
                {std::forward<Self>(self).data[0].value(std::forward<A>(args)...)};
            })
        {
            return (std::forward<Self>(self).data[0].value(std::forward<A>(args)...));
        }

        /* Perfectly forward the value at a specific index, where that index is known
        at compile time. */
        template <size_t I, typename Self> requires (I < size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return (std::forward<Self>(self).data[I].value);
        }

        /* Index into the tuple, perfectly forwarding the result according to the
        tuple's current cvref qualifications. */
        template <typename Self>
        constexpr decltype(auto) operator[](this Self&& self, size_type index) noexcept {
            return (std::forward<Self>(self).data[index].value);
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr iterator at(size_type index) noexcept {
            return {data.data() + index};
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr const_iterator at(size_type index) const noexcept {
            return {data.data() + index};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr iterator begin() noexcept {
            return {data.data()};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator begin() const noexcept {
            return {data.data()};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
            return {data.data()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr iterator end() noexcept {
            return {data.data() + size()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator end() const noexcept {
            return {data.data() + size()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator cend() const noexcept {
            return {data.data() + size()};
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
            return std::make_reverse_iterator(cend());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rend() noexcept {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
            return std::make_reverse_iterator(cbegin());
        }
    };

    template <typename... Ts>
    tuple_storage(Ts&&...) -> tuple_storage<meta::remove_rvalue<Ts>...>;

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


}  // namespace bertrand


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
    struct tuple_size<bertrand::impl::tuple_storage<Ts...>> : std::integral_constant<
        typename std::remove_cvref_t<bertrand::impl::tuple_storage<Ts...>>::size_type,
        bertrand::impl::tuple_storage<Ts...>::size()
    > {};

    template <size_t I, typename... Ts>
        requires (I < std::tuple_size<bertrand::impl::tuple_storage<Ts...>>::value)
    struct tuple_element<I, bertrand::impl::tuple_storage<Ts...>> {
        using type = bertrand::impl::tuple_storage<Ts...>::types::template at<I>;
    };

    template <size_t I, bertrand::meta::tuple_storage T>
        requires (I < std::tuple_size<std::remove_cvref_t<T>>::value)
    constexpr decltype(auto) get(T&& t) {
        return (std::forward<T>(t).template get<I>());
    }







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
