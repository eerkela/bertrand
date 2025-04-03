#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include <type_traits>


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
at which point the second syntax could be standardized as well.  However, there is
currently no proposal that would allow this in C++26, and it may require universal
template parameters to function correctly, which is unlikely to be implemented any time
soon. */
template <typename... Funcs>
struct visitor : Funcs... { using Funcs::operator()...; };


namespace impl {
    struct union_tag {};
    struct optional_tag {};
    struct expected_tag {};

    /* Provides an extensible mechanism for controlling the dispatching behavior of
    the `meta::exhaustive` concept and `bertrand::visit()` operator, including return
    type deduction from the possible alternatives.  Users can specialize this structure
    to extend those utilities to arbitrary types, without needing to reimplement the
    entire compile-time dispatch mechanism. */
    template <typename T>
    struct visitable;

}


namespace meta {

    template <typename T>
    concept visitable = impl::visitable<T>::enable;

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
        with the deduced type information, and must be acknowledged before the result
        can be safely accessed.
    7.  An extra `flatten()` method is provided to collapse unions of compatible types
        into a single common type, where possible.  This can be thought of as an
        explicit exit point for the monadic interface, allowing users to materialize
        results into a scalar type when needed.
    8.  All operations are usable at compile time as long as the underlying types
        support it, including `get()`, `visit()`, `flatten()`, and all operator
        overloads.

This class is the basis for both `Optional<T>` and `Expected<T, Es...>`, which together
form a complete, safe, and performant type-erasure mechanism for arbitrary C++ code,
including value-based exceptions at both compile time and runtime, which must be
acknowledged through the type system. */
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
        expressions, where `std::optional` may not.
    3.  `Optional`s support visitation just like `Union`s, allowing type-safe access
        to both states.

Just like `std::optional`, the empty state is modeled using `std::nullopt_t`, which
reduces redundancy with the standard library.
*/
template <meta::not_void T>
struct Optional;


template <typename T>
Optional(T) -> Optional<T>;


/* A wrapper for an arbitrarily qualified return type `T` that can also store a
possible exception or union of exceptions.

This type effectively encodes the error state into the C++ type system, forcing
downstream users to acknowledge it without relying on try/catch semantics.  Unless the
error state is explicitly handled, operations on the `Expected` type will fail to
compile, promoting exhaustive error coverage.  This is similar in spirit to
`std::expected<T, E>`, but with the following changes:

    1. `T` may have cvref qualifications, allowing it to model functions that return
        references, without requiring an extra copy or `std::reference_wrapper`
        workaround.
    2.  `E` is constrained to subclasses of `Exception` (the default) or unions of
        such.  Because `Exception`s always generate a coherent stack trace when
        compiled in debug mode, the expected type will also retain the same stack trace
        for diagnostic purposes.
    3.  The exception can be trivially re-thrown via `Expected.raise()`, which reveals
        the exact exception type (accounting for polymorphism) and original traceback,
        propagating the error using normal try/catch semantics.  Such errors may also
        be caught and converted to `Expected` wrappers temporarily, allowing users to
        easily transfer errors from one domain to the other.
    4.  The whole mechanism is safe at compile time, allowing `Expected` wrappers to be
        used in constant expressions, where error handling is often difficult.
    5.  `Expected` supports the same monadic operator interface as `Union`, allowing
        users to chain operations on the contained value while propagating errors and
        accumulating possible exception states.  The type system will enforce that all
        possible exceptions along the chain are accounted for before the result can
        be safely accessed.

Bertrand uses this type internally to represent any errors that could occur during
normal program execution, excluding debug assertions.  The same convention is
encouraged, though not required, for downstream users as well, in order to promote
exhaustive error handling via the type system. */
template <typename T, meta::unqualified E = Exception, meta::unqualified... Es>
    requires (meta::inherits<Es, Exception> && ...)
struct Expected;


namespace meta {

    namespace detail {

        template <typename F, typename... Args>
        struct visit {
            // 1. Convert arguments to a 2D pack of packs representing all possible
            //    permutations of the union types.
            template <typename...>
            struct permute { using type = bertrand::args<>; };
            template <typename First, typename... Rest>
            struct permute<First, Rest...> {
                using type = impl::visitable<First>::pack::template product<
                    typename impl::visitable<Rest>::pack...
                >;
            };
            using permutations = permute<Args...>::type;

            // 2. Analyze each permutation.  If at least one permutation is valid, then
            //    `enable` will evaluate to true.  If not all permutations are valid,
            //    then `type` will evaluate to `Expected<R, BadUnionAccess>`, where `R`
            //    is the deduced return type.  If some valid permutations return `void`
            //    and others return non-`void`, then `R` will deduce to `Optional<R>`
            //    and recur for `R`.  If all valid non-void permutations return the
            //    same type, then `R` will deduce to that type and terminate.
            //    Otherwise, it evaluates to `Union<Rs...>`, where `Rs...` represents
            //    the unique result types.  In the worst case, this collapses to
            //    `Expected<Optional<Union<Rs...>>, BadUnionAccess>` for a visitor
            //    that is not exhaustive over all possible permutations, in which some
            //    permutations return void, and others return heterogenous types.
            //    Users of such a visitor would be forced to acknowledge that the
            //    active member may not be valid, then that it may not have returned a
            //    result, and then precisely what type of result was returned, in that
            //    order.  Typical exhaustive visitors would collapse to just a single
            //    type, just like `std::visit()`.
            template <typename>
            struct check;
            template <typename... permutations>
            struct check<bertrand::args<permutations...>> {
                // 3. Determine if the function is invocable with the permuted
                //    arguments, and get its return type/noexcept status if so.
                template <typename>
                struct invoke {
                    static constexpr bool enable = false;
                    static constexpr bool nothrow = true;
                };
                template <typename... A> requires (meta::invocable<F, A...>)
                struct invoke<bertrand::args<A...>> {
                    static constexpr bool enable = true;
                    static constexpr bool nothrow = meta::nothrow::invocable<F, A...>;
                    using type = meta::invoke_type<F, A...>;
                };

                // 5. Base case - no valid permutations
                template <typename...>
                struct evaluate {
                    static constexpr bool enable = false;
                    static constexpr bool exhaustive = false;
                    static constexpr bool nothrow = true;
                    using type = void;
                };

