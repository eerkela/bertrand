#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/shape.h"


/* A (Gentle) Introduction to Monads

    "A monad is a monoid in the category of endofunctors of some fixed category"
        - Category Theory (https://medium.com/@felix.kuehl/a-monad-is-just-a-monoid-in-the-category-of-endofunctors-lets-actually-unravel-this-f5d4b7dbe5d6)

Unless you already know what each of these words means, this definition is about as
clear as mud, so here's a better one:

    Monads (in the context of computer science) can be thought of as separate *domains*
    of values, in which results are *labeled* with the monad type, and compositions of
    monads return other monads in the same domain.

Intuitively, a monad is a kind of "sticky" wrapper around a value, which behaves
similarly to the value itself in most respects, but with extra properties that alter
its semantics in a way that is consistent across the domain.  It is a natural extension
of the Gang of Four's Decorator pattern, and is a fundamental abstraction in functional
programming languages, such as Haskell, Scala, Elixir, etc.

To decode this further, let's look at one of the most basic examples of a monad present
in nearly every language in some form: the `Optional` type:

```
    int x1;  // the default constructor for `int` is undefined => `x` uninitialized
    int y1 = x1 + 2;  // undefined behavior -> `y1` may contain garbage

    Optional<int> x2;  // Optional<> provides a default constructor => empty state
    Optional<int> y2 = x2 + 2;  // well-defined -> `y2` is also empty.  No computation takes place
```

Formally, `Optional<T>` is a monad that adds a universal `None` state to the domain of
`T`, which can be used to represent the absence of a value regardless of the specific
characteristics of `T`.  Operating on an `Optional<T>` is exactly like operating on `T`
itself, except that the empty state will be implicitly propagated through the
computation unless otherwise handled (via visitor-based pattern matching).  Thus, any
algorithm that operates on `T` should also be able to operate on `Optional<T>` without
significant changes, and the result will simply not be computed if the input is in the
empty state.  Such operations can therefore be chained together without needing to
check for the empty state at every step, leading to more concise and readable syntax,
which better models the underlying problem space.

Note that `Optional`s are simply one example of a monad, and are actually just a
special case of `Union`, where the first type is the empty state.  In mathematical type
theory, monads of this form are categorized as "sum types": composites that can hold
**one** (and only one) of several possible types, where the overall space of types is
given by the disjunction (logical OR) between every alternative.  Bertrand's
`Union<Ts...>` monad generalizes this to any choice of `Ts...`, providing a type-safe,
monadic way of representing values with indeterminate type in statically-typed
languages such as C++.  Just as with `Optional`s, operations on a `Union` will be
forwarded to the active member, possibly returning a new `Union` if the alternatives
support the operation and the result type is ambiguous.  This allows Bertrand to
simulate a limited form of dynamic typing directly within C++, where the exact type of
an object may not be known until runtime, but the overall set of possible types can be
deduced at compile time and verified on that basis.  The compiler will simply emit a
small dispatch table (which usually amounts to a handful of `if` statements) whenever
the monad is operated on, ensuring that overhead is minimal, predictable, and
significantly lower than full dynamic typing in languages like Python and JavaScript,
while still allowing a semantic bridge between both worlds.

Sometimes, it is beneficial to explicitly leave a monadic domain and unwrap the raw
value(s), which can be done in a number of ways.  First, all monads support both
implicit and explicit conversion to arbitrary types as long as each alternative can be
converted to that type in turn.  This generates a "projection" from the monadic domain
to that of the destination type, which is the logical inversion of the monad's
constructor.  Just like all other operations, this is performed via a dispatch table
that selects the active alternative and converts it accordingly.  One can thus write:

```
    Union<int, double> u = 3.14;
    double d = u;  // int -> double || double -> double
```

... but not:

```
    Union<int, std::string> u = 42;
    double d = u;  // int -> double || std::string -> double (invalid)
```

This also enables fluent conversions to and from other monadic types, including
standard library equivalents like `std::variant`, `std::optional`, and `std::expected`,
possibly even in nested form.  For example:

```
    Union u = std::variant<std::optional<int>, double>{2.0};  // Union<std::nullopt_t, int, double>
    std::variant<std::nullopt_t, int, double> v = u;
    Optional o = u;  // Optional<Union<const int&, const double&>>
```

Note the use of CTAD to initialized `u`, which deduces the correct alternative types
from the nested `std::variant` and `std::optional`, using `std::nullopt` as the empty
alternative.  This produces a flattened union containing just the 3 primitive types,
which can then be converted to `v`, an equivalent `std::variant`.  Similarly, `u` can
also be converted to another Bertrand monad,`Optional` in this case (again using CTAD),
which generates a nested monad that treats `std::nullopt` as an empty state and
generates a union with the remaining, perfectly-forwarded types.

Additionally, monads support pattern matching via the `->*` operator, as is commonly
used by functional languages to destructure algebraic types into their constituent
parts.  Here's a simple example:

```
    using A = std::array<double, 3>;
    using B = std::string;
    using C = std::vector<bool>;

    Union<A, B, C> u = "Hello, World!";  // 13 characters
    size_t n = u ->* def{
        [](const A& a) { return a.size(); },
        [](const B& b) { return b.size(); },
        [](const C& c) { return c.size(); }
    };
    assert(n == 13);
    assert(n == u.size());  // (!)
```

Note that the `assert` in the last line only compiles because all 3 alternatives
support the `size()` operator, meaning that `u.size()` is well-formed and returns a
type comparable to `size_t`.  The pattern matching expression `u ->* def{...}` is not
so encumbered, and will compile as long as all alternatives are (unambiguously)
handled, regardless of each case's return type or internal logic.  If multiple cases
return different types (including `void`), then the overall return type may be promoted
to a new `Union`, `Optional`, or `Expected` monad as needed, with canonical nesting
(e.g. `Expected<Optional<Union<Ts...>>, Es...>`).  This pattern requires a similar
dispatch table to all other operations, but allows for arbitrary logic to be executed
for each alternative, rather than forwarding a predetermined operation.  The `->*`
operator is much more powerful than shown in this example, and also supports partial
matches for `Optional` and `Expected` operands, where only the non-empty and non-error
states are handled explicitly, as well as automatic recursion for each alternative in
the case of nested monads, until either the visitor function becomes callable or all
monads are exhausted.  See the documentation for that operator for more details.

Besides the `->*` operator, all Bertrand `def` functions will also automatically
perform pattern matching when called with monadic arguments, allowing users to visit
multiple monads simultaneously without any extra syntax.  As long as the underlying
function is callable with either the monad itself or each of its alternatives (possibly
recurring for nested monads), the function will compile, and will emit a familiar
dispatch table to select the correct alternative(s) at runtime.  This furthers the
illusion of dynamic typing, since any collection of statically-typed C++ functions can
be trivially converted into an equivalent visitor that accepts monadic arguments, as
well as simulating C++-style function overloading in unmangled languages that do not
normally permit it, such as Python.  For example:

```
def func{
    [] (int x) { return x + 1; },
    [] (const std::string& s) { return s + "!"; }
};  // -> Union<int, std::string>

Union<int, std::string> u = "Hello, World";
print(func(u));  // >>> "Hello, World!"
u = 41;
print(func(u));  // >>> 42
```

Lastly, some monads, including `Optional` and `Expected` also support pointer-like
dereference operators, which trivially map from the monadic domain to the underlying
type, assuming the monad is not in an empty or error state.  This means that
`Optional<T>` (and particularly `Optional<T&>`) can be used to model pointers, which is
useful when integrating with languages that do not normally expose them to the user,
including Python.  In fact, optional references are literally reduced to pointers in
the underlying implementation, with the only change being that they forward all
operations to the referenced value, rather than exposing pointer arithmetic or similar
operations (which may be error-prone and potentially insecure) to the user.
`Union<Ts...>` also support the same operators, but only if all alternatives share a
common type, and will fail to compile otherwise.

There are also other monads not covered here, such as `range<C>`, which extend these
monadic principles to so-called "product types", where the overall space of types is
given by the conjunction (logical AND) of each alternative.  Monads can also be useful
when modeling operations across time, as is the case for `async<F, A...>` as well as
`def` itself, which represent "continuation" monads.  Operating on such a monad extends
it with one or more continuation functions, which will be executed immediately after
the original function completes, in so-called "continuation-passing style".  This is
especially useful for asynchronous programming, where continuations allow users to
chain operations in a more intuitive and type-safe manner, without interrupting their
execution.  See the documentation of these types for more details on their specific
behavior and how it relates to Bertrand's overall monad ecosystem.

One of the most powerful features of monads is their composability.  There is nothing
inherently wrong with nested structures like optional unions, or unions of optionals,
or async ranges of expected tuples, for example.  They will all be treated
symmetrically during pattern matching and monadic operations, recurring into each layer
automatically until a valid permutation is found.  These kinds of transformations keep
the monadic interface clean and predictable, with no extra boilerplate or special
syntax, leading to simpler code that is easier to reason about, maintain, and
generalize to other languages, regardless of their capabilities.
*/


namespace bertrand {


namespace impl {

    /* Provides an extensible mechanism for controlling the dispatching behavior of
    the `meta::visit` concept(s) and `impl::visit()` operator for a given type `T`.
    Users can specialize this structure to extend those utilities to arbitrary types,
    by providing the following information:
    
        -   `enable`: a boolean which must be set to `true` for all custom
            specializations.  Controls the output of the `meta::visitable<T>` concept.
        -   `monad`: a boolean which exposes type `T` to bertrand's monadic operator
            interface.  This can be checked via the `meta::visit_monad<T>` concept,
            which all monadic operators are constrained with.
        -   `type`: an alias to the type being visited, which must be identical to `T`,
            preserving cvref qualifications.
        -   `alternatives`: a `meta::pack<Ts...>` with one or more types, all of which
            represent alternatives that the monad can dispatch to.  These must be
            perfectly-forwarded from the visitable type `T`, retaining all its cvref
            qualifications.
        -   `lookup`: a `meta::pack<Ts...>` containing the same types as `alternatives`
            (in the same order), but with the same cvref qualifications as were used to
            specialize `T`.  This is used to enable monadic comparisons against
            `bertrand::type<U>`, which effectively replace a traditional
            `holds_alternative<U>()` method, and reduces to a comparison between the
            active index and that of `U` within this pack (assuming it is present).
        -   `empty`: the type of the `None` state for this monad, or `void` if the
            monad does not model an empty state.  If this is not `void`, then it
            signals that the monad acts like an optional within the dispatch logic,
            which can trigger automatic propagation of the empty state and/or
            canonicalization of `T` to `bertrand::Optional<T>` when appropriate.  Just
            like `alternatives`, this must be perfectly forwarded from `T`, retaining
            all cvref qualifications.
        -   `errors`: a `meta::pack<Es...>` with zero or more error types, which
            represent the possible error states that the monad models.  If this is
            non-empty, then it signals that the monad acts like an expected within the
            dispatch logic, which can trigger automatic propagation of the error state
            and/or canonicalization of `T` to `bertrand::Expected<T, Es...>` when
            appropriate.  Again, this must be perfectly forwarded from `T`, retaining
            all cvref qualifications.
        -   `values`: A filtered `meta::pack<Ts...>` containing only the types in
            `alternatives` that are not `empty` or in `errors`.  These types will
            normalize to `bertrand::Union<Ts...>` if there are multiple such types,
            after concatenating the results from all visited monads and filtering for
            uniqueness.  This may be an empty pack if all alternatives are either empty
            or error states.
        -   `index(T)`: a static method that returns the monad's active index, aligned
            to the given `alternatives`.  This is used to determine which branch to
            dispatch to when the monad is destructured within `impl::visit()`.
        -   `get<I>(T)`: a static method that takes a compile-time index `I` as a
            template parameter, and casts the monad to the `I`-th type in
            `alternatives` (including cvref qualifications).  The output from this is
            what gets passed into the visitor function, completing the dispatch.

    Built-in specializations are provided for all qualifications of:

        -   `void`
        -   `bertrand::NoneType`
        -   `std::nullopt_t`
        -   `std::nullptr_t`
        -   `bertrand::Union<Ts...>`
        -   `bertrand::Optional<T>`
        -   `bertrand::Expected<T, Es...>`
        -   `std::variant<Ts...>`
        -   `std::optional<T>`
        -   `std::expected<T, E>`

    The default specialization covers all other non-monad types, which will be passed
    through without dispatching.

    Note that the type `T` may be arbitrarily-qualified, and always matches the
    qualifications of the observed type at the point where `impl::visit()` is called.
    It may therefore be useful to define all aliases in terms of `decltype()`
    reflections, which should always forward the qualifications of the input type,
    otherwise the visit logic may not work as intended. */
    template <typename T>
    struct visitable {
        static constexpr bool enable = false;
        static constexpr bool monad = false;
        using type = T;
        using lookup = meta::pack<T>;
        using alternatives = meta::pack<T>;
        using empty = void;
        using errors = meta::pack<>;
        using values = meta::pack<T>;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T>) noexcept {
            return 0;
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> x) noexcept {
            return (std::forward<T>(x));
        }
    };
    template <meta::is_void T>
    struct visitable<T> {
        static constexpr bool enable = false;  // meta::visitable is false
        static constexpr bool monad = false;
        using type = T;
        using lookup = meta::pack<T>;
        using alternatives = meta::pack<>;
        using empty = T;  // no empty state
        using errors = meta::pack<>;  // no error states
        using values = meta::pack<>;  // no value states
    };
    template <meta::is<NoneType> T>
    struct visitable<T> {
        static constexpr bool enable = false;
        static constexpr bool monad = false;
        using type = T;
        using lookup = meta::pack<T>;
        using alternatives = meta::pack<T>;
        using empty = T;  // empty state is NoneType
        using errors = meta::pack<>;  // no error states
        using values = meta::pack<>;  // no value states

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T>) noexcept {
            return 0;
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> x) noexcept {
            return (std::forward<T>(x));
        }
    };
    template <meta::is<std::nullopt_t> T>
    struct visitable<T> {
        static constexpr bool enable = false;  // meta::visitable is false
        static constexpr bool monad = false;
        using type = T;
        using lookup = meta::pack<T>;
        using alternatives = meta::pack<T>;
        using empty = T;  // empty state is std::nullopt_t
        using errors = meta::pack<>;  // no error states
        using values = meta::pack<>;  // no value states

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T>) noexcept {
            return 0;
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> x) noexcept {
            return (std::forward<T>(x));
        }
    };
    template <meta::is<std::nullptr_t> T>
    struct visitable<T> {
        static constexpr bool enable = false;  // meta::visitable is false
        static constexpr bool monad = false;
        using type = T;
        using lookup = meta::pack<T>;
        using alternatives = meta::pack<T>;
        using empty = T;  // empty state is std::nullptr_t
        using errors = meta::pack<>;  // no error states
        using values = meta::pack<>;  // no value states

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T>) noexcept {
            return 0;
        }

        template <size_t I> requires (I < alternatives::size())
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> x) noexcept {
            return (std::forward<T>(x));
        }
    };

}


namespace meta {

    /* True for types that have a custom `impl::visitable<T>` specialization. */
    template <typename T>
    concept visitable = impl::visitable<T>::enable;

    /* True for types where `impl::visitable<T>::monad` is set to true. */
    template <typename T>
    concept visit_monad = impl::visitable<T>::monad;

    namespace detail::visit {

        /* Decomposing an arbitrary type begins with a pre-pass that recursively
        collects all the empty and error states ahead of time (converting `void` to
        `NoneType`). */
        template <typename T, typename = meta::pack<>>
        struct prepass {
            using optionals = ::std::conditional_t<
                ::std::same_as<T, typename impl::visitable<T>::empty>,
                meta::pack<::std::conditional_t<meta::is_void<T>, NoneType, T>>,
                meta::pack<>
            >;
            using errors = impl::visitable<T>::errors;
        };
        template <meta::visitable T, typename skip> requires (!skip::template contains<T>())
        struct prepass<T, skip> {
            template <typename... Ts>
            struct _collect {
                using optionals = meta::concat<
                    ::std::conditional_t<
                        meta::not_void<typename impl::visitable<T>::empty>,
                        meta::pack<typename impl::visitable<T>::empty>,
                        meta::pack<>
                    >,
                    typename prepass<Ts, meta::append<skip, T>>::optionals...
                >;
                using errors = meta::concat<
                    typename impl::visitable<T>::errors,
                    typename prepass<Ts, meta::append<skip, T>>::errors...
                >;
            };
            using collect = impl::visitable<T>::values::template eval<_collect>;
            using optionals = collect::optionals;
            using errors = collect::errors;
        };

        /* Once the pre-pass is complete, the remaining types that are not present in
        the empty or error states will be identified as values, so that they always
        remain disjoint overall. */
        template <typename T, typename optionals, typename errors, typename skip = meta::pack<>>
        struct _decompose {
            using values = ::std::conditional_t<
                (
                    meta::is_void<T> ||
                    optionals::template contains<meta::remove_rvalue<T>>() ||
                    errors::template contains<meta::remove_rvalue<T>>()
                ),
                meta::pack<>,
                meta::pack<meta::remove_rvalue<T>>
            >;
            using visited = skip;
        };
        template <meta::visitable T, typename optionals, typename errors, typename skip>
            requires (!skip::template contains<T>())
        struct _decompose<T, optionals, errors, skip> {
            template <typename... Ts>
            struct filter {
                using values = meta::concat<typename _decompose<
                    Ts,
                    optionals,
                    errors,
                    meta::append<skip, T>
                >::values...>;
                using visited = meta::concat<typename _decompose<
                    Ts,
                    optionals,
                    errors,
                    meta::append<skip, T>
                >::visited...>;
            };
            using values = impl::visitable<T>::values::template eval<filter>::values;
            using visited = impl::visitable<T>::values::template eval<filter>::visited;
        };
        template <typename T, typename Optionals = meta::pack<>, typename Errors = meta::pack<>>
        struct decompose {
            using optionals = meta::concat<Optionals, typename prepass<T>::optionals>::
                template map<meta::remove_rvalue>::
                template eval<meta::to_unique>;
            using errors = meta::concat<Errors, typename prepass<T>::errors>::
                template map<meta::remove_rvalue>::
                template eval<meta::to_unique>;
            using values = _decompose<T, optionals, errors>::values::
                template eval<meta::to_unique>;
            using visited = _decompose<T, optionals, errors>::visited::
                template eval<meta::to_unique>;
        };

        /* If several types are decomposed at once, their optional, error, and value
        states will be concatenated together and filtered for global uniqueness.  This
        is how monadic unions detect and flatten nested monads, which is also the basis
        for CTAD guides. */
        template <typename Ts, typename Optionals = meta::pack<>, typename Errors = meta::pack<>>
        struct flatten;
        template <typename... Ts, typename Optionals, typename Errors>
        struct flatten<meta::pack<Ts...>, Optionals, Errors> {
            using optionals = meta::concat<Optionals, typename prepass<Ts>::optionals...>::
                template map<meta::remove_rvalue>::
                template eval<meta::to_unique>;
            using errors = meta::concat<Errors, typename prepass<Ts>::errors...>::
                template map<meta::remove_rvalue>::
                template eval<meta::to_unique>;
            using values = meta::concat<typename _decompose<Ts, optionals, errors>::values...>::
                template eval<meta::to_unique>;
        };

        template <typename>
        struct to_union { using type = void; };

        template <typename R, typename>
        struct to_optional { using type = R; };

        template <typename R, typename>
        struct to_expected { using type = R; };

    }

}


namespace impl {

    template <typename... Ts>
    using union_flatten = meta::concat<
        typename meta::detail::visit::flatten<meta::pack<Ts...>>::optionals,
        typename meta::detail::visit::flatten<meta::pack<Ts...>>::values,
        typename meta::detail::visit::flatten<meta::pack<Ts...>>::errors
    >;
    template <typename... Ts>
    concept union_concept =
        ((meta::not_void<Ts> && meta::not_rvalue<Ts>) && ...) &&
        meta::unique<Ts...> &&
        union_flatten<Ts...>::size() > 1;

    template <typename T, typename... Es>
    using _expected_flatten = meta::detail::visit::decompose<T, meta::pack<>, meta::concat<
        typename meta::detail::visit::flatten<meta::pack<Es...>>::optionals,
        typename meta::detail::visit::flatten<meta::pack<Es...>>::values,
        typename meta::detail::visit::flatten<meta::pack<Es...>>::errors
    >>;
    template <typename T, typename... Es>
    using expected_flatten = meta::concat<
        meta::pack<
            typename meta::detail::visit::to_optional<
                typename meta::detail::visit::to_union<
                    typename _expected_flatten<T, Es...>::values
                >::type,
                typename _expected_flatten<T, Es...>::optionals
            >::type
        >,
        typename _expected_flatten<T, Es...>::errors
    >;
    template <typename T, typename... Es>
    concept expected_concept =
        meta::not_rvalue<T> &&
        ((meta::not_void<Es> && meta::not_rvalue<Es>) && ...) &&
        meta::unique<Es...> &&
        expected_flatten<T, Es...>::size() > 1;

}


template <typename... Ts> requires (impl::union_concept<Ts...>)
struct Union;


template <meta::not_rvalue T = void>
struct Optional;


template <typename T, typename... Es> requires (impl::expected_concept<T, Es...>)
struct Expected;


/* A helper that produces a `std::in_place_index_t` instance specialized for a given
alternative.  This is meant to be used as a disambiguation tag for monadic `Union`,
`Optional`, and `Expected` constructors, which manually select an alternative to
initialize.  It can be followed by any number of arguments, which will be forwarded to
that alternative's constructor in turn.

The same tag can also be used during monadic comparisons to check against the active
alternative of a `Union`, `Optional`, `Expected`, or similar type.  For example:

```
    Union<int, double, std::string> u = 3.14;
    if (u == alternative<1>) {  // true, since `double` is at index 1
        print("u holds a double!");
    }
```
*/
template <size_t I>
constexpr std::in_place_index_t<I> alternative;


/* A helper that produces a `std::type_identity` instance specialized for type `T`.
Instances of this form can be supplied as `auto` template parameters in order to mix
values and types within the same argument list.

Additionally, the generated type identity can be used as a disambiguation tag for
monadic `Union`, `Optional`, and `Expected` constructors, which manually select an
alternative to initialize.  It can be followed by any number of arguments, which will
be perfectly forwarded to that alternative's constructor.  If `T` is visitable, then
all of its flattened alternatives must be present, and the conversion constructor from
`T` will be used to initialize the monad.

Lastly, the same tag can also be used during monadic comparisons to check against the
active alternative of a `Union`, `Optional`, `Expected`, or similar type.  For example:

```
    Union<int, double, std::string> u = 3.14;
    if (u == type<double>) {  // true, since `double` is the active alternative
        print("u holds a double!");
    }
```

Similar to the constructors, if `T` is visitable, then all of its flattened
alternatives must be present, and the result of the comparison will indicate whether
any of them are currently active.  If not, and the active index is strictly less or
greater than all of the flattened alternatives, then that result will be returned as a
`std::partial_ordering` tag.  Otherwise, the result will be
`std::partial_ordering::unordered`. */
template <typename T>
constexpr std::type_identity<T> type;


