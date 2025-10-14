#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"


/* INTRODUCTION

    "A monad is a monoid in the category of endofunctors of some fixed category"
        - Category Theory (https://medium.com/@felix.kuehl/a-monad-is-just-a-monoid-in-the-category-of-endofunctors-lets-actually-unravel-this-f5d4b7dbe5d6)

Unless you already know what each of these words means, this definition is about as
clear as mud, so here's a better one:

    A monad (in the context of computer science) is a separate *domain* of values, in
    which results are *labeled* with the monad type, and compositions of monads return
    other monads.

Intuitively, a monad is a kind of "sticky" wrapper around a value, which behaves
similarly to the value itself, but with extra properties that alter its semantics in
a way that is consistent across the entire domain.  It is a natural extension of the
Gang of Four's Decorator pattern, and is a fundamental abstraction in functional
programming languages, such as Haskell, Scala, and Elixir.

Let's look at one of the most basic examples of a monad: the `Optional` type:

```
    // the default constructor for `int` is undefined, so `x` is technically uninitialized
    int x1;
    int y1 = x1 + 2;  // undefined behavior -> `y1` may contain garbage

    // Optional<> provides a default constructor, initializing to an empty state
    Optional<int> x2;
    Optional<int> y2 = x2 + 2;  // well-defined -> `y2` is also empty.  No computation takes place
```

Formally, `Optional<T>` adds a universal `None` state to the domain of `T`, which can
be used to represent the absence of a value, regardless of the specific characteristics
of `T`.  Operating on an `Optional<T>` is exactly like operating on `T` itself, except
that the empty state will be propagated through the computation, and usually maps to
itself (which turns the operation into an identity function).  Thus, any algorithm that
operates on `T` should also be able to operate on `Optional<T>` without any changes;
it will simply not be computed if the input is in the empty state.  Such operations can
thus be chained together without needing to check for the empty state at every step,
allowing for more concise and readable code, which better models the underlying problem
space.

`Optional`s are just one example of a monad, and are actually just a special case of
`Union`, where the first type is the empty state.  In (mathematical) type theory, these
are categorized as "sum types": composite types that can hold **one** (and only one) of
several possible types, where the overall space of types is given by the disjunction
(logical OR) between every alternative.  Bertrand's `Union<Ts...>` monad generalizes
this concept to any choice of (non-void) `Ts...`, and provides a type-safe, monadic way
to represent values of indeterminate type in statically-typed languages such as C++.
Operators for monadic unions are allowed if and only if all alternatives support the
same operation, and may return further unions of several unique types, reflecting the
results from each alternative.

Sometimes, it is beneficial to explicitly leave the monadic domain and unwrap the raw
value(s), which can be done in a number of ways.  First, all monads support both
implicit and explicit conversion to arbitrary types as long as all alternatives can be
converted to that type in turn.  This generates a "projection" from the monadic domain
to that of the destination type, which is the logical inversion of the monad's
constructor.  Additionally, monads support pattern matching, as is commonly used by
functional languages to destructure algebraic types into their constituent parts.
Here's a simple example:

```
    using A = std::array<double, 3>;
    using B = std::string;
    using C = std::vector<bool>;

    Union<A, B, C> u = "Hello, World!";  // 13 characters
    int n = u ->* def{
        [](const A& a) { return a.size(); },
        [](const B& b) { return b.size(); },
        [](const C& c) { return c.size(); }
    };
    assert(n == 13);
    assert(n == u.size());  // (!)
```

Note that the `assert` in the last line only compiles because all 3 alternatives
support the `size()` operator, meaning that `u.size()` is well-formed and returns a
type comparable to `int`.  The pattern matching expression `u ->* def{...}` is not
constrained in the same way, and will compile as long as all alternatives are
(unambiguously) handled, regardless of each case's return type or internal logic.  This
allows for detailed and type-safe access to the internal structure of a union, possibly
including unique projections or other behavior for each alternative.

Some monads, including `Optional` and `Expected` also support pointer-like dereference
operators, which trivially map from the monadic domain to the underlying type, assuming
the monad is not in an empty or error state.  This means that `Optional<T>` (and
particularly `Optional<T&>`) can be used to model pointers, which is useful when
integrating with languages that do not otherwise expose them to the user, such as
Python.  In fact, optional references are literally reduced to pointers in the
underlying implementation, with the only change being that they forward all operations
to the referenced value, rather than exposing pointer arithmetic or similar operations
(which may be error-prone and potentially insecure) to the user.  `Union<Ts...>` also
support the same operators, but only if all alternatives share a common type, and will
fail to compile otherwise.

There are also other monads not covered here, such as `Tuple<Ts...>` and `range<C>`,
which extend these monadic principles to so-called "product types", where the overall
space of types is given by the conjunction (logical AND) of each alternative.  Monads
can also be useful when modeling operations across time, as is the case for
`async<F, A...>`, which schedules a function `F` to be executed asynchronously on a
separate resource (usually a thread).  Operating on the `async` monad after it has been
scheduled extends it with one or more continuations, which will be executed on the same
resource immediately after the original function completes, allowing users to chain
together asynchronous operations in a more intuitive and type-safe manner.  See the
documentation of these types for more details on their specific behavior and how it
relates to Bertrand's overall monad ecosystem.

One of the most powerful features of monads is their composability.  There is nothing
inherently wrong with an optional union, or union of optionals, or async expected
tuple, for example.  They will all be treated in exactly the same way during pattern
matching and monadic operations.  Formally, visitors act by flattening monads into
their constituent types, meaning that they will recursively unpack any nested monads,
and monadic return types will always be merged into a canonical (non-nested) form.
These kinds of transformations keep the monadic interface clean and predictable, with
no extra boilerplate or special syntax, leading to simpler code that is easier to
reason about, maintain, and generalize to other languages, regardless of their
capabilities.
*/


namespace bertrand {


namespace impl {
    struct basic_union_tag {};
    struct union_tag {};
    struct optional_tag {};
    struct expected_tag {};

    template <typename... Ts>
    concept union_concept =
        sizeof...(Ts) > 1 && (meta::not_void<Ts> && ... && meta::unique<Ts...>);

    template <typename T, typename E, typename... Es>
    concept expected_concept =
        (meta::not_void<E> && ... && meta::not_void<Es>) &&
        meta::unique<std::conditional_t<meta::is_void<T>, NoneType, T>, E, Es...>;

    /* Provides an extensible mechanism for controlling the dispatching behavior of
    the `meta::visit` concept(s) and `impl::visit()` operator for a given type `T`.
    Users can specialize this structure to extend those utilities to arbitrary types,
    by providing the following information:
    
        -   `enable`: a boolean which must be set to `true` for all custom
            specializations.  Controls the output of the `meta::visitable<T>` concept.
        -   `monad`: a boolean which exposes type `T` to bertrand's monadic operator
            interface.  This can be checked via the `meta::visit_monad<T>` concept,
            which all monadic operators are constrained with.
        -   `type`: an alias to the type being visited, which must be identical to `T`.
        -   `alternatives`: a `meta::pack<Ts...>` with one or more types, which
            represent the exact alternatives that the monad can dispatch to.
        -   `value`: if the monad supports pointer-like dereference operators, then
            this must be an alias to the exact type returned by the `*` dereference
            operator.  Otherwise, it must be `void`.
        -   `empty`: an alias to the `None` state for this monad, or `void` if the
            monad does not model an empty state.  If this is not `void`, then it
            signals that the monad acts like an optional within the dispatch logic,
            which can trigger automatic propagation of the empty state and/or
            canonicalization of `T` to `Optional<T>` when appropriate.
        -   `errors`: a `meta::pack<Es...>` with zero or more error types, which
            represent the possible error states that the monad models.  If this is
            non-empty, then it singals that the monad acts like an expected within the
            dispatch logic, which can trigger automatic propagation of the error state
            and/or canonicalization of `T` to `Expected<T, Es...>` when appropriate.
        -   `index(T)`: a static method that returns the monad's active index, aligned
            to the given `alternatives`.  This is used to determine which branch to
            dispatch to when the monad is destructured within `impl::visit()`.
        -   `get<I>(T)`: a static method that takes a compile-time index `I` as a
            template parameter, and casts the monad to the `I`-th type in
            `alternatives` (including cvref qualifications).  The output from this is
            what gets passed into the visitor function, completing the dispatch.

    Built-in specializations are provided for:

        -   `bertrand::Union<Ts...>`
        -   `bertrand::Optional<T>`
        -   `bertrand::Expected<T, Es...>`
        -   `std::variant<Ts...>`
        -   `std::optional<T>`
        -   `std::expected<T, E>`

    The default specialization covers all other non-monad types, which will be passed
    through without dispatching.

    Note also that the type `T` may be arbitrarily-qualified, and always matches the
    qualifications of the observed type at the point where `impl::visit()` is called.
    It may therefore be useful to define all aliases in terms of `decltype()`
    reflection, which should always forward the qualifications of the input type,
    otherwise the visit logic may not work as intended. */
    template <typename T>
    struct visitable {
        static constexpr bool enable = false;
        static constexpr bool monad = false;
        using type = T;
        using alternatives = meta::pack<T>;
        using value = void;  // no pointer-like dereference operators
        using empty = void;  // no empty state
        using errors = meta::pack<>;  // no error states
    };

}


namespace meta {

    /* True for types which have a custom `impl::visitable<T>` specialization. */
    template <typename T>
    concept visitable = impl::visitable<T>::enable;

    /* True for types where `impl::visitable<T>::monad` is set to true. */
    template <typename T>
    concept visit_monad = impl::visitable<T>::monad;

    template <typename T>
    concept basic_union = inherits<T, impl::basic_union_tag>;

    template <typename T>
    concept Union = inherits<T, impl::union_tag>;

    template <typename T>
    concept Optional = inherits<T, impl::optional_tag>;

    template <typename T>
    concept Expected = inherits<T, impl::expected_tag>;

}


/// TODO: CTAD guides for `Union<Ts...>` and `Expected<T, Es...>` with visitable
/// initializers, which will automatically deduce the correct types from
/// `impl::visitable`.  Unfortunately, this is not possible in current C++, since
/// CTAD guides are required to list the deduced type on to the right of `->`, which
/// precludes direct expansions over a `meta::pack<...>`.  This would require a
/// standards proposal to allow CTAD guides to list a type different from the deduced
/// type after `->`, as long as it resolves to that type during instantiation.
/// Alternatively, a standards proposal that allows syntax such as
/// `using alternatives = Ts...;`, or direct fold expressions over `meta::pack<...>`
/// would also work, but none of these are currently available.


/* A convenience function that produces a `std::in_place_index_t` instance specialized
for a given alternative.  This is meant to be used as a disambiguation tag for union
constructors, which manually selects an alternative to initialize.  It can be followed
by any number of arguments, which will be forwarded to that alternative's
constructor. */
template <size_t I>
constexpr std::in_place_index_t<I> alternative;


template <typename... Ts> requires (impl::union_concept<Ts...>)
struct Union;


template <typename T = void>
struct Optional;


template <meta::not_pointer T> requires (meta::is_void<typename impl::visitable<T>::empty>)
Optional(T&&) -> Optional<meta::remove_rvalue<T>>;


template <meta::pointer T> requires (meta::is_void<typename impl::visitable<T>::empty>)
Optional(T) -> Optional<meta::dereference_type<T>>;


template <typename T> requires (meta::not_void<typename impl::visitable<T>::empty>)
Optional(T&&) -> Optional<typename impl::visitable<T>::value>;


template <typename T, typename E, typename... Es> requires (impl::expected_concept<T, E, Es...>)
struct Expected;


namespace meta {

    namespace detail {

        /* The base case for the dispatch logic is to directly forward to the
        underlying function, assuming the permutation is valid. */
        template <typename F, size_t j, typename prefix, typename... suffix>
        struct visit_base {
            using permutations = meta::pack<>;
            using errors = meta::pack<>;
            static constexpr bool optional = false;
            static constexpr bool enable = false;
        };
        template <typename F, size_t j, typename... prefix, typename... suffix>
            requires (j == 0 && meta::callable<F, prefix..., suffix...>)
        struct visit_base<F, j, meta::pack<prefix...>, suffix...> {
            using permutations = meta::pack<meta::pack<prefix..., suffix...>>;
            using errors = meta::pack<>;
            static constexpr bool optional = false;
            static constexpr bool enable = true;

            template <typename R>
            struct fn {
                using type = R;
                [[gnu::always_inline]] static constexpr R operator()(
                    meta::forward<F> func,
                    meta::forward<prefix>... prefix_args,
                    meta::forward<suffix>... suffix_args
                ) noexcept (meta::nothrow::call_returns<R, F, prefix..., suffix...>) {
                    return ::std::forward<F>(func)(
                        ::std::forward<prefix>(prefix_args)...,
                        ::std::forward<suffix>(suffix_args)...
                    );
                }
            };
        };

        ///////////////////////////
        ////    PERMUTATION    ////
        ///////////////////////////

        /* Base case: either `F` is directly callable with the given arguments, or
        we've run out of visitable arguments to match against. */
        template <
            typename F,  // function to invoke
            typename prefix,  // processed arguments
            size_t j,  // remaining number of mandatory visits along this path
            size_t k,  // remaining visit budget (strictly >= j)
            typename... suffix  // unprocessed arguments
        >
        struct _visit_permute : visit_base<F, j, prefix, suffix...> {};

        /* Skip to the next visitable argument in the event of a substitution
        failure, avoiding unnecessary instantiations of `_visit_permute` and limiting
        template depth. */
        template <typename...>
        constexpr size_t _visit_skip = 0;
        template <typename A, typename... As> requires (!meta::visitable<A>)
        constexpr size_t _visit_skip<A, As...> = _visit_skip<As...> + 1;
        template <typename, typename, typename, typename, size_t, size_t, typename...>
        struct visit_skip;
        template <
            size_t... prev,  // identifies the non-visitable suffix arguments to skip over
            size_t... next,  // identifies the arguments to skip to
            typename F,  // function to invoke
            typename... prefix,  // processed arguments to this point
            size_t j,  // remaining number of mandatory visits along this path
            size_t k,  // remaining visit budget (strictly >= j)
            typename... suffix  // unprocessed arguments
        >
        struct visit_skip<
            ::std::index_sequence<prev...>,
            ::std::index_sequence<next...>,
            F,
            meta::pack<prefix...>,
            j,
            k,
            suffix...
        > {
            using type = _visit_permute<
                F,
                meta::pack<prefix..., meta::unpack_type<prev, suffix...>...>,
                j,
                k,
                meta::unpack_type<sizeof...(prev) + next, suffix...>...
            >;
        };

        template <typename F, size_t j, size_t k, typename... As>
        concept visit_failure = k > 0 && (j > 0 || !meta::callable<F, As...>);

        /* Recursive case: `F` is not directly callable and `k` allows further visits,
        but either the current argument `A` is not visitable, or attempting to visit it
        results in substitution failure.  In either case, we forward it as-is and try
        to visit a future argument instead, skipping immediately to that argument if it
        exists. */
        template <typename F, typename... prefix, size_t j, size_t k, typename A, typename... suffix>
            requires (visit_failure<F, j, k, prefix..., A, suffix...>)
        struct _visit_permute<F, meta::pack<prefix...>, j, k, A, suffix...> : visit_skip<
            ::std::make_index_sequence<_visit_skip<suffix...>>,
            ::std::make_index_sequence<sizeof...(suffix) - _visit_skip<suffix...>>,
            F,
            meta::pack<prefix..., A>,
            j,
            k,
            suffix...
        >::type {};

        /* Attempt to substitute alternatives for the current argument, assuming it is
        visitable.  If not all alternatives are valid, then this will evaluate to a
        substitution failure that causes the algorithm to ignore this argument and
        attempt to visit a future argument instead. */
        template <
            typename F,  // function to invoke
            typename prefix,  // processed arguments preceding current
            typename A,  // current argument
            typename alts,  // alternatives for current argument
            size_t j,  // remaining number of mandatory visits along this path
            size_t k,  // remaining visit budget along this path
            typename suffix  // unprocessed arguments
        >
        struct visit_substitute { static constexpr bool enable = false; };

        template <typename E, typename>
        constexpr bool visit_propagate_error = false;
        template <typename E, typename... Es> requires (meta::is<E, Es> || ...)
        constexpr bool visit_propagate_error<E, meta::pack<Es...>> = true;

        /* Substitution only succeeds if every alternative is associated with at least
        one valid permutation, or is an empty or error state of the parent visitable
        which can be implicitly propagated, and the permutation is not ambiguous. */
        template <
            typename F,
            typename... prefix,
            meta::visitable A,
            typename... alts,
            size_t j,
            size_t k,
            typename... suffix
        >
            requires ((
                _visit_permute<
                    F,
                    meta::pack<prefix...>,
                    j == 0 ? 0 : j - 1,
                    k - 1,
                    alts,
                    suffix...
                >::permutations::size() > 0 ||
                meta::is<alts, typename impl::visitable<A>::empty> ||
                visit_propagate_error<alts, typename impl::visitable<A>::errors>
            ) && ... && !_visit_permute<F, meta::pack<prefix..., A>, j, k, suffix...>::enable)
        struct visit_substitute<
            F,
            meta::pack<prefix...>,
            A,
            meta::pack<alts...>,
            j,
            k,
            meta::pack<suffix...>
        > {
        private:
            using traits = impl::visitable<A>;

            template <typename alt>
            using sub = _visit_permute<
                F,
                meta::pack<prefix...>,
                j == 0 ? 0 : j - 1,
                k - 1,
                alt,
                suffix...
            >;

        public:
            using permutations = meta::concat<typename sub<alts>::permutations...>;
            using errors = meta::concat_unique<::std::conditional_t<
                (
                    sub<alts>::permutations::empty() &&
                    visit_propagate_error<alts, typename traits::errors>
                ),
                meta::pack<alts>,
                typename sub<alts>::errors
            >...>;
            static constexpr bool optional = ((sub<alts>::optional || (
                sub<alts>::permutations::empty() && meta::is<alts, typename traits::empty>
            )) || ...);
            static constexpr bool enable = true;

            /* `impl::visitable<A>::get<I>()` is used to unpack the correct alternative
            when forwarding to the substituted permutation.  If the substituted
            permutation is invalid, then the result of `get<I>()` will be directly
            forwarded to the return type instead. */
            template <typename R>
            struct fn {
            private:

                template <size_t I>
                struct dispatch {
                    using alt = meta::unpack_type<I, alts...>;
                    using child = sub<alt>;
                    static constexpr R operator()(
                        meta::forward<F> func,
                        meta::forward<prefix>... prefix_args,
                        meta::forward<A> visitable,
                        meta::forward<suffix>... suffix_args
                    )
                        noexcept (requires{{typename child::template fn<R>{}(
                            ::std::forward<F>(func),
                            ::std::forward<prefix>(prefix_args)...,
                            traits::template get<I>(::std::forward<A>(visitable)),
                            ::std::forward<suffix>(suffix_args)...
                        )} noexcept;})
                        requires (child::enable)
                    {
                        return typename child::template fn<R>{}(
                            ::std::forward<F>(func),
                            ::std::forward<prefix>(prefix_args)...,
                            traits::template get<I>(::std::forward<A>(visitable)),
                            ::std::forward<suffix>(suffix_args)...
                        );
                    }
                    static constexpr R operator()(
                        meta::forward<F> func,
                        meta::forward<prefix>... prefix_args,
                        meta::forward<A> visitable,
                        meta::forward<suffix>... suffix_args
                    )
                        noexcept (
                            meta::is<alt, typename traits::empty> ||
                            requires{{traits::template get<I>(
                                ::std::forward<A>(visitable)
                            )} noexcept -> meta::nothrow::convertible_to<R>;}
                        )
                        requires (!child::enable)
                    {
                        if constexpr (meta::is<alt, typename traits::empty>) {
                            if constexpr (meta::not_void<R>) {
                                return bertrand::None;
                            }
                        } else {
                            return traits::template get<I>(::std::forward<A>(visitable));
                        }
                    }
                };

                using vtable = impl::basic_vtable<dispatch, traits::alternatives::size()>;

            public:
                using type = R;
                [[gnu::always_inline]] static constexpr R operator()(
                    meta::forward<F> func,
                    meta::forward<prefix>... prefix_args,
                    meta::forward<A> visitable,
                    meta::forward<suffix>... suffix_args
                )
                    noexcept (requires{{vtable{traits::index(visitable)}(
                        ::std::forward<F>(func),
                        ::std::forward<prefix>(prefix_args)...,
                        ::std::forward<A>(visitable),
                        ::std::forward<suffix>(suffix_args)...
                    )} noexcept;})
                {
                    return vtable{traits::index(visitable)}(
                        ::std::forward<F>(func),
                        ::std::forward<prefix>(prefix_args)...,
                        ::std::forward<A>(visitable),
                        ::std::forward<suffix>(suffix_args)...
                    );
                }
            };
        };

        template <
            typename F,
            typename prefix,
            typename A,
            typename alts,
            size_t j,
            size_t k,
            typename suffix,
            typename... As
        >
        concept visit_success =
            visit_failure<F, j, k, As...> &&
            visit_substitute<F, prefix, A, alts, j, k, suffix>::enable;

        /* Visit success: `F` is not directly callable, `k` allows visits, and the
        current argument `A` is substitutable with its alternatives.  This terminates
        the recursion early, effectively preferring to visit earlier arguments before
        later ones in case of ambiguity, without ever needing to check them. */
        template <typename F, typename... prefix, size_t j, size_t k, typename A, typename... suffix>
            requires (visit_success<
                F,
                meta::pack<prefix...>,
                A,
                typename impl::visitable<A>::alternatives,
                j,
                k,
                meta::pack<suffix...>,
                prefix...,
                A,
                suffix...
            >)
        struct _visit_permute<F, meta::pack<prefix...>, j, k, A, suffix...> : visit_substitute<
            F,
            meta::pack<prefix...>,
            A,
            typename impl::visitable<A>::alternatives,
            j,
            k,
            meta::pack<suffix...>
        > {};

        /* Recursion is bounded by the number of visitables (including nested
        visitables) in the input arguments.  Choices of `k` beyond this limit are
        meaningless, since there are not enough visitables to satisfy them. */
        template <typename>
        constexpr size_t visit_max_k = 0;
        template <typename A, typename... As>
        constexpr size_t visit_max_k<meta::pack<A, As...>> = visit_max_k<meta::pack<As...>>;
        template <meta::visitable A, typename... As>
        constexpr size_t visit_max_k<meta::pack<A, As...>> =
            visit_max_k<typename impl::visitable<A>::alternatives> + 1 +
            visit_max_k<meta::pack<As...>>;

        template <size_t j, typename... A>
        struct visit_ctx {
            static constexpr size_t max_k = visit_max_k<meta::pack<A...>>;

            template <typename F, size_t k>
            using permute = _visit_permute<F, meta::pack<>, j, k, A...>;

            template <typename F, size_t k = j>
            struct type : permute<F, k> {};
            template <typename F, size_t k> requires (!permute<F, k>::enable && k < max_k)
            struct type<F, k> : type<F, k + 1> {};
        };

        /* Evaluate `_visit_permute` with a recursively-increasing `k`, starting at 0,
        until a minimal set of valid permutations is found, or `k` exceeds the number
        of visitables in the arguments, whichever comes first.  The result is a struct
        holding a 2D pack of packs, where the inner packs represent the valid
        permutations for each argument, along with metadata about implicitly-propagated
        empty and error states, from which an overall return type can be deduced. */
        template <typename F, size_t j, typename... A>
        using visit_permute = visit_ctx<j, A...>::template type<F>;

        /////////////////////////////////////
        ////    RETURN TYPE DEDUCTION    ////
        /////////////////////////////////////

        template <typename F, typename args>
        struct _visit_invoke {
            using type = void;
            static constexpr bool nothrow = false;
        };
        template <typename F, typename... args> requires (meta::callable<F, args...>)
        struct _visit_invoke<F, meta::pack<args...>> {
            using type = meta::call_type<F, args...>;
            static constexpr bool nothrow = meta::nothrow::callable<F, args...>;
        };
        template <typename F, typename... args>
            requires (meta::call_returns<bertrand::NoneType, F, args...>)
        struct _visit_invoke<F, meta::pack<args...>> {
            using type = void;
            static constexpr bool nothrow =
                meta::nothrow::call_returns<bertrand::NoneType, F, args...>;
        };