                // 6.  Recursive case - apply (3) to filter out invalid permutations
                template <typename... valid, typename P, typename... Ps>
                struct evaluate<bertrand::args<valid...>, P, Ps...> {
                    template <typename T>
                    struct validate {
                        using recur = evaluate<bertrand::args<valid...>, Ps...>;
                    };
                    template <typename T> requires (invoke<P>::enable)
                    struct validate<T> {
                        using recur = evaluate<bertrand::args<valid..., P>, Ps...>;
                    };
                    static constexpr bool enable = validate<P>::recur::enable;
                    static constexpr bool exhaustive = validate<P>::recur::exhaustive;
                    static constexpr bool nothrow = validate<P>::recur::nothrow;
                    using type = validate<P>::recur::type;
                };

                // 7. Base case - some valid permutations.  Proceed to deduce an
                //    appropriate return type.
                template <typename... valid> requires (sizeof...(valid) > 0)
                struct evaluate<bertrand::args<valid...>> {
                    static constexpr bool enable = true;
                    static constexpr bool exhaustive =
                        sizeof...(valid) == sizeof...(permutations);
                    static constexpr bool nothrow = (invoke<valid>::nothrow && ...);

                    // 8. Base case - all valid permutations return void
                    template <bool, typename...>
                    struct deduce { using type = void; };

                    // 9. Recursive case - filter out void and duplicate types
                    template <bool opt, typename... unique, typename T, typename... Ts>
                    struct deduce<opt, bertrand::args<unique...>, T, Ts...> {
                        template <typename U>
                        struct filter {
                            using type = deduce<
                                opt || meta::is_void<U>,
                                bertrand::args<unique...>,
                                Ts...
                            >::type;
                        };
                        template <meta::not_void U>
                            requires (meta::index_of<U, unique...> == sizeof...(unique))
                        struct filter<U> {
                            using type = deduce<
                                opt,
                                bertrand::args<unique..., U>,
                                Ts...
                            >::type;
                        };
                        using type = filter<T>::type;
                    };

                    // 10. Base case - exactly one unique return type
                    template <typename T>
                    struct deduce<false, bertrand::args<T>> { using type = T; };
                    template <typename T>
                    struct deduce<true, bertrand::args<T>> {
                        using type = bertrand::Optional<T>;
                    };

                    // 11. Base case - more than one unique return type
                    template <typename... Ts> requires (sizeof...(Ts) > 1)
                    struct deduce<false, bertrand::args<Ts...>> {
                        using type = bertrand::Union<Ts...>;
                    };
                    template <typename... Ts> requires (sizeof...(Ts) > 1)
                    struct deduce<true, bertrand::args<Ts...>> {
                        using type = bertrand::Optional<bertrand::Union<Ts...>>;
                    };

                    // 12. execute `deduce<>` and wrap with an `Expected` if not
                    //     exhaustive
                    using result = deduce<
                        false,
                        bertrand::args<>,
                        typename invoke<valid>::type...
                    >;
                    using type = std::conditional_t<
                        exhaustive,
                        typename result::type,
                        bertrand::Expected<typename result::type, BadUnionAccess>
                    >;
                };

                // 13. execute `evaluate<>` to explore permutations
                using result = evaluate<bertrand::args<>, permutations...>;
                static constexpr bool enable = result::enable;
                static constexpr bool exhaustive = result::exhaustive;
                static constexpr bool nothrow = result::nothrow;
                using type = result::type;
            };

            // 14. Lift results from `check<>` into outer trait
            static constexpr bool enable = check<permutations>::enable;
            static constexpr bool exhaustive = check<permutations>::exhaustive;
            static constexpr bool nothrow = check<permutations>::nothrow;
            using type = check<permutations>::type;
        };

    }

    /* A visitor function can only be applied to a set of arguments if at least one
    permutation of the union types are valid. */
    template <typename F, typename... Args>
    concept visitor = detail::visit<F, Args...>::enable;

    /* Specifies that all permutations of the union types must be valid for the visitor
    function. */
    template <typename F, typename... Args>
    concept exhaustive = detail::visit<F, Args...>::exhaustive;

    /* A visitor function returns a new union of all possible results for each
    permutation of the input unions.  If all permutations return the same type, then
    that type is returned instead.  If some of the permutations return `void` and
    others do not, then the result will be wrapped in an `Optional`. */
    template <typename F, typename... Args> requires (visitor<F, Args...>)
    using visit_type = detail::visit<F, Args...>::type;

    /* Tests whether the return type of a visitor is implicitly convertible to the
    expected type. */
    template <typename Ret, typename F, typename... Args>
    concept visit_returns =
        visitor<F, Args...> && convertible_to<visit_type<F, Args...>, Ret>;

    namespace nothrow {

        template <typename F, typename... Args>
        concept visitor =
            meta::visitor<F, Args...> && detail::visit<F, Args...>::nothrow;

        template <typename F, typename... Args>
        concept exhaustive =
            meta::exhaustive<F, Args...> && detail::visit<F, Args...>::nothrow;

        template <typename F, typename... Args> requires (visitor<F, Args...>)
        using visit_type = meta::visit_type<F, Args...>;

        template <typename Ret, typename F, typename... Args>
        concept visit_returns =
            visitor<F, Args...> && convertible_to<visit_type<F, Args...>, Ret>;

    }

}


namespace impl {

    /* Backs the `visit()` algorithm by recursively expanding union types into a
    series of compile-time vtables covering all possible permutations of the argument
    types.  When `visit()` is executed, it equates to a sequence of indices into these
    vtables (one for each union), similar to a virtual method call, and with comparable
    overhead. */
    template <typename R, size_t I, typename F, typename... A>
        requires (meta::visitor<F, A...>)
    constexpr R visit_impl(F&& func, A&&... args)
        noexcept(meta::nothrow::visitor<F, A...>)
    {
        return std::forward<F>(func)(std::forward<A>(args)...);
    }

    template <typename R, size_t I, typename F, typename... A>
        requires (meta::visitor<F, A...> && I < sizeof...(A))
    constexpr R visit_impl(F&& func, A&&... args)
        noexcept(meta::nothrow::visitor<F, A...>)
    {
        return impl::visitable<meta::unpack_type<I, A...>>::template dispatch<R, I>(
            std::forward<F>(func),
            std::forward<A>(args)...
        );
    }

    template <typename T>
    struct visitable {
        static constexpr bool enable = false;  // meta::visitable<T> evaluates to false
        using type = T;
        using pack = bertrand::args<T>;

        template <typename R, size_t I, typename F, typename... A>
            requires (
                I < sizeof...(A) &&
                meta::is<T, meta::unpack_type<I, A...>> &&
                meta::visitor<F, A...>
            )
        static constexpr R dispatch(F&& func, A&&... args)
            noexcept(meta::nothrow::visitor<F, A...>)
        {
            return visit_impl<R, I + 1>(
                std::forward<F>(func),
                std::forward<A>(args)...
            );
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
                decltype((*std::declval<T>().template get_if<I>()))
            >::type;
        };
        template <typename... Ts>
        struct _pack<N, Ts...> { using type = bertrand::args<Ts...>; };

