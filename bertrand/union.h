#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


namespace impl {
    struct union_tag {};
    struct optional_tag {};
    struct expected_tag {};

}


namespace meta {

    template <typename T>
    concept Union = inherits<T, impl::union_tag>;

    template <typename T>
    concept Optional = inherits<T, impl::optional_tag>;

    template <typename T>
    concept Expected = inherits<T, impl::expected_tag>;

    template <typename E>
    concept unexpected = meta::unqualified<E> && (
        meta::inherits<E, Exception> ||
        (meta::Union<E> && []<size_t... Is>(std::index_sequence<Is...>) {
            return (meta::inherits<
                typename E::template alternative<Is>,
                Exception
            > && ...);
        }(std::make_index_sequence<E::alternatives>{}))
    );

}


/* A type-safe union capable of storing any number of arbitrarily-qualified types.

This is similar to `std::variant<Ts...>`, but with the following changes:

    1. `Ts...` may have cvref qualifications, allowing the union to model references
         and other cvref-qualified types without requiring an extra copy or
         `std::reference_wrapper` workaround.

    /// TODO: rest of documentation, similar to above


Unions of this form are monadic in nature, allowing users to chain operations on the
union in a type-safe manner.  At each step in the chain, the type system will attempt
to deduce the individual members that support the given operation, and form a new
union containing the possible results.  If all members return the same type, then that
type will be returned directly instead.  Additionally, if any type in the union does
not support the requested operation, the result will be returned as an
`Expected<T, BadUnionAccess>`, where `T` is the deduced result type, and the error
state indicates that the active type was invalid. */
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
template <typename T, meta::unexpected E = Exception>
struct Expected;


/* A specialization of `Expected` that can be used to model void functions, where no
result is expected, but an error can still occur.  This specialization lacks a
monadic operator interface and dereference operators. */
template <meta::is_void T, meta::unexpected E>
struct Expected<T, E>;


template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        (meta::not_void<Ts> && ...) &&
        meta::types_are_unique<Ts...>
    )
struct Union {
    static constexpr size_t alternatives = sizeof...(Ts);

private:
    template <meta::not_void T>
    friend struct bertrand::Optional;
    template <typename T, meta::unexpected E>
    friend struct bertrand::Expected;

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
    struct _conversion_type { using type = result; };
    template <typename T, typename result, typename U, typename... Us>
    struct _conversion_type<T, result, U, Us...> {
        // no match at this index: advance U
        template <typename V>
        struct filter { using type = _conversion_type<T, result, Us...>::type; };

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
            using type = _conversion_type<T, typename replace<result>::type, Us...>::type;
        };

        // execute the metafunction
        using type = filter<T>::type;
    };
    template <typename T>
    using conversion_type = _conversion_type<T, void, Ts...>::type;

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

        constexpr storage()
            noexcept(meta::nothrow::default_constructible<default_type>)
            requires(std::same_as<U, default_type>)
        :
            curr()  // not a reference type by definition
        {}

        constexpr storage()
            noexcept(meta::nothrow::default_constructible<default_type>)
            requires(!std::same_as<U, default_type>)
        :
            rest()
        {}

        template <typename T> requires (std::same_as<U, conversion_type<T>>)
        constexpr storage(T&& value) noexcept(
            noexcept(ref(std::forward<T>(value)))
        ) :
            curr(std::forward<T>(value))
        {}

        template <typename T> requires (std::same_as<U, conversion_type<T>> && meta::lvalue<U>)
        constexpr storage(T&& value) noexcept(
            noexcept(ref(&value))
        ) :
            curr(&value)
        {}

        template <typename T> requires (!std::same_as<U, conversion_type<T>>)
        constexpr storage(T&& value) noexcept(
            noexcept(storage<Us...>(std::forward<T>(value)))
        ) :
            rest(std::forward<T>(value))
        {}

        constexpr ~storage() noexcept {}

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