/* A helper tag that forces an `Expected` constructor to exclude the value state during
initialization.  Any number of arguments may be given after this tag, which will be
subjected to the same rules as the standard `Expected` constructors, as if the tag were
not present.  The only difference is that the value state will not be considered as a
valid alternative for the purpose of overload resolution.

The same tag can also be used during monadic comparisons to check whether an `Expected`
is currently in an error state, and can be invoked as a function to extract that state.
For example:

```
    Expected<int, std::string> e = "File not found";
    if (e == unexpected) {  // true, since `e` is in an error state
        print("An error occurred: {}", unexpected(e, type<std::string>));
    }
```

If there is more than one possible error type, the extracted error will be returned as
a `Union` of the possible types. */
inline constexpr struct Unexpected {
private:
    template <typename... Es>
    struct _type { using type = bertrand::Union<Es...>; };
    template <typename E>
    struct _type<E> { using type = E; };
    template <typename T>
    using type = typename impl::visitable<T>::errors
        ::template map<meta::remove_rvalue>
        ::template eval<meta::to_unique>
        ::template eval<_type>
        ::type;

    template <size_t I>
    struct fn {
        template <typename T>
        static constexpr type<T> operator()(T&& exp)
            noexcept (impl::visitable<T>::errors::template contains<
                typename impl::visitable<T>::alternatives::template at<I>
            >() && requires{{
                impl::visitable<T>::template get<I>(std::forward<T>(exp))
            } noexcept -> meta::nothrow::convertible_to<type<T>>;})
            requires (!impl::visitable<T>::errors::template contains<
                typename impl::visitable<T>::alternatives::template at<I>
            >() || requires{{
                impl::visitable<T>::template get<I>(std::forward<T>(exp))
            } -> meta::convertible_to<type<T>>;})
        {
            if constexpr (impl::visitable<T>::errors::template contains<
                typename impl::visitable<T>::alternatives::template at<I>
            >()) {
                return impl::visitable<T>::template get<I>(std::forward<T>(exp));
            } else {
                throw TypeError("Expected does not contain an error");
            }
        }
    };

public:
    template <typename T> requires (!impl::visitable<T>::errors::empty())
    [[nodiscard]] static constexpr type<T> operator()(T&& exp)
        noexcept (requires{{impl::basic_vtable<fn, impl::visitable<T>::alternatives::size()>{
            impl::visitable<T>::index(std::forward<T>(exp))
        }(std::forward<T>(exp))} noexcept;})
        requires (requires{{impl::basic_vtable<fn, impl::visitable<T>::alternatives::size()>{
            impl::visitable<T>::index(std::forward<T>(exp))
        }(std::forward<T>(exp))};})
    {
        return impl::basic_vtable<fn, impl::visitable<T>::alternatives::size()>{
            impl::visitable<T>::index(std::forward<T>(exp))
        }(std::forward<T>(exp));
    }
} unexpected;


namespace meta {

    /* True for any specialization of `bertrand::Union`. */
    template <typename T>
    concept Union = specialization_of<T, bertrand::Union>;

    /* True for any specialization of `bertrand::Optional`. */
    template <typename T>
    concept Optional = specialization_of<T, bertrand::Optional>;

    /* True for any specialization of `bertrand::Expected`. */
    template <typename T>
    concept Expected = specialization_of<T, bertrand::Expected>;

    namespace detail::visit {

        ///////////////////////////
        ////    PERMUTATION    ////
        ///////////////////////////

        /* The permutation algorithm works by tracking a minimum and maximum visit
        budget, as well as a partial argument list split between the processed and
        unprocessed portions.  The argument under consideration will always be the
        first argument of the suffix, and will always be visitable, skipping over any
        non-visitable arguments.  This base specialization represents an invalid
        permutation. */
        template <
            typename F,  // function to invoke
            size_t force,  // remaining number of mandatory visits along this path
            size_t budget,  // remaining visit budget (strictly >= `force`)
            typename prefix,  // processed arguments
            typename suffix  // unprocessed arguments (starting at first visitable)
        >
        struct permute {
            static constexpr bool enable = false;
            using returns = meta::pack<>;
            using optionals = meta::pack<>;
            using errors = meta::pack<>;
        };

        /* Valid permutations record the result type for this permutation for later
        analysis and expose a call operator templated on the overall, deduced return
        type, which represents a terminal call to the visitor function. */
        template <typename F, size_t force, size_t budget, typename... prefix, typename... suffix>
            requires (force == 0 && meta::callable<F, prefix..., suffix...>)
        struct permute<F, force, budget, meta::pack<prefix...>, meta::pack<suffix...>> {
            static constexpr bool enable = true;
            using returns = meta::pack<meta::call_type<F, prefix..., suffix...>>;
            using optionals = meta::pack<>;
            using errors = meta::pack<>;

            template <typename R>
            struct fn {
                using type = R;
                [[gnu::always_inline]] static constexpr R operator()(
                    meta::forward<F> func,
                    meta::forward<prefix>... pre,
                    meta::forward<suffix>... suf
                )
                    noexcept (requires{{::std::forward<F>(func)(
                        ::std::forward<prefix>(pre)...,
                        ::std::forward<suffix>(suf)...
                    )} noexcept -> meta::nothrow::convertible_to<R>;})
                {
                    return ::std::forward<F>(func)(
                        ::std::forward<prefix>(pre)...,
                        ::std::forward<suffix>(suf)...
                    );
                }
            };
        };

        /* In the event of a substitution failure, `skip` computes the required
        `prefix...` and `suffix...` packs to specialize the next `permute` helper,
        skipping over non-visitable arguments.  This limits overall template depth and
        reduces the number of unique instantiations needed to evaluate the algorithm. */
        template <typename P, typename... S>
        struct skip {
            using prefix = P;
            using suffix = meta::pack<S...>;
        };
        template <typename... P, typename A, typename... S> requires (!meta::visitable<A>)
        struct skip<meta::pack<P...>, A, S...> : skip<meta::pack<P..., A>, S...> {};

        /* `substitute` will attempt to replace a visitable argument with each of its
        alternatives, and recursively continue the algorithm with a reduced budget.  If
        any permutations are invalid and do not represent empty or error states that
        can be trivially propagated, then the substitution fails and the algorithm will
        continue to a future argument using `skip`, without changing the current
        budget.  Otherwise, the return types and monadic components from each
        alternative will be collected and merged. */
        template <typename F, size_t, size_t, typename prefix, typename alt, typename suffix>
        struct substitute {  // invalid, nontrivial alternative
            static constexpr bool enable = false;
            using returns = meta::pack<>;
            using optionals = meta::pack<>;
            using errors = meta::pack<>;
        };
        template <
            typename F,
            size_t force,
            size_t budget,
            typename prefix,
            meta::not_pack alt,
            meta::visitable curr,
            typename... suffix
        >
            requires (permute<
                F,
                force == 0 ? 0 : force - 1,
                budget - 1,
                prefix,
                meta::pack<alt, suffix...>
            >::enable)
        struct substitute<  // valid alternative - skip forward
            F,
            force,
            budget,
            prefix,
            alt,
            meta::pack<curr, suffix...>
        > : permute<
            F,
            force == 0 ? 0 : force - 1,
            budget - 1,
            typename skip<prefix, alt, suffix...>::prefix,
            typename skip<prefix, alt, suffix...>::suffix
        > {};
        template <
            typename F,
            size_t force,
            size_t budget,
            typename prefix,
            meta::not_pack alt,
            meta::visitable curr,
            typename... suffix
        >
            requires (
                !permute<
                    F,
                    force == 0 ? 0 : force - 1,
                    budget - 1,
                    prefix,
                    meta::pack<alt, suffix...>
                >::enable &&
                ::std::same_as<alt, typename impl::visitable<curr>::empty>
            )
        struct substitute<  // invalid alternative, trivial empty state
            F,
            force,
            budget,
            prefix,
            alt,
            meta::pack<curr, suffix...>
        > {
            static constexpr bool enable = true;
            using returns = meta::pack<>;
            using optionals = meta::pack<meta::remove_rvalue<alt>>;
            using errors = meta::pack<>;

            /* If an empty state if left unhandled by the visitor, it will be converted
            into `None` to initialize the resulting `Optional`. */
            template <typename R>
            struct fn {
                using type = R;
                template <typename... A>
                [[gnu::always_inline]] static constexpr R operator()(
                    meta::forward<F> func,
                    A&&... args
                ) noexcept {
                    /// NOTE: R will always be either an Optional<T> or
                    /// Expected<Optional<T>, Es...> in this context.
                    return {};
                }
            };
        };
        template <
            typename F,
            size_t force,
            size_t budget,
            typename prefix,
            meta::not_pack alt,
            meta::visitable curr,
            typename... suffix
        >
            requires (
                !permute<
                    F,
                    force == 0 ? 0 : force - 1,
                    budget - 1,
                    prefix,
                    meta::pack<alt, suffix...>
                >::enable &&
                !::std::same_as<alt, typename impl::visitable<curr>::empty> &&
                impl::visitable<curr>::errors::template contains<alt>()
            )
        struct substitute<  // invalid alternative, trivial error state
            F,
            force,
            budget,
            prefix,
            alt,
            meta::pack<curr, suffix...>
        > {
            static constexpr bool enable = true;
            using returns = meta::pack<>;
            using optionals = meta::pack<>;
            using errors = meta::pack<meta::remove_rvalue<alt>>;

            /* If an error state is left unhandled by the visitor, it will be
            perfectly-forwarded to the return type. */
            template <typename R>
            struct fn {
                using type = R;
                template <typename... A>
                [[gnu::always_inline]] static constexpr R operator()(
                    meta::forward<F> func,
                    A&&... args
                ) noexcept (meta::nothrow::constructible_from<
                    R,
                    bertrand::Unexpected,
                    meta::unpack_type<prefix::size(), A...>
                >) {
                    /// NOTE: R will always be an Expected<T, Es...> in this context.
                    return {
                        bertrand::unexpected,
                        meta::unpack_arg<prefix::size()>(::std::forward<A>(args)...)
                    };
                }
            };
        };
        template <
            typename F,
            size_t force,
            size_t budget,
            typename... prefix,
            typename... alts,
            meta::visitable curr,
            typename... suffix
        >
            requires (substitute<
                F,
                force,
                budget,
                meta::pack<prefix...>,
                alts,
                meta::pack<curr, suffix...>
            >::enable && ...)
        struct substitute<  // valid overall, merge results
            F,
            force,
            budget,
            meta::pack<prefix...>,
            meta::pack<alts...>,
            meta::pack<curr, suffix...>
        > {
            static constexpr bool enable = true;
            using returns = meta::concat<typename substitute<
                F,
                force,
                budget,
                meta::pack<prefix...>,
                alts,
                meta::pack<curr, suffix...>
            >::returns...>;
            using optionals = meta::concat<typename substitute<
                F,
                force,
                budget,
                meta::pack<prefix...>,
                alts,
                meta::pack<curr, suffix...>
            >::optionals...>;
            using errors = meta::concat<typename substitute<
                F,
                force,
                budget,
                meta::pack<prefix...>,
                alts,
                meta::pack<curr, suffix...>
            >::errors...>;

            /* Invoking a valid substitution performs the dispatch necessary to
            access the active alternative, and then forwards to the proper scalar
            `substitute` specialization listed above.  Those specializations expose
            their own call operators, which may either return early, call the function,
            or recur to another substitution, as necessary. */
            template <typename R>
            struct fn {
                using type = R;
                template <size_t I>
                struct dispatch {
                    static constexpr R operator()(
                        meta::forward<F> func,
                        meta::forward<prefix>... pre,
                        meta::forward<curr> v,
                        meta::forward<suffix>... suf
                    )
                        noexcept (requires{{typename substitute<
                            F,
                            force,
                            budget,
                            meta::pack<prefix...>,
                            meta::unpack_type<I, alts...>,
                            meta::pack<curr, suffix...>
                        >::template fn<R>{}(
                            ::std::forward<F>(func),
                            ::std::forward<prefix>(pre)...,
                            impl::visitable<curr>::template get<I>(::std::forward<curr>(v)),
                            ::std::forward<suffix>(suf)...
                        )} noexcept;})
                    {
                        return typename substitute<
                            F,
                            force,
                            budget,
                            meta::pack<prefix...>,
                            meta::unpack_type<I, alts...>,
                            meta::pack<curr, suffix...>
                        >::template fn<R>{}(
                            ::std::forward<F>(func),
                            ::std::forward<prefix>(pre)...,
                            impl::visitable<curr>::template get<I>(::std::forward<curr>(v)),
                            ::std::forward<suffix>(suf)...
                        );
                    }
                };

                [[gnu::always_inline]] static constexpr R operator()(
                    meta::forward<F> func,
                    meta::forward<prefix>... pre,
                    meta::forward<curr> v,
                    meta::forward<suffix>... suf
                )
                    noexcept (requires{{impl::basic_vtable<
                        dispatch,
                        impl::visitable<curr>::alternatives::size()
                    >{impl::visitable<curr>::index(v)}(
                        ::std::forward<F>(func),
                        ::std::forward<prefix>(pre)...,
                        ::std::forward<curr>(v),
                        ::std::forward<suffix>(suf)...
                    )} noexcept;})
                {
                    return impl::basic_vtable<
                        dispatch,
                        impl::visitable<curr>::alternatives::size()
                    >{impl::visitable<curr>::index(v)}(
                        ::std::forward<F>(func),
                        ::std::forward<prefix>(pre)...,
                        ::std::forward<curr>(v),
                        ::std::forward<suffix>(suf)...
                    );
                }
            };
        };

        /* If the permutation is not immediately callable with the argument list or we
        have not yet exhausted the forced visits, and we have enough budget to visit
        the current argument, then the permutation will invoke the `substitute`
        algorithm to extend the the function chain and gather the appropriate types. */
        template <
            typename F,
            size_t force,
            size_t budget,
            typename... prefix,
            meta::visitable curr,
            typename... suffix
        >
            requires (budget > 0 && (force > 0 || !meta::callable<F, prefix..., suffix...>))
        struct permute<
            F,
            force,
            budget,
            meta::pack<prefix...>,
            meta::pack<curr, suffix...>
        > : substitute<
            F,
            force,
            budget,
            meta::pack<prefix...>,
            typename impl::visitable<curr>::alternatives,
            meta::pack<curr, suffix...>
        > {};

        ///////////////////////////
        ////    RETURN TYPE    ////
        ///////////////////////////

        /* First, the `::values` obtained from decomposing the `::returns` of the
        outermost `permute` specialization are filtered to their unique types and then
        converted into a `Union` if there is more than one.  If there is precisely one
        result type, it will be returned directly.  Zero result types get converted to
        `void` instead. */
        template <typename T>
        struct to_union<meta::pack<T>> { using type = T; };
        template <typename... T> requires (sizeof...(T) > 1)
        struct to_union<meta::pack<T...>> { using type = bertrand::Union<T...>; };

        /* Next, the `::optionals` are applied, if there are any.  This converts the
        result from `to_union` into an `Optional` with the indicated, unless it is
        void. */
        template <meta::not_void R, typename... Ts> requires (sizeof...(Ts) > 0)
        struct to_optional<R, meta::pack<Ts...>> { using type = bertrand::Optional<R>; };

        /* Finally, the `::errors` are applied, if there are any.  This converts the
        result from `to_optional` into an `Expected` with the indicated, unique error
        types. */
        template <typename R, typename... Es> requires (sizeof...(Es) > 0)
        struct to_expected<R, meta::pack<Es...>> { using type = bertrand::Expected<R, Es...>; };

        /* The permutation object doesn't decompose the return types directly, so that
        the public entry point can reliably check to see if all permutations have been
        handled, or if all return types are consistent.  However, that means the result
        must be further processed to convert the return types into a canonical form,
        merging with the implicitly-propagated empty and error state(s). */
        template <typename Ts, typename Optionals, typename Errors>
        struct deduce : flatten<Ts, Optionals, Errors> {
            using type = to_expected<
                typename to_optional<
                    typename to_union<typename flatten<Ts, Optionals, Errors>::values>::type,
                    typename flatten<Ts, Optionals, Errors>::optionals
                >::type,
                typename flatten<Ts, Optionals, Errors>::errors
            >::type;
        };

        ///////////////////////////
        ////    ENTRY POINT    ////
        ///////////////////////////

        /* Recursion is bounded by the number of visitables (including nested
        visitables) in the input arguments.  Choices of `k` beyond this limit are
        meaningless, since there are not enough visitables to satisfy them. */
        template <typename>
        constexpr size_t max_budget = 0;
        template <typename A, typename... As>
        constexpr size_t max_budget<meta::pack<A, As...>> = max_budget<meta::pack<As...>>;
        template <meta::visitable A, typename... As>
        constexpr size_t max_budget<meta::pack<A, As...>> =
            max_budget<typename impl::visitable<A>::alternatives> + 1 +
            max_budget<meta::pack<As...>>;

        /* The public entry point starts by specializing the `permute` helper with a
        budget equal to the number of forced visits (usually zero) and the full
        argument list.  If that fails, it will increment the budget by 1 and repeat
        until a valid permutation is found or the budget exceeds the maximum. */
        template <typename F, size_t force, size_t budget, typename... A>
        struct search : permute<
            F,
            force,
            budget,
            typename skip<meta::pack<>, A...>::prefix,
            typename skip<meta::pack<>, A...>::suffix
        > {};
        template <typename F, size_t force, size_t budget, typename... A>
            requires (budget < max_budget<meta::pack<A...>> && !permute<
                F,
                force,
                budget,
                typename skip<meta::pack<>, A...>::prefix,
                typename skip<meta::pack<>, A...>::suffix
            >::enable)
        struct search<F, force, budget, A...> : search<F, force, budget + 1, A...> {};

        /* If a valid permutation is found, then the return type will be deduced
        according to the above helpers, which is then spliced into the permutation's
        call operator, which inherited by this object.  The call operator executes the
        proper, minimal sequence of substitutions needed to call the function. */
        template <typename F, size_t force, typename... A>
        struct execute {
            using permute = search<F, force, force, A...>;
            using type = void;
        };
        template <typename F, size_t force, typename... A>
            requires (search<F, force, force, A...>::enable)
        struct execute<F, force, A...> : search<F, force, force, A...>::template fn<
            typename deduce<
                typename search<F, force, force, A...>::returns,
                typename search<F, force, force, A...>::optionals,
                typename search<F, force, force, A...>::errors
            >::type
        > {
            using permute = search<F, force, force, A...>;
            /// NOTE: `type` is inherited from `fn<R>`
        };

    }

    /* Form a canonical union type from the given input types, filtering for uniqueness
    and flattening any nested monads. */
    template <typename... T>
    using make_union = detail::visit::deduce<meta::pack<T...>, meta::pack<>, meta::pack<>>::type;

    /* A visitor function can only be applied to a set of arguments if it covers all
    non-empty and non-error states of the visitable arguments. */
    template <typename F, typename... Args>
    concept visit = detail::visit::execute<F, 0, Args...>::permute::enable;
    template <size_t min_visits, typename F, typename... Args>
    concept force_visit = detail::visit::execute<F, min_visits, Args...>::permute::enable;

    /* Specifies that a visitor function covers all states of the visitable arguments,
    including empty and error states. */
    template <typename F, typename... Args>
    concept visit_exhaustive =
        visit<F, Args...> &&
        detail::visit::execute<F, 0, Args...>::permute::optionals::empty() &&
        detail::visit::execute<F, 0, Args...>::permute::errors::empty();
    template <size_t min_visits, typename F, typename... Args>
    concept force_visit_exhaustive =
        force_visit<min_visits, F, Args...> &&
        detail::visit::execute<F, min_visits, Args...>::permute::optionals::empty() &&
        detail::visit::execute<F, min_visits, Args...>::permute::errors::empty();

    /* Specifies that a visitor function covers all states of the visitable arguments,
    including empty and error states, and that the visitor returns the same type in
    all cases.  This concept implies `meta::visit_exhaustive`. */
    template <typename F, typename... Args>
    concept visit_consistent =
        visit_exhaustive<F, Args...> &&
        detail::visit::execute<F, 0, Args...>::permute::returns
            ::template eval<meta::to_unique>::size() == 1;
    template <size_t min_visits, typename F, typename... Args>
    concept force_visit_consistent =
        force_visit_exhaustive<min_visits, F, Args...> &&
        detail::visit::execute<F, min_visits, Args...>::permute::returns
            ::template eval<meta::to_unique>::size() == 1;

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
    using visit_type = detail::visit::execute<F, 0, Args...>::type;
    template <size_t min_visits, typename F, typename... Args>
        requires (force_visit<min_visits, F, Args...>)
    using force_visit_type = detail::visit::execute<F, min_visits, Args...>::type;

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
        concept visit = meta::visit<F, Args...> && requires(F f, Args... args) {
            {meta::detail::visit::execute<F, 0, Args...>{}(
                ::std::forward<F>(f),
                ::std::forward<Args>(args)...
            )} noexcept;
        };
        template <size_t min_visits, typename F, typename... Args>
        concept force_visit =
            meta::force_visit<min_visits, F, Args...> &&
            requires(F f, Args... args) {
                {meta::detail::visit::execute<F, min_visits, Args...>{}(
                    ::std::forward<F>(f),
                    ::std::forward<Args>(args)...
                )} noexcept;
            };

        template <typename F, typename... Args>
        concept visit_exhaustive =
            meta::visit_exhaustive<F, Args...> && nothrow::visit<F, Args...>;
        template <size_t min_visits, typename F, typename... Args>
        concept force_visit_exhaustive =
            meta::force_visit_exhaustive<min_visits, F, Args...> &&
            nothrow::force_visit<min_visits, F, Args...>;

