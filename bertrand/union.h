#ifndef BERTRAND_UNION_H
#define BERTRAND_UNION_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {

namespace impl {
    struct union_tag {};


}


namespace meta {

    template <typename T>
    concept Union = inherits<T, impl::union_tag>;


}


/* A type-safe union similar to `std::variant`, but usable entirely at compile time,
and supporting a wider forwarding operator interface. */
template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        (meta::not_void<Ts> && ...) &&
        meta::types_are_unique<Ts...>
    )
struct Union {
private:
    static constexpr size_t n = sizeof...(Ts);

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

    template <typename T, typename conv, typename... Us>
    struct _conversion_type { using type = conv; };
    template <typename T, typename conv, typename U, typename... Us>
    struct _conversion_type<T, conv, U, Us...> {
        template <typename V>
        struct filter { using type = _conversion_type<T, conv, Us...>::type; };
        template <meta::convertible_to<U> V> requires (!meta::inherits<V, U>)
        struct filter<V> {
            using type = _conversion_type<
                T,
                std::conditional_t<meta::is_void<conv>, U, conv>,
                Us...
            >::type;
        };
        template <meta::inherits<U> V>
        struct filter<V> { using type = U; };

        using type = filter<T>::type;
    };
    template <typename T>
    using conversion_type = _conversion_type<T, void, Ts...>::type;

    /// TODO: note that unions cannot store references, so I would need to promote
    /// them to pointers.

    template <typename... Us>
    union storage {
        constexpr storage() noexcept = default;
        template <typename V>
        constexpr storage(V&&) noexcept {};
        constexpr ~storage() noexcept {};
    };
    template <typename U, typename... Us>
    union storage<U, Us...> {
        U curr;
        storage<Us...> rest;

        constexpr storage()
            noexcept(meta::nothrow::default_constructible<default_type>)
            requires(std::same_as<U, default_type>)
        :
            curr()
        {}

        constexpr storage()
            noexcept(meta::nothrow::default_constructible<default_type>)
            requires(!std::same_as<U, default_type>)
        :
            rest()
        {}

        template <typename T> requires(std::same_as<U, conversion_type<T>>)
        constexpr storage(T&& value) :
            curr(std::forward<T>(value))
        {}

        template <typename T> requires (!std::same_as<U, conversion_type<T>>)
        constexpr storage(T&& value) :
            rest(std::forward<T>(value))
        {}

        constexpr ~storage() noexcept {}

        /// TODO: swap() is always done outside the union using the 2D vtable

        template <size_t I, size_t J = I>
        constexpr void swap(Union& other)
            /// TODO: factor lack of swap logic into noexcept qualifier
            noexcept((meta::nothrow::swappable<Ts> && ...))
        {
            if constexpr (J == 0) {
                // if the underlying type is swappable, use that implementation
                if constexpr (meta::swappable<U>) {
                    using std::swap;
                    swap(curr, other.template get<I>());

                // otherwise, we have to move into a temporary and then move back
                // with strong exception safety
                } else {
                    U temp = std::move(curr);

                    // if move assignment operators are available, use them
                    if constexpr (meta::move_assignable<U>) {
                        try {
                            curr = std::move(other).template get<I>();
                        } catch (...) {
                            curr = std::move(temp);
                            throw;
                        }
                        try {
                            other.template get<I>() = std::move(temp);
                        } catch (...) {
                            other.template get<I>() = std::move(curr);
                            curr = std::move(temp);
                            throw;
                        }

                    // otherwise, explicitly destroy and reconstruct
                    } else {
                        if constexpr (!meta::trivially_destructible<U>) {
                            curr.~U();
                        }
                        try {
                            std::construct_at(
                                &curr,
                                std::move(other).template get<I>()
                            );
                        } catch (...) {
                            std::construct_at(&curr, std::move(temp));
                            throw;
                        }
                        if constexpr (!meta::trivially_destructible<U>) {
                            other.template get<I>().~U();
                        }
                        try {

                        } catch (...) {
                            
                        }
                    }
                }
            } else {
                rest.template swap<I, J - 1>(other);
            }
        }

        template <size_t I>
        constexpr decltype(auto) get(this auto&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<decltype(self)>(self).curr);
            } else {
                return (std::forward<decltype(self)>(self).rest.template get<I - 1>());
            }
        }
    };

    size_t m_index;
    storage<Ts...> m_storage;