public:

    /* Get the templated type at index `I`.  Fails to compile if the index is out of
    bounds. */
    template <size_t I> requires (I < alternatives)
    using alternative = meta::unpack_type<I, Ts...>;

    /* Get the index of type `T`, assuming it is present in the union's template
    signature.  Fails to compile otherwise. */
    template <typename T> requires (std::same_as<T, Ts> || ...)
    static constexpr size_t index_of = meta::index_of<T, Ts...>;

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

    // template <size_t I, typename Self> requires (I < N)
    // [[nodiscard]] constexpr exp_val<I, Self> safe_get(
    //     this Self&& self
    // ) noexcept {
    //     if (self.index() != I) {
    //         return BadUnionAccess(
    //             "Invalid union index: " + std::to_string(I) + " (active is " +
    //             std::to_string(self.index()) + ")"
    //         );
    //     }
    //     return std::forward<Self>(self).m_storage.template get<I>();
    // }

    // template <typename T, typename Self> requires (std::same_as<T, Ts> || ...)
    // [[nodiscard]] constexpr exp_val<index_of<T>, Self> safe_get(
    //     this Self&& self
    // ) noexcept {
    //     if (self.index() != index_of<T>) {
    //         return BadUnionAccess(
    //             /// TODO: print the demangled type name
    //             "Invalid union index: " + std::to_string(index_of<T>) + " (active is " +
    //             std::to_string(self.index()) + ")"
    //         );
    //     }
    //     return std::forward<Self>(self).m_storage.template get<index_of<T>>();
    // }

    /* Get an optional wrapper for the value of the type at index `I`.  If that is not
    the active type, returns an empty optional instead. */
    template <size_t I, typename Self> requires (I < alternatives)
    [[nodiscard]] constexpr opt_val<I, Self> get_if(this Self&& self) noexcept(
        noexcept(opt_val<I, Self>(
            std::forward<Self>(self).m_storage.template get<I>()
        ))
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

    /// TODO: visit().  This probably requires moving the args<> class earlier in the
    /// dependency chain, so that I can easily compute cartesian products, etc.

private:
    using destructor_fn = void(*)(Union&);
    using copy_fn = storage<Ts...>(*)(const Union&);
    using move_fn = storage<Ts...>(*)(Union&&);
    using swap_fn = void(*)(Union&, Union&);

    template <typename... Us> requires (meta::destructible<Us> && ...)
    static constexpr std::array<destructor_fn, alternatives> destructors {
        +[](Union& self) {
            if constexpr (!meta::trivially_destructible<Ts>) {
                std::destroy_at(&self.m_storage.template get<index_of<Ts>>());
            }
        }...
    };

    template <typename... Us> requires (meta::copyable<Us> && ...)
    static constexpr std::array<copy_fn, alternatives> copy_constructors {
        +[](const Union& other) {
            return storage<Ts...>{other.m_storage.template get<index_of<Ts>>()};
        }...
    };

    template <typename... Us> requires (meta::movable<Us> && ...)
    static constexpr std::array<move_fn, alternatives> move_constructors {
        +[](Union&& other) {
            return storage<Ts...>{std::move(other).m_storage.template get<index_of<Ts>>()};
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
            +[](Union& self, Union& other) {
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

    template <typename... Us> requires (swappable<Us> && ...)
    static constexpr std::array<
        std::array<swap_fn, alternatives>,
        alternatives
    > swap_operators {
        pairwise_swap<Us>()...
    };

    static constexpr bool default_constructible = meta::not_void<default_type>;

    template <typename T>
    static constexpr bool bind_member = meta::not_void<conversion_type<T>>;

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
    finally implicit conversions (preferring the first, least qualified match).  If no
    such type exists, the conversion constructor is disabled. */
    template <typename T> requires (bind_member<T>)
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
        m_storage(move_constructors<Ts...>[index()](std::move(other)))
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

    /// TODO: all operators, forwarded using vtables and returning monadic unions
    /// for the types that support those operations.  It's possible that these
    /// narrowing conversions could return Expected<Union<Ts...>, TypeError>, which
    /// would itself be a monad.

};


template <meta::not_void T>
struct Optional {
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

        constexpr void reset() noexcept(noexcept(m_data = std::nullopt)) {
            m_data = std::nullopt;
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

        constexpr void reset() noexcept {
            m_data = nullptr;
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

    /* Clear the optional if it currently contains a value, returning it to the empty
    state. */
    constexpr void reset() noexcept(noexcept(m_storage.reset())) {
        m_storage.reset();
    }

    /* Returns `true` if the optional currently holds a value, or `false` if it is in
    the empty state. */
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return bool(m_storage);
    }

    /* Returns `true` if the optional currently holds a value, or `false` if it is in
    the empty state. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return bool(m_storage);
    }

    /* Access the stored value.  Throws a `BadOptionalAccess` exception if the optional
    is currently in the empty state when this method is called. */
    template <typename Self>
    [[nodiscard]] constexpr access<Self> value(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.has_value()) {
                throw BadOptionalAccess("Cannot access value of an empty Optional");
            }
        }
        return *std::forward<Self>(self).m_storage;
    }

    /* Dereference the optional to access the stored value.  Throws a
    `BadOptionalAccess` exception if the optional is currently in the empty state when
    this method is called. */
    template <typename Self>
    [[nodiscard]] constexpr access<Self> operator*(this Self&& self) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!self.has_value()) {
                throw BadOptionalAccess("Cannot dereference an empty Optional");
            }
        }
        return *std::forward<Self>(self).m_storage;
    }

    /* Dereference the optional to access a member of the stored value.  Throws a
    `BadOptionalAccess` exception if the optional is currently in the empty state when
    this method is called. */
    [[nodiscard]] constexpr pointer operator->() noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!has_value()) {
                throw BadOptionalAccess("Cannot dereference an empty Optional");
            }
        }
        return m_storage.operator->();
    }

    /* Dereference the optional to access a member of the stored value.  Throws a
    `BadOptionalAccess` exception if the optional is currently in the empty state when
    this method is called. */
    [[nodiscard]] constexpr const_pointer operator->() const noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (!has_value()) {
                throw BadOptionalAccess("Cannot dereference an empty Optional");
            }
        }
        return m_storage.operator->();
    }

    /* Access the stored value or return the default value if the optional is empty.
    Returns the common type between the wrapped type and the default value. */
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
    [[nodiscard]] constexpr and_then_t<Self, F> and_then(
        this Self&& self,
        F&& visit
    ) noexcept(
        meta::nothrow::invoke_returns<and_then_t<Self, F>, F, access<Self>>
    ) {
        if (!self.has_value()) {
            return {};
        }
        return std::forward<F>(visit)(*std::forward<Self>(self).m_storage);
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
    [[nodiscard]] constexpr or_else_t<Self, F> or_else(
        this Self&& self,
        F&& visit
    ) noexcept(
        meta::nothrow::invoke_returns<or_else_t<Self, F>, F> &&
        meta::nothrow::convertible_to<or_else_t<Self, F>, access<Self>>
    ) {
        if (self.has_value()) {
            return *std::forward<Self>(self).m_storage;
        }
        return std::forward<F>(visit)();
    }

    /// TODO: rest of monadic optional interface, including iterators, which for the
    /// empty state will simply produce an end() iterator.  Perhaps this can be
    /// implemented such that the iterator type holds a union of the begin and end
    /// iterators along with impl::sentinel{}, which would be used for the empty
    /// case.  This will be somewhat complex to implement, but would be a nice
    /// addition, and should be clearer than the `std::optional` interface, which
    /// treats the optional as a range with a single value.
};