    public:
        static constexpr bool enable = true;
        using type = T;
        using pack = _pack<0>::type;

        template <typename R, size_t I, typename F, typename... A>
            requires (
                I < sizeof...(A) &&
                meta::is<T, meta::unpack_type<I, A...>> &&
                meta::visitor<F, A...>
            )
        static constexpr R dispatch(F&& func, A&&... args)
            noexcept(meta::nothrow::visitor<F, A...>)
        {
            // Build a vtable that dispatches to all possible alternatives
            static constexpr auto vtable = []<
                size_t... Prev,
                size_t... Next,
                size_t... Is
            >(
                std::index_sequence<Prev...>,
                std::index_sequence<Next...>,
                std::index_sequence<Is...>
            ) {
                /// NOTE: func and args are forwarded as lvalues in order to avoid
                /// unnecessary copies/moves within the dispatch logic.  They are
                /// converted back to their proper categories upon recursion
                return std::array{+[](
                    meta::as_lvalue<F> func,
                    meta::as_lvalue<A>... args
                ) noexcept(meta::nothrow::visitor<F, A...>) -> R {
                    using type = decltype((meta::unpack_arg<I>(
                        std::forward<A>(args)...
                    ).template get<Is>()));
                    if constexpr (meta::visitor<
                        F,
                        meta::unpack_type<Prev, A...>...,
                        type,
                        meta::unpack_type<I + 1 + Next, A...>...
                    >) {
                        return visit_impl<R, I + 1>(
                            std::forward<F>(func),
                            meta::unpack_arg<Prev>(
                                std::forward<A>(args)...
                            )...,
                            meta::unpack_arg<I>(
                                std::forward<A>(args)...
                            ).template get<Is>(),
                            meta::unpack_arg<I + 1 + Next>(
                                std::forward<A>(args)...
                            )...
                        );
                    } else if constexpr (meta::Expected<R>) {
                        return BadUnionAccess(
                            "failed to invoke visitor for union argument " +
                            impl::int_to_static_string<I> + " with active type '" +
                            type_name<type> + "'"
                        );
                    } else {
                        static_assert(
                            false,
                            "unreachable: a non-exhaustive iterator must always "
                            "return an `Expected` result"
                        );
                        std::unreachable();
                    }
                }...};
            }(
                std::make_index_sequence<I>{},
                std::make_index_sequence<sizeof...(A) - I - 1>{},
                std::make_index_sequence<N>{}
            );

            // search the vtable for the actual type, and recur for the next
            // argument until all arguments have been fully deduced
            return vtable[meta::unpack_arg<I>(args...).index()](func, args...);
        }
    };

    /* Optionals are converted into packs of length 2. */
    template <meta::Optional T>
    struct visitable<T> {
        static constexpr bool enable = true;
        using type = T;
        using pack = bertrand::args<decltype((*std::declval<T>())), std::nullopt_t>;
    };

    /// TODO: visit() for Expected

    /* `std::variant`s are treated like unions. */
    template <meta::variant T>
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
        using type = T;
        using pack = _pack<0>::type;
    };

}


/* Non-member `visit(f, args...)` operator, similar to `std::visit()`.  A member
version of this operator is implemented for `Union` objects, which allows for chaining.

The visitor is constructed from either a single function or a set of functions defined
using `bertrand::visitor` or a similar overload set.  The remaining arguments will be
passed to it in the order they are defined, with unions being unwrapped to their actual
types within the visitor context.  If the visitor is not callable for all possible
permutations of the unwrapped values, or no common return type exists between them,
then a compilation error will occur.  Note that the arguments are not limited to
unions, unlike `std::visit()` - if no unions are present, then this function is
identical to invoking the visitor normally. */
template <typename F, typename... Args> requires (meta::visitor<F, Args...>)
constexpr meta::visit_type<F, Args...> visit(F&& f, Args&&... args)
    noexcept(meta::nothrow::visitor<F, Args...>)
{
    return impl::visit_impl<meta::visit_type<F, Args...>, 0>(
        std::forward<F>(f),
        std::forward<Args>(args)...
    );
}


template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        (meta::not_void<Ts> && ...) &&
        meta::types_are_unique<Ts...>
    )
struct Union : impl::union_tag {
    static constexpr size_t alternatives = sizeof...(Ts);

    /* Get the templated type at index `I`.  Fails to compile if the index is out of
    bounds. */
    template <size_t I> requires (I < alternatives)
    using alternative = meta::unpack_type<I, Ts...>;

    /* Get the index of type `T`, assuming it is present in the union's template
    signature.  Fails to compile otherwise. */
    template <typename T> requires (std::same_as<T, Ts> || ...)
    static constexpr size_t index_of = meta::index_of<T, Ts...>;

private:
    template <meta::not_void T>
    friend struct bertrand::Optional;
    template <typename T, meta::unqualified E, meta::unqualified... Es>
        requires (meta::inherits<Es, Exception> && ...)
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

    // recursive C unions are the only way to allow Union<Ts...> to be used at compile
    // time without triggering the compiler's UB filters
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
        struct ref_t { using type = meta::remove_rvalue<V>; };
        template <meta::lvalue V>
        struct ref_t<V> { using type = meta::as_pointer<V>; };
        using ref = ref_t<U>::type;