public:

    /* Get the index of the currently-active type in the union. */
    [[nodiscard]] constexpr size_t index() const noexcept {
        return m_index;
    }

    /* Get the value of the type at index `I`.  Fails to compile if the index is out of
    range.  Otherwise, throws a `TypeError` if the indexed type is not the active
    member. */
    template <size_t I, typename Self> requires (I < n)
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept(!DEBUG) {
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
    template <typename T, typename Self> requires ((std::same_as<T, Ts> || ...))
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept(!DEBUG) {
        constexpr size_t idx = meta::index_of<T, Ts...>;
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

private:
    using copy_constructor = storage<Ts...>(*)(const Union&);
    using copy_assignment = void(*)(Union&, const Union&);
    using move_constructor = storage<Ts...>(*)(Union&&);
    using move_assignment = void(*)(Union&, Union&&);
    using swap_operator = void(*)(Union&, Union&);
    using destructor = void(*)(Union&);

    static constexpr std::array<destructor, n> destructors {
        +[](Union& self) {
            if constexpr (!meta::trivially_destructible<Ts>) {
                std::destroy_at(
                    &self.m_storage.template get<meta::index_of<Ts, Ts...>>()
                );
            }
        }...
    };

    static constexpr std::array<copy_constructor, n> copy_constructors {
        +[](const Union& other) {
            return storage<Ts...>{
                other.m_storage.template get<meta::index_of<Ts, Ts...>>()
            };
        }...
    };

    static constexpr std::array<move_constructor, n> move_constructors {
        +[](Union&& other) {
            return storage<Ts...>{
                std::move(other).m_storage.template get<meta::index_of<Ts, Ts...>>()
            };
        }...
    };

    template <typename T>
    static constexpr std::array<swap_operator, n> pairwise_swap() {
        constexpr size_t I = meta::index_of<T, Ts...>;
        return {
            +[](Union& self, Union& other) {
                constexpr size_t J = meta::index_of<Ts, Ts...>;

                /// TODO: check for member swap vs ADL swap?

                // // if the underlying types are identical, we may be able to take
                // // advantage of optimized ADL swap() implementations.
                // if constexpr (std::same_as<T, Ts> && meta::swappable<T>) {
                //     /// TODO: implement using ADL swap

                // // if no swap() method is found for identical types, then we can
                // // possibly use move assignment operators
                // } else if constexpr (std::same_as<T, Ts>) {
                //     /// TODO: implement using move assignment operators

                // // otherwise we have to move construct into a temporary, which might
                // // not have the same index
                // } else {
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
                // }
            }...
        };
    }

    static constexpr std::array<std::array<swap_operator, n>, n> swap_operators {
        pairwise_swap<Ts>()...
    };

public:

    /* Default constructor finds the first type in Ts... that can be default
    constructed.  If no such type exists, the default constructor is disabled. */
    constexpr Union()
        noexcept(meta::nothrow::default_constructible<default_type>)
        requires(meta::not_void<default_type>)
    :
        m_index(meta::index_of<default_type, Ts...>),
        m_storage()
    {}

    /* Conversion constructor finds the first type in Ts... that the input can be
    implicitly converted to, preferring exact matches and inheritance relationships.
    If no such type exists, the conversion constructor is disabled. */
    template <typename T> requires (meta::not_void<conversion_type<T>>)
    constexpr Union(T&& value)
        noexcept(meta::nothrow::convertible_to<T, conversion_type<T>>)
    :
        m_index(meta::index_of<conversion_type<T>, Ts...>),
        m_storage(std::forward<T>(value))
    {}

    /* Copy constructor.  The resulting union will have the same index as the input
    union, and will be initialized by copy constructing the other stored type. */
    constexpr Union(const Union& other)
        noexcept(!DEBUG && (meta::nothrow::copyable<Ts> && ...))
        requires((meta::copyable<Ts> && ...))
    :
        m_index(other.index()),
        m_storage(copy_constructors[other.index()](other))
    {}

    /* Move constructor.  The resulting union will have the same index as the input
    union, and will be initialized by move constructing the other stored type. */
    constexpr Union(Union&& other)
        noexcept(!DEBUG && (meta::nothrow::movable<Ts> && ...))
        requires((meta::movable<Ts> && ...))
    :
        m_index(other.index()),
        m_storage(move_constructors[index()](std::move(other)))
    {}

    /// TODO: account for optimized swap() and assignment operators in the noexcept
    /// qualifier.  The requires clause should be ok as-is.

    /* Copy assignment operator.  Destroys the current value and then copy constructs
    the other stored type into the active index. */
    constexpr Union& operator=(const Union& other)
        noexcept(((meta::nothrow::destructible<Ts> && meta::nothrow::copyable<Ts>) && ...))
        requires(((meta::destructible<Ts> && meta::copyable<Ts>) && ...))
    {
        if (this != &other) {
            Union temp(other);
            swap_operators[index()][temp.index()](*this, temp);
        }
        return *this;
    }

    /* Move assignment operator.  Destroys the current value and then move constructs
    the other stored type into the active index. */
    constexpr Union& operator=(Union&& other)
        noexcept(((meta::nothrow::destructible<Ts> && meta::nothrow::movable<Ts>) && ...))
        requires(((meta::destructible<Ts> && meta::movable<Ts>) && ...))
    {
        if (this != &other) {
            Union temp(std::move(other));
            swap_operators[index()][temp.index()](*this, temp);
        }
        return *this;
    }

    /* Destructor destroys the currently-active type. */
    constexpr ~Union()
        noexcept((meta::nothrow::destructible<Ts> && ...))
        requires((meta::destructible<Ts> && ...))
    {
        destructors[index()](*this);
    }

    /* Swap the contents of two unions as efficiently as possible. */
    constexpr void swap(Union& other) {
        if (this != &other) {
            swap_operators[index()][other.index()](*this, other);
            std::swap(m_index, other.m_index);
        }
    }
};


inline void test() {
    static constexpr Union<int, std::string> u = "abc";
    static constexpr Union<int, std::string> u2 = u;
    constexpr Union u3 = std::move(Union{u});
    constexpr decltype(auto) x = u.get<1>();
    static_assert(u.index() == 1);
    static_assert(u.get<u.index()>() == "abc");
    static_assert(u2.get<1>() == "abc");  // <- This is where the problem comes from
    static_assert(u3.get<1>() == "abc");
    static_assert(u.get<std::string>() == "abc");

    Union<int, std::string> u4 = "abc";
    Union<int, std::string> u5 = u4;
    u4 = u;
    u4 = std::move(u5);
}


}


#endif  // BERTRAND_UNION_H