        template <typename F, typename... Args>
        concept visit_consistent =
            nothrow::visit_exhaustive<F, Args...> &&
            meta::visit_consistent<F, Args...>;
        template <size_t min_visits, typename F, typename... Args>
        concept force_visit_consistent =
            nothrow::force_visit_exhaustive<min_visits, F, Args...> &&
            meta::force_visit_consistent<min_visits, F, Args...>;

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

    }

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
        return (meta::detail::visit::execute<F, min_visits, Args...>{}(
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
        using lookup = meta::unqualify<T>::__type;
        using alternatives = basic_union_types<decltype((std::declval<T>().__value))>;
        using empty = void;
        using errors = meta::pack<>;
        using values = alternatives;

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
        using lookup = meta::specialization<T>;
        using alternatives = _alternatives<
            std::make_index_sequence<std::variant_size_v<meta::unqualify<T>>>
        >::type;
        using empty = void;
        using errors = meta::pack<>;
        using values = alternatives;

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
        using lookup = meta::unqualify<T>::__type;
        using empty = decltype((std::declval<T>().__value.template get<0>()));
        using alternatives = meta::pack<empty, decltype((std::declval<T>().__value.template get<1>()))>;
        using errors = meta::pack<>;
        using values = meta::pack<decltype((std::declval<T>().__value.template get<1>()))>;

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
        using lookup = meta::unqualify<T>::__type;
        using empty = decltype((std::declval<T>().__value.template get<0>()));
        using alternatives = meta::pack<empty>;
        using errors = meta::pack<>;
        using values = meta::pack<>;

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
        using lookup = meta::pack<
            std::nullopt_t,
            typename meta::unqualify<T>::value_type
        >;
        using empty = const std::nullopt_t&;
        using alternatives = meta::pack<const std::nullopt_t&, decltype((*std::declval<T>()))>;
        using errors = meta::pack<>;
        using values = meta::pack<decltype((*std::declval<T>()))>;

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
        using lookup = meta::unqualify<T>::__type;
        using alternatives = basic_union_types<decltype((std::declval<T>().__value))>;
        using empty = void;
        using errors = _errors<std::make_index_sequence<alternatives::size() - 1>>::type;
        using values = meta::pack<decltype((std::declval<T>().__value.template get<0>()))>;

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

    template <meta::std::expected T>
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using lookup = meta::concat<
            meta::pack<typename meta::unqualify<T>::value_type>,
            typename visitable<typename meta::unqualify<T>::error_type>::lookup
        >;
        using values = meta::pack<decltype((*std::declval<T>()))>;
        using empty = void;
        using errors = visitable<decltype((std::declval<T>().error()))>::alternatives;
        using alternatives = meta::concat<values, errors>;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u) noexcept {
            if constexpr (errors::size() > 1) {
                return u.has_value() ?
                    0 :
                    visitable<decltype((u.error()))>::index(u.error()) + 1;
            } else {
                return !u.has_value();
            }
        }

        template <size_t I> requires (I == 0)
        [[gnu::always_inline]] static constexpr decltype(auto) get(meta::forward<T> u)
            noexcept (requires{{*std::forward<T>(u)} noexcept;})
        {
            return (*std::forward<T>(u));
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

    template <meta::std::expected T>
        requires (meta::is_void<typename meta::unqualify<T>::value_type>)
    struct visitable<T> {
        static constexpr bool enable = true;
        static constexpr bool monad = false;
        using type = T;
        using lookup = meta::concat<
            meta::pack<typename meta::unqualify<T>::value_type>,
            typename visitable<typename meta::unqualify<T>::error_type>::lookup
        >;
        using values = meta::pack<>;
        using empty = void;
        using errors = visitable<decltype((std::declval<T>().error()))>::alternatives;
        using alternatives = meta::concat<meta::pack<const NoneType&>, errors>;

        [[gnu::always_inline]] static constexpr size_t index(meta::as_const_ref<T> u) noexcept {
            if constexpr (errors::size() > 1) {
                return u.has_value() ?
                    0 :
                    visitable<decltype((u.error()))>::index(u.error()) + 1;
            } else {
                return !u.has_value();
            }
        }

        template <size_t I> requires (I == 0)
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

    /// NOTE: comparisons between visitable monads are allowed if ANY of the
    /// alternative permutations support it.  Those that do not will be converted into
    /// `false` or `std::partial_ordering::unordered` results instead.  Additionally,
    /// ordered comparisons are allowed against `std::type_identity<T>`
    /// (aka `bertrand::type<T>`, where `T` may be visitable, implying a logical
    /// conjunction over all alternatives), `std::in_place_index_t<I>` (aka
    /// `bertrand::alternative<I>`), and `bertrand::Unexpected` in order to allow fast
    /// checks against the active alternative.

    namespace visit_cmp {

        template <typename, typename>
        struct _result {
            static constexpr bool exists = false;
            using type = std::partial_ordering;
        };
        template <typename L, typename R> requires (meta::has_spaceship<L, R>)
        struct _result<L, R> {
            static constexpr bool exists = true;
            using type = meta::unqualify<meta::spaceship_type<L, R>>;
        };
        template <typename L, typename... R>
            requires (meta::has_common_type<typename _result<L, R>::type...>)
        struct result {
            static constexpr bool exists = (_result<L, R>::exists || ...);
            using type = meta::common_type<typename _result<L, R>::type...>;
        };

        template <typename, typename>
        struct _fn { static constexpr bool exists = false; };
        template <typename... L, typename... R>
            requires (meta::has_common_type<typename result<L, R...>::type...>)
        struct _fn<meta::pack<L...>, meta::pack<R...>> {
            static constexpr bool exists = (result<L, R...>::exists || ...);
            using type = meta::common_type<typename result<L, R...>::type...>;
        };
        template <size_t I>
        struct fn {
            template <typename L, typename R>
            static constexpr auto operator()(L&& lhs, R&& rhs)
                noexcept (!meta::has_spaceship<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                > || meta::nothrow::has_spaceship<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >)
                -> _fn<
                    typename visitable<L>::alternatives,
                    typename visitable<R>::alternatives
                >::type
                requires (
                    !meta::std::in_place_index<L> &&
                    !meta::std::in_place_index<R> &&
                    !meta::std::type_identity<L> &&
                    !meta::std::type_identity<R> &&
                    !meta::is<L, Unexpected> &&
                    !meta::is<R, Unexpected> &&
                    _fn<
                        typename visitable<L>::alternatives,
                        typename visitable<R>::alternatives
                    >::exists
                )
            {
                if constexpr (meta::has_spaceship<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >) {
                    return (
                        visitable<L>::template get<
                            I / visitable<R>::alternatives::size()
                        >(std::forward<L>(lhs)) <=>
                        visitable<R>::template get<
                            I % visitable<R>::alternatives::size()
                        >(std::forward<R>(rhs))
                    );
                } else {
                    return std::partial_ordering::unordered;
                }
            }

            template <typename L, size_t J>
            static constexpr std::strong_ordering operator()(L&& lhs, std::in_place_index_t<J>)
                noexcept (requires{{visitable<L>::index(std::forward<L>(lhs)) <=> J} noexcept;})
                requires (requires{{visitable<L>::index(std::forward<L>(lhs)) <=> J};})
            {
                return impl::visitable<L>::index(std::forward<L>(lhs)) <=> J;
            }

            template <size_t J, typename R>
            static constexpr std::strong_ordering operator()(std::in_place_index_t<J>, R&& rhs)
                noexcept (requires{{J <=> visitable<R>::index(std::forward<R>(rhs))} noexcept;})
                requires (requires{{J <=> visitable<R>::index(std::forward<R>(rhs))};})
            {
                return J <=> impl::visitable<R>::index(std::forward<R>(rhs));
            }

            template <typename, typename>
            static constexpr bool subset = false;
            template <typename L, typename... Rs>
            static constexpr bool subset<L, meta::pack<Rs...>> =
                (L::template contains<Rs>() && ...);

            template <typename L, typename... Rs>
            static constexpr std::partial_ordering operator()(L&& lhs, meta::pack<Rs...>)
                noexcept (requires{{
                    impl::visitable<L>::index(std::forward<L>(lhs))
                } noexcept -> meta::nothrow::convertible_to<size_t>;})
                requires (subset<typename impl::visitable<L>::lookup, meta::pack<Rs...>>)
            {
                if constexpr (sizeof...(Rs) == 0) {
                    return std::partial_ordering::unordered;
                } else {
                    static constexpr size_t min_index = std::min({
                        impl::visitable<L>::lookup::template index<Rs>()...
                    });
                    static constexpr size_t max_index = std::max({
                        impl::visitable<L>::lookup::template index<Rs>()...
                    });

                    size_t index = impl::visitable<L>::index(std::forward<L>(lhs));
                    if (index < min_index) {
                        return std::partial_ordering::less;
                    }
                    if (index > max_index) {
                        return std::partial_ordering::greater;
                    }
                    return ((index == impl::visitable<L>::lookup::template index<Rs>()) || ...) ?
                        std::partial_ordering::equivalent :
                        std::partial_ordering::unordered;
                }
            }

            template <typename... Ls, typename R>
            static constexpr std::partial_ordering operator()(meta::pack<Ls...>, R&& rhs)
                noexcept (requires{{
                    impl::visitable<R>::index(std::forward<R>(rhs))
                } noexcept -> meta::nothrow::convertible_to<size_t>;})
                requires (subset<typename impl::visitable<R>::lookup, meta::pack<Ls...>>)
            {
                if constexpr (sizeof...(Ls) == 0) {
                    return std::partial_ordering::unordered;
                } else {
                    static constexpr size_t min_index = std::min({
                        impl::visitable<R>::lookup::template index<Ls>()...
                    });
                    static constexpr size_t max_index = std::max({
                        impl::visitable<R>::lookup::template index<Ls>()...
                    });

                    size_t index = impl::visitable<R>::index(std::forward<R>(rhs));
                    if (index < min_index) {
                        return std::partial_ordering::less;
                    }
                    if (index > max_index) {
                        return std::partial_ordering::greater;
                    }
                    return ((index == impl::visitable<R>::lookup::template index<Ls>()) || ...) ?
                        std::partial_ordering::equivalent :
                        std::partial_ordering::unordered;
                }
            }

            template <typename L, typename R>
            static constexpr std::partial_ordering operator()(L&& lhs, std::type_identity<R> rhs)
                noexcept (requires{{
                    operator()(std::forward<L>(lhs), typename impl::visitable<R>::lookup{})
                } noexcept;})
                requires (subset<
                    typename impl::visitable<L>::lookup,
                    typename impl::visitable<R>::lookup
                >)
            {
                return operator()(std::forward<L>(lhs), typename impl::visitable<R>::lookup{});
            }

            template <typename L, typename R>
            static constexpr std::partial_ordering operator()(std::type_identity<L> lhs, R&& rhs)
                noexcept (requires{{
                    operator()(typename impl::visitable<L>::lookup{}, std::forward<R>(rhs))
                } noexcept;})
                requires (subset<
                    typename impl::visitable<R>::lookup,
                    typename impl::visitable<L>::lookup
                >)
            {
                return operator()(typename impl::visitable<L>::lookup{}, std::forward<R>(rhs));
            }

            template <typename L>
            static constexpr std::strong_ordering operator()(L&& lhs, Unexpected) noexcept
                requires (!impl::visitable<L>::errors::empty())
            {
                return impl::visitable<L>::errors::template contains<
                    typename impl::visitable<L>::alternatives::template at<I>
                >() ?
                    std::strong_ordering::equivalent :
                    std::strong_ordering::less;
            }

            template <typename R>
            static constexpr std::strong_ordering operator()(Unexpected, R&& rhs) noexcept
                requires (!impl::visitable<R>::errors::empty())
            {
                return impl::visitable<R>::errors::template contains<
                    typename impl::visitable<R>::alternatives::template at<I>
                >() ?
                    std::strong_ordering::equivalent :
                    std::strong_ordering::greater;
            }
        };

    }

    namespace visit_lt {

        template <typename, typename>
        struct _result {
            static constexpr bool exists = false;
            using type = bool;
        };
        template <typename L, typename R> requires (meta::has_lt<L, R>)
        struct _result<L, R> {
            static constexpr bool exists = true;
            using type = meta::lt_type<L, R>;
        };
        template <typename L, typename... R>
        struct result {
            static constexpr bool exists = (_result<L, R>::exists || ...);
            using type = meta::pack<typename _result<L, R>::type...>;
        };

        template <typename, typename>
        struct _fn { static constexpr bool exists = false; };
        template <typename... L, typename... R>
        struct _fn<meta::pack<L...>, meta::pack<R...>> {
            static constexpr bool exists = (result<L, R...>::exists || ...);
            using type = meta::concat<
                typename result<L, R...>::type...
            >::template eval<meta::make_union>;
        };
        template <size_t I>
        struct fn {
            template <typename L, typename R>
            static constexpr auto operator()(L&& lhs, R&& rhs)
                noexcept (!meta::has_lt<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                > || meta::nothrow::has_lt<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >)
                -> _fn<
                    typename visitable<L>::alternatives,
                    typename visitable<R>::alternatives
                >::type
                requires (
                    !meta::std::in_place_index<L> &&
                    !meta::std::in_place_index<R> &&
                    !meta::std::type_identity<L> &&
                    !meta::std::type_identity<R> &&
                    !meta::is<L, Unexpected> &&
                    !meta::is<R, Unexpected> &&
                    _fn<
                        typename visitable<L>::alternatives,
                        typename visitable<R>::alternatives
                    >::exists
                )
            {
                if constexpr (meta::has_lt<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >) {
                    return (
                        visitable<L>::template get<
                            I / visitable<R>::alternatives::size()
                        >(std::forward<L>(lhs)) <
                        visitable<R>::template get<
                            I % visitable<R>::alternatives::size()
                        >(std::forward<R>(rhs))
                    );
                } else {
                    return false;
                }
            }

            template <typename L, typename R>
            static constexpr bool operator()(L&& lhs, R&& rhs)
                noexcept (requires{{visit_cmp::fn<I>{}(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                ) < 0} noexcept -> meta::nothrow::convertible_to<bool>;})
                requires ((
                    meta::std::in_place_index<L> ||
                    meta::std::in_place_index<R> ||
                    meta::std::type_identity<L> ||
                    meta::std::type_identity<R> ||
                    meta::is<L, Unexpected> ||
                    meta::is<R, Unexpected>
                ) && requires{
                    {visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) < 0};
                })
            {
                return visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) < 0;
            }
        };

    }

    namespace visit_le {

        template <typename, typename>
        struct _result {
            static constexpr bool exists = false;
            using type = bool;
        };
        template <typename L, typename R> requires (meta::has_le<L, R>)
        struct _result<L, R> {
            static constexpr bool exists = true;
            using type = meta::le_type<L, R>;
        };
        template <typename L, typename... R>
        struct result {
            static constexpr bool exists = (_result<L, R>::exists || ...);
            using type = meta::pack<typename _result<L, R>::type...>;
        };

        template <typename, typename>
        struct _fn { static constexpr bool exists = false; };
        template <typename... L, typename... R>
        struct _fn<meta::pack<L...>, meta::pack<R...>> {
            static constexpr bool exists = (result<L, R...>::exists || ...);
            using type = meta::concat<
                typename result<L, R...>::type...
            >::template eval<meta::make_union>;
        };
        template <size_t I>
        struct fn {
            template <typename L, typename R>
            static constexpr auto operator()(L&& lhs, R&& rhs)
                noexcept (!meta::has_le<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                > || meta::nothrow::has_le<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >)
                -> _fn<
                    typename visitable<L>::alternatives,
                    typename visitable<R>::alternatives
                >::type
                requires (
                    !meta::std::in_place_index<L> &&
                    !meta::std::in_place_index<R> &&
                    !meta::std::type_identity<L> &&
                    !meta::std::type_identity<R> &&
                    !meta::is<L, Unexpected> &&
                    !meta::is<R, Unexpected> &&
                    _fn<
                        typename visitable<L>::alternatives,
                        typename visitable<R>::alternatives
                    >::exists
                )
            {
                if constexpr (meta::has_le<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >) {
                    return (
                        visitable<L>::template get<
                            I / visitable<R>::alternatives::size()
                        >(std::forward<L>(lhs)) <=
                        visitable<R>::template get<
                            I % visitable<R>::alternatives::size()
                        >(std::forward<R>(rhs))
                    );
                } else {
                    return false;
                }
            }

            template <typename L, typename R>
            static constexpr bool operator()(L&& lhs, R&& rhs)
                noexcept (requires{{visit_cmp::fn<I>{}(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                ) <= 0} noexcept -> meta::nothrow::convertible_to<bool>;})
                requires ((
                    meta::std::in_place_index<L> ||
                    meta::std::in_place_index<R> ||
                    meta::std::type_identity<L> ||
                    meta::std::type_identity<R> ||
                    meta::is<L, Unexpected> ||
                    meta::is<R, Unexpected>
                ) && requires{
                    {visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) <= 0};
                })
            {
                return visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) <= 0;
            }
        };

    }

    namespace visit_eq {

        template <typename, typename>
        struct _result {
            static constexpr bool exists = false;
            using type = bool;
        };
        template <typename L, typename R> requires (meta::has_eq<L, R>)
        struct _result<L, R> {
            static constexpr bool exists = true;
            using type = meta::eq_type<L, R>;
        };
        template <typename L, typename... R>
        struct result {
            static constexpr bool exists = (_result<L, R>::exists || ...);
            using type = meta::pack<typename _result<L, R>::type...>;
        };

        template <typename, typename>
        struct _fn { static constexpr bool exists = false; };
        template <typename... L, typename... R>
        struct _fn<meta::pack<L...>, meta::pack<R...>> {
            static constexpr bool exists = (result<L, R...>::exists || ...);
            using type = meta::concat<
                typename result<L, R...>::type...
            >::template eval<meta::make_union>;
        };
        template <size_t I>
        struct fn {
            template <typename L, typename R>
            static constexpr auto operator()(L&& lhs, R&& rhs)
                noexcept (!meta::has_eq<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                > || meta::nothrow::has_eq<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >)
                -> _fn<
                    typename visitable<L>::alternatives,
                    typename visitable<R>::alternatives
                >::type
                requires (
                    !meta::std::in_place_index<L> &&
                    !meta::std::in_place_index<R> &&
                    !meta::std::type_identity<L> &&
                    !meta::std::type_identity<R> &&
                    !meta::is<L, Unexpected> &&
                    !meta::is<R, Unexpected> &&
                    _fn<
                        typename visitable<L>::alternatives,
                        typename visitable<R>::alternatives
                    >::exists
                )
            {
                if constexpr (meta::has_eq<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >) {
                    return (
                        visitable<L>::template get<
                            I / visitable<R>::alternatives::size()
                        >(std::forward<L>(lhs)) ==
                        visitable<R>::template get<
                            I % visitable<R>::alternatives::size()
                        >(std::forward<R>(rhs))
                    );
                } else {
                    return false;
                }
            }

            template <typename L, typename R>
            static constexpr bool operator()(L&& lhs, R&& rhs)
                noexcept (requires{{visit_cmp::fn<I>{}(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                ) == 0} noexcept -> meta::nothrow::convertible_to<bool>;})
                requires ((
                    meta::std::in_place_index<L> ||
                    meta::std::in_place_index<R> ||
                    meta::std::type_identity<L> ||
                    meta::std::type_identity<R> ||
                    meta::is<L, Unexpected> ||
                    meta::is<R, Unexpected>
                ) && requires{
                    {visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) == 0};
                })
            {
                return visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) == 0;
            }
        };

    }

    namespace visit_ne {

        template <typename, typename>
        struct _result {
            static constexpr bool exists = false;
            using type = bool;
        };
        template <typename L, typename R> requires (meta::has_ne<L, R>)
        struct _result<L, R> {
            static constexpr bool exists = true;
            using type = meta::ne_type<L, R>;
        };
        template <typename L, typename... R>
        struct result {
            static constexpr bool exists = (_result<L, R>::exists || ...);
            using type = meta::pack<typename _result<L, R>::type...>;
        };

        template <typename, typename>
        struct _fn { static constexpr bool exists = false; };
        template <typename... L, typename... R>
        struct _fn<meta::pack<L...>, meta::pack<R...>> {
            static constexpr bool exists = (result<L, R...>::exists || ...);
            using type = meta::concat<
                typename result<L, R...>::type...
            >::template eval<meta::make_union>;
        };
        template <size_t I>
        struct fn {
            template <typename L, typename R>
            static constexpr auto operator()(L&& lhs, R&& rhs)
                noexcept (!meta::has_ne<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                > || meta::nothrow::has_ne<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >)
                -> _fn<
                    typename visitable<L>::alternatives,
                    typename visitable<R>::alternatives
                >::type
                requires (
                    !meta::std::in_place_index<L> &&
                    !meta::std::in_place_index<R> &&
                    !meta::std::type_identity<L> &&
                    !meta::std::type_identity<R> &&
                    !meta::is<L, Unexpected> &&
                    !meta::is<R, Unexpected> &&
                    _fn<
                        typename visitable<L>::alternatives,
                        typename visitable<R>::alternatives
                    >::exists
                )
            {
                if constexpr (meta::has_ne<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >) {
                    return (
                        visitable<L>::template get<
                            I / visitable<R>::alternatives::size()
                        >(std::forward<L>(lhs)) !=
                        visitable<R>::template get<
                            I % visitable<R>::alternatives::size()
                        >(std::forward<R>(rhs))
                    );
                } else {
                    return true;
                }
            }

            template <typename L, typename R>
            static constexpr bool operator()(L&& lhs, R&& rhs)
                noexcept (requires{{visit_cmp::fn<I>{}(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                ) != 0} noexcept -> meta::nothrow::convertible_to<bool>;})
                requires ((
                    meta::std::in_place_index<L> ||
                    meta::std::in_place_index<R> ||
                    meta::std::type_identity<L> ||
                    meta::std::type_identity<R> ||
                    meta::is<L, Unexpected> ||
                    meta::is<R, Unexpected>
                ) && requires{
                    {visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) != 0};
                })
            {
                return visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) != 0;
            }
        };

    }

    namespace visit_ge {

        template <typename, typename>
        struct _result {
            static constexpr bool exists = false;
            using type = bool;
        };
        template <typename L, typename R> requires (meta::has_ge<L, R>)
        struct _result<L, R> {
            static constexpr bool exists = true;
            using type = meta::ge_type<L, R>;
        };
        template <typename L, typename... R>
        struct result {
            static constexpr bool exists = (_result<L, R>::exists || ...);
            using type = meta::pack<typename _result<L, R>::type...>;
        };

        template <typename, typename>
        struct _fn { static constexpr bool exists = false; };
        template <typename... L, typename... R>
        struct _fn<meta::pack<L...>, meta::pack<R...>> {
            static constexpr bool exists = (result<L, R...>::exists || ...);
            using type = meta::concat<
                typename result<L, R...>::type...
            >::template eval<meta::make_union>;
        };
        template <size_t I>
        struct fn {
            template <typename L, typename R>
            static constexpr auto operator()(L&& lhs, R&& rhs)
                noexcept (!meta::has_ge<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                > || meta::nothrow::has_ge<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >)
                -> _fn<
                    typename visitable<L>::alternatives,
                    typename visitable<R>::alternatives
                >::type
                requires (
                    !meta::std::in_place_index<L> &&
                    !meta::std::in_place_index<R> &&
                    !meta::std::type_identity<L> &&
                    !meta::std::type_identity<R> &&
                    !meta::is<L, Unexpected> &&
                    !meta::is<R, Unexpected> &&
                    _fn<
                        typename visitable<L>::alternatives,
                        typename visitable<R>::alternatives
                    >::exists
                )
            {
                if constexpr (meta::has_ge<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >) {
                    return (
                        visitable<L>::template get<
                            I / visitable<R>::alternatives::size()
                        >(std::forward<L>(lhs)) >=
                        visitable<R>::template get<
                            I % visitable<R>::alternatives::size()
                        >(std::forward<R>(rhs))
                    );
                } else {
                    return false;
                }
            }

            template <typename L, typename R>
            static constexpr bool operator()(L&& lhs, R&& rhs)
                noexcept (requires{{visit_cmp::fn<I>{}(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                ) >= 0} noexcept -> meta::nothrow::convertible_to<bool>;})
                requires ((
                    meta::std::in_place_index<L> ||
                    meta::std::in_place_index<R> ||
                    meta::std::type_identity<L> ||
                    meta::std::type_identity<R> ||
                    meta::is<L, Unexpected> ||
                    meta::is<R, Unexpected>
                ) && requires{
                    {visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) >= 0};
                })
            {
                return visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) >= 0;
            }
        };

    }

    namespace visit_gt {

        template <typename, typename>
        struct _result {
            static constexpr bool exists = false;
            using type = bool;
        };
        template <typename L, typename R> requires (meta::has_gt<L, R>)
        struct _result<L, R> {
            static constexpr bool exists = true;
            using type = meta::gt_type<L, R>;
        };
        template <typename L, typename... R>
        struct result {
            static constexpr bool exists = (_result<L, R>::exists || ...);
            using type = meta::pack<typename _result<L, R>::type...>;
        };

        template <typename, typename>
        struct _fn { static constexpr bool exists = false; };
        template <typename... L, typename... R>
        struct _fn<meta::pack<L...>, meta::pack<R...>> {
            static constexpr bool exists = (result<L, R...>::exists || ...);
            using type = meta::concat<
                typename result<L, R...>::type...
            >::template eval<meta::make_union>;
        };
        template <size_t I>
        struct fn {
            template <typename L, typename R>
            static constexpr auto operator()(L&& lhs, R&& rhs)
                noexcept (!meta::has_gt<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                > || meta::nothrow::has_gt<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >)
                -> _fn<
                    typename visitable<L>::alternatives,
                    typename visitable<R>::alternatives
                >::type
                requires (
                    !meta::std::in_place_index<L> &&
                    !meta::std::in_place_index<R> &&
                    !meta::std::type_identity<L> &&
                    !meta::std::type_identity<R> &&
                    !meta::is<L, Unexpected> &&
                    !meta::is<R, Unexpected> &&
                    _fn<
                        typename visitable<L>::alternatives,
                        typename visitable<R>::alternatives
                    >::exists
                )
            {
                if constexpr (meta::has_gt<
                    decltype((visitable<L>::template get<
                        I / visitable<R>::alternatives::size()
                    >(std::forward<L>(lhs)))),
                    decltype((visitable<R>::template get<
                        I % visitable<R>::alternatives::size()
                    >(std::forward<R>(rhs))))
                >) {
                    return (
                        visitable<L>::template get<
                            I / visitable<R>::alternatives::size()
                        >(std::forward<L>(lhs)) >
                        visitable<R>::template get<
                            I % visitable<R>::alternatives::size()
                        >(std::forward<R>(rhs))
                    );
                } else {
                    return false;
                }
            }

            template <typename L, typename R>
            static constexpr bool operator()(L&& lhs, R&& rhs)
                noexcept (requires{{visit_cmp::fn<I>{}(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs)
                ) > 0} noexcept -> meta::nothrow::convertible_to<bool>;})
                requires ((
                    meta::std::in_place_index<L> ||
                    meta::std::in_place_index<R> ||
                    meta::std::type_identity<L> ||
                    meta::std::type_identity<R> ||
                    meta::is<L, Unexpected> ||
                    meta::is<R, Unexpected>
                ) && requires{
                    {visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) > 0};
                })
            {
                return visit_cmp::fn<I>{}(std::forward<L>(lhs), std::forward<R>(rhs)) > 0;
            }
        };

    }

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

    /* Unions of unions are generally bad practice from a performance and design
    standpoint, so Bertrand always flattens them into a single union type containing
    only non-visitable alternatives instead.  This allows the visitation mechanism to
    cover all alternatives in a single dispatch, without any nested vtables, and
    effectively merges all the discriminators into a single index, which saves space.
    Since all operators are recursively forwarded in monadic fashion, there is no loss
    of functionality by doing this, although it may affect code that manually accesses
    the underlying index, which is generally discouraged, and only accessible via
    `bertrand::alternative<I>` or `impl::visitable<T>::index(T)`.  It also specifically
    places optional states before non-optional ones, so that the flattened union may be
    reliably default-constructed in an empty state where possible. */
    template <typename... Ts>
    using union_flatten_type = union_flatten<Ts...>::template eval<bertrand::Union>;

    /* CTAD guides to optional types work the same way as `meta::make_union`, but omit
    the intermediate conversion to `Optional`.  This allows deduction from other
    visitable types (possibly from the STL), with canonical nesting. */
    template <typename T>
    using optional_flatten_type = meta::detail::visit::to_expected<
        typename meta::detail::visit::to_union<
            typename meta::detail::visit::decompose<T>::values
        >::type,
        typename meta::detail::visit::decompose<T>::errors
    >::type;

    /* As with unions, Expecteds with nested error states will be flattened into the
    outermost Expected type, merging all error alternatives together and converting
    the remaining values into a canonical format.  Any alternatives that are placed in
    the `Es...` pack are defined as error states, and will take precedence over any
    conflicting alternatives in `T`.  This allows users to specialize
    `Expected<T, Es...>` in order to treat any number of alternatives of `T` as errors,
    while maintaining the same behavior under visitation. */
    template <typename T, typename... Es>
    using expected_flatten_type = expected_flatten<T, Es...>::template eval<bertrand::Expected>;

    /* Monads are formattable if all of their alternatives are formattable. */
    template <typename, typename>
    constexpr bool _alternatives_are_formattable = false;
    template <typename... Ts, typename Char> requires (meta::formattable<Ts, Char> && ...)
    constexpr bool _alternatives_are_formattable<meta::pack<Ts...>, Char> = true;
    template <meta::visit_monad T, typename Char>
    constexpr bool alternatives_are_formattable =
        _alternatives_are_formattable<typename impl::visitable<T>::alternatives, Char>;

}