        ref curr;
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
            noexcept(ref(std::forward<T>(value)))
        ) :
            curr(std::forward<T>(value))
        {}

        // base proximal constructor for lvalue references
        template <typename T>
            requires (std::same_as<U, proximal_type<T>> && meta::lvalue<U>)
        constexpr storage(T&& value) noexcept(
            noexcept(ref(&value))
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
            noexcept(ref(std::forward<T>(value)))
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
    using common = get_common_type<Ts...>::type;

    /* True if all alternatives share a common type to which they can be converted.
    If this is false, then the `flatten()` method will be disabled. */
    static constexpr bool can_flatten = meta::not_void<common>;

    /* Get the index of the currently-active type in the union. */
    [[nodiscard]] constexpr size_t index() const noexcept {
        return m_index;
    }

    /* Check whether the variant holds a specific type. */
    template <typename T> requires (std::same_as<T, Ts> || ...)
    [[nodiscard]] constexpr bool holds_alternative() const noexcept {
        return m_index == index_of<T>;
    }

    /* Get the value of the type at index `I`.  Fails to compile if the index is out of
    range.  Otherwise, throws a `TypeError` if the indexed type is not the active
    member. */
    template <size_t I, typename Self> requires (I < alternatives)
    [[nodiscard]] constexpr access<I, Self> get(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (I != self.index()) {
                throw TypeError(
                    "Invalid union index: " + std::to_string(I) + " (active is " +
                    std::to_string(self.index()) + ")"
                );
            }
        }
        return (std::forward<Self>(self).m_storage.template get<I>());
    }

    /* Get the value for the templated type.  Fails to compile if the templated type
    is not a valid union member.  Otherwise, throws a `TypeError` if the templated type
    is not the active member. */
    template <typename T, typename Self> requires (std::same_as<T, Ts> || ...)
    [[nodiscard]] constexpr access<index_of<T>, Self> get(this Self&& self) noexcept(!DEBUG) {
        constexpr size_t idx = index_of<T>;
        if constexpr (DEBUG) {
            if (idx != self.index()) {
                throw TypeError(
                    /// TODO: print the demangled type name
                    "Invalid union index: " + std::to_string(idx) + " (active is " +
                    std::to_string(self.index()) + ")"
                );
            }
        }
        return (std::forward<Self>(self).m_storage.template get<idx>());
    }

    /// TODO: use this as the default get<>() implementation once Expected<> is in a
    /// good enough state to support it

    // template <size_t I, typename Self> requires (I < alternatives)
    // [[nodiscard]] constexpr exp_val<I, Self> safe_get(this Self&& self) noexcept {
    //     if (self.index() != I) {
    //         return get_index_error<I>[self.index()]();
    //     }
    //     return std::forward<Self>(self).m_storage.template get<I>();
    // }

    // template <typename T, typename Self> requires (std::same_as<T, Ts> || ...)
    // [[nodiscard]] constexpr exp_val<index_of<T>, Self> safe_get(this Self&& self) noexcept {
    //     if (self.index() != index_of<T>) {
    //         return get_type_error<T>[self.index()]();
    //     }
    //     return std::forward<Self>(self).m_storage.template get<index_of<T>>();
    // }

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
    template <typename T, typename Self> requires (std::same_as<T, Ts> || ...)
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
        return impl::visit_impl<meta::visit_type<F, Self, Args...>, 0>(
            std::forward<F>(f),
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );
    }

    /* Flatten the union into a single type, implicitly converting the active member to
    the common type, assuming one exists.  If no common type exists, then this will
    fail to compile.  Users can check the `can_flatten` flag to guard against this. */
    template <typename Self> requires (can_flatten)
    [[nodiscard]] constexpr common flatten(this Self&& self) noexcept(
        (meta::nothrow::convertible_to<Ts, common> && ...)
    ) {
        return std::forward<Self>(self).visit([](auto&& value) -> common {
            return std::forward<decltype(value)>(value);
        });
    }

private:
    static constexpr bool default_constructible = meta::not_void<default_type>;

    template <typename T>
    static constexpr bool can_convert = meta::not_void<conversion_type<T>>;

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
            if constexpr (!meta::trivially_destructible<Ts>) {
                std::destroy_at(&self.m_storage.template get<index_of<Ts>>());
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

    /// TODO: dereference, ->* operator overloads?

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::size), Self>)
    constexpr decltype(auto) size(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::size), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::size);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::empty), Self>)
    constexpr decltype(auto) empty(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::empty), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::empty);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::begin), Self>)
    constexpr decltype(auto) begin(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::begin), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::begin);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::cbegin), Self>)
    constexpr decltype(auto) cbegin(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::cbegin), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::cbegin);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::end), Self>)
    constexpr decltype(auto) end(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::end), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::end);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::cend), Self>)
    constexpr decltype(auto) cend(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::cend), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::cend);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::rbegin), Self>)
    constexpr decltype(auto) rbegin(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::rbegin), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::rbegin);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::crbegin), Self>)
    constexpr decltype(auto) crbegin(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::crbegin), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::crbegin);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::rend), Self>)
    constexpr decltype(auto) rend(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::rend), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::rend);
    }

    template <typename Self>
        requires (meta::visitor<decltype(std::ranges::crend), Self>)
    constexpr decltype(auto) crend(this Self&& self)
        noexcept(meta::nothrow::visitor<decltype(std::ranges::crend), Self>)
    {
        return std::forward<Self>(self).visit(std::ranges::crend);
    }
};


/// TODO: I may want to remove the contextual bool conversion from optional and force
/// the user to call `has_value()` at all times, which would probably help avoid type
/// confusion in long chains of visitors.  The same might also apply to `Expected`,
/// so that you have to explicitly unwrap all values via the type system.  That also
/// potentially opens these operators up to be included in the monadic forwarding
/// interface as well.


template <meta::not_void T>
struct Optional : impl::optional_tag {
    using value_type = T;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<value_type>;
    using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

private:
    template <typename U>
    struct storage {
        Union<std::nullopt_t, T> m_data;

        constexpr storage() noexcept : m_data(std::nullopt) {};

        template <meta::convertible_to<value_type> V>
        constexpr storage(V&& value)
            noexcept(meta::nothrow::convertible_to<V, value_type>)
        :
            m_data(value_type(std::forward<V>(value)))
        {}

        constexpr void swap(storage& other)
            noexcept(noexcept(m_data.swap(other.m_data)))
            requires(requires{m_data.swap(other.m_data);})
        {
            m_data.swap(other.m_data);
        }

        constexpr void reset() noexcept(noexcept(m_data = std::nullopt)) {
            m_data = std::nullopt;
        }

        /// TODO: & operator on the expected returned by get<>() is not allowed.  I
        /// will have to dereference the expected first.

        constexpr explicit operator bool() const noexcept {
            return m_data.index() == 1;
        }

        template <typename Self>
        constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return std::forward<Self>(self).m_data.m_storage.template get<1>();
        }

        constexpr pointer operator->() noexcept {
            return &m_data.m_storage.template get<1>();
        }