template <typename T, meta::unexpected E>
struct Expected {
private:
    Union<T, E> m_data;

public:
    constexpr Expected()
        noexcept(meta::nothrow::default_constructible<T>)
        requires(meta::default_constructible<T>)
    :
        m_data()
    {}

    /// TODO: minimal interface, so that this can be the result of union.get()
};


template <meta::is_void T, meta::unexpected E>
struct Expected<T, E> {
private:
    bool m_ok;
    alignas(E) unsigned char m_data[sizeof(E)];

public:
};


/// TODO: possibly method implementations to get around circular dependencies between
/// these types.


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







inline void test() {
    static constexpr int i = 42;
    static constexpr Union<int, const int&, std::string> u = std::string("abc");
    static constexpr Union<int, const int&, std::string> u2 = u;
    constexpr Union u3 = std::move(Union{u});
    constexpr decltype(auto) x = u.get<2>();
    static_assert(u.index() == 2);
    static_assert(u.get<u.index()>() == "abc");
    static_assert(u2.get<2>() == "abc");
    static_assert(u3.get<2>() == "abc");
    static_assert(u.get<std::string>() == "abc");

    Union<int, const int&, std::string> u4 = std::string("abc");
    Union<int, const int&, std::string> u5 = u4;
    u4 = u;
    u4 = std::move(u5);


    static constexpr int value = 2;
    static constexpr Union<int, const int&, std::string> val = value;
    static_assert(val.index() == 1);
    static_assert(val.holds_alternative<const int&>());
    static_assert(*val.get_if<val.index()>() == 2);
    decltype(auto) v = val.get<val.index()>();
    decltype(auto) v2 = val.get_if<0>();



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


}


namespace std {

    /// TODO: variant_size and variant_alternative?

    template <bertrand::meta::Union T>
    struct hash<T> {
        static constexpr auto operator()(auto&& value) noexcept(
            /// TODO: figure out a proper noexcept guarantee
            false
        ) {
            /// TODO: use a vtable-based solution just like the other operators.
        }
    };

    template <bertrand::meta::Optional T>
    struct hash<T> {
        static constexpr auto operator()(auto&& value) noexcept(
            /// TODO: figure out a proper noexcept guarantee
            false
        ) {
            /// TODO: delegate to hash<Union<...>>?
        }
    };

    template <bertrand::meta::Expected T>
    struct hash<T> {
        static constexpr auto operator()(auto&& value) noexcept(
            /// TODO: figure out a proper noexcept guarantee
            false
        ) {
            /// TODO: delegate to hash<Union<...>>?
        }
    };

}


#endif  // BERTRAND_UNION_H