/* A special case of Union where at least one of the alternatives is a visitable type,
in which case its alternatives will be recursively unpacked and flattened into the
outermost union.  This enables unions of unions to be seamlessly composed together
without unnecessary dispatching or space overhead, while still preserving the same
observable behavior under visitation.

Note that this form also allows CTAD for any visitable type with at least 2 unique
alternatives. */
template <typename... Ts>
    requires (
        impl::union_concept<Ts...> &&
        !std::same_as<meta::pack<Ts...>, impl::union_flatten<Ts...>>
    )
struct Union<Ts...> : impl::union_flatten_type<Ts...> {
    using impl::union_flatten_type<Ts...>::union_flatten_type;
    using impl::union_flatten_type<Ts...>::operator=;
};


template <typename T>
Union(T&&) -> Union<meta::remove_rvalue<T>>;


template <meta::not_pointer T>
Optional(T&&) -> Optional<impl::optional_flatten_type<T>>;


template <meta::pointer T>
Optional(T) -> Optional<meta::dereference_type<T>>;


/* A special case of Expected where at least one of the alternatives is a visitable
type, in which case its alternatives will be recursively unpacked and flattened into
the outermost expected.  This enables expecteds of unions, optionals, and other
expecteds to be seamlessly composed together without unnecessary dispatching or space
overhead, while still preserving the same observable behavior under visitation.  Any
visitables that are placed in the `Es...` pack are defined as error states, and will
take precedence over any conflicting alternatives in `T`.  If `T` has error states,
then they will be merged with those in `Es...` during flattening.

Note that this form also allows CTAD for any visitable type with at least 1 error
state. */
template <typename T, typename... Es>
    requires (
        impl::expected_concept<T, Es...> &&
        !std::same_as<meta::pack<T, Es...>, impl::expected_flatten<T, Es...>>
    )
struct Expected<T, Es...> : impl::expected_flatten_type<T, Es...> {
    using impl::expected_flatten_type<T, Es...>::expected_flatten_type;
    using impl::expected_flatten_type<T, Es...>::operator=;
};


template <typename T>
Expected(T&&) -> Expected<meta::remove_rvalue<T>>;


/////////////////////
////    UNION    ////
/////////////////////


namespace impl {

    /* Because unions always flatten their alternatives, initializing one with a
    `bertrand::type<T>` disambiguation tag (where `T` is a visitable type) requires a
    fold expression over all alternatives to ensure they are all present in the
    union's template signature. */
    template <typename, typename>
    constexpr bool _union_init_subset = false;
    template <typename lookup, typename... Ts>
    constexpr bool _union_init_subset<lookup, meta::pack<Ts...>> =
        (lookup::template contains<Ts>() && ...);
    template <typename Self, typename T>
    constexpr bool union_init_subset = _union_init_subset<
        typename impl::visitable<Self>::lookup,
        meta::concat<
            typename meta::detail::visit::decompose<T>::values,
            typename meta::detail::visit::decompose<T>::optionals,
            typename meta::detail::visit::decompose<T>::errors
        >
    >;

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
    struct basic_union {
        using types = meta::pack<Ts...>;
        using default_type = impl::union_default_type<Ts...>;

        template <size_t I> requires (I < sizeof...(Ts))
        using tag = std::in_place_index_t<I>;

        static constexpr size_t size() noexcept { return sizeof...(Ts); }
        static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        static constexpr bool empty() noexcept { return (sizeof...(Ts) == 0); }

        template <typename T>
        [[nodiscard]] static constexpr bool contains() noexcept {
            return types::template contains<T>();
        }

        template <typename T> requires (contains<T>())
        [[nodiscard]] static constexpr size_t find() noexcept {
            return types::template index<T>();
        }

    private:
        template <typename... Us>
        union store {
            constexpr store() noexcept {};
            constexpr ~store() noexcept {};
        };
        template <typename U, typename... Us>
        union store<U, Us...> {
            [[no_unique_address]] impl::ref<U> curr;
            [[no_unique_address]] store<Us...> rest;

            constexpr store() noexcept {}
            constexpr ~store() noexcept {}

            template <typename... A>
            constexpr store(tag<0>, A&&... args)
                noexcept (requires{{U(std::forward<A>(args)...)} noexcept;})
                requires (requires{{U(std::forward<A>(args)...)};})
            :
                curr{U(std::forward<A>(args)...)}
            {}

            template <size_t I, typename... A> requires (I > 0)
            constexpr store(tag<I>, A&&... args)
                noexcept (requires{
                    {store<Us...>(tag<I - 1>{}, std::forward<A>(args)...)} noexcept;
                })
                requires (requires{{store<Us...>(tag<I - 1>{}, std::forward<A>(args)...)};})
            :
                rest(tag<I - 1>{}, std::forward<A>(args)...)
            {}

            constexpr store(store&& other)
                noexcept (requires{{impl::ref<U>(std::move(other).curr)} noexcept;})
                requires (requires{{impl::ref<U>(std::move(other).curr)};})
            :
                curr(std::move(other).curr)
            {}

            template <typename... Vs> requires (sizeof...(Vs) <= sizeof...(Us))
            constexpr store(store<Vs...>&& other)
                noexcept (requires{{store<Us...>(std::move(other))} noexcept;})
                requires (requires{{store<Us...>(std::move(other))};})
            :
                rest(std::move(other))
            {}

            template <size_t I, typename T> requires (I <= sizeof...(Us))
            constexpr decltype(auto) get(this T&& self) noexcept {
                if constexpr (I == 0) {
                    return (std::forward<T>(self));
                } else {
                    return (std::forward<T>(self).rest.template get<I - 1>());
                }
            }
        };

    public:
        using type = store<Ts...>;
        [[no_unique_address]] type m_data;
        [[no_unique_address]] meta::smallest_unsigned_int<sizeof...(Ts)> m_index;

        /* Default constructor selects the first default-constructible type in `Ts...`,
        and initializes the union to that type. */
        [[nodiscard]] constexpr basic_union()
            noexcept (meta::nothrow::default_constructible<default_type>)
            requires (meta::not_void<default_type>)
        :
            m_data(tag<find<union_default_type<Ts...>>()>{}),
            m_index(find<union_default_type<Ts...>>())
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
            return (*std::forward<Self>(self).m_data.template get<I>().curr);
        }