        constexpr const_pointer operator->() const noexcept {
            return &m_data.m_storage.template get<1>();
        }
    };

    template <meta::lvalue U>
    struct storage<U> {
        pointer m_data = nullptr;

        constexpr storage() noexcept = default;
        constexpr storage(U value) noexcept(noexcept(&value)) : m_data(&value) {}

        constexpr explicit operator bool() const noexcept {
            return m_data != nullptr;
        }

        constexpr void swap(storage& other) noexcept {
            std::swap(m_data, other.m_data);
        }

        constexpr void reset() noexcept {
            m_data = nullptr;
        }

        template <typename Self>
        constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return *std::forward<Self>(self).m_data;
        }

        constexpr pointer operator->() noexcept {
            return m_data;
        }

        constexpr const_pointer operator->() const noexcept {
            return m_data;
        }
    };

    storage<T> m_storage;

    template <typename Self>
    using access = decltype((*std::declval<Self>().m_storage));

    template <typename Self, meta::invocable<access<Self>> F>
    struct _and_then_t { using type = Optional<meta::invoke_type<F, access<Self>>>; };
    template <typename Self, meta::invocable<access<Self>> F>
        requires (meta::Optional<meta::invoke_type<F, access<Self>>>)
    struct _and_then_t<Self, F> { using type = meta::invoke_type<F, access<Self>>; };
    template <typename Self, meta::invocable<access<Self>> F>
    using and_then_t = _and_then_t<Self, F>::type;

    template <typename Self, meta::invocable<> F>
    struct _or_else_t {
        using type = Optional<meta::common_type<
            value_type,
            meta::invoke_type<F>
        >>;
    };
    template <typename Self, meta::invocable<> F>
        requires (meta::Optional<meta::invoke_type<F>>)
    struct _or_else_t<Self, F> {
        using type = Optional<meta::common_type<
            value_type,
            typename meta::invoke_type<F>::value_type
        >>;
    };
    template <typename Self, meta::invocable<> F>
    using or_else_t = _or_else_t<Self, F>::type;

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

    /* Clear the current value, restoring the optional to the empty state. */
    constexpr void reset() noexcept(noexcept(m_storage.reset())) {
        m_storage.reset();
    }

    /* Returns `true` if the optional currently holds a value, or `false` if it is in
    the empty state. */
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return bool(m_storage);
    }

    /// TODO: delete this

    /* Returns `true` if the optional currently holds a value, or `false` if it is in
    the empty state. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return bool(m_storage);
    }

    /* Access the stored value.  Throws a `BadUnionAccess` exception if the optional
    is currently in the empty state and the program is compiled in debug mode. */
    template <typename Self>
    [[nodiscard]] constexpr access<Self> value(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.has_value()) {
                throw BadUnionAccess("Cannot access value of an empty Optional");
            }
        }
        return *std::forward<Self>(self).m_storage;
    }

    /// TODO: delete these operators

    /* Dereference the optional to access the stored value.  Throws a
    `BadUnionAccess` exception if the optional is currently in the empty state. */
    template <typename Self>
    [[nodiscard]] constexpr access<Self> operator*(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.has_value()) {
                throw BadUnionAccess("Cannot dereference an empty Optional");
            }
        }
        return *std::forward<Self>(self).m_storage;
    }

    /* Dereference the optional to access a member of the stored value.  Throws a
    `BadUnionAccess` exception if the optional is currently in the empty state. */
    [[nodiscard]] constexpr pointer operator->() noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!has_value()) {
                throw BadUnionAccess("Cannot dereference an empty Optional");
            }
        }
        return m_storage.operator->();
    }

    /* Dereference the optional to access a member of the stored value.  Throws a
    `BadUnionAccess` exception if the optional is currently in the empty state. */
    [[nodiscard]] constexpr const_pointer operator->() const noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!has_value()) {
                throw BadUnionAccess("Cannot dereference an empty Optional");
            }
        }
        return m_storage.operator->();
    }

    /* Access the stored value or return the default value if the optional is empty,
    returning the common type between the wrapped type and the default value. */
    template <typename Self, typename V>
    [[nodiscard]] constexpr meta::common_type<access<Self>, V> value_or(
        this Self&& self,
        V&& fallback
    ) noexcept(
        noexcept(meta::common_type<access<Self>, V>(*std::forward<Self>(self).m_storage)) &&
        noexcept(meta::common_type<access<Self>, V>(std::forward<V>(fallback)))
    ) {
        if (self.has_value()) {
            return *std::forward<Self>(self).m_storage;
        } else {
            return std::forward<V>(fallback);
        }
    }

    /////////////////////
    ////    MONAD    ////
    /////////////////////

    /* Invoke a visitor function on the wrapped value if the optional is not empty,
    returning another optional containing the transformed result.  If the original
    optional was in the empty state, then the visitor function will not be invoked,
    and the result will be empty as well.  If the visitor returns an `Optional`, then
    it will be flattened into the result, merging the empty states.

    Note that the extra flattening behavior technically distinguishes this method from
    the `std::optional::and_then()` equivalent, which never flattens.  In that case,
    if the visitor returns an optional, then the result will be a nested optional with
    2 distinct empty states.  Bertrand considers this to be an anti-pattern, as the
    nested indirection can easily cause confusion, and does not conform to the rest of
    the monadic operator interface, which flattens by default.  If the empty states
    must remain distinct, then it is better expressed by explicitly handling the
    original empty state before chaining, or by converting the optional into a
    `Union<Ts...>` or `Expected<T, Union<Es...>>` beforehand, both of which can
    represent multiple distinct empty states without nesting. */
    template <typename Self, meta::invocable<access<Self>> F>
    [[nodiscard]] constexpr and_then_t<Self, F> and_then(this Self&& self, F&& f) noexcept(
        meta::nothrow::invoke_returns<and_then_t<Self, F>, F, access<Self>>
    ) {
        if (!self.has_value()) {
            return {};
        }
        return std::forward<F>(f)(*std::forward<Self>(self).m_storage);
    }

    /* Invoke a visitor function if the optional is empty, returning another optional
    containing either the original value or the transformed result.  If the original
    optional was not empty, then the visitor function will not be invoked, and the
    value will be converted to the common type between the original value and the
    result of the visitor.  If the visitor returns an `Optional`, then it will be
    flattened into the result.

    Note that the extra flattening behavior technically distinguishes this method from
    the `std::optional::or_else()` equivalent, which never flattens.  In that case,
    if the visitor returns an optional, then the result will be a nested optional with
    2 distinct empty states, the first of which can never occur.  Bertrand considers
    this to be an anti-pattern, as the nested indirection can easily cause confusion,
    and does not conform to the rest of the monadic operator interface, which flattens
    by default.  If the empty states must remain distinct, then it is better expressed
    by explicitly handling the original empty state before chaining, or by converting
    the optional into a `Union<Ts...>` or `Expected<T, Union<Es...>>` beforehand, both
    of which can represent multiple distinct empty states without nesting. */
    template <typename Self, meta::invocable<> F>
    [[nodiscard]] constexpr or_else_t<Self, F> or_else(this Self&& self, F&& f) noexcept(
        meta::nothrow::invoke_returns<or_else_t<Self, F>, F> &&
        meta::nothrow::convertible_to<or_else_t<Self, F>, access<Self>>
    ) {
        if (self.has_value()) {
            return *std::forward<Self>(self).m_storage;
        }
        return std::forward<F>(f)();
    }

    /// TODO: rest of monadic optional interface, including iterators, which for the
    /// empty state will simply produce an end() iterator.  Perhaps this can be
    /// implemented such that the iterator type holds a union of the begin and end
    /// iterators along with impl::sentinel{}, which would be used for the empty
    /// case.  This will be somewhat complex to implement, but would be a nice
    /// addition, and should be clearer than the `std::optional` interface, which
    /// treats the optional as a range with a single value.

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

    /// TODO: iterators

};