        /* Convert a permutation matrix from `visit_permute` into its corresponding
        unique return types, mapping types that are convertible to `None` into `void`,
        to simplify the optional promotion logic. */
        template <typename F, typename permutations>
        struct visit_invoke;
        template <typename F, typename... permutations>
        struct visit_invoke<F, meta::pack<permutations...>> {
            using returns = meta::to_unique<typename _visit_invoke<F, permutations>::type...>;
            static constexpr bool nothrow = (_visit_invoke<F, permutations>::nothrow && ...);
        };

        /* If there are multiple non-void return types, then we need to return a
        `Union` of those types.  Otherwise, we return the type itself if there is
        only one, or void if there are none. */
        template <typename>
        struct visit_to_union {
            using type = void;
            static constexpr bool trivial = true;
        };
        template <typename R>
        struct visit_to_union<meta::pack<R>> {
            using type = R;
            static constexpr bool trivial = true;
        };
        template <typename... Rs> requires (sizeof...(Rs) > 1)
        struct visit_to_union<meta::pack<Rs...>> {
            using type = bertrand::Union<Rs...>;
            static constexpr bool trivial = false;
        };

        /* If `option` is true (indicating either an unhandled empty state or void
        return type), then the result deduces to `Optional<R>`, where `R` is the output
        from `visit_to_union`.  If `R` is itself an optional type, then it will be
        flattened into the output. */
        template <typename R, bool optional>
        struct visit_to_optional {
            using type = R;
            static constexpr bool trivial = true;
        };
        template <meta::not_void R>
        struct visit_to_optional<R, true> {
            using type = bertrand::Optional<R>;
            static constexpr bool trivial = false;
        };

        /* If there are any accumulated error states, then the result will be further
        wrapped in `Expected<R, errors...>` to propagate them. */
        template <typename R, typename>
        struct visit_to_expected {
            using type = R;
            static constexpr bool trivial = true;
        };
        template <typename R, typename E, typename... Es>
        struct visit_to_expected<R, meta::pack<E, Es...>> {
            using type = bertrand::Expected<R, E, Es...>;
            static constexpr bool trivial = false;
        };

        template <typename R, typename returns, typename errors, bool optional>
        constexpr bool visit_nothrow = false;
        template <typename R, typename... returns, typename... errors, bool optional>
        constexpr bool visit_nothrow<R, meta::pack<returns...>, meta::pack<errors...>, optional> =
            (meta::nothrow::convertible_to<returns, R> && ...) &&
            (meta::nothrow::convertible_to<errors, R> && ...) &&
            (!optional || meta::nothrow::convertible_to<const bertrand::NoneType&, R>);

        /* Base case: combine the returns, errors, and optionals to form an overall
        return type */
        template <typename returns, typename errors, bool optional, typename>
        struct visit_deduce {
        private:
            using to_union = visit_to_union<returns>;
            using to_optional = visit_to_optional<typename to_union::type, optional>;
            using to_expected = visit_to_expected<typename to_optional::type, errors>;

        public:
            using type = to_expected::type;
            static constexpr bool trivial =
                to_union::trivial && to_optional::trivial && to_expected::trivial;
            static constexpr bool nothrow = visit_nothrow<type, returns, errors, optional>;
        };

        /* Recursive case: if `R` is void or convertible to `NoneType`, set `optional`
        to true and do not add it to the return types.  Otherwise, insert it as a
        return type if it is unique, or unpack its alternatives if it is a visitable
        union. */
        template <typename returns, typename errors, bool optional, typename R, typename... Rs>
        struct visit_deduce<returns, errors, optional, meta::pack<R, Rs...>> : visit_deduce<
            ::std::conditional_t<
                meta::is_void<R> || meta::convertible_to<R, bertrand::NoneType>,
                returns,
                meta::concat_unique<
                    returns,
                    typename impl::visitable<R>::alternatives::template map<meta::remove_rvalue>
                >
            >,
            errors,
            optional || meta::is_void<R> || meta::convertible_to<R, bertrand::NoneType>,
            meta::pack<Rs...>
        > {};

        /* Optional/Expected case: if `R` is an optional or expected type, then record
        those states in `optional` and `errors` before recurring for the underlying
        value type. */
        template <typename returns, typename errors, bool optional, typename R, typename... Rs>
            requires (
                meta::not_void<typename impl::visitable<R>::empty> ||
                impl::visitable<R>::errors::size() > 0
            )
        struct visit_deduce<returns, errors, optional, meta::pack<R, Rs...>> : visit_deduce<
            returns,
            meta::concat_unique<
                errors,
                typename impl::visitable<R>::errors::template map<meta::remove_rvalue>
            >,
            optional || meta::not_void<typename impl::visitable<R>::empty>,
            meta::pack<typename impl::visitable<R>::value, Rs...>
        > {};

        ///////////////////////////
        ////    ENTRY POINT    ////
        ///////////////////////////

        /* The actual visit metafunction provides simplified access to the dispatch
        logic and metaprogramming traits for the visit operation.  Calling the
        `visit` metafunction will execute the dispatch logic for the
        perfectly-forwarded function and argument types. */
        template <typename F, size_t j, typename... A>
        struct visit {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool exhaustive = false;
            static constexpr bool consistent = false;
            static constexpr bool nothrow = false;
        };
        template <typename F, size_t j, typename... A> requires (visit_permute<F, j, A...>::enable)
        struct visit<F, j, A...> : visit_permute<F, j, A...>::template fn<
            typename visit_deduce<
                meta::pack<>,
                typename visit_permute<F, j, A...>::errors,
                visit_permute<F, j, A...>::optional,
                typename visit_invoke<F, typename visit_permute<F, j, A...>::permutations>::returns
            >::type
        > {
        private:
            using permute = visit_permute<F, j, A...>;
            using invoke = visit_invoke<F, typename permute::permutations>;
            using deduce = visit_deduce<
                meta::pack<>,
                typename permute::errors,
                permute::optional,
                typename invoke::returns
            >;

        public:
            /// NOTE: ::type is inherited from `fn<R>`, along with an appropriate call
            /// operator.
            static constexpr bool enable = true;
            static constexpr bool exhaustive = !permute::optional && permute::errors::empty();
            static constexpr bool consistent = exhaustive && invoke::returns::size() <= 1;
            static constexpr bool nothrow = invoke::nothrow && deduce::nothrow;
        };

    }

    /* A visitor function can only be applied to a set of arguments if it covers all
    non-empty and non-error states of the visitable arguments. */
    template <typename F, typename... Args>
    concept visit = detail::visit<F, 0, Args...>::enable;
    template <size_t min_visits, typename F, typename... Args>
    concept force_visit = detail::visit<F, min_visits, Args...>::enable;

    /* Specifies that a visitor function covers all states of the visitable arguments,
    including empty and error states. */
    template <typename F, typename... Args>
    concept visit_exhaustive = visit<F, Args...> && detail::visit<F, 0, Args...>::exhaustive;
    template <size_t min_visits, typename F, typename... Args>
    concept force_visit_exhaustive =
        force_visit<min_visits, F, Args...> && detail::visit<F, min_visits, Args...>::exhaustive;

    /* Specifies that a visitor function covers all states of the visitable arguments,
    including empty and error states, and that all permutations return the same
    type. */
    template <typename F, typename... Args>
    concept visit_consistent = visit<F, Args...> && detail::visit<F, 0, Args...>::consistent;
    template <size_t min_visits, typename F, typename... Args>
    concept force_visit_consistent =
        force_visit<min_visits, F, Args...> && detail::visit<F, min_visits, Args...>::consistent;

    /* Visitor functions return a type that is derived from each permutation according
    to the following rules:

        1.  If `meta::visit_consistent<F, Args...>` is satisfied, then the return type
            is identical to the shared return type for all permutations, subject to
            canonicalization of expected, optional, and union outputs (in that order,
            e.g. `Expected<Optional<Union<Ts...>>, Es...>`).
        2.  If `meta::visit<F, Args...>` is satisfied, but
            `meta::visit_consistent<F, Args...>` is not, meaning that the visitor
            returns multiple types depending on the permutation, and/or propagates one
            or more empty or error states, then the return types will be analyzed as
            follows:
                a.  If any return type is an expected, then the possible error states
                    will be stripped and merged into the propagation set, promoting
                    the return type to an `Expected<T, Es...>`.  Only the value type
                    will be forwarded to (b).
                b.  If any return type is an optional or `void`, then the overall
                    return type will be promoted to an `Optional` and only the value
                    type will be forwarded to (c).  If `void`, then no type will be
                    forwarded.
                c.  If any return type is a union (i.e. has a corresponding
                    `impl::visitable` specialization without `empty` or `errors`
                    states), then the overall return type will deduce to `Union<Rs...>`,
                    where `Rs...` are the flattened alternatives.  If any of those
                    alternatives meet the criteria for (a) and/or (b), then they will
                    be recursively forwarded to those steps.  Otherwise, the return
                    type is forwarded as-is and promoted according to steps (a) and (b)
                    to produce the final result.

    Inconsistent visitors are extremely useful when implementing the monadic interface
    for visitable types.  By applying the above rules, such operations will always
    yield a regular, canonical form that can be used in further operations. */
    template <typename F, typename... Args> requires (visit<F, Args...>)
    using visit_type = detail::visit<F, 0, Args...>::type;
    template <size_t min_visits, typename F, typename... Args>
        requires (force_visit<min_visits, F, Args...>)
    using force_visit_type = detail::visit<F, min_visits, Args...>::type;

    /* Tests whether `meta::visit<F, Args...>` is satisfied and the result can be
    implicitly converted to the specified type.  See `meta::visit_type<F, Args...>` for
    a description of how the return type is deduced. */
    template <typename Ret, typename F, typename... Args>
    concept visit_returns = visit<F, Args...> && convertible_to<Ret, visit_type<F, Args...>>;
    template <typename Ret, size_t min_visits, typename F, typename... Args>
    concept force_visit_returns =
        force_visit<min_visits, F, Args...> &&
        convertible_to<Ret, force_visit_type<min_visits, F, Args...>>;

    namespace nothrow {

        template <typename F, typename... Args>
        concept visit = meta::visit<F, Args...> && detail::visit<F, 0, Args...>::nothrow;
        template <size_t min_visits, typename F, typename... Args>
        concept force_visit =
            meta::force_visit<min_visits, F, Args...> &&
            detail::visit<F, min_visits, Args...>::nothrow;

        template <typename F, typename... Args>
        concept visit_exhaustive =
            meta::visit_exhaustive<F, Args...> && nothrow::visit<F, Args...>;
        template <size_t min_visits, typename F, typename... Args>
        concept force_visit_exhaustive =
            meta::force_visit_exhaustive<min_visits, F, Args...> &&
            nothrow::force_visit<min_visits, F, Args...>;

        template <typename F, typename... Args>
        concept visit_consistent =
            meta::visit_consistent<F, Args...> && nothrow::visit<F, Args...>;
        template <size_t min_visits, typename F, typename... Args>
        concept force_visit_consistent =
            meta::force_visit_consistent<min_visits, F, Args...> &&
            nothrow::force_visit<min_visits, F, Args...>;

        template <typename F, typename... Args> requires (nothrow::visit<F, Args...>)
        using visit_type = meta::visit_type<F, Args...>;
        template <size_t min_visits, typename F, typename... Args>
            requires (nothrow::force_visit<min_visits, F, Args...>)
        using force_visit_type = meta::force_visit_type<min_visits, F, Args...>;

        template <typename Ret, typename F, typename... Args>
        concept visit_returns =
            nothrow::visit<F, Args...> &&
            nothrow::convertible_to<Ret, nothrow::visit_type<F, Args...>>;
        template <typename Ret, size_t min_visits, typename F, typename... Args>
        concept force_visit_returns =
            nothrow::force_visit<min_visits, F, Args...> &&
            nothrow::convertible_to<Ret, nothrow::force_visit_type<min_visits, F, Args...>>;

        template <typename... Ts>
            requires (
                detail::visit_deduce<meta::pack<>, meta::pack<>, false, meta::pack<Ts...>>::nothrow
            )
        using make_union =
            detail::visit_deduce<meta::pack<>, meta::pack<>, false, meta::pack<Ts...>>::type;

    }

    /* Form a canonical union type from the given input types, filtering for uniqueness
    and flattening any nested monads. */
    template <typename... Ts>
    using make_union =
        detail::visit_deduce<meta::pack<>, meta::pack<>, false, meta::pack<Ts...>>::type;

    /* Detect whether the canonical union type for the given inputs does not trigger
    promotion to a `Union`, `Optional`, or `Expected` monad, meaning that `make_union`
    reduces to a single scalar type after filtering for uniqueness and flattening any
    nested monads. */
    template <typename... Ts>
    concept trivial_union = detail::visit_deduce<
        meta::pack<>,
        meta::pack<>,
        false,
        meta::pack<Ts...>
    >::trivial;

    namespace detail {

        template <typename... Ts>
        constexpr bool prefer_constructor<bertrand::Union<Ts...>> = true;

        template <typename T>
        constexpr bool prefer_constructor<bertrand::Optional<T>> = true;

        template <typename T, typename... Es>
        constexpr bool prefer_constructor<bertrand::Expected<T, Es...>> = true;

    }

}


namespace impl {

    /* Invoke a function with the given arguments, unwrapping any sum types in the
    process.  This is similar to `std::visit()`, but with greatly expanded
    metaprogramming capabilities to fit Bertrand's overall monadic interface.

    A visitor is constructed from either a single function or a set of functions
    arranged into an overload set.  Any subsequent arguments will be passed to the
    visitor in the order they are defined, with each sum type being unwrapped to its
    current alternative within the visitor context.  A compilation error occurs if the
    visitor is not callable with all non-empty and non-error states of each
    alternative.

    Note that the visitor is free to ignore the empty and error states of optionals and
    expecteds respectively, which will be implicitly propagated to the return type if
    left unhandled.  This equates to adding additional implicit overloads to the
    visitor for these states, which reduce to a simple identity function that forwards
    that state without modification.  This allows visitors to treat optional and
    expected types as if they were always in the valid state.  The
    `meta::visit_exhaustive<F, Args...>` concept can be used to selectively forbid
    this behavior at compile time, which forces the visitor to explicitly handle all
    states instead.

    Similarly, the visitor is not constrained to return a single consistent type for
    all permutations.  If it does not, then the return type `R` will deduce according
    to the description provided in `meta::visit_type<F, Args...>`, which flattens and
    canonicalizes the output types into a regular form.  Similar to above, the
    `meta::visit_consistent<F, Args...>` concept can be used to selectively
    forbid this behavior at compile time, which forces the visitor to return a single
    consistent type for all permutations.  Note that
    `meta::visit_consistent<F, Args...>` automatically implies
    `meta::visit_exhaustive<F, Args...>`, and yields similar visitor semantics to
    `std::visit()`.

    Finally, note that the arguments are fully generic, and not strictly limited to
    visitable types, in contrast to `std::visit()`.  If no visitables are present, then
    `visit()` devolves to an inline invocation of the visitor directly, without any
    special handling.  Otherwise, the component visitables are expanded according to
    the semantics laid out in `impl::visitable<T>`, which describes how to register
    custom visitable types for use with this function. */
    template <size_t min_visits = 0, typename F, typename... Args>
    [[gnu::always_inline]] constexpr decltype(auto) visit(F&& f, Args&&... args)
        noexcept (meta::nothrow::force_visit<min_visits, F, Args...>)
        requires (meta::force_visit<min_visits, F, Args...>)
    {
        return (meta::detail::visit<F, min_visits, Args...>{}(
            std::forward<F>(f),
            std::forward<Args>(args)...
        ));
    }

    template <typename T, typename = std::make_index_sequence<meta::unqualify<T>::types::size()>>
    struct _basic_union_types;
    template <typename T, size_t... Is>
    struct _basic_union_types<T, std::index_sequence<Is...>> {
        using type = meta::pack<decltype((std::declval<T>().template get<Is>()))...>;
    };
    template <typename T>
    using basic_union_types = _basic_union_types<T>::type;

    template <meta::Union T>
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using alternatives = basic_union_types<decltype((std::declval<T>().__value))>;
        using value = void;
        using empty = void;
        using errors = meta::pack<>;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u)
            noexcept (requires{{u.__value.index()} noexcept;})
        {
            return u.__value.index();
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{std::forward<T>(u).__value.template get<I>()} noexcept;})
        {
            return (std::forward<T>(u).__value.template get<I>());
        }
    };

    template <meta::std::variant T>
    struct visitable<T> {
    private:
        template <typename>
        struct _alternatives;
        template <size_t... Is>
        struct _alternatives<std::index_sequence<Is...>> {
            using type = meta::pack<decltype((std::get<Is>(std::declval<T>())))...>;
        };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using alternatives = _alternatives<
            std::make_index_sequence<std::variant_size_v<meta::unqualify<T>>>
        >::type;
        using value = void;
        using empty = void;
        using errors = meta::pack<>;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u)
            noexcept (requires{{u.index()} noexcept;})
        {
            return u.index();
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{std::get<I>(std::forward<T>(u))} noexcept;})
        {
            return (std::get<I>(std::forward<T>(u)));
        }
    };

    template <meta::Optional T>
        requires (basic_union_types<decltype((std::declval<T>().__value))>::size() > 1)
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using empty = decltype((std::declval<T>().__value.template get<0>()));
        using value = decltype((std::declval<T>().__value.template get<1>()));
        using alternatives = meta::pack<empty, value>;
        using errors = meta::pack<>;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u) noexcept {
            return u.__value.index();
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{std::forward<T>(u).__value.template get<I>()} noexcept;})
        {
            return (std::forward<T>(u).__value.template get<I>());
        }
    };

    /// NOTE: we need a separate specialization to account for `Optional<void>`, which
    /// only stores the empty state.
    template <meta::Optional T>
        requires (basic_union_types<decltype((std::declval<T>().__value))>::size() == 1)
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using empty = decltype((std::declval<T>().__value.template get<0>()));
        using value = empty;
        using alternatives = meta::pack<empty>;
        using errors = meta::pack<>;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u) noexcept {
            return 0;
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{std::forward<T>(u).__value.template get<I>()} noexcept;})
        {
            return (std::forward<T>(u).__value.template get<0>());
        }
    };

    template <meta::std::optional T>
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using empty = const NoneType&;
        using value = decltype((*std::declval<T>()));
        using alternatives = meta::pack<const NoneType&, value>;
        using errors = meta::pack<>;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u) noexcept {
            return u.has_value();
        }

        template <size_t I> requires (I == 0)
        [[gnu::always_inline]] static constexpr const auto& get(meta::forward<T> u) noexcept {
            return std::nullopt;
        }

        template <size_t I> requires (I == 1)
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{*std::forward<T>(u)} noexcept;})
        {
            return (*std::forward<T>(u));
        }
    };

    template <meta::Expected T>
    struct visitable<T> {
    private:
        template <typename>
        struct _errors;
        template <size_t... Is>
        struct _errors<std::index_sequence<Is...>> {
            using type = meta::pack<
                decltype((std::declval<T>().__value.template get<Is + 1>()))...
            >;
        };

    public:
        static constexpr bool enable = true;
        static constexpr bool monad = true;
        using type = T;
        using alternatives = basic_union_types<decltype((std::declval<T>().__value))>;
        using value = decltype((*std::declval<T>()));
        using empty = void;
        using errors = _errors<std::make_index_sequence<alternatives::size() - 1>>::type;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u) noexcept {
            return u.__value.index();
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{std::forward<T>(u).__value.template get<I>()} noexcept;})
        {
            return (std::forward<T>(u).__value.template get<I>());
        }
    };

    template <typename T>
    struct std_expected_type { using type = decltype((*std::declval<T>())); };
    template <typename T> requires (meta::is_void<typename meta::unqualify<T>::value_type>)
    struct std_expected_type<T> { using type = NoneType; };

    template <meta::std::expected T>
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using value = std_expected_type<typename meta::unqualify<T>::value_type>::type;
        using errors = visitable<decltype((std::declval<T>().error()))>::alternatives;
        using alternatives = meta::concat<meta::pack<value>, errors>;
        using empty = void;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u) noexcept {
            if constexpr (errors::size() > 1) {
                return u.has_value() ?
                    0 :
                    visitable<decltype((u.error()))>::index(u.error()) + 1;
            } else {
                return u.has_value();
            }
        }

        template <size_t I>
            requires (I == 0 && meta::not_void<typename meta::unqualify<T>::value_type>)
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{*std::forward<T>(u)} noexcept;})
        {
            return (*std::forward<T>(u));
        }

        template <size_t I>
            requires (I == 0 && meta::is_void<typename meta::unqualify<T>::value_type>)
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u) noexcept {
            return (None);
        }

        template <size_t I> requires (I > 0 && I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{
                visitable<decltype((std::forward<T>(u).error()))>::template get<I - 1>(
                    std::forward<T>(u).error()
                )} noexcept;
            })
        {
            return (visitable<decltype((std::forward<T>(u).error()))>::template get<I - 1>(
                std::forward<T>(u).error()
            ));
        }
    };

    /* A generic visitor that backs the `->*` pattern matching operator for visitable
    types. */
    template <typename F>
    struct visit_pattern {
        /* Base: if the function to the right of `->*` is directly callable with the
        current alternative, prefer that and terminate recursion. */
        template <typename A>
        static constexpr decltype(auto) operator()(meta::forward<F> f, A&& alt)
            noexcept (requires{{std::forward<F>(f)(std::forward<A>(alt))} noexcept;})
            requires (requires{{std::forward<F>(f)(std::forward<A>(alt))};})
        {
            return (std::forward<F>(f)(std::forward<A>(alt)));
        }

        /* Recursive: If the function is not directly callable, attempt to recursively
        apply the `->*` operator on the alternative, allowing for nested patterns. */
        template <typename A>
        static constexpr decltype(auto) operator()(meta::forward<F> f, A&& alt)
            noexcept (requires{{std::forward<A>(alt)->*std::forward<F>(f)} noexcept;})
            requires (
                !requires{{std::forward<F>(f)(std::forward<A>(alt))};} &&
                requires{{std::forward<A>(alt)->*std::forward<F>(f)};}
            )
        {
            return (std::forward<A>(alt)->*std::forward<F>(f));
        }
    };

    /* Monads are formattable if all of their alternatives are formattable in turn. */
    template <typename, typename>
    constexpr bool _alternatives_are_formattable = false;
    template <typename... Ts, typename Char> requires (meta::formattable<Ts, Char> && ...)
    constexpr bool _alternatives_are_formattable<meta::pack<Ts...>, Char> = true;
    template <meta::visit_monad T, typename Char>
    constexpr bool alternatives_are_formattable =
        _alternatives_are_formattable<typename impl::visitable<T>::alternatives, Char>;

}


/////////////////////
////    UNION    ////
/////////////////////


namespace impl {

    /* Find the first type in Ts... that is default constructible (void if none) */
    template <typename...>
    struct _union_default_type { using type = void; };
    template <meta::default_constructible T, typename... Ts>
    struct _union_default_type<T, Ts...> { using type = T; };
    template <typename T, typename... Ts>
    struct _union_default_type<T, Ts...> : _union_default_type<Ts...> {};
    template <typename... Ts>
    using union_default_type = _union_default_type<Ts...>::type;

    /* A basic tagged union of alternatives `Ts...`, which automatically forwards
    lvalue references and provides vtable-based copy, move, swap, and destruction
    operators, as well as unsafe `index()` and `get<I>()` accessors that walk the
    overall structure.  This is a fundamental building block for sum types, and can
    dramatically reduce the amount of bookkeeping necessary to safely work with raw C
    unions. */
    template <meta::not_void... Ts> requires (!meta::rvalue<Ts> && ...)
    struct basic_union : basic_union_tag {
        using types = meta::pack<Ts...>;
        using default_type = impl::union_default_type<Ts...>;

