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


/// TODO: basic skeleton for making exceptions constructible at compile time:

struct Foo {
private:
    struct compile_time_t {};
    struct run_time_t {};

    bool m_compiled;
    union storage {
        std::string_view compile_time;
        struct {
            int stack;  // cpptrace
            std::string message;
        } run_time;

        constexpr storage(compile_time_t, std::string_view msg) noexcept :
            compile_time(msg)
        {}

        template <typename T>
        constexpr storage(run_time_t, T&& msg) noexcept(
            noexcept(std::string_view(std::forward<T>(msg)))
        ) : run_time{
            .stack = 0,
            .message = [](auto&& msg) {
                if constexpr (meta::inherits<T, std::string>) {
                    return std::string(std::forward<T>(msg));
                } else {
                    return std::string(std::string_view(std::forward<T>(msg)));
                }
            }(std::forward<T>(msg))
        } {}

        constexpr ~storage() noexcept {}
    } m_storage;

public:

    /// TODO: no need to check for string type, since passing a std::string at compile
    /// time will cause a compilation error regardless.

    template <std::convertible_to<std::string_view> T>
    constexpr Foo(T&& msg) :
        m_compiled(std::is_constant_evaluated()),
        m_storage([](bool compiled, auto&& msg) {
            if (compiled) {
                return storage{compile_time_t{}, std::forward<decltype(msg)>(msg)};
            } else {
                return storage{run_time_t{}, std::forward<decltype(msg)>(msg)};
            }
        }(m_compiled, std::forward<T>(msg)))
    {}

    constexpr ~Foo() {
        if (m_compiled) {
            std::destroy_at(&m_storage.compile_time);
        } else {
            std::destroy_at(&m_storage.run_time);
        }
    }
};



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
    static constexpr size_t N = sizeof...(Ts);

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
    enum conversion_tag : uint8_t {
        EXACT = 0,
        INHERITS = 1,
        CONVERT = 2,
        VOID = 3,
    };
    template <typename T, typename result, conversion_tag tag, typename... Us>
    struct _conversion_type { using type = result; };  // base case: return intermediate result
    template <typename T, typename result, conversion_tag tag, typename U, typename... Us>
    struct _conversion_type<T, result, tag, U, Us...> {
        // no match at this index: advance U
        template <typename V>
        struct filter { using type = _conversion_type<T, result, tag, Us...>::type; };

        // if the type is an exact match, short-circuit and return it directly
        template <std::same_as<U> V>
        struct filter<V> { using type = U; };

        // if the types differ only in cvref qualifiers, prefer the least-qualified
        // match
        template <meta::is<U> V> requires (!std::same_as<V, U> && meta::convertible_to<V, U>)
        struct filter<V> {
            template <typename R>
            struct replace { using type = R; };

            // if the intermediate result is from a lower priority tag, or is more
            // qualified than U, replace it with U
            template <typename R> requires (tag > EXACT || meta::more_qualified_than<R, U>)
            struct replace<R> { using type = U; };

            // recur with updated result
            using type = _conversion_type<
                T,
                typename replace<result>::type,
                tag,
                Us...
            >::type;
        };

        // if the type is a base class of T, prefer the most proximal match
        template <meta::inherits<U> V> requires (!meta::is<V, U> && meta::convertible_to<V, U>)
        struct filter<V> {
            template <typename R>
            struct replace { using type = R; };

            // if the intermediate result is from a lower priority tag, or is less
            // derived or more qualified than U, replace it with U
            template <typename R>
                requires (
                    tag > INHERITS ||
                    (meta::inherits<U, R> && !meta::is<U, R>) ||
                    (meta::is<U, R> && meta::more_qualified_than<R, U>)
                )
            struct replace<R> { using type = U; };

            // recur with updated result
            using type = _conversion_type<
                T,
                typename replace<result>::type,
                tag,
                Us...
            >::type;
        };

        // if an implicit conversion exists from V to U, prefer the first match
        template <meta::convertible_to<U> V> requires (!meta::inherits<V, U>)
        struct filter<V> {
            template <typename R>
            struct replace { using type = R; };

            // if intermediate result is from a lower priority tag, or is more
            // qualified than U, replace it with U
            template <typename R>
                requires (
                    tag > CONVERT ||
                    (meta::is<U, R> && meta::more_qualified_than<R, U>)
                )
            struct replace<R> { using type = U; };

            // recur with updated result
            using type = _conversion_type<
                T,
                typename replace<result>::type,
                tag,
                Us...
            >::type;
        };

        // trigger the metafunction
        using type = filter<T>::type;
    };
    template <typename T>
    using conversion_type = _conversion_type<T, void, VOID, Ts...>::type;

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