template <typename T, meta::unqualified E, meta::unqualified... Es>
    requires (meta::inherits<Es, Exception> && ...)
struct Expected : impl::expected_tag {
    using value_type = T;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<value_type>;
    using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

private:
    struct compile_time_t {};
    struct run_time_t {};

    template <typename out, typename...>
    struct _search_error { using type = out; };
    template <typename out, typename in, typename curr, typename... next>
    struct _search_error<out, in, curr, next...> {
        using type = _search_error<out, in, next...>::type;
    };
    template <typename out, typename in, typename curr, typename... next>
        requires (
            !meta::constructible_from<value_type, in> &&
            meta::inherits<in, curr>
        )
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

    /// TODO: if executed at runtime, store each of the errors as a unique_ptr to
    /// allow for polymorphism.  Otherwise, store them directly to avoid a heap
    /// allocation from compile time to runtime.
    /// -> Allocating them as heap objects is probably incorrect?  I need to research
    /// how to do this correctly.  This should probably just work

    /// TODO: figure out proper noexcept specifiers

    bool m_compiled;
    union storage {
        struct empty_t {};
        using type = std::conditional_t<meta::is_void<T>, empty_t, value_type>;

        /* At compile time, we store the error instances directly in a non-polymorphic
        fashion, so as to avoid leaking an allocation into runtime. */
        struct CompileTime {
            template <typename... Us>
            struct _Error { using type = Union<Us...>; };
            template <typename U>
            struct _Error<U> { using type = U; };
            using Error = _Error<E, Es...>::type;

            bool ok;
            union Data {
                type result;
                Error error;

                template <typename... As> requires (meta::constructible_from<type, As...>)
                constexpr Data(As&&... args)
                    noexcept(meta::nothrow::constructible_from<type, As...>)
                :
                    result(std::forward<As>(args)...)
                {}

                template <meta::convertible_to<Error> V>
                    requires (meta::not_void<err_type<V>>)
                constexpr Data(V&& e)
                    noexcept(meta::nothrow::convertible_to<V, Error>)
                :
                    error(std::forward<V>(e))
                {}

                constexpr ~Data() noexcept {}
            } data;

            template <typename... As> requires (meta::constructible_from<type, As...>)
            constexpr CompileTime(As&&... args)
                noexcept(meta::nothrow::constructible_from<type, As...>)
            :
                ok(true),
                data(std::forward<As>(args)...)
            {}

            template <meta::convertible_to<Error> V>
                requires (meta::not_void<err_type<V>>)
            constexpr CompileTime(V&& e)
                noexcept(meta::nothrow::convertible_to<V, Error>)
            :
                ok(false),
                data(std::forward<V>(e))
            {}

            constexpr CompileTime(const CompileTime& other) noexcept(
                false
            ) :
                ok(other.ok),
                data(other.ok ? Data(other.data.result) : Data(other.data.error))
            {}

            constexpr CompileTime(CompileTime&& other) noexcept(
                false
            ) :
                ok(other.ok),
                data(other.ok ?
                    Data(std::move(other.data.result)) :
                    Data(std::move(other.data.error))
                )
            {}

            constexpr CompileTime& operator=(const CompileTime& other) noexcept(
                false
            ) {
                if (ok) {
                    if (other.ok) {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        data.result = other.data.result;
                    } else {
                        std::destroy_at(&data.result);
                        std::construct_at(
                            &data.error,
                            other.data.error
                        );
                    }
                } else {
                    if (other.ok) {
                        std::destroy_at(&data.error);
                        std::construct_at(
                            &data.result,
                            other.data.result
                        );
                    } else {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        data.error = other.data.error;
                    }
                }
                ok = other.ok;
                return *this;
            }

            constexpr CompileTime& operator=(CompileTime&& other) noexcept(
                false
            ) {
                if (ok) {
                    if (other.ok) {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        data.result = std::move(other.data.result);
                    } else {
                        std::destroy_at(&data.result);
                        std::construct_at(
                            &data.error,
                            std::move(other.data.error)
                        );
                    }
                } else {
                    if (other.ok) {
                        std::destroy_at(&data.error);
                        std::construct_at(
                            &data.result,
                            std::move(other.data.result)
                        );
                    } else {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        data.error = std::move(other.data.error);
                    }
                }
                ok = other.ok;
                return *this;
            }

            constexpr CompileTime() noexcept(
                meta::nothrow::destructible<type> &&
                meta::nothrow::destructible<Error>
            ) {
                if (ok) {
                    std::destroy_at(&data.result);
                } else {
                    std::destroy_at(&data.error);
                }
            }
        } compile_time;

        /* At run time, we store the error instances as unique_ptrs to the templated
        classes.  This allows them to respect polymorphic error types, which can be
        simpler for code that involves many errors. */
        struct RunTime {
            template <typename... Us>
            struct _Error { using type = Union<std::unique_ptr<Us>...>; };
            template <typename U>
            struct _Error<U> { using type = std::unique_ptr<U>; };
            using Error = _Error<E, Es...>::type;

            bool ok;
            union Data {
                type result;
                Error error;

                template <typename... As> requires (meta::constructible_from<type, As...>)
                constexpr Data(As&&... args)
                    noexcept(meta::nothrow::constructible_from<type, As...>)
                :
                    result(std::forward<As>(args)...)
                {}

                /// TODO: I might need to manually specify which alternative to
                /// construct here.

                template <typename V> requires (meta::not_void<err_type<V>>)
                constexpr Data(V&& e)
                    noexcept(noexcept(Error(
                        std::make_unique<meta::unqualify<V>>(std::forward<V>(e))
                    )))
                :
                    error(std::make_unique<meta::unqualify<V>>(std::forward<V>(e)))
                {}

                constexpr ~Data() noexcept {}
            } data;            

            template <typename... As> requires (meta::constructible_from<type, As...>)
            RunTime(As&&... args)
                noexcept(meta::nothrow::constructible_from<type, As...>)
            :
                ok(true),
                data(std::forward<As>(args)...)
            {}