        template <size_t I> requires (I < sizeof...(Ts))
        using tag = std::in_place_index_t<I>;

        static constexpr size_t size() noexcept { return sizeof...(Ts); }
        static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        static constexpr bool empty() noexcept { return (sizeof...(Ts) == 0); }

    private:
        template <size_t I, typename T>
        static constexpr decltype(auto) get_at(T&& u) noexcept {
            if constexpr (I == 0) {
                return (std::forward<T>(u));
            } else {
                return (get_at<I - 1>(std::forward<T>(u).rest));
            }
        }

        template <typename... Us>
        union _type {
            constexpr _type() noexcept {};
            constexpr ~_type() noexcept {};
        };
        template <typename U, typename... Us>
        union _type<U, Us...> {
            [[no_unique_address]] impl::ref<U> curr;
            [[no_unique_address]] _type<Us...> rest;

            constexpr _type() noexcept {}
            constexpr ~_type() noexcept {}

            template <typename... A>
            constexpr _type(tag<0>, A&&... args)
                noexcept (requires{{U(std::forward<A>(args)...)} noexcept;})
                requires (requires{{U(std::forward<A>(args)...)};})
            :
                curr{U(std::forward<A>(args)...)}
            {}

            template <size_t I, typename... A> requires (I > 0)
            constexpr _type(tag<I>, A&&... args)
                noexcept (meta::nothrow::constructible_from<_type<Us...>, tag<I - 1>, A...>)
                requires (meta::constructible_from<_type<Us...>, tag<I - 1>, A...>)
            :
                rest(tag<I - 1>{}, std::forward<A>(args)...)  // recur
            {}

            constexpr _type(_type&& other)
                noexcept (meta::nothrow::movable<impl::ref<U>>)
                requires (meta::movable<impl::ref<U>>)
            :
                curr(std::move(other).curr)
            {}

            template <typename... Vs> requires (sizeof...(Vs) <= sizeof...(Us))
            constexpr _type(_type<Vs...>&& other)
                noexcept (meta::nothrow::movable<_type<Us...>>)
                requires (meta::movable<_type<Us...>>)
            :
                rest(std::move(other).rest)
            {}

            /// TODO: maybe copy/move constructors can bypass the issue with
            /// the swap operator?  I would just take a top-level move/assign
            /// against the upper union, which might be smart enough to do what I
            /// need.

            /// -> The first move would call the move constructor of the top-level
            /// type with the detected subtype from the current union.  Then, the
            /// top-level assignment operator would destroy the leftover value and
            /// then move-construct the new value in the appropriate place.  It's a
            /// long shot, but that might be sophisticated enough to actually work.

            /// I would need to use some kind of `owning_tag<I, _type&&>` class for
            /// the assignment operator.  If `I` reaches zero, then I would destroy the
            /// moved-from value at this index.  Then, if the moved type is further
            /// down the chain, I would construct `rest` with
            /// `owning_tag<I - 1, _type&&>` until the type is found, at which point
            /// I use it to move-construct the new value in place.  If the `__type&&`
            /// is above the zeroth index in the chain, then I would destroy `rest`
            /// for every index between it and the new value, and then move-construct
            /// the new value at the end.  This should hopefully ensure that I never
            /// access a `rest` object that is outside its lifetime at any point.
        };

    public:
        using type = _type<Ts...>;
        [[no_unique_address]] type m_data;
        [[no_unique_address]] meta::smallest_unsigned_int<sizeof...(Ts)> m_index;

        /* Default constructor selects the first default-constructible type in `Ts...`,
        and initializes the union to that type. */
        [[nodiscard]] constexpr basic_union()
            noexcept (meta::nothrow::default_constructible<default_type>)
            requires (meta::not_void<default_type>)
        :
            m_data(tag<meta::index_of<union_default_type<Ts...>, Ts...>>{}),
            m_index(meta::index_of<union_default_type<Ts...>, Ts...>)
        {}