public:

    /* Get the index of the currently-active type in the union. */
    [[nodiscard]] constexpr size_t index() const noexcept {
        return m_index;
    }

    /* Get the value of the type at index `I`.  Fails to compile if the index is out of
    range.  Otherwise, throws a `TypeError` if the indexed type is not the active
    member. */
    template <size_t I, typename Self> requires (I < N)
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
    using destructor = void(*)(Union&);
    using copy_constructor = storage<Ts...>(*)(const Union&);
    using move_constructor = storage<Ts...>(*)(Union&&);
    using swap_operator = void(*)(Union&, Union&);

    template <typename... Us> requires (meta::destructible<Us> && ...)
    static constexpr std::array<destructor, N> destructors {
        +[](Union& self) {
            if constexpr (!meta::trivially_destructible<Ts>) {
                std::destroy_at(
                    &self.m_storage.template get<meta::index_of<Ts, Ts...>>()
                );
            }
        }...
    };

    template <typename... Us> requires (meta::copyable<Us> && ...)
    static constexpr std::array<copy_constructor, N> copy_constructors {
        +[](const Union& other) {
            return storage<Ts...>{
                other.m_storage.template get<meta::index_of<Ts, Ts...>>()
            };
        }...
    };

    template <typename... Us> requires (meta::movable<Us> && ...)
    static constexpr std::array<move_constructor, N> move_constructors {
        +[](Union&& other) {
            return storage<Ts...>{
                std::move(other).m_storage.template get<meta::index_of<Ts, Ts...>>()
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
    static constexpr std::array<swap_operator, N> pairwise_swap() noexcept {
        constexpr size_t I = meta::index_of<T, Ts...>;
        return {
            +[](Union& self, Union& other) {
                constexpr size_t J = meta::index_of<Ts, Ts...>;

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
    static constexpr std::array<std::array<swap_operator, N>, N> swap_operators {
        pairwise_swap<Us>()...
    };

public:

    /* Default constructor finds the first type in `Ts...` that can be default
    constructed.  If no such type exists, the default constructor is disabled. */
    constexpr Union()
        noexcept(meta::nothrow::default_constructible<default_type>)
        requires(meta::not_void<default_type>)
    :
        m_index(meta::index_of<default_type, Ts...>),
        m_storage()
    {}

    /* Conversion constructor finds the most proximal type in `Ts...` that can be
    implicitly converted from the input type.  This will prefer exact matches or
    differences in qualifications (preferring the least qualified) first, then
    inheritance relationships (preferring the most derived and least qualified), and
    finally implicit conversions (preferring the first, least qualified match).  If no
    such type exists, the conversion constructor is disabled. */
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
    constexpr void swap(Union& other) noexcept((nothrow_swappable<Ts> && ...)) {
        if (this != &other) {
            swap_operators<Ts...>[index()][other.index()](*this, other);
            std::swap(m_index, other.m_index);
        }
    }
};


inline void test() {
    static constexpr Union<int, const int, std::string> u = "abc";
    static constexpr Union<int, const int, std::string> u2 = u;
    constexpr Union u3 = std::move(Union{u});
    constexpr decltype(auto) x = u.get<1>();
    static_assert(u.index() == 2);
    static_assert(u.get<u.index()>() == "abc");
    static_assert(u2.get<2>() == "abc");  // <- This is where the problem comes from
    static_assert(u3.get<2>() == "abc");
    static_assert(u.get<std::string>() == "abc");

    Union<int, const int, std::string> u4 = "abc";
    Union<int, const int, std::string> u5 = u4;
    u4 = u;
    u4 = std::move(u5);


    static constexpr int value = 2;
    static constexpr Union<int&, const int&, std::string> val = value;
    static_assert(val.index() == 1);
    static_assert(val.get<val.index()>() == 2);
    decltype(auto) v = val.get<val.index()>();
}


}


#endif  // BERTRAND_UNION_H