            template <meta::convertible_to<Error> V>
                requires (meta::not_void<err_type<V>>)
            RunTime(V&& e)
                noexcept(meta::nothrow::convertible_to<V, Error>)
            :
                ok(false),
                data(std::forward<V>(e))
            {}

            constexpr RunTime(const RunTime& other) noexcept(
                false
            ) :
                ok(other.ok),
                data(other.ok ? Data(other.data.result) : Data(other.data.error))
            {}

            constexpr RunTime(RunTime&& other) noexcept(
                false
            ) :
                ok(other.ok),
                data(other.ok ?
                    Data(std::move(other.data.result)) :
                    Data(std::move(other.data.error))
                )
            {}

            constexpr RunTime& operator=(const RunTime& other) noexcept(
                false
            ) {
                if (ok) {
                    if (other.ok) {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        data.result = other.data.result;
                    } else {
                        std::destroy_at(&data.result);
                        std::construct_at(
                            &data.error,
                            other.data.error
                        );
                    }
                } else {
                    if (other.ok) {
                        std::destroy_at(&data.error);
                        std::construct_at(
                            &data.result,
                            other.data.result
                        );
                    } else {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        data.error = other.data.error;
                    }
                }
                ok = other.ok;
                return *this;
            }

            constexpr RunTime& operator=(RunTime&& other) noexcept(
                false
            ) {
                if (ok) {
                    if (other.ok) {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        data.result = std::move(other.data.result);
                    } else {
                        std::destroy_at(&data.result);
                        std::construct_at(
                            &data.error,
                            std::move(other.data.error)
                        );
                    }
                } else {
                    if (other.ok) {
                        std::destroy_at(&data.error);
                        std::construct_at(
                            &data.result,
                            std::move(other.data.result)
                        );
                    } else {
                        /// TODO: check to see for assignment, etc.  Do this as
                        /// intelligently as possible
                        data.error = std::move(other.data.error);
                    }
                }
                ok = other.ok;
                return *this;
            }

            constexpr RunTime() noexcept(
                meta::nothrow::destructible<type> &&
                meta::nothrow::destructible<Error>
            ) {
                if (ok) {
                    std::destroy_at(&data.result);
                } else {
                    std::destroy_at(&data.error);
                }
            }
        } run_time;

        template <typename... As> requires (meta::constructible_from<CompileTime, As...>)
        constexpr storage(compile_time_t, As&&... args)
            noexcept(meta::nothrow::constructible_from<CompileTime, As...>)
        :
            compile_time(std::forward<As>(args)...)
        {}

        template <typename... As> requires (meta::constructible_from<RunTime, As...>)
        storage(run_time_t, As&&... args)
            noexcept(meta::nothrow::constructible_from<RunTime, As...>)
        :
            run_time(std::forward<As>(args)...)
        {}

        constexpr ~storage() noexcept {}
    } m_storage;

public:
    template <typename... Args>
        requires (meta::constructible_from<typename storage::type, Args...>)
    [[nodiscard]] constexpr Expected(Args&&... args) noexcept(
        meta::nothrow::constructible_from<typename storage::type, Args...>
    ) :
        m_compiled(std::is_constant_evaluated()),
        m_storage([](auto&&... args) noexcept(false) -> storage {
            if consteval {
                return {compile_time_t{}, std::forward<Args>(args)...};
            } else {
                return {run_time_t{}, std::forward<Args>(args)...};
            }
        }(std::forward<Args>(args)...))
    {}

    template <typename V>
        requires (
            !meta::constructible_from<typename storage::type, V> &&
            meta::inherits<V, E> ||
            (meta::inherits<V, Es> || ...)
        )
    [[nodiscard]] constexpr Expected(V&& e) noexcept(
        noexcept(storage(compile_time_t{}, std::forward<V>(e))) &&
        noexcept(storage(run_time_t{}, std::forward<V>(e)))
    ) :
        m_compiled(std::is_constant_evaluated()),
        m_storage([](auto&& e) noexcept(false) -> storage {
            if consteval {
                return {compile_time_t{}, std::forward<V>(e)};
            } else {
                return {run_time_t{}, std::forward<V>(e)};
            }
        }(std::forward<V>(e)))
    {}

    [[nodiscard]] constexpr Expected(const Expected& other) noexcept(
        false
    ) :
        m_compiled(other.compiled()),
        m_storage([](const Expected& other) noexcept(false) -> storage {
            if consteval {
                return {compile_time_t{}, other.m_storage.compile_time};
            } else {
                if (other.compiled()) {
                    return {compile_time_t{}, other.m_storage.compile_time};
                }
                return {run_time_t{}, other.m_storage.run_time};
            }
        }(other))
    {}

    [[nodiscard]] constexpr Expected(Expected&& other) noexcept(
        false
    ) :
        m_compiled(other.compiled()),
        m_storage([](Expected&& other) noexcept(false) -> storage {
            if consteval {
                return {compile_time_t{}, std::move(other.m_storage.compile_time)};
            } else {
                if (other.compiled()) {
                    return {compile_time_t{}, std::move(other.m_storage.compile_time)};
                }
                return {run_time_t{}, std::move(other.m_storage.run_time)};
            }
        }(std::move(other)))
    {}

    constexpr Expected& operator=(const Expected& other) noexcept(
        false
    ) {
        if (this != &other) {
            if consteval {
                m_storage.compile_time = other.m_storage.compile_time;
            } else {
                if (compiled()) {
                    if (other.compiled()) {
                        m_storage.compile_time = other.m_storage.compile_time;
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
                        m_storage.run_time = other.m_storage.run_time;
                    }
                }
            }
            m_compiled = other.compiled();
        }
        return *this;
    }

    constexpr Expected& operator=(Expected&& other) noexcept(
        false
    ) {
        if (this != &other) {
            if consteval {
                m_storage.compile_time = std::move(other.m_storage.compile_time);
            } else {
                if (compiled()) {
                    if (other.compiled()) {
                        m_storage.compile_time = std::move(other.m_storage.compile_time);
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
                        m_storage.run_time = std::move(other.m_storage.run_time);
                    }
                }
            }
            m_compiled = other.compiled();
        }
        return *this;
    }

    constexpr ~Expected() noexcept(
        false
    ) {
        if consteval {
            std::destroy_at(&m_storage.compile_time);
        } else {
            if (compiled()) {
                std::destroy_at(&m_storage.compile_time);
            } else {
                std::destroy_at(&m_storage.run_time);
            }
        }
    }

    /* `True` if the `Expected` was created at compile time.  `False` if it was created
    at runtime.  Compile-time exception states will not store a stack trace, and will
    not support polymorphism, so as to be constexpr-compatible. */
    [[nodiscard]] constexpr bool compiled() const noexcept { return m_compiled; }