        /* Tagged constructor specifically initializes the alternative at index `I`
        with the given arguments. */
        template <size_t I, typename... A>
        [[nodiscard]] constexpr basic_union(tag<I> t, A&&... args)
            noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, Ts...>, A...>)
            requires (meta::constructible_from<meta::unpack_type<I, Ts...>, A...>)
        :
            m_data(tag<I>{}, std::forward<A>(args)...),
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
            return (*get_at<I>(std::forward<Self>(self).m_data).curr);
        }

        /* Access a specific type, assuming it is present in the union. */
        template <typename T, typename Self> requires (types::template contains<T>())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return (*get_at<meta::index_of<T, Ts...>>(std::forward<Self>(self).m_data).curr);
        }

        /* Get a pointer to a specific value by index if it is the active alternative.
        Returns a null pointer otherwise. */
        template <size_t I>
            requires (
                I < sizeof...(Ts) &&
                meta::has_address<meta::as_lvalue<meta::unpack_type<I, Ts...>>>
            )
        [[nodiscard]] constexpr auto get_if() noexcept
            -> meta::address_type<meta::as_lvalue<meta::unpack_type<I, Ts...>>>
        {
            return m_index == I ? std::addressof(get<I>()) : nullptr;
        }

        /* Get a pointer to a specific value by index if it is the active alternative.
        Returns a null pointer otherwise. */
        template <size_t I>
            requires (
                I < sizeof...(Ts) &&
                meta::has_address<meta::as_const_ref<meta::unpack_type<I, Ts...>>>
            )
        [[nodiscard]] constexpr auto get_if() const noexcept
            -> meta::address_type<meta::as_const_ref<meta::unpack_type<I, Ts...>>>
        {
            return m_index == I ? std::addressof(get<I>()) : nullptr;
        }

        /* Get a pointer to a specific type if it is the active alternative.  Returns
        a null pointer otherwise. */
        template <typename T>
            requires (
                types::template contains<T>() &&
                meta::has_address<meta::as_lvalue<T>>
            )
        [[nodiscard]] constexpr auto get_if() noexcept
            -> meta::address_type<meta::as_lvalue<T>>
        {
            constexpr size_t I = meta::index_of<T, Ts...>;
            return m_index == I ? std::addressof(get<I>()) : nullptr;
        }

        /* Get a pointer to a specific type if it is the active alternative.  Returns
        a null pointer otherwise. */
        template <typename T>
            requires (
                types::template contains<T>() &&
                meta::has_address<meta::as_const_ref<T>>
            )
        [[nodiscard]] constexpr auto get_if() const noexcept
            -> meta::address_type<meta::as_const_ref<T>>
        {
            constexpr size_t I = meta::index_of<T, Ts...>;
            return m_index == I ? std::addressof(get<I>()) : nullptr;
        }

    private:
        static constexpr bool copyable = ((meta::lvalue<Ts> || meta::copyable<Ts>) && ...);
        static constexpr bool nothrow_copyable =
            ((meta::lvalue<Ts> || meta::nothrow::copyable<Ts>) && ...);
        static constexpr bool trivially_copyable =
            ((meta::lvalue<Ts> || meta::trivially_copyable<Ts>) && ...);

        static constexpr bool movable =
            ((meta::lvalue<Ts> || meta::movable<Ts>) && ...);
        static constexpr bool nothrow_movable =
            ((meta::lvalue<Ts> || meta::nothrow::movable<Ts>) && ...);

        static constexpr bool destructible =
            ((meta::lvalue<Ts> || meta::destructible<Ts>) && ...);
        static constexpr bool nothrow_destructible =
            ((meta::lvalue<Ts> || meta::nothrow::destructible<Ts>) && ...);
        static constexpr bool trivially_destructible =
            ((meta::lvalue<Ts> || meta::trivially_destructible<Ts>) && ...);

        static constexpr bool swappable =
            ((meta::lvalue<Ts> || (meta::destructible<Ts> && meta::movable<Ts>)) && ...);
        static constexpr bool nothrow_swappable = ((meta::lvalue<Ts> || (
            meta::nothrow::destructible<Ts> &&
            meta::nothrow::movable<Ts> && (
                meta::nothrow::swappable<Ts> ||
                (!meta::swappable<Ts> && meta::nothrow::move_assignable<Ts>) ||
                (!meta::swappable<Ts> && !meta::move_assignable<Ts>)
            )
        )) && ...);

        template <size_t I>
        struct copy {
            static constexpr type operator()(const basic_union& other)
                noexcept (nothrow_copyable)
            {
                return {tag<I>{}, other.template get<I>()};
            }
        };

        template <size_t I>
        struct move {
            static constexpr type operator()(basic_union&& other)
                noexcept (nothrow_movable)
            {
                return {tag<I>{}, std::move(other).template get<I>()};
            }
        };

        template <size_t I>
        struct destroy {
            static constexpr void operator()(type& u)
                noexcept (nothrow_destructible)
                requires (requires{{std::destroy_at(std::addressof(get_at<I>(u).curr))};})
            {
                if constexpr (!meta::trivially_destructible<decltype(get_at<I>(u).curr)>) {
                    std::destroy_at(std::addressof(get_at<I>(u).curr));
                }
            }
        };

        template <size_t I>
        struct _swap {
            static constexpr void operator()(basic_union& self, basic_union& other)
                noexcept (nothrow_swappable)
            {
                static constexpr size_t J = I / sizeof...(Ts);
                static constexpr size_t K = I % sizeof...(Ts);
                using T = meta::unpack_type<J, Ts...>;

                // prefer a direct swap if the indices match and a corresponding operator
                // is available
                if constexpr (J == K) {
                    std::ranges::swap(
                        get_at<J>(self.m_data).curr,
                        get_at<K>(other.m_data).curr
                    );

                // If the indices differ or the types are lvalues, then we need to move
                // construct and destroy the original value behind us.
                } else {
                    type temp(get_at<J>(std::move(self).m_data));
                    destroy<J>{}(self.m_data);
                    try {
                        std::construct_at(
                            &self.m_data,
                            get_at<K>(std::move(other).m_data)
                        );
                        destroy<K>{}(other.m_data);
                        try {
                            std::construct_at(
                                &other.m_data,
                                get_at<J>(std::move(temp))
                            );
                            destroy<J>{}(temp);
                        } catch (...) {
                            std::construct_at(
                                &other.m_data,
                                get_at<K>(std::move(self).m_data)
                            );
                            destroy<K>{}(self.m_data);
                            throw;
                        }
                    } catch (...) {
                        std::construct_at(
                            &self.m_data,
                            get_at<J>(std::move(temp))
                        );
                        destroy<J>{}(temp);
                        throw;
                    }
                    other.m_index = J;
                    self.m_index = K;
                }
            }
        };

        static constexpr type dispatch_copy(const basic_union& other)
            noexcept (nothrow_copyable)
            requires (copyable)
        {
            return impl::basic_vtable<copy, size()>{other.index()}(other);
        }

        static constexpr type dispatch_move(basic_union&& other)
            noexcept (nothrow_movable)
            requires (movable)
        {
            return impl::basic_vtable<move, size()>{other.index()}(std::move(other));
        }

        static constexpr void dispatch_destroy(basic_union& self)
            noexcept (nothrow_destructible)
            requires (destructible)
        {
            if constexpr (!trivially_destructible) {
                impl::basic_vtable<destroy, size()>{self.index()}(self.m_data);
            }
        }

        static constexpr void dispatch_swap(basic_union& lhs, basic_union& rhs)
            noexcept (nothrow_swappable)
            requires (swappable)
        {
            return impl::basic_vtable<_swap, sizeof...(Ts) * sizeof...(Ts)>{
                lhs.index() * sizeof...(Ts) + rhs.index()
            }(lhs, rhs);
        }

    public:
        [[nodiscard]] constexpr basic_union(const basic_union& other)
            noexcept (nothrow_copyable)
            requires (copyable)
        :
            m_data(dispatch_copy(other)),
            m_index(other.m_index)
        {}

        [[nodiscard]] constexpr basic_union(basic_union&& other)
            noexcept (nothrow_movable)
            requires (movable)
        :
            m_data(dispatch_move(std::move(other))),
            m_index(other.m_index)
        {}

        constexpr basic_union& operator=(const basic_union& other)
            noexcept (nothrow_copyable && nothrow_swappable)
            requires (copyable && swappable)
        {
            if (this != &other) {
                basic_union temp(other);
                dispatch_swap(*this, temp);
            }
            return *this;
        }

        constexpr basic_union& operator=(basic_union&& other)
            noexcept (nothrow_movable && nothrow_swappable)
            requires (movable && swappable)
        {
            if (this != &other) {
                basic_union temp(std::move(other));
                dispatch_swap(*this, temp);
            }
            return *this;
        }

        constexpr ~basic_union()
            noexcept (nothrow_destructible)
            requires (destructible)
        {
            dispatch_destroy(*this);
        }

        constexpr void swap(basic_union& other)
            noexcept (nothrow_swappable)
            requires (swappable)
        {
            if (this != &other) {
                dispatch_swap(*this, other);
            }
        }
    };

    /* Result 1: convert to proximal type. */
    template <typename from, typename proximal, typename convert, typename...>
    struct _union_convert_from { using type = proximal; };

    /* Result 2: convert to first implicitly convertible type (void if none). */
    template <typename from, meta::is_void proximal, typename convert>
    struct _union_convert_from<from, proximal, convert> { using type = convert; };

    template <typename from, typename curr>
    concept union_proximal =
        meta::inherits<from, curr> &&
        meta::convertible_to<from, curr> &&
        (meta::lvalue<from> ? !meta::rvalue<curr> : !meta::lvalue<curr>);

    template <typename from, typename proximal, typename curr>
    using union_replace_proximal = std::conditional_t<
        meta::is_void<proximal> ||
        (meta::inherits<curr, proximal> && !meta::is<curr, proximal>) || (
            meta::is<curr, proximal> && (
                (meta::lvalue<curr> && !meta::lvalue<proximal>) ||
                meta::more_qualified_than<proximal, curr>
            )
        ),
        curr,
        proximal
    >;

    /* Recursive 1: prefer the most derived and least qualified matching alternative,
    with lvalues binding to lvalues and prvalues, and rvalues binding to rvalues and
    prvalues.  If the result type is void, the candidate is more derived than it, or
    the candidate is less qualified, replace the intermediate result. */
    template <typename from, typename proximal, typename convert, typename curr, typename... next>
        requires (union_proximal<from, curr>)
    struct _union_convert_from<from, proximal, convert, curr, next...> : _union_convert_from<
        from,
        union_replace_proximal<from, proximal, curr>,
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

    /* Recursive 3: no match at this index, discard curr. */
    template <typename from, typename proximal, typename convert, typename curr, typename... next>
        requires (!union_proximal<from, curr> && !union_convertible<from, curr, convert>)
    struct _union_convert_from<from, proximal, convert, curr, next...> :
        _union_convert_from<from, proximal, convert, next...>
    {};

    /* A simple visitor that backs the implicit constructor for a `Union<Ts...>`
    object, returning a corresponding `impl::basic_union` primitive type. */
    template <typename>
    struct union_convert_from {};
    template <typename... Ts>
    struct union_convert_from<meta::pack<Ts...>> {
        template <typename from>
        using type = _union_convert_from<from, void, void, Ts...>::type;
        template <typename from>
        static constexpr auto operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, type<from>>)
            requires (meta::not_void<type<from>>)
        {
            return impl::basic_union<meta::remove_rvalue<Ts>...>{
                bertrand::alternative<meta::index_of<type<from>, Ts...>>,
                std::forward<from>(arg)
            };
        }
    };

    /* A simple visitor that backs the explicit constructor for a `Union<Ts...>`
    object, returning a corresponding `impl::basic_union` primitive type.  Note that
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
        static constexpr auto operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<type<A...>, A...>)
            requires (meta::not_void<type<A...>>)
        {
            return impl::basic_union<meta::remove_rvalue<Ts>...>{
                bertrand::alternative<meta::index_of<type<A...>, Ts...>>,
                std::forward<A>(args)...
            };
        }
    };

    /* A simple vtable that backs the implicit conversion operator from `Union<Ts...>`
    to any type `T` to which all alternatives can be converted. */
    template <typename Self, typename to>
    struct union_convert_to {
        template <size_t I>
        struct fn {
            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{
                    std::forward<Self>(self).__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<to>;})
                requires (requires{{
                    std::forward<Self>(self).__value.template get<I>()
                } -> meta::convertible_to<to>;})
            {
                return std::forward<Self>(self).__value.template get<I>();
            }
        };

        using dispatch = impl::basic_vtable<fn, visitable<Self>::alternatives::size()>;

        [[nodiscard]] static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
        }
    };

    /* A special case of `union_convert_to` that corresponds to the result of the
    dereference and indirection operators for `Union<Ts...>`. */
    template <
        typename Self,
        typename = std::make_index_sequence<visitable<Self>::alternatives::size()>
    >
    struct union_flatten {};
    template <typename Self, size_t... Is>
        requires (meta::has_common_type<
            decltype((std::declval<Self>().__value.template get<Is>()))...
        >)
    struct union_flatten<Self, std::index_sequence<Is...>> : union_convert_to<
        Self,
        meta::common_type<decltype((std::declval<Self>().__value.template get<Is>()))...>
    > {};

    /* A simple vtable that backs the explicit conversion operator from `Union<Ts...>`
    to any type `T` to which all alternatives can be explicitly converted.  Only
    applies if `union_convert_to` would be malformed. */
    template <typename Self, typename to>
    struct union_cast_to {
        template <size_t I>
        struct fn {
            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{
                    static_cast<to>(std::forward<Self>(self).__value.template get<I>())
                } noexcept;})
                requires (requires{{
                    static_cast<to>(std::forward<Self>(self).__value.template get<I>())
                };})
            {
                return static_cast<to>(std::forward<Self>(self).__value.template get<I>());
            }
        };

        using dispatch = impl::basic_vtable<fn, visitable<Self>::alternatives::size()>;

        [[nodiscard]] static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
        }
    };

    /* Unions may be tuples if and only if all of their alternatives are tuples of the
    same size, in which case they can be indexed at compile time and destructured, with
    the elements possibly being promoted to unions if they are not all the same type. */
    template <
        typename Self,
        typename = std::make_index_sequence<visitable<Self>::alternatives::size()>
    >
    struct union_tuple { static constexpr bool enable = false; };
    template <typename Self, size_t I, size_t... Is>
        requires ((
            meta::tuple_like<decltype(std::declval<Self>().__value.template get<I>())> &&
            ... &&
            meta::tuple_like<decltype(std::declval<Self>().__value.template get<Is>())>
        ) && ((
            meta::tuple_size<decltype(std::declval<Self>().__value.template get<I>())> ==
            meta::tuple_size<decltype(std::declval<Self>().__value.template get<Is>())>
        ) && ...))
    struct union_tuple<Self, std::index_sequence<I, Is...>> {
        static constexpr bool enable = true;
        static constexpr size_t size =
            meta::tuple_size<decltype(std::declval<Self>().__value.template get<I>())>;

        template <ssize_t J>
        static constexpr size_t wrap = size_t(impl::normalize_index<size, J>());

        template <ssize_t J>
        using type = meta::make_union<
            decltype((meta::get<wrap<J>>(std::declval<Self>().__value.template get<I>()))),
            decltype((meta::get<wrap<J>>(std::declval<Self>().__value.template get<Is>())))...
        >;

        template <ssize_t J, ssize_t K>
        static constexpr type<J> get(meta::forward<Self> self)
            noexcept (requires{{
                meta::get<wrap<J>>(std::forward<Self>(self).__value.template get<K>())
            } noexcept -> meta::nothrow::convertible_to<type<J>>;})
            requires (requires{{
                meta::get<wrap<J>>(std::forward<Self>(self).__value.template get<K>())
            } -> meta::convertible_to<type<J>>;})
        {
            return meta::get<wrap<J>>(std::forward<Self>(self).__value.template get<K>());
        }
    };
    template <typename Self, ssize_t I>
        requires (union_tuple<Self>::enable && impl::valid_index<union_tuple<Self>::size, I>)
    struct union_get {
        using Union = union_tuple<Self>::template type<I>;

        template <size_t J>
        struct fn {
            static constexpr Union operator()(meta::forward<Self> self)
                noexcept (requires{{
                    union_tuple<meta::forward<Self>>::template get<I, J>(std::forward<Self>(self))
                } noexcept -> meta::nothrow::convertible_to<Union>;})
                requires (requires{{
                    union_tuple<meta::forward<Self>>::template get<I, J>(std::forward<Self>(self))
                } -> meta::convertible_to<Union>;})
            {
                return union_tuple<meta::forward<Self>>::template get<I, J>(std::forward<Self>(self));
            }
        };

        using dispatch = impl::basic_vtable<fn, visitable<Self>::alternatives::size()>;

        [[nodiscard]] static constexpr Union operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
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
            meta::has_common_type<meta::iterator_difference<Ts>...>
        )
    struct union_iterator {
        using types = meta::pack<Ts...>;
        using iterator_category = meta::common_type<meta::iterator_category<Ts>...>;
        using difference_type = meta::common_type<meta::iterator_difference<Ts>...>;
        using reference = union_iterator_ref<Ts...>;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::address_type<reference>;

        basic_union<Ts...> __value;

    private:
        template <size_t I>
        struct _deref {
            static constexpr reference operator()(const union_iterator& self)
                noexcept (requires{{
                    *self.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<reference>;})
                requires (requires{
                    {*self.__value.template get<I>()} -> meta::convertible_to<reference>;
                })
            {
                return *self.__value.template get<I>();
            }
        };
        using deref = impl::basic_vtable<_deref, sizeof...(Ts)>;

        template <size_t I>
        struct _subscript {
            static constexpr reference operator()(const union_iterator& self, difference_type n)
                noexcept (requires{{
                    self.__value.template get<I>()[n]
                } noexcept -> meta::nothrow::convertible_to<reference>;})
                requires (requires{
                    {self.__value.template get<I>()[n]} -> meta::convertible_to<reference>;
                })
            {
                return self.__value.template get<I>()[n];
            }
        };
        using subscript = impl::basic_vtable<_subscript, sizeof...(Ts)>;

        template <size_t I>
        struct _increment {
            static constexpr void operator()(union_iterator& self)
                noexcept (requires{{++self.__value.template get<I>()} noexcept;})
                requires (requires{{++self.__value.template get<I>()};})
            {
                ++self.__value.template get<I>();
            }
        };
        using increment = impl::basic_vtable<_increment, sizeof...(Ts)>;

        template <size_t I>
        struct _add {
            static constexpr union_iterator operator()(const union_iterator& self, difference_type n)
                noexcept (requires{{
                    union_iterator{{bertrand::alternative<I>, self.__value.template get<I>() + n}}
                } noexcept -> meta::nothrow::convertible_to<union_iterator>;})
                requires (requires{{
                    union_iterator{{bertrand::alternative<I>, self.__value.template get<I>() + n}}
                };})
            {
                return {{bertrand::alternative<I>, self.__value.template get<I>() + n}};
            }
            static constexpr union_iterator operator()(difference_type n, const union_iterator& self)
                noexcept (requires{{
                    union_iterator{{bertrand::alternative<I>, n + self.__value.template get<I>()}}
                } noexcept -> meta::nothrow::convertible_to<union_iterator>;})
                requires (requires{{
                    union_iterator{{bertrand::alternative<I>, n + self.__value.template get<I>()}}
                };})
            {
                return {{bertrand::alternative<I>, n + self.__value.template get<I>()}};
            }
        };
        using add = impl::basic_vtable<_add, sizeof...(Ts)>;

        template <size_t I>
        struct _iadd {
            static constexpr void operator()(union_iterator& self, difference_type n)
                noexcept (requires{{self.__value.template get<I>() += n} noexcept;})
                requires (requires{{self.__value.template get<I>() += n};})
            {
                self.__value.template get<I>() += n;
            }
        };
        using iadd = impl::basic_vtable<_iadd, sizeof...(Ts)>;

        template <size_t I>
        struct _decrement {
            static constexpr void operator()(union_iterator& self)
                noexcept (requires{{--self.__value.template get<I>()} noexcept;})
                requires (requires{{--self.__value.template get<I>()};})
            {
                --self.__value.template get<I>();
            }
        };
        using decrement = impl::basic_vtable<_decrement, sizeof...(Ts)>;

        template <size_t I>
        struct _subtract {
            static constexpr union_iterator operator()(const union_iterator& self, difference_type n)
                noexcept (requires{{
                    union_iterator{{bertrand::alternative<I>, self.__value.template get<I>() - n}}
                } noexcept -> meta::nothrow::convertible_to<union_iterator>;})
                requires (requires{{
                    union_iterator{{bertrand::alternative<I>, self.__value.template get<I>() - n}}
                };})
            {
                return {{bertrand::alternative<I>, self.__value.template get<I>() - n}};
            }
        };
        using subtract = impl::basic_vtable<_subtract, sizeof...(Ts)>;

        template <size_t I>
        struct _isub {
            static constexpr void operator()(union_iterator& self, difference_type n)
                noexcept (requires{{self.__value.template get<I>() -= n} noexcept;})
                requires (requires{{self.__value.template get<I>() -= n};})
            {
                self.__value.template get<I>() -= n;
            }
        };
        using isub = impl::basic_vtable<_isub, sizeof...(Ts)>;

        template <size_t I>
        struct _distance {
            template <typename other>
            static constexpr difference_type operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs.__value.template get<I>() - rhs
                } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                requires (requires{{
                    lhs.__value.template get<I>() - rhs
                } -> meta::convertible_to<difference_type>;})
            {
                return lhs.__value.template get<I>() - rhs;
            }

            template <typename other>
            static constexpr difference_type operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs - rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                requires (requires{{
                    lhs - rhs.__value.template get<I>()
                } -> meta::convertible_to<difference_type>;})
            {
                return lhs - rhs.__value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr difference_type operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() - rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                requires (requires{{
                    lhs.__value.template get<I>() - rhs.__value.template get<I>()
                } -> meta::convertible_to<difference_type>;})
            {
                return lhs.__value.template get<I>() - rhs.__value.template get<I>();
            }
        };
        using distance = impl::basic_vtable<_distance, sizeof...(Ts)>;

        template <size_t I>
        struct _less {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs.__value.template get<I>() < rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() < rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() < rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs < rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs < rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs < rhs.__value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() < rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() < rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() < rhs.__value.template get<I>();
            }
        };
        using less = impl::basic_vtable<_less, sizeof...(Ts)>;

        template <size_t I>
        struct _less_equal {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs.__value.template get<I>() <= rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() <= rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() <= rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs <= rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs <= rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs <= rhs.__value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() <= rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() <= rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() <= rhs.__value.template get<I>();
            }
        };
        using less_equal = impl::basic_vtable<_less_equal, sizeof...(Ts)>;

        template <size_t I>
        struct _equal {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs.__value.template get<I>() == rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() == rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() == rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs == rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs == rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs == rhs.__value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() == rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() == rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() == rhs.__value.template get<I>();
            }
        };
        using equal = impl::basic_vtable<_equal, sizeof...(Ts)>;

        template <size_t I>
        struct _unequal {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs.__value.template get<I>() != rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() != rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() != rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs != rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs != rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs != rhs.__value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() == rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() == rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() != rhs.__value.template get<I>();
            }
        };
        using unequal = impl::basic_vtable<_unequal, sizeof...(Ts)>;

        template <size_t I>
        struct _greater_equal {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs.__value.template get<I>() >= rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() >= rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() >= rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs >= rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs >= rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs >= rhs.__value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() >= rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() >= rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() >= rhs.__value.template get<I>();
            }
        };
        using greater_equal = impl::basic_vtable<_greater_equal, sizeof...(Ts)>;

        template <size_t I>
        struct _greater {
            template <typename other>
            static constexpr bool operator()(const union_iterator& lhs, const other& rhs)
                noexcept (requires{{
                    lhs.__value.template get<I>() > rhs
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() > rhs
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() > rhs;
            }

            template <typename other>
            static constexpr bool operator()(const other& lhs, const union_iterator& rhs)
                noexcept (requires{{
                    lhs > rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs > rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs > rhs.__value.template get<I>();
            }

            template <typename... Us> requires (sizeof...(Us) == sizeof...(Ts))
            static constexpr bool operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() > rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.__value.template get<I>() > rhs.__value.template get<I>()
                } -> meta::convertible_to<bool>;})
            {
                return lhs.__value.template get<I>() > rhs.__value.template get<I>();
            }
        };
        using greater = impl::basic_vtable<_greater, sizeof...(Ts)>;

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
                    lhs.__value.template get<I>() <=> rhs
                } noexcept -> meta::nothrow::convertible_to<forward_spaceship_type<other>>;})
            {
                return lhs.__value.template get<I>() <=> rhs;
            }

            template <typename other> requires (reverse_spaceship<other>)
            static constexpr reverse_spaceship_type<other> operator()(
                const other& lhs,
                const union_iterator& rhs
            )
                noexcept (requires{{
                    lhs <=> rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<reverse_spaceship_type<other>>;})
            {
                return lhs <=> rhs.__value.template get<I>();
            }

            template <typename... Us>
                requires (sizeof...(Us) == sizeof...(Ts) && forward_spaceship<union_iterator<Us...>>)
            static constexpr forward_spaceship_type<union_iterator<Us...>> operator()(
                const union_iterator& lhs,
                const union_iterator<Us...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() <=> rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<
                    forward_spaceship_type<union_iterator<Us...>>
                >;})
            {
                return lhs.__value.template get<I>() <=> rhs.__value.template get<I>();
            }
        };
        using spaceship = impl::basic_vtable<_spaceship, sizeof...(Ts)>;

    public:
        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{{deref{__value.index()}(*this)} noexcept;})
            requires (requires{{deref{__value.index()}(*this)};})
        {
            return deref{__value.index()}(*this);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const
            noexcept (requires{{subscript{__value.index()}(*this)} noexcept;})
            requires (requires{{subscript{__value.index()}(*this)};})
        {
            return subscript{__value.index()}(*this, n);
        }

        constexpr union_iterator& operator++()
            noexcept (requires{{increment{__value.index()}(*this)} noexcept;})
            requires (requires{{increment{__value.index()}(*this)};})
        {
            increment{__value.index()}(*this);
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
            noexcept (requires{{add{self.__value.index()}(self, n)} noexcept;})
            requires (requires{{add{self.__value.index()}(self, n)};})
        {
            return add{self.__value.index()}(self, n);
        }

        [[nodiscard]] friend constexpr union_iterator operator+(
            difference_type n,
            const union_iterator& self
        )
            noexcept (requires{{add{self.__value.index()}(n, self)} noexcept;})
            requires (requires{{add{self.__value.index()}(n, self)};})
        {
            return add{self.__value.index()}(n, self);
        }

        constexpr union_iterator& operator+=(difference_type n)
            noexcept (requires{{iadd{__value.index()}(*this, n)} noexcept;})
            requires (requires{{iadd{__value.index()}(*this, n)};})
        {
            iadd{__value.index()}(*this, n);
            return *this;
        }

        constexpr union_iterator& operator--()
            noexcept (requires{{decrement{__value.index()}(*this)} noexcept;})
            requires (requires{{decrement{__value.index()}(*this)};})
        {
            decrement{__value.index()}(*this);
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
            noexcept (requires{{subtract{self.__value.index()}(self, n)} noexcept;})
            requires (requires{{subtract{self.__value.index()}(self, n)};})
        {
            return subtract{self.__value.index()}(self, n);
        }

        template <typename other>
        [[nodiscard]] friend constexpr difference_type operator-(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{distance{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{distance{lhs.__value.index()}(lhs, rhs)};})
        {
            return distance{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr difference_type operator-(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{distance{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{distance{rhs.__value.index()}(lhs, rhs)};})
        {
            return distance{rhs.__value.index()}(lhs, rhs);
        }

        constexpr union_iterator& operator-=(difference_type n)
            noexcept (requires{{isub{__value.index()}(*this, n)} noexcept;})
            requires (requires{{isub{__value.index()}(*this, n)};})
        {
            isub{__value.index()}(*this, n);
            return *this;
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator<(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{less{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{less{lhs.__value.index()}(lhs, rhs)};})
        {
            return less{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator<(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{less{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{less{rhs.__value.index()}(lhs, rhs)};})
        {
            return less{rhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator<=(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{less_equal{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{less_equal{lhs.__value.index()}(lhs, rhs)};})
        {
            return less_equal{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator<=(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{less_equal{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{less_equal{rhs.__value.index()}(lhs, rhs)};})
        {
            return less_equal{rhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator==(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{equal{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{equal{lhs.__value.index()}(lhs, rhs)};})
        {
            return equal{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator==(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{equal{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{equal{rhs.__value.index()}(lhs, rhs)};})
        {
            return equal{rhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator!=(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{unequal{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{unequal{lhs.__value.index()}(lhs, rhs)};})
        {
            return unequal{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator!=(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{unequal{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{unequal{rhs.__value.index()}(lhs, rhs)};})
        {
            return unequal{rhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator>=(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{greater_equal{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{greater_equal{lhs.__value.index()}(lhs, rhs)};})
        {
            return greater_equal{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator>=(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{greater_equal{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{greater_equal{rhs.__value.index()}(lhs, rhs)};})
        {
            return greater_equal{rhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator>(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{greater{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{greater{lhs.__value.index()}(lhs, rhs)};})
        {
            return greater{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr bool operator>(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{greater{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{greater{rhs.__value.index()}(lhs, rhs)};})
        {
            return greater{rhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr decltype(auto) operator<=>(
            const union_iterator& lhs,
            const other& rhs
        )
            noexcept (requires{{spaceship{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{spaceship{lhs.__value.index()}(lhs, rhs)};})
        {
            return spaceship{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other>
        [[nodiscard]] friend constexpr decltype(auto) operator<=>(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{spaceship{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{spaceship{rhs.__value.index()}(lhs, rhs)};})
        {
            return spaceship{rhs.__value.index()}(lhs, rhs);
        }
    };

    /* `make_union_iterator<Ts...>` accepts a union of types `Ts...` and composes a set
    of iterators over it, which can be used to traverse the union, possibly yielding
    further unions.  If all types share the same iterator type, then the iterator will
    be returned directly.  Otherwise, a `union_iterator<Iters...>` will be returned,
    where `Iters...` are the (index-aligned) iterator types that were detected. */
    template <size_t, typename, typename, typename, typename, typename>
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
            requires (meta::has_size<decltype(std::declval<U>().__value.template get<Is>())> && ...)
        struct _size_type<std::index_sequence<Is...>> {
            using type = meta::common_type<
                meta::size_type<decltype(std::declval<U>().__value.template get<Is>())>...
            >;
        };

        template <typename = std::make_index_sequence<N>>
        struct _ssize_type { using type = void; };
        template <size_t... Is>
            requires (meta::has_ssize<decltype(std::declval<U>().__value.template get<Is>())> && ...)
        struct _ssize_type<std::index_sequence<Is...>> {
            using type = meta::common_type<
                meta::ssize_type<decltype(std::declval<U>().__value.template get<Is>())>...
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
        template <size_t I>
        struct begin_fn {
            static constexpr begin_type operator()(U u)
                noexcept (requires{{
                    meta::begin(u.__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<begin_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    meta::begin(u.__value.template get<I>())
                } -> meta::convertible_to<begin_type>;})
            {
                return meta::begin(u.__value.template get<I>());
            }
            static constexpr begin_type operator()(U u)
                noexcept (requires{{begin_type{
                    {bertrand::alternative<I>, meta::begin(u.__value.template get<I>())}
                }} noexcept;})
                requires (sizeof...(B) == N && !iter<B...>::direct && requires{{begin_type{
                    {bertrand::alternative<I>, meta::begin(u.__value.template get<I>())}
                }};})
            {
                return {{bertrand::alternative<I>, meta::begin(u.__value.template get<I>())}};
            }
        };
        using _begin = impl::basic_vtable<begin_fn, N>;

        template <size_t I>
        struct end_fn {
            static constexpr end_type operator()(U u)
                noexcept (requires{{
                    meta::end(u.__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<end_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    meta::end(u.__value.template get<I>())
                } -> meta::convertible_to<end_type>;})
            {
                return meta::end(u.__value.template get<I>());
            }
            static constexpr end_type operator()(U u)
                noexcept (requires{{end_type{
                    {bertrand::alternative<I>, meta::end(u.__value.template get<I>())}
                }} noexcept;})
                requires (sizeof...(B) == N && !iter<B...>::direct && requires{{end_type{
                    {bertrand::alternative<I>, meta::end(u.__value.template get<I>())}
                }};})
            {
                return {{bertrand::alternative<I>, meta::end(u.__value.template get<I>())}};
            }
        };
        using _end = impl::basic_vtable<end_fn, N>;

        template <size_t I>
        struct rbegin_fn {
            static constexpr rbegin_type operator()(U u)
                noexcept (requires{{
                    meta::rbegin(u.__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<rbegin_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    meta::rbegin(u.__value.template get<I>())
                } -> meta::convertible_to<rbegin_type>;})
            {
                return meta::rbegin(u.__value.template get<I>());
            }
            static constexpr rbegin_type operator()(U u)
                noexcept (requires{{rbegin_type{
                    {bertrand::alternative<I>, meta::rbegin(u.__value.template get<I>())}
                }} noexcept;})
                requires (sizeof...(B) == N && !iter<B...>::direct && requires{{rbegin_type{
                    {bertrand::alternative<I>, meta::rbegin(u.__value.template get<I>())}
                }};})
            {
                return {{
                    bertrand::alternative<I>,
                    meta::rbegin(u.__value.template get<I>())
                }};
            }
        };
        using _rbegin = impl::basic_vtable<rbegin_fn, N>;

        template <size_t I>
        struct rend_fn {
            static constexpr rend_type operator()(U u)
                noexcept (requires{{
                    meta::rend(u.__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<rend_type>;})
                requires (sizeof...(B) == N && iter<B...>::direct && requires{{
                    meta::rend(u.__value.template get<I>())
                } -> meta::convertible_to<rend_type>;})
            {
                return meta::rend(u.__value.template get<I>());
            }
            static constexpr rend_type operator()(U u)
                noexcept (requires{{rend_type{
                    {bertrand::alternative<I>, meta::rend(u.__value.template get<I>())}
                }} noexcept;})
                requires (sizeof...(B) == N && !iter<B...>::direct && requires{{rend_type{
                    {bertrand::alternative<I>, meta::rend(u.__value.template get<I>())}
                }};})
            {
                return {{
                    bertrand::alternative<I>,
                    meta::rend(u.__value.template get<I>())
                }};
            }
        };
        using _rend = impl::basic_vtable<rend_fn, N>;

        template <size_t I>
        struct size_fn {
            static constexpr size_type operator()(U u)
                noexcept (requires{{
                    meta::size(u.__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<size_type>;})
                requires (requires{{
                    meta::size(u.__value.template get<I>())
                } -> meta::convertible_to<size_type>;})
            {
                return meta::size(u.__value.template get<I>());
            }
        };
        using _size = impl::basic_vtable<size_fn, N>;

        template <size_t I>
        struct ssize_fn {
            static constexpr ssize_type operator()(U u)
                noexcept (requires{{
                    meta::ssize(u.__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<ssize_type>;})
                requires (requires{{
                    meta::ssize(u.__value.template get<I>())
                } -> meta::convertible_to<ssize_type>;})
            {
                return meta::ssize(u.__value.template get<I>());
            }
        };
        using _ssize = impl::basic_vtable<ssize_fn, N>;

        template <size_t I>
        struct empty_fn {
            static constexpr bool operator()(U u)
                noexcept (requires{{
                    std::ranges::empty(u.__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    std::ranges::empty(u.__value.template get<I>())
                } -> meta::convertible_to<bool>;})
            {
                return std::ranges::empty(u.__value.template get<I>());
            }
        };
        using _empty = impl::basic_vtable<empty_fn, N>;

    public:
        static constexpr begin_type begin(U u)
            noexcept (requires{{_begin{u.__value.index()}(u)} noexcept;})
            requires (requires{{_begin{u.__value.index()}(u)};})
        {
            return _begin{u.__value.index()}(u);
        }

        static constexpr end_type end(U u)
            noexcept (requires{{_end{u.__value.index()}(u)} noexcept;})
            requires (requires{{_end{u.__value.index()}(u)};})
        {
            return _end{u.__value.index()}(u);
        }

        static constexpr rbegin_type rbegin(U u)
            noexcept (requires{{_rbegin{u.__value.index()}(u)} noexcept;})
            requires (requires{{_rbegin{u.__value.index()}(u)};})
        {
            return _rbegin{u.__value.index()}(u);
        }

        static constexpr rend_type rend(U u)
            noexcept (requires{{_rend{u.__value.index()}(u)} noexcept;})
            requires (requires{{_rend{u.__value.index()}(u)};})
        {
            return _rend{u.__value.index()}(u);
        }

        static constexpr size_type size(U u)
            noexcept (requires{{_size{u.__value.index()}(u)} noexcept;})
            requires (requires{{_size{u.__value.index()}(u)};})
        {
            return _size{u.__value.index()}(u);
        }

        static constexpr ssize_type ssize(U u)
            noexcept (requires{{_ssize{u.__value.index()}(u)} noexcept;})
            requires (requires{{_ssize{u.__value.index()}(u)};})
        {
            return _ssize{u.__value.index()}(u);
        }

        static constexpr bool empty(U u)
            noexcept (requires{{_empty{u.__value.index()}(u)} noexcept;})
            requires (requires{{_empty{u.__value.index()}(u)};})
        {
            return _empty{u.__value.index()}(u);
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
        I < visitable<U>::alternatives::size() &&
        meta::iterable<decltype(std::declval<U>().__value.template get<I>())> &&
        meta::reverse_iterable<decltype(std::declval<U>().__value.template get<I>())>
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
            decltype(std::declval<U>().__value.template get<I>())
        >>>,
        meta::pack<end..., meta::unqualify<meta::end_type<
            decltype(std::declval<U>().__value.template get<I>())
        >>>,
        meta::pack<rbegin..., meta::unqualify<meta::rbegin_type<
            decltype(std::declval<U>().__value.template get<I>())
        >>>,
        meta::pack<rend..., meta::unqualify<meta::rend_type<
            decltype(std::declval<U>().__value.template get<I>())
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
        I < visitable<U>::alternatives::size() &&
        meta::iterable<decltype(std::declval<U>().__value.template get<I>())> &&
        !meta::reverse_iterable<decltype(std::declval<U>().__value.template get<I>())>
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
            decltype(std::declval<U>().__value.template get<I>())
        >>>,
        meta::pack<end..., meta::unqualify<meta::end_type<
            decltype(std::declval<U>().__value.template get<I>())
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
        I < visitable<U>::alternatives::size() &&
        !meta::iterable<decltype(std::declval<U>().__value.template get<I>())> &&
        meta::reverse_iterable<decltype(std::declval<U>().__value.template get<I>())>
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
            decltype(std::declval<U>().__value.template get<I>())
        >>>,
        meta::pack<rend..., meta::unqualify<meta::rend_type<
            decltype(std::declval<U>().__value.template get<I>())
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
        I < visitable<U>::alternatives::size() &&
        !meta::iterable<decltype(std::declval<U>().__value.template get<I>())> &&
        !meta::reverse_iterable<decltype(std::declval<U>().__value.template get<I>())>
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


/* A type-safe union capable of storing two or more arbitrarily-qualified types.

This is similar to `std::variant<Ts...>`, but with the following changes:

    1.  `Ts...` may have cvref qualifications, allowing the union to model references
        and other cv-qualified types without requiring an extra copy or
        `std::reference_wrapper` workaround.  Note that cvref qualifications may also
        be forwarded from the union itself when accessed, and no attempt is made to
        extend the lifetime of referenced objects, so all of the usual guidelines for
        references still apply.
    2.  The union can never be in an invalid state, meaning the index is always within
        range and points to a valid alternative of the union.
    3.  The constructor is more precise than `std::variant`, preferring exact matches
        if possible, then the most proximal cvref-qualified alternative or base class,
        with implicit conversions being considered only as a last resort.  If an
        implicit conversion is selected, then it will always be the leftmost match in
        the template signature.  The same is true for the default constructor, which
        always constructs the leftmost default-constructible alternative.  A specific
        alternative can be constructed by providing an `alternative<I>` or `type<T>`
        tag as the first argument, whereby all other arguments will be forwarded to the
        constructor of the selected alternative.
    4.  All operators and members will be forwarded to the alternatives in monadic
        fashion, assuming they all support them.  The only exception is `.__value`,
        which provides access to the (unsafe) union internals, and is prefixed by
        double underscores to avoid conflicts.  This maximizes the surface area for
        automatically-generated member methods, which can be emitted via static
        reflection.
    5.  `get<I>()`, and `get<T>()` are reserved for the forwarding interface, allowing
        unions to model tuple-like behavior as long as all of their alternatives are
        tuple-like and have the same size.  The results might be further unions, if
        indexing via the tuple protocol results in multiple types.  If you need unsafe
        access to the union types, then you can directly access the `.__value` member
        instead, which obeys the same rules as `std::variant<Ts...>`.
    6.  Basic pattern matching is supported using the `->*` operator, which takes a
        visitor function that is invocable with a single argument representing each
        alternative.  If any of the alternatives are nested monads, then the `->*`
        operator will recursively apply over them, similar to `->`.  This may cause
        product types to unpack to more or less than one argument, simulating a
        structured binding, which is applied after unwrapping the outer union.
    7.  Pointer-like dereference operators are supplied if and only if all alternatives
        in the union have a common type, after accounting for the cvref qualifiers of
        the union itself.  `*` returns that type directly, while `->` returns a proxy
        that extends its lifetime for the duration of the access.
    8.  Iterating over the union is possible, provided each of the alternatives are
        iterable.  If so, and all types return the same iterator, then the result will
        simply be that iterator, without any extra indirection.  Otherwise, if the
        alternatives return multiple iterator types, then the result will be a wrapper
        which acts like a union of the possible iterators, and retains as much shared
        functionality as possible.  If all iterators dereference to the same type, then
        so will the union iterator.  If they dereference to multiple types, then the
        union iterator will dereference to a `bertrand::Union<Us...>`, where `Us...`
        represent the dereferenced types of each iterator.
 */
template <typename... Ts> requires (impl::union_concept<Ts...>)
struct Union : impl::union_tag {
    impl::basic_union<meta::remove_rvalue<Ts>...> __value;

    /* Default constructor finds the first type in `Ts...` that can be default
    constructed.  If no such type exists, then the default constructor is disabled. */
    [[nodiscard]] constexpr Union()
        noexcept (meta::nothrow::default_constructible<
            impl::basic_union<meta::remove_rvalue<Ts>...>
        >)
        requires (impl::basic_union<meta::remove_rvalue<Ts>...>::default_constructible)
    :
        __value()
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
        noexcept (meta::nothrow::visit_exhaustive<impl::union_convert_from<meta::pack<Ts...>>, from>)
        requires (meta::visit_exhaustive<impl::union_convert_from<meta::pack<Ts...>>, from>)
    :
        __value(impl::visit(
            impl::union_convert_from<meta::pack<Ts...>>{},
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
        noexcept (meta::nothrow::visit_exhaustive<impl::union_construct_from<Ts...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<impl::union_convert_from<meta::pack<Ts...>>, A...> &&
            meta::visit_exhaustive<impl::union_construct_from<Ts...>, A...>
        )
    :
        __value(impl::visit(impl::union_construct_from<Ts...>{}, std::forward<A>(args)...))
    {}

    /* Explicitly construct a union with the alternative at index `I` using the
    provided arguments.  This is more explicit than using the standard constructors,
    for cases where only a specific alternative should be considered. */
    template <size_t I, typename... A> requires (I < sizeof...(Ts))
    [[nodiscard]] explicit constexpr Union(std::in_place_index_t<I> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, Ts...>, A...>)
        requires (meta::constructible_from<meta::unpack_type<I, Ts...>, A...>)
    :
        __value{tag, std::forward<A>(args)...}
    {}

    /* Explicitly construct a union with the specified alternative using the given
    arguments.  This is more explicit than using the standard constructors, for cases
    where only a specific alternative should be considered. */
    template <typename T, typename... A> requires (meta::pack<Ts...>::template contains<T>())
    [[nodiscard]] explicit constexpr Union(std::type_identity<T> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<T, A...>)
        requires (meta::constructible_from<T, A...>)
    :
        __value{bertrand::alternative<meta::index_of<T, Ts...>>, std::forward<A>(args)...}
    {}

    /* Swap the contents of two unions as efficiently as possible.  This will use
    swap operators for the wrapped alternatives if possible, otherwise falling back to
    a 3-way move using a temporary of the same type. */
    constexpr void swap(Union& other)
        noexcept (requires{{__value.swap(other.__value)} noexcept;})
        requires (requires{{__value.swap(other.__value)};})
    {
        __value.swap(other.__value);
    }

    /* Implicit conversion operator allows conversions toward any type to which all
    alternatives can be exhaustively converted.  This allows conversion to scalar types
    as well as union types (regardless of source) that satisfy the conversion
    criteria. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr operator to(this Self&& self)
        noexcept (requires{{impl::union_convert_to<Self, to>{}(std::forward<Self>(self))} noexcept;})
        requires (
            !meta::prefer_constructor<to> &&
            requires{{impl::union_convert_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::union_convert_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Explicit conversion operator allows functional-style conversions toward any type
    to which all alternatives can be explicitly converted.  This allows conversion to
    scalar types as well as union types (regardless of source) that satisfy the
    conversion criteria, and will only be considered if an implicit conversion would
    be malformed. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr explicit operator to(this Self&& self)
        noexcept (requires{{impl::union_cast_to<Self, to>{}(std::forward<Self>(self))} noexcept;})
        requires (
            !meta::prefer_constructor<to> &&
            !requires{{impl::union_convert_to<Self, to>{}(std::forward<Self>(self))};} &&
            requires{{impl::union_cast_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::union_cast_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Flatten the union into a common type, assuming one exists.  Fails to compile if
    no common type can be found.  Note that the contents will be perfectly forwarded
    according to their storage qualifiers as well as those of the union itself. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
        noexcept (requires{{impl::union_flatten<Self>{}(std::forward<Self>(self))} noexcept;})
        requires (requires{{impl::union_flatten<Self>{}(std::forward<Self>(self))};})
    {
        return (impl::union_flatten<Self>{}(std::forward<Self>(self)));
    }

    /* Indirectly access a member of the flattened union type, assuming one exists.
    Fails to compile if no common type can be found.  Note that the contents will be
    perfectly forwarded according to their storage qualifiers as well as those of the
    union itself. */
    template <typename Self>
    [[nodiscard]] constexpr auto operator->(this Self&& self)
        noexcept (requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
        requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
    {
        return impl::arrow{*std::forward<Self>(self)};
    }

    /* Forward tuple access to `Ts...`, assuming they all support it and have the same
    size.  If so, then the return type may be a further union if the indexed types
    differ. */
    template <ssize_t I, typename Self>
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{impl::union_get<Self, I>{}(std::forward<Self>(self))} noexcept;})
        requires (
            impl::union_tuple<Self>::enable &&
            impl::valid_index<impl::union_tuple<Self>::size, I> &&
            requires{{impl::union_get<Self, I>{}(std::forward<Self>(self))};}
        )
    {
        return (impl::union_get<Self, I>{}(std::forward<Self>(self)));
    }

    /* Returns the result of `meta::size()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) size(this Self&& self)
        noexcept (requires{{impl::make_union_iterator<Self&>::size(self)} noexcept;})
        requires (requires{{impl::make_union_iterator<Self&>::size(self)};})
    {
        return (impl::make_union_iterator<Self&>::size(self));
    }

    /* Returns the result of `meta::ssize()` on the current alternative if it is
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

    template <typename Self, typename... A>
    constexpr decltype(auto) operator()(this Self&& self, A&&... args)
        noexcept (meta::nothrow::force_visit<1, impl::Call, Self, A...>)
        requires (meta::force_visit<1, impl::Call, Self, A...>)
    {
        return (impl::visit<1>(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<A>(args)...
        ));
    }

    template <typename Self, typename... K>
    constexpr decltype(auto) operator[](this Self&& self, K&&... keys)
        noexcept (meta::nothrow::force_visit<1, impl::Subscript, Self, K...>)
        requires (meta::force_visit<1, impl::Subscript, Self, K...>)
    {
        return (impl::visit<1>(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<K>(keys)...
        ));
    }
};


////////////////////////
////    OPTIONAL    ////
////////////////////////


/// TODO: optionals and expecteds can conditionally expose `data()`, returning a
/// null pointer if empty.


namespace impl {

    /* Return a standardized error if an optional is dereferenced while in the empty
    state.  Note that these checks will be optimized out in release builds. */
    template <typename curr> requires (DEBUG)
    constexpr TypeError bad_optional_access() noexcept {
        static constexpr static_str msg =
            "'" + demangle<curr>() + "' is not the active type in the optional "
            "(active is 'NoneType')";
        return TypeError(msg);
    }

    /* A special case of `basic_union` for optional references, which encode the
    reference as a raw pointer, with null representing the empty state.  This removes
    the need for an additional tracking index. */
    template <meta::None none, meta::lvalue ref> requires (meta::has_address<ref>)
    struct basic_union<none, ref> {
        using types = meta::pack<none, ref>;
        using default_type = none;
        using ptr = meta::address_type<ref>;

        template <size_t I> requires (I < 2)
        using tag = std::in_place_index_t<I>;

        static constexpr size_t size() noexcept { return 2; }
        static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        static constexpr bool empty() noexcept { return false; }

        [[no_unique_address]] ptr m_data;

        /* Default constructor always initializes to the empty state. */
        [[nodiscard]] constexpr basic_union(tag<0> = tag<0>{}) noexcept : m_data(nullptr) {};

        /* Tagged constructor specifically initializes the alternative at index `I`
        with the given arguments. */
        [[nodiscard]] explicit constexpr basic_union(tag<1>, ref r)
            noexcept (meta::nothrow::has_address<ref>)
        :
            m_data(std::addressof(r))
        {}

        /* Special constructor that takes a pointer directly, enabling direct
        conversions. */
        [[nodiscard]] explicit constexpr basic_union(ptr p) noexcept : m_data(p) {}

        /* Swap the contents of two unions as efficiently as possible. */
        constexpr void swap(basic_union& other) noexcept {
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
    };

    /* A simple visitor that backs the implicit constructor for an `Optional<T>`
    object, returning a corresponding `impl::basic_union` primitive type. */
    template <typename T, typename...>
    struct optional_convert_from {};
    template <typename T, typename in>
    struct optional_convert_from<T, in> {
        using type = meta::remove_rvalue<T>;
        using empty = impl::visitable<in>::empty;
        using result = impl::basic_union<NoneType, type>;

        // 1) prefer direct conversion to `T` if possible
        template <typename alt>
        static constexpr result operator()(alt&& v)
            noexcept (meta::nothrow::convertible_to<alt, type>)
            requires (meta::convertible_to<alt, type>)
        {
            return result{bertrand::alternative<1>, std::forward<alt>(v)};
        }

        // 2) otherwise, if the argument is in an empty state as defined by the input's
        // `impl::visitable` specification, or is given as a none sentinel, then we
        // return it as an empty `basic_union` object. 
        template <typename alt>
        static constexpr result operator()(alt&&)
            noexcept (meta::nothrow::default_constructible<impl::basic_union<NoneType, type>>)
            requires (
                !meta::convertible_to<alt, type> &&
                (meta::is<alt, empty> || meta::None<alt> || meta::is<alt, std::nullopt_t>)
            )
        {
            return result{};
        }

        // 3) if `type` is an lvalue, then an extra conversion is enabled from raw
        // pointers, where nullptr gets translated into an empty `basic_union`
        // object, exploiting the pointer optimization.
        template <typename alt>
        static constexpr result operator()(alt&& p)
            noexcept (meta::nothrow::convertible_to<alt, meta::address_type<type>>)
            requires (
                !meta::convertible_to<alt, type> &&
                !(meta::is<alt, empty> || meta::None<alt> || meta::is<alt, std::nullopt_t>) &&
                meta::lvalue<type> &&
                meta::has_address<type> &&
                meta::convertible_to<alt, meta::address_type<type>>
            )
        {
            return result{p};
        }
    };
    template <typename T, typename in> requires (meta::is_void<T> || meta::None<T>)
    struct optional_convert_from<T, in> {
        using empty = impl::visitable<in>::empty;
        using result = impl::basic_union<NoneType>;

        // 1) prefer direct conversion to `NoneType` if possible
        template <typename alt>
        static constexpr result operator()(alt&& v)
            noexcept (meta::nothrow::convertible_to<alt, NoneType>)
            requires (meta::convertible_to<alt, NoneType>)
        {
            return result{bertrand::alternative<0>, std::forward<alt>(v)};
        }

        // 2) otherwise, if the argument is in an empty state as defined by the input's
        // `impl::visitable` specification, then we retain that state.
        template <typename alt>
        static constexpr result operator()(alt&&)
            noexcept (meta::nothrow::default_constructible<impl::basic_union<NoneType>>)
            requires (
                !meta::convertible_to<alt, NoneType> &&
                meta::is<alt, empty>
            )
        {
            return result{};
        }
    };

    /* A simple visitor that backs the explicit constructor for an `Optional<T>`
    object, returning a corresponding `impl::basic_union` primitive type.  Note that
    this only applies if `optional_convert_from` is invalid. */
    template <typename T>
    struct optional_construct_from {
        using type = meta::remove_rvalue<T>;
        template <typename... A>
        static constexpr auto operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<type, A...>)
            requires (meta::constructible_from<type, A...>)
        {
            return impl::basic_union<NoneType, type>{
                bertrand::alternative<1>,
                std::forward<A>(args)...
            };
        }
    };
    template <typename T> requires (meta::is_void<T> || meta::None<T>)
    struct optional_construct_from<T> {
        template <typename... A>
        static constexpr auto operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<NoneType, A...>)
            requires (meta::constructible_from<NoneType, A...>)
        {
            return impl::basic_union<NoneType>{
                bertrand::alternative<0>,
                std::forward<A>(args)...
            };
        }
    };

    /* A simple visitor that backs the implicit conversion operator from `Optional<T>`,
    which attempts a normal visitor conversion where possible, falling back to a
    conversion from `std::nullopt` or `nullptr` to cover all STL types and raw
    pointers in the case of optional lvalues. */
    template <typename Self, typename to>
    struct optional_convert_to {
        static constexpr bool from_none = requires{{bertrand::None} -> meta::convertible_to<to>;};
        static constexpr bool from_nullopt = requires{{std::nullopt} -> meta::convertible_to<to>;};
        static constexpr bool from_nullptr =
            meta::lvalue<typename impl::visitable<meta::unqualify<Self>>::value> &&
            requires(meta::forward<Self> self) {
                {nullptr} -> meta::convertible_to<to>;
                {
                    std::addressof(std::forward<Self>(self).__value.template get<1>())
                } -> meta::convertible_to<to>;
            };

        template <size_t I>
        struct fn {
            static constexpr bool convert = requires(meta::forward<Self> self) {{
                std::forward<Self>(self).__value.template get<I>()
            } -> meta::convertible_to<to>;};

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{
                    std::forward<Self>(self).__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<to>;})
                requires (convert)
            {
                return std::forward<Self>(self).__value.template get<I>();
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{bertrand::None} -> meta::nothrow::convertible_to<to>;})
                requires (!convert && I == 0 && from_none)
            {
                return bertrand::None;
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{std::nullopt} -> meta::nothrow::convertible_to<to>;})
                requires (!convert && I == 0 && !from_none && from_nullopt)
            {
                return std::nullopt;
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{
                    {nullptr} -> meta::nothrow::convertible_to<to>;
                    {
                        std::addressof(std::forward<Self>(self).__value.template get<1>())
                    } -> meta::nothrow::convertible_to<to>;
                })
                requires (!convert && !from_none && !from_nullopt && from_nullptr)
            {
                if constexpr (I == 0) {
                    return nullptr;
                } else {
                    return std::addressof(std::forward<Self>(self).__value.template get<1>());
                }
            }
        };

        using dispatch = impl::basic_vtable<fn, 2>;

        [[nodiscard]] static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
        }
    };
    template <typename Self, typename to> requires (visitable<Self>::alternatives::size() == 1)
    struct optional_convert_to<Self, to> {
        static constexpr bool convert = requires(meta::forward<Self> self) {{
            std::forward<Self>(self).__value.template get<0>()
        } -> meta::convertible_to<to>;};
        static constexpr bool from_nullopt = requires{{std::nullopt} -> meta::convertible_to<to>;};
        static constexpr bool from_nullptr = requires{{nullptr} -> meta::convertible_to<to>;};

        static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{
                std::forward<Self>(self).__value.template get<0>()
            } noexcept -> meta::nothrow::convertible_to<to>;})
            requires (convert)
        {
            return std::forward<Self>(self).__value.template get<0>();
        }

        static constexpr to operator()(meta::forward<Self> self)
            noexcept (meta::nothrow::convertible_to<const std::nullopt_t&, to>)
            requires (!convert && from_nullopt)
        {
            return std::nullopt;
        }

        static constexpr to operator()(meta::forward<Self> self)
            noexcept (meta::nothrow::convertible_to<std::nullptr_t, to>)
            requires (!convert && !from_nullopt && from_nullptr)
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
        static constexpr bool from_none = requires{{static_cast<to>(bertrand::None)};};
        static constexpr bool from_nullopt = requires{{static_cast<to>(std::nullopt)};};
        static constexpr bool from_nullptr =
            meta::lvalue<typename impl::visitable<meta::unqualify<Self>>::value> &&
            requires(meta::forward<Self> self) {
                {static_cast<to>(nullptr)} -> meta::convertible_to<to>;
                {static_cast<to>(
                    std::addressof(std::forward<Self>(self).__value.template get<1>())
                )};
            };

        template <size_t I>
        struct fn {
            static constexpr bool convert = requires(meta::forward<Self> self) {{
                static_cast<to>(std::forward<Self>(self).__value.template get<I>())
            };};

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{
                    static_cast<to>(std::forward<Self>(self).__value.template get<I>())
                } noexcept;})
                requires (convert)
            {
                return static_cast<to>(std::forward<Self>(self).__value.template get<I>());
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{static_cast<to>(bertrand::None)} noexcept;})
                requires (!convert && I == 0 && from_none)
            {
                return static_cast<to>(bertrand::None);
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{static_cast<to>(std::nullopt)} noexcept;})
                requires (!convert && I == 0 && !from_none && from_nullopt)
            {
                return static_cast<to>(std::nullopt);
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{
                    {static_cast<to>(nullptr)} noexcept;
                    {static_cast<to>(
                        std::addressof(std::forward<Self>(self).__value.template get<1>())
                    )} noexcept;
                })
                requires (!convert && !from_none && !from_nullopt && from_nullptr)
            {
                if constexpr (I == 0) {
                    return static_cast<to>(nullptr);
                } else {
                    return static_cast<to>(
                        std::addressof(std::forward<Self>(self).__value.template get<1>())
                    );
                }
            }
        };

        using dispatch = impl::basic_vtable<fn, 2>;

        [[nodiscard]] static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
        }
    };
    template <typename Self, typename to> requires (visitable<Self>::alternatives::size() == 1)
    struct optional_cast_to<Self, to> {
        static constexpr bool from_none =
            meta::explicitly_convertible_to<const bertrand::NoneType&, to>;
        static constexpr bool from_nullopt =
            meta::explicitly_convertible_to<const std::nullopt_t&, to>;
        static constexpr bool from_nullptr =
            meta::lvalue<typename impl::visitable<meta::unqualify<Self>>::value> &&
            meta::explicitly_convertible_to<std::nullptr_t, to>;
        static constexpr bool convert = requires(meta::forward<Self> self) {{
            static_cast<to>(std::forward<Self>(self).__value.template get<0>())
        };};

        static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{
                static_cast<to>(std::forward<Self>(self).__value.template get<0>())
            } noexcept;})
            requires (convert)
        {
            return static_cast<to>(std::forward<Self>(self).__value.template get<0>());
        }

        static constexpr to operator()(meta::forward<Self> self)
            noexcept (meta::nothrow::explicitly_convertible_to<const std::nullopt_t&, to>)
            requires (!convert && !from_none && from_nullopt)
        {
            return static_cast<to>(std::nullopt);
        }

        static constexpr to operator()(meta::forward<Self> self)
            noexcept (meta::nothrow::explicitly_convertible_to<std::nullptr_t, to>)
            requires (!convert && !from_none && !from_nullopt && from_nullptr)
        {
            return static_cast<to>(nullptr);
        }
    };

    /* A simple visitor that backs the tuple indexing operator for `Optional<T>`, where
    `T` is tuple-like, and the return type is promoted to an optional. */
    template <typename Self, ssize_t I>
        requires (
            visitable<Self>::alternatives::size() == 2 &&
            meta::tuple_like<typename visitable<Self>::value> &&
            impl::valid_index<meta::tuple_size<typename visitable<Self>::value>, I>
        )
    struct optional_get {
        using value = visitable<Self>::value;
        static constexpr size_t idx = size_t(impl::normalize_index<meta::tuple_size<value>, I>());
        using Optional = bertrand::Optional<
            meta::remove_rvalue<decltype((meta::get<idx>(std::declval<value>())))>
        >;

        template <size_t J>
        struct fn {
            static constexpr Optional operator()(meta::forward<Self> self)
                noexcept (requires{{meta::get<idx>(
                    visitable<Self>::template get<J>(std::forward<Self>(self))
                )} noexcept -> meta::nothrow::convertible_to<Optional>;})
                requires (requires{{meta::get<idx>(
                    visitable<Self>::template get<J>(std::forward<Self>(self))
                )} -> meta::convertible_to<Optional>;})
            {
                return meta::get<idx>(
                    visitable<Self>::template get<J>(std::forward<Self>(self))
                );
            }
        };
        template <>
        struct fn<0> {
            static constexpr Optional operator()(meta::forward<Self> self)
                noexcept (meta::nothrow::default_constructible<Optional>)
                requires (meta::default_constructible<Optional>)
            {
                return {};
            }
        };

        using dispatch = impl::basic_vtable<fn, 2>;

        [[nodiscard]] static constexpr Optional operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
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
            noexcept (requires{{storage{other.__value.iter}} noexcept;})
            requires (requires{{storage{other.__value.iter}};})
        {
            if (other.initialized) {
                return {other.__value.iter};
            } else {
                return {};
            }
        }

        static constexpr storage move(optional_iterator&& other)
            noexcept (requires{{storage{std::move(other).__value.iter}} noexcept;})
            requires (requires{{storage{std::move(other).__value.iter}};})
        {
            if (other.initialized) {
                return {std::move(other).__value.iter};
            } else {
                return {};
            }
        }

    public:
        using iterator_category = meta::iterator_category<T>;
        using difference_type = meta::iterator_difference<T>;
        using value_type = meta::iterator_value<T>;
        using reference = meta::iterator_reference<T>;
        using pointer = meta::iterator_pointer<T>;

        [[no_unique_address]] storage __value;
        [[no_unique_address]] bool initialized;

        [[nodiscard]] constexpr optional_iterator() noexcept : initialized(false) {}

        [[nodiscard]] constexpr optional_iterator(const T& it)
            noexcept (meta::nothrow::copyable<T>)
            requires (meta::copyable<T>)
        :
            __value(it),
            initialized(true)
        {}

        [[nodiscard]] constexpr optional_iterator(T&& it)
            noexcept (meta::nothrow::movable<T>)
            requires (meta::movable<T>)
        :
            __value(std::move(it)),
            initialized(true)
        {}

        [[nodiscard]] constexpr optional_iterator(const optional_iterator& other)
            noexcept (meta::nothrow::copyable<T>)
            requires (meta::copyable<T>)
        :
            __value(copy(other)),
            initialized(other.initialized)
        {}

        [[nodiscard]] constexpr optional_iterator(optional_iterator&& other)
            noexcept (meta::nothrow::movable<T>)
            requires (meta::movable<T>)
        :
            __value(move(std::move(other))),
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
                    __value.iter = other.__value.iter;
                }
            } else if (initialized) {
                std::destroy_at(&__value.iter);
                initialized = false;
            } else if (other.initialized) {
                std::construct_at(&__value.iter, other.__value.iter);
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
                    __value.iter = std::move(other).__value.iter;
                    other.initialized = false;
                }
            } else if (initialized) {
                std::destroy_at(&__value.iter);
                initialized = false;
            } else if (other.initialized) {
                std::construct_at(&__value.iter, std::move(other).__value.iter);
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
                std::destroy_at(&__value.iter);
            }
            initialized = false;
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).__value.iter} noexcept;})
            requires (requires{{*std::forward<Self>(self).__value.iter};})
        {
            return (*std::forward<Self>(self).__value.iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{std::to_address(std::forward<Self>(self).__value.iter)} noexcept;})
            requires (requires{{std::to_address(std::forward<Self>(self).__value.iter)};})
        {
            return std::to_address(std::forward<Self>(self).__value.iter);
        }

        template <typename Self, typename V>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, V&& v)
            noexcept (requires{
                {std::forward<Self>(self).__value.iter[std::forward<V>(v)]} noexcept;
            })
            requires (requires{
                {std::forward<Self>(self).__value.iter[std::forward<V>(v)]};
            })
        {
            return (std::forward<Self>(self).__value.iter[std::forward<V>(v)]);
        }

        constexpr optional_iterator& operator++()
            noexcept (requires{{++__value.iter} noexcept;})
            requires (requires{{++__value.iter};})
        {
            ++__value.iter;
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
            noexcept (requires{{optional_iterator(lhs.__value.iter + rhs)} noexcept;})
            requires (requires{{optional_iterator(lhs.__value.iter + rhs)};})
        {
            return optional_iterator(lhs.__value.iter + rhs);
        }

        [[nodiscard]] friend constexpr optional_iterator operator+(
            difference_type lhs,
            const optional_iterator& rhs
        )
            noexcept (requires{{optional_iterator(lhs + rhs.__value.iter)} noexcept;})
            requires (requires{{optional_iterator(lhs + rhs.__value.iter)};})
        {
            return optional_iterator(lhs + rhs.__value.iter);
        }

        constexpr optional_iterator& operator+=(difference_type n)
            noexcept (requires{{__value.iter += n} noexcept;})
            requires (requires{{__value.iter += n};})
        {
            __value.iter += n;
            return *this;
        }

        constexpr optional_iterator& operator--()
            noexcept (requires{{--__value.iter} noexcept;})
            requires (requires{{--__value.iter};})
        {
            --__value.iter;
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
            noexcept (requires{{optional_iterator(__value.iter - n)} noexcept;})
            requires (requires{{optional_iterator(__value.iter - n)};})
        {
            return optional_iterator(__value.iter - n);
        }

        [[nodiscard]] difference_type operator-(const optional_iterator& other) const
            noexcept (requires{{
                __value.iter - other.__value.iter
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{
                __value.iter - other.__value.iter
            } -> meta::convertible_to<difference_type>;})
        {
            return __value.iter - other.__value.iter;
        }

        constexpr optional_iterator& operator-=(difference_type n)
            noexcept (requires{{__value.iter -= n} noexcept;})
            requires (requires{{__value.iter -= n};})
        {
            __value.iter -= n;
            return *this;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<(const optional_iterator<U>& other) const
            noexcept (requires{{
                initialized && other.initialized && __value.iter < other.__value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                initialized && other.initialized && __value.iter < other.__value.iter
            } -> meta::convertible_to<bool>;})
        {
            return initialized && other.initialized && __value.iter < other.__value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<=(const optional_iterator<U>& other) const
            noexcept (requires{{
                !initialized || !other.initialized || __value.iter <= other.__value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                !initialized || !other.initialized || __value.iter <= other.__value.iter
            } -> meta::convertible_to<bool>;})
        {
            return !initialized || !other.initialized || __value.iter <= other.__value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator==(const optional_iterator<U>& other) const
            noexcept (requires{{
                !initialized || !other.initialized || __value.iter == other.__value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                !initialized || !other.initialized || __value.iter == other.__value.iter
            } -> meta::convertible_to<bool>;})
        {
            return !initialized || !other.initialized || __value.iter == other.__value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>=(const optional_iterator<U>& other) const
            noexcept (requires{{
                !initialized || !other.initialized || __value.iter >= other.__value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                !initialized || !other.initialized || __value.iter >= other.__value.iter
            } -> meta::convertible_to<bool>;})
        {
            return !initialized || !other.initialized || __value.iter >= other.__value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>(const optional_iterator<U>& other) const
            noexcept (requires{{
                initialized && other.initialized && __value.iter > other.__value.iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                initialized && other.initialized && __value.iter > other.__value.iter
            } -> meta::convertible_to<bool>;})
        {
            return initialized && other.initialized && __value.iter > other.__value.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr auto operator<=>(const optional_iterator<U>& other) const
            noexcept (requires{{__value.iter <=> other.__value.iter} noexcept;})
            requires (requires{{__value.iter <=> other.__value.iter};})
        {
            using type = meta::unqualify<decltype(__value.iter <=> other.__value.iter)>;
            if (!initialized || !other.initialized) {
                return type::equivalent;
            }
            return __value.iter <=> other.__value.iter;
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

    /* `make_optional_iterator<T>` chooses the right iterator type to return for an
    optional or expected type `T`.  If the value type for `T` is iterable, then this
    will be an `optional_iterator` wrapper around the iterator type.  Otherwise, it
    is a trivial `contiguous_iterator`, which only yields a single value. */
    template <meta::lvalue T>
    struct make_optional_iterator {
        static constexpr bool trivial = true;
        using type = decltype(std::declval<T>().__value.template get<1>());
        using begin_type = contiguous_iterator<type>;
        using end_type = begin_type;
        using rbegin_type = std::reverse_iterator<begin_type>;
        using rend_type = rbegin_type;

        static constexpr auto begin(T opt)
            noexcept (requires{
                {begin_type(std::addressof(opt.__value.template get<1>()) + (opt == None))} noexcept;
            })
            requires (requires{
                {begin_type(std::addressof(opt.__value.template get<1>()) + (opt == None))};
            })
        {
            return begin_type{std::addressof(opt.__value.template get<1>()) + (opt == None)};
        }

        static constexpr auto end(T opt)
            noexcept (requires{
                {end_type(std::addressof(opt.__value.template get<1>()) + 1)} noexcept;
            })
            requires (requires{
                {end_type(std::addressof(opt.__value.template get<1>()) + 1)};
            })
        {
            return end_type{std::addressof(opt.__value.template get<1>()) + 1};
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
            meta::iterable<decltype(std::declval<T>().__value.template get<1>())> ||
            meta::reverse_iterable<decltype(std::declval<T>().__value.template get<1>())>
        )
    struct make_optional_iterator<T> {
        static constexpr bool trivial = false;
        using type = decltype(std::declval<T>().__value.template get<1>());
        using begin_type = make_optional_begin<type>::type;
        using end_type = make_optional_end<type>::type;
        using rbegin_type = make_optional_rbegin<type>::type;
        using rend_type = make_optional_rend<type>::type;

        static constexpr auto begin(T opt)
            noexcept (requires{
                {opt == None} noexcept;
                {begin_type{}} noexcept;
                {begin_type{meta::begin(opt.__value.template get<1>())}} noexcept;
            })
            requires (requires{
                {opt == None};
                {begin_type{}};
                {begin_type{meta::begin(opt.__value.template get<1>())}};
            })
        {
            if (opt == None) {
                return begin_type{};
            } else {
                return begin_type{meta::begin(opt.__value.template get<1>())};
            }
        }

        static constexpr auto end(T opt)
            noexcept (requires{
                {opt == None} noexcept;
                {end_type{}} noexcept;
                {end_type{meta::end(opt.__value.template get<1>())}} noexcept;
            })
            requires (requires{
                {opt == None};
                {end_type{}};
                {end_type{meta::end(opt.__value.template get<1>())}};
            })
        {
            if (opt == None) {
                return end_type{};
            } else {
                return end_type{meta::end(opt.__value.template get<1>())};
            }
        }

        static constexpr auto rbegin(T opt)
            noexcept (requires{
                {opt == None} noexcept;
                {rbegin_type{}} noexcept;
                {rbegin_type{meta::rbegin(opt.__value.template get<1>())}} noexcept;
            })
            requires (requires{
                {opt == None};
                {rbegin_type{}};
                {rbegin_type{meta::rbegin(opt.__value.template get<1>())}};
            })
        {
            if (opt == None) {
                return rbegin_type{};
            } else {
                return rbegin_type{meta::rbegin(opt.__value.template get<1>())};
            }
        }

        static constexpr auto rend(T opt)
            noexcept (requires{
                {opt == None} noexcept;
                {rend_type{}} noexcept;
                {rend_type{meta::rend(opt.__value.template get<1>())}} noexcept;
            })
            requires (requires{
                {opt == None};
                {rend_type{}};
                {rend_type{meta::rend(opt.__value.template get<1>())}};
            })
        {
            if (opt == None) {
                return rend_type{};
            } else {
                return rend_type{meta::rend(opt.__value.template get<1>())};
            }
        }
    };

    /* A trivial iterator that represents an empty range.  Instances of this type
    always compare equal.  The template parameter sets the return type of the
    dereference operators, which is useful for metaprogramming purposes.  No values
    will ever actually be yielded. */
    template <meta::not_void T = NoneType>
    struct empty_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = meta::remove_reference<T>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::address_type<reference>;

        [[noreturn]] constexpr reference operator*() { throw StopIteration(); }
        [[noreturn]] constexpr reference operator*() const { throw StopIteration(); }
        [[noreturn]] constexpr pointer operator->() { throw StopIteration(); }
        [[noreturn]] constexpr pointer operator->() const { throw StopIteration(); }
        [[noreturn]] constexpr reference operator[](difference_type) { throw StopIteration(); }
        [[noreturn]] constexpr reference operator[](difference_type) const { throw StopIteration(); }
        constexpr empty_iterator& operator++() noexcept { return *this; }
        [[nodiscard]] constexpr empty_iterator operator++(int) noexcept { return {}; }
        [[nodiscard]] friend constexpr empty_iterator operator+(
            const empty_iterator&,
            difference_type
        ) noexcept {
            return {};
        }
        [[nodiscard]] friend constexpr empty_iterator operator+(
            difference_type,
            const empty_iterator&
        ) noexcept {
            return {};
        }
        constexpr empty_iterator& operator+=(difference_type) noexcept { return *this; }
        constexpr empty_iterator& operator--() noexcept { return *this; }
        [[nodiscard]] constexpr empty_iterator operator--(int) noexcept { return {}; }
        [[nodiscard]] constexpr empty_iterator operator-(difference_type) const noexcept {
            return {};
        }
        template <typename U>
        [[nodiscard]] constexpr difference_type operator-(const empty_iterator<U>&) const noexcept {
            return 0;
        }
        constexpr empty_iterator& operator-=(difference_type) noexcept { return *this; }
        template <typename U>
        [[nodiscard]] constexpr bool operator==(const empty_iterator<U>&) const noexcept {
            return true;
        }
        template <typename U>
        [[nodiscard]] constexpr auto operator<=>(const empty_iterator<U>&) const noexcept {
            return std::strong_ordering::equal;
        }
    };

}


/* A wrapper for an arbitrarily qualified type that can also represent an empty state.

This is identical to `Union<NoneType, T>`, except in the following cases:

    1.  The implicit and explicit constructors always construct `T` as the active
        member, except for the default constructor and implicit conversion conversion
        from `None` or `std::nullopt` (assuming those are not valid constructors for
        `T`).
    2.  CTAD guides allow `T` to be omitted in many cases and inferred from a
        corresponding initializer, including as non-type template parameters for
        arbitrary classes.
    3.  As a consequence of (2), `Optional<void>` is well-formed, as is
        `Optional<NoneType>`, both of which equate to the same type.  These types will
        only store the empty state, and will dereference to that state rather than a
        non-empty value.  They also yield an empty range when iterated over.
    4.  Otherwise, dereferencing and pointer indirection assume that the active member
        is not empty, and always return references to `T`.  A check confirming the
        non-empty state will only be generated in debug builds, meaning that in release
        builds, the pointer operations will produce the same machine code as raw
        pointers.
    5.  If `T` is an lvalue reference (i.e. `T&`), then the optional also supports
        conversions both to and from `T*`, where the empty state maps to a null
        pointer.  This allows `Optional<T&>` to be used as a drop-in replacement for
        pointers in most cases, as long as pointer arithmetic is not required.
    6.  The empty state can be omitted during monadic operations and `impl::visit()`
        calls, in which case it will be implicitly propagated to the return type,
        possibly promoting it to an optional.  Note that this is not the case for
        pattern matching via `->*`, which must exhaustively cover both states.
    7.  Optionals are always iterable, with one of the following behaviors depending on
        `T`:
        -   If `T` is iterable, then the result is a forwarding adaptor for the
            iterator(s) over `T`, which behave identically.  If the optional is in the
            empty state, then the adaptor will be uninitialized, and will always
            compare equal to its sentinel, yielding an empty range.
        -   If `T` is not iterable, then the result is an iterator over a single
            element, which is equivalent to a simple pointer to the contained value.
            If the optional is in the empty state, then a one-past-the-end pointer will
            be used instead.
        -   If `T` is `void` or `NoneType`, then the optional returns a trivial
            iterator that yields no values, acting as an empty range.

Note that because optional references are compatible with pointers, Bertrand's binding
generators will generate them wherever pointers or pointer-like objects are exposed to
a language that otherwise does not implement them as first-class citizens. */
template <typename T>
struct Optional : impl::optional_tag {
    impl::basic_union<NoneType, meta::remove_rvalue<T>> __value;

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
        noexcept (meta::nothrow::visit_exhaustive<impl::optional_convert_from<T, from>, from>)
        requires (meta::visit_exhaustive<impl::optional_convert_from<T, from>, from>)
    : 
        __value(impl::visit(impl::optional_convert_from<T, from>{}, std::forward<from>(v)))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the value type's
    constructor, and initializes the optional with the result. */
    template <typename... A>
    [[nodiscard]] constexpr explicit Optional(A&&... args)
        noexcept (meta::nothrow::visit_exhaustive<impl::optional_construct_from<T>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<impl::optional_convert_from<T, A...>, A...> &&
            meta::visit_exhaustive<impl::optional_construct_from<T>, A...>
        )
    :
        __value(impl::visit(impl::optional_construct_from<T>{}, std::forward<A>(args)...))
    {}

    /* Swap the contents of two optionals as efficiently as possible. */
    constexpr void swap(Optional& other)
        noexcept (requires{{__value.swap(other.__value)} noexcept;})
        requires (requires{{__value.swap(other.__value)};})
    {
        __value.swap(other.__value);
    }

    /* Implicit conversion from `Optional<T>` to any type that is convertible from both
    the perfectly-forwarded value type and any of `None`, `std::nullopt`, or `nullptr`
    (if `T` is an lvalue reference). */
    template <typename Self, typename to>
    [[nodiscard]] constexpr operator to(this Self&& self)
        noexcept (requires{
            {impl::optional_convert_to<Self, to>{}(std::forward<Self>(self))} noexcept;
        })
        requires (
            !meta::prefer_constructor<to> &&
            !meta::boolean<to> &&
            requires{{impl::optional_convert_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::optional_convert_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Explicit conversion from `Optional<T>` to any type that is explicitly
    convertible from both the perfectly-forwarded value type and any of `None`,
    `std::nullopt`, or `nullptr` (if `T` is an lvalue reference).  This operator only
    applies if an implicit conversion could not be found. */
    template <typename Self, typename to>
    [[nodiscard]] explicit constexpr operator to(this Self&& self)
        noexcept (requires{
            {impl::optional_cast_to<Self, to>{}(std::forward<Self>(self))} noexcept;
        })
        requires (
            !meta::prefer_constructor<to> &&
            !meta::is<to, bool> &&
            !requires{{impl::optional_convert_to<Self, to>{}(std::forward<Self>(self))};} &&
            requires{{impl::optional_cast_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::optional_cast_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Contextually convert the optional to a boolean, where true indicates the
    presence of a value. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return __value.index();
    }
    [[deprecated(
        "`Optional<bool>` should never be contextually converted to `bool`.  Consider "
        "an explicit comparison against `None`, a dereference with a leading `*`, or "
        "an exhaustive visitor via trailing `->*` instead. "
    )]] explicit constexpr operator bool() const noexcept requires (DEBUG && meta::boolean<T>) {
        return __value.index();
    }

    /* Dereference to obtain the stored value, perfectly forwarding it according to the
    optional's current cvref qualifications.  A `TypeError` will be thrown if the
    program is compiled in debug mode and the optional is empty.  This requires a
    single extra conditional, which will be optimized out in release builds to maintain
    zero overhead. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept (!DEBUG) {
        if constexpr (DEBUG) {
            if (self.__value.index() == 0) {
                throw impl::bad_optional_access<T>();
            }
        }
        return (std::forward<Self>(self).__value.template get<1>());
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  A `TypeError` error will be thrown if
    the program is compiled in debug mode and the optional is empty.  This requires a
    single extra conditional, which will be optimized out in release builds to maintain
    zero overhead. */
    [[nodiscard]] constexpr auto operator->()
        noexcept (!DEBUG && requires{{meta::to_arrow(__value.template get<1>())} noexcept;})
        requires (requires{{meta::to_arrow(__value.template get<1>())};})
    {
        if constexpr (DEBUG) {
            if (__value.index() == 0) {
                throw impl::bad_optional_access<T>();
            }
        }
        return meta::to_arrow(__value.template get<1>());
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  A `TypeError` error will be thrown if
    the program is compiled in debug mode and the optional is empty.  This requires a
    single extra conditional, which will be optimized out in release builds to maintain
    zero overhead. */
    [[nodiscard]] constexpr auto operator->() const
        noexcept (!DEBUG && requires{{meta::to_arrow(__value.template get<1>())} noexcept;})
        requires (requires{{meta::to_arrow(__value.template get<1>())};})
    {
        if constexpr (DEBUG) {
            if (__value.index() == 0) {
                throw impl::bad_optional_access<T>();
            }
        }
        return meta::to_arrow(__value.template get<1>());
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `None` or `std::nullopt`. */
    [[nodiscard]] friend constexpr bool operator==(const Optional& opt, NoneType) noexcept {
        return opt.__value.index() == 0;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `None` or `std::nullopt`. */
    [[nodiscard]] friend constexpr bool operator==(NoneType, const Optional& opt) noexcept {
        return opt.__value.index() == 0;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `nullptr`, assuming `T` is an lvalue reference. */
    [[nodiscard]] friend constexpr bool operator==(const Optional& opt, std::nullptr_t) noexcept
        requires (meta::lvalue<T>)
    {
        return opt.__value.index() == 0;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `nullptr`, assuming `T` is an lvalue reference. */
    [[nodiscard]] friend constexpr bool operator==(std::nullptr_t, const Optional& opt) noexcept
        requires (meta::lvalue<T>)
    {
        return opt.__value.index() == 0;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `None` or `std::nullopt`. */
    [[nodiscard]] friend constexpr bool operator!=(const Optional& opt, NoneType) noexcept {
        return opt.__value.index() != 0;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `None` or `std::nullopt`. */
    [[nodiscard]] friend constexpr bool operator!=(NoneType, const Optional& opt) noexcept {
        return opt.__value.index() != 0;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `nullptr`, assuming `T` is an lvalue reference. */
    [[nodiscard]] friend constexpr bool operator!=(const Optional& opt, std::nullptr_t) noexcept
        requires (meta::lvalue<T>)
    {
        return opt.__value.index() != 0;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `nullptr`, assuming `T` is an lvalue reference. */
    [[nodiscard]] friend constexpr bool operator!=(std::nullptr_t, const Optional& opt) noexcept
        requires (meta::lvalue<T>)
    {
        return opt.__value.index() != 0;
    }

    /* Forward tuple access to `T`, assuming it supports it.  If so, then the return
    type will be promoted to an `Optional<R>`, where `R` represents the forwarded
    result, and the empty state is implicitly propagated. */
    template <ssize_t I, typename Self>
        requires (meta::tuple_like<T> && impl::valid_index<meta::tuple_size<T>, I>)
    [[nodiscard]] constexpr auto get(this Self&& self)
        noexcept (requires{{impl::optional_get<Self, I>{}(std::forward<Self>(self))} noexcept;})
        requires (requires{{impl::optional_get<Self, I>{}(std::forward<Self>(self))};})
    {
        return impl::optional_get<Self, I>{}(std::forward<Self>(self));
    }

    /* Return 0 if the optional is empty or `meta::size(*opt)` otherwise.  If
    `meta::size(*opt)` would be malformed and the value is not iterable (meaning that
    iterating over the optional would return just a single element), then the result
    will be identical to `opt != None`.  If neither option is available, then this
    method will fail to compile. */
    [[nodiscard]] constexpr auto size() const
        noexcept (
            meta::nothrow::has_size<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
        requires (
            meta::has_size<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
    {
        if constexpr (meta::has_size<meta::as_const_ref<T>>) {
            if (__value.index()) {
                return meta::size(__value.template get<1>());
            } else {
                return meta::size_type<T>(0);
            }
        } else {
            return size_t(__value.index());
        }
    }

    /* Return 0 if the optional is empty or `meta::ssize(*opt)` otherwise.  If
    `meta::ssize(*opt)` would be malformed and the value is not iterable (meaning that
    iterating over the optional would return just a single element), then the result
    will be identical to `opt != None`.  If neither option is available, then this
    method will fail to compile. */
    [[nodiscard]] constexpr auto ssize() const
        noexcept (
            meta::nothrow::has_ssize<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
        requires (
            meta::has_ssize<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
    {
        if constexpr (meta::has_ssize<meta::as_const_ref<T>>) {
            if (__value.index()) {
                return meta::ssize(__value.template get<1>());
            } else {
                return meta::ssize_type<T>(0);
            }
        } else {
            return ssize_t(__value.index());
        }
    }

    /* Return true if the optional is empty or `std::ranges::empty(*opt)` otherwise.
    If `std::ranges::empty(*opt)` would be malformed and the value is not iterable
    (meaning that iterating over the optional would return just a single element), then
    the result will be identical to `opt == None`.  If neither option is available,
    then this method will fail to compile. */
    [[nodiscard]] constexpr bool empty() const
        noexcept (
            meta::nothrow::has_empty<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
        requires (
            meta::has_empty<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Optional&>::trivial
        )
    {
        if constexpr (meta::has_empty<meta::as_const_ref<T>>) {
            return __value.index() ? std::ranges::empty(__value.template get<1>()) : true;
        } else {
            return !__value.index();
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

    template <typename Self, typename... A>
    constexpr decltype(auto) operator()(this Self&& self, A&&... args)
        noexcept (meta::nothrow::force_visit<1, impl::Call, Self, A...>)
        requires (meta::force_visit<1, impl::Call, Self, A...>)
    {
        return (impl::visit<1>(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<A>(args)...
        ));
    }

    template <typename Self, typename... K>
    constexpr decltype(auto) operator[](this Self&& self, K&&... keys)
        noexcept (meta::nothrow::force_visit<1, impl::Subscript, Self, K...>)
        requires (meta::force_visit<1, impl::Subscript, Self, K...>)
    {
        return (impl::visit<1>(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<K>(keys)...
        ));
    }
};


/* A special case of `Optional<T>` that always represents a purely empty state, which
has no other value.  See `Optional<T>` for more details.

This specialization is necessary to allow CTAD to correctly deduce the type of
`Optional` non-type template parameters, where an initializer of `None` would otherwise
be invalid.  With this in place, the following code compiles:

```
template <Optional value>
struct Foo { static constexpr bool empty = (value == None); };
static_assert(Foo<None>::empty);
static_assert(!Foo<1>::empty);
static_assert(!Foo<2.5>::empty);
```
*/
template <typename T> requires (meta::is_void<T> || meta::None<T>)
struct Optional<T> : impl::optional_tag {
    impl::basic_union<NoneType> __value;

    /* Default constructor.  Initializes the optional in the empty state. */
    [[nodiscard]] constexpr Optional() = default;

    /* Converting constructor.  Implicitly converts the input to `NoneType`. */
    template <typename from>
    [[nodiscard]] constexpr Optional(from&& v)
        noexcept (meta::nothrow::visit_exhaustive<impl::optional_convert_from<T, from>, from>)
        requires (meta::visit_exhaustive<impl::optional_convert_from<T, from>, from>)
    : 
        __value(impl::visit(
            impl::optional_convert_from<T, from>{},
            std::forward<from>(v)
        ))
    {}

    /* Explicit constructor.  Explicitly converts the input to `NoneType`. */
    template <typename... A>
    [[nodiscard]] constexpr explicit Optional(A&&... args)
        noexcept (meta::nothrow::visit_exhaustive<impl::optional_construct_from<T>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<impl::optional_convert_from<T, A...>, A...> &&
            meta::visit_exhaustive<impl::optional_construct_from<T>, A...>
        )
    :
        __value(impl::visit(
            impl::optional_construct_from<T>{},
            std::forward<A>(args)...
        ))
    {}

    /* Swap the contents of two optionals as efficiently as possible. */
    constexpr void swap(Optional& other)
        noexcept (requires{{__value.swap(other.__value)} noexcept;})
        requires (requires{{__value.swap(other.__value)};})
    {
        __value.swap(other.__value);
    }

    /* Implicit conversion from an empty `Optional` to any type that is convertible
    from any of `None`, `std::nullopt`, or `nullptr`. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr operator to(this Self&& self)
        noexcept (requires{
            {impl::optional_convert_to<Self, to>{}(std::forward<Self>(self))} noexcept;
        })
        requires (
            !meta::prefer_constructor<to> &&
            requires{{impl::optional_convert_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::optional_convert_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Explicit conversion from an empty `Optional` to any type that is explicitly
    convertible from any of `None`, `std::nullopt`, or `nullptr`.  This operator only
    applies if an implicit conversion could not be found. */
    template <typename Self, typename to>
    [[nodiscard]] explicit constexpr operator to(this Self&& self)
        noexcept (requires{
            {impl::optional_cast_to<Self, to>{}(std::forward<Self>(self))} noexcept;
        })
        requires (
            !meta::prefer_constructor<to> &&
            !requires{{impl::optional_convert_to<Self, to>{}(std::forward<Self>(self))};} &&
            requires{{impl::optional_cast_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::optional_cast_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Contextually convert the optional to a boolean, where true indicates the
    presence of a value.  For empty optionals, this will always return false. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return false; }

    /* Dereference to obtain the inner value, returning `None`. */
    [[nodiscard]] constexpr const NoneType& operator*() const noexcept { return None; }

    /* Indirectly read the inner value, returning a pointer to `None`. */
    [[nodiscard]] constexpr const NoneType* operator->() const noexcept { return &None; }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `None` or `std::nullopt`.  For empty optionals, this will always return true. */
    [[nodiscard]] friend constexpr bool operator==(const Optional& opt, NoneType) noexcept {
        return true;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `None` or `std::nullopt`.  For empty optionals, this will always return true. */
    [[nodiscard]] friend constexpr bool operator==(NoneType, const Optional& opt) noexcept {
        return true;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `nullptr`.  For empty optionals, this will always return true. */
    [[nodiscard]] friend constexpr bool operator==(const Optional& opt, std::nullptr_t) noexcept {
        return true;
    }

    /* Explicitly check whether the optional is in the empty state by comparing against
    `nullptr`.  For empty optionals, this will always return true. */
    [[nodiscard]] friend constexpr bool operator==(std::nullptr_t, const Optional& opt) noexcept {
        return true;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `None` or `std::nullopt`.  For empty optionals, this will always return
    false. */
    [[nodiscard]] friend constexpr bool operator!=(const Optional& opt, NoneType) noexcept {
        return false;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `None` or `std::nullopt`.  For empty optionals, this will always return
    false. */
    [[nodiscard]] friend constexpr bool operator!=(NoneType, const Optional& opt) noexcept {
        return false;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `nullptr`.  For empty optionals, this will always return false. */
    [[nodiscard]] friend constexpr bool operator!=(const Optional& opt, std::nullptr_t) noexcept {
        return false;
    }

    /* Explicitly check whether the optional is in the non-empty state by comparing
    against `nullptr`.  For empty optionals, this will always return false. */
    [[nodiscard]] friend constexpr bool operator!=(std::nullptr_t, const Optional& opt) noexcept {
        return false;
    }

    /* Empty optionals always have a size of zero. */
    [[nodiscard]] static constexpr size_t size() noexcept { return 0; }

    /* Empty optionals always have a size of zero. */
    [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }

    /* Empty optionals are always empty. */
    [[nodiscard]] static constexpr bool empty() noexcept { return true; }

    /* Get a forward iterator over an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `end()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
        return impl::empty_iterator<NoneType>{};
    }

    /* Get a forward iterator over an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `end()`. */
    [[nodiscard]] constexpr auto cbegin() const noexcept {
        return impl::empty_iterator<NoneType>{};
    }

    /* Get a forward sentinel for an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `begin()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
        return impl::empty_iterator<NoneType>{};
    }

    /* Get a forward sentinel for an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `begin()`. */
    [[nodiscard]] constexpr auto cend() const noexcept {
        return impl::empty_iterator<NoneType>{};
    }

    /* Get a reverse iterator over an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `rend()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
        return std::make_reverse_iterator(self.end());
    }

    /* Get a reverse iterator over an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `rend()`. */
    [[nodiscard]] constexpr auto crbegin() const noexcept {
        return std::make_reverse_iterator(cend());
    }

    /* Get a reverse sentinel for an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `rbegin()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
        return std::make_reverse_iterator(self.begin());
    }

    /* Get a reverse sentinel for an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `rbegin()`. */
    [[nodiscard]] constexpr auto crend() const noexcept {
        return std::make_reverse_iterator(cbegin());
    }

    template <typename Self, typename... A>
    constexpr decltype(auto) operator()(this Self&& self, A&&... args)
        noexcept (meta::nothrow::force_visit<1, impl::Call, Self, A...>)
        requires (meta::force_visit<1, impl::Call, Self, A...>)
    {
        return (impl::visit<1>(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<A>(args)...
        ));
    }

    template <typename Self, typename... K>
    constexpr decltype(auto) operator[](this Self&& self, K&&... keys)
        noexcept (meta::nothrow::force_visit<1, impl::Subscript, Self, K...>)
        requires (meta::force_visit<1, impl::Subscript, Self, K...>)
    {
        return (impl::visit<1>(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<K>(keys)...
        ));
    }
};


////////////////////////
////    EXPECTED    ////
////////////////////////


namespace impl {

    /* Expecteds can have void results, which get mapped to `None`. */
    template <typename T>
    using expected_value = std::conditional_t<meta::is_void<T>, NoneType, T>;

    /* Result: convert to proximal type (void if none). */
    template <typename from, typename proximal, typename...>
    struct _expected_convert_from { using type = proximal; };

    /* Recursive 1: prefer the most derived and least qualified matching error state,
    with lvalues binding to lvalues and prvalues, and rvalues binding to rvalues and
    prvalues.  If the result type is void, the candidate is more derived than it, or
    the candidate is less qualified, replace the intermediate result. */
    template <typename from, typename proximal, typename curr, typename... next>
        requires (union_proximal<from, curr>)
    struct _expected_convert_from<from, proximal, curr, next...> : _expected_convert_from<
        from,
        union_replace_proximal<from, curr, proximal>,
        next...
    > {};

    /* Recursive 2: no match at this index, discard curr. */
    template <typename from, typename proximal, typename curr, typename... next>
    struct _expected_convert_from<from, proximal, curr, next...> :
        _expected_convert_from<from, proximal, next...>
    {};

    /* A simple visitor that backs the implicit constructor for an `Expected<T, Es...>`
    object, returning a corresponding `impl::basic_union` primitive type. */
    template <typename, typename...>
    struct expected_convert_from {};
    template <typename T, typename... Es, typename in>
    struct expected_convert_from<meta::pack<T, Es...>, in> {
        using type = meta::remove_rvalue<expected_value<T>>;
        using result = impl::basic_union<type, meta::remove_rvalue<Es>...>;

        // 1) prefer direct conversion to `out` if possible
        template <typename from>
        static constexpr result operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, type>)
            requires (meta::convertible_to<from, type>)
        {
            return result{bertrand::alternative<0>, std::forward<from>(arg)};
        }

        template <typename from>
        using err = _expected_convert_from<from, void, Es...>::type;

        // 2) otherwise, if the input inherits from one of the expected error types,
        // then we convert it to the most proximal such type.
        template <typename from>
        static constexpr result operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, err<from>>)
            requires (!meta::convertible_to<from, type> && meta::not_void<err<from>>)
        {
            return result{
                bertrand::alternative<meta::index_of<err<from>, Es...> + 1>,
                std::forward<from>(arg)
            };
        }
    };

    /* A simple visitor that backs the explicit constructor for an `Expected<T, Es...>`
    object, returning a corresponding `impl::basic_union` primitive type.  Note that
    this only applies if `expected_convert_from` is invalid. */
    template <typename T, typename... Es>
    struct expected_construct_from {
        using type = meta::remove_rvalue<expected_value<T>>;
        using result = impl::basic_union<type, meta::remove_rvalue<Es>...>;

        template <typename... A>
        static constexpr result operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<type, A...>)
            requires (meta::constructible_from<type, A...>)
        {
            return result{bertrand::alternative<0>, std::forward<A>(args)...};
        }
    };

    /* A simple visitor that backs the implicit conversion operator from
    `Expected<T, Es...>`, which attempts a normal visitor conversion where possible,
    falling back to a conversion from `std::unexpected` to cover all STL types. */
    template <typename Self, typename to>
    struct expected_convert_to {
        template <size_t I>
        struct fn {
            static constexpr bool convert = requires(meta::forward<Self> self) {{
                std::forward<Self>(self).__value.template get<I>()
            } -> meta::convertible_to<to>;};

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{
                    std::forward<Self>(self).__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<to>;})
                requires (convert)
            {
                return std::forward<Self>(self).__value.template get<I>();
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{
                    std::unexpected(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<to>;})
                requires (!convert && I > 0 && requires{{
                    std::unexpected(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<to>;})
            {
                return std::unexpected(std::forward<Self>(self).__value.template get<I>());
            }
        };

        using dispatch = impl::basic_vtable<fn, visitable<Self>::alternatives::size()>;

        [[nodiscard]] static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
        }
    };

    /* A simple visitor that backs the explicit conversion operator from
    `Expected<T, Es...>`, which attempts a normal visitor conversion where possible,
    falling back to a conversion from `std::unexpected` to cover all STL types. */
    template <typename Self, typename to>
    struct expected_cast_to {
        template <size_t I>
        struct fn {
            static constexpr bool convert = requires(meta::forward<Self> self) {{
                static_cast<to>(std::forward<Self>(self).__value.template get<I>())
            };};

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{
                    static_cast<to>(std::forward<Self>(self).__value.template get<I>())
                } noexcept;})
                requires (convert)
            {
                return static_cast<to>(std::forward<Self>(self).__value.template get<I>());
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires {{static_cast<to>(std::unexpected(
                    std::forward<Self>(self).__value.template get<I>()
                ))} noexcept;})
                requires (!convert && I > 0 && requires {{static_cast<to>(std::unexpected(
                    std::forward<Self>(self).__value.template get<I>()
                ))};})
            {
                return static_cast<to>(std::unexpected(
                    std::forward<Self>(self).__value.template get<I>()
                ));
            }
        };

        using dispatch = impl::basic_vtable<fn, visitable<Self>::alternatives::size()>;

        [[nodiscard]] static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
        }
    };

    /* Attempt to dereference an expected.  If it is in an error state, then that state
    will be thrown as an exception.  Otherwise, perfectly forwards the contained
    value. */
    template <typename Self>
    struct expected_access {
        using type = decltype((std::declval<Self>().__value.template get<0>()));

        template <size_t I>
        struct fn {
            static constexpr type operator()(meta::forward<Self> self)
                requires (I == 0)
            {
                return (std::forward<Self>(self).__value.template get<0>());
            }
            [[noreturn]] static constexpr type operator()(meta::forward<Self> self)
                requires (I > 0)
            {
                throw std::forward<Self>(self).__value.template get<I>();
            }
        };

        using dispatch = impl::basic_vtable<fn, visitable<Self>::alternatives::size()>;

        [[nodiscard]] static constexpr type operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
        }
    };

    /* A simple visitor that backs the tuple indexing operator for `Optional<T>`, where
    `T` is tuple-like, and the return type is promoted to an optional. */
    template <typename Self, ssize_t I>
        requires (
            meta::tuple_like<typename visitable<Self>::value> &&
            impl::valid_index<meta::tuple_size<typename visitable<Self>::value>, I>
        )
    struct expected_get {
        using value = visitable<Self>::value;
        static constexpr size_t idx = size_t(impl::normalize_index<meta::tuple_size<value>, I>());
        using T = meta::remove_rvalue<decltype((meta::get<idx>(std::declval<value>())))>;
        template <typename... Es>
        using _Expected = bertrand::Expected<T, Es...>;
        using Expected = visitable<Self>::errors::template eval<_Expected>;

        template <size_t J>
        struct fn {
            static constexpr Expected operator()(meta::forward<Self> self)
                noexcept (requires{{
                    visitable<Self>::template get<J>(std::forward<Self>(self))
                } noexcept -> meta::nothrow::convertible_to<Expected>;})
                requires (requires{{
                    visitable<Self>::template get<J>(std::forward<Self>(self))
                } -> meta::convertible_to<Expected>;})
            {
                return visitable<Self>::template get<J>(std::forward<Self>(self));
            }
        };
        template <>
        struct fn<0> {
            static constexpr Expected operator()(meta::forward<Self> self)
                noexcept (requires{{meta::get<idx>(
                    visitable<Self>::template get<0>(std::forward<Self>(self))
                )} noexcept -> meta::nothrow::convertible_to<Expected>;})
                requires (requires{{meta::get<idx>(
                    visitable<Self>::template get<0>(std::forward<Self>(self))
                )} -> meta::convertible_to<Expected>;})
            {
                return meta::get<idx>(visitable<Self>::template get<0>(std::forward<Self>(self)));
            }
        };

        using dispatch = impl::basic_vtable<fn, visitable<Self>::alternatives::size()>;

        [[nodiscard]] static constexpr Expected operator()(meta::forward<Self> self)
            noexcept (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))} noexcept;})
            requires (requires{{dispatch{self.__value.index()}(std::forward<Self>(self))};})
        {
            return dispatch{self.__value.index()}(std::forward<Self>(self));
        }
    };

}


/* A wrapper for an arbitrarily qualified type that can also represent one or more
possible error states.

This is identical to `Union<T, E, Es...>`, except in the following case:

    1.  The implicit and explicit constructors always prefer to construct `T` unless
        the initializer(s) would be invalid, in which case the same rules apply as for
        `Union<E, Es...>` (i.e. the most proximal cvref-qualified type or base class,
        with implicit conversions only as a last resort).  Like `Union`, it is possible
        to unambiguously specify an error by providing an `alternative<I>` or `type<T>`
        tag as the first argument.
    2.  Pointer indirection assumes that the active member is not an error state.  If
        it is, then attempting to dereference it will throw that state as an exception,
        which can then be caught and analyzed using traditional try/catch semantics.
        This will never be optimized out in debug builds, differentiating it from
        `Optional`, at the cost of an extra branch in release builds.
    3.  `T` may be `void`, which is treated identically to `NoneType`, except that
        iterating over the result yields an empty range.  This allows `Expected` to be
        used as an error-handling strategy for functions that may fail, but otherwise
        do not return a value.
    4.  The error state(s) can be omitted during monadic operations and
        `impl::visit()` calls, in which case they will be implicitly propagated to the
        return type, possibly promoting that type to an expected.  Note that this is
        not the case for pattern matching via `->*`, which must exhaustively cover all
        possible states.
    5.  Expecteds are always iterable, with one of the following behaviors depending on
        `T`:
        -   If `T` is iterable, then the result is a forwarding adaptor for the
            iterator(s) over `T`, which behave identically.  If the expected is in an
            error state, then the adaptor will be uninitialized, and will always
            compare equal to its sentinel, yielding an empty range.
        -   If `T` is not iterable, then the result is an iterator over a single
            element, which is equivalent to a simple pointer to the contained value.
            If the expected is in an error state, then a one-past-the-end pointer will
            be used instead.
        -   If `T` is `void`, then the optional returns a trivial iterator that yields
            no values, acting as an empty range.

Note that the intended use for `Expected` is as a value-based error handling strategy,
which can be used as a safer and more explicit alternative to `try/catch` blocks,
promoting exhaustive error coverage via the type system. */
template <typename T, typename E, typename... Es> requires (impl::expected_concept<T, E, Es...>)
struct Expected : impl::expected_tag {
    impl::basic_union<
        meta::remove_rvalue<T>,
        meta::remove_rvalue<E>,
        meta::remove_rvalue<Es>...
    > __value;

    /* Default constructor.  Enabled if and only if the result type is default
    constructible or void. */
    [[nodiscard]] constexpr Expected()
        noexcept (meta::nothrow::default_constructible<meta::remove_rvalue<T>>)
        requires (meta::default_constructible<meta::remove_rvalue<T>>)
    {}

    /* Converting constructor.  Implicitly converts the input to the value type if
    possible, otherwise accepts subclasses of the error states.  Also allows conversion
    from other visitable types whose alternatives all meet the conversion criteria. */
    template <typename from>
    [[nodiscard]] constexpr Expected(from&& v)
        noexcept (meta::nothrow::visit_exhaustive<
            impl::expected_convert_from<meta::pack<T, E, Es...>, from>,
            from
        >)
        requires (meta::visit_exhaustive<
            impl::expected_convert_from<meta::pack<T, E, Es...>, from>,
            from
        >)
    :
        __value(impl::visit(
            impl::expected_convert_from<meta::pack<T, E, Es...>, from>{},
            std::forward<from>(v)
        ))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the result type's
    constructor, and initializes the expected with the result. */
    template <typename... A>
    [[nodiscard]] explicit constexpr Expected(A&&... args)
        noexcept (meta::nothrow::visit_exhaustive<impl::expected_construct_from<T, E, Es...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<
                impl::expected_convert_from<meta::pack<T, E, Es...>, A...>,
                A...
            > &&
            meta::visit_exhaustive<impl::expected_construct_from<T, E, Es...>, A...>
        )
    :
        __value(impl::visit(
            impl::expected_construct_from<T, E, Es...>{},
            std::forward<A>(args)...
        ))
    {}

    /* Explicitly construct an expected with the alternative at index `I` using the
    provided arguments.  This is more explicit than using the standard constructors,
    for cases where only a specific alternative should be considered. */
    template <size_t I, typename... A> requires (I < (sizeof...(Es) + 2))
    [[nodiscard]] explicit constexpr Expected(std::in_place_index_t<I> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<
            meta::unpack_type<I, T, E, Es...>,
            A...
        >)
        requires (meta::constructible_from<
            meta::unpack_type<I, T, E, Es...>,
            A...
        >)
    :
        __value{tag, std::forward<A>(args)...}
    {}

    /* Explicitly construct an expected with the specified alternative using the given
    arguments.  This is more explicit than using the standard constructors, for cases
    where only a specific alternative should be considered. */
    template <typename U, typename... A>
        requires (meta::pack<T, E, Es...>::template contains<U>())
    [[nodiscard]] explicit constexpr Expected(std::type_identity<U> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<U, A...>)
        requires (meta::constructible_from<U, A...>)
    :
        __value{bertrand::alternative<meta::index_of<U, T, E, Es...>>, std::forward<A>(args)...}
    {}

    /* Swap the contents of two expecteds as efficiently as possible. */
    constexpr void swap(Expected& other)
        noexcept (requires{{__value.swap(other.__value)} noexcept;})
        requires (requires{{__value.swap(other.__value)};})
    {
        if (this != &other) {
            __value.swap(other.__value);
        }
    }

    /* Implicitly convert the `Expected` to any other type to which all alternatives
    can be converted.  If an error state is not directly convertible to the type, the
    algorithm will try again with the type wrapped in `std::unexpected` instead. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr operator to(this Self&& self)
        noexcept (requires{
            {impl::expected_convert_to<Self, to>{}(std::forward<Self>(self))} noexcept;
        })
        requires (
            !meta::prefer_constructor<to> &&
            requires{{impl::expected_convert_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::expected_convert_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Explicitly convert the `Expected` to any other type to which all alternatives
    can be explicitly converted.  If an error state is not directly convertible to the
    type, the algorithm will try again with the type wrapped in `std::unexpected`
    instead.  This operator only applies if an implicit conversion could not be
    found. */
    template <typename Self, typename to>
    [[nodiscard]] explicit constexpr operator to(this Self&& self)
        noexcept (requires{
            {impl::expected_cast_to<Self, to>{}(std::forward<Self>(self))} noexcept;
        })
        requires (
            !meta::prefer_constructor<to> &&
            !meta::is<to, bool> &&
            !requires{{impl::expected_convert_to<Self, to>{}(std::forward<Self>(self))};} &&
            requires{{impl::expected_cast_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::expected_cast_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Contextually convert the expected to a boolean, where true indicates the
    presence of a value. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return __value.index() == 0;
    }
    [[deprecated(
        "`Expected<bool>` should never be contextually converted to `bool`.  Consider "
        "an explicit comparison against `None`, a dereference with a leading `*`, or "
        "an exhaustive visitor via trailing `->*` instead. "
    )]] explicit constexpr operator bool() const noexcept requires (DEBUG && meta::boolean<T>) {
        return __value.index() == 0;
    }

    /* Dereference to obtain the stored value, perfectly forwarding it according to the
    expected's current cvref qualifications.  If the expected is in an error state,
    then the error will be thrown as an exception. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) {
        return (impl::expected_access<Self>{}(std::forward<Self>(self)));
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  If the expected is in an error state,
    then the error will be thrown as an exception. */
    [[nodiscard]] constexpr auto operator->()
        noexcept (requires{
            {meta::to_arrow(impl::expected_access<Expected&>{}(*this))} noexcept;
        })
        requires (requires{
            {meta::to_arrow(impl::expected_access<Expected&>{}(*this))};
        })
    {
        return meta::to_arrow(impl::expected_access<Expected&>{}(*this));
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  If the expected is in an error state,
    then the error will be thrown as an exception. */
    [[nodiscard]] constexpr auto operator->() const
        noexcept (requires{
            {meta::to_arrow(impl::expected_access<const Expected&>{}(*this))} noexcept;
        })
        requires (requires{
            {meta::to_arrow(impl::expected_access<const Expected&>{}(*this))};
        })
    {
        return meta::to_arrow(impl::expected_access<const Expected&>{}(*this));
    }

    /* Forward tuple access to `T`, assuming it supports it.  If so, then the return
    type will be promoted to an `Expected<R, Es...>`, where `R` represents the
    forwarded result, and `Es...` represent the forwarded error state(s). */
    template <ssize_t I, typename Self>
        requires (meta::tuple_like<T> && impl::valid_index<meta::tuple_size<T>, I>)
    [[nodiscard]] constexpr auto get(this Self&& self)
        noexcept (requires{{impl::expected_get<Self, I>{}(std::forward<Self>(self))} noexcept;})
        requires (requires{{impl::expected_get<Self, I>{}(std::forward<Self>(self))};})
    {
        return impl::expected_get<Self, I>{}(std::forward<Self>(self));
    }

    /* Return 0 if the expected is empty or `meta::size(*exp)` otherwise.  If
    `meta::size(*exp)` would be malformed and the value is not iterable (meaning that
    iterating over the expected would return just a single element), then the result
    will be identical to `exp != None`.  If neither option is available, then this
    method will fail to compile. */
    [[nodiscard]] constexpr auto size() const
        noexcept (
            meta::nothrow::has_size<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
        requires (
            meta::has_size<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
    {
        if constexpr (meta::has_size<meta::as_const_ref<T>> ) {
            if (__value.index() == 0) {
                return meta::size(__value.template get<0>());
            } else {
                return meta::size_type<T>(0);
            }
        } else {
            return size_t(__value.index() == 0);
        }
    }

    /* Return 0 if the expected is empty or `meta::ssize(*exp)` otherwise.  If
    `meta::ssize(*exp)` would be malformed and the value is not iterable (meaning that
    iterating over the expected would return just a single element), then the result
    will be identical to `exp != None`.  If neither option is available, then this
    method will fail to compile. */
    [[nodiscard]] constexpr auto ssize() const
        noexcept (
            meta::nothrow::has_ssize<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
        requires (
            meta::has_ssize<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
    {
        if constexpr (meta::has_ssize<meta::as_const_ref<T>>) {
            if (__value.index() == 0) {
                return meta::ssize(__value.template get<0>());
            } else {
                return meta::ssize_type<T>(0);
            }
        } else {
            return ssize_t(__value.index() == 0);
        }
    }

    /* Return true if the expected is in an error state or `std::ranges::empty(*exp)`
    otherwise.  If `std::ranges::empty(*exp)` would be malformed and the value is not
    iterable (meaning that iterating over the expected would return just a single
    element), then the result will be identical to `exp == None`.  If neither option is
    available, then this method will fail to compile. */
    [[nodiscard]] constexpr bool empty() const
        noexcept (
            meta::nothrow::has_empty<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
        requires (
            meta::has_empty<meta::as_const_ref<T>> ||
            impl::make_optional_iterator<const Expected&>::trivial
        )
    {
        if constexpr (meta::has_empty<meta::as_const_ref<T>>) {
            return __value.index() != 0 || std::ranges::empty(__value.template get<0>());
        } else {
            return __value.index() != 0;
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

    template <typename Self, typename... A>
    constexpr decltype(auto) operator()(this Self&& self, A&&... args)
        noexcept (meta::nothrow::force_visit<1, impl::Call, Self, A...>)
        requires (meta::force_visit<1, impl::Call, Self, A...>)
    {
        return (impl::visit<1>(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<A>(args)...
        ));
    }

    template <typename Self, typename... K>
    constexpr decltype(auto) operator[](this Self&& self, K&&... keys)
        noexcept (meta::nothrow::force_visit<1, impl::Subscript, Self, K...>)
        requires (meta::force_visit<1, impl::Subscript, Self, K...>)
    {
        return (impl::visit<1>(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<K>(keys)...
        ));
    }
};


/* A special case of `Expected<T, Es...>` where the result type is `void`, which maps
to `NoneType`, but yields an empty range when iterated over.  See `Expected<T, Es...>`
for more details.

This specialization is necessary to allow `Expected` to be used as a return type for
functions that may fail, but do not otherwise return a value.  With this in place,
the following code compiles:

```
auto foo(bool b) -> Expected<void, TypeError> {
    if (!b) {
        return TypeError("some error");  // OK, returns the error state
    }
    return {};  // OK, returns the result state (None)
}
```
*/
template <meta::is_void T, typename E, typename... Es>
    requires (impl::expected_concept<T, E, Es...>)
struct Expected<T, E, Es...> {
    impl::basic_union<
        NoneType,
        meta::remove_rvalue<E>,
        meta::remove_rvalue<Es>...
    > __value;

    /* Default constructor.  Enabled if and only if the result type is default
    constructible or void. */
    [[nodiscard]] constexpr Expected()
        noexcept (meta::nothrow::default_constructible<NoneType>)
        requires (meta::default_constructible<NoneType>)
    {}

    /* Converting constructor.  Implicitly converts the input to the value type if
    possible, otherwise accepts subclasses of the error states.  Also allows conversion
    from other visitable types whose alternatives all meet the conversion criteria. */
    template <typename from>
    [[nodiscard]] constexpr Expected(from&& v)
        noexcept (meta::nothrow::visit_exhaustive<
            impl::expected_convert_from<meta::pack<NoneType, E, Es...>, from>,
            from
        >)
        requires (meta::visit_exhaustive<
            impl::expected_convert_from<meta::pack<NoneType, E, Es...>, from>,
            from
        >)
    :
        __value(impl::visit(
            impl::expected_convert_from<meta::pack<NoneType, E, Es...>, from>{},
            std::forward<from>(v)
        ))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the result type's
    constructor, and initializes the expected with the result. */
    template <typename... A>
    [[nodiscard]] explicit constexpr Expected(A&&... args)
        noexcept (meta::nothrow::visit_exhaustive<impl::expected_construct_from<T, E, Es...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<
                impl::expected_convert_from<meta::pack<NoneType, E, Es...>, A...>,
                A...
            > &&
            meta::visit_exhaustive<impl::expected_construct_from<T, E, Es...>, A...>
        )
    :
        __value(impl::visit(
            impl::expected_construct_from<T, E, Es...>{},
            std::forward<A>(args)...
        ))
    {}

    /* Explicitly construct an expected with the alternative at index `I` using the
    provided arguments.  This is more explicit than using the standard constructors,
    for cases where only a specific alternative should be considered. */
    template <size_t I, typename... A> requires (I < (sizeof...(Es) + 2))
    [[nodiscard]] explicit constexpr Expected(std::in_place_index_t<I> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<
            meta::unpack_type<I, NoneType, E, Es...>,
            A...
        >)
        requires (meta::constructible_from<
            meta::unpack_type<I, NoneType, E, Es...>,
            A...
        >)
    :
        __value{tag, std::forward<A>(args)...}
    {}

    /* Explicitly construct an expected with the specified alternative using the given
    arguments.  This is more explicit than using the standard constructors, for cases
    where only a specific alternative should be considered. */
    template <typename U, typename... A>
        requires (meta::pack<NoneType, E, Es...>::template contains<U>())
    [[nodiscard]] explicit constexpr Expected(std::type_identity<U> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<U, A...>)
        requires (meta::constructible_from<U, A...>)
    :
        __value{
            bertrand::alternative<meta::index_of<U, NoneType, E, Es...>>,
            std::forward<A>(args)...
        }
    {}

    /* Implicitly convert the `Expected` to any other type to which all alternatives
    can be converted.  If an error state is not directly convertible to the type, the
    algorithm will try again with the type wrapped in `std::unexpected` instead. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr operator to(this Self&& self)
        noexcept (requires{
            {impl::expected_convert_to<Self, to>{}(std::forward<Self>(self))} noexcept;
        })
        requires (
            !meta::prefer_constructor<to> &&
            requires{{impl::expected_convert_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::expected_convert_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Explicitly convert the `Expected` to any other type to which all alternatives
    can be explicitly converted.  If an error state is not directly convertible to the
    type, the algorithm will try again with the type wrapped in `std::unexpected`
    instead.  This operator only applies if an implicit conversion could not be
    found. */
    template <typename Self, typename to>
    [[nodiscard]] explicit constexpr operator to(this Self&& self)
        noexcept (requires{
            {impl::expected_cast_to<Self, to>{}(std::forward<Self>(self))} noexcept;
        })
        requires (
            !meta::prefer_constructor<to> &&
            !requires{{impl::expected_convert_to<Self, to>{}(std::forward<Self>(self))};} &&
            requires{{impl::expected_cast_to<Self, to>{}(std::forward<Self>(self))};}
        )
    {
        return impl::expected_cast_to<Self, to>{}(std::forward<Self>(self));
    }

    /* Swap the contents of two expecteds as efficiently as possible. */
    constexpr void swap(Expected& other)
        noexcept (requires{{__value.swap(other.__value)} noexcept;})
        requires (requires{{__value.swap(other.__value)};})
    {
        if (this != &other) {
            __value.swap(other.__value);
        }
    }

    /* Contextually convert the expected to a boolean, where true indicates the
    presence of a value. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return __value.index() == 0;
    }

    /* Dereference to obtain the stored value, perfectly forwarding it according to the
    expected's current cvref qualifications.  If the expected is in an error state,
    then the error will be thrown as an exception. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) {
        return (impl::expected_access<Self>{}(std::forward<Self>(self)));
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  If the expected is in an error state,
    then the error will be thrown as an exception. */
    [[nodiscard]] constexpr auto operator->()
        noexcept (requires{
            {meta::to_arrow(impl::expected_access<Expected&>{}(*this))} noexcept;
        })
        requires (requires{
            {meta::to_arrow(impl::expected_access<Expected&>{}(*this))};
        })
    {
        return meta::to_arrow(impl::expected_access<Expected&>{}(*this));
    }

    /* Indirectly read the stored value, forwarding to its `->` operator if it exists,
    or directly returning its address otherwise.  If the expected is in an error state,
    then the error will be thrown as an exception. */
    [[nodiscard]] constexpr auto operator->() const
        noexcept (requires{
            {meta::to_arrow(impl::expected_access<const Expected&>{}(*this))} noexcept;
        })
        requires (requires{
            {meta::to_arrow(impl::expected_access<const Expected&>{}(*this))};
        })
    {
        return meta::to_arrow(impl::expected_access<const Expected&>{}(*this));
    }

    /* Empty expected monads always have a size of zero. */
    [[nodiscard]] static constexpr size_t size() noexcept { return 0; }

    /* Empty expected monads always have a size of zero. */
    [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }

    /* Empty expected monads are always empty. */
    [[nodiscard]] static constexpr bool empty() noexcept { return true; }

    /* Get a forward iterator over an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `end()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
        return impl::empty_iterator<NoneType>{};
    }

    /* Get a forward iterator over an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `end()`. */
    [[nodiscard]] constexpr auto cbegin() const noexcept {
        return impl::empty_iterator<NoneType>{};
    }

    /* Get a forward sentinel for an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `begin()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
        return impl::empty_iterator<NoneType>{};
    }

    /* Get a forward sentinel for an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `begin()`. */
    [[nodiscard]] constexpr auto cend() const noexcept {
        return impl::empty_iterator<NoneType>{};
    }

    /* Get a reverse iterator over an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `rend()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
        return std::make_reverse_iterator(self.end());
    }

    /* Get a reverse iterator over an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `rend()`. */
    [[nodiscard]] constexpr auto crbegin() const noexcept {
        return std::make_reverse_iterator(cend());
    }

    /* Get a reverse sentinel for an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `rbegin()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
        return std::make_reverse_iterator(self.begin());
    }

    /* Get a reverse sentinel for an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `rbegin()`. */
    [[nodiscard]] constexpr auto crend() const noexcept {
        return std::make_reverse_iterator(cbegin());
    }

    template <typename Self, typename... A>
    constexpr decltype(auto) operator()(this Self&& self, A&&... args)
        noexcept (meta::nothrow::force_visit<1, impl::Call, Self, A...>)
        requires (meta::force_visit<1, impl::Call, Self, A...>)
    {
        return (impl::visit<1>(
            impl::Call{},
            std::forward<Self>(self),
            std::forward<A>(args)...
        ));
    }

    template <typename Self, typename... K>
    constexpr decltype(auto) operator[](this Self&& self, K&&... keys)
        noexcept (meta::nothrow::force_visit<1, impl::Subscript, Self, K...>)
        requires (meta::force_visit<1, impl::Subscript, Self, K...>)
    {
        return (impl::visit<1>(
            impl::Subscript{},
            std::forward<Self>(self),
            std::forward<K>(keys)...
        ));
    }
};


/////////////////////////////////
////    MONADIC OPERATORS    ////
/////////////////////////////////


/* Pattern matching operator for union monads.  A visitor function must be provided on
the right hand side of this operator, which must be callable using all alternatives
of the monad on the left.  Nested monads will be recursively expanded into their
alternatives before attempting to invoke the visitor.

Similar to the built-in `->` indirection operator, this operator will attempt to
recursively call itself in order to match nested patterns involving other monads.
Namely, if an alternative `A` is not directly handled by the visitor `F`, and the
expression `A->*F` is valid, then the operator will fall back to that form.  This
means a single visitor can cover both direct and nested patterns, preferring the
former over the latter.  If neither are satisfied, then the operator will fail to
compile. */
template <meta::visit_monad T, typename F>
constexpr decltype(auto) operator->*(T&& val, F&& func)
    noexcept (meta::nothrow::force_visit_exhaustive<1, impl::visit_pattern<F>, F, T>)
    requires (meta::force_visit_exhaustive<1, impl::visit_pattern<F>, F, T>)
{
    return (impl::visit<1>(
        impl::visit_pattern<F>{},
        std::forward<F>(func),
        std::forward<T>(val)
    ));
}


/// NOTE: All other operators are conditionally supported for union types, but only if
/// the underlying type(s) also support them according to the semantics of the wrapper.
/// These all basically boil down to visitors that take advantage of the implicit
/// propagation semantics of `impl::visit`, meaning the visitor only needs to handle
/// the raw operation on the underlying types, and the rest is handled for free by the
/// same visitor logic as everything else.


template <meta::visit_monad T>
constexpr decltype(auto) operator!(T&& val)
    noexcept (meta::nothrow::force_visit<1, impl::LogicalNot, T>)
    requires (!meta::truthy<T> && meta::force_visit<1, impl::LogicalNot, T>)
{
    return (impl::visit<1>(impl::LogicalNot{}, std::forward<T>(val)));
}


template <typename L, typename R>
constexpr decltype(auto) operator&&(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::LogicalAnd, L, R>)
    requires ((
        (meta::visit_monad<L> && !meta::truthy<L>) ||
        (meta::visit_monad<R> && !meta::truthy<R>)
    ) && meta::force_visit<1, impl::LogicalAnd, L, R>)
{
    return (impl::visit<1>(
        impl::LogicalAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator||(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::LogicalOr, L, R>)
    requires ((
        (meta::visit_monad<L> && !meta::truthy<L>) ||
        (meta::visit_monad<R> && !meta::truthy<R>)
    ) && meta::force_visit<1, impl::LogicalOr, L, R>)
{
    return (impl::visit<1>(
        impl::LogicalOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator<(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Less, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Less, L, R>
    )
{
    return (impl::visit<1>(
        impl::Less{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator<=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::LessEqual, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::LessEqual, L, R>
    )
{
    return (impl::visit<1>(
        impl::LessEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator==(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Equal, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Equal, L, R>
    )
{
    return (impl::visit<1>(
        impl::Equal{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator!=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::NotEqual, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::NotEqual, L, R>
    )
{
    return (impl::visit<1>(
        impl::NotEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator>=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::GreaterEqual, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::GreaterEqual, L, R>
    )
{
    return (impl::visit<1>(
        impl::GreaterEqual{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator>(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Greater, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Greater, L, R>
    )
{
    return (impl::visit<1>(
        impl::Greater{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator<=>(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Spaceship, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Spaceship, L, R>
    )
{
    return (impl::visit<1>(
        impl::Spaceship{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <meta::visit_monad T>
constexpr decltype(auto) operator+(T&& val)
    noexcept (meta::nothrow::force_visit<1, impl::Pos, T>)
    requires (meta::force_visit<1, impl::Pos, T>)
{
    return (impl::visit<1>(impl::Pos{}, std::forward<T>(val)));
}


template <meta::visit_monad T>
constexpr decltype(auto) operator-(T&& val)
    noexcept (meta::nothrow::force_visit<1, impl::Neg, T>)
    requires (meta::force_visit<1, impl::Neg, T>)
{
    return (impl::visit<1>(impl::Neg{}, std::forward<T>(val)));
}


template <meta::visit_monad T>
constexpr decltype(auto) operator++(T&& val)
    noexcept (meta::nothrow::force_visit<1, impl::PreIncrement, T>)
    requires (meta::force_visit<1, impl::PreIncrement, T>)
{
    return (impl::visit<1>(impl::PreIncrement{}, std::forward<T>(val)));
}


template <meta::visit_monad T>
constexpr decltype(auto) operator++(T&& val, int)
    noexcept (meta::nothrow::force_visit<1, impl::PostIncrement, T>)
    requires (meta::force_visit<1, impl::PostIncrement, T>)
{
    return (impl::visit<1>(impl::PostIncrement{}, std::forward<T>(val)));
}


template <meta::visit_monad T>
constexpr decltype(auto) operator--(T&& val)
    noexcept (meta::nothrow::force_visit<1, impl::PreDecrement, T>)
    requires (meta::force_visit<1, impl::PreDecrement, T>)
{
    return (impl::visit<1>(impl::PreDecrement{}, std::forward<T>(val)));
}


template <meta::visit_monad T>
constexpr decltype(auto) operator--(T&& val, int)
    noexcept (meta::nothrow::force_visit<1, impl::PostDecrement, T>)
    requires (meta::force_visit<1, impl::PostDecrement, T>)
{
    return (impl::visit<1>(impl::PostDecrement{}, std::forward<T>(val)));
}


template <typename L, typename R>
constexpr decltype(auto) operator+(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Add, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Add, L, R>
    )
{
    return (impl::visit<1>(
        impl::Add{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator+=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceAdd, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceAdd, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceAdd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator-(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Subtract, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Subtract, L, R>
    )
{
    return (impl::visit<1>(
        impl::Subtract{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator-=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceSubtract, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceSubtract, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceSubtract{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator*(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Multiply, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Multiply, L, R>
    )
{
    return (impl::visit<1>(
        impl::Multiply{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator*=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceMultiply, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceMultiply, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceMultiply{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator/(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Divide, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Divide, L, R>
    )
{
    return (impl::visit<1>(
        impl::Divide{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator/=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceDivide, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceDivide, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceDivide{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator%(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::Modulus, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::Modulus, L, R>
    )
{
    return (impl::visit<1>(
        impl::Modulus{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator%=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceModulus, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceModulus, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceModulus{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator<<(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::LeftShift, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::LeftShift, L, R>
    )
{
    return (impl::visit<1>(
        impl::LeftShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator<<=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceLeftShift, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceLeftShift, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceLeftShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator>>(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::RightShift, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::RightShift, L, R>
    )
{
    return (impl::visit<1>(
        impl::RightShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator>>=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceRightShift, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceRightShift, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceRightShift{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <meta::visit_monad T>
constexpr decltype(auto) operator~(T&& val)
    noexcept (meta::nothrow::force_visit<1, impl::BitwiseNot, T>)
    requires (meta::force_visit<1, impl::BitwiseNot, T>)
{
    return (impl::visit<1>(impl::BitwiseNot{}, std::forward<T>(val)));
}


template <typename L, typename R>
constexpr decltype(auto) operator&(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::BitwiseAnd, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::BitwiseAnd, L, R>
    )
{
    return (impl::visit<1>(
        impl::BitwiseAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator&=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceBitwiseAnd, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceBitwiseAnd, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceBitwiseAnd{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator|(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::BitwiseOr, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::BitwiseOr, L, R>
    )
{
    return (impl::visit<1>(
        impl::BitwiseOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator|=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceBitwiseOr, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceBitwiseOr, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceBitwiseOr{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator^(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::BitwiseXor, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::BitwiseXor, L, R>
    )
{
    return (impl::visit<1>(
        impl::BitwiseXor{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


template <typename L, typename R>
constexpr decltype(auto) operator^=(L&& lhs, R&& rhs)
    noexcept (meta::nothrow::force_visit<1, impl::InplaceBitwiseXor, L, R>)
    requires (
        (meta::visit_monad<L> || meta::visit_monad<R>) &&
        meta::force_visit<1, impl::InplaceBitwiseXor, L, R>
    )
{
    return (impl::visit<1>(
        impl::InplaceBitwiseXor{},
        std::forward<L>(lhs),
        std::forward<R>(rhs)
    ));
}


}  // namespace bertrand


_LIBCPP_BEGIN_NAMESPACE_STD

    template <bertrand::meta::visit_monad T>
        requires (bertrand::meta::force_visit<1, bertrand::impl::Hash, T>)
    struct hash<T> {
        static constexpr auto operator()(bertrand::meta::as_const_ref<T> value)
            noexcept (bertrand::meta::nothrow::force_visit<
                1,
                bertrand::impl::Hash,
                bertrand::meta::as_const_ref<T>
            >)
            requires (bertrand::meta::force_visit<
                1,
                bertrand::impl::Hash,
                bertrand::meta::as_const_ref<T>
            >)
        {
            return bertrand::impl::visit<1>(
                bertrand::impl::Hash{},
                value
            );
        }
    };

    template <bertrand::meta::visit_monad T, typename Char>
        requires (bertrand::impl::alternatives_are_formattable<T, Char>)
    struct formatter<T, Char> {
    private:
        using str = std::basic_string_view<Char>;
        using parse_context = std::basic_format_parse_context<Char>;
        template <typename out>
        using format_context = std::basic_format_context<out, Char>;

        str m_fmt;

        struct fn {
            template <typename A, typename out>
                requires (!bertrand::meta::visit_monad<A> && std::formattable<A, Char>)
            static constexpr auto operator()(
                const A& value,
                format_context<out>& ctx,
                parse_context& parse_ctx
            ) {
                std::formatter<A, Char> fmt;
                auto it = fmt.parse(parse_ctx);
                return fmt.format(value, ctx);
            }
        };

    public:
        constexpr auto parse(parse_context& ctx) {
            auto it = ctx.begin();
            auto end = ctx.end();
            m_fmt = str(it, size_t(end - it));
            while (it != end && *it != '}') ++it;
            return it;
        }

        template <typename out>
        constexpr auto format(const T& value, format_context<out>& ctx) const {
            parse_context parse_ctx {m_fmt};
            return bertrand::impl::visit<1>(fn{}, value, ctx, parse_ctx);
        }
    };

    template <typename... Ts>
    struct variant_size<bertrand::impl::basic_union<Ts...>> :
        std::integral_constant<size_t, bertrand::impl::basic_union<Ts...>::types::size()>
    {};

    template <typename... Ts>
    struct variant_size<bertrand::Union<Ts...>> : std::integral_constant<
        size_t,
        bertrand::impl::visitable<bertrand::Union<Ts...>>::alternatives::size()
    > {};

    template <typename T>
    struct variant_size<bertrand::Optional<T>> : std::integral_constant<
        size_t,
        bertrand::impl::visitable<bertrand::Optional<T>>::alternatives::size()
    > {};

    template <typename T, typename... Es>
    struct variant_size<bertrand::Expected<T, Es...>> : std::integral_constant<
        size_t,
        bertrand::impl::visitable<bertrand::Union<T, Es...>>::alternatives::size()
    > {};

    template <size_t I, typename... Ts>
        requires (I < variant_size<bertrand::impl::basic_union<Ts...>>::value)
    struct variant_alternative<I, bertrand::impl::basic_union<Ts...>> {
        using type = bertrand::impl::basic_union<Ts...>::types::template at<I>;
    };

    template <size_t I, typename... Ts> requires (I < variant_size<bertrand::Union<Ts...>>::value)
    struct variant_alternative<I, bertrand::Union<Ts...>> {
        using type = bertrand::impl::visitable<
            bertrand::Union<Ts...>
        >::alternatives::template at<I>;
    };

    template <size_t I, typename T> requires (I < variant_size<bertrand::Optional<T>>::value)
    struct variant_alternative<I, bertrand::Optional<T>> {
        using type = bertrand::impl::visitable<
            bertrand::Optional<T>
        >::alternatives::template at<I>;
    };

    template <size_t I, typename T, typename... Es>
        requires (I < variant_size<bertrand::Expected<T, Es...>>::value)
    struct variant_alternative<I, bertrand::Expected<T, Es...>> {
        using type = bertrand::impl::visitable<
            bertrand::Union<T, Es...>
        >::alternatives::template at<I>;
    };

    template <size_t I, bertrand::meta::basic_union U>
    [[nodiscard]] constexpr decltype(auto) get(U&& u)
        noexcept (requires{{std::forward<U>(u).template get<I>()} noexcept;})
        requires (requires{{std::forward<U>(u).template get<I>()};})
    {
        return (std::forward<U>(u).template get<I>());
    }

    template <typename T, bertrand::meta::basic_union U>
    [[nodiscard]] constexpr decltype(auto) get(U&& u)
        noexcept (requires{{std::forward<U>(u).template get<T>()} noexcept;})
        requires (requires{{std::forward<U>(u).template get<T>()};})
    {
        return (std::forward<U>(u).template get<T>());
    }

    template <size_t I, bertrand::meta::basic_union U>
    [[nodiscard]] constexpr decltype(auto) get_if(U&& u)
        noexcept (requires{{std::forward<U>(u).template get_if<I>()} noexcept;})
        requires (requires{{std::forward<U>(u).template get_if<I>()};})
    {
        return (std::forward<U>(u).template get_if<I>());
    }

    template <typename T, bertrand::meta::basic_union U>
    [[nodiscard]] constexpr decltype(auto) get_if(U&& u)
        noexcept (requires{{std::forward<U>(u).template get_if<T>()} noexcept;})
        requires (requires{{std::forward<U>(u).template get_if<T>()};})
    {
        return (std::forward<U>(u).template get_if<T>());
    }

    template <bertrand::meta::Union T> requires (bertrand::impl::union_tuple<T>::enable)
    struct tuple_size<T> : std::integral_constant<
        size_t,
        bertrand::impl::union_tuple<T>::size
    > {};

    template <bertrand::meta::Optional T>
        requires (bertrand::meta::tuple_like<typename bertrand::impl::visitable<T>::value>)
    struct tuple_size<T> : std::integral_constant<
        size_t,
        bertrand::meta::tuple_size<typename bertrand::impl::visitable<T>::value>
    > {};

    template <bertrand::meta::Expected T>
        requires (bertrand::meta::tuple_like<typename bertrand::impl::visitable<T>::value>)
    struct tuple_size<T> : std::integral_constant<
        size_t,
        bertrand::meta::tuple_size<typename bertrand::impl::visitable<T>::value>
    > {};

    template <size_t I, bertrand::meta::Union T>
        requires (requires(T t) {{std::forward<T>(t).template get<I>()};})
    struct tuple_element<I, T> {
        using type = decltype((std::declval<T>().template get<I>()));
    };

    template <size_t I, bertrand::meta::Optional T>
        requires (requires(T t) {{std::forward<T>(t).template get<I>()};})
    struct tuple_element<I, T> {
        using type = decltype((std::declval<T>().template get<I>()));
    };

    template <size_t I, bertrand::meta::Expected T>
        requires (requires(T t) {{std::forward<T>(t).template get<I>()};})
    struct tuple_element<I, T> {
        using type = decltype((std::declval<T>().template get<I>()));
    };

    template <ssize_t I, bertrand::meta::Union T>
    constexpr decltype(auto) get(T&& t)
        noexcept (requires{{std::forward<T>(t).template get<I>()} noexcept;})
        requires (requires{{std::forward<T>(t).template get<I>()};})
    {
        return (std::forward<T>(t).template get<I>());
    }

    template <ssize_t I, bertrand::meta::Optional T>
    constexpr decltype(auto) get(T&& t)
        noexcept (requires{{std::forward<T>(t).template get<I>()} noexcept;})
        requires (requires{{std::forward<T>(t).template get<I>()};})
    {
        return (std::forward<T>(t).template get<I>());
    }

    template <ssize_t I, bertrand::meta::Expected T>
    constexpr decltype(auto) get(T&& t)
        noexcept (requires{{std::forward<T>(t).template get<I>()} noexcept;})
        requires (requires{{std::forward<T>(t).template get<I>()};})
    {
        return (std::forward<T>(t).template get<I>());
    }

    /// TODO: a CTAD guide from Union<Ts...> to variant<Ts...>.  This will require
    /// manual specialization for a given number of types, since CTAD doesn't allow
    /// unpacking variadic parameter packs in a deduction guide.

    template <bertrand::meta::Optional T>
    optional(T&&) -> optional<
        bertrand::meta::remove_reference<typename bertrand::impl::visitable<T>::value>
    >;

    template <bertrand::meta::Expected T>
    expected(T&&) -> expected<
        bertrand::meta::remove_reference<typename bertrand::impl::visitable<T>::value>,
        typename bertrand::impl::visitable<T>::errors::template eval<bertrand::meta::make_union>
    >;

_LIBCPP_END_NAMESPACE_STD


#endif  // BERTRAND_UNION_H
