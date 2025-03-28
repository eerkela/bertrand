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


/// TODO: greatly expand this


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
        struct filter<V> { using type = V; };

        using type = filter<T>::type;
    };
    template <typename T>
    using conversion_type = _conversion_type<T, void, Ts...>::type;

    template <typename... Us>
    union storage {
        constexpr storage() noexcept = default;
        template <typename V>
        constexpr storage(V&&) noexcept {};
        /// TODO: copy/move constructors
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

        /// TODO: copy/move constructors/assignment operators.

        constexpr ~storage() noexcept {}

        template <size_t I>
        constexpr void destroy() noexcept {
            if constexpr (I > 0) {
                rest.template destroy<I - 1>();
            } else if constexpr (!meta::trivially_destructible<U>) {
                curr.~U();
            }
        }

        template <size_t I>
        constexpr decltype(auto) get() const noexcept {
            if constexpr (I == 0) {
                return curr;
            } else {
                return rest.template get<I - 1>();
            }
        }
    };

    static constexpr std::array<void(*)(Union&), sizeof...(Ts)> destructors {
        +[](Union& self) {
            self.m_storage.template destroy<meta::index_of<Ts, Ts...>>();
        }...
    };

    size_t m_index;
    storage<Ts...> m_storage;

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
    constexpr Union(T&& value) noexcept(
        meta::nothrow::convertible_to<T, conversion_type<T>>
    ) :
        m_index(meta::index_of<conversion_type<T>, Ts...>),
        m_storage(std::forward<T>(value))
    {}

    /* Destructor destroys the currently-active type. */
    constexpr ~Union() noexcept((meta::nothrow::destructible<Ts> && ...)) {
        destructors[index()](*this);
    }

    /* Get the index of the currently-active type in the union. */
    [[nodiscard]] constexpr size_t index() const noexcept {
        return m_index;
    }

    /* Get the value of the type at index `I`.  Fails to compile if the index is out of
    range.  Otherwise, throws a `TypeError` if the indexed type is not the active
    member. */
    template <size_t I> requires (I < sizeof...(Ts))
    [[nodiscard]] constexpr decltype(auto) get() const noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (I != index()) {
                throw TypeError(
                    "Invalid union index: " + std::to_string(I) + " (active is " +
                    std::to_string(index()) + ")"
                );
            }
        }
        return m_storage.template get<I>();
    }

    /* Get the value for the templated type.  Fails to compile if the templated type
    is not a valid union member.  Otherwise, throws a `TypeError` if the templated type
    is not the active member. */
    template <typename T> requires ((std::same_as<T, Ts> || ...))
    [[nodiscard]] constexpr decltype(auto) get() const noexcept(!DEBUG) {
        constexpr size_t idx = meta::index_of<T, Ts...>;
        if constexpr (DEBUG) {
            if (idx != index()) {
                throw TypeError(
                    /// TODO: print the demangled type name
                    "Invalid union index: " + std::to_string(idx) + " (active is " +
                    std::to_string(index()) + ")"
                );
            }
        }
        return m_storage.template get<idx>();
    }

};


inline void test() {
    constexpr Union<int, std::string> u = "abc";
    static_assert(u.index() == 1);
    static_assert(u.get<u.index()>() == "abc");
    static_assert(u.get<std::string>() == "abc");
}


}


#endif  // BERTRAND_UNION_H