    /* True if the `Expected` stores a valid result.  `False` if it is in the error
    state. */
    [[nodiscard]] constexpr bool ok() const noexcept {
        if consteval {
            return m_storage.compile_time.ok;
        } else {
            return compiled() ? m_storage.compile_time.ok : m_storage.run_time.ok;
        }
    }

    /* Access the valid state.  Throws a `BadUnionAccess` exception if the expected is
    currently in the error state and the program is compiled in debug mode. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) result(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.ok()) {
                throw BadUnionAccess("Expected in error state has no result");
            }
        }
        if consteval {
            return (std::forward<Self>(self).m_storage.compile_time.data.result);
        } else {
            if (self.compiled()) {
                return (std::forward<Self>(self).m_storage.compile_time.data.result);
            } else {
                return (std::forward<Self>(self).m_storage.run_time.data.result);
            }
        }
    }

    /* Access the error state.  Throws a `BadUnionAccess` exception if the expected is
    currently in the valid state and the program is compiled in debug mode. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) error(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.ok()) {
                throw BadUnionAccess("Expected in valid state has no error");
            }
        }
        if consteval {
            return (std::forward<Self>(self).m_storage.compile_time.data.error);
        } else {
            if (self.compiled()) {
                return (std::forward<Self>(self).m_storage.compile_time.data.error);
            } else {
                return (std::forward<Self>(self).m_storage.run_time.data.error);
            }
        }
    }


    /// TODO: don't really know if I even need any other methods here?  Maybe not
    /// even visit()?  That or visit() needs to cover all states, including the
    /// valid state, and that gets pretty tricky.  Maybe it can be accounted for within
    /// the visitor interface, where it inserts an extra item for the valid state, and
    /// then delegates to `visitable<errors...>::dispatch()` to reuse existing logic


    /////////////////////
    ////    MONAD    ////
    /////////////////////

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

    /// TODO: figure out iterators
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


template <meta::visitable T> requires (meta::visitor<impl::Pos, T>)
constexpr decltype(auto) operator+(T&& val)
    noexcept(meta::nothrow::visitor<impl::Pos, T>)
{
    return visit(
        impl::Pos{},
        std::forward<T>(val)
    );
}


template <meta::visitable T> requires (meta::visitor<impl::Neg, T>)
constexpr decltype(auto) operator-(T&& val)
    noexcept(meta::nothrow::visitor<impl::Neg, T>)
{
    return visit(
        impl::Neg{},
        std::forward<T>(val)
    );
}


template <meta::visitable T> requires (meta::visitor<impl::Invert, T>)
constexpr decltype(auto) operator~(T&& val)
    noexcept(meta::nothrow::visitor<impl::Invert, T>)
{
    return visit(
        impl::Invert{},
        std::forward<T>(val)
    );
}


template <meta::visitable T> requires (meta::visitor<impl::PreIncrement, T>)
constexpr decltype(auto) operator++(T&& val)
    noexcept(meta::nothrow::visitor<impl::PreIncrement, T>)
{
    return visit(
        impl::PreIncrement{},
        std::forward<T>(val)
    );
}


template <meta::visitable T> requires (meta::visitor<impl::PostIncrement, T>)
constexpr decltype(auto) operator++(T&& val, int)
    noexcept(meta::nothrow::visitor<impl::PostIncrement, T>)
{
    return visit(
        impl::PostIncrement{},
        std::forward<T>(val)
    );
}


template <meta::visitable T> requires (meta::visitor<impl::PreDecrement, T>)
constexpr decltype(auto) operator--(T&& val)
    noexcept(meta::nothrow::visitor<impl::PreDecrement, T>)
{
    return visit(
        impl::PreDecrement{},
        std::forward<T>(val)
    );
}


template <meta::visitable T> requires (meta::visitor<impl::PostDecrement, T>)
constexpr decltype(auto) operator--(T&& val, int)
    noexcept(meta::nothrow::visitor<impl::PostDecrement, T>)
{
    return visit(
        impl::PostDecrement{},
        std::forward<T>(val)
    );
}


template <meta::visitable T>
constexpr decltype(auto) operator!(T&& val)
    noexcept(meta::nothrow::visitor<impl::LogicalNot, T>)
{
    return visit(
        impl::LogicalNot{},
        std::forward<T>(val)
    );
}


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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


template <typename L, typename R>
    requires (
        (meta::visitable<L> || meta::visitable<R>) &&
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

    /// TODO: variant_size and variant_alternative?

    template <bertrand::meta::visitable T>
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
            static_assert(u.get<u.index()>() == "abc");
            static_assert(u2.get<2>() == "abc");
            static_assert(u3.get<2>() == "abc");
            static_assert(u.get<std::string>() == "abc");
    
            Union<int, const int&, std::string> u4 = "abc";
            Union<int, const int&, std::string> u5 = u4;
            u4 = u;
            u4 = std::move(u5);
        }

        {
            static constexpr int value = 2;
            static constexpr Union<int, const int&> val = value;
            static constexpr Union<int, const int&> val2 = val;
            static_assert(val <= 2);
            static_assert((val + 1) / val2 == 1);
            static constexpr auto f = [](int x, int y) noexcept {
                return x + y;
            };
            static_assert(val.get<val.index()>() == 2);
            static_assert(visit(f, val, 1) == 3);
            static_assert(val.visit(f, 1) == 3);
            static_assert(noexcept(visit(f, val, 1) == 3));
            static_assert(noexcept(val.visit(f, 1) == 3));
            static_assert(val.flatten() == 2);
            static_assert(noexcept(val.flatten() == 2));
            static_assert(double(val) == 2.0);
            static_assert(val.index() == 1);
            static_assert(val.holds_alternative<const int&>());
            static_assert(*val.get_if<val.index()>() == 2);
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
            static_assert(o->size() == 3);
            decltype(auto) ov = *o;
            decltype(auto) ov2 = *Optional<std::string_view>{"abc", 3};
        }


        {
            struct A { static constexpr int operator()() { return 42; } };
            struct B { static constexpr std::string operator()() { return "hello, world"; } };
            static constexpr Union<A, B> u = B{};
            // static_assert(u() == "hello, world");

            static constexpr Union<std::array<int, 3>, std::string> u2 =
                std::array{1, 2, 3};
            static_assert(u2[1] == 2);
        }

        {
            static constexpr Union<std::string, const std::string&> u =
                "abc";
            for (auto&& x : u) {

            }
        }
    }

}


#endif  // BERTRAND_UNION_H