        /* Access a specific type, assuming it is present in the union. */
        template <typename T, typename Self> requires (contains<T>())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_data.template get<find<T>()>().curr);
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
            requires (contains<T>() && meta::has_address<meta::as_lvalue<T>>)
        [[nodiscard]] constexpr auto get_if() noexcept
            -> meta::address_type<meta::as_lvalue<T>>
        {
            return m_index == find<T>() ? std::addressof(get<T>()) : nullptr;
        }

        /* Get a pointer to a specific type if it is the active alternative.  Returns
        a null pointer otherwise. */
        template <typename T>
            requires (contains<T>() && meta::has_address<meta::as_const_ref<T>>)
        [[nodiscard]] constexpr auto get_if() const noexcept
            -> meta::address_type<meta::as_const_ref<T>>
        {
            return m_index == find<T>() ? std::addressof(get<T>()) : nullptr;
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
                requires (requires{{std::destroy_at(std::addressof(u.template get<I>().curr))};})
            {
                if constexpr (!meta::trivially_destructible<decltype(u.template get<I>().curr)>) {
                    std::destroy_at(std::addressof(u.template get<I>().curr));
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
                if constexpr (J == K && requires{
                    {meta::swap(
                        self.m_data.template get<J>().curr,
                        other.m_data.template get<K>().curr
                    )};
                }) {
                    meta::swap(
                        self.m_data.template get<J>().curr,
                        other.m_data.template get<K>().curr
                    );

                // If the indices differ or the types are lvalues, then we need to move
                // construct and destroy the original value behind us.
                } else {
                    type temp(std::move(self).m_data.template get<J>());
                    destroy<J>{}(self.m_data);
                    try {
                        std::construct_at(
                            &self.m_data,
                            std::move(other).m_data.template get<K>()
                        );
                        destroy<K>{}(other.m_data);
                        try {
                            std::construct_at(
                                &other.m_data,
                                std::move(temp).template get<J>()
                            );
                            destroy<J>{}(temp);
                        } catch (...) {
                            std::construct_at(
                                &other.m_data,
                                std::move(self).m_data.template get<K>()
                            );
                            destroy<K>{}(self.m_data);
                            throw;
                        }
                    } catch (...) {
                        std::construct_at(
                            &self.m_data,
                            std::move(temp).template get<J>()
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

    template <typename from, typename curr>
    concept union_proximal =
        meta::inherits<from, curr> &&
        meta::convertible_to<from, curr> &&
        (meta::lvalue<from> || !meta::lvalue<curr>);

    template <typename from, typename curr, typename convert>
    concept union_convertible =
        meta::is_void<convert> &&
        !meta::lvalue<curr> &&
        meta::convertible_to<from, curr>;

    template <typename from, typename proximal, typename convert, typename...>
    struct _union_convert_from {
        using type = std::conditional_t<meta::not_void<proximal>, proximal, convert>;
    };
    template <typename from, typename proximal, typename convert, typename curr, typename... next>
        requires (union_proximal<from, curr>)
    struct _union_convert_from<from, proximal, convert, curr, next...> : _union_convert_from<
        from,
        std::conditional_t<
            meta::is_void<proximal> ||  // first candidate
            (meta::inherits<curr, proximal> && !meta::is<curr, proximal>) ||  // more derived
            (
                meta::is<curr, proximal> &&
                meta::lvalue<from> == meta::lvalue<curr> &&
                meta::more_qualified_than<proximal, curr>  // less qualified
            ),
            curr,
            proximal
        >,
        convert,
        next...
    > {};
    template <typename from, typename proximal, typename convert, typename curr, typename... next>
        requires (!union_proximal<from, curr> && union_convertible<from, curr, convert>)
    struct _union_convert_from<from, proximal, convert, curr, next...> :
        _union_convert_from<from, proximal, curr, next...>
    {};
    template <typename from, typename proximal, typename convert, typename curr, typename... next>
        requires (!union_proximal<from, curr> && !union_convertible<from, curr, convert>)
    struct _union_convert_from<from, proximal, convert, curr, next...> :
        _union_convert_from<from, proximal, convert, next...>
    {};

    /* A manual visitor that backs the implicit constructor for a `Union<Ts...>`
    object, returning a corresponding `basic_union` primitive type initialized to the
    most derived and least qualified alternative in the inheritance chain of `from`,
    or the first implicitly convertible alternative if no such type exists. Lvalue
    inputs will bind to lvalue or prvalue alternatives, and rvalue inputs will only
    bind to prvalues.  If no convertible alternative exists, the `type` member will be
    `void`. */
    template <typename... Ts>
    struct union_convert_from {
        template <typename from>
        using type = _union_convert_from<from, void, void, Ts...>::type;
        template <typename from>
        static constexpr auto operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, type<from>>)
            requires (!meta::visitable<from> && meta::not_void<type<from>>)
        {
            return impl::basic_union<Ts...>{
                bertrand::alternative<meta::index_of<type<from>, Ts...>>,
                std::forward<from>(arg)
            };
        }
    };

    template <typename... A>
    struct _union_construct_from {
        template <typename... Ts>
        struct select { using type = void; };
        template <meta::constructible_from<A...> T, typename... Ts>
        struct select<T, Ts...> { using type = T; };
        template <typename T, typename... Ts>
        struct select<T, Ts...> : select<Ts...> {};
    };

    /* A manual visitor that backs the explicit constructor for a `Union<Ts...>`
    object, returning a corresponding `basic_union` primitive type initialized to the
    first alternative in `Ts...` that is constructible from the given arguments.  Note
    that this only applies if `union_convert_from` would be invalid. */
    template <typename... Ts>
    struct union_construct_from {
        template <typename... A>
        using type = _union_construct_from<A...>::template select<Ts...>::type;
        template <typename... A>
        static constexpr auto operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<type<A...>, A...>)
            requires (meta::not_void<type<A...>>)
        {
            return impl::basic_union<Ts...>{
                bertrand::alternative<meta::index_of<type<A...>, Ts...>>,
                std::forward<A>(args)...
            };
        }
    };

    /* A manual visitor that backs the implicit conversion operator from `Union<Ts...>`
    to any type `T` to which all alternatives can be converted. */
    template <typename to>
    struct union_convert_to {
        template <size_t I>
        struct fn {
            template <typename Self>
            static constexpr to operator()(Self&& self)
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
    };

    /* A manual visitor that backs the explicit conversion operator from `Union<Ts...>`
    to any type `T` to which all alternatives can be explicitly converted.  Only
    applies if `union_convert_to` would be malformed. */
    template <typename to>
    struct union_cast_to {
        template <size_t I>
        struct fn {
            template <typename Self>
            static constexpr to operator()(Self&& self)
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
    };

    template <typename Return>
    struct union_data {
        template <size_t I>
        struct fn {
            template <typename Self>
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::data(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::data(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return meta::data(std::forward<Self>(self).__value.template get<I>());
            }
        };
    };

    template <typename Return>
    struct union_size {
        template <size_t I>
        struct fn {
            template <typename Self>
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::size(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::size(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return meta::size(std::forward<Self>(self).__value.template get<I>());
            }
        };
    };

    template <typename Return>
    struct union_ssize {
        template <size_t I>
        struct fn {
            template <typename Self>
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::ssize(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::ssize(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return meta::ssize(std::forward<Self>(self).__value.template get<I>());
            }
        };
    };

    template <size_t I>
    struct union_empty {
        template <typename Self>
        [[nodiscard]] static constexpr bool operator()(Self&& self)
            noexcept (requires{{
                meta::empty(std::forward<Self>(self).__value.template get<I>())
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                meta::empty(std::forward<Self>(self).__value.template get<I>())
            } -> meta::convertible_to<bool>;})
        {
            return meta::empty(std::forward<Self>(self).__value.template get<I>());
        }
    };

    template <typename Return>
    struct union_shape {
        template <size_t I>
        struct fn {
            template <typename Self>
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::shape(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::shape(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return (meta::shape(std::forward<Self>(self).__value.template get<I>()));
            }
        };
    };

    template <typename Return, auto... A>
    struct union_get {
        template <size_t I>
        struct fn {
            template <typename Self>
            static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::get<A...>(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::get<A...>(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return meta::get<A...>(std::forward<Self>(self).__value.template get<I>());
            }
        };
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
    };

    template <meta::unqualified... Ts> requires (sizeof...(Ts) > 1)
    struct union_iterator;

    template <typename T>
    constexpr bool is_union_iterator = false;
    template <typename... Ts>
    constexpr bool is_union_iterator<union_iterator<Ts...>> = true;

    template <typename Return>
    struct union_iterator_deref {
        template <size_t I>
        struct fn {
            template <typename Self>
            static constexpr Return operator()(const Self& self)
                noexcept (requires{{
                    *self.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    *self.__value.template get<I>()
                } -> meta::convertible_to<Return>;})
            {
                return *self.__value.template get<I>();
            }
        };
    };

    template <typename Return, typename Difference>
    struct union_iterator_subscript {
        template <size_t I>
        struct fn {
            template <typename Self>
            static constexpr Return operator()(const Self& self, Difference n)
                noexcept (requires{{
                    self.__value.template get<I>()[n]
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    self.__value.template get<I>()[n]
                } -> meta::convertible_to<Return>;})
            {
                return self.__value.template get<I>()[n];
            }
        };
    };

    template <size_t I>
    struct union_iterator_preincrement {
        template <typename Self>
        static constexpr Self& operator()(Self& self)
            noexcept (requires{{++self.__value.template get<I>()} noexcept;})
            requires (requires{{++self.__value.template get<I>()};})
        {
            ++self.__value.template get<I>();
            return self;
        }
    };

    template <size_t I>
    struct union_iterator_postincrement {
        template <typename Self>
        static constexpr Self operator()(Self& self)
            noexcept (requires{
                {Self{{bertrand::alternative<I>, self.__value.template get<I>()++}}} noexcept;
            })
            requires (requires{
                {Self{{bertrand::alternative<I>, self.__value.template get<I>()++}}};
            })
        {
            return {{
                bertrand::alternative<I>,
                self.__value.template get<I>()++
            }};
        }
    };

    template <size_t I>
    struct union_iterator_iadd {
        template <typename Self>
        static constexpr Self& operator()(Self& self, typename Self::difference_type n)
            noexcept (requires{{self.__value.template get<I>() += n} noexcept;})
            requires (requires{{self.__value.template get<I>() += n};})
        {
            self.__value.template get<I>() += n;
            return self;
        }
    };

    template <size_t I>
    struct union_iterator_add {
        template <typename Self>
        static constexpr Self operator()(const Self& self, typename Self::difference_type n)
            noexcept (requires{{
                Self{{bertrand::alternative<I>, self.__value.template get<I>() + n}}
            } noexcept;})
            requires (requires{{
                Self{{bertrand::alternative<I>, self.__value.template get<I>() + n}}
            };})
        {
            return {{bertrand::alternative<I>, self.__value.template get<I>() + n}};
        }
        template <typename Self>
        static constexpr Self operator()(typename Self::difference_type n, const Self& self)
            noexcept (requires{{
                Self{{bertrand::alternative<I>, n + self.__value.template get<I>()}}
            } noexcept;})
            requires (requires{{
                Self{{bertrand::alternative<I>, n + self.__value.template get<I>()}}
            };})
        {
            return {{bertrand::alternative<I>, n + self.__value.template get<I>()}};
        }
    };

    template <size_t I>
    struct union_iterator_predecrement {
        template <typename Self>
        static constexpr Self& operator()(Self& self)
            noexcept (requires{{--self.__value.template get<I>()} noexcept;})
            requires (requires{{--self.__value.template get<I>()};})
        {
            --self.__value.template get<I>();
            return self;
        }
    };

    template <size_t I>
    struct union_iterator_postdecrement {
        template <typename Self>
        static constexpr Self operator()(Self& self)
            noexcept (requires{
                {Self{{bertrand::alternative<I>, self.__value.template get<I>()--}}} noexcept;
            })
            requires (requires{
                {Self{{bertrand::alternative<I>, self.__value.template get<I>()--}}};
            })
        {
            return {{bertrand::alternative<I>, self.__value.template get<I>()--}};
        }
    };

    template <size_t I>
    struct union_iterator_isub {
        template <typename Self>
        static constexpr Self& operator()(Self& self, typename Self::difference_type n)
            noexcept (requires{{self.__value.template get<I>() -= n} noexcept;})
            requires (requires{{self.__value.template get<I>() -= n};})
        {
            self.__value.template get<I>() -= n;
            return self;
        }
    };

    template <size_t I>
    struct union_iterator_sub {
        template <typename Self>
        static constexpr Self operator()(const Self& self, typename Self::difference_type n)
            noexcept (requires{{
                Self{{bertrand::alternative<I>, self.__value.template get<I>() - n}}
            } noexcept;})
            requires (requires{{
                Self{{bertrand::alternative<I>, self.__value.template get<I>() - n}}
            };})
        {
            return {{bertrand::alternative<I>, self.__value.template get<I>() - n}};
        }
    };

    template <typename Difference>
    struct union_iterator_distance {
        template <size_t I>
        struct fn {
            template <typename... L, typename R> requires (!is_union_iterator<R>)
            static constexpr Difference operator()(const union_iterator<L...>& lhs, const R& rhs)
                noexcept (requires{{
                    lhs.__value.template get<I>() - rhs
                } noexcept -> meta::nothrow::convertible_to<Difference>;})
                requires (requires{{
                    lhs.__value.template get<I>() - rhs
                } -> meta::convertible_to<Difference>;})
            {
                return lhs.__value.template get<I>() - rhs;
            }

            template <typename L, typename... R> requires (!is_union_iterator<L>)
            static constexpr Difference operator()(const L& lhs, const union_iterator<R...>& rhs)
                noexcept (requires{{
                    lhs - rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<Difference>;})
                requires (requires{{
                    lhs - rhs.__value.template get<I>()
                } -> meta::convertible_to<Difference>;})
            {
                return lhs - rhs.__value.template get<I>();
            }

            template <typename... L, typename... R> requires (sizeof...(L) == sizeof...(R))
            static constexpr Difference operator()(
                const union_iterator<L...>& lhs,
                const union_iterator<R...>& rhs
            )
                noexcept (requires{{
                    lhs.__value.template get<I>() - rhs.__value.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<Difference>;})
                requires (requires{{
                    lhs.__value.template get<I>() - rhs.__value.template get<I>()
                } -> meta::convertible_to<Difference>;})
            {
                return lhs.__value.template get<I>() - rhs.__value.template get<I>();
            }
        };
    };

    template <size_t I>
    struct union_iterator_less {
        template <typename... L, typename R> requires (!is_union_iterator<R>)
        static constexpr bool operator()(const union_iterator<L...>& lhs, const R& rhs)
            noexcept (requires{{
                lhs.__value.template get<I>() < rhs
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs.__value.template get<I>() < rhs
            } -> meta::convertible_to<bool>;})
        {
            return lhs.__value.template get<I>() < rhs;
        }

        template <typename L, typename... R> requires (!is_union_iterator<L>)
        static constexpr bool operator()(const L& lhs, const union_iterator<R...>& rhs)
            noexcept (requires{{
                lhs < rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs < rhs.__value.template get<I>()
            } -> meta::convertible_to<bool>;})
        {
            return lhs < rhs.__value.template get<I>();
        }

        template <typename... L, typename... R> requires (sizeof...(L) == sizeof...(R))
        static constexpr bool operator()(
            const union_iterator<L...>& lhs,
            const union_iterator<R...>& rhs
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

    template <size_t I>
    struct union_iterator_less_equal {
        template <typename... L, typename R> requires (!is_union_iterator<R>)
        static constexpr bool operator()(const union_iterator<L...>& lhs, const R& rhs)
            noexcept (requires{{
                lhs.__value.template get<I>() <= rhs
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs.__value.template get<I>() <= rhs
            } -> meta::convertible_to<bool>;})
        {
            return lhs.__value.template get<I>() <= rhs;
        }

        template <typename L, typename... R> requires (!is_union_iterator<L>)
        static constexpr bool operator()(const L& lhs, const union_iterator<R...>& rhs)
            noexcept (requires{{
                lhs <= rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs <= rhs.__value.template get<I>()
            } -> meta::convertible_to<bool>;})
        {
            return lhs <= rhs.__value.template get<I>();
        }

        template <typename... L, typename... R> requires (sizeof...(L) == sizeof...(R))
        static constexpr bool operator()(
            const union_iterator<L...>& lhs,
            const union_iterator<R...>& rhs
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

    template <size_t I>
    struct union_iterator_equal {
        template <typename... L, typename R> requires (!is_union_iterator<R>)
        static constexpr bool operator()(const union_iterator<L...>& lhs, const R& rhs)
            noexcept (requires{{
                lhs.__value.template get<I>() == rhs
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs.__value.template get<I>() == rhs
            } -> meta::convertible_to<bool>;})
        {
            return lhs.__value.template get<I>() == rhs;
        }

        template <typename L, typename... R> requires (!is_union_iterator<L>)
        static constexpr bool operator()(const L& lhs, const union_iterator<R...>& rhs)
            noexcept (requires{{
                lhs == rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs == rhs.__value.template get<I>()
            } -> meta::convertible_to<bool>;})
        {
            return lhs == rhs.__value.template get<I>();
        }

        template <typename... L, typename... R> requires (sizeof...(L) == sizeof...(R))
        static constexpr bool operator()(
            const union_iterator<L...>& lhs,
            const union_iterator<R...>& rhs
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

    template <size_t I>
    struct union_iterator_not_equal {
        template <typename... L, typename R> requires (!is_union_iterator<R>)
        static constexpr bool operator()(const union_iterator<L...>& lhs, const R& rhs)
            noexcept (requires{{
                lhs.__value.template get<I>() != rhs
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs.__value.template get<I>() != rhs
            } -> meta::convertible_to<bool>;})
        {
            return lhs.__value.template get<I>() != rhs;
        }

        template <typename L, typename... R> requires (!is_union_iterator<L>)
        static constexpr bool operator()(const L& lhs, const union_iterator<R...>& rhs)
            noexcept (requires{{
                lhs != rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs != rhs.__value.template get<I>()
            } -> meta::convertible_to<bool>;})
        {
            return lhs != rhs.__value.template get<I>();
        }

        template <typename... L, typename... R> requires (sizeof...(L) == sizeof...(R))
        static constexpr bool operator()(
            const union_iterator<L...>& lhs,
            const union_iterator<R...>& rhs
        )
            noexcept (requires{{
                lhs.__value.template get<I>() != rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs.__value.template get<I>() != rhs.__value.template get<I>()
            } -> meta::convertible_to<bool>;})
        {
            return lhs.__value.template get<I>() != rhs.__value.template get<I>();
        }
    };

    template <size_t I>
    struct union_iterator_greater_equal {
        template <typename... L, typename R> requires (!is_union_iterator<R>)
        static constexpr bool operator()(const union_iterator<L...>& lhs, const R& rhs)
            noexcept (requires{{
                lhs.__value.template get<I>() >= rhs
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs.__value.template get<I>() >= rhs
            } -> meta::convertible_to<bool>;})
        {
            return lhs.__value.template get<I>() >= rhs;
        }

        template <typename L, typename... R> requires (!is_union_iterator<L>)
        static constexpr bool operator()(const L& lhs, const union_iterator<R...>& rhs)
            noexcept (requires{{
                lhs >= rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs >= rhs.__value.template get<I>()
            } -> meta::convertible_to<bool>;})
        {
            return lhs >= rhs.__value.template get<I>();
        }

        template <typename... L, typename... R> requires (sizeof...(L) == sizeof...(R))
        static constexpr bool operator()(
            const union_iterator<L...>& lhs,
            const union_iterator<R...>& rhs
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

    template <size_t I>
    struct union_iterator_greater {
        template <typename... L, typename R> requires (!is_union_iterator<R>)
        static constexpr bool operator()(const union_iterator<L...>& lhs, const R& rhs)
            noexcept (requires{{
                lhs.__value.template get<I>() > rhs
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs.__value.template get<I>() > rhs
            } -> meta::convertible_to<bool>;})
        {
            return lhs.__value.template get<I>() > rhs;
        }

        template <typename L, typename... R> requires (!is_union_iterator<L>)
        static constexpr bool operator()(const L& lhs, const union_iterator<R...>& rhs)
            noexcept (requires{{
                lhs > rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                lhs > rhs.__value.template get<I>()
            } -> meta::convertible_to<bool>;})
        {
            return lhs > rhs.__value.template get<I>();
        }

        template <typename... L, typename... R> requires (sizeof...(L) == sizeof...(R))
        static constexpr bool operator()(
            const union_iterator<L...>& lhs,
            const union_iterator<R...>& rhs
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

    template <size_t I>
    struct union_iterator_three_way {
        template <typename... L, typename R> requires (!is_union_iterator<R>)
        static constexpr auto operator()(const union_iterator<L...>& lhs, const R& rhs)
            noexcept (requires{{
                lhs.__value.template get<I>() <=> rhs
            } noexcept -> meta::nothrow::convertible_to<
                meta::common_type<meta::spaceship_type<const L&, const R&>...>
            >;})
            -> meta::common_type<meta::spaceship_type<const L&, const R&>...>
            requires (
                (meta::has_spaceship<const L&, const R&> && ...) &&
                meta::has_common_type<meta::spaceship_type<const L&, const R&>...>
            )
        {
            return lhs.__value.template get<I>() <=> rhs;
        }
        template <typename L, typename... R> requires (!is_union_iterator<L>)
        static constexpr auto operator()(const L& lhs, const union_iterator<R...>& rhs)
            noexcept (requires{{
                lhs <=> rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<
                meta::common_type<meta::spaceship_type<const L&, const R&>...>
            >;})
            -> meta::common_type<meta::spaceship_type<const L&, const R&>...>
            requires (
                (meta::has_spaceship<const L&, const R&> && ...) &&
                meta::has_common_type<meta::spaceship_type<const L&, const R&>...>
            )
        {
            return lhs <=> rhs.__value.template get<I>();
        }
        template <typename... L, typename... R> requires (sizeof...(L) == sizeof...(R))
        static constexpr auto operator()(
            const union_iterator<L...>& lhs,
            const union_iterator<R...>& rhs
        )
            noexcept (requires{{
                lhs.__value.template get<I>() <=> rhs.__value.template get<I>()
            } noexcept -> meta::nothrow::convertible_to<
                meta::common_type<meta::spaceship_type<const L&, const R&>...>
            >;})
            -> meta::common_type<meta::spaceship_type<const L&, const R&>...>
            requires (
                (meta::has_spaceship<const L&, const R&> && ...) &&
                meta::has_common_type<meta::spaceship_type<const L&, const R&>...>
            )
        {
            return lhs.__value.template get<I>() <=> rhs.__value.template get<I>();
        }
    };

    /* A union of iterator types `Ts...`, which attempts to forward their combined
    interface as faithfully as possible.  All operations are enabled if each of the
    underlying types supports them, and will use vtables to exhaustively cover them.
    If any operations would cause the result to narrow to a single type, then that
    type will be returned directly, else it will be returned as a union of types,
    except where otherwise specified.  Iteration performance will be reduced slightly
    due to the extra dynamic dispatch, but should otherwise not degrade functionality
    in any way. */
    template <meta::unqualified... Ts> requires (sizeof...(Ts) > 1)
    struct union_iterator {
        using types = meta::pack<Ts...>;
        using iterator_category = meta::common_type<meta::iterator_category<Ts>...>;
        using difference_type = meta::common_type<meta::iterator_difference<Ts>...>;
        using reference = meta::make_union<meta::iterator_reference<Ts>...>;
        using value_type = meta::make_union<meta::iterator_value<Ts>...>;
        using pointer = meta::make_union<meta::iterator_pointer<Ts>...>;

        basic_union<Ts...> __value;

    private:
        using deref = impl::basic_vtable<
            impl::union_iterator_deref<reference>::template fn,
            sizeof...(Ts)
        >;
        using subscript = impl::basic_vtable<
            impl::union_iterator_subscript<reference, difference_type>::template fn,
            sizeof...(Ts)
        >;
        using preincrement = impl::basic_vtable<impl::union_iterator_preincrement, sizeof...(Ts)>;
        using postincrement = impl::basic_vtable<impl::union_iterator_postincrement, sizeof...(Ts)>;
        using iadd = impl::basic_vtable<impl::union_iterator_iadd, sizeof...(Ts)>;
        using add = impl::basic_vtable<impl::union_iterator_add, sizeof...(Ts)>;
        using predecrement = impl::basic_vtable<impl::union_iterator_predecrement, sizeof...(Ts)>;
        using postdecrement = impl::basic_vtable<impl::union_iterator_postdecrement, sizeof...(Ts)>;
        using isub = impl::basic_vtable<impl::union_iterator_isub, sizeof...(Ts)>;
        using sub = impl::basic_vtable<impl::union_iterator_sub, sizeof...(Ts)>;
        using distance = impl::basic_vtable<
            impl::union_iterator_distance<difference_type>::template fn,
            sizeof...(Ts)
        >;
        using less = impl::basic_vtable<impl::union_iterator_less, sizeof...(Ts)>;
        using less_equal = impl::basic_vtable<impl::union_iterator_less_equal, sizeof...(Ts)>;
        using equal = impl::basic_vtable<impl::union_iterator_equal, sizeof...(Ts)>;
        using not_equal = impl::basic_vtable<impl::union_iterator_not_equal, sizeof...(Ts)>;
        using greater_equal = impl::basic_vtable<impl::union_iterator_greater_equal, sizeof...(Ts)>;
        using greater = impl::basic_vtable<impl::union_iterator_greater, sizeof...(Ts)>;
        using three_way = impl::basic_vtable<impl::union_iterator_three_way, sizeof...(Ts)>;

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
            noexcept (requires{{subscript{__value.index()}(*this, n)} noexcept;})
            requires (requires{{subscript{__value.index()}(*this, n)};})
        {
            return subscript{__value.index()}(*this, n);
        }

        constexpr union_iterator& operator++()
            noexcept (requires{{preincrement{__value.index()}(*this)} noexcept;})
            requires (requires{{preincrement{__value.index()}(*this)};})
        {
            return preincrement{__value.index()}(*this);
        }

        [[nodiscard]] constexpr union_iterator operator++(int)
            noexcept (requires{{postincrement{__value.index()}(*this)} noexcept;})
            requires (requires{{postincrement{__value.index()}(*this)};})
        {
            return postincrement{__value.index()}(*this);
        }

        constexpr union_iterator& operator+=(difference_type n)
            noexcept (requires{{iadd{__value.index()}(*this, n)} noexcept;})
            requires (requires{{iadd{__value.index()}(*this, n)};})
        {
            return iadd{__value.index()}(*this, n);
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

        constexpr union_iterator& operator--()
            noexcept (requires{{predecrement{__value.index()}(*this)} noexcept;})
            requires (requires{{predecrement{__value.index()}(*this)};})
        {
            return predecrement{__value.index()}(*this);
        }

        [[nodiscard]] constexpr union_iterator operator--(int)
            noexcept (requires{{postdecrement{__value.index()}(*this)} noexcept;})
            requires (postdecrement{__value.index()}(*this))
        {
            return postdecrement{__value.index()}(*this);
        }

        constexpr union_iterator& operator-=(difference_type n)
            noexcept (requires{{isub{__value.index()}(*this, n)} noexcept;})
            requires (requires{{isub{__value.index()}(*this, n)};})
        {
            return isub{__value.index()}(*this, n);
        }

        [[nodiscard]] constexpr union_iterator operator-(difference_type n)
            noexcept (requires{{sub{__value.index()}(*this, n)} noexcept;})
            requires (requires{{sub{__value.index()}(*this, n)};})
        {
            return sub{__value.index()}(*this, n);
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

        template <typename other> requires (!is_union_iterator<other>)
        [[nodiscard]] friend constexpr difference_type operator-(
            const other& rhs,
            const union_iterator& lhs
        )
            noexcept (requires{{distance{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{distance{lhs.__value.index()}(lhs, rhs)};})
        {
            return distance{lhs.__value.index()}(lhs, rhs);
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

        template <typename other> requires (!is_union_iterator<other>)
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

        template <typename other> requires (!is_union_iterator<other>)
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

        template <typename other> requires (!is_union_iterator<other>)
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
            noexcept (requires{{not_equal{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{not_equal{lhs.__value.index()}(lhs, rhs)};})
        {
            return not_equal{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other> requires (!is_union_iterator<other>)
        [[nodiscard]] friend constexpr bool operator!=(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{not_equal{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{not_equal{rhs.__value.index()}(lhs, rhs)};})
        {
            return not_equal{rhs.__value.index()}(lhs, rhs);
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

        template <typename other> requires (!is_union_iterator<other>)
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

        template <typename other> requires (!is_union_iterator<other>)
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
            noexcept (requires{{three_way{lhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{three_way{lhs.__value.index()}(lhs, rhs)};})
        {
            return three_way{lhs.__value.index()}(lhs, rhs);
        }

        template <typename other> requires (!is_union_iterator<other>)
        [[nodiscard]] friend constexpr decltype(auto) operator<=>(
            const other& lhs,
            const union_iterator& rhs
        )
            noexcept (requires{{three_way{rhs.__value.index()}(lhs, rhs)} noexcept;})
            requires (requires{{three_way{rhs.__value.index()}(lhs, rhs)};})
        {
            return three_way{rhs.__value.index()}(lhs, rhs);
        }
    };

    /* If all alternatives share a common begin/end type, then those iterators will be
    returned directly.  Otherwise, they will be merged into a `union_iterator` with
    aligned alternatives. */
    template <typename Begin, typename End>
    struct as_union_iterator {
        using begin = Begin::template eval<union_iterator>;
        using end = End::template eval<union_iterator>;
    };
    template <typename... Begin, typename... End>
        requires (
            meta::to_unique<Begin...>::size() == 1 &&
            meta::to_unique<End...>::size() == 1
        )
    struct as_union_iterator<meta::pack<Begin...>, meta::pack<End...>> {
        using begin = meta::first_type<Begin...>;
        using end = meta::first_type<End...>;
    };
    template <typename Return>
    struct make_union_iterator {
        template <size_t I>
        struct begin {
            template <typename Self> requires (!is_union_iterator<Return>)
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::begin(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::begin(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return meta::begin(std::forward<Self>(self).__value.template get<I>());
            }
            template <typename Self> requires (is_union_iterator<Return>)
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{Return{{
                    bertrand::alternative<I>,
                    meta::begin(std::forward<Self>(self).__value.template get<I>())
                }}} noexcept;})
                requires (requires{{Return{{
                    bertrand::alternative<I>,
                    meta::begin(std::forward<Self>(self).__value.template get<I>())
                }}};})
            {
                return {{
                    bertrand::alternative<I>,
                    meta::begin(std::forward<Self>(self).__value.template get<I>())
                }};
            }
        };

        template <size_t I>
        struct end {
            template <typename Self> requires (!is_union_iterator<Return>)
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::end(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::end(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return meta::end(std::forward<Self>(self).__value.template get<I>());
            }
            template <typename Self> requires (is_union_iterator<Return>)
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{Return{{
                    bertrand::alternative<I>,
                    meta::end(std::forward<Self>(self).__value.template get<I>())
                }}} noexcept;})
                requires (requires{{Return{{
                    bertrand::alternative<I>,
                    meta::end(std::forward<Self>(self).__value.template get<I>())
                }}};})
            {
                return {{
                    bertrand::alternative<I>,
                    meta::end(std::forward<Self>(self).__value.template get<I>())
                }};
            }
        };

        template <size_t I>
        struct rbegin {
            template <typename Self> requires (!is_union_iterator<Return>)
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::rbegin(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::rbegin(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return meta::rbegin(std::forward<Self>(self).__value.template get<I>());
            }
            template <typename Self> requires (is_union_iterator<Return>)
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{Return{{
                    bertrand::alternative<I>,
                    meta::rbegin(std::forward<Self>(self).__value.template get<I>())
                }}} noexcept;})
                requires (requires{{Return{{
                    bertrand::alternative<I>,
                    meta::rbegin(std::forward<Self>(self).__value.template get<I>())
                }}};})
            {
                return {{
                    bertrand::alternative<I>,
                    meta::rbegin(std::forward<Self>(self).__value.template get<I>())
                }};
            }
        };

        template <size_t I>
        struct rend {
            template <typename Self> requires (!is_union_iterator<Return>)
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{
                    meta::rend(std::forward<Self>(self).__value.template get<I>())
                } noexcept -> meta::nothrow::convertible_to<Return>;})
                requires (requires{{
                    meta::rend(std::forward<Self>(self).__value.template get<I>())
                } -> meta::convertible_to<Return>;})
            {
                return meta::rend(std::forward<Self>(self).__value.template get<I>());
            }
            template <typename Self> requires (is_union_iterator<Return>)
            [[nodiscard]] static constexpr Return operator()(Self&& self)
                noexcept (requires{{Return{{
                    bertrand::alternative<I>,
                    meta::rend(std::forward<Self>(self).__value.template get<I>())
                }}} noexcept;})
                requires (requires{{Return{{
                    bertrand::alternative<I>,
                    meta::rend(std::forward<Self>(self).__value.template get<I>())
                }}};})
            {
                return {{
                    bertrand::alternative<I>,
                    meta::rend(std::forward<Self>(self).__value.template get<I>())
                }};
            }
        };
    };

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
struct Union {
    using __type = meta::pack<Ts...>;
    [[no_unique_address]] impl::basic_union<Ts...> __value;

    /* Default constructor finds the first type in `Ts...` that can be default
    constructed.  If no such type exists, then the default constructor is disabled. */
    [[nodiscard]] constexpr Union()
        noexcept (meta::nothrow::default_constructible<impl::basic_union<Ts...>>)
        requires (meta::not_void<typename impl::basic_union<Ts...>::default_type>)
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
        noexcept (meta::nothrow::visit_exhaustive<impl::union_convert_from<Ts...>, from>)
        requires (meta::visit_exhaustive<impl::union_convert_from<Ts...>, from>)
    :
        __value(impl::visit(
            impl::union_convert_from<Ts...>{},
            std::forward<from>(v)
        ))
    {}

    /* Construct a union with the alternative at index `I` using a
    `std::in_place_index<I>` disambiguation tag, forwarding the remaining arguments to
    that alternative's constructor.  This is more explicit than using the standard
    constructors, for cases where only a specific alternative should be considered.
    Additionally, users should note that due to flattening of nested monads, the index
    may not exactly match the template signature for this class, but is guaranteed to
    always align with `impl::visitable<Union<Ts...>>::alternatives`. */
    template <size_t I, typename... A> requires (I < sizeof...(Ts))
    [[nodiscard]] constexpr Union(std::in_place_index_t<I> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, Ts...>, A...>)
        requires (meta::constructible_from<meta::unpack_type<I, Ts...>, A...>)
    :
        __value{tag, std::forward<A>(args)...}
    {}

    /* Explicitly construct a union with the specified alternative using the given
    arguments.  This is more explicit than using the standard constructors, for cases
    where only a specific subset of alternatives should be considered.  If the
    specified alternative is given as a visitable type, then each of its alternatives
    must be present in the union's template signature.  All arguments after the
    disambiguation tag will be perfectly forwarded to the indicated type's
    constructor. */
    template <typename T, typename... A> requires (impl::union_init_subset<Union, T>)
    [[nodiscard]] constexpr Union(std::type_identity<T> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<T, A...>)
        requires (!meta::visitable<T> && meta::constructible_from<T, A...>)
    :
        __value{bertrand::alternative<meta::index_of<T, Ts...>>, std::forward<A>(args)...}
    {}
    template <typename T, typename... A> requires (impl::union_init_subset<Union, T>)
    [[nodiscard]] constexpr Union(std::type_identity<T> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<Union, T>)
        requires (meta::visitable<T> && meta::constructible_from<T, A...>)
    :
        Union(T(std::forward<A>(args)...))
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
            !meta::visit_exhaustive<impl::union_convert_from<Ts...>, A...> &&
            meta::visit_exhaustive<impl::union_construct_from<Ts...>, A...>
        )
    :
        __value(impl::visit(impl::union_construct_from<Ts...>{}, std::forward<A>(args)...))
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
        noexcept (requires{{impl::basic_vtable<
            impl::union_convert_to<to>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires (
            !meta::prefer_constructor<to> &&
            (meta::convertible_to<
                decltype((std::forward<Self>(self).__value.template get<Ts>())),
                to
            > && ...)
        )
    {
        return impl::basic_vtable<
            impl::union_convert_to<to>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Explicit conversion operator allows functional-style conversions toward any type
    to which all alternatives can be explicitly converted.  This allows conversion to
    scalar types as well as union types (regardless of source) that satisfy the
    conversion criteria, and will only be considered if an implicit conversion would
    be malformed. */
    template <typename Self, typename to>
    [[nodiscard]] constexpr explicit operator to(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::union_cast_to<to>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires (
            !meta::prefer_constructor<to> &&
            (!meta::convertible_to<
                decltype((std::forward<Self>(self).__value.template get<Ts>())),
                to
            > || ...) &&
            (meta::explicitly_convertible_to<
                decltype((std::forward<Self>(self).__value.template get<Ts>())),
                to
            > && ...)
        )
    {
        return impl::basic_vtable<
            impl::union_cast_to<to>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Flatten the union into a common type, assuming one exists.  Fails to compile if
    no common type can be found.  Note that the contents will be perfectly forwarded
    according to their storage qualifiers as well as those of the union itself. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::union_convert_to<meta::common_type<
                decltype((std::forward<Self>(self).__value.template get<Ts>()))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires (meta::has_common_type<
            decltype((std::forward<Self>(self).__value.template get<Ts>()))...
        >)
    {
        return (impl::basic_vtable<
            impl::union_convert_to<meta::common_type<
                decltype((std::forward<Self>(self).__value.template get<Ts>()))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self)));
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

    /* Returns the result of `meta::size()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr auto size(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::union_size<meta::common_type<
                decltype((meta::size(std::forward<Self>(self).__value.template get<Ts>())))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires (
            (meta::has_size<
                decltype((std::forward<Self>(self).__value.template get<Ts>()))
            > && ...) &&
            (meta::has_common_type<
                decltype((meta::size(std::forward<Self>(self).__value.template get<Ts>())))...
            >)
        )
    {
        return impl::basic_vtable<
            impl::union_size<meta::common_type<
                decltype((meta::size(std::forward<Self>(self).__value.template get<Ts>())))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Returns the result of `meta::ssize()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr auto ssize(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::union_ssize<meta::common_type<
                decltype((meta::ssize(std::forward<Self>(self).__value.template get<Ts>())))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires (
            (meta::has_ssize<
                decltype((std::forward<Self>(self).__value.template get<Ts>()))
            > && ...) &&
            (meta::has_common_type<
                decltype((meta::ssize(std::forward<Self>(self).__value.template get<Ts>())))...
            >)
        )
    {
        return impl::basic_vtable<
            impl::union_ssize<meta::common_type<
                decltype((meta::ssize(std::forward<Self>(self).__value.template get<Ts>())))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Returns the result of `meta::empty()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr bool empty(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::union_empty,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires ((meta::has_empty<
            decltype((std::forward<Self>(self).__value.template get<Ts>()))
        > && ...))
    {
        return impl::basic_vtable<
            impl::union_empty,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Returns the result of `meta::data()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr auto data(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::union_data<meta::common_type<
                decltype((meta::data(std::forward<Self>(self).__value.template get<Ts>())))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires (
            (meta::has_data<
                decltype((std::forward<Self>(self).__value.template get<Ts>()))
            > && ...) &&
            (meta::has_common_type<
                decltype((meta::data(std::forward<Self>(self).__value.template get<Ts>())))...
            >)
        )
    {
        return impl::basic_vtable<
            impl::union_data<meta::common_type<
                decltype((meta::data(std::forward<Self>(self).__value.template get<Ts>())))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Returns the result of `meta::shape()` on the current alternative if it is
    well-formed and all results share a common type.  Fails to compile otherwise. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) shape(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::union_shape<meta::common_type<
                decltype((meta::shape(std::forward<Self>(self).__value.template get<Ts>())))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires (
            (meta::has_shape<
                decltype((std::forward<Self>(self).__value.template get<Ts>()))
            > && ...) &&
            meta::has_common_type<
                decltype((meta::shape(std::forward<Self>(self).__value.template get<Ts>())))...
            >
        )
    {
        return (impl::basic_vtable<
            impl::union_shape<meta::common_type<
                decltype((meta::shape(std::forward<Self>(self).__value.template get<Ts>())))...
            >>::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self)));
    }

    /* Forward tuple access to `Ts...`, assuming they all support it and have the same
    size.  If so, then the return type may be a further union if the indexed types
    differ. */
    template <auto... A, typename Self>
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::union_get<
                meta::make_union<
                    decltype(meta::get<A...>(std::forward<Self>(self).__value.template get<Ts>()))...
                >,
                A...
            >::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires ((meta::has_get<
            decltype((std::forward<Self>(self).__value.template get<Ts>())),
            A...
        > && ...))
    {
        return (impl::basic_vtable<
            impl::union_get<
                meta::make_union<
                    decltype(meta::get<A...>(std::forward<Self>(self).__value.template get<Ts>()))...
                >,
                A...
            >::template fn,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self)));
    }

    /* Get a forward iterator over the union, assuming all alternatives are iterable.
    Fails to compile otherwise.  The result is either passed through as-is if all
    alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::make_union_iterator<
                typename impl::as_union_iterator<
                    meta::pack<decltype(meta::begin(
                        std::forward<Self>(self).__value.template get<Ts>())
                    )...>,
                    meta::pack<decltype(meta::end(
                        std::forward<Self>(self).__value.template get<Ts>())
                    )...>
                >::begin
            >::template begin,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires ((meta::iterable<
            decltype((std::forward<Self>(self).__value.template get<Ts>()))
        > && ...))
    {
        return impl::basic_vtable<
            impl::make_union_iterator<
                typename impl::as_union_iterator<
                    meta::pack<decltype(meta::begin(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>,
                    meta::pack<decltype(meta::end(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>
                >::begin
            >::template begin,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Get a forward sentinel for the union, assuming all alternatives are iterable.
    Fails to compile otherwise.  The result is either passed through as-is if all
    alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::make_union_iterator<
                typename impl::as_union_iterator<
                    meta::pack<decltype(meta::begin(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>,
                    meta::pack<decltype(meta::end(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>
                >::end
            >::template end,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires ((meta::iterable<
            decltype((std::forward<Self>(self).__value.template get<Ts>()))
        > && ...))
    {
        return impl::basic_vtable<
            impl::make_union_iterator<
                typename impl::as_union_iterator<
                    meta::pack<decltype(meta::begin(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>,
                    meta::pack<decltype(meta::end(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>
                >::end
            >::template end,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Get a reverse iterator over the union, assuming all alternatives are reverse
    iterable.  Fails to compile otherwise.  The result is either passed through as-is
    if all alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::make_union_iterator<
                typename impl::as_union_iterator<
                    meta::pack<decltype(meta::rbegin(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>,
                    meta::pack<decltype(meta::rend(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>
                >::begin
            >::template rbegin,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires ((meta::reverse_iterable<
            decltype((std::forward<Self>(self).__value.template get<Ts>()))
        > && ...))
    {
        return impl::basic_vtable<
            impl::make_union_iterator<
                typename impl::as_union_iterator<
                    meta::pack<decltype(meta::rbegin(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>,
                    meta::pack<decltype(meta::rend(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>
                >::begin
            >::template rbegin,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
    }

    /* Get a reverse sentinel for the union, assuming all alternatives are reverse
    iterable.  Fails to compile otherwise.  The result is either passed through as-is
    if all alternatives resolve to the same underlying iterator type, or a specialized
    `union_iterator` wrapper that encapsulates multiple iterator types and forwards
    their overall interface.  Iteration performance may be slightly degraded in the
    latter case due to an extra vtable lookup for each iterator operation. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::make_union_iterator<
                typename impl::as_union_iterator<
                    meta::pack<decltype(meta::rbegin(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>,
                    meta::pack<decltype(meta::rend(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>
                >::end
            >::template rend,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires ((meta::reverse_iterable<
            decltype((std::forward<Self>(self).__value.template get<Ts>()))
        > && ...))
    {
        return impl::basic_vtable<
            impl::make_union_iterator<
                typename impl::as_union_iterator<
                    meta::pack<decltype(meta::rbegin(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>,
                    meta::pack<decltype(meta::rend(
                        std::forward<Self>(self).__value.template get<Ts>()
                    ))...>
                >::end
            >::template rend,
            sizeof...(Ts)
        >{self.__value.index()}(std::forward<Self>(self));
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
    template <meta::lvalue ref> requires (meta::has_address<ref>)
    struct basic_union<NoneType, ref> {
        using types = meta::pack<NoneType, ref>;
        using default_type = NoneType;
        using ptr = meta::address_type<ref>;
        using const_ptr = meta::address_type<meta::as_const<ref>>;

        template <size_t I> requires (I < 2)
        using tag = std::in_place_index_t<I>;

        static constexpr size_t size() noexcept { return 2; }
        static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        static constexpr bool empty() noexcept { return false; }

        template <typename T>
        [[nodiscard]] static constexpr bool contains() noexcept {
            return types::template contains<T>();
        }

        template <typename T> requires (contains<T>())
        [[nodiscard]] static constexpr size_t find() noexcept {
            return types::template index<T>();
        }

        [[no_unique_address]] ptr m_data = nullptr;

        [[nodiscard]] constexpr basic_union() noexcept = default;

        /* Default constructor always initializes to the empty state. */
        [[nodiscard]] constexpr basic_union(tag<0>, NoneType) noexcept : m_data(nullptr) {};

        /* Tagged constructor specifically initializes the alternative at index `I`
        with the given arguments. */
        [[nodiscard]] constexpr explicit basic_union(tag<1>, ref r)
            noexcept (meta::nothrow::has_address<ref>)
        :
            m_data(std::addressof(r))
        {}

        /* Special constructor that takes a pointer directly, enabling direct
        conversions. */
        [[nodiscard]] constexpr explicit basic_union(ptr p) noexcept : m_data(p) {}

        /* Swap the contents of two unions as efficiently as possible. */
        constexpr void swap(basic_union& other) noexcept {
            meta::swap(m_data, other.m_data);
        }

        /* Return the index of the active alternative. */
        [[nodiscard]] constexpr size_t index() const noexcept { return m_data != nullptr; }

        /* Access a specific value by index, where the index is known at compile
        time. */
        template <size_t I> requires (I < 2)
        [[nodiscard]] constexpr auto& get() noexcept {
            if constexpr (I == 0) {
                return None;
            } else {
                return *m_data;
            }
        }
        template <size_t I> requires (I < 2)
        [[nodiscard]] constexpr auto& get() const noexcept {
            if constexpr (I == 0) {
                return None;
            } else {
                return *static_cast<const_ptr>(m_data);
            }
        }

        /* Access a specific value by index, where the index is known at compile
        time. */
        template <typename T> requires (types::template contains<T>())
        [[nodiscard]] constexpr auto& get() noexcept {
            if constexpr (std::is_same_v<T, NoneType>) {
                return None;
            } else {
                return *m_data;
            }
        }
        template <typename T> requires (types::template contains<T>())
        [[nodiscard]] constexpr auto& get() const noexcept {
            if constexpr (std::is_same_v<T, NoneType>) {
                return None;
            } else {
                return *m_data;
            }
        }

        /* Access a specific value by index, where the index is known at compile
        time. */
        template <size_t I> requires (I < 2)
        [[nodiscard]] constexpr auto& get_if() noexcept {
            if constexpr (I == 0) {
                return None;
            } else {
                return *m_data;
            }
        }
        template <size_t I> requires (I < 2)
        [[nodiscard]] constexpr auto& get_if() const noexcept {
            if constexpr (I == 0) {
                return None;
            } else {
                return *m_data;
            }
        }
    };

    /* A simple visitor that backs the implicit constructor for an `Optional<T>`
    object, returning a corresponding `impl::basic_union` primitive type. */
    template <typename T, typename...>
    struct optional_convert_from {};
    template <typename T, typename in>
    struct optional_convert_from<T, in> {
        using result = impl::basic_union<NoneType, T>;

        // 1) prefer direct conversion to `T` if possible
        template <typename alt>
        static constexpr result operator()(alt&& v)
            noexcept (meta::nothrow::convertible_to<alt, T>)
            requires (meta::convertible_to<alt, T>)
        {
            return result{bertrand::alternative<1>, std::forward<alt>(v)};
        }

        // 2) otherwise, if the argument is in an empty state as defined by the input's
        // `impl::visitable` specification, or is given as a none sentinel, then we
        // return it as an empty `basic_union` object. 
        template <typename alt>
        static constexpr result operator()(alt&&)
            noexcept (meta::nothrow::default_constructible<impl::basic_union<NoneType, T>>)
            requires (
                !meta::convertible_to<alt, T> &&
                (
                    std::same_as<alt, typename impl::visitable<alt>::empty> ||
                    std::same_as<alt, typename impl::visitable<in>::empty>
                )
            )
        {
            return result{};
        }

        // 3) if `type` is an lvalue, then an extra conversion is enabled from raw
        // pointers, where nullptr gets translated into an empty `basic_union`
        // object, exploiting the pointer optimization to avoid conditionals.
        template <typename alt>
        static constexpr result operator()(alt&& p)
            noexcept (meta::nothrow::convertible_to<alt, meta::address_type<T>>)
            requires (
                !meta::convertible_to<alt, T> &&
                (
                    !std::same_as<alt, typename impl::visitable<alt>::empty> &&
                    !std::same_as<alt, typename impl::visitable<in>::empty>
                ) &&
                meta::lvalue<T> &&
                meta::has_address<T> &&
                meta::convertible_to<alt, meta::address_type<T>>
            )
        {
            return result{p};
        }
    };
    template <typename T, typename in>
        requires (meta::is_void<T> || std::same_as<T, typename impl::visitable<T>::empty>)
    struct optional_convert_from<T, in> {
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
                (
                    std::same_as<alt, typename impl::visitable<alt>::empty> ||
                    std::same_as<alt, typename impl::visitable<in>::empty>
                )
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
        template <typename... A>
        static constexpr auto operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<T, A...>)
            requires (meta::constructible_from<T, A...>)
        {
            return impl::basic_union<NoneType, T>{
                bertrand::alternative<1>,
                std::forward<A>(args)...
            };
        }
    };
    template <typename T>
        requires (meta::is_void<T> || std::same_as<T, typename impl::visitable<T>::empty>)
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
    which attempts a normal visitor conversion where possible, faLLing back to
    default-constructing other optional types or possibly pointers in the case of
    optional references. */
    template <typename Self, typename to>
    struct optional_convert_to {
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
                noexcept (meta::nothrow::default_constructible<to>)
                requires (
                    !convert &&
                    I == 0 &&
                    meta::not_void<typename impl::visitable<to>::empty> &&
                    meta::default_constructible<to>
                )
            {
                return to{};
            }

            static constexpr to operator()(meta::forward<Self> self)
                noexcept (requires{{
                    std::addressof(self.__value.template get<1>())
                } -> meta::nothrow::convertible_to<to>;})
                requires (
                    !convert &&
                    meta::is_void<typename impl::visitable<to>::empty> &&
                    meta::lvalue<typename impl::visitable<Self>::lookup::template at<1>> &&
                    meta::pointer<to> &&
                    requires{{
                        std::addressof(self.__value.template get<1>())
                    } -> meta::convertible_to<to>;}
                )
            {
                if constexpr (I == 0) {
                    return nullptr;
                } else {
                    return std::addressof(self.__value.template get<1>());
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

        static constexpr to operator()(meta::forward<Self> self)
            noexcept (requires{{
                std::forward<Self>(self).__value.template get<0>()
            } noexcept -> meta::nothrow::convertible_to<to>;})
            requires (convert)
        {
            return std::forward<Self>(self).__value.template get<0>();
        }

        static constexpr to operator()(meta::forward<Self> self)
            noexcept (meta::nothrow::default_constructible<to>)
            requires (
                !convert &&
                meta::not_void<typename impl::visitable<to>::empty> &&
                meta::default_constructible<to>
            )
        {
            return to{};
        }

        static constexpr to operator()(meta::forward<Self> self) noexcept
            requires (
                !convert &&
                meta::is_void<typename impl::visitable<to>::empty> &&
                meta::pointer<to>
            )
        {
            return nullptr;
        }
    };

    /* A simple visitor that backs the explicit conversion operator from `Optional<T>`,
    which attempts a normal visitor conversion where possible, falling back to
    default-constructing other optional types or possibly pointers in the case of
    optional references. */
    template <typename Self, typename to>
    struct optional_cast_to {
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
                noexcept (meta::nothrow::default_constructible<to>)
                requires (
                    !convert &&
                    I == 0 &&
                    meta::not_void<typename impl::visitable<to>::empty> &&
                    meta::default_constructible<to>
                )
            {
                return to{};
            }

            static constexpr to operator()(meta::forward<Self> self) noexcept
                requires (
                    !convert &&
                    meta::is_void<typename impl::visitable<to>::empty> &&
                    meta::lvalue<typename impl::visitable<Self>::lookup::template at<1>> &&
                    meta::pointer<to> &&
                    requires{{
                        std::addressof(self.__value.template get<1>())
                    } -> meta::explicitly_convertible_to<to>;}
                )
            {
                if constexpr (I == 0) {
                    return nullptr;
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
            noexcept (meta::nothrow::default_constructible<to>)
            requires (
                !convert &&
                meta::not_void<typename impl::visitable<to>::empty> &&
                meta::default_constructible<to>
            )
        {
            return to{};
        }

        static constexpr to operator()(meta::forward<Self> self) noexcept
            requires (
                !convert &&
                meta::is_void<typename impl::visitable<to>::empty> &&
                meta::pointer<to>
            )
        {
            return nullptr;
        }
    };

    /* A simple visitor that backs the tuple indexing operator for `Optional<T>`, where
    `T` is tuple-like, and the return type is promoted to an optional. */
    template <auto... A>
    struct optional_get {
        template <typename Self>
        using type = Optional<meta::remove_rvalue<
            meta::get_type<decltype((*std::declval<Self>())), A...>
        >>;

        template <size_t I>
        struct fn {
            template <typename Self>
            static constexpr type<Self> operator()(Self&& self)
                noexcept (requires{{meta::get<A...>(
                    visitable<Self>::template get<I>(std::forward<Self>(self))
                )} noexcept -> meta::nothrow::convertible_to<type<Self>>;})
                requires (requires{{meta::get<A...>(
                    visitable<Self>::template get<I>(std::forward<Self>(self))
                )} -> meta::convertible_to<type<Self>>;})
            {
                return meta::get<A...>(
                    visitable<Self>::template get<I>(std::forward<Self>(self))
                );
            }
        };
        template <>
        struct fn<0> {
            template <typename Self>
            static constexpr type<Self> operator()(Self&& self)
                noexcept (meta::nothrow::default_constructible<type<Self>>)
                requires (meta::default_constructible<type<Self>>)
            {
                return {};
            }
        };
    };

    /* A wrapper for an arbitrary iterator that allows it to be constructed in an empty
    state in order to represent empty optionals.  This is the iterator type for
    `Optional<T>`, where `T` is iterable.  In that case, this iterator behaves exactly
    like the underlying iterator type, with the caveat that it can only be compared
    against other `optional_iterator<U>` wrappers, where `U` may be different from
    `T`. */
    template <meta::unqualified T>
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
            noexcept (requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
            requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
        {
            return impl::arrow{*std::forward<Self>(self)};
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
    optional_iterator(T) -> optional_iterator<T>;

    /* A trivial iterator that represents an empty range.  Instances of this type
    always compare equal.  The template parameter sets the `reference` type returned by
    the dereference operators, which is useful for metaprogramming purposes.  No values
    will actually be yielded. */
    template <meta::not_void T = const NoneType&>
    struct empty_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = T;
        using value_type = meta::remove_reference<T>;
        using pointer = meta::as_pointer<reference>;

        [[noreturn]] constexpr reference operator*() { throw StopIteration(); }
        [[noreturn]] constexpr reference operator*() const { throw StopIteration(); }
        [[noreturn]] constexpr pointer operator->() { throw StopIteration(); }
        [[noreturn]] constexpr pointer operator->() const { throw StopIteration(); }
        [[noreturn]] constexpr reference operator[](difference_type) { throw StopIteration(); }
        [[noreturn]] constexpr reference operator[](difference_type) const {
            throw StopIteration();
        }
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

    template <typename Self>
    concept optional_forward_iterable = requires(Self self) {
        {meta::begin(*std::forward<Self>(self))};
        {meta::end(*std::forward<Self>(self))};
    };

    template <typename Self>
    concept optional_reverse_iterable = requires(Self self) {
        {meta::rbegin(*std::forward<Self>(self))};
        {meta::rend(*std::forward<Self>(self))};
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
template <meta::not_rvalue T>
struct Optional {
    using __type = meta::pack<NoneType, T>;
    [[no_unique_address]] impl::basic_union<NoneType, T> __value;

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
            !meta::is<to, bool> &&
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
    [[nodiscard]] constexpr explicit operator to(this Self&& self)
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
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return __value.index();
    }
    [[deprecated(
        "`Optional<bool>` should never be contextually converted to `bool`.  Consider "
        "an explicit comparison against `None`, a dereference with a leading `*`, or "
        "an exhaustive visitor via trailing `->*` instead. "
    )]] constexpr explicit operator bool() const noexcept requires (DEBUG && meta::boolean<T>) {
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

    /* Indirectly read the stored value, returning a proxy that extends its lifetime
    if necessary and forwards to its `->` where possible.  A `TypeError` error will be
    thrown if the program is compiled in debug mode and the optional is empty.  This
    requires a single extra conditional, which will be optimized out in release builds
    to maintain zero overhead. */
    template <typename Self>
    [[nodiscard]] constexpr auto operator->(this Self&& self)
        noexcept (!DEBUG && requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
        requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
    {
        if constexpr (DEBUG) {
            if (self.__value.index() == 0) {
                throw impl::bad_optional_access<T>();
            }
        }
        return impl::arrow{*std::forward<Self>(self)};
    }

    /* Return 0 if the optional is empty or `meta::size(*opt)` otherwise.  If
    `meta::size(*opt)` would be malformed and the value is not iterable (meaning that
    iterating over the optional would return just a single element), then the result
    will be identical to `opt != None`.  If neither option is available, then this
    method will fail to compile. */
    [[nodiscard]] constexpr auto size() const
        noexcept (
            !meta::has_size<meta::as_const_ref<T>> ||
            meta::nothrow::has_size<meta::as_const_ref<T>>
        )
        requires (meta::has_size<meta::as_const_ref<T>> || (
            !impl::optional_forward_iterable<const Optional&> &&
            !impl::optional_reverse_iterable<const Optional&>
        ))
    {
        if constexpr (meta::has_size<meta::as_const_ref<T>>) {
            if (__value.index()) {
                return meta::size(__value.template get<1>());
            } else {
                return meta::size_type<meta::as_const_ref<T>>(0);
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
            !meta::has_ssize<meta::as_const_ref<T>> ||
            meta::nothrow::has_ssize<meta::as_const_ref<T>>
        )
        requires (meta::has_ssize<meta::as_const_ref<T>> || (
            !impl::optional_forward_iterable<const Optional&> &&
            !impl::optional_reverse_iterable<const Optional&>
        ))
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

    /* Return true if the optional is empty or `meta::empty(*opt)` otherwise.  If
    `meta::empty(*opt)` would be malformed and the value is not iterable (meaning that
    iterating over the optional would return just a single element), then the result
    will be identical to `opt == None`.  If neither option is available, then this
    method will fail to compile. */
    [[nodiscard]] constexpr bool empty() const
        noexcept (
            !meta::has_empty<meta::as_const_ref<T>> ||
            meta::nothrow::has_empty<meta::as_const_ref<T>>
        )
        requires (meta::has_empty<meta::as_const_ref<T>> || (
            !impl::optional_forward_iterable<const Optional&> &&
            !impl::optional_reverse_iterable<const Optional&>
        ))
    {
        if constexpr (meta::has_empty<meta::as_const_ref<T>>) {
            return __value.index() ? meta::empty(__value.template get<1>()) : true;
        } else {
            return !__value.index();
        }
    }

    /* Get a pointer to the underlying data buffer if the templated type supports it.
    If the optional is empty, then a null pointer will be returned instead. */
    template <typename Self>
    [[nodiscard]] constexpr auto data(this Self&& self)
        noexcept (meta::nothrow::has_data<decltype((*std::forward<Self>(self)))> || (
            !impl::optional_forward_iterable<Self> &&
            !impl::optional_reverse_iterable<Self>
        ))
        requires (meta::has_data<decltype((*std::forward<Self>(self)))> || (
            !impl::optional_forward_iterable<Self> &&
            !impl::optional_reverse_iterable<Self>
        ))
    {
        if constexpr (meta::has_data<decltype((*std::forward<Self>(self)))>) {
            return self == None ?
                nullptr :
                meta::data(std::forward<Self>(self).__value.template get<1>());
        } else {
            return self == None ?
                nullptr :
                std::addressof(self.__value.template get<1>());
        }
    }

    /* Forward the shape of the underlying value if the templated type supports it.
    If the optional is empty, then an empty shape filled with zeros will be returned
    instead. */
    template <typename Self>
    [[nodiscard]] constexpr auto shape(this Self&& self)
        noexcept (meta::nothrow::has_shape<decltype(*std::forward<Self>(self))> || (
            !impl::optional_forward_iterable<Self> &&
            !impl::optional_reverse_iterable<Self>
        ))
        requires (meta::has_shape<decltype(*std::forward<Self>(self))> || (
            !impl::optional_forward_iterable<Self> &&
            !impl::optional_reverse_iterable<Self>
        ))
    {
        if constexpr (meta::has_shape<decltype(*std::forward<Self>(self))>) {
            if (self == None) {
                return meta::unqualify<meta::shape_type<decltype(*std::forward<Self>(self))>>{};
            } else {
                return meta::shape(std::forward<Self>(self).__value.template get<1>());
            }
        } else {
            return impl::shape<1>{self != None};
        }
    }

    /* Forward tuple access to `T`, assuming it supports it.  If so, then the return
    type will be promoted to an `Optional<R>`, where `R` represents the forwarded
    result, and the empty state is implicitly propagated. */
    template <auto... A, typename Self>
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{impl::basic_vtable<impl::optional_get<A...>::template fn, 2>{
            self.__value.index()
        }(std::forward<Self>(self))} noexcept;})
        requires (meta::has_get<decltype((*std::forward<Self>(self))), A...>)
    {
        return (impl::basic_vtable<impl::optional_get<A...>::template fn, 2>{
            self.__value.index()
        }(std::forward<Self>(self)));
    }

    /* Get a forward iterator over the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `begin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `end()` iterator if the
    optional is currently empty. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self)
        noexcept (
            !impl::optional_forward_iterable<Self> ||
            requires{{impl::optional_iterator<decltype(meta::begin(*std::forward<Self>(self)))>{
                meta::begin(std::forward<Self>(self).__value.template get<1>())
            }} noexcept;}
        )
        requires (
            impl::optional_forward_iterable<Self> ||
            !impl::optional_reverse_iterable<Self>
        )
    {
        if constexpr (impl::optional_forward_iterable<Self>) {
            if (self == None) {
                return impl::optional_iterator<decltype(meta::begin(*std::forward<Self>(self)))>{};
            } else {
                return impl::optional_iterator<decltype(meta::begin(*std::forward<Self>(self)))>{
                    meta::begin(std::forward<Self>(self).__value.template get<1>())
                };
            }
        } else {
            return
                impl::trivial_iterator(std::forward<Self>(self).__value.template get<1>()) +
                (self == None);
        }
    }

    /* Get a forward sentinel for the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `end()` type.  Otherwise, it will
    return an empty iterator. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self)
        noexcept (
            !impl::optional_forward_iterable<Self> ||
            requires{{impl::optional_iterator<decltype(meta::end(*std::forward<Self>(self)))>{
                meta::end(std::forward<Self>(self).__value.template get<1>())
            }} noexcept;}
        )
        requires (
            impl::optional_forward_iterable<Self> ||
            !impl::optional_reverse_iterable<Self>
        )
    {
        if constexpr (impl::optional_forward_iterable<Self>) {
            if (self == None) {
                return impl::optional_iterator<decltype(meta::end(*std::forward<Self>(self)))>{};
            } else {
                return impl::optional_iterator<decltype(meta::end(*std::forward<Self>(self)))>{
                    meta::end(std::forward<Self>(self).__value.template get<1>())
                };
            }
        } else {
            return
                impl::trivial_iterator(std::forward<Self>(self).__value.template get<1>()) + 1;
        }
    }

    /* Get a reverse iterator over the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `rbegin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `rend()` iterator if the
    optional is currently empty. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self)
        noexcept (
            !impl::optional_reverse_iterable<Self> ||
            requires{{impl::optional_iterator<decltype(meta::rbegin(*std::forward<Self>(self)))>{
                meta::rbegin(std::forward<Self>(self).__value.template get<1>())
            }} noexcept;}
        )
        requires (
            impl::optional_reverse_iterable<Self> ||
            !impl::optional_forward_iterable<Self>
        )
    {
        if constexpr (impl::optional_reverse_iterable<Self>) {
            if (self == None) {
                return impl::optional_iterator<decltype(meta::rbegin(*std::forward<Self>(self)))>{};
            } else {
                return impl::optional_iterator<decltype(meta::rbegin(*std::forward<Self>(self)))>{
                    meta::rbegin(std::forward<Self>(self).__value.template get<1>())
                };
            }
        } else {
            return std::make_reverse_iterator(std::forward<Self>(self).end());
        }
    }

    /* Get a reverse sentinel for the optional.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `rend()` type.  Otherwise, it will
    return an empty iterator. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self)
        noexcept (
            !impl::optional_reverse_iterable<Self> ||
            requires{{impl::optional_iterator<decltype(meta::rend(*std::forward<Self>(self)))>{
                meta::rend(std::forward<Self>(self).__value.template get<1>())
            }} noexcept;}
        )
        requires (
            impl::optional_reverse_iterable<Self> ||
            !impl::optional_forward_iterable<Self>
        )
    {
        if constexpr (impl::optional_reverse_iterable<Self>) {
            if (self == None) {
                return impl::optional_iterator<decltype(meta::rend(*std::forward<Self>(self)))>{};
            } else {
                return impl::optional_iterator<decltype(meta::rend(*std::forward<Self>(self)))>{
                    meta::rend(std::forward<Self>(self).__value.template get<1>())
                };
            }
        } else {
            return std::make_reverse_iterator(std::forward<Self>(self).begin());
        }
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
template <meta::not_rvalue T>
    requires (meta::is_void<T> || std::same_as<T, typename impl::visitable<T>::empty>)
struct Optional<T> {
    using __type = meta::pack<NoneType, T>;
    [[no_unique_address]] impl::basic_union<NoneType> __value;

    /* Default constructor.  Initializes the optional in the empty state. */
    [[nodiscard]] constexpr Optional() = default;

    /* Converting constructor.  Implicitly converts the input to `NoneType`. */
    template <meta::convertible_to<NoneType> from>
    [[nodiscard]] constexpr Optional(from&& v) noexcept {}

    /* Swap the contents of two optionals as efficiently as possible. */
    static constexpr void swap(Optional& other) noexcept {}

    /* Implicit conversion from an empty `Optional` to any type that is convertible
    from `None`. */
    template <typename to>
    [[nodiscard]] constexpr operator to() const
        noexcept (meta::nothrow::convertible_to<NoneType, to>)
        requires (!meta::prefer_constructor<to> && meta::convertible_to<NoneType, to>)
    {
        return NoneType{};
    }

    /* Explicit conversion from an empty `Optional` to any type that is explicitly
    convertible from `None`.  This operator only applies if an implicit conversion
    could not be found. */
    template <typename to>
    [[nodiscard]] constexpr operator to() const
        noexcept (meta::nothrow::explicitly_convertible_to<NoneType, to>)
        requires (
            !meta::prefer_constructor<to> &&
            !meta::convertible_to<NoneType, to> &&
            meta::explicitly_convertible_to<NoneType, to>
        )
    {
        return static_cast<to>(NoneType{});
    }

    /* Contextually convert the optional to a boolean, where true indicates the
    presence of a value.  For empty optionals, this will always return false. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return false; }

    /* Dereference to obtain the inner value, returning `None`. */
    [[nodiscard]] constexpr const NoneType& operator*() const noexcept { return None; }

    /* Indirectly read the inner value, returning a pointer to `None`. */
    [[nodiscard]] constexpr const NoneType* operator->() const noexcept { return &None; }

    /* Empty optionals always have a size of zero. */
    [[nodiscard]] static constexpr size_t size() noexcept { return 0; }

    /* Empty optionals always have a size of zero. */
    [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }

    /* Empty optionals are always empty. */
    [[nodiscard]] static constexpr bool empty() noexcept { return true; }

    /* Empty optionals always have a 1D shape of length zero. */
    [[nodiscard]] static constexpr impl::shape<1> shape() noexcept { return {0}; }

    /* Get a forward iterator over an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `end()`. */
    [[nodiscard]] static constexpr auto begin() noexcept {
        return impl::empty_iterator{};
    }

    /* Get a forward sentinel for an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `begin()`. */
    [[nodiscard]] static constexpr auto end() noexcept {
        return impl::empty_iterator{};
    }

    /* Get a reverse iterator over an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `rend()`. */
    [[nodiscard]] static constexpr auto rbegin() noexcept {
        return std::make_reverse_iterator(end());
    }

    /* Get a reverse sentinel for an empty optional.  This will always return a
    trivial iterator that yields no values and always compares equal to `rbegin()`. */
    [[nodiscard]] static constexpr auto rend() noexcept {
        return std::make_reverse_iterator(begin());
    }
};


////////////////////////
////    EXPECTED    ////
////////////////////////


namespace impl {

    /* A manual visitor that backs the `Expected<T, Es...>(unexpected, V)` constructor,
    where `V` is a value that can be converted to one of the error types `Es...`, but
    specifically excluding the value type `T`.  If there are multiple error types, then
    the selection rules match those of `Union<Es...>`. */
    template <typename T, typename... Es>
    struct unexpected_convert_from {
        template <typename from>
        using type = _union_convert_from<from, void, void, Es...>::type;
        template <typename from>
        static constexpr auto operator()(from&& arg)
            noexcept (meta::nothrow::convertible_to<from, type<from>>)
            requires (!meta::visitable<from> && meta::not_void<type<from>>)
        {
            return impl::basic_union<T, Es...>{
                bertrand::alternative<meta::index_of<type<from>, T, Es...>>,
                std::forward<from>(arg)
            };
        }
    };

    /* A manual visitor that backs the explicit constructor for an `Expected<T, Es...>`
    object, returning a corresponding `impl::basic_union` primitive type.  Note that
    this only applies if `union_convert_from` is invalid. */
    template <typename T, typename... Es>
    struct expected_construct_from {
        using type = std::conditional_t<meta::is_void<T>, NoneType, T>;
        using result = impl::basic_union<type, Es...>;

        template <typename... A>
        static constexpr result operator()(A&&... args)
            noexcept (meta::nothrow::constructible_from<type, A...>)
            requires (meta::constructible_from<type, A...>)
        {
            return result{bertrand::alternative<0>, std::forward<A>(args)...};
        }

        template <typename... A>
        static constexpr result operator()(Unexpected, A&&... args)
            noexcept (meta::nothrow::constructible_from<type, A...>)
            requires (meta::constructible_from<type, A...>)
        {
            return result{bertrand::alternative<0>, std::forward<A>(args)...};
        }
    };

    /* A manual visitor that backs the implicit conversion operator from
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

    /* A manual visitor that backs the explicit conversion operator from
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
    template <size_t I>
    struct expected_deref {
        template <typename Self>
        static constexpr auto operator()(Self&& self)
            -> decltype((std::forward<Self>(self).__value.template get<0>()))
            requires (I == 0)
        {
            return (std::forward<Self>(self).__value.template get<0>());
        }
        template <typename Self>
        [[noreturn]] static constexpr auto operator()(Self&& self)
            -> decltype((std::forward<Self>(self).__value.template get<0>()))
            requires (I > 0)
        {
            throw std::forward<Self>(self).__value.template get<I>();
        }
    };

    /* A simple visitor that backs the tuple indexing operator for `Optional<T>`, where
    `T` is tuple-like, and the return type is promoted to an optional. */
    template <auto... A>
    struct expected_get {
        template <typename... Es>
        struct _type {
            template <typename Self>
            using type = Expected<meta::remove_rvalue<
                meta::get_type<decltype((*std::declval<Self>())), A...>
            >, Es...>;
        };

        template <typename Self>
        using type = impl::visitable<Self>::errors::template eval<_type>::template type<Self>;

        template <size_t I>
        struct fn {
            template <typename Self> requires (I == 0)
            static constexpr type<Self> operator()(Self&& self)
                noexcept (requires{{meta::get<A...>(
                    visitable<Self>::template get<I>(std::forward<Self>(self))
                )} noexcept -> meta::nothrow::convertible_to<type<Self>>;})
                requires (requires{{meta::get<A...>(
                    visitable<Self>::template get<I>(std::forward<Self>(self))
                )} -> meta::convertible_to<type<Self>>;})
            {
                return meta::get<A...>(visitable<Self>::template get<I>(
                    std::forward<Self>(self)
                ));
            }
            template <typename Self> requires (I > 0)
            static constexpr type<Self> operator()(Self&& self)
                noexcept (requires{{
                    visitable<Self>::template get<I>(std::forward<Self>(self))
                } noexcept -> meta::nothrow::convertible_to<type<Self>>;})
                requires (requires{{
                    visitable<Self>::template get<I>(std::forward<Self>(self))
                } -> meta::convertible_to<type<Self>>;})
            {
                return visitable<Self>::template get<I>(std::forward<Self>(self));
            }
        };
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
template <typename T, typename... Es> requires (impl::expected_concept<T, Es...>)
struct Expected {
    using __type = meta::pack<T, Es...>;
    [[no_unique_address]] impl::basic_union<T, Es...> __value;

    /* Default constructor.  Enabled if and only if the result type is default
    constructible or void. */
    [[nodiscard]] constexpr Expected()
        noexcept (meta::nothrow::default_constructible<T>)
        requires (meta::default_constructible<T>)
    {}

    /* Converting constructor.  Implicitly converts the input to the value type if
    possible, otherwise accepts subclasses of the error states.  Also allows conversion
    from other visitable types whose alternatives all meet the conversion criteria. */
    template <typename from>
    [[nodiscard]] constexpr Expected(from&& v)
        noexcept (meta::nothrow::visit_exhaustive<impl::union_convert_from<T, Es...>, from>)
        requires (meta::visit_exhaustive<impl::union_convert_from<T, Es...>, from>)
    :
        __value(impl::visit(impl::union_convert_from<T, Es...>{}, std::forward<from>(v)))
    {}

    /* Convert an arbitrary value into an error state of the expected, explicitly
    excluding the value type from consideration.  If there are multiple error types,
    then the selection rules are the same as for `Union<Es...>`. */
    template <typename from>
    [[nodiscard]] constexpr Expected(Unexpected, from&& e)
        noexcept (meta::nothrow::visit_exhaustive<impl::unexpected_convert_from<T, Es...>, from>)
        requires (meta::visit_exhaustive<impl::unexpected_convert_from<T, Es...>, from>)
    :
        __value(impl::visit(
            impl::unexpected_convert_from<T, Es...>{},
            std::forward<from>(e)
        ))
    {}

    /* Explicit error constructor.  Accepts a `bertrand::unexpected` disambiguation
    tag, followed by any number of argumets, which will be used to initialize one of
    the error states in `Es...`.  If multiple error states are given, then the
    selection rules match those of `Union<Es...>`. */
    template <typename... A>
    [[nodiscard]] constexpr Expected(Unexpected, A&&... args)
        noexcept (meta::nothrow::visit_exhaustive<impl::expected_construct_from<T, Es...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<impl::unexpected_convert_from<T, Es...>, A...> &&
            meta::visit_exhaustive<impl::expected_construct_from<T, Es...>, A...>
        )
    :
        __value(impl::visit(
            impl::expected_construct_from<T, Es...>{},
            std::forward<A>(args)...
        ))
    {}

    /* Explicitly construct an expected with the alternative at index `I` using the
    provided arguments.  This is more explicit than using the standard constructors,
    for cases where only a specific alternative should be considered. */
    template <size_t I, typename... A> requires (I < (sizeof...(Es) + 1))
    [[nodiscard]] constexpr Expected(std::in_place_index_t<I> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<meta::unpack_type<I, T, Es...>, A...>)
        requires (meta::constructible_from<meta::unpack_type<I, T, Es...>, A...>)
    :
        __value(tag, std::forward<A>(args)...)
    {}

    /* Explicitly construct an expected with the specified alternative using the given
    arguments.  This is more explicit than using the standard constructors, for cases
    where only a specific subset of alternatives should be considered.  If the
    specified alternative is given as a visitable type, then each of its alternatives
    must be present in the union's template signature.  All arguments after the
    disambiguation tag will be perfectly forwarded to the indicated type's
    constructor. */
    template <typename U, typename... A> requires (impl::union_init_subset<Expected, U>)
    [[nodiscard]] constexpr Expected(std::type_identity<U> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<U, A...>)
        requires (!meta::visitable<U> && meta::constructible_from<U, A...>)
    :
        __value{bertrand::alternative<meta::index_of<U, T, Es...>>, std::forward<A>(args)...}
    {}
    template <typename U, typename... A> requires (impl::union_init_subset<Expected, U>)
    [[nodiscard]] constexpr Expected(std::type_identity<U> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<Expected, U>)
        requires (meta::visitable<U> && meta::constructible_from<U, A...>)
    :
        Expected(U(std::forward<A>(args)...))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the value type's
    constructor, and initializes the expected with the result.  If
    `bertrand::unexpected` is the first argument, then this will construct an error
    state with the remaining arguments instead. */
    template <typename... A>
    [[nodiscard]] constexpr explicit Expected(A&&... args)
        noexcept (meta::nothrow::visit_exhaustive<impl::expected_construct_from<T, Es...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<impl::union_convert_from<T, Es...>, A...> &&
            meta::visit_exhaustive<impl::expected_construct_from<T, Es...>, A...>
        )
    :
        __value(impl::visit(
            impl::expected_construct_from<T, Es...>{},
            std::forward<A>(args)...
        ))
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
    [[nodiscard]] constexpr explicit operator to(this Self&& self)
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
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return __value.index() == 0;
    }
    [[deprecated(
        "`Expected<bool>` should never be contextually converted to `bool`.  Consider "
        "an explicit comparison against `None`, a dereference with a leading `*`, or "
        "an exhaustive visitor via trailing `->*` instead. "
    )]] constexpr explicit operator bool() const noexcept requires (DEBUG && meta::boolean<T>) {
        return __value.index() == 0;
    }

    /* Dereference to obtain the stored value, perfectly forwarding it according to the
    expected's current cvref qualifications.  If the expected is in an error state,
    then the error will be thrown as an exception. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) {
        return (impl::basic_vtable<
            impl::expected_deref,
            impl::visitable<Self>::alternatives::size()
        >{self.__value.index()}(std::forward<Self>(self)));
    }

    /* Indirectly read the stored value, returning a proxy that extends its lifetime if
    necessary and forwards to its `->` operator where possible.  If the expected is in
    an error state, then the error will be thrown as an exception. */
    template <typename Self>
    [[nodiscard]] constexpr auto operator->(this Self&& self)
        noexcept (requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
        requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
    {
        return impl::arrow{*std::forward<Self>(self)};
    }

    /* Return 0 if the expected is empty or `meta::size(*exp)` otherwise.  If
    `meta::size(*exp)` would be malformed and the value is not iterable (meaning that
    iterating over the expected would return just a single element), then the result
    will be identical to `exp != None`.  If neither option is available, then this
    method will fail to compile. */
    [[nodiscard]] constexpr auto size() const
        noexcept (
            !meta::has_size<meta::as_const_ref<T>> ||
            meta::nothrow::has_size<meta::as_const_ref<T>>
        )
        requires (meta::has_size<meta::as_const_ref<T>> || (
            !impl::optional_forward_iterable<const Expected&> &&
            !impl::optional_reverse_iterable<const Expected&>
        ))
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
            !meta::has_ssize<meta::as_const_ref<T>> ||
            meta::nothrow::has_ssize<meta::as_const_ref<T>>
        )
        requires (meta::has_ssize<meta::as_const_ref<T>> || (
            !impl::optional_forward_iterable<const Expected&> &&
            !impl::optional_reverse_iterable<const Expected&>
        ))
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

    /* Return true if the expected is in an error state or `meta::empty(*exp)`
    otherwise.  If `meta::empty(*exp)` would be malformed and the value is not iterable
    (meaning that iterating over the expected would return just a single element), then
    the result will be identical to `exp == None`.  If neither option is available,
    then this method will fail to compile. */
    [[nodiscard]] constexpr bool empty() const
        noexcept (
            !meta::has_empty<meta::as_const_ref<T>> ||
            meta::nothrow::has_empty<meta::as_const_ref<T>>
        )
        requires (meta::has_empty<meta::as_const_ref<T>> || (
            !impl::optional_forward_iterable<const Expected&> &&
            !impl::optional_reverse_iterable<const Expected&>
        ))
    {
        if constexpr (meta::has_empty<meta::as_const_ref<T>>) {
            return __value.index() != 0 || meta::empty(__value.template get<0>());
        } else {
            return __value.index() != 0;
        }
    }

    /* Get a pointer to the underlying data buffer if the templated type supports it.
    If the expected represents an error state, then a null pointer will be returned
    instead. */
    template <typename Self>
    [[nodiscard]] constexpr auto data(this Self&& self)
        noexcept (meta::nothrow::has_data<decltype((*std::forward<Self>(self)))> || (
            !impl::optional_forward_iterable<Self> &&
            !impl::optional_reverse_iterable<Self>
        ))
        requires (meta::has_data<decltype((*std::forward<Self>(self)))> || (
            !impl::optional_forward_iterable<Self> &&
            !impl::optional_reverse_iterable<Self>
        ))
    {
        if constexpr (meta::has_data<decltype((*std::forward<Self>(self)))>) {
            return self.__value.index() != 0 ?
                nullptr :
                meta::data(std::forward<Self>(self).__value.template get<0>());
        } else {
            return self.__value.index() != 0 ?
                nullptr :
                std::addressof(self.__value.template get<0>());
        }
    }

    /* Forward the shape of the underlying value if the templated type supports it.
    If the expected represents an error state, then an empty shape filled with zeros
    will be returned instead. */
    template <typename Self>
    [[nodiscard]] constexpr auto shape(this Self&& self)
        noexcept (meta::nothrow::has_shape<decltype(*std::forward<Self>(self))> || (
            !impl::optional_forward_iterable<Self> &&
            !impl::optional_reverse_iterable<Self>
        ))
        requires (meta::has_shape<decltype(*std::forward<Self>(self))> || (
            !impl::optional_forward_iterable<Self> &&
            !impl::optional_reverse_iterable<Self>
        ))
    {
        if constexpr (meta::has_shape<decltype(*std::forward<Self>(self))>) {
            if (self.__value.index() != 0) {
                return meta::unqualify<meta::shape_type<decltype(*std::forward<Self>(self))>>{};
            } else {
                return meta::shape(std::forward<Self>(self).__value.template get<0>());
            }
        } else {
            return impl::shape<1>{self.__value.index() == 0};
        }
    }

    /* Forward tuple access to `T`, assuming it supports it.  If so, then the return
    type will be promoted to an `Expected<R, Es...>`, where `R` represents the
    forwarded result, and `Es...` represent the forwarded error state(s). */
    template <auto... A, typename Self>
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{impl::basic_vtable<
            impl::expected_get<A...>::template fn,
            impl::visitable<Self>::alternatives::size()
        >{self.__value.index()}(std::forward<Self>(self))} noexcept;})
        requires (meta::has_get<decltype((*std::forward<Self>(self))), A...>)
    {
        return (impl::basic_vtable<
            impl::expected_get<A...>::template fn,
            impl::visitable<Self>::alternatives::size()
        >{self.__value.index()}(std::forward<Self>(self)));
    }

    /* Get a forward iterator over the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `begin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `end()` iterator if the
    expected is currently in an error state. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self)
        noexcept (
            !impl::optional_forward_iterable<Self> ||
            requires{{impl::optional_iterator<decltype(meta::begin(*std::forward<Self>(self)))>{
                meta::begin(std::forward<Self>(self).__value.template get<0>())
            }} noexcept;}
        )
        requires (
            impl::optional_forward_iterable<Self> ||
            !impl::optional_reverse_iterable<Self>
        )
    {
        if constexpr (impl::optional_forward_iterable<Self>) {
            if (self.__value.index() != 0) {
                return impl::optional_iterator<decltype(meta::begin(*std::forward<Self>(self)))>{};
            } else {
                return impl::optional_iterator<decltype(meta::begin(*std::forward<Self>(self)))>{
                    meta::begin(std::forward<Self>(self).__value.template get<0>())
                };
            }
        } else {
            return
                impl::trivial_iterator(std::forward<Self>(self).__value.template get<0>()) +
                (self.__value.index() != 0);
        }
    }

    /* Get a forward sentinel for the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `end()` type.  Otherwise, it will
    return an empty iterator. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self)
        noexcept (
            !impl::optional_forward_iterable<Self> ||
            requires{{impl::optional_iterator<decltype(meta::end(*std::forward<Self>(self)))>{
                meta::end(std::forward<Self>(self).__value.template get<0>())
            }} noexcept;}
        )
        requires (
            impl::optional_forward_iterable<Self> ||
            !impl::optional_reverse_iterable<Self>
        )
    {
        if constexpr (impl::optional_forward_iterable<Self>) {
            if (self.__value.index() != 0) {
                return impl::optional_iterator<decltype(meta::end(*std::forward<Self>(self)))>{};
            } else {
                return impl::optional_iterator<decltype(meta::end(*std::forward<Self>(self)))>{
                    meta::end(std::forward<Self>(self).__value.template get<0>())
                };
            }
        } else {
            return
                impl::trivial_iterator(std::forward<Self>(self).__value.template get<0>()) + 1;
        }
    }

    /* Get a reverse iterator over the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `rbegin()` type.  Otherwise, it will
    return an iterator with only a single element, or an `rend()` iterator if the
    expected is currently in an error state. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self)
        noexcept (
            !impl::optional_reverse_iterable<Self> ||
            requires{{impl::optional_iterator<decltype(meta::rbegin(*std::forward<Self>(self)))>{
                meta::rbegin(std::forward<Self>(self).__value.template get<0>())
            }} noexcept;}
        )
        requires (
            impl::optional_reverse_iterable<Self> ||
            !impl::optional_forward_iterable<Self>
        )
    {
        if constexpr (impl::optional_reverse_iterable<Self>) {
            if (self.__value.index() != 0) {
                return impl::optional_iterator<decltype(meta::rbegin(*std::forward<Self>(self)))>{};
            } else {
                return impl::optional_iterator<decltype(meta::rbegin(*std::forward<Self>(self)))>{
                    meta::rbegin(std::forward<Self>(self).__value.template get<0>())
                };
            }
        } else {
            return std::make_reverse_iterator(std::forward<Self>(self).end());
        }
    }

    /* Get a reverse sentinel for the expected.  If the wrapped type is iterable, then
    this will be a lightweight wrapper around its `rend()` type.  Otherwise, it will
    return an empty iterator. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self)
        noexcept (
            !impl::optional_reverse_iterable<Self> ||
            requires{{impl::optional_iterator<decltype(meta::rend(*std::forward<Self>(self)))>{
                meta::rend(std::forward<Self>(self).__value.template get<0>())
            }} noexcept;}
        )
        requires (
            impl::optional_reverse_iterable<Self> ||
            !impl::optional_forward_iterable<Self>
        )
    {
        if constexpr (impl::optional_reverse_iterable<Self>) {
            if (self.__value.index() != 0) {
                return impl::optional_iterator<decltype(meta::rend(*std::forward<Self>(self)))>{};
            } else {
                return impl::optional_iterator<decltype(meta::rend(*std::forward<Self>(self)))>{
                    meta::rend(std::forward<Self>(self).__value.template get<0>())
                };
            }
        } else {
            return std::make_reverse_iterator(std::forward<Self>(self).begin());
        }
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
template <meta::is_void T, typename... Es> requires (impl::expected_concept<T, Es...>)
struct Expected<T, Es...> {
    using __type = meta::pack<T, Es...>;
    [[no_unique_address]] impl::basic_union<NoneType, Es...> __value;

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
        noexcept (meta::nothrow::visit_exhaustive<impl::union_convert_from<NoneType, Es...>, from>)
        requires (meta::visit_exhaustive<impl::union_convert_from<NoneType, Es...>, from>)
    :
        __value(impl::visit(
            impl::union_convert_from<NoneType, Es...>{},
            std::forward<from>(v)
        ))
    {}

    /* Convert an arbitrary value into an error state of the expected, explicitly
    excluding the value type from consideration.  If there are multiple error types,
    then the selection rules are the same as for `Union<Es...>`. */
    template <typename from>
    [[nodiscard]] constexpr Expected(Unexpected, from&& e)
        noexcept (meta::nothrow::visit_exhaustive<impl::unexpected_convert_from<T, Es...>, from>)
        requires (meta::visit_exhaustive<impl::unexpected_convert_from<T, Es...>, from>)
    :
        __value(impl::visit(
            impl::unexpected_convert_from<T, Es...>{},
            std::forward<from>(e)
        ))
    {}

    /* Explicit error constructor.  Accepts a `bertrand::unexpected` disambiguation
    tag, followed by any number of argumets, which will be used to initialize one of
    the error states in `Es...`.  If multiple error states are given, then the
    selection rules match those of `Union<Es...>`. */
    template <typename... A>
    [[nodiscard]] constexpr Expected(Unexpected, A&&... args)
        noexcept (meta::nothrow::visit_exhaustive<impl::expected_construct_from<T, Es...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<impl::unexpected_convert_from<T, Es...>, A...> &&
            meta::visit_exhaustive<impl::expected_construct_from<T, Es...>, A...>
        )
    :
        __value(impl::visit(
            impl::expected_construct_from<T, Es...>{},
            std::forward<A>(args)...
        ))
    {}

    /* Explicitly construct an expected with the alternative at index `I` using the
    provided arguments.  This is more explicit than using the standard constructors,
    for cases where only a specific alternative should be considered. */
    template <size_t I, typename... A> requires (I < (sizeof...(Es) + 1))
    [[nodiscard]] constexpr Expected(std::in_place_index_t<I> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<
            meta::unpack_type<I, NoneType, Es...>,
            A...
        >)
        requires (meta::constructible_from<
            meta::unpack_type<I, NoneType, Es...>,
            A...
        >)
    :
        __value{tag, std::forward<A>(args)...}
    {}

    /* Explicitly construct an expected with the specified alternative using the given
    arguments.  This is more explicit than using the standard constructors, for cases
    where only a specific subset of alternatives should be considered.  If the
    specified alternative is given as a visitable type, then each of its alternatives
    must be present in the union's template signature.  All arguments after the
    disambiguation tag will be perfectly forwarded to the indicated type's
    constructor. */
    template <typename U, typename... A> requires (impl::union_init_subset<Expected, U>)
    [[nodiscard]] constexpr Expected(std::type_identity<U> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<U, A...>)
        requires (!meta::visitable<U> && meta::constructible_from<U, A...>)
    :
        __value{bertrand::alternative<meta::index_of<U, T, Es...>>, std::forward<A>(args)...}
    {}
    template <typename U, typename... A> requires (impl::union_init_subset<Expected, U>)
    [[nodiscard]] constexpr Expected(std::type_identity<U> tag, A&&... args)
        noexcept (meta::nothrow::constructible_from<Expected, U>)
        requires (meta::visitable<U> && meta::constructible_from<U, A...>)
    :
        Expected(U(std::forward<A>(args)...))
    {}

    /* Explicit constructor.  Accepts arbitrary arguments to the value type's
    constructor, and initializes the expected with the result.  If
    `bertrand::unexpected` is the first argument, then this will construct an error
    state with the remaining arguments instead. */
    template <typename... A>
    [[nodiscard]] constexpr explicit Expected(A&&... args)
        noexcept (meta::nothrow::visit_exhaustive<impl::expected_construct_from<T, Es...>, A...>)
        requires (
            sizeof...(A) > 0 &&
            !meta::visit_exhaustive<impl::union_convert_from<NoneType, Es...>, A...> &&
            meta::visit_exhaustive<impl::expected_construct_from<T, Es...>, A...>
        )
    :
        __value(impl::visit(
            impl::expected_construct_from<T, Es...>{},
            std::forward<A>(args)...
        ))
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
    [[nodiscard]] constexpr explicit operator to(this Self&& self)
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
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return __value.index() == 0;
    }

    /* Dereference to obtain the stored value, perfectly forwarding it according to the
    expected's current cvref qualifications.  If the expected is in an error state,
    then the error will be thrown as an exception. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) {
        return (impl::basic_vtable<
            impl::expected_deref,
            impl::visitable<Self>::alternatives::size()
        >{self.__value.index()}(std::forward<Self>(self)));
    }

    /* Indirectly read the stored value, returning a proxy that extends its lifetime if
    necessary and forwards to its `->` operator where possible.  If the expected is in
    an error state, then the error will be thrown as an exception. */
    template <typename Self>
    [[nodiscard]] constexpr auto operator->(this Self&& self)
        noexcept (requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
        requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
    {
        return impl::arrow{*std::forward<Self>(self)};
    }

    /* Empty expected monads always have a size of zero. */
    [[nodiscard]] static constexpr size_t size() noexcept { return 0; }

    /* Empty expected monads always have a size of zero. */
    [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }

    /* Empty expected monads are always empty. */
    [[nodiscard]] static constexpr bool empty() noexcept { return true; }

    /* Empty expected monads always have a 1D shape of length zero. */
    [[nodiscard]] static constexpr impl::shape<1> shape() noexcept { return {}; }

    /* Get a forward iterator over an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `end()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
        return impl::empty_iterator{};
    }

    /* Get a forward sentinel for an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `begin()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
        return impl::empty_iterator{};
    }

    /* Get a reverse iterator over an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `rend()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
        return std::make_reverse_iterator(self.end());
    }

    /* Get a reverse sentinel for an empty expected monad.  This will always return a
    trivial iterator that yields no values and always compares equal to `rbegin()`. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
        return std::make_reverse_iterator(self.begin());
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
the right hand side of this operator, which must be callable using the alternatives of
the monad on the left.  If the monad is an optional or expected, then the visitor may
omit the empty/error state(s), in which case they will be implicitly propagated to the
return type.  Nested monads will be recursively expanded into their alternatives before
attempting to invoke the visitor.

Similar to the built-in `->` indirection operator, this operator will attempt to
recursively call itself in order to match nested patterns involving other monads.
Namely, if an alternative `A` is not directly handled by the visitor `F`, and the
expression `A->*F` is valid, then the operator will fall back to that form.  This
means a single visitor can cover both direct and nested patterns, preferring the
former over the latter.  If neither are satisfied, then the operator will fail to
compile. */
template <meta::visit_monad T, typename F>
constexpr decltype(auto) operator->*(T&& val, F&& func)
    noexcept (meta::nothrow::force_visit<1, impl::visit_pattern<F>, F, T>)
    requires (meta::force_visit<1, impl::visit_pattern<F>, F, T>)
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
    noexcept (requires{{impl::basic_vtable<
        impl::visit_lt::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))} noexcept;})
    requires ((meta::visit_monad<L> || meta::visit_monad<R>) && requires{{impl::basic_vtable<
        impl::visit_lt::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))};})
{
    return impl::basic_vtable<
        impl::visit_lt::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs));
}


template <typename L, typename R>
constexpr decltype(auto) operator<=(L&& lhs, R&& rhs)
    noexcept (requires{{impl::basic_vtable<
        impl::visit_le::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))} noexcept;})
    requires ((meta::visit_monad<L> || meta::visit_monad<R>) && requires{{impl::basic_vtable<
        impl::visit_le::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))};})
{
    return impl::basic_vtable<
        impl::visit_le::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs));
}


template <typename L, typename R>
constexpr decltype(auto) operator==(L&& lhs, R&& rhs)
    noexcept (requires{{impl::basic_vtable<
        impl::visit_eq::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))} noexcept;})
    requires ((meta::visit_monad<L> || meta::visit_monad<R>) && requires{{impl::basic_vtable<
        impl::visit_eq::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))};})
{
    return impl::basic_vtable<
        impl::visit_eq::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs));
}


template <typename L, typename R>
constexpr decltype(auto) operator!=(L&& lhs, R&& rhs)
    noexcept (requires{{impl::basic_vtable<
        impl::visit_ne::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))} noexcept;})
    requires ((meta::visit_monad<L> || meta::visit_monad<R>) && requires{{impl::basic_vtable<
        impl::visit_ne::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))};})
{
    return impl::basic_vtable<
        impl::visit_ne::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs));
}


template <typename L, typename R>
constexpr decltype(auto) operator>=(L&& lhs, R&& rhs)
    noexcept (requires{{impl::basic_vtable<
        impl::visit_ge::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))} noexcept;})
    requires ((meta::visit_monad<L> || meta::visit_monad<R>) && requires{{impl::basic_vtable<
        impl::visit_ge::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))};})
{
    return impl::basic_vtable<
        impl::visit_ge::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs));
}


template <typename L, typename R>
constexpr decltype(auto) operator>(L&& lhs, R&& rhs)
    noexcept (requires{{impl::basic_vtable<
        impl::visit_gt::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))} noexcept;})
    requires ((meta::visit_monad<L> || meta::visit_monad<R>) && requires{{impl::basic_vtable<
        impl::visit_gt::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))};})
{
    return impl::basic_vtable<
        impl::visit_gt::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs));
}


template <typename L, typename R>
constexpr decltype(auto) operator<=>(L&& lhs, R&& rhs)
    noexcept (requires{{impl::basic_vtable<
        impl::visit_cmp::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))} noexcept;})
    requires ((meta::visit_monad<L> || meta::visit_monad<R>) && requires{{impl::basic_vtable<
        impl::visit_cmp::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs))};})
{
    return impl::basic_vtable<
        impl::visit_cmp::fn,
        impl::visitable<L>::alternatives::size() * impl::visitable<R>::alternatives::size()
    >{
        impl::visitable<R>::index(std::forward<R>(rhs)) + (
            impl::visitable<L>::index(std::forward<L>(lhs)) *
            impl::visitable<R>::alternatives::size()
        )
    }(std::forward<L>(lhs), std::forward<R>(rhs));
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


namespace std {

    namespace ranges {

        template <typename... Ts>
            requires ((bertrand::meta::lvalue<Ts> || (
                bertrand::meta::iterable<Ts> &&
                enable_borrowed_range<bertrand::meta::unqualify<Ts>>
            )) && ...)
        constexpr bool enable_borrowed_range<bertrand::Union<Ts...>> = true;

        template <typename T>
            requires (
                bertrand::meta::lvalue<T> ||
                std::same_as<T, typename bertrand::impl::visitable<T>::empty> ||
                enable_borrowed_range<bertrand::meta::unqualify<T>>
            )
        constexpr bool enable_borrowed_range<bertrand::Optional<T>> = true;

        template <typename T, typename... Es>
            requires (
                bertrand::meta::lvalue<T> ||
                std::same_as<T, typename bertrand::impl::visitable<T>::empty> ||
                enable_borrowed_range<bertrand::meta::unqualify<T>>
            )
        constexpr bool enable_borrowed_range<bertrand::Expected<T, Es...>> = true;

    }

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

    template <bertrand::meta::Union T> requires (bertrand::impl::union_tuple<T>::enable)
    struct tuple_size<T> : std::integral_constant<
        size_t,
        bertrand::impl::union_tuple<T>::size
    > {};

    template <bertrand::meta::Optional T>
        requires (bertrand::meta::tuple_like<
            typename bertrand::impl::visitable<T>::values::template at<0>
        >)
    struct tuple_size<T> : std::integral_constant<
        size_t,
        bertrand::meta::tuple_size<
            typename bertrand::impl::visitable<T>::values::template at<0>
        >
    > {};

    template <bertrand::meta::Expected T>
        requires (bertrand::meta::tuple_like<
            typename bertrand::impl::visitable<T>::values::template at<0>
        >)
    struct tuple_size<T> : std::integral_constant<
        size_t,
        bertrand::meta::tuple_size<
            typename bertrand::impl::visitable<T>::values::template at<0>
        >
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

}


#endif  // BERTRAND_UNION_H